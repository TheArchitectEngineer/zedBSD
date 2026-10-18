/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Host stand-in for the riscv64 HAL's spin.h: one thread, so a lock is a check. */

#ifndef KERN_HAL_RISCV64_SPIN_H
#define KERN_HAL_RISCV64_SPIN_H

#include <hal/hal.h>
#include "asm.h"

static __inline bool riscv64_spin_lock(volatile unsigned *lock) { bool e = riscv64_irq_save(); if (*lock) HAL_FATAL("lock held"); *lock = 1; return e; }
static __inline void riscv64_spin_unlock(volatile unsigned *lock, bool enabled) { *lock = 0; riscv64_irq_restore(enabled); }

#endif
