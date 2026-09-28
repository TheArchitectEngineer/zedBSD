/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the USB HID touch screen (ws079-p012).
 *
 * It builds synthetic report descriptors of touch screens -- ten Finger
 * collections in one report (Tip Switch, Confidence, Contact Identifier, X,
 * Y) with a Contact Count, two fingers per report with a Contact Count (a
 * "hybrid" screen that splits a frame over reports), two fingers without a
 * Contact Count or Contact Identifier, and a pen and a touch screen on one
 * interface -- parses and decodes them with the unchanged driver files, runs
 * the reports through the touch state machine, and checks the multitouch
 * protocol B events: ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X/Y,
 * BTN_TOUCH and ABS_X/ABS_Y of the oldest finger.  The driver files are
 * compiled freestanding into their own objects; the test supplies the kernel
 * allocator and memory functions.
 */

#include <drivers/usb/hid-digitizer.h>
#include <drivers/usb/hid-touch.h>
#include <uapi/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The kernel's error numbers (include/uapi/errno.h): the driver objects are
 * compiled for the kernel, while this test sees the host's <errno.h>.
 */
#define KERNEL_EINVAL	3
#define KERNEL_ENOENT	6

/* The largest synthetic descriptor and report. */
#define DESCRIPTOR_MAX	1024U
#define REPORT_MAX	128U

/* The descriptor variants a touch screen is built with. */
#define WITH_CONFIDENCE	0x01U
#define WITH_CONTACT_ID	0x02U
#define WITH_COUNT	0x04U
#define WITH_PEN	0x08U

/* The report IDs of the touch screen and of the pen. */
#define TOUCH_REPORT_ID	1U
#define PEN_REPORT_ID	2U

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

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

/*
 * A synthetic descriptor being built, and what its reports look like.
 */
struct descriptor {
	uint8_t bytes[DESCRIPTOR_MAX];
	size_t length;
	unsigned fingers;
	unsigned flags;
	/* Bytes one finger takes in the touch report. */
	unsigned finger_bytes;
};

/*
 * One finger to put into a touch report.
 */
struct finger {
	int tip;
	int confidence;
	int contact_id;
	int x;
	int y;
};

/* Reports one failed check. */
static void
check(int condition, const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Appends bytes to a descriptor. */
static void
put(struct descriptor *descriptor, const uint8_t *bytes, size_t length)
{
	if (descriptor->length + length > DESCRIPTOR_MAX) {
		fprintf(stderr, "descriptor too long\n");
		exit(2);
	}
	memcpy(descriptor->bytes + descriptor->length, bytes, length);
	descriptor->length += length;
}

/*
 * Appends one Finger collection: Tip Switch, Confidence (or padding), 6 bits
 * of padding, Contact Identifier 0..127 (8 bits), X 0..4095 over 216 mm and
 * Y 0..4095 over 135 mm (16 bits each).
 */
static void
put_finger(struct descriptor *descriptor)
{
	static const uint8_t open[] = {
		0x05, 0x0d,		/* Usage Page (Digitizer) */
		0x09, 0x22,		/* Usage (Finger) */
		0xa1, 0x02,		/* Collection (Logical) */
		0x15, 0x00,		/*  Logical Minimum (0) */
		0x25, 0x01,		/*  Logical Maximum (1) */
		0x75, 0x01,		/*  Report Size (1) */
		0x95, 0x01,		/*  Report Count (1) */
		0x09, 0x42,		/*  Usage (Tip Switch) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
	};
	static const uint8_t confidence[] = {
		0x09, 0x47,		/*  Usage (Confidence) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
		0x95, 0x06,		/*  Report Count (6) */
		0x81, 0x03,		/*  Input (Constant) */
	};
	static const uint8_t no_confidence[] = {
		0x95, 0x07,		/*  Report Count (7) */
		0x81, 0x03,		/*  Input (Constant) */
	};
	static const uint8_t contact_id[] = {
		0x75, 0x08,		/*  Report Size (8) */
		0x95, 0x01,		/*  Report Count (1) */
		0x25, 0x7f,		/*  Logical Maximum (127) */
		0x09, 0x51,		/*  Usage (Contact Identifier) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
	};
	static const uint8_t position[] = {
		0x05, 0x01,		/*  Usage Page (Generic Desktop) */
		0x75, 0x10,		/*  Report Size (16) */
		0x95, 0x01,		/*  Report Count (1) */
		0x26, 0xff, 0x0f,	/*  Logical Maximum (4095) */
		0x55, 0x0e,		/*  Unit Exponent (-2) */
		0x65, 0x11,		/*  Unit (SI Linear, cm) */
		0x35, 0x00,		/*  Physical Minimum (0) */
		0x46, 0x70, 0x08,	/*  Physical Maximum (2160, i.e. 21.6 cm) */
		0x09, 0x30,		/*  Usage (X) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
		0x46, 0x46, 0x05,	/*  Physical Maximum (1350, i.e. 13.5 cm) */
		0x09, 0x31,		/*  Usage (Y) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
		0x65, 0x00,		/*  Unit (None) */
		0x55, 0x00,		/*  Unit Exponent (0) */
		0x45, 0x00,		/*  Physical Maximum (0) */
		0xc0,			/* End Collection */
	};

	put(descriptor, open, sizeof(open));
	if (descriptor->flags & WITH_CONFIDENCE)
		put(descriptor, confidence, sizeof(confidence));
	else
		put(descriptor, no_confidence, sizeof(no_confidence));
	if (descriptor->flags & WITH_CONTACT_ID)
		put(descriptor, contact_id, sizeof(contact_id));
	put(descriptor, position, sizeof(position));
}

/*
 * Appends the pen of test/host-hid-pen.c in short: a Digitizer collection,
 * report ID 2, In Range and Tip Switch, 6 bits of padding, X 0..21600 and Y
 * 0..13500 (16 bits each), Tip Pressure 0..4095 (16 bits).
 */
static void
put_pen(struct descriptor *descriptor)
{
	static const uint8_t pen[] = {
		0x05, 0x0d,		/* Usage Page (Digitizer) */
		0x09, 0x01,		/* Usage (Digitizer) */
		0xa1, 0x01,		/* Collection (Application) */
		0x85, PEN_REPORT_ID,	/*  Report ID (2) */
		0x09, 0x20,		/*  Usage (Stylus) */
		0xa1, 0x00,		/*  Collection (Physical) */
		0x15, 0x00,		/*   Logical Minimum (0) */
		0x25, 0x01,		/*   Logical Maximum (1) */
		0x75, 0x01,		/*   Report Size (1) */
		0x95, 0x02,		/*   Report Count (2) */
		0x09, 0x32,		/*   Usage (In Range) */
		0x09, 0x42,		/*   Usage (Tip Switch) */
		0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
		0x95, 0x06,		/*   Report Count (6) */
		0x81, 0x03,		/*   Input (Constant) */
		0x05, 0x01,		/*   Usage Page (Generic Desktop) */
		0x75, 0x10,		/*   Report Size (16) */
		0x95, 0x01,		/*   Report Count (1) */
		0x26, 0x60, 0x54,	/*   Logical Maximum (21600) */
		0x09, 0x30,		/*   Usage (X) */
		0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
		0x26, 0xbc, 0x34,	/*   Logical Maximum (13500) */
		0x09, 0x31,		/*   Usage (Y) */
		0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
		0x05, 0x0d,		/*   Usage Page (Digitizer) */
		0x26, 0xff, 0x0f,	/*   Logical Maximum (4095) */
		0x09, 0x30,		/*   Usage (Tip Pressure) */
		0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
		0xc0,			/*  End Collection */
		0xc0,			/* End Collection */
	};

	put(descriptor, pen, sizeof(pen));
}

/*
 * Builds a touch screen of a number of fingers per report: the Touch Screen
 * application collection, report ID 1, the fingers, a Scan Time (which the
 * driver ignores) and, with WITH_COUNT, a Contact Count 0..16, and a feature
 * report 3 with the Contact Count Maximum (which the driver does not read).
 */
static void
build(struct descriptor *descriptor, unsigned fingers, unsigned flags)
{
	static const uint8_t open[] = {
		0x05, 0x0d,		/* Usage Page (Digitizer) */
		0x09, 0x04,		/* Usage (Touch Screen) */
		0xa1, 0x01,		/* Collection (Application) */
		0x85, TOUCH_REPORT_ID,	/*  Report ID (1) */
	};
	static const uint8_t scan_time[] = {
		0x05, 0x0d,		/*  Usage Page (Digitizer) */
		0x15, 0x00,		/*  Logical Minimum (0) */
		0x75, 0x10,		/*  Report Size (16) */
		0x95, 0x01,		/*  Report Count (1) */
		0x26, 0xff, 0x7f,	/*  Logical Maximum (32767) */
		0x09, 0x56,		/*  Usage (Scan Time) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
	};
	static const uint8_t count[] = {
		0x75, 0x08,		/*  Report Size (8) */
		0x25, 0x10,		/*  Logical Maximum (16) */
		0x09, 0x54,		/*  Usage (Contact Count) */
		0x81, 0x02,		/*  Input (Data, Variable, Absolute) */
	};
	static const uint8_t close[] = {
		0x85, 0x03,		/*  Report ID (3) */
		0x75, 0x08,		/*  Report Size (8) */
		0x25, 0x0a,		/*  Logical Maximum (10) */
		0x09, 0x55,		/*  Usage (Contact Count Maximum) */
		0xb1, 0x02,		/*  Feature (Data, Variable, Absolute) */
		0xc0,			/* End Collection */
	};
	unsigned index;

	memset(descriptor, 0, sizeof(*descriptor));
	descriptor->fingers = fingers;
	descriptor->flags = flags;
	descriptor->finger_bytes = 5U;
	if (flags & WITH_CONTACT_ID)
		descriptor->finger_bytes = 6U;
	if (flags & WITH_PEN)
		put_pen(descriptor);
	put(descriptor, open, sizeof(open));
	for (index = 0; index < fingers; index++)
		put_finger(descriptor);
	put(descriptor, scan_time, sizeof(scan_time));
	if (flags & WITH_COUNT)
		put(descriptor, count, sizeof(count));
	put(descriptor, close, sizeof(close));
}

/* Builds one touch report: the fingers given, the rest zero, and the count. */
static size_t
touch_report(const struct descriptor *descriptor, uint8_t *report,
	const struct finger *fingers, unsigned finger_count, int count)
{
	unsigned index;
	size_t offset;
	uint8_t *entry;

	memset(report, 0, REPORT_MAX);
	report[0] = TOUCH_REPORT_ID;
	offset = 1;
	for (index = 0; index < descriptor->fingers; index++) {
		entry = report + offset;
		if (index < finger_count) {
			entry[0] = (uint8_t)(fingers[index].tip ? 1 : 0);
			if (descriptor->flags & WITH_CONFIDENCE)
				entry[0] |= (uint8_t)(fingers[index].confidence ? 2 : 0);
			if (descriptor->flags & WITH_CONTACT_ID) {
				entry[1] = (uint8_t)fingers[index].contact_id;
				entry += 1;
			}
			entry[1] = (uint8_t)fingers[index].x;
			entry[2] = (uint8_t)(fingers[index].x >> 8);
			entry[3] = (uint8_t)fingers[index].y;
			entry[4] = (uint8_t)(fingers[index].y >> 8);
		}
		offset += descriptor->finger_bytes;
	}

	/* The Scan Time, then the count. */
	report[offset] = 0x34;
	report[offset + 1] = 0x12;
	offset += 2;
	if (descriptor->flags & WITH_COUNT) {
		report[offset] = (uint8_t)count;
		offset++;
	}
	return offset;
}

/*
 * The expected events of one step, written as a flat list of
 * (type, code, value) triples.
 */
struct expected {
	const char *name;
	size_t count;
	const int *triples;
};

/* Prints an event list, for a failure. */
static void
print_events(const char *label, const struct hid_touch_output *output)
{
	size_t index;

	fprintf(stderr, "  %s (%zu events):", label, output->event_count);
	for (index = 0; index < output->event_count; index++) {
		fprintf(stderr, " %u/%#x/%d", output->events[index].type,
			output->events[index].code, output->events[index].value);
	}
	fprintf(stderr, "\n");
}

/* Checks an output against expected triples. */
static void
expect_events(const char *name, const struct hid_touch_output *output,
	const int *triples, size_t count)
{
	size_t index;
	int same;

	same = output->event_count == count;
	for (index = 0; same && index < count; index++) {
		if (output->events[index].type != triples[index * 3] ||
		    output->events[index].code != triples[index * 3 + 1] ||
		    output->events[index].value != triples[index * 3 + 2])
			same = 0;
	}
	check(same, name);
	if (!same) {
		fprintf(stderr, "  expected:");
		for (index = 0; index < count; index++)
			fprintf(stderr, " %d/%#x/%d", triples[index * 3],
				triples[index * 3 + 1], triples[index * 3 + 2]);
		fprintf(stderr, "\n");
		print_events("got", output);
	}
}

/* Counts the fingers an output opens. */
static int
count_opened(const struct hid_touch_output *output)
{
	size_t index;
	int count;

	count = 0;
	for (index = 0; index < output->event_count; index++) {
		if (output->events[index].type == EV_ABS &&
		    output->events[index].code == ABS_MT_TRACKING_ID &&
		    output->events[index].value >= 0)
			count++;
	}
	return count;
}

/* Counts the fingers an output lifts. */
static int
count_lifted(const struct hid_touch_output *output)
{
	size_t index;
	int count;

	count = 0;
	for (index = 0; index < output->event_count; index++) {
		if (output->events[index].type == EV_ABS &&
		    output->events[index].code == ABS_MT_TRACKING_ID &&
		    output->events[index].value < 0)
			count++;
	}
	return count;
}

/* Decodes one report and runs it through the touch state machine. */
static void
feed(struct hid_report_layout *layout, struct hid_touch_state *state,
	const uint8_t *report, size_t length, struct hid_touch_output *output)
{
	struct hid_report_input decoded;
	int error;
	int touch;

	error = drv_hid_report_decode(layout, report, length, &decoded);
	check(error == 0, "a touch report decodes");
	touch = drv_hid_touch_report_is_touch(&decoded);
	check(touch == 1, "a touch report is a touch report");
	check(drv_hid_digitizer_report_is_pen(&decoded) == 0,
	      "a touch report is not a pen report");
	error = drv_hid_touch_translate(state, &decoded, output);
	check(error == 0, "a touch report translates");
}

#define S(code, value)	EV_ABS, code, value
#define SYN		EV_SYN, SYN_REPORT, 0

/* The ten-finger screen: capabilities, and a two-finger script through it. */
static void
test_ten_fingers(void)
{
	struct descriptor descriptor;
	struct hid_report_layout *layout;
	struct hid_report_layout_info info;
	struct hid_report_touch_info touch;
	struct hid_touch_description description;
	struct hid_touch_state state;
	struct hid_touch_output output;
	struct finger fingers[10];
	uint8_t report[REPORT_MAX];
	size_t length;
	size_t index;
	int error;
	int found;

	build(&descriptor, 10, WITH_CONFIDENCE | WITH_CONTACT_ID | WITH_COUNT);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "the ten-finger descriptor parses");
	if (error != 0)
		return;

	/* The layout: ten fingers, no capability of its own but EV_SYN. */
	error = drv_hid_report_layout_get_info(layout, &info);
	check(error == 0 && info.touch_contacts == 10, "ten fingers per report");
	check(info.capability_count == 1, "a touch screen alone declares only EV_SYN itself");
	check(info.absolute_axis_count == 0, "a touch screen alone has no axes of its own");
	check(info.pen == HID_REPORT_PEN_NONE, "a touch screen is not a pen");

	/* The touch screen: count, X and Y with their resolution. */
	error = drv_hid_report_layout_get_touch(layout, &touch);
	check(error == 0, "the layout has a touch screen");
	check(touch.contacts == 10 && touch.count_present, "ten fingers with a Contact Count");
	check(touch.x.minimum == 0 && touch.x.maximum == 4095, "X 0..4095");
	check(touch.y.minimum == 0 && touch.y.maximum == 4095, "Y 0..4095");
	check(touch.x.resolution == 19, "X 19 units/mm (4095 over 216 mm)");
	check(touch.y.resolution == 30, "Y 30 units/mm (4095 over 135 mm)");

	/* The device it is published as. */
	error = drv_hid_touch_describe(&touch, &description);
	check(error == 0 && description.slots == 10, "ten slots");
	check(description.capability_count == 9, "nine capabilities (MSC_TIMESTAMP for the Scan Time, ws081-p002)");
	found = 0;
	for (index = 0; index < description.capability_count; index++) {
		if (description.capabilities[index].type == EV_KEY &&
		    description.capabilities[index].code == BTN_TOUCH)
			found |= 1;
		if (description.capabilities[index].type == EV_ABS &&
		    description.capabilities[index].code == ABS_MT_TRACKING_ID)
			found |= 2;
		if (description.capabilities[index].type == EV_SYN)
			found |= 4;
	}
	check(found == 7, "BTN_TOUCH, ABS_MT_TRACKING_ID and EV_SYN are declared");
	check(description.axis_count == 6, "six axes");
	for (index = 0; index < description.axis_count; index++) {
		if (description.axes[index].code == ABS_MT_SLOT)
			check(description.axes[index].info.maximum == 9, "ABS_MT_SLOT 0..9");
		if (description.axes[index].code == ABS_MT_TRACKING_ID)
			check(description.axes[index].info.maximum == 65535 &&
			      description.axes[index].info.value == 0, "ABS_MT_TRACKING_ID 0..65535 at rest at 0");
		if (description.axes[index].code == ABS_MT_POSITION_X)
			check(description.axes[index].info.maximum == 4095 &&
			      description.axes[index].info.resolution == 19, "ABS_MT_POSITION_X like X");
		if (description.axes[index].code == ABS_X)
			check(description.axes[index].info.maximum == 4095, "ABS_X like X");
	}
	drv_hid_touch_reset(&state, description.slots);

	/* 1. Finger 5 touches. */
	memset(fingers, 0, sizeof(fingers));
	fingers[0] = (struct finger){ 1, 1, 5, 100, 200 };
	length = touch_report(&descriptor, report, fingers, 1, 1);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 0),
			S(ABS_MT_POSITION_X, 100), S(ABS_MT_POSITION_Y, 200),
			EV_KEY, BTN_TOUCH, 1, S(ABS_X, 100), S(ABS_Y, 200), SYN };
		expect_events("1: the first finger opens slot 0 with tracking 0", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 2. Finger 5 moves right; finger 9 touches. */
	fingers[0] = (struct finger){ 1, 1, 5, 110, 200 };
	fingers[1] = (struct finger){ 1, 1, 9, 300, 400 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_POSITION_X, 110),
			S(ABS_MT_SLOT, 1), S(ABS_MT_TRACKING_ID, 1),
			S(ABS_MT_POSITION_X, 300), S(ABS_MT_POSITION_Y, 400),
			S(ABS_X, 110), SYN };
		expect_events("2: a move in slot 0, a second finger in slot 1", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 3. Finger 5 lifts (reported up); finger 9 moves. */
	fingers[0] = (struct finger){ 0, 1, 5, 110, 200 };
	fingers[1] = (struct finger){ 1, 1, 9, 310, 390 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, -1),
			S(ABS_MT_SLOT, 1), S(ABS_MT_POSITION_X, 310), S(ABS_MT_POSITION_Y, 390),
			S(ABS_X, 310), S(ABS_Y, 390), SYN };
		expect_events("3: slot 0 lifts, ABS_X/Y follow the finger left", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 4. The same frame again: nothing changed, nothing is written. */
	fingers[0] = (struct finger){ 1, 1, 9, 310, 390 };
	length = touch_report(&descriptor, report, fingers, 1, 1);
	feed(layout, &state, report, length, &output);
	check(output.event_count == 0, "4: an unchanged frame writes nothing");

	/* 5. Finger 5 comes back: the lowest free slot, the next tracking identifier. */
	fingers[0] = (struct finger){ 1, 1, 9, 310, 390 };
	fingers[1] = (struct finger){ 1, 1, 5, 500, 600 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 2),
			S(ABS_MT_POSITION_X, 500), S(ABS_MT_POSITION_Y, 600), SYN };
		expect_events("5: a returning finger takes slot 0 with tracking 2", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 6. A palm: finger 5 with Confidence 0 is lifted. */
	fingers[1] = (struct finger){ 1, 0, 5, 500, 600 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_TRACKING_ID, -1), SYN };
		expect_events("6: a finger without confidence is lifted", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 7. The last finger lifts: its slot is freed and BTN_TOUCH goes to 0. */
	fingers[0] = (struct finger){ 0, 1, 9, 310, 390 };
	length = touch_report(&descriptor, report, fingers, 1, 1);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 1), S(ABS_MT_TRACKING_ID, -1),
			EV_KEY, BTN_TOUCH, 0, SYN };
		expect_events("7: the last finger lifts", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 8. Two fingers at once, then a frame with a count of 0 that forgets them both. */
	fingers[0] = (struct finger){ 1, 1, 20, 1000, 1000 };
	fingers[1] = (struct finger){ 1, 1, 21, 2000, 2000 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 3),
			S(ABS_MT_POSITION_X, 1000), S(ABS_MT_POSITION_Y, 1000),
			S(ABS_MT_SLOT, 1), S(ABS_MT_TRACKING_ID, 4),
			S(ABS_MT_POSITION_X, 2000), S(ABS_MT_POSITION_Y, 2000),
			EV_KEY, BTN_TOUCH, 1, S(ABS_X, 1000), S(ABS_Y, 1000), SYN };
		expect_events("8a: two fingers in one frame", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}
	length = touch_report(&descriptor, report, fingers, 0, 0);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, -1),
			S(ABS_MT_SLOT, 1), S(ABS_MT_TRACKING_ID, -1),
			EV_KEY, BTN_TOUCH, 0, SYN };
		expect_events("8b: a frame of no finger lifts every finger", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 9. Entries past the count are unused, whatever they hold. */
	fingers[0] = (struct finger){ 1, 1, 30, 50, 60 };
	fingers[1] = (struct finger){ 1, 1, 31, 70, 80 };
	length = touch_report(&descriptor, report, fingers, 2, 1);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 5),
			S(ABS_MT_POSITION_X, 50), S(ABS_MT_POSITION_Y, 60),
			EV_KEY, BTN_TOUCH, 1, S(ABS_X, 50), S(ABS_Y, 60), SYN };
		expect_events("9: a stale entry past the count is ignored", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/*
	 * 10. Ten new fingers while finger 30 goes: its slot is kept for its
	 * lift, so nine fingers get slots now and the tenth in the next frame.
	 */
	for (index = 0; index < 10; index++)
		fingers[index] = (struct finger){ 1, 1, 40 + (int)index, 10 * (int)index, 20 };
	length = touch_report(&descriptor, report, fingers, 10, 10);
	feed(layout, &state, report, length, &output);
	check(count_opened(&output) == 9 && count_lifted(&output) == 1, "10a: nine fingers open while the old one lifts");
	feed(layout, &state, report, length, &output);
	check(count_opened(&output) == 1 && count_lifted(&output) == 0, "10b: the tenth finger opens in the freed slot");

	drv_hid_report_layout_destroy(layout);
}

/* A screen of two fingers per report that splits a frame over reports. */
static void
test_hybrid(void)
{
	struct descriptor descriptor;
	struct hid_report_layout *layout;
	struct hid_report_touch_info touch;
	struct hid_touch_description description;
	struct hid_touch_state state;
	struct hid_touch_output output;
	struct finger fingers[2];
	uint8_t report[REPORT_MAX];
	size_t length;
	unsigned step;
	unsigned pass;
	int error;
	int opened;
	int lifted;

	build(&descriptor, 2, WITH_CONFIDENCE | WITH_CONTACT_ID | WITH_COUNT);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "the hybrid descriptor parses");
	if (error != 0)
		return;
	error = drv_hid_report_layout_get_touch(layout, &touch);
	check(error == 0 && touch.contacts == 2 && touch.count_present, "two fingers per report, counted");
	error = drv_hid_touch_describe(&touch, &description);
	check(error == 0 && description.slots == 10, "a counting screen of two fingers per report still gets ten slots");
	drv_hid_touch_reset(&state, description.slots);

	/* 1. Three fingers over two reports: nothing until the last finger. */
	fingers[0] = (struct finger){ 1, 1, 1, 100, 100 };
	fingers[1] = (struct finger){ 1, 1, 2, 200, 200 };
	length = touch_report(&descriptor, report, fingers, 2, 3);
	feed(layout, &state, report, length, &output);
	check(output.event_count == 0, "h1: the first report of a split frame writes nothing");
	fingers[0] = (struct finger){ 1, 1, 3, 300, 300 };
	fingers[1] = (struct finger){ 1, 1, 99, 999, 999 };
	length = touch_report(&descriptor, report, fingers, 2, 0);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 0),
			S(ABS_MT_POSITION_X, 100), S(ABS_MT_POSITION_Y, 100),
			S(ABS_MT_SLOT, 1), S(ABS_MT_TRACKING_ID, 1),
			S(ABS_MT_POSITION_X, 200), S(ABS_MT_POSITION_Y, 200),
			S(ABS_MT_SLOT, 2), S(ABS_MT_TRACKING_ID, 2),
			S(ABS_MT_POSITION_X, 300), S(ABS_MT_POSITION_Y, 300),
			EV_KEY, BTN_TOUCH, 1, S(ABS_X, 100), S(ABS_Y, 100), SYN };
		expect_events("h1: the frame is written when its third finger comes; the entry past it is ignored", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* 2. A frame whose second report is lost: the next count writes what came, lifting nobody. */
	fingers[0] = (struct finger){ 1, 1, 1, 110, 100 };
	fingers[1] = (struct finger){ 1, 1, 2, 210, 200 };
	length = touch_report(&descriptor, report, fingers, 2, 3);
	feed(layout, &state, report, length, &output);
	check(output.event_count == 0, "h2: the first report of a split frame writes nothing");
	length = touch_report(&descriptor, report, fingers, 2, 3);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_POSITION_X, 110),
			S(ABS_MT_SLOT, 1), S(ABS_MT_POSITION_X, 210),
			S(ABS_X, 110), SYN };
		expect_events("h2: a new count writes the open frame without lifting finger 3", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}
	fingers[0] = (struct finger){ 1, 1, 3, 300, 300 };
	length = touch_report(&descriptor, report, fingers, 1, 0);
	feed(layout, &state, report, length, &output);
	check(output.event_count == 0, "h2: finger 3 unchanged completes the frame with nothing to write");

	/* 3. Finger 2 is no longer reported: it lifts. */
	fingers[0] = (struct finger){ 1, 1, 1, 110, 100 };
	fingers[1] = (struct finger){ 1, 1, 3, 300, 300 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_TRACKING_ID, -1), SYN };
		expect_events("h3: a finger the frame leaves out lifts (slot 1 is selected already)", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/*
	 * 4. Twelve new fingers over six reports while fingers 1 and 3 go: the
	 * eight free slots now, the two lifted ones in the next frame, and two
	 * fingers are never tracked.
	 */
	for (pass = 0; pass < 2; pass++) {
		opened = 0;
		lifted = 0;
		for (step = 0; step < 6; step++) {
			fingers[0] = (struct finger){ 1, 1, 50 + 2 * (int)step, 10 * (int)step, 400 };
			fingers[1] = (struct finger){ 1, 1, 51 + 2 * (int)step, 10 * (int)step + 5, 400 };
			length = touch_report(&descriptor, report, fingers, 2, step == 0 ? 12 : 0);
			feed(layout, &state, report, length, &output);
			opened += count_opened(&output);
			lifted += count_lifted(&output);
		}
		if (pass == 0)
			check(opened == 8 && lifted == 2, "h4a: twelve fingers take the eight free slots, two old ones lift");
		else
			check(opened == 2 && lifted == 0, "h4b: the next frame fills the two freed slots");
	}

	drv_hid_report_layout_destroy(layout);
}

/* A screen with neither Contact Count nor Contact Identifier. */
static void
test_plain(void)
{
	struct descriptor descriptor;
	struct hid_report_layout *layout;
	struct hid_report_touch_info touch;
	struct hid_touch_description description;
	struct hid_touch_state state;
	struct hid_touch_output output;
	struct finger fingers[2];
	uint8_t report[REPORT_MAX];
	size_t length;
	int error;

	build(&descriptor, 2, 0);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "the plain descriptor parses");
	if (error != 0)
		return;
	error = drv_hid_report_layout_get_touch(layout, &touch);
	check(error == 0 && touch.contacts == 2 && !touch.count_present, "two fingers, not counted");
	error = drv_hid_touch_describe(&touch, &description);
	check(error == 0 && description.slots == 2, "a screen that does not count has one slot per finger");
	drv_hid_touch_reset(&state, description.slots);

	/* The second entry touches: its place names it. */
	memset(fingers, 0, sizeof(fingers));
	fingers[1] = (struct finger){ 1, 0, 0, 40, 50 };
	length = touch_report(&descriptor, report, fingers, 2, 0);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_SLOT, 0), S(ABS_MT_TRACKING_ID, 0),
			S(ABS_MT_POSITION_X, 40), S(ABS_MT_POSITION_Y, 50),
			EV_KEY, BTN_TOUCH, 1, S(ABS_X, 40), S(ABS_Y, 50), SYN };
		expect_events("p1: an uncounted report is a whole frame", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	/* Its tip goes up: it lifts. */
	fingers[1].tip = 0;
	length = touch_report(&descriptor, report, fingers, 2, 0);
	feed(layout, &state, report, length, &output);
	{
		static const int want[] = { S(ABS_MT_TRACKING_ID, -1), EV_KEY, BTN_TOUCH, 0, SYN };
		expect_events("p2: a tip up lifts it", &output, want, sizeof(want) / sizeof(want[0]) / 3);
	}

	drv_hid_report_layout_destroy(layout);
}

/* A pen and a touch screen on one interface keep apart. */
static void
test_pen_and_touch(void)
{
	struct descriptor descriptor;
	struct hid_report_layout *layout;
	struct hid_report_layout_info info;
	struct hid_report_touch_info touch;
	struct input_capability capability;
	struct input_abs_axis axis;
	struct hid_report_input decoded;
	uint8_t report[REPORT_MAX];
	size_t index;
	int error;
	int mt;

	build(&descriptor, 2, WITH_CONFIDENCE | WITH_CONTACT_ID | WITH_COUNT | WITH_PEN);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "the pen and touch descriptor parses");
	if (error != 0)
		return;
	error = drv_hid_report_layout_get_info(layout, &info);
	check(error == 0 && info.pen == HID_REPORT_PEN_TABLET, "the pen is found");
	check(info.touch_contacts == 2, "the touch screen is found beside it");

	/* The layout's own capabilities are the pen's: no multitouch, the pen's X range. */
	mt = 0;
	for (index = 0; index < info.capability_count; index++) {
		error = drv_hid_report_layout_get_capability(layout, index, &capability);
		if (error == 0 && capability.type == EV_ABS && capability.code >= ABS_MT_SLOT)
			mt = 1;
	}
	check(mt == 0, "the pen's capabilities have no multitouch axis");
	for (index = 0; index < info.absolute_axis_count; index++) {
		error = drv_hid_report_layout_get_absolute_axis(layout, index, &axis);
		if (error == 0 && axis.code == ABS_X)
			check(axis.info.maximum == 21600, "ABS_X of the layout is the pen's");
	}
	error = drv_hid_report_layout_get_touch(layout, &touch);
	check(error == 0 && touch.x.maximum == 4095, "the touch screen keeps its own X range");

	/* A pen report goes to the pen, a touch report to the touch screen. */
	memset(report, 0, sizeof(report));
	report[0] = PEN_REPORT_ID;
	report[1] = 0x03;
	error = drv_hid_report_decode(layout, report, 8, &decoded);
	check(error == 0, "a pen report decodes");
	check(drv_hid_digitizer_report_is_pen(&decoded) == 1, "a pen report is a pen report");
	check(drv_hid_touch_report_is_touch(&decoded) == 0, "a pen report is not a touch report");

	drv_hid_report_layout_destroy(layout);
}

/* The edges: a pen alone has no touch screen, a screen without range is refused, the identifiers wrap. */
static void
test_edges(void)
{
	struct descriptor descriptor;
	struct hid_report_layout *layout;
	struct hid_report_touch_info touch;
	struct hid_touch_description description;
	struct hid_touch_state state;
	struct hid_touch_output output;
	struct finger fingers[2];
	uint8_t report[REPORT_MAX];
	size_t length;
	int error;

	/* A pen alone. */
	memset(&descriptor, 0, sizeof(descriptor));
	put_pen(&descriptor);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "a pen alone parses");
	if (error == 0) {
		error = drv_hid_report_layout_get_touch(layout, &touch);
		check(error == KERNEL_ENOENT, "a pen alone has no touch screen");
		drv_hid_report_layout_destroy(layout);
	}

	/* A description without a position range. */
	memset(&touch, 0, sizeof(touch));
	touch.contacts = 2;
	error = drv_hid_touch_describe(&touch, &description);
	check(error == KERNEL_EINVAL, "a screen without a position range is refused");

	/* The tracking identifiers wrap after 65535. */
	build(&descriptor, 2, WITH_CONFIDENCE | WITH_CONTACT_ID | WITH_COUNT);
	error = drv_hid_report_layout_parse(descriptor.bytes, descriptor.length, &layout);
	check(error == 0, "the wrap descriptor parses");
	if (error != 0)
		return;
	drv_hid_touch_reset(&state, 10);
	state.next_tracking_id = 65535;
	fingers[0] = (struct finger){ 1, 1, 1, 10, 10 };
	fingers[1] = (struct finger){ 1, 1, 2, 20, 20 };
	length = touch_report(&descriptor, report, fingers, 2, 2);
	feed(layout, &state, report, length, &output);
	check(output.event_count > 6 &&
	      output.events[1].value == 65535 &&
	      output.events[5].value == 0, "tracking 65535 is followed by 0");
	drv_hid_report_layout_destroy(layout);
}

int
main(void)
{
	test_ten_fingers();
	test_hybrid();
	test_plain();
	test_pen_and_touch();
	test_edges();
	if (failures != 0) {
		fprintf(stderr, "host-hid-touch: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}
	printf("host-hid-touch: ok (%d checks)\n", checks);
	return 0;
}
