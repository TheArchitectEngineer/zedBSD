# WS048 host tests (the guest is not started).
#
#   make -f plan/ws048/tests/host-test.mk run [DTB=...]
#
# DTB is the Raspberry Pi 4 device tree the firmware ships.  The disabled
# copy marks the PCIe node the way QEMU's raspi4b does.

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
OUT ?= $(REPO)/build/ws048-host
CC ?= cc
DTC ?= dtc
DTB ?= $(REPO)/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
TESTS := $(REPO)/plan/ws048/tests

CPPFLAGS := -I$(REPO)/include -I$(REPO)/src -I$(REPO)
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

.PHONY: all run
all: run

run: $(OUT)/fdt-host-test $(OUT)/disabled.dtb
	$(OUT)/fdt-host-test $(DTB) $(OUT)/disabled.dtb

$(OUT)/fdt-host-test: $(TESTS)/fdt-host-test.c $(REPO)/src/drivers/generic/fdt.c \
	$(REPO)/include/drivers/generic/fdt.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/fdt-host-test.c \
		$(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

$(OUT)/disabled.dtb: $(DTB)
	@mkdir -p $(dir $@)
	$(DTC) -q -I dtb -O dts $(DTB) | \
		sed '/compatible = "brcm,bcm2711-pcie";/a status = "disabled";' | \
		$(DTC) -q -I dts -O dtb -o $@
