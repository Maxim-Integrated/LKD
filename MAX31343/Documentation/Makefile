MODULES := rtc-max31343.o

PWD=$(shell pwd)
VER=$(shell uname -r)
KERNEL_BUILD=/lib/modules/$(VER)/build
TEST_VER=4.19.59-v7+
CUSTOM_KERNEL_MOD=/lib/modules/$(VER)/kernel/drivers/rtc

obj-m := $(MODULES)

#path of the arm compiled kernel
ROOTDIR := /lib/modules/$(shell uname -r)/build

MAKEARCH := $(MAKE)

all: 
	$(MAKEARCH) -C $(ROOTDIR) M=${shell pwd} modules
	sudo cp rtc-max31343.ko $(CUSTOM_KERNEL_MOD)
build:
	$(MAKEARCH) -C $(ROOTDIR) M=${shell pwd} modules
	./checkpatch.pl -f --no-tree rtc-max31343.c
clean:
	$(MAKEARCH) -C $(ROOTDIR) M=${shell pwd} clean
install:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) INSTALL_MOD_PATH=$(INSTALL_ROOT) modules_install
	depmod -A
dtbs:
	dtc -I dts -O dtb -q -o i2c-rtc.dtbo i2c-rtc-overlay.dts
	sudo cp i2c-rtc.dtbo /boot/overlays
