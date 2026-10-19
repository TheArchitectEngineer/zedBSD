/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS exception vectors, which EBase points at.
 *
 * The TLB refill walks the page tables of the space selected now and
 * writes the pair of pages the missed address is in.  The general
 * exception goes to the trap handling of trap.S.  The 64-bit refill and
 * the cache error, which nothing on a 32-bit processor expects, report
 * the exception on the console and stop: the processor's state is passed
 * to C on a stack of its own, since the stack pointer of the code that
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
	/*
	 * The TLB refill, with k0 and k1 alone and nothing else touched.
	 *
	 * It counts itself for the self-test, then reads the root entry of
	 * BadVAddr from the selected space's root.  An empty root entry
	 * writes an invalid pair, so that the retried access takes a TLB
	 * invalid exception, which the general handler reports as a fault.
	 * Otherwise the even and odd entries of the pair, shifted right by
	 * six, are EntryLo0 and EntryLo1.  The hardware already put the
	 * pair's VPN2 and the space's ASID into EntryHi.  A refill comes only
	 * for a pair the TLB does not hold, so tlbwr makes no duplicate.
	 */
	.org MIPS_VECTOR_REFILL
	.global mips_tlb_refill
mips_tlb_refill:
	la $26, mips_tlb_refill_count
	lw $27, 0($26)
	addiu $27, $27, 1
	sw $27, 0($26)
	mfc0 $26, MIPS_CP0_BAD_ADDRESS
	la $27, mips_tlb_root
	lw $27, 0($27)
	srl $26, $26, MIPS_ROOT_SHIFT
	sll $26, $26, 2
	addu $27, $27, $26
	lw $27, 0($27)
	beqz $27, 1f
	mfc0 $26, MIPS_CP0_BAD_ADDRESS
	srl $26, $26, MIPS_PAGE_SHIFT - 2
	andi $26, $26, (MIPS_TABLE_ENTRIES - 2) * 4
	addu $27, $27, $26
	lw $26, 0($27)
	lw $27, 4($27)
	srl $26, $26, MIPS_PTE_HARDWARE_SHIFT
	mtc0 $26, MIPS_CP0_ENTRY_LO0
	srl $27, $27, MIPS_PTE_HARDWARE_SHIFT
	mtc0 $27, MIPS_CP0_ENTRY_LO1
	ehb
	tlbwr
	eret
1:
	mtc0 $0, MIPS_CP0_ENTRY_LO0
	mtc0 $0, MIPS_CP0_ENTRY_LO1
	ehb
	tlbwr
	eret

	.org MIPS_VECTOR_REFILL64
	j mips_early_vector
	nop

	.org MIPS_VECTOR_CACHE_ERROR
	j mips_early_vector
	nop

	.org MIPS_VECTOR_GENERAL
	j mips_trap_entry
	nop

	/*
	 * Reports an exception the trap handling does not take:
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
