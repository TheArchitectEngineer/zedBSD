/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Host stand-in for the riscv64 HAL's asm.h: the interrupt state is a variable. */

#ifndef KERN_HAL_RISCV64_ASM_H
#define KERN_HAL_RISCV64_ASM_H

#include <hal/types.h>
#include "defs.h"

extern int host_sie;

static __inline bool riscv64_irq_save(void) { bool was = host_sie != 0; host_sie = 0; return was; }
static __inline void riscv64_irq_restore(bool enabled) { if (enabled) host_sie = 1; }
static __inline void riscv64_set_sstatus(uint64_t bits) { if (bits & RISCV64_SSTATUS_SIE) host_sie = 1; }
static __inline void riscv64_clear_sstatus(uint64_t bits) { if (bits & RISCV64_SSTATUS_SIE) host_sie = 0; }
void riscv64_halt_forever(void) __attribute__((noreturn));

#endif
