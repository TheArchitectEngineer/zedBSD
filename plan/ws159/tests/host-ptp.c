/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Precision Touchpad path (ws159-p003).
 *
 * It parses the Latitude 5330's touchpad report descriptor (Synaptics
 * 06CB:CE65, taken from its Linux, plan/ws159/tests/latitude5330-linux/)
 * with the unchanged kernel HID files, and checks: the touch pad with five
 * fingers, one button and the fingers' range and resolution; the feature
 * fields the I2C-HID driver sets (Device Mode, Surface and Button Switch,
 * Latency Mode, Contact Count Maximum, Pad Type) where the Linux log shows
 * them; and, from synthetic reports 3 laid out as the descriptor says, the
 * events of the touch state machine in touch pad mode: one finger down
 * (BTN_TOOL_FINGER), a second (BTN_TOOL_DOUBLETAP), the pad pressed
 * (BTN_LEFT), released, and the fingers lifted.
 *
 *   plan/ws159/tests/run-host-ptp.sh
 */

#include <drivers/generic/hid-report.h>
#include <drivers/generic/hid-touch.h>
#include <uapi/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The report of the fingers and its size in bytes, without its identifier. */
#define PAD_REPORT_ID		3U
#define PAD_REPORT_BYTES	29U

/* Where report 3 keeps its items (bits, after the identifier). */
#define FINGER_BITS		40U
#define SCAN_TIME_BIT		200U
#define CONTACT_COUNT_BIT	216U
#define BUTTON_BIT		224U

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void *kern_memcpy(void *destination, const void *source, size_t length);
void *kern_memset(void *destination, int value, size_t length);
static void check(int condition, const char *what);
static void put_bits(unsigned char *report, unsigned offset, unsigned bits, unsigned value);
static void put_finger(unsigned char *report, unsigned place, unsigned tip, unsigned contact, unsigned x, unsigned y);
static int has_event(const struct hid_touch_output *output, uint16_t type, uint16_t code, int32_t value);
static int run_report(const struct hid_report_layout *layout, struct hid_touch_state *state, const unsigned char *data, struct hid_touch_output *output);
static size_t read_descriptor(const char *path, unsigned char *buffer, size_t capacity);
static void check_layout(const struct hid_report_layout *layout, struct hid_report_touch_info *touch);
static void check_description(const struct hid_report_touch_info *touch, struct hid_touch_description *description);
static void check_frames(const struct hid_report_layout *layout, const struct hid_report_touch_info *touch, const struct hid_touch_description *description);

/* Allocates a zeroed object for the parser, as the kernel heap does. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *object;

	/* The host's allocator stands in for the kernel's. */
	object = calloc(count, size);
	return object;
}

/* Frees an object of the parser. */
void
kern_free(
	void *pointer)
{
	/* The host's allocator stands in for the kernel's. */
	free(pointer);
}

/* Copies bytes for the parser, as the kernel's kcrt does. */
void *
kern_memcpy(
	void *destination,
	const void *source,
	size_t length)
{
	void *copied;

	/* The host's copy stands in for the kernel's. */
	copied = memcpy(destination, source, length);
	return copied;
}

/* Fills bytes for the parser, as the kernel's kcrt does. */
void *
kern_memset(
	void *destination,
	int value,
	size_t length)
{
	void *filled;

	/* The host's fill stands in for the kernel's. */
	filled = memset(destination, value, length);
	return filled;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Stores a value of some bits at a bit offset of a report, least significant bit first. */
static void
put_bits(
	unsigned char *report,
	unsigned offset,
	unsigned bits,
	unsigned value)
{
	unsigned index;

	/* One bit at a time. */
	for (index = 0; index < bits; index++) {
		/* A set bit of the value sets the report's bit. */
		if ((value >> index) & 1U)
			report[(offset + index) / 8U] |= (unsigned char)(1U << ((offset + index) % 8U));
	}
}

/* Writes one finger of report 3: confidence, tip, contact identifier, X and Y. */
static void
put_finger(
	unsigned char *report,
	unsigned place,
	unsigned tip,
	unsigned contact,
	unsigned x,
	unsigned y)
{
	unsigned base;

	/* The finger's first bit. */
	base = place * FINGER_BITS;

	/* Confidence and Tip Switch, the Contact Identifier, X and Y. */
	put_bits(report, base, 1U, 1U);
	put_bits(report, base + 1U, 1U, tip);
	put_bits(report, base + 2U, 3U, contact);
	put_bits(report, base + 8U, 16U, x);
	put_bits(report, base + 24U, 16U, y);
}

/* Tells whether the output has an event, and with which value. */
static int
has_event(
	const struct hid_touch_output *output,
	uint16_t type,
	uint16_t code,
	int32_t value)
{
	size_t index;

	/* Looks through the events in order. */
	for (index = 0; index < output->event_count; index++) {
		/* The same event with the same value. */
		if (output->events[index].type == type &&
		    output->events[index].code == code &&
		    output->events[index].value == value)
			return 1;
	}

	/* No such event. */
	return 0;
}

/* Decodes one report 3 and runs it through the touch state machine. */
static int
run_report(
	const struct hid_report_layout *layout,
	struct hid_touch_state *state,
	const unsigned char *data,
	struct hid_touch_output *output)
{
	static struct hid_report_input decoded;
	unsigned char report[PAD_REPORT_BYTES + 1U];
	int error;

	/* The identifier, then the data. */
	report[0] = PAD_REPORT_ID;
	memcpy(report + 1, data, PAD_REPORT_BYTES);

	/* Decodes the report. */
	error = drv_hid_report_decode(layout, report, sizeof(report), &decoded);
	if (error != 0)
		return error;

	/* Turns it into events. */
	error = drv_hid_touch_translate(state, &decoded, output);
	return error;
}

/* Reads the whole descriptor file. */
static size_t
read_descriptor(
	const char *path,
	unsigned char *buffer,
	size_t capacity)
{
	FILE *stream;
	size_t length;

	/* Opens the file. */
	stream = fopen(path, "rb");
	if (stream == NULL)
		return 0;

	/* Reads as much as fits. */
	length = fread(buffer, 1, capacity, stream);
	fclose(stream);
	return length;
}

/* Checks the touch pad and the features the descriptor gives. */
static void
check_layout(
	const struct hid_report_layout *layout,
	struct hid_report_touch_info *touch)
{
	struct hid_report_feature_info feature;
	int error;

	/* The touch pad: five fingers counted per report, one button, a Scan Time. */
	error = drv_hid_report_layout_get_touch(layout, touch);
	check(error == 0, "the layout has a touch pad");
	check(touch->pad == 1, "the fingers are a touch pad's");
	check(touch->contacts == 5U, "five fingers per report");
	check(touch->buttons == 1U, "one button");
	check(touch->count_present == 1, "a Contact Count");
	check(touch->scan_time_present == 1, "a Scan Time");
	check(touch->x.maximum == 1336 && touch->y.maximum == 760, "X 0..1336, Y 0..760");
	check(touch->x.resolution == 12, "12 units a millimetre across, as Linux has it");

	/* Device Mode: report 4, its first byte. */
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_DEVICE_MODE, &feature);
	check(error == 0 && feature.report_id == 4U && feature.bit_offset == 0U && feature.bit_size == 8U && feature.data_size == 1U,
	      "Device Mode is report 4, one byte");

	/* Surface Switch and Button Switch: report 6, bits 0 and 1. */
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_SURFACE_SWITCH, &feature);
	check(error == 0 && feature.report_id == 6U && feature.bit_offset == 0U && feature.bit_size == 1U, "Surface Switch is report 6, bit 0");
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_BUTTON_SWITCH, &feature);
	check(error == 0 && feature.report_id == 6U && feature.bit_offset == 1U && feature.data_size == 1U, "Button Switch is report 6, bit 1");

	/* Latency Mode: report 13. */
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_LATENCY_MODE, &feature);
	check(error == 0 && feature.report_id == 13U && feature.bit_offset == 0U, "Latency Mode is report 13");

	/* Contact Count Maximum and Pad Type: report 8, four bits each. */
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_CONTACT_COUNT_MAXIMUM, &feature);
	check(error == 0 && feature.report_id == 8U && feature.bit_offset == 0U && feature.bit_size == 4U, "Contact Count Maximum is report 8, bits 0..3");
	error = drv_hid_report_layout_get_feature(layout, HID_REPORT_USAGE_PAD_TYPE, &feature);
	check(error == 0 && feature.report_id == 8U && feature.bit_offset == 4U, "Pad Type is report 8, bits 4..7");
}

/* Checks the device the touch pad is published as. */
static void
check_description(
	const struct hid_report_touch_info *touch,
	struct hid_touch_description *description)
{
	int error;

	/* A pointer and a button pad, with the finger counts and the left button. */
	error = drv_hid_touch_describe(touch, description);
	check(error == 0, "the touch pad is described");
	check(description->properties == ((1U << INPUT_PROP_POINTER) | (1U << INPUT_PROP_BUTTONPAD)), "a pointer and a button pad");
	check(description->capability_count == 15U, "9 events of a screen with a Scan Time, 5 finger counts and BTN_LEFT");
}

/* Runs the fingers and the pad's press through the state machine. */
static void
check_frames(
	const struct hid_report_layout *layout,
	const struct hid_report_touch_info *touch,
	const struct hid_touch_description *description)
{
	static struct hid_touch_state state;
	static struct hid_touch_output output;
	unsigned char data[PAD_REPORT_BYTES];
	int error;

	/* A touch pad with nothing on it. */
	drv_hid_touch_reset(&state, description->slots);
	drv_hid_touch_set_scan_time(&state, touch);
	drv_hid_touch_set_pad(&state, touch);

	/* One finger down. */
	memset(data, 0, sizeof(data));
	put_finger(data, 0U, 1U, 0U, 600U, 300U);
	put_bits(data, SCAN_TIME_BIT, 16U, 100U);
	put_bits(data, CONTACT_COUNT_BIT, 8U, 1U);
	error = run_report(layout, &state, data, &output);
	check(error == 0, "the first report runs");
	check(has_event(&output, EV_ABS, ABS_MT_POSITION_X, 600), "the finger's X");
	check(has_event(&output, EV_KEY, BTN_TOUCH, 1), "BTN_TOUCH down");
	check(has_event(&output, EV_KEY, BTN_TOOL_FINGER, 1), "BTN_TOOL_FINGER for one finger");
	check(!has_event(&output, EV_KEY, BTN_LEFT, 1), "no button yet");

	/* A second finger: the count's tool changes. */
	memset(data, 0, sizeof(data));
	put_finger(data, 0U, 1U, 0U, 602U, 300U);
	put_finger(data, 1U, 1U, 1U, 800U, 320U);
	put_bits(data, SCAN_TIME_BIT, 16U, 200U);
	put_bits(data, CONTACT_COUNT_BIT, 8U, 2U);
	error = run_report(layout, &state, data, &output);
	check(error == 0, "the second report runs");
	check(has_event(&output, EV_KEY, BTN_TOOL_FINGER, 0), "BTN_TOOL_FINGER released");
	check(has_event(&output, EV_KEY, BTN_TOOL_DOUBLETAP, 1), "BTN_TOOL_DOUBLETAP for two fingers");

	/* The pad pressed with both fingers on it. */
	put_bits(data, BUTTON_BIT, 1U, 1U);
	put_bits(data, SCAN_TIME_BIT, 16U, 300U);
	error = run_report(layout, &state, data, &output);
	check(error == 0, "the press runs");
	check(has_event(&output, EV_KEY, BTN_LEFT, 1), "BTN_LEFT pressed");

	/* Released. */
	memset(data, 0, sizeof(data));
	put_finger(data, 0U, 1U, 0U, 602U, 300U);
	put_finger(data, 1U, 1U, 1U, 800U, 320U);
	put_bits(data, SCAN_TIME_BIT, 16U, 400U);
	put_bits(data, CONTACT_COUNT_BIT, 8U, 2U);
	error = run_report(layout, &state, data, &output);
	check(error == 0, "the release runs");
	check(has_event(&output, EV_KEY, BTN_LEFT, 0), "BTN_LEFT released");

	/* Every finger lifted: a frame of no finger. */
	memset(data, 0, sizeof(data));
	put_bits(data, SCAN_TIME_BIT, 16U, 500U);
	put_bits(data, CONTACT_COUNT_BIT, 8U, 0U);
	error = run_report(layout, &state, data, &output);
	check(error == 0, "the lift runs");
	check(has_event(&output, EV_KEY, BTN_TOOL_DOUBLETAP, 0), "BTN_TOOL_DOUBLETAP released");
	check(has_event(&output, EV_KEY, BTN_TOUCH, 0), "BTN_TOUCH up");
	check(has_event(&output, EV_ABS, ABS_MT_TRACKING_ID, -1), "the fingers' tracking ends");
}

/* Runs the checks on the descriptor file named on the command line. */
int
main(
	int argc,
	char **argv)
{
	static unsigned char descriptor[HID_REPORT_DESCRIPTOR_SIZE_MAX];
	static struct hid_report_touch_info touch;
	static struct hid_touch_description description;
	struct hid_report_layout *layout;
	size_t length;
	int error;

	/* The descriptor file. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-ptp DESCRIPTOR\n");
		return 2;
	}

	/* Reads it. */
	length = read_descriptor(argv[1], descriptor, sizeof(descriptor));
	check(length == 665U, "the 5330's descriptor is 665 bytes");

	/* Parses it. */
	layout = NULL;
	error = drv_hid_report_layout_parse(descriptor, length, &layout);
	check(error == 0, "the descriptor parses");
	if (error != 0) {
		printf("host-ptp: parse error %d\n", error);
		return 1;
	}

	/* The layout, the device and the frames. */
	check_layout(layout, &touch);
	check_description(&touch, &description);
	check_frames(layout, &touch, &description);
	drv_hid_report_layout_destroy(layout);

	/* The verdict. */
	if (failures != 0) {
		printf("host-ptp: FAIL (%d of %d checks)\n", failures, checks);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("host-ptp: ok (%d checks)\n", checks);
	return 0;
}
