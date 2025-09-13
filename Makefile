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
	sudo mkdir -p /etc
	sudo cp etc/rotary_encoder.conf /etc/rotary_encoder.conf.example
	@echo "Configuration example installed to /etc/rotary_encoder.conf.example"
	@echo "Copy to /etc/rotary_encoder.conf and edit as needed"

# Install configuration file
install-config:
	sudo mkdir -p /etc
	sudo cp etc/rotary_encoder.conf /etc/rotary_encoder.conf.example
	@if [ ! -f /etc/rotary_encoder.conf ]; then \
		sudo cp etc/rotary_encoder.conf /etc/rotary_encoder.conf; \
		echo "Default configuration installed to /etc/rotary_encoder.conf"; \
	else \
		echo "Configuration file /etc/rotary_encoder.conf already exists, not overwriting"; \
	fi

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

.PHONY: all clean install install-config load unload reload info logs