#!/bin/bash
SCRIPT_PATH="`dirname $0`/"
cd ${SCRIPT_PATH}../../; make M=$(pwd)/drivers/md modules
