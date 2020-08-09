#include "utf_list.h"

#pragma pack(1)
struct utf_msghdr { /* 20 Byte */
    uint32_t	src;	  /* 32 bit is enoguth for Fugaku */
    uint64_t	tag;	  /* libfabric requirement */
    union {
	struct {
	    uint64_t size:35,  /* 35 bit, 32GiB is enough for Fugaku */
		pyldsz:10, /* size of payload: up to 1KiB */
		rndz: 1,
		type: 2,   /* libfabric or utf message */
		flgs: 4,   /* libfabric */
		marker: 4,
		sidx: 8;
	};
	uint64_t hall;
    };
};

struct utf_fabmsghdr { /* 28 Byte */
    uint32_t	src;	  /* 32 bit is enoguth for Fugaku */
    uint64_t	tag;	  /* libfabric requirement */
    union {
	struct {
	    uint64_t size:35,  /* 35 bit, 32GiB is enough for Fugaku */
		pyldsz:10, /* size of payload */
		rndz: 1,
		type: 4,	/* libfabric or utf message */
		flgs: 4,
		marker: 2,
		sidx: 8;
	};
	uint64_t all;
    };
    uint64_t	data;	/* used for libfabric */
};

struct utf_vcqhdl_stadd {		/* 40B */
    size_t	nent;			/* */
    uint64_t	vcqhdl[MSG_NTNI];	/* utofu_vcq_hdl_t */
    uint64_t	stadd[MSG_NTNI];	/* utofu_stadd_t */
};

struct utf_vcqid_stadd {		/* 40B (8+8*2+8*2) */
    size_t	nent;			/* */
    uint64_t	vcqid[MSG_NTNI];	/* utofu_vcq_hdl_t */
    uint64_t	stadd[MSG_NTNI];	/* utofu_stadd_t */
};

struct utf_packet {
    struct utf_msghdr	hdr;
    uint32_t	marker;
    union {
	uint8_t		msgdata[MSG_PYLDSZ];
	struct utf_vcqid_stadd rndzdata;
    } pyld;
};

#define PKT_HDR(pkt) ((pkt)->hdr)
#define PKT_MSGSRC(pkt) ((pkt)->hdr.src)
#define PKT_MSGTAG(pkt) ((pkt)->hdr.tag)
#define PKT_MSGSZ(pkt) ((pkt)->hdr.size)
#define PKT_DATA(pkt) ((pkt)->pyld.msgdata)
#define PKT_PYLDSZ(pkt) ((pkt)->hdr.pyldsz)
#define PKT_RADDR(pkt)  ((pkt)->pyld.rndzdata)

struct utf_egr_sbuf {
    union {
	utfslist_entry_t	slst;
	struct utf_packet	pkt;
    };
};

#define SCNTR_NOROOM	-1
#define SCNTR_HEAD	0
#define SCNTR_TAIL	1
#define SCNTR_OK	1

union utf_chain_addr {
    struct {
	uint64_t rank:23,	/* = 8,388,608 > 7,962,624 */
		 sidx:23,	/* = 8,388,608 > 7,962,624 */
		 recvidx:18;	/* = 262,144 */
    };
    uint64_t	      rank_sidx;
};
#define RANK_ALL1	0x7fffff
#define IS_CHAIN_EMPTY(val)    ((val).rank == RANK_ALL1)

union utf_chain_rdy {
    struct {
	uint64_t	rdy:1,
		recvidx:63;
    };
    uint64_t	data;
};

/* receive management */
union utf_erv_head {
    struct {
	uint64_t cntr;
	union utf_chain_addr chntail;	/* tail address of request chain */
    };
    char	 pad[256];
};
struct utf_egr_rbuf {
    union utf_erv_head	head;
    struct utf_packet	rbuf[COM_RBUF_TOTSZ];	/* 50 MiB: COM_RBUF_SIZE*COM_PEERS */
};

/*
 * req->status
 */
enum utq_reqstatus {
    REQ_NONE	= 0,
    REQ_PRG_NORMAL,
    REQ_PRG_RECLAIM,
    REQ_DONE,
    REQ_OVERRUN,
    REQ_WAIT_RNDZ		/* waiting rendzvous */
};

/*
 * recv_ctr state
 */
enum {
    REQ_RECV_EXPECTED,
    REQ_RECV_UNEXPECTED,
    REQ_RECV_EXPECTED2,
    REQ_SND_BUFFERED_EAGER,
    REQ_SND_INPLACE_EAGER,
    REQ_SND_RENDEZOUS
};
enum rstate {
    R_FREE		= 0,
    R_NONE		= 1,
    R_HEAD		= 2,
    R_BODY		= 3,
    R_WAIT_RNDZ		= 4,
    R_DO_RNDZ		= 5,
    R_DO_READ		= 6,
    R_DO_WRITE		= 7,
    R_DONE		= 8
};

struct utf_msgreq {
    struct utf_msghdr hdr;	/* 28: message header */
    uint8_t	*buf;		/* 32: buffer address */
    uint64_t	rsize:35,	/* 40: received size */
		state: 8,	/* 40: utf-level  status */
		ustatus: 8,	/* 40: user-level status */
		rndz:1,		/* 40: set if rendezvous mode */
		type:3,		/* 40: EXPECTED or UNEXPECTED or SENDREQ */
		ptype:4,	/* 40: PKT_EAGER | PKT_RENDZ | PKT_WRITE | PKT_READ */
		fistate:5;	/* 40: fabric-level status */
    size_t	expsize;	/* 48: expected size in expected queue */
    utfslist_entry_t slst;	/* 56: list */
    void	(*notify)(struct utf_msgreq*);	/* 64: notifier */
    struct utf_recv_cntr *rcntr;	/* 72: point to utf_recv_cntr */
    struct utf_vcqid_stadd rgetsender; /* 112: rendezous: sender's stadd's and vcqid's */
    struct utf_vcqhdl_stadd bufinfo; /* 152: rendezous: receiver's stadd's and vcqid's  */
};

struct utf_msglst {
    utfslist_entry_t	slst;	/*  8 B */
    struct utf_msghdr	hdr;	/* 24 B */
    uint32_t		reqidx;	/* 28 B: index of utf_msgreq */
};

struct utf_recv_cntr {
    uint8_t	state;
    uint8_t	mypos;
    uint32_t	recvidx;
    struct utf_msgreq	*req;
    utofu_vcq_id_t	svcqid;
    uint64_t		flags;
    utfslist_entry_t	rget_slst;/* rendezous: list of rget progress */
};

/*****************************************************
 *	Sender Control
 *****************************************************/
enum {
    S_FREE		= 0,
    S_NONE		= 1,
    S_REQ_ROOM		= 2,
    S_HAS_ROOM		= 3,
    S_DO_EGR		= 4,
    S_DO_EGR_WAITCMPL	= 5,
    S_DONE_EGR		= 6,
    S_REQ_RDVR		= 7,
    S_RDVDONE		= 8,
    S_DONE		= 9,
    S_WAIT_BUFREADY	= 10
};

enum {
    SNDCNTR_NONE		   = 0,
    SNDCNTR_BUFFERED_EAGER_PIGBACK = 1,
    SNDCNTR_BUFFERED_EAGER	   = 2,
    SNDCNTR_INPLACE_EAGER	   = 3,
    SNDCNTR_RENDEZOUS		   = 4
};

/*
 * Event types for handling messages
 */
#define EVT_START	0
#define EVT_CONT	1
#define EVT_LCL		2
#define EVT_RMT_RGETDON	3	/* remote armw operation */
#define EVT_RMT_RECVRST	4	/* remote armw operation */
#define EVT_RMT_GET	5	/* remote get operation */
#define EVT_RMT_CHNRDY	6	/* ready in request chain mode */
#define EVT_RMT_CHNUPDT	7	/* next chain is set by remote */
#define EVT_END		8

struct utf_send_msginfo { /* msg info */
    struct utf_msghdr	msghdr;		/* message header     +28 = 28 Byte */
    uint32_t	cntrtype;
    struct utf_egr_sbuf	*sndbuf;	/* send data for eger  +8 = 40 Byte */
    utofu_stadd_t	sndstadd;	/* stadd of sndbuf     +8 = 48 Byte */
    void		*usrbuf;	/* stadd of user buf   +8 = 56 Byte */
    struct utf_vcqid_stadd rgetaddr;	/* for rendezvous     +40 = 96 Byte
					 * stadd and vcqid for rget by dest, expose it to dest */
    struct utf_msgreq	*mreq;		/* request struct      +8 =104 Byte */
};

/* used for remote operation */
#define SCNTR_RGETDONE_OFFST		0x0
#define SCNTR_RST_RECVRESET_OFFST	0x4
#define SCNTR_RST_RMARESET_OFFST	0x8
#define SCNTR_CHN_NEXT_OFFST		0x10
#define SCNTR_CHN_READY_OFFST		0x18
#define SCNTR_ADDR_CNTR_FIELD(sidx)	\
    (utf_sndctr_stadd + sizeof(struct utf_send_cntr)*(sidx))
#define SCNTR_IS_RGETDONE_OFFST(off)	((off) == SCNTR_RGETDONE_OFFST)
#define SCNTR_IS_RECVRESET_OFFST(off)	((off) == SCNTR_RST_RECVRESET_OFFST)
#define SCNTR_IS_CHN_NEXT_OFFST(off)	((off) == SCNTR_CHN_NEXT_OFFST + sizeof(uint64_t))
#define SCNTR_IS_CHN_RDY_OFFST(off)	((off) == SCNTR_CHN_READY_OFFST + sizeof(uint64_t))

#pragma pack(1)
struct utf_send_cntr {	/* 500 Byte */
    uint32_t		rgetdone:1,	/* 0: ready for resetting recv offset */
			ineager: 1,	/* position depends */
			rgetwait:2,	/* */
			state: 5,	/* upto 31 states */
			ostate: 5,	/* upto 31 states */
			mypos: 18;	/* */
    uint32_t		rcvreset: 1,	/* position depends */
			recvidx: 31;	/*  +4 =  8 Byte */
    uint32_t		rmareset: 1,	/* position depends. ready for reseting */
			rmawait: 1,	/* Wait for reseting */
			rmaoff: 30;	/*  */
    uint32_t		dst;		/* 12  = +4  Byte */
    union utf_chain_addr chn_next;	/* position depends. 16 = +8 24 Byte */
    union utf_chain_rdy	chn_ready;	/* position depends. 24 = +8 = 32 Byte */
    uint64_t		flags;		/*  32 = +8 Byte */
    utofu_vcq_id_t	svcqid;		/*  40 = +8 Byte */
    utofu_vcq_id_t	rvcqid;		/*  48 = +8 Byte */
    size_t		usize;		/*  56 = +8 user-level sent size */
    utfslist_t		smsginfo;	/*  64 = +8 Byte */
    uint16_t		micur;		/*  72 = +8 Byte */
    uint16_t		mient;		/*  80 = +4 Byte */
    struct utf_send_msginfo msginfo[COM_SCNTR_MINF_SZ];	/*  84 = +408 the first entry */
    utfslist_entry_t	slst;		/* 492 = + 8 Byte for free list */
					/* 500  */
};

/*****************************************************
 *	End of Sender Control
 *****************************************************/
extern utfslist_t	utf_explst;	/* expected message list */
extern utfslist_t	utf_uexplst;	/* unexpected message list */
extern utfslist_t	utf_msglst_freelst;

static inline void
utf_msglst_free(struct utf_msglst *msl)
{
    utfslist_insert(&utf_msglst_freelst, &msl->slst);
}

static inline int
utf_uexplst_match(uint32_t src, uint32_t tag, int peek)
{
    struct utf_msglst	*msl;
    uint32_t		idx;
    utfslist_entry_t	*cur, *prev;

    if (utfslist_isnull(&utf_uexplst)) {
	return -1;
    }
    if (src == -1 && tag == -1) {
	cur = utf_uexplst.head; prev = 0;
	msl = container_of(cur, struct utf_msglst, slst);
	goto find;
    } else if (src == -1 && tag != -1) {
	utfslist_foreach2(&utf_uexplst, cur, prev) {
	    msl = container_of(cur, struct utf_msglst, slst);
	    if (tag == msl->hdr.tag) {
		goto find;
	    } 
	}
    } else if (tag == -1) {
	utfslist_foreach2(&utf_uexplst, cur, prev) {
	    msl = container_of(cur, struct utf_msglst, slst);
	    if (src == msl->hdr.tag) {
		goto find;
	    } 
	}
    } else {
	utfslist_foreach2(&utf_uexplst, cur, prev) {
	    msl = container_of(cur, struct utf_msglst, slst);
	    if (src == msl->hdr.src && tag == msl->hdr.tag) {
		goto find;
	    } 
	}
    }
    return -1;
find:
    idx = msl->reqidx;
    if (peek == 0) {
	utfslist_remove2(&utf_uexplst, cur, prev);
	utf_msglst_free(msl);
    }
    return idx;
}

static inline int
utf_explst_match(uint32_t src, uint32_t tag, int flg)
{
    struct utf_msglst	*msl;
    utfslist_entry_t	*cur, *prev;
    uint32_t		idx;

    DEBUG(DLEVEL_PROTOCOL) {
	utf_printf("explst_match: utf_explst(%p) src(%d) tag(%d)\n",
		 &utf_explst, src, tag);
    }
    if (utfslist_isnull(&utf_explst)) {
	return -1;
    }
    utfslist_foreach2(&utf_explst, cur, prev) {
	msl = container_of(cur, struct utf_msglst, slst);
	uint32_t exp_src = msl->hdr.src;
	uint32_t exp_tag = msl->hdr.tag;
	DEBUG(DLEVEL_PROTOCOL) {
	    utf_printf("\t exp_src(%d) exp_tag(%x)\n", exp_src, exp_tag);
	}
	if (exp_src == -1 && exp_tag != -1) {
	    goto find;
	} else if (exp_src == -1 && exp_tag == tag) {
	    goto find;
	} else if (exp_tag == -1 && exp_src == src) {
	    goto find;
	} else if (exp_src == src && exp_tag == tag) {
	    goto find;
	}
    }
    return -1;
find:
    idx = msl->reqidx;
    if (flg) {
	utfslist_remove2(&utf_explst, cur, prev);
	utf_msglst_free(msl);
    }
    DEBUG(DLEVEL_PROTOCOL) {
	utf_printf("\t return idx(%d)\n", idx);
    }
    return idx;
}
