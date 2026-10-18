/*
 * zedBSD m68k virt kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked and loaded at KERNEL_PHYS_LOAD and runs there: the
 * supervisor reaches RAM at its own addresses.  The loader places its boot
 * information at the end of the last segment, which __kernel_end marks.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

KERNEL_PHYS_LOAD = 0x00010000;

PHDRS
{
	text PT_LOAD;
	data PT_LOAD;
}

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
	} :text

	.rodata : ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.*)
		__kernel_rodata_end = .;
	} :text

	.data : ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.*)
	} :data

	.bss (NOLOAD) : ALIGN(16)
	{
		__bss_start = .;
		*(.bss.boot)
		*(.bss .bss.* COMMON)
		. = ALIGN(16);
		__bss_end = .;
	} :data

	__kernel_end = .;
	__kernel_vma_end = .;

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(_start == KERNEL_PHYS_LOAD, "_start must be the image's first byte")
