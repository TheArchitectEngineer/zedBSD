/*
 * zedBSD armv7 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked in the direct map, which holds RAM from
 * ARMV7_PHYS_BASE at 0x80000000, and loaded at ARMV7_KERNEL_PHYS: every
 * section's load address is its linked address less that offset.  The
 * loader enters at the physical _start, the image's first byte, so a raw
 * image made from it starts there too.  The board's makefile gives both
 * addresses with --defsym.
 */

OUTPUT_ARCH(arm)
ENTRY(armv7_entry_phys)

ARMV7_VIRT_OFFSET = 0x80000000 - ARMV7_PHYS_BASE;
ARMV7_KERNEL_VIRT = ARMV7_KERNEL_PHYS + ARMV7_VIRT_OFFSET;

SECTIONS
{
	. = ARMV7_KERNEL_VIRT;
	__kernel_vma_start = .;

	.text : AT(ADDR(.text) - ARMV7_VIRT_OFFSET)
	{
		__kernel_text_start = .;
		KEEP(*(.text.boot))
		*(.hightext)
		*(.text .text.*)
		. = ALIGN(4096);
		__kernel_text_end = .;
	}

	.rodata : AT(ADDR(.rodata) - ARMV7_VIRT_OFFSET) ALIGN(4096)
	{
		__kernel_rodata_start = .;
		*(.rodata .rodata.*)
		. = ALIGN(4096);
		__kernel_rodata_end = .;
	}

	.data : AT(ADDR(.data) - ARMV7_VIRT_OFFSET) ALIGN(4096)
	{
		__kernel_data_start = .;
		*(.data .data.*)
		_edata = .;
	}

	.bss (NOLOAD) : ALIGN(4096)
	{
		__bss_start = .;
		*(.bss.boot)
		*(.bss .bss.* COMMON)
		*(.vfs_bss*) *(.net_bss*)
		KEEP(*(.kernel_heap*))
		. = ALIGN(8);
		__bss_end = .;
	}

	. = ALIGN(4096);
	__kernel_vma_end = .;
	_end = .;

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) *(.ARM.extab*) }
}

armv7_entry_phys = _start - ARMV7_VIRT_OFFSET;

ASSERT(_start == ARMV7_KERNEL_VIRT, "_start must be the image's first byte")
ASSERT(__kernel_vma_end - __kernel_vma_start <= 0x2000000, "the image spans at most 32 MiB")
