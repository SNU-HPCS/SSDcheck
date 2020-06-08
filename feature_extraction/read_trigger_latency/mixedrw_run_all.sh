#!/bin/bash

RUN_TIME=10
THINK_TIME=400 # us

block_sizes="4k"

########################## testing function ##########################
function do_test {

    ssd_name=${1}
    test_name=${2}
    read_rat=${3}
	write_rat=${4}
	buffer_size=${5}
	date=$(date +%Y%m%d)

    bench_name="mixedrw-rt-lat-test_${read_rat}"

    rm -rf $bench_name
    echo $bench_name
    echo ""
    mkdir "$bench_name"_"$date"

	command="sudo DEV_NAME=/dev/${ssd_name} READ_RATIO=${read_rat}\
    WRITE_RATIO=${write_rat} ../../benchmarks/fio/fio mix_readwrite.fio \
    --output-format=json --output=\"${bench_name}.json\""

    # print & execute command
    echo $command
    eval $command

    mv ${bench_name}.json "$bench_name"_"$date/"
    mv mixed-rw*.log "$bench_name"_"$date/"
    mkdir ../../results/${ssd_name}-${buffer_size}
    mkdir ../../results/${ssd_name}-${buffer_size}/${test_name}
    rm -rf ../../results/${ssd_name}-${buffer_size}/${test_name}/"$bench_name"_"$date"
    mv "$bench_name"_"$date" ../../results/${ssd_name}-${buffer_size}/${test_name}/

    sleep 2
}

########################## TEST ##########################
read_ratio=${1}
buf_size=${2}
write_ratio=`expr 100 - ${1}`
echo ${write_ratio}
#do_test "dm-0" "latency_read_trigger" ${read_ratio} ${write_ratio} ${buf_size}
#do_test "dm-0" "latency_read_trigger" 50 50 ${buf_size}
do_test "dm-0" "latency_read_trigger" 70 30 ${buf_size}
#do_test "dm-0" "latency_read_trigger" 90 10 ${buf_size}


#do_test "sdb" "latency_read_trigger" ${read_ratio} ${write_ratio} ${buf_size}
#fio_test "sdc" "latency_read_trigger"
echo "[LOG] Test is finished"
