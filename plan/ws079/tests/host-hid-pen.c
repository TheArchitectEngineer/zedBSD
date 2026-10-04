/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the USB HID pen (ws079-p002).
 *
 * It parses a synthetic Wacom-like report descriptor (a Digitizer collection
 * with In Range, Invert, Tip Switch, Eraser, two barrel switches, X and Y,
 * a 4096-step Tip Pressure and X and Y Tilt), decodes a sequence of reports
 * and runs them through the pen state machine, then checks the emitted
 * events: pressure 0..4095, tilt, eraser via Invert, and in-range enter and
 * leave.  The unchanged driver file is compiled freestanding into its own
 * object; the linker drops its USB half, which the test never calls, and the
 * test supplies the kernel allocator.
 */

#include <drivers/generic/hid-digitizer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The number of checks that failed. */
static int failures;

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

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

void *kern_memcpy(void *destination, const void *source, size_t length);
void *kern_memset(void *destination, int value, size_t length);

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
 * A pen on a separate tablet, report ID 2, 13 bytes of payload:
 * byte 0: In Range, Invert, Tip Switch, Eraser, Barrel, Secondary Barrel, 2 pad
 * bytes 1-2: X 0..21600 (216 mm), bytes 3-4: Y 0..13500 (135 mm)
 * bytes 5-6: Tip Pressure 0..4095, byte 7: X Tilt -60..60, byte 8: Y Tilt
 */
static const uint8_t pen_descriptor[] = {
	0x05, 0x0d,		/* Usage Page (Digitizer) */
	0x09, 0x01,		/* Usage (Digitizer) */
	0xa1, 0x01,		/* Collection (Application) */
	0x85, 0x02,		/*  Report ID (2) */
	0x09, 0x20,		/*  Usage (Stylus) */
	0xa1, 0x00,		/*  Collection (Physical) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x25, 0x01,		/*   Logical Maximum (1) */
	0x75, 0x01,		/*   Report Size (1) */
	0x95, 0x06,		/*   Report Count (6) */
	0x09, 0x32,		/*   Usage (In Range) */
	0x09, 0x3c,		/*   Usage (Invert) */
	0x09, 0x42,		/*   Usage (Tip Switch) */
	0x09, 0x45,		/*   Usage (Eraser) */
	0x09, 0x44,		/*   Usage (Barrel Switch) */
	0x09, 0x5a,		/*   Usage (Secondary Barrel Switch) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0x95, 0x02,		/*   Report Count (2) */
	0x81, 0x03,		/*   Input (Constant) */
	0x05, 0x01,		/*   Usage Page (Generic Desktop) */
	0x75, 0x10,		/*   Report Size (16) */
	0x95, 0x01,		/*   Report Count (1) */
	0x55, 0x0d,		/*   Unit Exponent (-3) */
	0x65, 0x11,		/*   Unit (SI Linear, cm) */
	0x35, 0x00,		/*   Physical Minimum (0) */
	0x46, 0x60, 0x54,	/*   Physical Maximum (21600, i.e. 21.6 cm) */
	0x26, 0x60, 0x54,	/*   Logical Maximum (21600) */
	0x09, 0x30,		/*   Usage (X) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0x46, 0xbc, 0x34,	/*   Physical Maximum (13500) */
	0x26, 0xbc, 0x34,	/*   Logical Maximum (13500) */
	0x09, 0x31,		/*   Usage (Y) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0x05, 0x0d,		/*   Usage Page (Digitizer) */
	0x65, 0x00,		/*   Unit (None) */
	0x55, 0x00,		/*   Unit Exponent (0) */
	0x45, 0x00,		/*   Physical Maximum (0) */
	0x26, 0xff, 0x0f,	/*   Logical Maximum (4095) */
	0x09, 0x30,		/*   Usage (Tip Pressure) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0x75, 0x08,		/*   Report Size (8) */
	0x95, 0x02,		/*   Report Count (2) */
	0x15, 0xc4,		/*   Logical Minimum (-60) */
	0x25, 0x3c,		/*   Logical Maximum (60) */
	0x35, 0xc4,		/*   Physical Minimum (-60) */
	0x45, 0x3c,		/*   Physical Maximum (60) */
	0x65, 0x14,		/*   Unit (English Rotation, degrees) */
	0x09, 0x3d,		/*   Usage (X Tilt) */
	0x09, 0x3e,		/*   Usage (Y Tilt) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0xc0,			/*  End Collection */
	0xc0,			/* End Collection */
};

/* Reports one failed check. */
static void
check(int condition, const char *what)
{
	if (condition)
		return;
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Builds one pen report. */
static void
make_report(uint8_t *report, unsigned switches, unsigned x, unsigned y,
	unsigned pressure, int tilt_x, int tilt_y)
{
	report[0] = 2;
	report[1] = (uint8_t)switches;
	report[2] = (uint8_t)x;
	report[3] = (uint8_t)(x >> 8);
	report[4] = (uint8_t)y;
	report[5] = (uint8_t)(y >> 8);
	report[6] = (uint8_t)pressure;
	report[7] = (uint8_t)(pressure >> 8);
	report[8] = (uint8_t)(int8_t)tilt_x;
	report[9] = (uint8_t)(int8_t)tilt_y;
}

#define SW_IN_RANGE	0x01U
#define SW_INVERT	0x02U
#define SW_TIP		0x04U
#define SW_ERASER	0x08U
#define SW_BARREL	0x10U
#define SW_BARREL2	0x20U

/* Decodes and translates one report into the output. */
static void
run(const struct hid_report_layout *layout, struct hid_digitizer_state *state,
	const uint8_t *report, struct hid_digitizer_output *output)
{
	struct hid_report_input decoded;
	int error;

	error = drv_hid_report_decode(layout, report, 10, &decoded);
	check(error == 0, "decode");
	check(drv_hid_digitizer_report_is_pen(&decoded), "report is pen");
	error = drv_hid_digitizer_translate(state, &decoded, output);
	check(error == 0, "translate");
}

/* Finds the index of an event, or -1. */
static int
find(const struct hid_digitizer_output *output, uint16_t type, uint16_t code,
	int32_t value)
{
	size_t index;

	for (index = 0; index < output->event_count; index++) {
		if (output->events[index].type == type &&
		    output->events[index].code == code &&
		    output->events[index].value == value)
			return (int)index;
	}
	return -1;
}

/* Counts the SYN_REPORTs of an output. */
static int
frames(const struct hid_digitizer_output *output)
{
	size_t index;
	int count;

	count = 0;
	for (index = 0; index < output->event_count; index++) {
		if (output->events[index].type == EV_SYN)
			count++;
	}
	return count;
}

/* Reports whether the layout declares a capability. */
static int
has_capability(const struct hid_report_layout *layout, uint16_t type, uint16_t code)
{
	struct hid_report_layout_info info;
	struct input_capability capability;
	size_t index;

	drv_hid_report_layout_get_info(layout, &info);
	for (index = 0; index < info.capability_count; index++) {
		drv_hid_report_layout_get_capability(layout, index, &capability);
		if (capability.type == type && capability.code == code)
			return 1;
	}
	return 0;
}

/* Finds the absinfo of an axis. */
static int
axis(const struct hid_report_layout *layout, uint16_t code, struct input_absinfo *result)
{
	struct hid_report_layout_info info;
	struct input_abs_axis entry;
	size_t index;

	drv_hid_report_layout_get_info(layout, &info);
	for (index = 0; index < info.absolute_axis_count; index++) {
		drv_hid_report_layout_get_absolute_axis(layout, index, &entry);
		if (entry.code == code) {
			*result = entry.info;
			return 1;
		}
	}
	return 0;
}

int
main(void)
{
	struct hid_report_layout *layout;
	struct hid_report_layout_info info;
	struct hid_digitizer_state state;
	struct hid_digitizer_output output;
	struct input_absinfo absinfo;
	uint8_t report[10];
	unsigned pressure;
	int tool, axis_index, touch, error;

	/* Parses the descriptor and checks what it declares. */
	error = drv_hid_report_layout_parse(pen_descriptor, sizeof(pen_descriptor), &layout);
	check(error == 0, "parse the pen descriptor");
	if (error != 0) {
		fprintf(stderr, "parse error %d\n", error);
		return 1;
	}
	drv_hid_report_layout_get_info(layout, &info);
	check(info.pen == HID_REPORT_PEN_TABLET, "layout is a tablet pen");
	check(has_capability(layout, EV_KEY, BTN_TOOL_PEN), "BTN_TOOL_PEN declared");
	check(has_capability(layout, EV_KEY, BTN_TOOL_RUBBER), "BTN_TOOL_RUBBER declared");
	check(has_capability(layout, EV_KEY, BTN_TOUCH), "BTN_TOUCH declared");
	check(has_capability(layout, EV_KEY, BTN_STYLUS), "BTN_STYLUS declared");
	check(has_capability(layout, EV_KEY, BTN_STYLUS2), "BTN_STYLUS2 declared");
	check(has_capability(layout, EV_ABS, ABS_PRESSURE), "ABS_PRESSURE declared");
	check(has_capability(layout, EV_ABS, ABS_TILT_X), "ABS_TILT_X declared");
	check(has_capability(layout, EV_ABS, ABS_TILT_Y), "ABS_TILT_Y declared");
	check(!has_capability(layout, HID_REPORT_TYPE_DIGITIZER, HID_DIGITIZER_USAGE_IN_RANGE),
		"no pseudo capability leaks");

	check(axis(layout, ABS_PRESSURE, &absinfo), "pressure axis");
	check(absinfo.minimum == 0 && absinfo.maximum == 4095, "pressure 0..4095");
	check(absinfo.resolution == 0, "pressure has no resolution");
	check(axis(layout, ABS_X, &absinfo), "X axis");
	check(absinfo.maximum == 21600 && absinfo.resolution == 100, "X 100 units/mm");
	check(axis(layout, ABS_Y, &absinfo), "Y axis");
	check(absinfo.maximum == 13500 && absinfo.resolution == 100, "Y 100 units/mm");
	check(axis(layout, ABS_TILT_X, &absinfo), "tilt X axis");
	check(absinfo.minimum == -60 && absinfo.maximum == 60, "tilt -60..60");
	check(absinfo.resolution == 57, "tilt 57 units/radian");
	if (absinfo.resolution != 57)
		fprintf(stderr, "tilt resolution %d\n", absinfo.resolution);

	drv_hid_digitizer_reset(&state);

	/* Out of range with no tool before: nothing. */
	make_report(report, 0, 0, 0, 0, 0, 0);
	run(layout, &state, report, &output);
	check(output.event_count == 0, "out of range emits nothing");

	/* Enters range: BTN_TOOL_PEN first, then axes, then SYN. */
	make_report(report, SW_IN_RANGE, 1000, 2000, 0, 10, -20);
	run(layout, &state, report, &output);
	check(output.event_count > 0 && output.events[0].type == EV_KEY &&
	      output.events[0].code == BTN_TOOL_PEN && output.events[0].value == 1,
	      "enter: BTN_TOOL_PEN=1 first");
	check(find(&output, EV_ABS, ABS_X, 1000) > 0, "enter: X");
	check(find(&output, EV_ABS, ABS_Y, 2000) > 0, "enter: Y");
	check(find(&output, EV_ABS, ABS_TILT_X, 10) > 0, "enter: tilt X 10");
	check(find(&output, EV_ABS, ABS_TILT_Y, -20) > 0, "enter: tilt Y -20");
	check(find(&output, EV_KEY, BTN_TOUCH, 1) < 0, "enter: no touch");
	check(frames(&output) == 1, "enter: one frame");
	check(output.events[output.event_count - 1].type == EV_SYN, "enter: ends in SYN");

	/* Touches and sweeps the pressure 0..4095: BTN_TOUCH once, after the axes. */
	touch = 0;
	for (pressure = 0; pressure <= 4095; pressure += 13) {
		make_report(report, SW_IN_RANGE | SW_TIP, 1000 + pressure, 2000, pressure, 30, 40);
		run(layout, &state, report, &output);
		check(find(&output, EV_ABS, ABS_PRESSURE, (int32_t)pressure) >= 0, "pressure value");
		check(find(&output, EV_KEY, BTN_TOOL_PEN, 1) < 0, "no repeated tool");
		if (find(&output, EV_KEY, BTN_TOUCH, 1) >= 0) {
			touch++;
			check(find(&output, EV_KEY, BTN_TOUCH, 1) >
			      find(&output, EV_ABS, ABS_PRESSURE, (int32_t)pressure),
			      "touch after axes");
		}
	}
	make_report(report, SW_IN_RANGE | SW_TIP, 1000, 2000, 4095, 30, 40);
	run(layout, &state, report, &output);
	check(find(&output, EV_ABS, ABS_PRESSURE, 4095) >= 0, "pressure reaches 4095");
	check(touch == 1, "BTN_TOUCH=1 exactly once");

	/* Side buttons. */
	make_report(report, SW_IN_RANGE | SW_TIP | SW_BARREL, 1000, 2000, 2048, 0, 0);
	run(layout, &state, report, &output);
	check(find(&output, EV_KEY, BTN_STYLUS, 1) >= 0, "BTN_STYLUS=1");
	make_report(report, SW_IN_RANGE | SW_TIP | SW_BARREL2, 1000, 2000, 2048, 0, 0);
	run(layout, &state, report, &output);
	check(find(&output, EV_KEY, BTN_STYLUS, 0) >= 0, "BTN_STYLUS=0");
	check(find(&output, EV_KEY, BTN_STYLUS2, 1) >= 0, "BTN_STYLUS2=1");

	/* Leaves while touching and holding a button: pressure, touch, button, tool. */
	make_report(report, 0, 1000, 2000, 0, 0, 0);
	run(layout, &state, report, &output);
	check(frames(&output) == 1, "leave: one frame");
	check(find(&output, EV_ABS, ABS_PRESSURE, 0) == 0, "leave: pressure 0 first");
	check(find(&output, EV_KEY, BTN_TOUCH, 0) == 1, "leave: touch 0 second");
	check(find(&output, EV_KEY, BTN_STYLUS2, 0) == 2, "leave: button released");
	check(find(&output, EV_KEY, BTN_TOOL_PEN, 0) == 3, "leave: tool 0 last");
	check(find(&output, EV_ABS, ABS_X, 1000) < 0, "leave: no position");
	check(state.tool == 0 && state.touch == 0, "leave: state cleared");

	/* The eraser end comes near: Invert gives BTN_TOOL_RUBBER. */
	make_report(report, SW_IN_RANGE | SW_INVERT, 500, 600, 0, 0, 0);
	run(layout, &state, report, &output);
	check(find(&output, EV_KEY, BTN_TOOL_RUBBER, 1) == 0, "eraser: rubber in range");
	check(find(&output, EV_KEY, BTN_TOOL_PEN, 1) < 0, "eraser: not the pen");

	/* The eraser touches: Eraser gives BTN_TOUCH with the rubber tool. */
	make_report(report, SW_IN_RANGE | SW_INVERT | SW_ERASER, 500, 600, 3000, 0, 0);
	run(layout, &state, report, &output);
	check(find(&output, EV_KEY, BTN_TOUCH, 1) >= 0, "eraser: touch");
	check(find(&output, EV_ABS, ABS_PRESSURE, 3000) >= 0, "eraser: pressure");
	check(state.tool == BTN_TOOL_RUBBER, "eraser: tool is rubber");

	/* Flipped to the pen in range: rubber closes in its own frame, pen opens. */
	make_report(report, SW_IN_RANGE, 500, 600, 0, 0, 0);
	run(layout, &state, report, &output);
	check(frames(&output) == 2, "switch: two frames");
	tool = find(&output, EV_KEY, BTN_TOOL_RUBBER, 0);
	axis_index = find(&output, EV_KEY, BTN_TOOL_PEN, 1);
	check(tool >= 0 && axis_index > tool, "switch: rubber 0 before pen 1");
	check(find(&output, EV_KEY, BTN_TOUCH, 0) >= 0 &&
	      find(&output, EV_KEY, BTN_TOUCH, 0) < tool, "switch: touch released first");
	check(output.events[tool + 1].type == EV_SYN, "switch: SYN between the tools");

	/* Leaves. */
	make_report(report, 0, 0, 0, 0, 0, 0);
	run(layout, &state, report, &output);
	check(find(&output, EV_KEY, BTN_TOOL_PEN, 0) >= 0, "final leave");

	drv_hid_report_layout_destroy(layout);

	if (failures != 0) {
		fprintf(stderr, "host-hid-pen: %d failures\n", failures);
		return 1;
	}
	printf("host-hid-pen: ok\n");
	return 0;
}
