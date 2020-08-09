#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pmix.h>
#include <utofu.h>
#include <jtofu.h>
#include <pmix_fjext.h>
#include <limits.h>
#include "utf_conf.h"
#include "utf_externs.h"
#include "utf_errmacros.h"
#include "utf_debug.h"
#include "utf_tofu.h"

#define LIB_CALL(rc, funcall, lbl, evar, str) do { \
	rc = funcall;		      \
	if (rc != 0) {			\
		evar = str; goto lbl;	\
	}				\
} while(0);

/*
 * 31             24 23           16 15            8 7 6 5  2 1 0
 * +---------------+---------------+---------------+---+----+---+
 * |              x|              y|              z|  a|   b|  c|
 * +---------------+---------------+---------------+---+----+---+
 */
union tofu_coord {
    struct { uint32_t	c: 2, b: 4, a: 2, z: 8, y: 8, x: 8; };
    uint32_t	val;
};

struct utf_info utf_info;
utofu_vcq_id_t	*tab_vcqid;

static uint32_t
generate_hash_string(char *cp, int len)
{
    uint32_t	hval = 0;
    int	i;
    for (i = 0; i < len; i++) {
	hval += *cp;
    }
    return hval;
}


/*
 * utf_jtofuinit(int pmixclose)
 *	pmix library is initialized and then getinitializing pmix_myrank pmix_nprocs
 */
static int
utf_jtofuinit(int pmixclose)
{
    pmix_value_t	*pval;
    char *errstr;
    int	rc;

    fprintf(stderr, "%s: calling PMIX_Init\n", __func__); fflush(stderr);
    /* PMIx initialization */
    LIB_CALL(rc, PMIx_Init(utf_info.pmix_proc, NULL, 0),
	     err, errstr, "PMIx_Init");
    fprintf(stderr, "%s: nspace(%s)\n", __func__, utf_info.pmix_proc->nspace); fflush(stderr);
    utf_info.myrank = utf_info.pmix_proc->rank;
    {	/* PMIx info is created */
	int	flag = 1;
	PMIX_INFO_CREATE(utf_info.pmix_info, 1);
	PMIX_INFO_LOAD(utf_info.pmix_info, PMIX_COLLECT_DATA, &flag, PMIX_BOOL);
    }
    /* PMIx wildcard proc is created */
    memcpy(utf_info.pmix_wproc, utf_info.pmix_proc, sizeof(pmix_proc_t));
    utf_info.pmix_wproc->rank = PMIX_RANK_WILDCARD;
    /* Getting # of processes */
    LIB_CALL(rc, PMIx_Get(utf_info.pmix_wproc, PMIX_JOB_SIZE, NULL, 0, &pval),
	     err, errstr, "Cannot get PMIX_JOB_SIZE");
    utf_info.nprocs = pval->data.uint32;
    fprintf(stderr, "%s: rank(%d) nprocs(%d)\n", __func__, utf_info.myrank, utf_info.nprocs); fflush(stderr);

    /* Getting Fujitsu's rank pmap and initializing jtofu library */
    LIB_CALL(rc, PMIx_Get(utf_info.pmix_wproc, FJPMIX_RANKMAP, NULL, 0, &pval),
	     err, errstr, "Cannot get FJPMIX_RANKMAP");
    utf_info.jobid = generate_hash_string(utf_info.pmix_proc->nspace, PMIX_MAX_NSLEN);
    jtofu_initialize(utf_info.jobid, utf_info.myrank,  pval->data.ptr);

    if (pmixclose) {
	LIB_CALL(rc, PMIx_Finalize(NULL, 0),
		 err, errstr, "PMIx_Finalize");
    }
    return 0;
err:
    fprintf(stderr, "%s\n", errstr);
    fprintf(stdout, "%s\n", errstr);
    return -1;
}

static void
utf_vname_vcqid(struct tofu_vname *vnmp)
{
    utofu_vcq_id_t  vcqid;
    size_t	ncnt;

    UTOFU_CALL(1, utofu_construct_vcq_id,  (uint8_t*) &vnmp->xyzabc,
	       vnmp->tniq[0]>>4, vnmp->tniq[0]&0x0f, vnmp->cid, &vcqid);
    /* getting candidates of path coord, vnmp->pcoords */
    JTOFU_CALL(1, jtofu_query_onesided_paths, &vnmp->xyzabc, 5, vnmp->pcoords, &ncnt);
    vnmp->pent = ncnt;
    /* pathid is set using the first candidate of vnmp->pcoords */
    UTOFU_CALL(1, utofu_get_path_id, vcqid, vnmp->pcoords[0].a, &vnmp->pathid);
    /* The default pathid is also embeded into vcqid */
#if 0
    UTOFU_CALL(1, utofu_set_vcq_id_path, &vcqid, vnmp->pcoords[0].a);
#endif
    /* suggested by Hatanaka-san 2020/07/20 */
    UTOFU_CALL(1, utofu_set_vcq_id_path, &vcqid, 0);
    {
	union jtofu_path_coords pcoords;
	utofu_path_id_t     paid;
	pcoords.s.pa = vnmp->xyzabc.s.a; pcoords.s.pb = vnmp->xyzabc.s.b;
	pcoords.s.pc = vnmp->xyzabc.s.c;
	utofu_get_path_id(vcqid, pcoords.a, &paid);
	// utf_printf("%s: uc(%d) assumed pathid(0x%x) default pathid(0x%x)\n", __func__, uc, paid, vnmp->pathid);
    }
    vnmp->vcqid = vcqid;
}

static inline int
utf_peers_init()
{
    struct tofu_vname *vnmp;
    union jtofu_phys_coords pcoords;
    union jtofu_log_coords lcoords;
    size_t	sz;
    int		rank;
    int		ppn;	/* process per node */
    uint8_t	*pmarker;

    utf_jtofuinit(0);
    ppn = jtofu_query_max_proc_per_node();
    utf_printf("%s: ppn(%d) jobid(%x)\n", __func__, ppn, utf_info.jobid);
    /* node info */
    sz = sizeof(struct tofu_vname)*utf_info.nprocs;
    vnmp = utf_info.vname = utf_malloc(sz);
    memset(vnmp, 0, sz);
    /* vcqid is also copied */
    sz = sizeof(utofu_vcq_id_t)*utf_info.nprocs;
    tab_vcqid =  utf_malloc(sz);
    memset(tab_vcqid, 0, sz);
    /* fi addr */
    utf_info.myfiaddr = utf_malloc(sz);
    memset(utf_info.myfiaddr, 0, sz);
    sz = utf_info.nprocs*sizeof(uint8_t);
    /* */
    pmarker = utf_malloc(sz);
    memset(pmarker, UCHAR_MAX, sz);
    for (rank = 0; rank < utf_info.nprocs; rank++) {
	if(pmarker[rank] == UCHAR_MAX) {
	    utofu_tni_id_t	tni;
	    utofu_cq_id_t	cq;
	    int	i;
	    jtofu_rank_t	nd_ranks[48];
	    size_t		nranks;
	    /* xyzabc */
	    JTOFU_CALL(1, jtofu_query_phys_coords, utf_info.jobid,
		       rank, &pcoords);
	    JTOFU_CALL(1, jtofu_query_log_coords, utf_info.jobid,
		       rank, &lcoords);
	    /* rank info */
	    JTOFU_CALL(1, jtofu_query_ranks_from_phys_coords, utf_info.jobid,
		       &pcoords, ppn, nd_ranks, &nranks);
	    for (i = 0; i < nranks; i++) {
		jtofu_rank_t	this_rank;
		utf_info.mynrnk = i;	/* rank within node */
		/* tni & cqi */
		utf_tni_select(ppn, i, &tni, &cq);
		this_rank = nd_ranks[i];
		vnmp[this_rank].tniq[0] = ((tni << 4) & 0xf0) | (cq & 0x0f);
		vnmp[this_rank].vpid = rank;
		vnmp[this_rank].cid = CONF_TOFU_CMPID;
		memcpy(&vnmp[this_rank].xyzabc, &pcoords, sizeof(pcoords));
		memcpy(&vnmp[this_rank].xyz, &lcoords, sizeof(lcoords));
		utf_vname_vcqid(&vnmp[this_rank]);
		tab_vcqid[this_rank] = vnmp[this_rank].vcqid;
		vnmp[this_rank].v = 1;
		utf_info.myfiaddr[this_rank] = this_rank;
		pmarker[this_rank] = i;
	    }
	}
	/* show */
	utf_printf("\t<%d> %s, vcqid(%lx), tni(%d), cq(%d), cid(%d)\n",
		   rank, pcoords2string(pcoords, NULL, 0),
		   vnmp[rank].vcqid, vnmp[rank].tniq[0]>>4, vnmp[rank].tniq[0]&0x0f,
		   vnmp[rank].cid);
    }
    utf_free(pmarker);
    utf_info.myppn = ppn;
    utf_printf("%s: returns\n", __func__);
    return 1;
}

/*
 * fi_addr - pointer to tofu vcqhid array
 * npp - number of processes
 * ppnp - process per node
 * rankp - my rank
 */
struct tofu_vname *
utf_get_peers(uint64_t **fi_addr, int *npp, int *ppnp, int *rnkp)
{
    static int notfirst = 0;

    if (notfirst == 0) {
	notfirst = utf_peers_init();
    }
    if (notfirst == -1) {
	*fi_addr = NULL;
	*npp = 0; *ppnp = 0; *rnkp = -1;
	return NULL;
    }
    if (fi_addr) {
	if (*fi_addr) {
	    int	rank;
	    uint64_t	*addr = *fi_addr;
	    for (rank = 0; rank < utf_info.nprocs; rank++) {
		addr[rank] = (uint64_t) rank;
	    }
	} else {
	    *fi_addr = utf_info.myfiaddr;
	}
    }
    *npp = utf_info.nprocs;
    *ppnp = utf_info.myppn;
    *rnkp = utf_info.myrank;
    return utf_info.vname;
}

#define PMIX_TOFU_SHMEM	"TOFU_SHM"

void	*
utf_shm_init(size_t sz, char *mykey)
{
    int	rc;
    char	*errstr;
    int	lead_rank = (utf_info.myrank/utf_info.myppn)*utf_info.myppn;
    key_t	key;
    int		shmid;
    char	buf[128];
    void	*addr;

    if (utf_info.myrank == lead_rank) {
	pmix_value_t	pv;
	strcpy(buf, mykey);
	key = ftok(buf, 1);
	shmid = shmget(key, sz, IPC_CREAT | 0666);
	if (shmid < 0) { perror("error:"); exit(-1); }
	addr = shmat(shmid, NULL, 0);
	/* expose SHMEM_KEY_VAL */
	pv.type = PMIX_STRING;
	pv.data.string = buf;
	PMIx_Put(PMIX_LOCAL, PMIX_TOFU_SHMEM, &pv);
	LIB_CALL(rc, PMIx_Commit(), err, errstr, "PMIx_Commit");
    } else {
	pmix_proc_t		pmix_tproc[1];
	pmix_value_t	*pv;
	char	*errstr;
	int	rc;
	
	memcpy(pmix_tproc, utf_info.pmix_proc, sizeof(pmix_proc_t));
	pmix_tproc->rank = lead_rank; // PMIX_RANK_LOCAL_NODE does not work
	do {
	    rc = PMIx_Get(pmix_tproc, PMIX_TOFU_SHMEM, NULL, 0, &pv);
	    usleep(1000);
	} while (rc == PMIX_ERR_NOT_FOUND);
	if (rc != PMIX_SUCCESS) {
	    errstr = "PMIx_Get";
	    goto err;
	}
	key = ftok(pv->data.string, 1);
	shmid = shmget(key, sz, 0);
	addr = shmat(shmid, NULL, 0);
    }
    return addr;
err:
    fprintf(stderr, "%s: rc(%d)\n", errstr, rc);
    return NULL;
}

int
utf_shm_finalize(void *addr)
{
    return shmdt(addr);
}


void
utf_fence()
{
    int	rc;
    char *errstr;

    utf_printf("%s: begin\n", __func__);
    LIB_CALL(rc, PMIx_Fence(utf_info.pmix_wproc, 1, utf_info.pmix_info, 1),
	     err, errstr, "PMIx_Fence");
    utf_printf("%s: end\n", __func__);
    return;
err:
    fprintf(stderr, "%s\n", errstr);
}

void
utf_procmap_finalize()
{
    PMIx_Finalize(NULL, 0);
}

