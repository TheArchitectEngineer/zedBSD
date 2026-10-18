/*
 * zedBSD ppc32 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked at KERNEL_PHYS_LOAD, where the firmware loads the
 * flat image and enters its first byte, and runs there: the supervisor
 * reaches RAM at its own addresses.
 */

OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)

KERNEL_PHYS_LOAD = 0x01000000;

SECTIONS
{
	. = KERNEL_PHYS_LOAD;
	__kernel_vma_start = .;

	.text :
	{
		__kernel_text_start = .;
		KEEP(*(.text.boot))
		*(.text .text.*)
		__kernel_text_end = .;
	}

	.rodata : ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.* .sdata2 .sdata2.*)
		__kernel_rodata_end = .;
	}

	.data : ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.* .sdata .sdata.* .got .got2)
	}

	.bss (NOLOAD) : ALIGN(4096)
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

ASSERT(_start == KERNEL_PHYS_LOAD, "_start must be the image's first byte")
ASSERT(__kernel_end < 0x02000000, "the vectors branch to the image absolutely, within 32 MiB")
