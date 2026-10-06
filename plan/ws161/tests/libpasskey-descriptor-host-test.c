/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the report descriptor's reading for Linux's raw HID
 * nodes (ws161-p004, userland/base/libpasskey/descriptor.c): a security
 * key's descriptor is FIDO's (with a two-byte page and a usage, or a
 * four-byte usage that carries its page); a keyboard's, a descriptor whose
 * first application collection is another's, one cut inside an item, an
 * empty one and one with a long item before FIDO's are told apart.
 */

#include "userland/base/libpasskey/os.h"

#include <stdio.h>

/* The checks that failed. */
static unsigned failures;

static void expect(int condition, const char *what);

/* Counts and reports a check that does not hold. */
static void
expect(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

/*
 * Runs the checks; the exit status says whether they all held.
 */
int
main(void)
{
	/* A YubiKey 5's FIDO interface: page 0xF1D0, usage 1, an application collection with its reports. */
	static const uint8_t yubikey[] = {
		0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09, 0x20, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
		0x95, 0x40, 0x81, 0x02, 0x09, 0x21, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91,
		0x02, 0xc0
	};
	/* A boot keyboard's beginning: Generic Desktop, Keyboard, an application collection. */
	static const uint8_t keyboard[] = { 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0xc0 };
	/* A four-byte usage that carries FIDO's page. */
	static const uint8_t extended[] = { 0x0b, 0x01, 0x00, 0xd0, 0xf1, 0xa1, 0x01, 0xc0 };
	/* A physical collection of FIDO's usage, then a keyboard's application collection: the application decides. */
	static const uint8_t physical_first[] = { 0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x00, 0xc0, 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0xc0 };
	/* FIDO's page and usage, then a main item (Input) that ends the usage, then the application collection. */
	static const uint8_t usage_ended[] = { 0x06, 0xd0, 0xf1, 0x09, 0x01, 0x81, 0x02, 0xa1, 0x01, 0xc0 };
	/* A long item (three bytes of data) before FIDO's application collection. */
	static const uint8_t long_item[] = { 0xfe, 0x03, 0x10, 0xaa, 0xbb, 0xcc, 0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0xc0 };
	/* Cut inside the usage page's data. */
	static const uint8_t cut[] = { 0x06, 0xd0 };
	int fido;

	/* The key's. */
	fido = pk_os_descriptor_is_fido(yubikey, sizeof(yubikey));
	expect(fido == 1, "a YubiKey's FIDO descriptor");
	fido = pk_os_descriptor_is_fido(extended, sizeof(extended));
	expect(fido == 1, "a four-byte usage with FIDO's page");
	fido = pk_os_descriptor_is_fido(long_item, sizeof(long_item));
	expect(fido == 1, "a long item skipped");

	/* Not a key's. */
	fido = pk_os_descriptor_is_fido(keyboard, sizeof(keyboard));
	expect(fido == 0, "a keyboard");
	fido = pk_os_descriptor_is_fido(physical_first, sizeof(physical_first));
	expect(fido == 0, "the first application collection is a keyboard's");
	fido = pk_os_descriptor_is_fido(usage_ended, sizeof(usage_ended));
	expect(fido == 0, "a usage ended by a main item");
	fido = pk_os_descriptor_is_fido(cut, sizeof(cut));
	expect(fido == 0, "a descriptor cut inside an item");
	fido = pk_os_descriptor_is_fido(cut, 0U);
	expect(fido == 0, "an empty descriptor");

	/* The verdict. */
	if (failures != 0U) {
		printf("libpasskey-descriptor: %u FAILED\n", failures);
		return 1;
	}

	/* Every check held. */
	printf("libpasskey-descriptor: PASS\n");
	return 0;
}
