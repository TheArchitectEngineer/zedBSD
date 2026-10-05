/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the raw HID devices' descriptor reading (ws161-p002,
 * src/drivers/generic/hidraw-describe.c): a security key's FIDO
 * descriptor, a boot keyboard's (input 8 bytes, output 1 byte of LEDs),
 * a numbered one, an extended (4-byte) usage, a descriptor without a
 * collection, and descriptors cut short.
 */

#include <drivers/generic/hidraw.h>

#include <stdio.h>
#include <uapi/errno.h>

/* The checks that failed. */
static unsigned failures;

/* Counts and reports a check that does not hold. */
static void
expect(
	int condition,
	const char *what)
{
	if (condition)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

int
main(void)
{
	/* A YubiKey's FIDO interface (the same as the test kernel's loopback key). */
	static const unsigned char fido[] = {
		0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09, 0x20, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
		0x95, 0x40, 0x81, 0x02, 0x09, 0x21, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91,
		0x02, 0xc0
	};
	/* A boot keyboard: 8 modifier bits, a reserved byte, 5 LED bits and 3 of padding out, 6 key bytes. */
	static const unsigned char keyboard[] = {
		0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
		0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
		0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
		0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0
	};
	/* A vendor collection with a report ID and an extended usage (page 0xff00, usage 0x0001). */
	static const unsigned char numbered[] = {
		0x0b, 0x01, 0x00, 0x00, 0xff, 0xa1, 0x01, 0x85, 0x02, 0x75, 0x08, 0x95, 0x10, 0x81, 0x02, 0xc0
	};
	/* No collection. */
	static const unsigned char flat[] = { 0x05, 0x01, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02 };
	/* An item whose data runs past the end, and a long item cut short. */
	static const unsigned char cut[] = { 0x06, 0xd0 };
	static const unsigned char long_cut[] = { 0xfe, 0x10, 0x01, 0x00 };
	struct drv_hidraw_layout layout;
	int error;

	/* The FIDO key: usage f1d0:0001, 64 bytes each way, not numbered. */
	error = drv_hidraw_describe(fido, sizeof(fido), &layout);
	expect(error == 0, "fido: read");
	expect(layout.usage_page == HIDRAW_USAGE_PAGE_FIDO && layout.usage == HIDRAW_USAGE_CTAPHID, "fido: usage");
	expect(layout.input_size == 64U && layout.output_size == 64U, "fido: sizes");
	expect(layout.numbered == 0, "fido: not numbered");

	/* The keyboard: usage 0001:0006, 8 bytes in, 1 byte out. */
	error = drv_hidraw_describe(keyboard, sizeof(keyboard), &layout);
	expect(error == 0, "keyboard: read");
	expect(layout.usage_page == 0x0001U && layout.usage == 0x0006U, "keyboard: usage");
	expect(layout.input_size == 8U && layout.output_size == 1U, "keyboard: sizes");

	/* The numbered vendor collection: the extended usage's own page, 16 bytes in, nothing out. */
	error = drv_hidraw_describe(numbered, sizeof(numbered), &layout);
	expect(error == 0, "numbered: read");
	expect(layout.usage_page == 0xff00U && layout.usage == 0x0001U, "numbered: extended usage");
	expect(layout.numbered == 1, "numbered: numbered");
	expect(layout.input_size == 16U && layout.output_size == 0U, "numbered: sizes");

	/* Nothing to describe, and descriptors cut short. */
	error = drv_hidraw_describe(flat, sizeof(flat), &layout);
	expect(error == ENOENT, "flat: no collection");
	error = drv_hidraw_describe(cut, sizeof(cut), &layout);
	expect(error == EINVAL, "cut: item past the end");
	error = drv_hidraw_describe(long_cut, sizeof(long_cut), &layout);
	expect(error == EINVAL, "long item past the end");
	error = drv_hidraw_describe(fido, 0U, &layout);
	expect(error == ENOENT, "empty: no collection");

	/* The verdict. */
	if (failures != 0U) {
		printf("hidraw-describe-host-test: FAIL (%u)\n", failures);
		return 1;
	}
	printf("hidraw-describe-host-test: PASS\n");
	return 0;
}
