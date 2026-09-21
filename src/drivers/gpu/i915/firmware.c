/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The read-only firmware provider (see firmware.h).
 */

#include "firmware.h"

#include <errno.h>
#include <stddef.h>

/*
 * One embedded firmware image and the name it is requested by.
 *
 * The size is reached through a pointer because the blob's length is a
 * constant defined next to the blob in another translation unit.
 */
struct i915_firmware_entry {
	const char *name;
	const unsigned char *data;
	const unsigned *size;
};

/*
 * The embedded images.
 *
 * Each is a byte-for-byte copy of a fixed reference blob, defined in its own
 * source file.  The two DMC programs are Alder Lake-P's and Tiger Lake's;
 * the two VBTs are explicit, machine-specific tables used only when the
 * BIOS table pins them by subsystem.
 *
 * XXX: the blob sources are not rebuilt yet; they must define these names.
 */
extern const unsigned char drv_i915_firmware_adlp_dmc[];
extern const unsigned drv_i915_firmware_adlp_dmc_size;
extern const unsigned char drv_i915_firmware_tgl_dmc[];
extern const unsigned drv_i915_firmware_tgl_dmc_size;
extern const unsigned char drv_i915_firmware_vbt_dell_latitude_5330[];
extern const unsigned drv_i915_firmware_vbt_dell_latitude_5330_size;
extern const unsigned char drv_i915_firmware_vbt_dell_latitude_5320[];
extern const unsigned drv_i915_firmware_vbt_dell_latitude_5320_size;

/*
 * The name table the requests are served from.
 *
 * It is constant and shared by every device.
 */
static const struct i915_firmware_entry i915_firmware_table[] = {
	{
		"i915/adlp_dmc.bin",
		drv_i915_firmware_adlp_dmc,
		&drv_i915_firmware_adlp_dmc_size
	},
	{
		"i915/tgl_dmc_ver2_12.bin",
		drv_i915_firmware_tgl_dmc,
		&drv_i915_firmware_tgl_dmc_size
	},
	{
		"zedbsd/vbt/dell-latitude-5330-1028-0b02.vbt",
		drv_i915_firmware_vbt_dell_latitude_5330,
		&drv_i915_firmware_vbt_dell_latitude_5330_size
	},
	{
		"zedbsd/vbt/dell-latitude-5320-1028-0a1f.vbt",
		drv_i915_firmware_vbt_dell_latitude_5320,
		&drv_i915_firmware_vbt_dell_latitude_5320_size
	}
};

/*
 * Test checkpoints.
 *
 * The test build defines the request checkpoint to serve a request with an
 * image of its own, or to report one as absent, while a unit test runs; it
 * sets *served when it answered, and the table is not consulted.  It is weak
 * so that a production kernel links without it and the call site is a null
 * test.
 */
extern int drv_i915_firmware_test_request(struct i915_firmware *firmware, const char *name, int *served) __attribute__((weak));

static int i915_firmware_name_equal(const char *left, const char *right);

/*
 * Looks a firmware image up by name, as request_firmware() does.
 *
 * Returns 0 with the image's data and size filled in, or ENOENT with data
 * NULL and size 0 when no image has that name.
 */
int
drv_i915_firmware_request(
	struct i915_firmware *firmware,
	const char *name)
{
	const struct i915_firmware_entry *entry;
	unsigned index;
	unsigned count;
	int served;
	int equal;
	int error;

	/* A test build may serve the request instead of the table. */
	if (drv_i915_firmware_test_request != NULL) {
		served = 0;
		error = drv_i915_firmware_test_request(firmware, name, &served);
		if (served != 0) {
			/* The test's answer stands: an absent image or an error. */
			if (error != 0)
				return error;

			/* Succeeded: the test handed back an image. */
			return 0;
		}
	}

	/* Finds the table image that has the requested name. */
	entry = NULL;
	count = sizeof(i915_firmware_table) / sizeof(i915_firmware_table[0]);
	for (index = 0U; index < count; index++) {
		equal = i915_firmware_name_equal(name, i915_firmware_table[index].name);
		if (equal != 0) {
			entry = &i915_firmware_table[index];
			break;
		}
	}

	/* The image genuinely does not exist: no bytes are handed back. */
	if (entry == NULL) {
		firmware->data = NULL;
		firmware->size = 0U;
		return ENOENT;
	}

	/* Hands back the embedded image. */
	firmware->data = entry->data;
	firmware->size = *entry->size;

	/* Succeeded: the caller holds the embedded image. */
	return 0;
}

/*
 * Drops a firmware handle, as release_firmware() does.
 *
 * The image is static read-only data and is not freed.
 */
void
drv_i915_firmware_release(
	struct i915_firmware *firmware)
{
	/* Forgets the image; the bytes stay where they are. */
	firmware->data = NULL;
	firmware->size = 0U;
}

/* Reports nonzero when two firmware names are the same string. */
static int
i915_firmware_name_equal(
	const char *left,
	const char *right)
{
	/* Skips the common prefix. */
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}

	/* The names are equal when both ended together. */
	if (*left == *right)
		return 1;

	/* The names differ. */
	return 0;
}
