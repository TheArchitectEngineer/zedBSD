/*
 * zedBSD mips32 RG350 kernel link script.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The image is linked in KSEG0 at KERNEL_LINK, 6 MiB above the start of
 * RAM, where the boot loader reads a raw kernel and jumps to its first
 * byte through KSEG1; the same address serves as the uImage's load and
 * entry address, so that one image boots either way.  Everything below
 * it is left to the loader and is reserved with the image.
 *
 * The memory the LCD controller reads by DMA, the framebuffer and its
 * descriptors, is in the NOLOAD section .uncached, after the bss and
 * before the image's end.  locore.S clears the bss through the cache
 * and would leave dirty lines over it; .uncached is outside that range
 * and is only ever written through KSEG1.
 *
 * Values referenced: pcercuei UBIBoot master src/fat.c and src/config.h
 * (LD_ADDR, physical 0x00600000, where a raw kernel is read and entered
 * through KSEG1).
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

KERNEL_LINK = 0x80600000;

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

	.uncached (NOLOAD) : ALIGN(4096)
	{
		__uncached_start = .;
		*(.uncached .uncached.*)
		. = ALIGN(4096);
		__uncached_end = .;
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
