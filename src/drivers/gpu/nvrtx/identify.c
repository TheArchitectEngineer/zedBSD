/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Chip identity of the nvrtx driver.
 *
 * The PCI device ID only names a product; the chip is named by the identity
 * register at the start of the register window.  This file turns the two
 * identity registers into a chip record.  It touches no hardware, so the
 * host test links it alone.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* The architecture numbers of the identity register this driver knows. */
#define IDENTIFY_ARCH_TURING		0x16U
#define IDENTIFY_ARCH_AMPERE		0x17U
#define IDENTIFY_ARCH_HOPPER		0x18U
#define IDENTIFY_ARCH_ADA		0x19U
#define IDENTIFY_ARCH_BLACKWELL_DC	0x1aU
#define IDENTIFY_ARCH_BLACKWELL		0x1bU

/*
 * One known chip: its number in the identity register, its name, and whether
 * the GSP boot path of the first target applies to it.
 */
struct identify_entry {
	uint32_t chipset;
	const char *name;
	int gsp_tu102;
};

/*
 * The chips of Turing and later, by identity number.
 *
 * Only TU102, TU104 and TU106 boot the GSP firmware below the tu102
 * directory; TU116 and TU117 have their own booter images, and the later
 * generations their own firmware.  Those are recognized and named, and the
 * stages after N1 refuse them until their firmware path exists.
 */
static const struct identify_entry identify_table[] = {
	{ 0x162U, "TU102", 1 },
	{ 0x164U, "TU104", 1 },
	{ 0x166U, "TU106", 1 },
	{ 0x167U, "TU117", 0 },
	{ 0x168U, "TU116", 0 },
	{ 0x170U, "GA100", 0 },
	{ 0x172U, "GA102", 0 },
	{ 0x173U, "GA103", 0 },
	{ 0x174U, "GA104", 0 },
	{ 0x176U, "GA106", 0 },
	{ 0x177U, "GA107", 0 },
	{ 0x180U, "GH100", 0 },
	{ 0x192U, "AD102", 0 },
	{ 0x193U, "AD103", 0 },
	{ 0x194U, "AD104", 0 },
	{ 0x196U, "AD106", 0 },
	{ 0x197U, "AD107", 0 },
	{ 0x1b2U, "GB202", 0 },
	{ 0x1b3U, "GB203", 0 },
	{ 0x1b5U, "GB205", 0 },
	{ 0x1b6U, "GB206", 0 },
	{ 0x1b7U, "GB207", 0 }
};

static enum nvrtx_family identify_family(uint32_t architecture);

/*
 * Decodes the chip identity from the two identity registers.
 *
 * Returns 0 for a Turing or later GPU the identity names, ENODEV for a GPU
 * the driver never drives (older generations, a virtual function, a
 * big-endian or unreadable identity), and ENOTSUP for a newer chip number
 * it does not know.
 */
int
nvrtx_identify(
	uint32_t ident0,
	uint32_t ident1,
	struct nvrtx_chip *chip)
{
	uint32_t architecture;
	size_t index;

	/* Starts from an unknown chip, so a refusal leaves nothing half filled. */
	chip->chipset = 0U;
	chip->revision = 0U;
	chip->family = NVRTX_FAMILY_UNKNOWN;
	chip->name = "unknown";
	chip->gsp_tu102 = 0;

	/* A window that reads all ones has no GPU behind it, or the GPU fell off the bus. */
	if (ident0 == 0xffffffffU)
		return ENODEV;

	/*
	 * A GPU switched to big-endian would need a write to switch back, and
	 * N0 never writes; anything else in the second register but the
	 * little-endian zero and the virtual-function bits is not trusted either.
	 */
	if (ident1 == NVRTX_IDENT_1_BIG_ENDIAN)
		return ENODEV;
	if ((ident1 & ~NVRTX_IDENT_1_VIRTUAL_MASK) != 0U)
		return ENODEV;

	/* Reads the chip number and the revision. */
	chip->chipset = (ident0 >> NVRTX_IDENT_CHIP_SHIFT) & NVRTX_IDENT_CHIP_MASK;
	chip->revision = ident0 & NVRTX_IDENT_REVISION_MASK;

	/* Refuses the generations before Turing, which this driver never covers. */
	architecture = (ident0 >> NVRTX_IDENT_ARCH_SHIFT) & NVRTX_IDENT_ARCH_MASK;
	chip->family = identify_family(architecture);
	if (chip->family == NVRTX_FAMILY_UNKNOWN) {
		/* A number above the newest known architecture is newer, not older. */
		if (architecture > IDENTIFY_ARCH_BLACKWELL)
			return ENOTSUP;

		return ENODEV;
	}

	/* A virtual function of a partitioned GPU is not a whole GPU to drive. */
	if ((ident1 & NVRTX_IDENT_1_VIRTUAL_MASK) != 0U)
		return ENODEV;

	/* Names the chip from the table of known chips. */
	for (index = 0U; index < sizeof(identify_table) / sizeof(identify_table[0]); index++) {
		if (identify_table[index].chipset == chip->chipset) {
			chip->name = identify_table[index].name;
			chip->gsp_tu102 = identify_table[index].gsp_tu102;

			/* Succeeded: the chip is known. */
			return 0;
		}
	}

	/* A chip of a known generation that the table does not list yet. */
	return ENOTSUP;
}

/*
 * Names a GPU generation for the stage marks.
 */
const char *
nvrtx_family_name(
	enum nvrtx_family family)
{
	/* Names each generation the identity register can report. */
	switch (family) {
	case NVRTX_FAMILY_TURING:
		return "Turing";
	case NVRTX_FAMILY_AMPERE:
		return "Ampere";
	case NVRTX_FAMILY_HOPPER:
		return "Hopper";
	case NVRTX_FAMILY_ADA:
		return "Ada";
	case NVRTX_FAMILY_BLACKWELL:
		return "Blackwell";
	case NVRTX_FAMILY_UNKNOWN:
		break;
	}

	/* An unknown generation has no name. */
	return "unknown";
}

/* Maps the architecture field of the identity register to a generation. */
static enum nvrtx_family
identify_family(
	uint32_t architecture)
{
	/* Each architecture number belongs to one generation. */
	switch (architecture) {
	case IDENTIFY_ARCH_TURING:
		return NVRTX_FAMILY_TURING;
	case IDENTIFY_ARCH_AMPERE:
		return NVRTX_FAMILY_AMPERE;
	case IDENTIFY_ARCH_HOPPER:
		return NVRTX_FAMILY_HOPPER;
	case IDENTIFY_ARCH_ADA:
		return NVRTX_FAMILY_ADA;
	case IDENTIFY_ARCH_BLACKWELL_DC:
		return NVRTX_FAMILY_BLACKWELL;
	case IDENTIFY_ARCH_BLACKWELL:
		return NVRTX_FAMILY_BLACKWELL;
	default:
		break;
	}

	/* Older generations and unknown numbers. */
	return NVRTX_FAMILY_UNKNOWN;
}
