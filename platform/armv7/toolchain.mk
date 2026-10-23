# zedBSD armv7 toolchain.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The kernel needs no C library, so it is built for the generic EABI target
# with the zedBSD target's define; the user programs, built later, use
# armv7-unknown-zedbsd-eabihf.
ARMV7_CC ?= $(ZEDBSD_TARGET_LLVM_BIN)/clang --target=armv7-unknown-none-eabi -D__ZEDBSD__=1
ARMV7_LD ?= $(ZEDBSD_TARGET_LLVM_BIN)/ld.lld
ARMV7_OBJCOPY ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objcopy
ARMV7_NM ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-nm

.PHONY: toolchain
toolchain:
	@command -v $(firstword $(ARMV7_CC)) >/dev/null
	@command -v $(ARMV7_LD) >/dev/null
	@echo "ARMV7 toolchain: $(ARMV7_CC), $(ARMV7_LD)"
