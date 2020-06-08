lsmod | grep trigger
if [ $? -eq 0 ]; then
	dmsetup remove mydm
	rmmod dm_read_trigger
fi


echo -n "Loading device mapper for read triggering ssd.."
echo -e "\n"

insmod ./dm-read-trigger.ko
# echo $READ_DELAY
# echo $WRITE_DELAY


TARGET_SSD=${1}
WRITE_BUFFER_SIZE=${2}

SSD_SIZE=`blockdev --getsz $TARGET_SSD`
TARGET_SIZE=`expr $SSD_SIZE`
#echo $TARGET_SIZE



echo "0 $TARGET_SIZE dm-read-trigger $TARGET_SSD 4096 ${WRITE_BUFFER_SIZE} " | dmsetup create mydm 
