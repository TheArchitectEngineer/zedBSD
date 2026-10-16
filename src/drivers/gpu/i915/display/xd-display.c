/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The driver stages of the xd display (see xd.h): the start, which
 * chooses the device's description; the south display's kind once the PCH
 * is known; and the read of the firmware's OpRegion and its VBT.
 */

#include "display-internal.h"
#include "xd.h"

#include <hal/hal.h>
#include <kern/kcrt.h>
#include <kern/klog.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

static int i915_xd_map_device(void *context, uint64_t address, size_t size, const volatile uint8_t **bytes);
static void i915_xd_unmap_device(void *context, const volatile uint8_t *bytes, size_t size);
static const char *i915_xd_south_name(int pch_kind);
static const char *i915_xd_vbt_source_name(unsigned source);

/*
 * Prepares the xd display of a device and chooses its description.
 *
 * The description is chosen by the PCI product and revision; the PCH is
 * not known yet, so the platform's own south display is assumed until
 * drv_i915_xd_south() reads it.  A device without a description keeps
 * chosen at zero and only logs it: its display does not use the xd parts.
 */
void
drv_i915_xd_start(
	struct i915_xd *xd,
	struct i915_mmio *mmio,
	struct mutex *mailbox_lock,
	uint16_t product,
	uint8_t revid)
{
	int error;

	/* Starts from nothing chosen and nothing read. */
	kern_memset(xd, 0, sizeof(*xd));

	/* The register window writes the device's registers; the mailbox is serialized with the GT's own users. */
	drv_i915_xd_io_init(&xd->io, mmio, mailbox_lock);

	/* Chooses the description by the product and revision. */
	error = drv_i915_xd_platform_choose(product, revid, 0, 0U, &xd->choice);
	if (error != 0) {
		kern_logf("i915: xd: no description of the device 0x%04x: the xd display is not used\n", (unsigned)product);
		return;
	}

	/* Records and reports the choice. */
	xd->chosen = 1;
	kern_logf("i915: xd: platform %s, display %u, stepping %u (south display %s assumed until the PCH is read)\n",
	    xd->choice.platform->name,
	    (unsigned)xd->choice.platform->display_ver,
	    (unsigned)xd->choice.step,
	    i915_xd_south_name(xd->choice.pch_kind));
}

/*
 * Chooses the description again now that the PCH is known.
 *
 * pch_id is the PCH's PCI device id, or 0 when no PCH was found (a
 * virtual machine): the platform's own south display stays assumed.
 */
void
drv_i915_xd_south(
	struct i915_xd *xd,
	uint16_t product,
	uint8_t revid,
	uint16_t pch_id)
{
	struct i915_xd_choice choice;
	int pch_present;
	int error;

	/* A device without a description has no south display to read. */
	if (!xd->chosen)
		return;

	/* Chooses again with the PCH. */
	pch_present = 0;
	if (pch_id != 0U)
		pch_present = 1;
	error = drv_i915_xd_platform_choose(product, revid, pch_present, pch_id, &choice);
	if (error != 0) {
		kern_logf("i915: xd: the PCH 0x%04x does not go with platform %s (error %d): the description is kept\n",
		    (unsigned)pch_id,
		    xd->choice.platform->name,
		    error);
		return;
	}

	/* Reports a description that changed with the PCH. */
	if (choice.platform != xd->choice.platform)
		kern_logf("i915: xd: the PCH changed the description from %s to %s\n", xd->choice.platform->name, choice.platform->name);

	/* Keeps the choice and reports the south display. */
	xd->choice = choice;
	kern_logf("i915: xd: platform %s, south display %s (%s, PCH 0x%04x)\n",
	    choice.platform->name,
	    i915_xd_south_name(choice.pch_kind),
	    choice.pch_assumed ? "assumed" : "read",
	    (unsigned)pch_id);
}

/*
 * Reads the firmware's OpRegion and the VBT it keeps into the copies.
 *
 * The OpRegion and the raw VBT are mapped read-only for the copy and
 * unmapped again.  Returns what drv_i915_xd_opregion_read() returns; the
 * result is kept in xd->opregion and logged.
 */
int
drv_i915_xd_read_opregion(
	struct i915_xd *xd,
	uint32_t asls,
	uint8_t *opregion_copy,
	uint8_t *vbt_copy,
	size_t vbt_room)
{
	struct i915_xd_opregion_access access;
	const struct i915_xd_opregion_read *read;
	const uint8_t *digest;
	int error;

	/* Reads through read-only device mappings. */
	access.map = i915_xd_map_device;
	access.unmap = i915_xd_unmap_device;
	access.context = NULL;
	error = drv_i915_xd_opregion_read(&access, asls, opregion_copy, vbt_copy, vbt_room, &xd->opregion);

	/* Reports what was found. */
	read = &xd->opregion;
	digest = read->vbt_sha256;
	if (read->present == 0) {
		kern_logf("i915: xd-vbt: no OpRegion (ASLS 0)\n");
	} else if (error != 0) {
		kern_logf("i915: xd-vbt: OpRegion at 0x%08x: no VBT (error %d, mapped %d, version %u.%u, raw VBT status %u)\n",
		    asls,
		    error,
		    read->mapped,
		    (unsigned)read->where.major,
		    (unsigned)read->where.minor,
		    read->raw_status);
	} else {
		kern_logf("i915: xd-vbt: OpRegion at 0x%08x version %u.%u mailboxes 0x%x: VBT from %s, %u bytes, sha256 %02x%02x%02x%02x%02x%02x%02x%02x.. (raw VBT status %u)\n",
		    asls,
		    (unsigned)read->where.major,
		    (unsigned)read->where.minor,
		    read->where.mailboxes,
		    i915_xd_vbt_source_name(read->source),
		    read->vbt_size,
		    digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7],
		    read->raw_status);
	}

	/* Reports a failed read. */
	if (error != 0)
		return error;

	/* Succeeded: the VBT is in vbt_copy. */
	return 0;
}

/* Maps device memory read-only for the OpRegion reader; 0, or EIO. */
static int
i915_xd_map_device(
	void *context,
	uint64_t address,
	size_t size,
	const volatile uint8_t **bytes)
{
	void *mapping;
	int status;

	(void)context;

	/* Asks the HAL for a read-only kernel mapping. */
	mapping = NULL;
	status = hal_space_map_device((hal_physaddr_t)address, size, HAL_SPACE_READ, &mapping);
	if (status != HAL_OK)
		return EIO;
	if (mapping == NULL)
		return EIO;

	/* Succeeded: the memory can be read. */
	*bytes = mapping;
	return 0;
}

/* Gives back a mapping the OpRegion reader made. */
static void
i915_xd_unmap_device(
	void *context,
	const volatile uint8_t *bytes,
	size_t size)
{
	(void)context;

	/* Removes the mapping; nothing is left to do if that fails. */
	(void)hal_space_unmap_device((void *)(uintptr_t)bytes, size);
}

/* Names a south display kind for the log. */
static const char *
i915_xd_south_name(
	int pch_kind)
{
	/* The south displays the descriptions name. */
	if (pch_kind == I915_PCH_ICP)
		return "ICP";
	if (pch_kind == I915_PCH_TGP)
		return "TGP";
	if (pch_kind == I915_PCH_ADP)
		return "ADP";

	/* Any other kind. */
	return "none";
}

/* Names where a VBT was taken from, for the log. */
static const char *
i915_xd_vbt_source_name(
	unsigned source)
{
	/* The raw VBT or mailbox 4. */
	if (source == I915_XD_VBT_FROM_RAW)
		return "the raw VBT";
	if (source == I915_XD_VBT_FROM_MAILBOX)
		return "mailbox 4";

	/* No VBT. */
	return "nowhere";
}
