# WS050 p002: the host test of the UCSI core and the Type-C layer, and their
# compilation as the amd64 kernel compiles (warning 0).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   make -C plan/ws050/tests            build and run build/ws050/host/ucsi-host (ASan/UBSan)
#   make -C plan/ws050/tests acpi       build and run build/ws050/host/ucsi-acpi-host over the
#                                       Latitude 5330's tables (ws050-p003)
#   make -C plan/ws050/tests kernel-check   compile ucsi.c and typec.c with the kernel's flags

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
OUT ?= $(REPO)/build/ws050/host
CC ?= cc

SOURCES := $(REPO)/src/drivers/typec/ucsi.c $(REPO)/src/drivers/typec/typec.c \
	$(REPO)/plan/ws050/tests/ucsi-host.c
HEADERS := $(wildcard $(REPO)/src/drivers/typec/*.h) $(REPO)/include/drivers/typec/typec.h

WARNINGS := -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-Wstrict-prototypes -Wmissing-prototypes -Wshadow
CPPFLAGS := -I$(REPO)/include -I$(REPO)/src
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer

.PHONY: all run acpi kernel-check
all: run acpi

run: $(OUT)/ucsi-host
	$(OUT)/ucsi-host

$(OUT)/ucsi-host: $(SOURCES) $(HEADERS) $(lastword $(MAKEFILE_LIST))
	@mkdir -p $(OUT)
	$(CC) -std=gnu11 -g -O1 $(WARNINGS) $(SANITIZE) $(CPPFLAGS) -o $@ $(SOURCES)

# The ACPI transport over the AML interpreter and the 5330's firmware (ws050-p003).
ACPI_SOURCES := $(sort $(wildcard $(REPO)/src/drivers/acpi/aml-*.c)) \
	$(REPO)/src/drivers/acpi/acpi-resource.c $(REPO)/src/drivers/acpi/acpi-event.c \
	$(REPO)/src/drivers/typec/ucsi-acpi.c $(REPO)/src/drivers/typec/ucsi.c \
	$(REPO)/src/drivers/typec/typec.c $(REPO)/plan/ws050/tests/ucsi-acpi-host.c
ACPI_HEADERS := $(HEADERS) $(wildcard $(REPO)/src/drivers/acpi/*.h) \
	$(REPO)/include/drivers/acpi/acpi.h $(REPO)/include/drivers/typec/ucsi-acpi.h
TABLES := $(REPO)/plan/ws049/tests/latitude5330

acpi: $(OUT)/ucsi-acpi-host
	$(OUT)/ucsi-acpi-host $(TABLES)

$(OUT)/ucsi-acpi-host: $(ACPI_SOURCES) $(ACPI_HEADERS) $(lastword $(MAKEFILE_LIST))
	@mkdir -p $(OUT)
	$(CC) -std=gnu11 -g -O1 $(WARNINGS) $(SANITIZE) $(CPPFLAGS) -o $@ $(ACPI_SOURCES)

# The core and the layer as the amd64 kernel compiles them (same target and
# flags as plan/ws049/tests' kernel-check).
KERNEL_CC := $(REPO)/build/llvm/bin/clang --target=x86_64-unknown-zedbsd
KERNEL_CFLAGS := -nostdlibinc -I$(REPO)/include -I$(REPO)/src -I$(REPO) \
	-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 \
	-m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only \
	-ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror \
	-Wframe-larger-than=8192 -fno-builtin -fstack-usage
KERNEL_OBJECTS := $(OUT)/kernel/ucsi.o $(OUT)/kernel/typec.o \
	$(OUT)/kernel/ucsi-acpi.o $(OUT)/kernel/typec-kern.o

kernel-check: $(KERNEL_OBJECTS)
	@sort -t'	' -k2 -n -r $(OUT)/kernel/*.su | head -5

$(OUT)/kernel/%.o: $(REPO)/src/drivers/typec/%.c $(HEADERS)
	@mkdir -p $(OUT)/kernel
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c $< -o $@
