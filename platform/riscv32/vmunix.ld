/*
 * zedBSD riscv32 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked at KERNEL_PHYS_LOAD, the first megapage after the
 * firmware's, and runs there: the kernel's direct map holds RAM at its own
 * addresses, so a linked address is a physical one.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_PHYS_LOAD = 0x80400000;

SECTIONS
{
	. = KERNEL_PHYS_LOAD;
	__kernel_vma_start = .;

	.text :
	{
		__kernel_text_start = .;
		KEEP(*(.text.boot))
		*(.hightext)
		*(.text .text.*)
		__kernel_text_end = .;
	}

	.rodata : ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.* .srodata .srodata.*)
		__kernel_rodata_end = .;
	}

	.data : ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.* .sdata .sdata.*)
	}

	.bss (NOLOAD) : ALIGN(4096)
	{
		__bss_start = .;
		*(.bss.boot)
		*(.bss .bss.* .sbss .sbss.* COMMON)
		. = ALIGN(8);
		__bss_end = .;
	}

	. = ALIGN(4096);
	__kernel_vma_end = .;

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(_start == KERNEL_PHYS_LOAD, "_start must be the image's first and lowest-loaded byte")
ASSERT(__kernel_vma_end - __kernel_vma_start <= 0x1000000, "the image spans at most 16 MiB")
