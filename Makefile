# Makefile for Raspberry Pi Rotary Encoder Driver

obj-m += rotary_encoder.o

# Kernel source directory (adjust for your system)
KDIR ?= /lib/modules/$(shell uname -r)/build

# Default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Install target
install: all
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install
	sudo depmod -a

# Load module
load:
	sudo insmod rotary_encoder.ko

# Unload module
unload:
	sudo rmmod rotary_encoder

# Reload module
reload: unload load

# Show module info
info:
	modinfo rotary_encoder.ko

# Show kernel logs
logs:
	dmesg | tail -20

.PHONY: all clean install load unload reload info logs