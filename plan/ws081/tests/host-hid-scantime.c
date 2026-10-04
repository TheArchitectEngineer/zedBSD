/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the touch screen's Scan Time (ws081-p002,
 * plan/ws081/design.md section 2.2).
 *
 * The parser (usb-hid.c) must find a Touch Screen's Scan Time, its range
 * and its unit (seconds with an exponent, or 100 us when the descriptor
 * gives no time, including a length unit left over from the fingers), and
 * hand its value on with the report.  The state machine (hid-touch.c) must
 * declare MSC_TIMESTAMP, count the Scan Time in microseconds across wraps,
 * start again at zero after a second of silence, stamp a split frame with
 * its first report's time and a frame a lost report cut short with its own,
 * write every frame while a finger touches, write nothing while none does,
 * and behave as before for a screen without a Scan Time.  The driver files
 * are compiled freestanding, as the kernel compiles them.
 */

#include <drivers/generic/hid-digitizer.h>
#include <drivers/generic/hid-touch.h>
#include <uapi/input.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The largest synthetic descriptor. */
#define DESCRIPTOR_MAX	512U

/* The report ID and the size of the synthetic screen's report: ID, two fingers of six bytes, Scan Time, count. */
#define TOUCH_REPORT_ID	1U
#define REPORT_SIZE	16U

/* The Scan Time's unit items the descriptors are built with. */
enum scan_unit {
	UNIT_NONE,
	UNIT_SECONDS_E4,
	UNIT_SECONDS_E6,
	UNIT_LEFTOVER_CM,
	UNIT_ABSENT
};

/* The number of checks run, and of those that failed. */
static unsigned checks;
static unsigned failures;

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void *kern_memcpy(void *destination, const void *source, size_t length);
void *kern_memset(void *destination, int value, size_t length);

/* Allocates a zeroed object for the parser, as the kernel heap does. */
void *
kern_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

/* Frees an object of the parser. */
void
kern_free(void *pointer)
{
	free(pointer);
}

/* Copies bytes for the parser, as the kernel's kcrt does. */
void *
kern_memcpy(void *destination, const void *source, size_t length)
{
	return memcpy(destination, source, length);
}

/* Fills bytes for the parser, as the kernel's kcrt does. */
void *
kern_memset(void *destination, int value, size_t length)
{
	return memset(destination, value, length);
}

/* Counts one check and prints it when it fails. */
static void
check(int passed, const char *format, ...)
{
	va_list arguments;

	checks++;
	if (passed)
		return;
	failures++;
	fprintf(stderr, "FAIL: ");
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fprintf(stderr, "\n");
}

/* Appends bytes to a descriptor. */
static void
put(uint8_t *bytes, size_t *length, const uint8_t *items, size_t size)
{
	memcpy(bytes + *length, items, size);
	*length += size;
}

/*
 * Builds a touch screen of two fingers per report (Tip Switch, Contact
 * Identifier, X, Y), a Scan Time 0..65535 with the unit asked for, and a
 * Contact Count.  UNIT_LEFTOVER_CM gives the fingers' X and Y a centimetre
 * unit that is never reset; UNIT_ABSENT leaves the Scan Time out (a
 * constant word of 16 bits takes its place).
 */
static size_t
build(uint8_t *bytes, enum scan_unit unit)
{
	static const uint8_t open[] = {
		0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01, 0x85, TOUCH_REPORT_ID,
	};
	static const uint8_t finger_head[] = {
		0x09, 0x22, 0xa1, 0x02,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x09, 0x42, 0x81, 0x02,
		0x95, 0x07, 0x81, 0x03,
		0x75, 0x08, 0x95, 0x01, 0x25, 0x7f, 0x09, 0x51, 0x81, 0x02,
		0x05, 0x01, 0x75, 0x10, 0x26, 0xff, 0x0f,
	};
	static const uint8_t centimetres[] = {
		0x55, 0x0e, 0x65, 0x11, 0x35, 0x00, 0x46, 0x70, 0x08,
	};
	static const uint8_t finger_tail[] = {
		0x09, 0x30, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0x05, 0x0d, 0xc0,
	};
	static const uint8_t unit_none[] = { 0x65, 0x00, 0x55, 0x00 };
	static const uint8_t unit_e4[] = { 0x66, 0x01, 0x10, 0x55, 0x0c };
	static const uint8_t unit_e6[] = { 0x66, 0x01, 0x10, 0x55, 0x0a };
	static const uint8_t scan_time[] = {
		0x75, 0x10, 0x95, 0x01, 0x27, 0xff, 0xff, 0x00, 0x00, 0x09, 0x56, 0x81, 0x02,
	};
	static const uint8_t no_scan_time[] = { 0x75, 0x10, 0x95, 0x01, 0x81, 0x03 };
	static const uint8_t count_and_close[] = {
		0x75, 0x08, 0x95, 0x01, 0x25, 0x10, 0x09, 0x54, 0x81, 0x02, 0xc0,
	};
	size_t length;
	int finger;

	length = 0;
	put(bytes, &length, open, sizeof(open));
	for (finger = 0; finger < 2; finger++) {
		put(bytes, &length, finger_head, sizeof(finger_head));
		if (unit == UNIT_LEFTOVER_CM)
			put(bytes, &length, centimetres, sizeof(centimetres));
		put(bytes, &length, finger_tail, sizeof(finger_tail));
	}
	if (unit == UNIT_NONE)
		put(bytes, &length, unit_none, sizeof(unit_none));
	if (unit == UNIT_SECONDS_E4)
		put(bytes, &length, unit_e4, sizeof(unit_e4));
	if (unit == UNIT_SECONDS_E6)
		put(bytes, &length, unit_e6, sizeof(unit_e6));
	if (unit == UNIT_ABSENT)
		put(bytes, &length, no_scan_time, sizeof(no_scan_time));
	else
		put(bytes, &length, scan_time, sizeof(scan_time));
	put(bytes, &length, count_and_close, sizeof(count_and_close));
	return length;
}

/* Builds one report of the synthetic screen: finger 0 touching at x, y, the Scan Time and a count of 1. */
static void
build_report(uint8_t *report, int x, int y, unsigned scan)
{
	memset(report, 0, REPORT_SIZE);
	report[0] = TOUCH_REPORT_ID;
	report[1] = 1;
	report[2] = 7;
	report[3] = (uint8_t)x;
	report[4] = (uint8_t)(x >> 8);
	report[5] = (uint8_t)y;
	report[6] = (uint8_t)(y >> 8);
	report[13] = (uint8_t)scan;
	report[14] = (uint8_t)(scan >> 8);
	report[15] = 1;
}

/* The parser: presence, range, unit, and the value handed on; the description's capability. */
static void
test_parser(void)
{
	static const enum scan_unit units[] = { UNIT_NONE, UNIT_SECONDS_E4, UNIT_SECONDS_E6, UNIT_LEFTOVER_CM, UNIT_ABSENT };
	static const uint32_t unit_ns[] = { 100000U, 100000U, 1000U, 100000U, 0U };
	struct hid_report_layout *layout;
	struct hid_report_touch_info touch;
	struct hid_touch_description description;
	struct hid_report_input decoded;
	uint8_t bytes[DESCRIPTOR_MAX];
	uint8_t report[REPORT_SIZE];
	size_t length;
	size_t index;
	size_t u;
	int found;
	int error;

	for (u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
		length = build(bytes, units[u]);
		error = drv_hid_report_layout_parse(bytes, length, &layout);
		check(error == 0, "descriptor %zu parses (error %d)", u, error);
		if (error != 0)
			continue;
		memset(&touch, 0, sizeof(touch));
		error = drv_hid_report_layout_get_touch(layout, &touch);
		check(error == 0 && touch.contacts == 2 && touch.count_present, "descriptor %zu has the touch screen", u);

		/* The Scan Time's presence, range and unit. */
		if (units[u] == UNIT_ABSENT) {
			check(!touch.scan_time_present, "descriptor %zu has no Scan Time", u);
		} else {
			check(touch.scan_time_present, "descriptor %zu has a Scan Time", u);
			check(touch.scan_time_maximum == 65535, "descriptor %zu Scan Time maximum %d", u, touch.scan_time_maximum);
			check(touch.scan_time_unit_ns == unit_ns[u], "descriptor %zu Scan Time unit %u ns, wanted %u", u,
			      touch.scan_time_unit_ns, unit_ns[u]);
		}

		/* The description declares MSC_TIMESTAMP with a Scan Time, and eight capabilities without. */
		error = drv_hid_touch_describe(&touch, &description);
		check(error == 0, "descriptor %zu describes", u);
		found = 0;
		for (index = 0; index < description.capability_count; index++) {
			if (description.capabilities[index].type == EV_MSC && description.capabilities[index].code == MSC_TIMESTAMP)
				found = 1;
		}
		if (units[u] == UNIT_ABSENT) {
			check(description.capability_count == 8 && !found, "descriptor %zu: eight capabilities, no MSC", u);
		} else {
			check(description.capability_count == 9 && found, "descriptor %zu: nine capabilities with MSC_TIMESTAMP", u);
		}

		/* A report's Scan Time is handed on with its fingers. */
		build_report(report, 100, 200, 0xbeef);
		error = drv_hid_report_decode(layout, report, sizeof(report), &decoded);
		check(error == 0, "descriptor %zu report decodes", u);
		found = 0;
		for (index = 0; index < decoded.value_count; index++) {
			if (decoded.values[index].type == HID_REPORT_TYPE_TOUCH &&
			    decoded.values[index].code == HID_TOUCH_SCAN_TIME_CODE &&
			    decoded.values[index].value == 0xbeef)
				found = 1;
		}
		check(found == (units[u] != UNIT_ABSENT), "descriptor %zu Scan Time value handed on: %d", u, found);
		drv_hid_report_layout_destroy(layout);
	}
}

/* Starts a report for the state machine directly, as the injector builds it. */
static void
report_start(struct hid_report_input *report, int count, int scan_present, int scan)
{
	memset(report, 0, sizeof(*report));
	report->report_id = TOUCH_REPORT_ID;
	report->values[report->value_count].type = HID_REPORT_TYPE_TOUCH;
	report->values[report->value_count].code = HID_TOUCH_CONTACT_COUNT_CODE;
	report->values[report->value_count].value = count;
	report->value_count++;
	if (scan_present) {
		report->values[report->value_count].type = HID_REPORT_TYPE_TOUCH;
		report->values[report->value_count].code = HID_TOUCH_SCAN_TIME_CODE;
		report->values[report->value_count].value = scan;
		report->value_count++;
	}
}

/* Adds one finger (in the report's place `place`) to a report. */
static void
report_finger(struct hid_report_input *report, unsigned place, int contact_id, int tip, int x, int y)
{
	static const unsigned items[] = { HID_TOUCH_ITEM_TIP, HID_TOUCH_ITEM_CONFIDENCE, HID_TOUCH_ITEM_CONTACT_ID, HID_TOUCH_ITEM_X, HID_TOUCH_ITEM_Y };
	int values[5];
	size_t index;

	values[0] = tip;
	values[1] = 1;
	values[2] = contact_id;
	values[3] = x;
	values[4] = y;
	for (index = 0; index < 5; index++) {
		report->values[report->value_count].type = HID_REPORT_TYPE_TOUCH;
		report->values[report->value_count].code = HID_TOUCH_CODE(place, items[index]);
		report->values[report->value_count].value = values[index];
		report->value_count++;
	}
}

/* Finds an output's MSC_TIMESTAMP (-1 for none) and checks it comes right before SYN_REPORT. */
static long
timestamp_of(const struct hid_touch_output *output)
{
	size_t index;

	for (index = 0; index < output->event_count; index++) {
		if (output->events[index].type != EV_MSC)
			continue;
		if (index + 1 >= output->event_count || output->events[index + 1].type != EV_SYN)
			return -2;
		return (long)(uint32_t)output->events[index].value;
	}
	return -1;
}

/* Sets up a state machine of ten slots with a Scan Time of a range and unit. */
static void
start(struct hid_touch_state *state, int scan_present, int32_t maximum, uint32_t unit_ns)
{
	struct hid_report_touch_info info;

	memset(&info, 0, sizeof(info));
	info.scan_time_present = scan_present;
	info.scan_time_maximum = maximum;
	info.scan_time_unit_ns = unit_ns;
	drv_hid_touch_reset(state, 10);
	drv_hid_touch_set_scan_time(state, &info);
}

/*
 * One finger through a screen with a Scan Time of 100 us units: the first
 * report stamps 0, the steps add up, a wrap counts on, an unchanged frame
 * with the finger down is still written, the lift is stamped, an empty
 * report with no finger writes nothing, and a second of silence starts at
 * zero again.
 */
static void
test_counting(void)
{
	struct hid_touch_state state;
	struct hid_report_input report;
	struct hid_touch_output output;
	uint64_t now;
	long stamp;
	int error;

	start(&state, 1, 65535, 100000U);
	now = 5000;

	/* Down at Scan Time 1000: stamped 0. */
	report_start(&report, 1, 1, 1000);
	report_finger(&report, 0, 3, 1, 100, 200);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 0, "first report stamped 0, got %ld", stamp);

	/* Moved 83 units later: 8300 us. */
	now += 8;
	report_start(&report, 1, 1, 1083);
	report_finger(&report, 0, 3, 1, 110, 200);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 8300, "second report stamped 8300, got %ld", stamp);

	/* Not moved: MSC_TIMESTAMP and SYN_REPORT alone. */
	now += 8;
	report_start(&report, 1, 1, 1166);
	report_finger(&report, 0, 3, 1, 110, 200);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && output.event_count == 2 && stamp == 16600, "unchanged frame: %zu events, stamp %ld", output.event_count, stamp);

	/* Across the wrap: 65530 is 64364 units on, 4 is 10 more. */
	now += 8;
	report_start(&report, 1, 1, 65530);
	report_finger(&report, 0, 3, 1, 120, 200);
	(void)drv_hid_touch_translate_at(&state, &report, now, &output);
	now += 8;
	report_start(&report, 1, 1, 4);
	report_finger(&report, 0, 3, 1, 130, 200);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 16600 + 6436400 + 1000, "wrap counted on: %ld", stamp);

	/* The lift is stamped. */
	now += 8;
	report_start(&report, 0, 1, 90);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 16600 + 6436400 + 1000 + 8600, "lift stamped: %ld", stamp);

	/* Another empty report with no finger writes nothing. */
	now += 8;
	report_start(&report, 0, 1, 173);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	check(error == 0 && output.event_count == 0, "empty report with no finger writes nothing (%zu events)", output.event_count);

	/* After a second of silence the count starts at zero again, whatever the Scan Time did. */
	now += 1000;
	report_start(&report, 1, 1, 40000);
	report_finger(&report, 0, 4, 1, 300, 300);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 0, "a second of silence starts at 0: %ld", stamp);

	/* 999 ms of silence does not. */
	now += 999;
	report_start(&report, 1, 1, 40010);
	report_finger(&report, 0, 4, 1, 310, 300);
	error = drv_hid_touch_translate_at(&state, &report, now, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 1000, "999 ms of silence counts on: %ld", stamp);

	/* Without a host time the count never starts again. */
	report_start(&report, 1, 1, 40020);
	report_finger(&report, 0, 4, 1, 320, 300);
	error = drv_hid_touch_translate(&state, &report, &output);
	stamp = timestamp_of(&output);
	check(error == 0 && stamp == 2000, "no host time counts on: %ld", stamp);
}

/*
 * Units and ranges: a 1 us unit of a Scan Time 0..999 wraps at 1000, and a
 * screen whose Scan Time cannot count (maximum 0) writes none.
 */
static void
test_units(void)
{
	struct hid_touch_state state;
	struct hid_report_input report;
	struct hid_touch_output output;
	long stamp;

	start(&state, 1, 999, 1000U);
	report_start(&report, 1, 1, 990);
	report_finger(&report, 0, 1, 1, 1, 1);
	(void)drv_hid_touch_translate_at(&state, &report, 100, &output);
	report_start(&report, 1, 1, 5);
	report_finger(&report, 0, 1, 1, 2, 1);
	(void)drv_hid_touch_translate_at(&state, &report, 101, &output);
	stamp = timestamp_of(&output);
	check(stamp == 15, "1 us units wrapping at 1000: %ld", stamp);

	start(&state, 1, 0, 100000U);
	report_start(&report, 1, 1, 5);
	report_finger(&report, 0, 1, 1, 2, 1);
	(void)drv_hid_touch_translate_at(&state, &report, 100, &output);
	check(timestamp_of(&output) == -1, "a Scan Time of maximum 0 writes no MSC_TIMESTAMP");
}

/*
 * A split frame (two fingers a report, three fingers) is stamped with its
 * first report's time, and a lost report's frame, written early when the
 * next count comes, keeps its own time while the new frame takes the new
 * one.
 */
static void
test_hybrid(void)
{
	struct hid_touch_state state;
	struct hid_report_input report;
	struct hid_touch_output output;
	long stamp;

	start(&state, 1, 65535, 100000U);

	/* Three fingers over two reports; the second report's Scan Time is later (a slow screen). */
	report_start(&report, 3, 1, 500);
	report_finger(&report, 0, 1, 1, 10, 10);
	report_finger(&report, 1, 2, 1, 20, 20);
	(void)drv_hid_touch_translate_at(&state, &report, 10, &output);
	check(output.event_count == 0, "first report of a split frame writes nothing");
	report_start(&report, 0, 1, 510);
	report_finger(&report, 0, 3, 1, 30, 30);
	(void)drv_hid_touch_translate_at(&state, &report, 11, &output);
	stamp = timestamp_of(&output);
	check(stamp == 0, "split frame stamped with its first report's time: %ld", stamp);

	/* The next frame's first report comes, its second is lost, the next count closes it early. */
	report_start(&report, 3, 1, 600);
	report_finger(&report, 0, 1, 1, 11, 10);
	report_finger(&report, 1, 2, 1, 21, 20);
	(void)drv_hid_touch_translate_at(&state, &report, 20, &output);
	report_start(&report, 3, 1, 700);
	report_finger(&report, 0, 1, 1, 12, 10);
	report_finger(&report, 1, 2, 1, 22, 20);
	(void)drv_hid_touch_translate_at(&state, &report, 30, &output);
	stamp = timestamp_of(&output);
	check(stamp == 10000, "the frame a lost report cut short keeps its time: %ld", stamp);
	report_start(&report, 0, 1, 710);
	report_finger(&report, 0, 3, 1, 32, 30);
	(void)drv_hid_touch_translate_at(&state, &report, 31, &output);
	stamp = timestamp_of(&output);
	check(stamp == 20000, "the new frame has its own time: %ld", stamp);
}

/*
 * A screen without a Scan Time behaves as before: no MSC_TIMESTAMP, and an
 * unchanged frame writes nothing even while a finger touches.  A screen
 * with one but a report without it keeps the last time.
 */
static void
test_without(void)
{
	struct hid_touch_state state;
	struct hid_report_input report;
	struct hid_touch_output output;

	start(&state, 0, 0, 0U);
	report_start(&report, 1, 1, 123);
	report_finger(&report, 0, 1, 1, 5, 5);
	(void)drv_hid_touch_translate_at(&state, &report, 1, &output);
	check(timestamp_of(&output) == -1 && output.event_count > 0, "no Scan Time: no MSC_TIMESTAMP");
	report_start(&report, 1, 1, 456);
	report_finger(&report, 0, 1, 1, 5, 5);
	(void)drv_hid_touch_translate_at(&state, &report, 2, &output);
	check(output.event_count == 0, "no Scan Time: an unchanged frame writes nothing (%zu events)", output.event_count);
}

/*
 * The event list holds the worst report: a lost report's frame of sixteen
 * fingers written early and a new frame of sixteen other fingers, each with
 * MSC_TIMESTAMP.
 */
static void
test_capacity(void)
{
	struct hid_touch_state state;
	struct hid_report_input report;
	struct hid_touch_output output;
	struct hid_report_touch_info info;
	unsigned finger;
	int error;

	memset(&info, 0, sizeof(info));
	info.scan_time_present = 1;
	info.scan_time_maximum = 65535;
	info.scan_time_unit_ns = 100000U;
	drv_hid_touch_reset(&state, HID_TOUCH_SLOTS_MAX);
	drv_hid_touch_set_scan_time(&state, &info);

	/* Sixteen fingers, a frame of seventeen (one never comes). */
	report_start(&report, 17, 1, 1);
	for (finger = 0; finger < HID_TOUCH_CONTACTS_MAX; finger++)
		report_finger(&report, finger, (int)finger, 1, 100 + (int)finger, 100);
	error = drv_hid_touch_translate_at(&state, &report, 1, &output);
	check(error == 0 && output.event_count == 0, "sixteen of seventeen fingers wait (error %d)", error);

	/* A new count: the sixteen are written, and the new frame of sixteen others lifts them. */
	report_start(&report, 16, 1, 100);
	for (finger = 0; finger < HID_TOUCH_CONTACTS_MAX; finger++)
		report_finger(&report, finger, 100 + (int)finger, 1, 200 + (int)finger, 200);
	error = drv_hid_touch_translate_at(&state, &report, 2, &output);
	check(error == 0, "the worst report fits the event list (error %d, %zu events of %u)", error, output.event_count,
	      (unsigned)HID_TOUCH_EVENT_MAX);
}

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	test_parser();
	test_counting();
	test_units();
	test_hybrid();
	test_without();
	test_capacity();
	if (failures != 0) {
		printf("host-hid-scantime: %u of %u checks FAILED\n", failures, checks);
		return 1;
	}
	printf("host-hid-scantime: ok (%u checks)\n", checks);
	return 0;
}
