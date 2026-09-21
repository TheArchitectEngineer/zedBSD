/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the OpRegion VBT locator (drv_i915_opregion_locate_vbt()
 * in display/vbt.c) against the OpRegion the target laptop really has
 * (plan/ws031/display-ref/i915_opregion.bin, taken under Linux) and the
 * VBT Linux extracted from it (i915_vbt.bin), plus the other branches of the
 * Linux decision on edited copies.
 *
 *   sh plan/ws031/tests/run-opregion-host-test.sh
 */

#include "host-test.h"

#include "../../display/internal.h"
#include "../../display/vbt-parse.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The ASLS the target reports (setpci 00:02.0 FC.L). */
#define I915_HOST_TARGET_ASLS 0x614e5018U

/*
 * The reference OpRegion, the reference VBT, and a working copy.
 *
 * Filled by main() and edited only in the working copy, one case at a time.
 */
static uint8_t i915_host_opregion[0x2000];
static uint8_t i915_host_vbt[16384];
static uint8_t i915_host_copy[0x2000];

static size_t i915_host_slurp(const char *path, uint8_t *buffer, size_t capacity);

/*
 * Locates the VBT in the target's OpRegion and in edited copies of it.
 */
int
main(void)
{
	struct i915_opregion_info info;
	size_t opregion_bytes;
	size_t vbt_bytes;
	uint32_t vbt_size;
	uint32_t bdb;
	int vbt_signature;
	int bdb_signature;
	int error;
	int status;

	/* Reads the target's OpRegion and VBT. */
	opregion_bytes = i915_host_slurp("plan/ws031/display-ref/i915_opregion.bin", i915_host_opregion, sizeof(i915_host_opregion));
	vbt_bytes = i915_host_slurp("plan/ws031/display-ref/i915_vbt.bin", i915_host_vbt, sizeof(i915_host_vbt));
	i915_host_check(opregion_bytes == 0x2000U && vbt_bytes > 0U, "the target OpRegion (8 KiB) and its VBT are present");

	/* The header: IntelGraphicsMem, 8 KiB, version 2.1. */
	error = drv_i915_opregion_locate_vbt(i915_host_opregion, opregion_bytes, I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 &&
			info.signature_ok != 0 &&
			info.size_kib == 8U &&
			info.major == 2U &&
			info.minor == 1U,
			"TARGET: IntelGraphicsMem, 8 KiB, version 2.1");

	/* The VBT lies outside the OpRegion, at ASLS + RVDA. */
	i915_host_check(info.mboxes == 0x1dU &&
			info.src == I915_OPVBT_RVDA &&
			info.rvda == 0x2000U &&
			info.rvds == 8704U &&
			info.rvda_relative != 0 &&
			info.rvda_inside == 0 &&
			info.vbt_phys == I915_HOST_TARGET_ASLS + 0x2000U,
			"TARGET: the VBT is outside the OpRegion (RVDA 0x2000 relative, 8704 bytes) -> ASLS + 0x2000, not mailbox #4");

	/* The VBT Linux read from there: RVDS bytes, a $VBT header whose size fits, the BDB inside. */
	vbt_size = 0U;
	bdb = 0U;
	memcpy(&vbt_size, i915_host_vbt + 24, 2U);
	memcpy(&bdb, i915_host_vbt + 28, 4U);
	vbt_signature = memcmp(i915_host_vbt, "$VBT", 4U);
	bdb_signature = -1;
	if (bdb < vbt_bytes)
		bdb_signature = memcmp(i915_host_vbt + bdb, "BIOS_DATA_BLOCK", 15U);
	i915_host_check(vbt_bytes == info.rvds &&
			vbt_signature == 0 &&
			vbt_size <= vbt_bytes &&
			vbt_size > bdb &&
			bdb < vbt_bytes &&
			bdb_signature == 0,
			"TARGET: the VBT Linux read from there is RVDS bytes, its $VBT header declares a size that fits (8701 of 8704), BDB inside");
	printf("  target: ver %u.%u mboxes 0x%x rvda 0x%llx rvds %u -> phys 0x%llx | vbt %zu bytes, bdb at %u\n",
	       info.major,
	       info.minor,
	       info.mboxes,
	       (unsigned long long)info.rvda,
	       info.rvds,
	       (unsigned long long)info.vbt_phys,
	       vbt_bytes,
	       bdb);

	/* The runtime mailboxes, read as the running Linux i915 left them when the dump was taken. */
	i915_host_check(info.acpi_drdy == 1U &&
			info.acpi_csts == 0U &&
			info.acpi_chpd == 1U &&
			info.asle_ardy == 1U &&
			info.asle_tche == 2U,
			"MAILBOXES: struct opregion_acpi drdy/csts/chpd at 0x100/0x104/0x1a8 and struct opregion_asle ardy/tche at 0x300/0x308 "
			"read as Linux left them (drdy=1, ardy=1, tche=BLC_EN)");
	printf("  mailboxes: drdy %u csts %u cevt %u chpd %u clid 0x%x | ardy %u aslc %u tche %u\n",
	       info.acpi_drdy,
	       info.acpi_csts,
	       info.acpi_cevt,
	       info.acpi_chpd,
	       info.acpi_clid,
	       info.asle_ardy,
	       info.asle_aslc,
	       info.asle_tche);

	/* Without RVDA / RVDS: mailbox #4, up to the ASLE-ext mailbox. */
	memcpy(i915_host_copy, i915_host_opregion, sizeof(i915_host_copy));
	memset(i915_host_copy + 0x300 + 186, 0, 12U);
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 &&
			info.src == I915_OPVBT_MAILBOX4 &&
			info.vbt_offset == 0x400U &&
			info.vbt_max == 0x1c00U - 0x400U,
			"MBOX4 no RVDA: mailbox #4, up to the ASLE-ext mailbox (MBOX_ASLE_EXT set)");

	/* ... and without the ASLE-ext mailbox, up to the end of the OpRegion. */
	i915_host_copy[0x58] &= (uint8_t)~0x10U;
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 && info.vbt_max == 0x2000U - 0x400U, "MBOX4 without the ASLE-ext mailbox: up to the end of the OpRegion");

	/* Version 2.0: the RVDA is a physical address. */
	memcpy(i915_host_copy, i915_host_opregion, sizeof(i915_host_copy));
	i915_host_copy[22] = 0U;
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 &&
			info.src == I915_OPVBT_RVDA &&
			info.rvda_relative == 0 &&
			info.vbt_phys == 0x2000U,
			"V2.0: RVDA is a physical address (not added to ASLS)");

	/* Version 2.1 with the RVDA inside the OpRegion (rvda 0x1000). */
	memcpy(i915_host_copy, i915_host_opregion, sizeof(i915_host_copy));
	i915_host_copy[0x300 + 186] = 0U;
	i915_host_copy[0x300 + 187] = 0x10U;
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 && info.rvda_inside != 0, "INSIDE 2.1 RVDA inside the OpRegion is flagged (the reference WARNs)");

	/* Without the ASLE mailbox the RVDA is not consulted. */
	memcpy(i915_host_copy, i915_host_opregion, sizeof(i915_host_copy));
	i915_host_copy[0x58] &= (uint8_t)~0x4U;
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == 0 && info.src == I915_OPVBT_MAILBOX4, "NO-ASLE: RVDA ignored without mailbox #3");

	/* A wrong signature and a short copy are refused. */
	memcpy(i915_host_copy, i915_host_opregion, sizeof(i915_host_copy));
	i915_host_copy[0] = 'X';
	error = drv_i915_opregion_locate_vbt(i915_host_copy, sizeof(i915_host_copy), I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == EINVAL, "BAD-SIG refused");
	error = drv_i915_opregion_locate_vbt(i915_host_opregion, 0x1000U, I915_HOST_TARGET_ASLS, &info);
	i915_host_check(error == EINVAL, "SHORT refused");

	/* Reports the tally. */
	status = i915_host_report("opregion_host_test");
	if (status != 0)
		return status;

	/* Succeeded: the locator follows the Linux decision on every case. */
	return 0;
}

/* Reads up to capacity bytes of a file; a missing file reads as empty. */
static size_t
i915_host_slurp(
	const char *path,
	uint8_t *buffer,
	size_t capacity)
{
	FILE *file;
	size_t count;

	/* A missing file reads as empty; the first check reports it. */
	file = fopen(path, "rb");
	if (file == NULL)
		return 0U;

	/* Reads what the file holds. */
	count = fread(buffer, 1U, capacity, file);
	fclose(file);

	/* Succeeded: reports the bytes read. */
	return count;
}
