obj-m += qm2mod.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
CFLAGS_qm2mod.o := -DDEBUG

all:
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD clean