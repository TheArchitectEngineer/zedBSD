/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the riscv64 UEFI loader hands to the riscv64 HAL.
 *
 * Only the loader and the HAL read this header.  The HAL turns what it
 * holds into the kernel's common handoff and its named objects.
 */

#ifndef KERN_BOOTLOADER_RISCV64_HANDOFF_H
#define KERN_BOOTLOADER_RISCV64_HANDOFF_H

#include <stdint.h>

#include "../../include/kern/boot.h"

/*
 * The magic the loader's handoffs start with, "ZBL6" read as a
 * little-endian word.  A flattened device tree starts with a different
 * word, so the kernel's entry tells the loader's handoff from a device tree
 * the firmware passed directly by the first word alone.
 */
#ifndef ZBL6_HANDOFF_MAGIC
#define ZBL6_HANDOFF_MAGIC		0x364c425a
#endif

/*
 * The layout of this handoff.  A later layout keeps every field of the
 * earlier ones in place and appends its own after them, raising the version
 * and the size; a reader takes the fields of the versions it knows and
 * leaves the rest.  Version 1 ends with the boot parameters.
 */
#define ZBL6_RISCV64_HANDOFF_VERSION	1U

/*
 * The handoff record.
 *
 * The loader fills one in memory it allocated, passes its physical address
 * in a1 with the boot hart's id in a0, and enters the kernel with
 * translation off.  Every address is physical.  The HAL reads the record
 * before it sets memory up and does not keep it; the device tree and the
 * ramdisk it names stay where the loader put them.
 */
struct zbl6_riscv64_handoff {
	uint32_t magic;
	uint32_t version;
	uint32_t size;
	uint32_t flags;

	/* The hart the firmware booted on, which runs the kernel's entry. */
	uint64_t boot_hart;

	/* The loader's copy of the firmware's device tree. */
	uint64_t fdt_phys;
	uint64_t fdt_size;

	/* The initial ramdisk, both zero when there is none. */
	uint64_t initrd_phys;
	uint64_t initrd_size;

	/* Where the kernel image was put. */
	uint64_t kernel_phys_start;
	uint64_t kernel_phys_end;

	/* The boot parameters zedbsd.cfg named. */
	struct kern_boot_parameter_record parameters;
} __attribute__((packed));

#endif
