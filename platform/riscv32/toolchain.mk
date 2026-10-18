# zedBSD riscv32 toolchain.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The project's LLVM has no zedBSD target for 32-bit RISC-V, so the kernel is
# built for the generic ELF target with the zedBSD target's define (the
# kernel needs no C library and no user program is built for this port).
RISCV32_CC ?= $(ZEDBSD_TARGET_LLVM_BIN)/clang --target=riscv32-unknown-elf -D__ZEDBSD__=1
RISCV32_LD ?= $(ZEDBSD_TARGET_LLVM_BIN)/ld.lld
RISCV32_OBJCOPY ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objcopy
RISCV32_NM ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-nm

.PHONY: toolchain
toolchain:
	@command -v $(firstword $(RISCV32_CC)) >/dev/null
	@command -v $(RISCV32_LD) >/dev/null
	@echo "RISCV32 toolchain: $(RISCV32_CC), $(RISCV32_LD)"
