#!/bin/bash

RUN_TIME=10
THINK_TIME=400 # us

block_sizes="4k"

########################## testing function ##########################
function do_test {

    ssd_name=${1}
    test_name=${2}
    write_num=${3}
	date=$(date +%Y%m%d)

    bench_name="rt-lat-test_${write_num}"
	trace_name="finaltest.log"	
#    trace_name="trace/trace_${write_num}"

    rm -rf $bench_name
    echo $bench_name
    echo ""
    mkdir "$bench_name"_"$date"

    command="sudo DEV_NAME=/dev/${ssd_name} LOG_NAME=${trace_name} WRITE_NUM=${write_num}\
    ../../benchmarks/fio/fio test.fio \
    --output-format=json --output=\"${bench_name}.json\""

    # print & execute command
    echo $command
    eval $command

    mv ${bench_name}.json "$bench_name"_"$date/"
    mv test-rw*.log "$bench_name"_"$date/"
    mkdir ../../results/${ssd_name}
    mkdir ../../results/${ssd_name}/${test_name}
    rm -rf ../../results/${ssd_name}/${test_name}/"$bench_name"_"$date"
    mv "$bench_name"_"$date" ../../results/${ssd_name}/${test_name}/

#    sleep 60
}

function total_test {
    ssd_name=${1}
    test_name=${2}
    
	for write_num in 1 #$( seq 1 15 )
	do
		command_gen="./latency_test ${write_num} 1000 trace_${write_num}"
		echo $command_gen
		echo ""
		eval $command_gen 
		mv trace_${write_num} trace/
        do_test $ssd_name $test_name $write_num
    done
}

function fio_test {
	ssd_name=$1
	test_name=$2
	
	for write_num in $( seq 17 32 )
	do
		do_test $ssd_name $test_name $write_num
	done
}


########################## TEST ##########################
total_test "sdb" "latency_read_trigger"
#total_test "dm-0" "latency_read_trigger"
#fio_test "sdc" "latency_read_trigger"
echo "[LOG] Test is finished"
