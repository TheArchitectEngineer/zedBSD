# WS304 host tests (no guest is started).
#
#   make -f plan/ws304/tests/host-test.mk run [RPI5_DTB_DIR=...]
#
# brcmstb2712: the Broadcom STB driver on the BCM2712, against a model of
#   the Pi 5's pcie1 and the Pi 5's trees with pcie1 enabled.
# The hosts the driver starts live for as long as the kernel runs, so the
# leak check is off.
#
# rpi5-mip: the MIP module against the Pi 5's trees and a fake GIC.
# brcmstb-trace: the BCM2711 register trace of the driver before this WS
#   (the main commit 13c03a376 this WS started from, or BASE=<commit>) and now, which must be equal.

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
OUT ?= $(REPO)/build/ws304-host
CC ?= cc
DTC ?= dtc
PYTHON ?= python3
BASE ?= 13c03a376
RPI5_DTB_DIR ?= $(REPO)/build/sources/firmware/rpi5-boot/3d301dd924bcd758a4c8cb19fe8531031f033f43
C1_DTB := $(RPI5_DTB_DIR)/bcm2712-rpi-5-b.dtb
D0_DTB := $(RPI5_DTB_DIR)/bcm2712d0-rpi-5-b.dtb
TESTS := $(REPO)/plan/ws304/tests

CPPFLAGS := -D_POSIX_C_SOURCE=200809L -I$(REPO)/include -I$(REPO)/src -I$(REPO)
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

.PHONY: all run
all: run

run: $(OUT)/brcmstb2712-host-test $(OUT)/rpi5-mip-host-test \
	$(OUT)/enabled-c1.dtb $(OUT)/enabled-d0.dtb \
	$(OUT)/brcmstb-trace-base $(OUT)/brcmstb-trace-now
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/brcmstb2712-host-test $(C1_DTB) $(OUT)/enabled-c1.dtb $(OUT)/enabled-d0.dtb
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/rpi5-mip-host-test $(C1_DTB) $(D0_DTB)
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/brcmstb-trace-base > $(OUT)/brcmstb-trace-base.txt
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/brcmstb-trace-now > $(OUT)/brcmstb-trace-now.txt
	cmp $(OUT)/brcmstb-trace-base.txt $(OUT)/brcmstb-trace-now.txt
	@echo "brcmstb-trace: the BCM2711 sequence is unchanged ($$(wc -l < $(OUT)/brcmstb-trace-now.txt) lines)"

$(OUT)/enabled-%.dtb: $(TESTS)/enable-pcie1.py
	@mkdir -p $(dir $@)
	$(DTC) -q -I dtb -O dts $(if $(filter c1,$*),$(C1_DTB),$(D0_DTB)) | \
		$(PYTHON) -I $(TESTS)/enable-pcie1.py | \
		$(DTC) -q -I dts -O dtb -o $@

$(OUT)/brcmstb2712-host-test: $(TESTS)/brcmstb2712-host-test.c \
	$(REPO)/src/drivers/pci/pci-brcmstb.c $(REPO)/src/drivers/generic/fdt.c \
	$(REPO)/include/drivers/pci/pci-brcmstb.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/brcmstb2712-host-test.c \
		$(REPO)/src/drivers/pci/pci-brcmstb.c \
		$(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

$(OUT)/rpi5-mip-host-test: $(TESTS)/rpi5-mip-host-test.c \
	$(REPO)/src/drivers/platform/rpi5/rpi5-mip.c \
	$(REPO)/src/drivers/platform/rpi5/rpi5-mip.h $(REPO)/src/drivers/generic/fdt.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/rpi5-mip-host-test.c \
		$(REPO)/src/drivers/platform/rpi5/rpi5-mip.c \
		$(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

$(OUT)/pci-brcmstb-base.c: FORCE
	@mkdir -p $(dir $@)
	git -C $(REPO) show $(BASE):src/drivers/pci/pci-brcmstb.c > $@.tmp
	cmp -s $@.tmp $@ || cp $@.tmp $@

$(OUT)/brcmstb-trace-base: $(TESTS)/brcmstb-trace.c $(OUT)/pci-brcmstb-base.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/brcmstb-trace.c \
		$(OUT)/pci-brcmstb-base.c $(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

$(OUT)/brcmstb-trace-now: $(TESTS)/brcmstb-trace.c $(REPO)/src/drivers/pci/pci-brcmstb.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TESTS)/brcmstb-trace.c \
		$(REPO)/src/drivers/pci/pci-brcmstb.c $(REPO)/src/drivers/generic/fdt.c $(LDFLAGS) -o $@

.PHONY: FORCE
FORCE:
