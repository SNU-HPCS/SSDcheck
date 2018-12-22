#!/bin/bash
DEV_VOL2="/dev/sdk"
#Disk /dev/sdk: 128.0 GB, 128035676160 bytes
DEV_VOL4="/dev/sdl"
#Disk /dev/sdl: 256.1 GB, 256060514304 bytes

# create vol2
dmsetup create vol2_0 --table "0 124969304 split ${DEV_VOL2} 0 17:0 0:0"
dmsetup create vol2_1 --table "0 124969304 split ${DEV_VOL2} 0 17:1 0:0"
                                          
# create vol4                             
dmsetup create vol4_0 --table "0 124931244 split ${DEV_VOL4} 0 17:0 18:0"
dmsetup create vol4_1 --table "0 124931244 split ${DEV_VOL4} 0 17:1 18:0"
dmsetup create vol4_2 --table "0 124931244 split ${DEV_VOL4} 0 17:0 18:1"
dmsetup create vol4_3 --table "0 124931244 split ${DEV_VOL4} 0 17:1 18:1"
