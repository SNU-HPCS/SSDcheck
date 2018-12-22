#!/bin/bash
DEV_VOL2="/dev/sdk"
#Disk /dev/sdk: 128.0 GB, 128035676160 bytes
DEV_VOL4="/dev/sdl"
#Disk /dev/sdl: 256.1 GB, 256060514304 bytes

# create vol2
dmsetup create linear_vol2_0 --table "0 124969304 linear ${DEV_VOL2} 0"
dmsetup create linear_vol2_1 --table "0 124969304 linear ${DEV_VOL2} 124969304"

# create vol4
dmsetup create linear_vol4_0 --table "0 124931244 linear ${DEV_VOL4} 0"
dmsetup create linear_vol4_1 --table "0 124931244 linear ${DEV_VOL4} 124931244"
dmsetup create linear_vol4_2 --table "0 124931244 linear ${DEV_VOL4} 249862488"
dmsetup create linear_vol4_3 --table "0 124931244 linear ${DEV_VOL4} 374793732"
