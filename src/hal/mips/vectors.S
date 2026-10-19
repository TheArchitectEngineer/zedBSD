/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS exception vectors, which EBase points at.
 *
 * Until the trap handling of the HAL is in place every vector reports the
 * exception on the console and stops: the processor's state is passed to
 * C on a stack of its own, since the stack pointer of the code that
 * faulted may be what failed.
 */

#include "asm.h"

	.set noreorder
	.set noat

	/*
	 * The vectors are at fixed offsets from EBase, which takes an
	 * address aligned to 4 KiB.
	 */
	.section .text.vectors, "ax"
	.balign 4096
	.global mips_vectors
mips_vectors:
	.org MIPS_VECTOR_REFILL
	j mips_early_vector
	nop

	.org MIPS_VECTOR_REFILL64
	j mips_early_vector
	nop

	.org MIPS_VECTOR_CACHE_ERROR
	j mips_early_vector
	nop

	.org MIPS_VECTOR_GENERAL
	j mips_early_vector
	nop

	/*
	 * Reports an exception taken before the trap handling exists:
	 * Cause, EPC, BadVAddr and Status go to mips_early_exception(), which
	 * does not return.
	 */
	.balign 4
	.type mips_early_vector, %function
mips_early_vector:
	la $29, mips_trap_stack_top
	addiu $29, $29, -MIPS_ARGUMENT_HOME_SIZE
	mfc0 $4, MIPS_CP0_CAUSE
	mfc0 $5, MIPS_CP0_EXCEPTION_PC
	mfc0 $6, MIPS_CP0_BAD_ADDRESS
	jal mips_early_exception
	mfc0 $7, MIPS_CP0_STATUS

	/* The report does not return; waits here for good if it does. */
1:
	wait
	b 1b
	nop
	.size mips_early_vector, . - mips_early_vector

	/* The stack of the early exception report. */
	.section .bss.boot, "aw", %nobits
	.balign 16
mips_trap_stack:
	.space MIPS_TRAP_STACK_SIZE
mips_trap_stack_top:
