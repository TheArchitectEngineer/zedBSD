# WS052 (ws052-p003): the host test of the ACPI side of S0 idle.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   make -C plan/ws052/tests     build build/ws052/host/aml-host-sleep (ASan/UBSan)
#
# The WS049 harness (plan/ws049/tests/aml-host.c) with acpi-sleep.c and
# host-sleep.c, which plays the WS052_SLEEP steps before the harness frees
# the namespace (the linker's --wrap=drv_acpi_reset).

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
OUT ?= $(REPO)/build/ws052/host
CC ?= cc
HARNESS := $(REPO)/plan/ws049/tests

SOURCES := $(sort $(wildcard $(REPO)/src/drivers/acpi/aml-*.c)) \
	$(REPO)/src/drivers/acpi/acpi-tables.c \
	$(REPO)/src/drivers/acpi/acpi-event.c \
	$(REPO)/src/drivers/acpi/acpi-ec.c \
	$(REPO)/src/drivers/acpi/acpi-text.c \
	$(REPO)/src/drivers/acpi/acpi-resource.c \
	$(REPO)/src/drivers/acpi/acpi-sleep.c \
	$(HARNESS)/aml-host.c \
	$(HARNESS)/aml-host-hardware.c \
	$(REPO)/plan/ws052/tests/host-sleep.c
HEADERS := $(wildcard $(REPO)/src/drivers/acpi/*.h) \
	$(REPO)/include/drivers/acpi/acpi.h \
	$(HARNESS)/aml-host-hardware.h

WARNINGS := -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-Wstrict-prototypes -Wmissing-prototypes -Wshadow
CPPFLAGS := -I$(REPO)/include -I$(REPO)/src -I$(HARNESS)
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer

.PHONY: all
all: $(OUT)/aml-host-sleep

$(OUT)/aml-host-sleep: $(SOURCES) $(HEADERS) $(lastword $(MAKEFILE_LIST))
	@mkdir -p $(OUT)
	$(CC) -std=gnu11 -g -O1 $(WARNINGS) $(SANITIZE) $(CPPFLAGS) \
		-Wl,--wrap=drv_acpi_reset -o $@ $(SOURCES)
