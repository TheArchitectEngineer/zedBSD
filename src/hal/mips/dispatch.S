/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS context switch and the first entry of a kernel task.
 *
 * A switch keeps only what the o32 calling convention makes the callee
 * save: s0 to s7, fp and ra, with sp itself in the save slot.  Status is
 * neither saved nor loaded, so the interrupt mask and the interrupt
 * enable go on as they are; the caller's interrupt save and restore
 * bring IE back on each side.
 */

#include "asm.h"

	.set noreorder

	.text
	.balign 4

	/*
	 * Switches stacks: pushes the callee-saved registers, stores sp at
	 * a0, loads sp from a1 and pops the registers kept there.
	 */
	.global mips_task_dispatch
	.type mips_task_dispatch, %function
mips_task_dispatch:
	addiu $29, $29, -(MIPS_DISPATCH_WORDS * 4)
	sw $16, 0($29)
	sw $17, 4($29)
	sw $18, 8($29)
	sw $19, 12($29)
	sw $20, 16($29)
	sw $21, 20($29)
	sw $22, 24($29)
	sw $23, 28($29)
	sw $30, MIPS_DISPATCH_FP * 4($29)
	sw $31, MIPS_DISPATCH_RA * 4($29)
	sw $29, 0($4)
	lw $29, 0($5)
	lw $16, 0($29)
	lw $17, 4($29)
	lw $18, 8($29)
	lw $19, 12($29)
	lw $20, 16($29)
	lw $21, 20($29)
	lw $22, 24($29)
	lw $23, 28($29)
	lw $30, MIPS_DISPATCH_FP * 4($29)
	lw $31, MIPS_DISPATCH_RA * 4($29)
	jr $31
	addiu $29, $29, MIPS_DISPATCH_WORDS * 4
	.size mips_task_dispatch, . - mips_task_dispatch

	/*
	 * The first entry of a kernel task: s0 is its function and s1 its
	 * argument.  It first releases the task it was switched from, whose
	 * stack it no longer uses.  It starts the function with interrupts
	 * on, since a new task has no suspended switch that would turn them
	 * back on, and stops if the function returns.
	 */
	.global mips_kernel_task_entry
	.type mips_kernel_task_entry, %function
mips_kernel_task_entry:
	jal mips_task_finish_switch
	nop
	ei
	ehb
	move $4, $17
	jalr $16
	addiu $29, $29, -MIPS_ARGUMENT_HOME_SIZE
	jal mips_task_returned
	nop
1:
	wait
	b 1b
	nop
	.size mips_kernel_task_entry, . - mips_kernel_task_entry
