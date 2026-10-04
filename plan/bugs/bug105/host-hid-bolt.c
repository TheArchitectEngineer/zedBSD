/*
 * BUG-105: parses the three HID report descriptors of the Logi Bolt receiver
 * (046d:c548, captured on the 5330's Linux) with the kernel's HID parser and
 * decodes a mouse report (report ID 2: 16 buttons, 16-bit X/Y, wheel, AC pan)
 * and a keyboard report (modifiers and a 112-key bitmap in which usages 0x31
 * and 0x32 both stand for KEY_BACKSLASH).  The third interface is Logitech's
 * HID++ channel: it has no field the driver publishes, so its parse reports
 * EOPNOTSUPP and the driver leaves the interface alone (ws073-p032).
 *
 *   plan/bugs/bug105/run-hid-bolt.sh [build-dir]
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <drivers/generic/hid-report.h>
#include <uapi/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernel's EOPNOTSUPP, which differs from the host's. */
#define KERNEL_EOPNOTSUPP 21

static int failures;

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void *kern_memcpy(void *destination, const void *source, size_t length);
void *kern_memset(void *destination, int value, size_t length);

void *
kern_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

void
kern_free(void *pointer)
{
	free(pointer);
}

void *
kern_memcpy(void *destination, const void *source, size_t length)
{
	return memcpy(destination, source, length);
}

void *
kern_memset(void *destination, int value, size_t length)
{
	return memset(destination, value, length);
}

static void
check(int condition, const char *what)
{
	if (condition)
		return;
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Reads a descriptor file into a buffer; returns its size. */
static size_t
load(const char *path, uint8_t *buffer, size_t limit)
{
	FILE *file;
	size_t size;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	size = fread(buffer, 1, limit, file);
	fclose(file);
	return size;
}

/* Finds a decoded value, or returns 0 with *found clear. */
static int32_t
value_of(const struct hid_report_input *input, uint16_t type, uint16_t code, int *found)
{
	size_t index;

	*found = 0;
	for (index = 0; index < input->value_count; index++) {
		if (input->values[index].type == type && input->values[index].code == code) {
			*found = 1;
			return input->values[index].value;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct hid_report_layout *layouts[3];
	struct hid_report_layout_info info;
	struct hid_report_input decoded;
	uint8_t descriptor[512];
	uint8_t report[8];
	char path[1024];
	size_t size, index;
	int32_t value;
	int error, found;

	if (argc != 2) {
		fprintf(stderr, "usage: host-hid-bolt DIR\n");
		return 2;
	}

	/* Every interface's descriptor parses. */
	for (index = 0; index < 3; index++) {
		snprintf(path, sizeof(path), "%s/logi-bolt-c548-if%zu.rdesc", argv[1], index);
		size = load(path, descriptor, sizeof(descriptor));
		error = drv_hid_report_layout_parse(descriptor, size, &layouts[index]);
		printf("interface %zu: %zu bytes, parse %d", index, size, error);
		if (error == 0) {
			drv_hid_report_layout_get_info(layouts[index], &info);
			printf(", reports %zu, fields %zu, capabilities %zu, report ids %d",
			    info.report_count, info.field_count, info.capability_count, info.uses_report_ids);
		}
		printf("\n");
		if (index == 2)
			check(error == KERNEL_EOPNOTSUPP, "the HID++ interface has nothing to publish");
		else
			check(error == 0, "parse the keyboard and mouse interfaces");
		if (error != 0)
			layouts[index] = NULL;
	}
	if (layouts[0] == NULL || layouts[1] == NULL)
		return 1;

	/* A keyboard report: left Shift, A (usage 0x04) and Non-US # (usage 0x32). */
	{
		uint8_t keys[16];

		memset(keys, 0, sizeof(keys));
		keys[0] = 0x02;
		/* The bitmap starts at byte 1 with usage 0x04. */
		keys[1 + (0x04 - 0x04) / 8] |= (uint8_t)(1U << ((0x04 - 0x04) % 8));
		keys[1 + (0x32 - 0x04) / 8] |= (uint8_t)(1U << ((0x32 - 0x04) % 8));
		error = drv_hid_report_decode(layouts[0], keys, sizeof(keys), &decoded);
		printf("keyboard decode %d: %zu values\n", error, decoded.value_count);
		check(error == 0, "decode the keyboard report");
		value = value_of(&decoded, EV_KEY, KEY_LEFTSHIFT, &found);
		check(found && value == 1, "KEY_LEFTSHIFT is held");
		value = value_of(&decoded, EV_KEY, KEY_A, &found);
		check(found && value == 1, "KEY_A is held");
		value = value_of(&decoded, EV_KEY, KEY_BACKSLASH, &found);
		check(found && value == 1, "Non-US # is KEY_BACKSLASH");
		check(decoded.value_count == 3, "exactly three keys are held");
	}

	/* A mouse report: ID 2, left button, X +5, Y -3, wheel +1, pan 0. */
	memset(report, 0, sizeof(report));
	report[0] = 2;
	report[1] = 0x01;
	report[3] = 5;
	report[5] = (uint8_t)-3;
	report[6] = 0xff;
	report[7] = 1;
	error = drv_hid_report_decode(layouts[1], report, 9, &decoded);
	printf("decode (9 bytes) %d: %zu values\n", error, decoded.value_count);
	/* The descriptor's report 2 is 1 + 2 + 4 + 1 + 1 = 9 bytes. */
	{
		uint8_t full[9] = { 2, 0x01, 0x00, 5, 0x00, 0xfd, 0xff, 1, 0xfe };

		error = drv_hid_report_decode(layouts[1], full, sizeof(full), &decoded);
		printf("decode %d: %zu values\n", error, decoded.value_count);
		check(error == 0, "decode the mouse report");
		for (index = 0; index < decoded.value_count; index++)
			printf("  type %u code %u value %d\n", decoded.values[index].type,
			    decoded.values[index].code, decoded.values[index].value);
		value = value_of(&decoded, EV_REL, REL_X, &found);
		check(found && value == 5, "REL_X is +5");
		value = value_of(&decoded, EV_REL, REL_Y, &found);
		check(found && value == -3, "REL_Y is -3");
		value = value_of(&decoded, EV_KEY, BTN_LEFT, &found);
		check(found && value == 1, "BTN_LEFT is held");
		value = value_of(&decoded, EV_REL, REL_WHEEL, &found);
		check(found && value == 1, "REL_WHEEL is +1");
		value = value_of(&decoded, EV_REL, REL_HWHEEL, &found);
		check(found && value == -2, "AC Pan is REL_HWHEEL -2");
	}

	for (index = 0; index < 3; index++) {
		if (layouts[index] != NULL)
			drv_hid_report_layout_destroy(layouts[index]);
	}
	printf("host-hid-bolt: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
