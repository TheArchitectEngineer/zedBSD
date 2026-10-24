/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The shader ISAs the GHCI driver compiles for.
 *
 * Every GHCI GPU with a shader unit runs programs of the standard ISA,
 * GSI1: natively, or translated by its command processor when its own ISA
 * differs.  The driver therefore needs one compiler, GSI1's, for every
 * GPU; a vendor's own ISA would add a row to the table.
 */

#include <stddef.h>
#include <stdint.h>

#include "ghci.h"
#include "hcr.h"

/* The FourCC of the standard shader ISA. */
#define ISA_GSI1			GHCI_FOURCC('G', 'S', 'I', '1')

/* The compilers, by ISA; the table lives as long as the kernel. */
static const struct ghci_isa_backend isa_backends[] = {
	{ ISA_GSI1, "GSI1", ghci_gsi1_compile, ghci_gsi1_program_free }
};

/*
 * Reports the backend that compiles for a device with these capabilities
 * and native ISA.
 */
const struct ghci_isa_backend *
ghci_isa_select(
	uint32_t caps,
	uint32_t isa_id)
{
	(void)isa_id;

	/* A GPU without a shader unit runs no program. */
	if ((caps & GHCI_CAPS_SHADER) == 0U)
		return NULL;

	/* Every shader unit takes the standard ISA, whatever its native one. */
	return &isa_backends[0];
}
