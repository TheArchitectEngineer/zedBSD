#!/usr/bin/env python3
# Generates src/drivers/gpu/nvrtx/products.c from the PCI ID repository (ws242-p001).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: python3 -I plan/ws242/tests/nvrtx-products-gen.py PCI.IDS > src/drivers/gpu/nvrtx/products.c
# PCI.IDS is https://pci-ids.ucw.cz/v2.2/pci.ids (the file of 2026-10-15 was used).  Every function of vendor
# 10de whose chip label is a Turing, Ampere or Ada chip the driver knows is listed, except the HD audio, USB and
# UCSI functions: its device ID, the chip number, whether it is a laptop part, and the product name in brackets.
import re
import sys

CHIPS = {
    "TU102": 0x162, "TU104": 0x164, "TU106": 0x166, "TU116": 0x168, "TU117": 0x167,
    "GA102": 0x172, "GA103": 0x173, "GA104": 0x174, "GA106": 0x176, "GA107": 0x177,
    "AD102": 0x192, "AD103": 0x193, "AD104": 0x194, "AD106": 0x196, "AD107": 0x197,
}


def entries(path):
    vendor = False
    for line in open(path, encoding="utf-8", errors="replace"):
        if re.match(r"^[0-9a-f]{4} ", line):
            vendor = line.startswith("10de ")
            continue
        if not vendor:
            continue
        match = re.match(r"^\t([0-9a-f]{4})  (\S+)(.*)$", line.rstrip("\n"))
        if not match:
            continue
        device, label, rest = match.groups()
        chip = re.match(r"^(TU10[246]|TU11[67]|GA10[2-7]|AD10[2-7])([A-Z]*)$", label)
        if not chip:
            continue
        if re.search(r"Audio|USB|UCSI", rest):
            continue
        name = None
        bracket = re.search(r"\[(.*)\]", rest)
        if bracket:
            name = bracket.group(1)
        laptop = "M" in chip.group(2) or (name is not None and re.search(r"Laptop|Mobile|Max-Q", name) is not None)
        yield int(device, 16), CHIPS[chip.group(1)], laptop, name


def c_string(text):
    if text is None:
        return "NULL"
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    rows = sorted(entries(sys.argv[1]))
    print("""/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The products of the chips the nvrtx driver knows.
 *
 * Every PCI device ID of a Turing (TU102, TU104, TU106, TU116, TU117),
 * Ampere (GA102 to GA107) or Ada (AD102 to AD107) GPU function: desktop and
 * laptop boards of the GeForce RTX 20, 30 and 40 and GTX 16, and the
 * workstation, data-centre and mining boards built on the same chips.  The
 * IDs and names are facts from the PCI ID repository (pci-ids.ucw.cz,
 * pci.ids of 2026-10-15), written out by plan/ws242/tests/nvrtx-products-gen.py;
 * an unnamed entry carries no name.  The table only names a GPU in the stage
 * marks: the chip, and with it the driver's path, is decided by the identity
 * register, so a board missing here is still driven.
 */

#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

/*
 * The products, by device ID in increasing order.
 *
 * Each row is the device ID, the chip number in the identity register, the
 * laptop flag and the product name.
 */
static const struct nvrtx_product products_table[] = {""")
    for index, (device, chip, laptop, name) in enumerate(rows):
        comma = "," if index + 1 < len(rows) else ""
        print("\t{ 0x%04xU, 0x%03xU, %d, %s }%s" % (device, chip, 1 if laptop else 0, c_string(name), comma))
    print("""};

/*
 * Finds the product a PCI device ID names.
 *
 * Returns NULL for a device ID the table does not list.
 */
const struct nvrtx_product *
nvrtx_product_find(
	uint16_t device)
{
	size_t low;
	size_t high;
	size_t middle;

	/* Halves the sorted table until the ID is found or the range is empty. */
	low = 0U;
	high = sizeof(products_table) / sizeof(products_table[0]);
	while (low < high) {
		middle = low + (high - low) / 2U;

		/* The ID is in the upper half. */
		if (products_table[middle].device < device) {
			low = middle + 1U;
			continue;
		}

		/* The ID is in the lower half. */
		if (products_table[middle].device > device) {
			high = middle;
			continue;
		}

		/* Succeeded: the product with this ID. */
		return &products_table[middle];
	}

	/* No product has this ID. */
	return NULL;
}

/*
 * Reports how many products the table lists.
 */
size_t
nvrtx_product_count(void)
{
	/* The table's length. */
	return sizeof(products_table) / sizeof(products_table[0]);
}

/*
 * Reports one product of the table by its index.
 */
const struct nvrtx_product *
nvrtx_product_at(
	size_t index)
{
	/* An index past the end names nothing. */
	if (index >= sizeof(products_table) / sizeof(products_table[0]))
		return NULL;

	/* Succeeded: the product at the index. */
	return &products_table[index];
}""")


main()
