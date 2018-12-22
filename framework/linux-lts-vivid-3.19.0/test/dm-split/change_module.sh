#!/bin/bash
SCRIPT_PATH="`dirname $0`/"
rmmod dm-split.ko
insmod ${SCRIPT_PATH}../../drivers/md/dm-split.ko
