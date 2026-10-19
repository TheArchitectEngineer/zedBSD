# zedBSD riscv64: the UEFI loader, BOOTRISCV64.EFI.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# LLVM writes no PE/COFF object for RISC-V, so the loader is linked as a
# position-independent ELF for address 0 whose relocations are only
# relative ones; a tool puts a PE header in front of its flat image, and the
# image's entry applies the relocations for wherever the firmware loaded it.
# The loader's C uses no floating point, as the kernel's does not.
RISCV64_EFI_DIR := $(BUILD)/uefi
RISCV64_EFI_CFLAGS := -std=c11 -march=rv64imac_zicsr_zifencei -mabi=lp64 \
	-mcmodel=medany -mno-relax -fPIC -fvisibility=hidden \
	-ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident \
	-ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror \
	-nostdlibinc -Iinclude -I.
RISCV64_EFI_C := bootloader/uefi/bootriscv64.c bootloader/uefi/elf64.c \
	bootloader/uefi/volume-discovery.c bootloader/uefi/zedbsd-config.c \
	bootloader/uefi/fdt-hartid.c
RISCV64_EFI_S := bootloader/uefi/riscv64/start.S bootloader/uefi/riscv64/transition.S
RISCV64_EFI_OBJS := $(patsubst bootloader/uefi/%.c,$(RISCV64_EFI_DIR)/%.o,$(RISCV64_EFI_C)) \
	$(patsubst bootloader/uefi/%.S,$(RISCV64_EFI_DIR)/%.o,$(RISCV64_EFI_S))

.PHONY: bootloader
bootloader: $(RISCV64_EFI_DIR)/BOOTRISCV64.EFI

$(RISCV64_EFI_DIR)/%.o: bootloader/uefi/%.c
	@mkdir -p $(dir $@)
	$(RISCV64_CC) $(RISCV64_EFI_CFLAGS) -MMD -MP -c $< -o $@

$(RISCV64_EFI_DIR)/riscv64/%.o: bootloader/uefi/riscv64/%.S
	@mkdir -p $(dir $@)
	$(RISCV64_CC) -march=rv64imac_zicsr_zifencei -mabi=lp64 -mno-relax -fPIC -c $< -o $@

$(RISCV64_EFI_DIR)/bootriscv64.elf: $(RISCV64_EFI_OBJS) bootloader/uefi/riscv64/loader.ld
	$(RISCV64_LD) -m elf64lriscv -shared -Bsymbolic --no-relax --gc-sections \
 -z norelro -z max-page-size=4096 --hash-style=sysv --no-undefined \
 --pack-dyn-relocs=none -e _start -T bootloader/uefi/riscv64/loader.ld \
 $(RISCV64_EFI_OBJS) -o $@

$(RISCV64_EFI_DIR)/bootriscv64.bin: $(RISCV64_EFI_DIR)/bootriscv64.elf
	$(RISCV64_OBJCOPY) -O binary $< $@

$(RISCV64_EFI_DIR)/BOOTRISCV64.EFI: $(RISCV64_EFI_DIR)/bootriscv64.elf \
	$(RISCV64_EFI_DIR)/bootriscv64.bin platform/riscv64/tools/make-bootriscv64.py \
	platform/riscv64/tools/check-bootriscv64.py
	$(PYTHON) platform/riscv64/tools/make-bootriscv64.py --force \
 --elf $(RISCV64_EFI_DIR)/bootriscv64.elf --binary $(RISCV64_EFI_DIR)/bootriscv64.bin $@
	$(PYTHON) platform/riscv64/tools/check-bootriscv64.py \
 --elf $(RISCV64_EFI_DIR)/bootriscv64.elf $@

-include $(RISCV64_EFI_OBJS:.o=.d)
