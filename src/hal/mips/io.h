/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Device access for the MIPS HAL: loads and stores of device registers by
 * physical address, through the uncached segment.
 *
 * The devices of the boards this port runs on are in the low 512 MiB of
 * physical space, which KSEG1 maps uncached without the TLB.  The
 * processor is little-endian, as are the PCI devices, so a register is
 * read and written as it is.
 */

#ifndef KERN_HAL_MIPS_IO_H
#define KERN_HAL_MIPS_IO_H

#include <hal/types.h>
#include "asm.h"

/*
 * Reports the uncached address of a device's physical address.
 */
static __inline uintptr_t
mips_device_address(
	uint32_t physical)
{
	/* KSEG1 holds the low 512 MiB of physical space at a fixed offset. */
	return (uintptr_t)MIPS_KSEG1_BASE + (uintptr_t)physical;
}

/*
 * Reads a 32-bit device register.
 */
static __inline uint32_t
mips_io_read32(
	uint32_t physical)
{
	const volatile uint32_t *word;

	/* The register through the uncached segment. */
	word = (const volatile uint32_t *)mips_device_address(physical);

	/* Succeeded: the register's value. */
	return *word;
}

/*
 * Writes a 32-bit device register.
 */
static __inline void
mips_io_write32(
	uint32_t physical,
	uint32_t value)
{
	volatile uint32_t *word;

	/* The register through the uncached segment. */
	word = (volatile uint32_t *)mips_device_address(physical);
	*word = value;
}

/*
 * Writes a 16-bit device register.
 */
static __inline void
mips_io_write16(
	uint32_t physical,
	uint16_t value)
{
	volatile uint16_t *halfword;

	/* The register through the uncached segment. */
	halfword = (volatile uint16_t *)mips_device_address(physical);
	*halfword = value;
}

/*
 * Reads an 8-bit device register.
 */
static __inline uint8_t
mips_io_read8(
	uint32_t physical)
{
	const volatile uint8_t *byte;

	/* The register through the uncached segment. */
	byte = (const volatile uint8_t *)mips_device_address(physical);

	/* Succeeded: the register's value. */
	return *byte;
}

/*
 * Writes an 8-bit device register.
 */
static __inline void
mips_io_write8(
	uint32_t physical,
	uint8_t value)
{
	volatile uint8_t *byte;

	/* The register through the uncached segment. */
	byte = (volatile uint8_t *)mips_device_address(physical);
	*byte = value;
}

#endif
