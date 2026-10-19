/*
 * zedBSD mips64 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked in CKSEG0 at KERNEL_LINK, 1 MiB above the start of
 * RAM, and runs there: the loader places each segment at its CKSEG0
 * address less 0xffffffff80000000, below which it keeps its own tables.
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

KERNEL_LINK = 0xffffffff80100000;

SECTIONS
{
	. = KERNEL_LINK;
	__kernel_vma_start = .;

	.text :
	{
		__kernel_text_start = .;
		KEEP(*(.text.boot))
		. = ALIGN(4096);
		KEEP(*(.text.vectors))
		*(.hightext)
		*(.text .text.*)
		__kernel_text_end = .;
	}

	.rodata : ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.*)
		__kernel_rodata_end = .;
	}

	.data : ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.*)
	}

	.bss (NOLOAD) : ALIGN(4096)
	{
		__bss_start = .;
		*(.bss.boot)
		*(.bss .bss.* COMMON)
		. = ALIGN(8);
		__bss_end = .;
	}

	. = ALIGN(4096);
	__kernel_vma_end = .;

	/*
	 * Notes, unwind tables and the MIPS ABI records are not loaded.  The
	 * small-data sections would need the global pointer, which the kernel
	 * does not set up; they are not listed here, so that the check after
	 * the link sees one the compiler made in spite of -G 0.
	 */
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.pdr) *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.gnu.attributes) }
}

ASSERT(_start == KERNEL_LINK, "_start must be the image's first and lowest-loaded byte")
ASSERT(__kernel_vma_end - __kernel_vma_start <= 0x1000000, "the image spans at most 16 MiB")
