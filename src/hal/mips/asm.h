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
