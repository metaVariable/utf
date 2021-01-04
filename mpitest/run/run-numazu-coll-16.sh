#!/bin/bash
#------ pjsub option --------#
#PJM -N "MPICH-COLL16" # jobname
#PJM -S		# output statistics
#PJM --spath "results/coll-16/%n.%j.stat"
#PJM -o "results/coll-16/%n.%j.out"
#PJM -e "results/coll-16/%n.%j.err"
#
#PJM -L "node=4:noncont"
#PJM --mpi "max-proc-per-node=4"
#PJM -L "elapse=00:01:20"
#PJM -L "rscunit=rscunit_ft02,rscgrp=dvsys-mck2_and_spack2,jobenv=linux"
#PJM -L proc-core=unlimited
#------- Program execution -------#
MPIOPT="-of results/coll-16/%n.%j.out -oferr results/coll-16/%n.%j.err"
# max RMA 8388608
###export MPIR_CVAR_REDUCE_SHORT_MSG_SIZE=100000


#
#   coll -s  0x1: Barrier, 0x2: Reduce, 0x4: Allreduce, 0x8: Gather, 0x10: Alltoall, 0x20: Scatter
#	     0x40: Gatherv
#
export MPIR_CVAR_CH4_OFI_CAPABILITY_SETS_DEBUG=1
export MPICH_TOFU_SHOW_PARAMS=1
export UTF_INFO=0x1
#export UTF_DEBUG=0xffffff
#export FI_LOG_PROV=tofu
#export FI_LOG_LEVEL=Debug
#export UTF_DBGTIMER_INTERVAL=10
#export UTF_DBGTIMER_ACTION=1
export UTF_DEBUG=0x200

NP=16
ITER=10000 # DEADLOCK
#ITER=500 # DEADLOCK, but OK for NONTAGGED
#ITER=200 # PASS 3 sec
#ITER=250 # PASS 2 sec
#ITER=290 # PASS 2 sec
#ITER=300 # DEADLOCK

VRYFY="-V 1"
VERB ="-v"
# for LEN in 8192 65536 524288 # size in double 14sec
#for LEN in 8 128 256 512 1024 2048 # size in double for 100time reduce 6 sec
#for LEN in 8
#for LEN in 1024
for LEN in 256 512 1024 2048
do
    echo;echo
    mpich_exec -n $NP $MPIOPT ../src/colld -l $LEN -i $ITER -s 0x2 $VRYFY $VERB # Reduce
##    mpich_exec -n $NP $MPIOPT ../src/colld -l $LEN -i $ITER -s 0x40	$VRYFY # Gatherv
    unset MPIR_CVAR_CH4_OFI_CAPABILITY_SETS_DEBUG
    unset MPICH_TOFU_SHOW_PARAMS
    unset UTF_INFO
done

#mpich_exec -n $NP $MPIOPT ../src/colld -l $LEN -i $ITER -s 0x10	$VRYFY # Alltoall

exit
