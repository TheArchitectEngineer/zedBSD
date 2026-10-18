/*
 * zedBSD ppc64 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked at KERNEL_LOAD and loaded there: the emulator places
 * a segment at its load address's low 28 bits after its firmware's 4 MiB,
 * so the load addresses start from 0.  The firmware enters the first byte.
 */

OUTPUT_FORMAT("elf64-powerpc")
OUTPUT_ARCH(powerpc:common64)
ENTRY(_start)

KERNEL_LOAD = 0x00400000;
FIRMWARE_GAP = 0x00400000;

SECTIONS
{
	. = KERNEL_LOAD;
	__kernel_vma_start = .;

	.text : AT(ADDR(.text) - FIRMWARE_GAP)
	{
		__kernel_text_start = .;
		KEEP(*(.text.boot))
		*(.text .text.*)
		__kernel_text_end = .;
	}

	.rodata : AT(ADDR(.rodata) - FIRMWARE_GAP) ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.*)
		__kernel_rodata_end = .;
	}

	.data : AT(ADDR(.data) - FIRMWARE_GAP) ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.*)
		*(.toc .got .toc1)
	}

	.bss (NOLOAD) : AT(ADDR(.bss) - FIRMWARE_GAP) ALIGN(4096)
	{
		__bss_start = .;
		*(.bss.boot)
		*(.bss .bss.* .sbss .sbss.* COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}

	__kernel_end = .;
	__kernel_vma_end = .;

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(_start == KERNEL_LOAD, "_start must be the image's first byte")
ASSERT(__kernel_end < 0x02000000, "the vectors branch to the image absolutely, within 32 MiB")
