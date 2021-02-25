#!/bin/bash
#------ pjsub option --------#
#PJM -N "MPICH-HELLO" # jobname
#PJM -S		# output statistics
#PJM --spath "results/%n.%j.stat"
#PJM -o "results/%n.%j.out"
#PJM -e "results/%n.%j.err"
#
#	PJM -L "node=192"		# 4x3x16
#	PJM -L "node=120"		#
#	PJM -L "node=64" #OK
#	PJM -L "node=32" #OK 3sec (4190) 1025704
#	PJM -L "node=32"
#PJM -L "node=2"
#	PJM -L "node=256"
#	PJM --mpi "max-proc-per-node=2"
#	PJM --mpi "max-proc-per-node=4"
#PJM --mpi "max-proc-per-node=1"
#PJM -L "elapse=00:01:00"
#PJM -L "rscunit=rscunit_ft01,rscgrp=dvsys-mck6-4,jobenv=linux2"
#PJM -L proc-core=unlimited
#------- Program execution -------#

#module switch lang/tcsds-1.2.27b

MPIOPT="-of results/%n.%j.out -oferr results/%n.%j.err"
#export MPICH_HOME=$HOME/mpich-tofu-fc
#export MPICH_HOME=$HOME/mpich-tofu-fc
#export MPICH_HOME=$HOME/mpich-tofu-fc2
export MPICH_HOME=$HOME/mpich-tofu-fc3

export MPIR_CVAR_CH4_OFI_CAPABILITY_SETS_DEBUG=1
export MPICH_TOFU_SHOW_PARAMS=1

#export UTF_INFO=0x1
#export UTF_ARMA_COUNT=2		# defined in mpich.env 2021/02/15
#export UTF_DBGTIMER_INTERVAL=200
#export UTF_DBGTIMER_ACTION=1
#export UTF_DEBUG=0x102200 # DLEVEL_ERR | DLEVEL_WARN | DLEVEL_LOG2
#export UTF_DEBUG=0x600
#export FI_LOG_PROV=tofu
#export FI_LOG_LEVEL=Debug

export UTF_TRANSMODE=0		# Chained mode
export MPIR_CVAR_CH4_OFI_ENABLE_TAGGED=1

echo "******************"
echo $MPICH_HOME

#NP=128 # OK
#NP=1024
#NP=16
NP=2
#NP=64 # OK
$MPICH_HOME/bin/mpich_exec -n $NP $MPIOPT ../bin/hello -v
exit
#	-x FI_LOG_PROV=tofu \
#	-x MPICH_DBG=FILE \
#	-x MPICH_DBG_CLASS=COLL \
#	-x MPICH_DBG_LEVEL=TYPICAL \
#
#	-x PMIX_DEBUG=1 \
#	-x FI_LOG_LEVEL=Debug \
#
