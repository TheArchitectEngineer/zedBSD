/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS trap frame and the trap handling's entry points.
 */

#ifndef KERN_HAL_MIPS_TRAP_H
#define KERN_HAL_MIPS_TRAP_H

#include <hal/types.h>
#include "asm.h"

/*
 * The general register numbers the trap handling reads or writes in a
 * frame: v0 and a3, which carry a system call's result and its error
 * mark, and the stack pointer.
 */
#define MIPS_REGISTER_V0		2
#define MIPS_REGISTER_A0		4
#define MIPS_REGISTER_A3		7
#define MIPS_REGISTER_SP		29

/*
 * The state of the code an exception interrupted, as trap.S saves it.
 *
 * One frame lives on the stack the exception moved to, from the entry to
 * the eret; the handler may change it, and the return loads what it
 * holds, except that Status is replaced before every return.
 */
struct mips_trap_frame {
	uint32_t r[32];
	uint32_t hi;
	uint32_t lo;
	uint32_t exception_pc;
	uint32_t status;
	uint32_t cause;
	uint32_t bad_address;
};

_Static_assert(__builtin_offsetof(struct mips_trap_frame, hi) == MIPS_FRAME_HI, "trap frame hi");
_Static_assert(__builtin_offsetof(struct mips_trap_frame, lo) == MIPS_FRAME_LO, "trap frame lo");
_Static_assert(__builtin_offsetof(struct mips_trap_frame, exception_pc) == MIPS_FRAME_EXCEPTION_PC, "trap frame epc");
_Static_assert(__builtin_offsetof(struct mips_trap_frame, status) == MIPS_FRAME_STATUS, "trap frame status");
_Static_assert(__builtin_offsetof(struct mips_trap_frame, cause) == MIPS_FRAME_CAUSE, "trap frame cause");
_Static_assert(__builtin_offsetof(struct mips_trap_frame, bad_address) == MIPS_FRAME_BAD_ADDRESS, "trap frame badvaddr");
_Static_assert(sizeof(struct mips_trap_frame) == MIPS_FRAME_SIZE, "trap frame size");
_Static_assert((MIPS_TRAP_STACK_BYTES % 8) == 0, "trap frame alignment");

/*
 * The top of the kernel stack an exception from user mode moves to.  The
 * processor runs one task at a time, so one word serves; the task switch
 * sets it.  Zero until a task that enters user mode runs.
 */
extern uint32_t mips_kernel_stack_pointer;

void
mips_trap(
	struct mips_trap_frame *frame);

void
mips_trap_selftest(
	void);

#endif
