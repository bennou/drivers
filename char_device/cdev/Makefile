ifneq ($(KERNELRELEASE),)
	obj-m := cdev.o
else
	DIR := /usr/src/linux-headers-$(shell uname -r)
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(DIR) M=$(PWD) modules
clean:
	rm *.o
	rm *.ko
	rm *mod*
	rm Module.*
endif
