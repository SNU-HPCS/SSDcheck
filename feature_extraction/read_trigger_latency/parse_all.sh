#!/bin/bash

RUN_TIME=10
THINK_TIME=400 # us

block_sizes="4k"

########################## testing function ##########################

function total_test {
    ssd_name=${1}
    latency_threshold=${2}
    gc_threshold=${3}
    #for write_num in 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32; do
    for write_num in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
	command_gen="./check_latency.py --io_log ../../results/${ssd_name}/latency_read_trigger/rt-lat-test_${write_num}/test-rw_${write_num}_clat.1.log -l ${latency_threshold} -g ${gc_threshold}"
#	echo $command_gen
	echo "For ${write_num}th index: "
	eval $command_gen 
#        echo ""
    done
}


########################## TEST ##########################
latency_threshold="$1"
gc_threshold="$2"

total_test "sdc" ${latency_threshold} ${gc_threshold}
echo "[LOG] Test is finished"
