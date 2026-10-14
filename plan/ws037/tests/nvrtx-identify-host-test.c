/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the nvrtx chip identity decoding (ws037-p003).
 */

#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* One identity pair and what the decoding must make of it. */
struct case_row {
	const char *what;
	unsigned ident0;
	unsigned ident1;
	int error;
	unsigned chipset;
	const char *name;
	int gsp_tu102;
};

/* The cases: the first target, its neighbours, and every refusal. */
static const struct case_row cases[] = {
	{ "TU106 rev a1", 0x166000a1U, 0U, 0, 0x166U, "TU106", 1 },
	{ "TU104", 0x164000a1U, 0U, 0, 0x164U, "TU104", 1 },
	{ "TU102", 0x162000a1U, 0U, 0, 0x162U, "TU102", 1 },
	{ "TU116 (own booter)", 0x168000a1U, 0U, 0, 0x168U, "TU116", 0 },
	{ "GA104", 0x174000a1U, 0U, 0, 0x174U, "GA104", 0 },
	{ "AD102", 0x192000a1U, 0U, 0, 0x192U, "AD102", 0 },
	{ "GB202", 0x1b2000a1U, 0U, 0, 0x1b2U, "GB202", 0 },
	{ "unknown Turing number", 0x16f000a1U, 0U, ENOTSUP, 0x16fU, "unknown", 0 },
	{ "newer architecture", 0x1c2000a1U, 0U, ENOTSUP, 0x1c2U, "unknown", 0 },
	{ "Volta (older)", 0x140000a1U, 0U, ENODEV, 0x140U, "unknown", 0 },
	{ "all ones", 0xffffffffU, 0U, ENODEV, 0U, "unknown", 0 },
	{ "big-endian", 0x166000a1U, 0x01000001U, ENODEV, 0U, "unknown", 0 },
	{ "virtual function", 0x166000a1U, 0x00010000U, ENODEV, 0x166U, "unknown", 0 },
	{ "odd second register", 0x166000a1U, 0x00000100U, ENODEV, 0U, "unknown", 0 }
};

int
main(void)
{
	struct nvrtx_chip chip;
	unsigned index;
	unsigned failures;
	int error;

	/* Decodes every case and compares the answer. */
	failures = 0U;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		error = nvrtx_identify(cases[index].ident0, cases[index].ident1, &chip);
		if (error != cases[index].error ||
		    chip.chipset != cases[index].chipset ||
		    strcmp(chip.name, cases[index].name) != 0 ||
		    chip.gsp_tu102 != cases[index].gsp_tu102) {
			printf("FAIL %s: error %d chip %03x %s gsp %d\n",
			    cases[index].what,
			    error,
			    chip.chipset,
			    chip.name,
			    chip.gsp_tu102);
			failures++;
		}
	}

	/* Reports the verdict. */
	if (failures != 0U)
		return 1;

	printf("PASS nvrtx identify: %u cases\n", index);
	return 0;
}
