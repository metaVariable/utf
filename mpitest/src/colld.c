/*
 * This is a test for the dev1 branch
 *
 *	-V: 0: no info, 1: verify only,
 *	    2: display timing with min and max using MPI_Reduce,
 *	    3: display timing on rank 0 only without verify
 */
#include <mpi.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "testlib.h"
#ifndef FJMPI
#include <utf.h>
#endif
#include <utf_tsc.h>

#define LEN_INIT	1
double	*sendbuf;
double	*recvbuf;

#ifdef FJMPI
const char	*marker = "coll-fjmpi";
#else
const char	*marker = "coll-mpich";
#endif

#define MYPRINT	if (myrank == 0)

#define SEND_RECV do {						\
    MPI_Send(buf, 0, MPI_BYTE, receiver, 0, MPI_COMM_WORLD);	\
    MPI_Recv(buf, 0, MPI_BYTE, receiver, 0, MPI_COMM_WORLD, &stat);	\
} while(0);

#define RECV_SEND do {						\
    MPI_Recv(buf, 0, MPI_BYTE, sender, 0, MPI_COMM_WORLD, &stat);	\
    MPI_Send(buf, 0, MPI_BYTE, sender, 0, MPI_COMM_WORLD);		\
} while(0);

#define CLK2USEC(tm)	((double)(tm) / ((double)hz/(double)1000000))
void
show(const char *name, int iter, uint64_t st, uint64_t et)
{
    uint64_t	tm, hz;
    hz = tick_helz(0);
    tm = et - st;
    if (Vflag == 2) {
	uint64_t	tm_max, tm_min;
	MPI_Reduce(&tm, &tm_max, 1, MPI_LONG_LONG, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&tm, &tm_min, 1, MPI_LONG_LONG, MPI_MIN, 0, MPI_COMM_WORLD);
	MYPRINT {
	    printf("@%s, %8d, %13s, %6d, %10.4f, %10.4f\n",
		   marker, nprocs, name, iter, (float)CLK2USEC(tm_max)/(float)iter,
		   (float)CLK2USEC(tm_min)/(float)iter);
	}
    } else if (Vflag == 3) {
	MYPRINT {
	    printf("@%s, %d, %s, %d, %8.3f\n",
		   marker, nprocs, name, iter, (float)CLK2USEC(tm)/(float)iter);
	}
    }
    fflush(stdout);
}

int
verify_reduce(double *sendbuf, double *recvbuf, int length)
{
    int errs = 0;
    int	j, k;
    for (k = 0; k < length; k++) {
	double	val = 0;
	for (j = 0; j < nprocs; j++) {
	    val += (double) (j + k + 1);
	}
	if (recvbuf[k] != val) {
	    printf("recvbuf[%d] = %e, expect = %e\n", k, recvbuf[k], val); fflush(stdout);
	    errs++;
	}
    }
    return errs;
}


int
main(int argc, char** argv)
{
    int	tsz;
    int	errs = 0, toterrs = 0;
    size_t	i, sz;
    uint64_t	st, et;

    length = LEN_INIT;
    iteration = 1;
    sflag = 0x10;	// Alltoall in default
    test_init(argc, argv);

#ifndef FJMPI
    if (vflag) {
	utf_vname_show(stdout);
    }
#endif
    MPI_Type_size(MPI_DOUBLE, &tsz);
    sz = length*nprocs*tsz;
    sendbuf = malloc(sz);
    recvbuf = malloc(sz);
    MYPRINT {
	printf("sendbuf=%p recvbuf=%p\n"
	       "MPI_DOUBLE SIZE: %d\n"
	       "length(%ld) byte(%ld) nprocs(%d) iteration(%d) sflag(0x%x)\n",
	       sendbuf, recvbuf, tsz, length, sz, nprocs, iteration, sflag); fflush(stdout);
    }
    if (sendbuf == NULL || recvbuf == NULL) {
	MYPRINT {
	    printf("Cannot allocate buffers: sz=%ldMiB * 2\n", (uint64_t)(((double)sz)/(1024.0*1024.0)));
	    fflush(stdout);
	}
	exit(-1);
    }
    for (i = 0; i < length*nprocs; i++) {
	sendbuf[i] = (double) (myrank + i + 1);
	recvbuf[i] = (double) -1;
    }
    MYPRINT { VERBOSE("Start MPI_Barier %ldth\n", i); }
    if (sflag & 0x1) {
	int	sender, receiver;
	char	buf[128];
	MPI_Status stat;

	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    MPI_Barrier(MPI_COMM_WORLD);
	}
	et = tick_time(); show("MPI_Barrier", iteration, st, et);

#ifdef FJMPI
	sender = 0; receiver = 1;
#else
	{
	    int iprocs;
	    utf_intra_nprocs(&iprocs);
	    MYPRINT { printf("iprocs=%d\n", iprocs); }
	    sender = 0;
	    receiver = iprocs;	/* next node */
	}
#endif
	if (myrank == sender) {
	    /* dry run */
	    SEND_RECV;
	    st = tick_time();
	    for (i = 0; i < iteration; i++) {
		SEND_RECV;
	    }
	    et = tick_time();
	} else if (myrank == receiver) {
	    /* dry run */
	    RECV_SEND;
	    st = tick_time();
	    for (i = 0; i < iteration; i++) {
		RECV_SEND;
	    }
	    et = tick_time();
	} else {
	    st = et = 0;
	}
	show("PINGPONG", iteration, st, et);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    MYPRINT { VERBOSE("Start MPI_Reduce %ldth\n", i); }
    if (sflag & 0x2) {
	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    MPI_Reduce(sendbuf, recvbuf, length, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	    if (Vflag == 1) { /* verify and reset */
		int	j;
		if (myrank == 0) {
		    errs += verify_reduce(sendbuf, recvbuf, length);
		}
		/* reset value */
		for (j = 0; j < length*nprocs; j++) {
		    sendbuf[j] = myrank + j + 1;
		    recvbuf[j] = -1;
		}
	    }
	}
	if (errs) {
	    printf("[%d] MPI_Reduce: Errors = %d\n", myrank, errs); fflush(stdout);
	} else {
	    MYPRINT { printf("MPI_Reduce: No errors\n"); fflush(stdout); }
	}
	et = tick_time(); show("MPI_Reduce", iteration, st, et);
    }
    toterrs += errs; errs = 0;
    MYPRINT { VERBOSE("Start MPI_Allreduce %ldth\n", i); }
    if (sflag & 0x4) {
	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    // MPI_Allreduce(MPI_IN_PLACE, sendbuf, length, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	    MPI_Allreduce(sendbuf, recvbuf, length, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	    if (Vflag == 1) { /* verify and reset */
		int	j;
		errs += verify_reduce(sendbuf, recvbuf, length);
		/* reset value */
		for (j = 0; j < length*nprocs; j++) {
		    sendbuf[j] = myrank + j + 1;
		    recvbuf[j] = -1;
		}
	    }
	}
	if (errs) {
	    printf("[%d] MPI_Allreduce: Errors = %d\n", myrank, errs); fflush(stdout);
	} else {
	    MYPRINT { printf("MPI_Allreduce: No errors\n"); fflush(stdout); }
	}
	et = tick_time(); show("MPI_Allreduce", iteration, st, et);
    }
    toterrs += errs; errs = 0;
    MYPRINT { VERBOSE("Start MPI_Gather %ldth\n", i); }
    if (sflag & 0x8) {
	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    MPI_Gather(sendbuf, length, MPI_DOUBLE, recvbuf, length, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}
	et = tick_time(); show("MPI_Gather", iteration, st, et);
    }
    VERBOSE("Start of Alltoall %ldth\n", i);
    if (sflag & 0x10) {
	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    MPI_Alltoall(sendbuf, length, MPI_DOUBLE, recvbuf, length, MPI_DOUBLE, MPI_COMM_WORLD);
	}
	et = tick_time(); show("MPI_Alltoall", iteration, st, et);
    }
    MYPRINT { VERBOSE("Start MPI_Scatter %ldth\n", i); }
    if (sflag & 0x20) {
	st = tick_time();
	for (i = 0; i < iteration; i++) {
	    MPI_Scatter(sendbuf, length, MPI_DOUBLE, recvbuf, length, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}
	et = tick_time(); show("MPI_Scatter", iteration, st, et);
    }

    MPI_Finalize();
    MYPRINT {
	printf("RESULT(0x%x) coll: %s\n", sflag, toterrs == 0 ? "PASS" : "FAIL"); fflush(stdout);
    }
    return 0;
}
