SHELL = /bin/bash


EXTRA_CFLAGS += -DEXPORT_SYMTAB

TARGET_MODULE :=dm-read-trigger
obj-m :=$(TARGET_MODULE).o


$(TARGET_MODULE)-objs := dm-nvram.o dm-bio.o dm-parse.o dm-target.o

export BUILDSYSTEM_DIR=/lib/modules/$(shell uname -r)/build
PWD :=$(shell pwd)

# EXTRA_CFLAGS := -Idrivers/md

all:
	make -C $(BUILDSYSTEM_DIR) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

clean:
	make -C $(BUILDSYSTEM_DIR) M=$(PWD) clean

install:
	make -C $(BUILDSYSTEM_DIR) M=$(PWD) modules_install



