obj-m += qm2mod.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
#CFLAGS_qm2mod.o := -DDEBUG

all: modules
# all: modules qm2detect

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD modules

# qm2detect: qm2detect.c
# 	$(CC) -Wall -O2 -o $@ $<

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD clean
#	rm -f qm2detect
