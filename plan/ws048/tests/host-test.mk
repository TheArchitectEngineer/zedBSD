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

CPPFLAGS := -D_POSIX_C_SOURCE=200809L -I$(REPO)/include -I$(REPO)/src -I$(REPO)
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

.PHONY: all run
all: run

run: $(OUT)/fdt-host-test $(OUT)/brcmstb-host-test $(OUT)/firmware-host-test \
	$(OUT)/dma-host-test $(OUT)/dma-uncached-host-test $(OUT)/disabled.dtb
	$(OUT)/dma-host-test
	$(OUT)/dma-uncached-host-test
	$(OUT)/fdt-host-test $(DTB) $(OUT)/disabled.dtb
	$(OUT)/brcmstb-host-test $(DTB) $(OUT)/disabled.dtb
	$(OUT)/firmware-host-test $(DTB)

$(OUT)/firmware-host-test: $(TESTS)/firmware-host-test.c \
	$(REPO)/src/drivers/platform/rpi4/rpi4-firmware.c \
	$(REPO)/src/drivers/platform/rpi4/rpi4-firmware.h \
	$(REPO)/src/drivers/generic/fdt.c $(REPO)/include/drivers/generic/fdt.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/firmware-host-test.c \
		$(REPO)/src/drivers/platform/rpi4/rpi4-firmware.c \
		$(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

$(OUT)/brcmstb-host-test: $(TESTS)/brcmstb-host-test.c \
	$(REPO)/src/drivers/pci/pci-brcmstb.c $(REPO)/src/drivers/generic/fdt.c \
	$(REPO)/include/drivers/pci/pci-brcmstb.h $(REPO)/include/drivers/generic/fdt.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/brcmstb-host-test.c \
		$(REPO)/src/drivers/pci/pci-brcmstb.c \
		$(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

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

$(OUT)/dma-host-test: $(TESTS)/dma-host-test.c $(REPO)/src/drivers/generic/dma.c \
	$(REPO)/include/drivers/generic/dma.h $(REPO)/include/kern/pmem.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/dma-host-test.c \
		$(REPO)/src/drivers/generic/dma.c $(LDFLAGS) -o $@

$(OUT)/dma-uncached-host-test: $(TESTS)/dma-host-test.c $(REPO)/src/drivers/generic/dma.c \
	$(REPO)/include/drivers/generic/dma.h $(REPO)/include/kern/pmem.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DWS048_UNCACHED $(CFLAGS) $(TESTS)/dma-host-test.c \
		$(REPO)/src/drivers/generic/dma.c $(LDFLAGS) -o $@
