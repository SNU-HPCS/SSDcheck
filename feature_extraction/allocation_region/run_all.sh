#!/bin/bash
my_dir="$(dirname "$0")"

########################## parameters for various SSDs ##########################
# setup bitidx & bdev for each SSD
declare -A bitidxs_start
declare -A bitidxs_end
bitidx_values="0 1"

# Example SSDA (size => 1tb)
#bitidxs_start["ssda_1tb"]=12
#bitidxs_end["ssda_1tb"]=41
#bdevs["ssda_1tb"]="/dev/sd*"

function do_test {
    bdev=${1}
    bitidx=${2}
    bitidx_value=${3}
    ssd_name=${4}
    capacity=${5}
    output_file=${6}


    # do experiment
    command="DEV_NAME=${bdev} BIT_IDX_1=${bitidx} BIT_1_VALUE=${bitidx_value} RUN_TIME=${RUN_TIME} \
    ../../tools/fio/fio bit_fix.fio \
    --output-format=json --output=\"${output_file}\" --eta=never"

    echo $command
    eval $command
}

function start_test {
    ssd_name=${1}
    capacity=${2}
    parm_name="${1}_${2}"

    bdev=${bdevs[${parm_name}]}

    bitidx_start=${bitidxs_start[${parm_name}]}
    bitidx_end=${bitidxs_end[${parm_name}]}

    # init result directory
    for bitidx in $(seq ${bitidx_start} ${bitidx_end}); do
        for bitidx_value in $bitidx_values; do
            bench_name="${ssd_name}_${capacity}_bitidx-${bitidx}_value-${bitidx_value}_runtime-${RUN_TIME}"
            rm -rf ../../results/${ssd_name}/${capacity}/${TEST_NAME}/$bench_name
            mkdir -p ../../results/${ssd_name}/${capacity}/${TEST_NAME}/$bench_name
        done
    done

    # do experiment
    for iter in $(seq "1" "$ITER_NUM"); do
        for bitidx in $(seq ${bitidx_start} ${bitidx_end}); do
            for bitidx_value in $bitidx_values; do
                bench_name="${ssd_name}_${capacity}_bitidx-${bitidx}_value-${bitidx_value}_runtime-${RUN_TIME}"
                output_file="${bench_name}_iter-${iter}.json"

                do_test $bdev $bitidx $bitidx_value $ssd_name $capacity $output_file

                mv $output_file ../../results/${ssd_name}/${capacity}/${TEST_NAME}/${bench_name}/
            done
        done
    done
}


########################## TEST ##########################
# fixed parameters
TEST_NAME="allocation_region"

if [ $# -ne 2 ]; then
    echo "Usage $0 [runtime: e.g., 5s)] [iter_num]"
    exit 1
fi

# parameter checking
RUN_TIME="$1"
ITER_NUM="$2"

# Example (for ssd_a)
#start_test "ssda" "1tb" &

# wait for subprocesses
FAIL=0
for job in `jobs -p`
do
    echo $job
    wait $job || let "FAIL+=1"
done

if [ "$FAIL" == "0" ]; then
    echo "[Success]"
else
    echo "[FAIL] ($FAIL)"
fi
