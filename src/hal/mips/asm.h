/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS HAL's processor definitions, shared by its C and its assembly:
 * the coprocessor 0 registers it uses and their bits, and the fixed
 * segments of the 32-bit address space.
 */

#ifndef KERN_HAL_MIPS_ASM_H
#define KERN_HAL_MIPS_ASM_H

/* Marks a parameter a function does not use. */
#define UNUSED_PARAMETER(parameter) ((void)(parameter))

/*
 * The coprocessor 0 registers, as register number and select.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III, the
 * Privileged Resource Architecture (the coprocessor 0 register numbers
 * and selects of BadVAddr, Count, Compare, Status, Cause, EPC, PRId,
 * EBase, Config and Config1).
 */
#define MIPS_CP0_BAD_ADDRESS		$8
#define MIPS_CP0_COUNT			$9
#define MIPS_CP0_COMPARE		$11
#define MIPS_CP0_STATUS			$12
#define MIPS_CP0_CAUSE			$13
#define MIPS_CP0_EXCEPTION_PC		$14
#define MIPS_CP0_PROCESSOR_ID		$15
#define MIPS_CP0_PROCESSOR_ID_SELECT	0
#define MIPS_CP0_EXCEPTION_BASE		$15
#define MIPS_CP0_EXCEPTION_BASE_SELECT	1
#define MIPS_CP0_CONFIG			$16
#define MIPS_CP0_CONFIG_SELECT		0
#define MIPS_CP0_CONFIG1_SELECT		1

/*
 * The bits of Status.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III
 * (Status: IE bit 0, EXL bit 1, ERL bit 2, KSU bits 3-4, IM bits 8-15,
 * BEV bit 22, FR bit 26, CU0 bit 28, CU1 bit 29).
 */
#define MIPS_STATUS_INTERRUPTS		0x00000001
#define MIPS_STATUS_EXCEPTION_LEVEL	0x00000002
#define MIPS_STATUS_ERROR_LEVEL		0x00000004
#define MIPS_STATUS_MODE_MASK		0x00000018
#define MIPS_STATUS_MODE_USER		0x00000010
#define MIPS_STATUS_INTERRUPT_MASK	0x0000ff00
#define MIPS_STATUS_BOOT_VECTORS	0x00400000
#define MIPS_STATUS_FLOAT_64		0x04000000
#define MIPS_STATUS_COPROCESSOR_0	0x10000000
#define MIPS_STATUS_COPROCESSOR_1	0x20000000

/*
 * The fields of Cause and Config the HAL reads.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III
 * (Cause: ExcCode bits 2-6, BD bit 31; Config: K0 bits 0-2, the value 2
 * uncached and 3 cacheable noncoherent).
 */
#define MIPS_CAUSE_CODE_SHIFT		2
#define MIPS_CAUSE_CODE_MASK		0x1f
#define MIPS_CAUSE_BRANCH_DELAY		0x80000000
#define MIPS_CAUSE_PENDING_MASK		0x0000ff00

/*
 * The interrupt lines of Cause.IP and Status.IM the malta board wires:
 * the PIIX4's 8259 pair on hardware line 0 (IP2) and the timer of
 * coprocessor 0 on hardware line 5 (IP7).
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III (IP2
 * bit 10, IP7 bit 15); QEMU v10.0.11 target/mips/cpu.c (the IntCtl reset
 * value 0xe0000000, IPTI 7) and hw/mips/malta.c (the 8259 output on the
 * processor's interrupt 2).
 */
#define MIPS_INTERRUPT_BOARD		0x00000400
#define MIPS_INTERRUPT_TIMER		0x00008000

/*
 * The exception codes of Cause.ExcCode the trap handling tells apart.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III (Int
 * 0, Mod 1, TLBL 2, TLBS 3, AdEL 4, AdES 5, Sys 8, Bp 9, RI 10, CpU 11,
 * Ov 12, Tr 13, FPE 15).
 */
#define MIPS_EXCEPTION_INTERRUPT	0
#define MIPS_EXCEPTION_TLB_MODIFIED	1
#define MIPS_EXCEPTION_TLB_LOAD		2
#define MIPS_EXCEPTION_TLB_STORE	3
#define MIPS_EXCEPTION_ADDRESS_LOAD	4
#define MIPS_EXCEPTION_ADDRESS_STORE	5
#define MIPS_EXCEPTION_SYSTEM_CALL	8
#define MIPS_EXCEPTION_BREAKPOINT	9
#define MIPS_EXCEPTION_RESERVED		10
#define MIPS_EXCEPTION_COPROCESSOR	11
#define MIPS_EXCEPTION_OVERFLOW		12
#define MIPS_EXCEPTION_TRAP		13
#define MIPS_EXCEPTION_FLOAT		15

/*
 * The Cause field of the floating-point control and status register,
 * whose bits raise an exception as soon as a write sets one together
 * with its Enable bit.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume I (FCSR:
 * Cause bits 12-17, Enables bits 7-11).
 */
#define MIPS_FCSR_CAUSE_MASK		0x0003f000
#define MIPS_CONFIG_K0_MASK		0x00000007
#define MIPS_CONFIG_K0_CACHED		3

/*
 * The fixed segments of the 32-bit address space: KSEG0 maps the low
 * 512 MiB of physical space cached and KSEG1 the same uncached, both
 * without the TLB.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III (the
 * kseg0 and kseg1 segments).
 */
#define MIPS_KSEG0_BASE			0x80000000
#define MIPS_KSEG1_BASE			0xa0000000
#define MIPS_KSEG_SIZE			0x20000000

/*
 * The offsets from EBase of the exception vectors when Status.BEV is clear:
 * the TLB refill, the 64-bit TLB refill, the cache error and the general
 * exception.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III (the
 * exception vector offsets 0x000, 0x080, 0x100 and 0x180).
 */
#define MIPS_VECTOR_REFILL		0x000
#define MIPS_VECTOR_REFILL64		0x080
#define MIPS_VECTOR_CACHE_ERROR		0x100
#define MIPS_VECTOR_GENERAL		0x180

/*
 * The trap frame, as offsets from its start: the 32 general registers,
 * hi, lo, and the exception's EPC, Status, Cause and BadVAddr.  The trap
 * entry places it above the 16-byte argument home of the C handler it
 * calls; the whole takes 168 bytes, a multiple of eight.
 */
#define MIPS_FRAME_REGISTER_BYTES	4
#define MIPS_FRAME_HI			128
#define MIPS_FRAME_LO			132
#define MIPS_FRAME_EXCEPTION_PC		136
#define MIPS_FRAME_STATUS		140
#define MIPS_FRAME_CAUSE		144
#define MIPS_FRAME_BAD_ADDRESS		148
#define MIPS_FRAME_SIZE			152
#define MIPS_TRAP_STACK_BYTES		(MIPS_ARGUMENT_HOME_SIZE + MIPS_FRAME_SIZE)

/*
 * The words mips_task_dispatch() keeps on a stack it leaves: s0 to s7,
 * fp and ra, and two words that keep the stack eight-byte aligned with a
 * free home slot.
 */
#define MIPS_DISPATCH_WORDS		12
#define MIPS_DISPATCH_FP		8
#define MIPS_DISPATCH_RA		9

/* The bytes of the boot stack, which ends with the image's bss. */
#define MIPS_BOOT_STACK_SIZE		16384

/* The bytes of the stack the early exception report runs on. */
#define MIPS_TRAP_STACK_SIZE		4096

/*
 * The bytes an o32 caller reserves below its outgoing arguments for the
 * callee to keep a0 to a3 in.
 */
#define MIPS_ARGUMENT_HOME_SIZE		16

#endif
