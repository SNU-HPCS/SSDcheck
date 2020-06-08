#!/bin/bash

###### Testing Function #######

date=$(date +%Y_%m_%d_%H)

function do_test {
    device_name=$1
    test_name=$2    
	bdev=$3

	mkdir ${device_name}_${test_name}_${date}

    for i in 0s 1s 5s 10s 30s 1m 5m ; do
        #sleep 120;
        depth=32
        block_size=4k
        dir_name="./${device_name}_${test_name}_${i}"

		command="sudo DEV_NAME=${bdev} IO_DEPTH=${depth} WRITE_BS=${block_size} THINK_TIME=${i} \
				BW_LOG_FNAME=${dir_name} ../../benchmarks/fio/fio idleGC.fio"

		echo $command
		eval $command

		sleep 120;
    done

	mv *.log ./${device_name}_${test_name}_${date}
}

echo "[LOG] Test Started"
echo "[TEST] $date"
#do_test "Tammuz_240GB_rand" "idleGC" "/dev/sdb"
do_test "Kingston" "idleGC" "/dev/sdd"
echo "[LOG] Test Finished"
