# zedBSD riscv64 toolchain.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The project's LLVM knows the zedBSD target on RISC-V and defines __ZEDBSD__
# for it, so the kernel is built as on the other ports.
RISCV64_CC ?= $(ZEDBSD_TARGET_LLVM_BIN)/clang --target=riscv64-unknown-zedbsd
RISCV64_LD ?= $(ZEDBSD_TARGET_LLVM_BIN)/ld.lld
RISCV64_OBJCOPY ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objcopy
RISCV64_NM ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-nm

.PHONY: toolchain
toolchain:
	@command -v $(firstword $(RISCV64_CC)) >/dev/null
	@command -v $(RISCV64_LD) >/dev/null
	@echo "RISCV64 toolchain: $(RISCV64_CC), $(RISCV64_LD)"
