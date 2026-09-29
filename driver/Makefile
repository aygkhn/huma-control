# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
MODNAME = qc71_laptop
MODVER = $(shell sed -n "s/^PACKAGE_VERSION=//p" dkms.conf)

obj-m += $(MODNAME).o

# version comes from dkms.conf; shown by modinfo and /sys/module/qc71_laptop/version
ccflags-y += -DQC71_VERSION='"$(shell sed -n 's/^PACKAGE_VERSION=//p' $(src)/dkms.conf)"'

# alphabetically sorted
$(MODNAME)-y += ec.o \
		features.o \
		main.o \
		misc.o \
		models.o \
		pdev.o \
		events.o \
		uniwill_ext.o \

$(MODNAME)-$(CONFIG_DEBUG_FS)     += debugfs.o
$(MODNAME)-$(CONFIG_ACPI_BATTERY) += battery.o
$(MODNAME)-$(CONFIG_LEDS_CLASS)   += led_lightbar.o led_keyboard.o
$(MODNAME)-$(CONFIG_HWMON)        += hwmon.o hwmon_fan.o hwmon_pwm.o fan.o

KVER = $(shell uname -r)
KDIR = /lib/modules/$(KVER)/build
MDIR = /usr/src/$(MODNAME)-$(MODVER)

all:
	make -C $(KDIR) M=$(CURDIR) modules

clean:
	make -C $(KDIR) M=$(CURDIR) clean

dkmsinstall:
	mkdir -p $(MDIR)
	cp Makefile dkms.conf $(wildcard *.c) $(wildcard *.h) $(MDIR)/.
	dkms add $(MODNAME)/$(MODVER)
	dkms build $(MODNAME)/$(MODVER)
	dkms install $(MODNAME)/$(MODVER)

dkmsuninstall:
	-rmmod $(MODNAME)
	-dkms uninstall $(MODNAME)/$(MODVER)
	-dkms remove $(MODNAME)/$(MODVER) --all
	rm -rf $(MDIR)
