#!/bin/bash

RUN_TIME=5
THINK_TIME=5000 # us

iodepths="1"
#block_sizes="4k 8k 16k 32k"
block_sizes="4k 8k"

########################## parameters for various SSDs ##########################
#
# setup bitidx & bdev for each SSD
#
declare -A bitidx_1_idxs
declare -A bitidx_1_vals
declare -A bitidx_2_idxs
declare -A bitidx_2_vals
declare -A bdevs

# Example SSDA (size => 1tb) whose volume index is 14
#bitidx_1_idxs["ssda_1tb"]=14
#bitidx_1_vals["ssda_1tb"]=1
#bitidx_2_idxs["ssda_1tb"]=0
#bitidx_2_vals["ssda_1tb"]=0
#bdevs["sktssd_3tb"]="/dev/sd*"


########################## testing function ##########################
function do_test {
    bdev=${1}
    io_depth=${2}
    bs=${3}
    runtime=${4}
    ssd_name=${5}
    test_name=${6}

    bitidx_1_idx=${bitidx_1_idxs[${ssd_name}]}
    bitidx_1_val=${bitidx_1_vals[${ssd_name}]}
    bitidx_2_idx=${bitidx_2_idxs[${ssd_name}]}
    bitidx_2_val=${bitidx_2_vals[${ssd_name}]}

    test_type_message="[TEST:${test_name}, SSD:${ssd_name}] bdev:${bdev}, bit_mani: \
    (${bitidx_1_idx} => ${bitidx_1_val}, ${bitidx_2_idx} => ${bitidx_2_val}), \
    read_io_depth:${io_depth}, write_bs:${bs}, \
    run_time:${runtime}"
    echo ""
    echo $test_type_message

    #echo 3 > /proc/sys/vm/drop_caches
    #bench_name="${6}_iodepth-${2}_jobs-${3}_bs-${4}_runtime-${5}"
    bench_name="iodepth-${io_depth}_writebs-${bs}_runtime-${runtime}"

    rm -rf $bench_name
    mkdir "$bench_name"
    mkdir "$bench_name/rand-write"
    mkdir "$bench_name/rand-read"

    command="DEV_NAME=${bdev} RUN_TIME=${runtime} \
    BIT_IDX_1=${bitidx_1_idx} BIT_1_VALUE=${bitidx_1_val} \
    BIT_IDX_2=${bitidx_2_idx} BIT_2_VALUE=${bitidx_2_val} \
    WRITE_BS=${bs} WRITE_THINKTIME=${THINK_TIME} \
    READ_IO_DEPTH=${io_depth} \
    ../../benchmarks/fio/fio buffer_test.fio \
    --output-format=json --output=\"${bench_name}.json\""

    # print & execute command
    echo $command
    eval $command

    mv rand-write*.log "${bench_name}/rand-write"
    mv rand-read*.log "${bench_name}/rand-read"
    mv ${bench_name}.json "${bench_name}/"

    rm -rf ../../results/${ssd_name}/${test_name}/${bench_name}
    mv "${bench_name}" ../../results/${ssd_name}/${test_name}/

    sleep 1
}

function total_test {
    ssd_name=${1}
    bdev=${bdevs[${ssd_name}]}
    test_name=${2}

    for iodepth in ${iodepths}; do
        for bs in ${block_sizes}; do
            do_test $bdev $iodepth $bs $RUN_TIME $ssd_name $test_name
        done
    done
}


########################## TEST ##########################
#Example (for ssda)
#total_test "ssda_1tb" "write_buffer"
#echo "[LOG] Test is finished"
