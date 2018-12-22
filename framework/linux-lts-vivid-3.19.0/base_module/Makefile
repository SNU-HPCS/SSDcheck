obj-m += base_module_clean.o
obj-m += base_module.o
obj-m += prediction_binder.o

all:
	make -C ../ M=$(PWD) modules

clean:
	make -C ../ M=$(PWD) clean
