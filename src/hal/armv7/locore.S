/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The armv7 kernel's first instructions.
 *
 * The loader enters in supervisor mode with the MMU and the caches off,
 * at the physical address the image was linked for (ARMV7_KERNEL_PHYS),
 * with the device tree's physical address in r2 when it passes one (the
 * emulator does so for a raw image, as it boots Linux).  The image is
 * linked in the direct map, ARMV7_VIRT_OFFSET above that address, so
 * until the MMU is on the code reaches its own data only through
 * physical addresses it computes, and it calls no C.
 *
 * The boot table maps the section that holds the image as it is (so the
 * instruction after the MMU is turned on is still mapped) and the first
 * ARMV7_BOOT_MAP_BYTES of RAM in the direct map, in sections.  TTBCR.N
 * stays 0 so the one table serves both halves.  The C code maps the early
 * devices into it, then builds the kernel's own table from the RAM the
 * device tree describes and moves to it, splitting the address space
 * (space.c).
 */

#include "defs.h"

	.syntax unified
	.arm

	.section .text.boot, "ax"
	.balign 4
	.global _start
	.type _start, %function
_start:
	/* Interrupts, FIQs and asynchronous aborts stay masked until the vectors exist. */
	cpsid aif

	/* Keeps the device tree's physical address. */
	mov r10, r2

	/* Only supervisor mode is supported; another mode stops here. */
	mrs r4, cpsr
	and r4, r4, #ARMV7_PSR_MODE_MASK
	cmp r4, #ARMV7_PSR_MODE_SVC
	bne armv7_halt_loop

	/* The image must be where it is linked for, which the boot map assumes. */
	adr r4, _start
	ldr r5, =ARMV7_KERNEL_PHYS
	cmp r4, r5
	bne armv7_halt_loop

	/*
	 * A device tree inside the image's memory would be lost when the
	 * BSS is zeroed, before anything can report it: stops here, where a
	 * debugger sees why.  cmain.c checks the tree's whole extent and the
	 * ramdisk once the console is up.  r4 is the image's first byte.
	 */
	ldr r6, =ARMV7_VIRT_OFFSET
	ldr r5, =_end
	sub r5, r5, r6
	cmp r10, r4
	blo 3f
	cmp r10, r5
	blo armv7_halt_loop
3:
	/* Turns off the MMU, the caches and the alignment check, in case the loader left them on. */
	mrc p15, 0, r4, c1, c0, 0
	ldr r5, =(ARMV7_SCTLR_M | ARMV7_SCTLR_A | ARMV7_SCTLR_C | ARMV7_SCTLR_I)
	bic r4, r4, r5
	mcr p15, 0, r4, c1, c0, 0
	isb

	/* Drops the instruction cache, the branch predictor and the TLB the loader left. */
	mov r4, #0
	mcr p15, 0, r4, c7, c5, 0
	mcr p15, 0, r4, c7, c5, 6
	mcr p15, 0, r4, c8, c7, 0
	dsb
	isb

	/* C requires zeroed BSS, which holds the boot table and the boot stack. */
	ldr r6, =ARMV7_VIRT_OFFSET
	ldr r4, =__bss_start
	ldr r5, =__bss_end
	sub r4, r4, r6
	sub r5, r5, r6
	mov r7, #0
1:	cmp r4, r5
	strlo r7, [r4], #4
	blo 1b

	/* The boot table, at its physical address. */
	ldr r4, =armv7_boot_l1
	sub r4, r4, r6

	/* The section that holds the image, at its own address, for the jump after the MMU is on. */
	ldr r9, =ARMV7_L1_KERNEL_NORMAL
	ldr r5, =ARMV7_KERNEL_PHYS
	lsr r7, r5, #ARMV7_SECTION_SHIFT
	lsl r8, r7, #ARMV7_SECTION_SHIFT
	orr r8, r8, r9
	str r8, [r4, r7, lsl #2]

	/* The first sections of RAM in the direct map. */
	ldr r7, =(ARMV7_DIRECT_BASE >> ARMV7_SECTION_SHIFT)
	ldr r8, =ARMV7_PHYS_BASE
	orr r8, r8, r9
	ldr r11, =(ARMV7_BOOT_MAP_BYTES >> ARMV7_SECTION_SHIFT)
2:	str r8, [r4, r7, lsl #2]
	add r7, r7, #1
	add r8, r8, #ARMV7_SECTION_SIZE
	subs r11, r11, #1
	bne 2b
	dsb

	/* Domain 0 is checked as a client; one table translates both halves for now. */
	mov r7, #ARMV7_DACR_CLIENT0
	mcr p15, 0, r7, c3, c0, 0
	mov r7, #0
	mcr p15, 0, r7, c2, c0, 2
	orr r7, r4, #ARMV7_TTBR_FLAGS
	mcr p15, 0, r7, c2, c0, 0
	mcr p15, 0, r7, c2, c0, 1
	mov r7, #0
	mcr p15, 0, r7, c13, c0, 1

	/* No per-CPU record yet: a trap before C has made one sees zero. */
	mcr p15, 0, r7, c13, c0, 4
	isb

	/* Turns on the MMU, the caches, branch prediction and the access flag. */
	mrc p15, 0, r7, c1, c0, 0
	ldr r8, =ARMV7_SCTLR_SET
	orr r7, r7, r8
	ldr r8, =ARMV7_SCTLR_CLEAR
	bic r7, r7, r8
	mcr p15, 0, r7, c1, c0, 0
	isb

	/* Moves to the address the image is linked at. */
	ldr pc, =armv7_boot_high

armv7_boot_high:
	/* The boot stack and the vectors, at their linked addresses. */
	ldr sp, =armv7_boot_stack_top
	ldr r4, =armv7_vectors
	mcr p15, 0, r4, c12, c0, 0
	isb

	/* Asynchronous aborts are reported from here on. */
	cpsie a

	/* Enters C with the device tree's and the image's physical addresses. */
	mov r0, r10
	ldr r1, =ARMV7_KERNEL_PHYS
	bl armv7_cmain
	b armv7_halt_loop
	.ltorg
	.size _start, . - _start

	/*
	 * Stops the processor for good with interrupts masked.  It is used
	 * before the MMU is on as well, so it reaches nothing by address.
	 */
	.balign 4
	.global armv7_halt_loop
	.type armv7_halt_loop, %function
armv7_halt_loop:
	cpsid if
1:	dsb
	wfi
	b 1b
	.size armv7_halt_loop, . - armv7_halt_loop

	.section .bss.boot, "aw", %nobits
	.balign 16
armv7_boot_stack:
	.skip 16384
	.global armv7_boot_stack_top
armv7_boot_stack_top:
