/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * /dev/input-inject: a test-only pen or touch screen for machines without
 * one (QEMU).
 *
 * Only root may open the node, and only one open at a time.  The first
 * write declares the device (include/uapi/input-inject.h): a fixed pen
 * shape or a fixed touch screen shape, so no other device type can be made.
 * For a pen, later writes pass checked events to the ordinary input layer.
 * For a touch screen, later writes are frames of fingers, which are split
 * into the reports a USB touch screen sends and run through the USB touch
 * screen's state machine (src/drivers/usb/hid-touch.c).  Either way the
 * device reaches readers as /dev/input/eventN like a USB one.  Closing
 * removes it.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/input-inject.h>
#include <uapi/input.h>

#include <drivers/generic/input-inject.h>
#include <drivers/usb/hid-touch.h>

#include "kern/cdev.h"
#include "kern/cred.h"
#include "kern/file.h"
#include "kern/input-device.h"
#include "kern/kmem.h"
#include "kern/lock.h"

/* The device number of /dev/input-inject. */
#define INJECT_DEVICE_NUMBER	0x000e0000U

/* The tilt resolution: units per radian for units of one degree. */
#define INJECT_TILT_RESOLUTION	57

/* The number of absolute axes the pen declares. */
#define INJECT_AXIS_COUNT	5U

/*
 * One open of the node: the device it declared, if any.
 *
 * It lives from open to close.  The lock serializes the writes; kind is 0
 * until the first write declares the device.  A touch screen keeps its
 * state machine and the report and events of one write here, not on the
 * stack.
 */
struct inject_open {
	struct mutex lock;
	struct input_device *device;
	uint32_t kind;
	struct input_abs_axis axes[INJECT_AXIS_COUNT];
	uint32_t report_contacts;
	int32_t touch_x_max;
	int32_t touch_y_max;
	struct hid_touch_description touch_description;
	struct hid_touch_state touch;
	struct hid_report_input report;
	struct hid_touch_output output;
};

static int inject_open(struct file *file);
static int inject_close(struct file *file);
static ssize_t inject_write(struct file *file, const void *buffer, size_t size);
static int inject_declare(struct inject_open *state, const void *buffer, size_t size);
static int inject_declare_touch(struct inject_open *state, const struct input_inject_setup *setup);
static ssize_t inject_write_touch(struct inject_open *state, const void *buffer, size_t size);
static int inject_frame_valid(const struct inject_open *state, const struct input_inject_touch_frame *frame);
static int inject_touch_report(struct inject_open *state, const struct input_inject_touch_frame *frame, uint32_t first, uint32_t count);
static void inject_report_value(struct hid_report_input *report, uint16_t code, int32_t value);
static int inject_event_valid(const struct inject_open *state, const struct input_event *event);
static int inject_key_valid(uint16_t code);
static void inject_axis(struct input_abs_axis *axis, uint16_t code, int32_t minimum, int32_t maximum, int32_t resolution);

/* The file operations of the node. */
static const struct cdev_ops inject_ops = {
	.open = inject_open,
	.close = inject_close,
	.write = inject_write,
};

/* The pen's fixed capabilities: the report boundary, the buttons and the axes. */
static const struct input_capability inject_capabilities[] = {
	{ EV_SYN, SYN_REPORT },
	{ EV_KEY, BTN_TOOL_PEN },
	{ EV_KEY, BTN_TOOL_RUBBER },
	{ EV_KEY, BTN_TOUCH },
	{ EV_KEY, BTN_STYLUS },
	{ EV_KEY, BTN_STYLUS2 },
	{ EV_ABS, ABS_X },
	{ EV_ABS, ABS_Y },
	{ EV_ABS, ABS_PRESSURE },
	{ EV_ABS, ABS_TILT_X },
	{ EV_ABS, ABS_TILT_Y },
};

/* Guards inject_busy. */
static struct mutex inject_lock;

/* Set while one open holds the node. */
static int inject_busy;

/*
 * Publishes /dev/input-inject.
 */
int
drv_input_inject_register(void)
{
	int error;

	/* Prepares the single-open lock. */
	error = mutex_init(&inject_lock, LOCK_RANK_DEVICE, "input inject");
	if (error != 0)
		return error;

	/* Registers the character device. */
	error = cdev_register("input-inject", (dev_t)INJECT_DEVICE_NUMBER,
			      &inject_ops, NULL);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Admits one root open with no pen declared yet. */
static int
inject_open(
	struct file *file)
{
	struct inject_open *state;
	int error;

	/* Refuses anyone but root, whatever the node's mode says. */
	if (!cred_is_superuser(cred_current()))
		return EPERM;

	/* Admits one open at a time. */
	mutex_lock(&inject_lock);
	if (inject_busy) {
		mutex_unlock(&inject_lock);
		return EBUSY;
	}
	inject_busy = 1;
	mutex_unlock(&inject_lock);

	/* Allocates the open's state. */
	state = kern_malloc(sizeof(*state));
	if (state == NULL) {
		error = ENOMEM;
		goto fail;
	}
	kern_memset(state, 0, sizeof(*state));
	error = mutex_init(&state->lock, LOCK_RANK_DEVICE, "input inject open");
	if (error != 0) {
		kern_free(state);
		goto fail;
	}

	/* Succeeded: the file holds the state until it closes. */
	file->f_data = state;
	return 0;

fail:
	/* Gives the node back. */
	mutex_lock(&inject_lock);
	inject_busy = 0;
	mutex_unlock(&inject_lock);
	return error;
}

/* Removes the pen and gives the node back. */
static int
inject_close(
	struct file *file)
{
	struct inject_open *state;

	/* Removes the pen; the input layer releases its held buttons. */
	state = file->f_data;
	if (state != NULL) {
		if (state->device != NULL)
			drv_input_device_unregister(state->device);
		kern_free(state);
	}
	file->f_data = NULL;

	/* Gives the node back. */
	mutex_lock(&inject_lock);
	inject_busy = 0;
	mutex_unlock(&inject_lock);

	/* Succeeded. */
	return 0;
}

/* Declares the pen on the first write and emits checked events after it. */
static ssize_t
inject_write(
	struct file *file,
	const void *buffer,
	size_t size)
{
	struct inject_open *state;
	struct input_event event;
	const uint8_t *bytes;
	size_t count, index;
	ssize_t written;
	int error;

	/* Declares the device when none is declared yet. */
	state = file->f_data;
	mutex_lock(&state->lock);
	if (state->device == NULL) {
		error = inject_declare(state, buffer, size);
		mutex_unlock(&state->lock);
		if (error != 0)
			return -error;
		return (ssize_t)size;
	}

	/* A touch screen takes frames of fingers. */
	if (state->kind == INPUT_INJECT_KIND_TOUCH) {
		written = inject_write_touch(state, buffer, size);
		mutex_unlock(&state->lock);
		return written;
	}

	/* Refuses a write that is not a whole, bounded array of events. */
	count = size / sizeof(event);
	if (size == 0 || size % sizeof(event) != 0 ||
	    count > INPUT_INJECT_EVENTS_MAX) {
		mutex_unlock(&state->lock);
		return -EINVAL;
	}

	/* Checks every event before any is emitted. */
	bytes = buffer;
	for (index = 0; index < count; index++) {
		kern_memcpy(&event, bytes + index * sizeof(event), sizeof(event));
		if (!inject_event_valid(state, &event)) {
			mutex_unlock(&state->lock);
			return -EINVAL;
		}
	}

	/* Emits the events through the input layer. */
	for (index = 0; index < count; index++) {
		kern_memcpy(&event, bytes + index * sizeof(event), sizeof(event));
		drv_input_device_emit(state->device, event.type, event.code,
				      event.value);
	}
	mutex_unlock(&state->lock);

	/* Succeeded: the whole write was taken. */
	return (ssize_t)size;
}

/* Checks the setup record and registers the pen it declares. */
static int
inject_declare(
	struct inject_open *state,
	const void *buffer,
	size_t size)
{
	struct input_inject_setup setup;
	struct input_device_info info;
	int error;

	/* Refuses anything but one setup record with sane ranges. */
	if (size != sizeof(setup))
		return EINVAL;
	kern_memcpy(&setup, buffer, sizeof(setup));
	if (setup.magic != INPUT_INJECT_MAGIC)
		return EINVAL;
	if (setup.x_max < 1 || setup.x_max > INPUT_INJECT_AXIS_MAX ||
	    setup.y_max < 1 || setup.y_max > INPUT_INJECT_AXIS_MAX)
		return EINVAL;
	if (setup.reserved != 0U)
		return EINVAL;

	/* A touch screen is declared apart. */
	if (setup.kind == INPUT_INJECT_KIND_TOUCH) {
		error = inject_declare_touch(state, &setup);
		if (error != 0)
			return error;

		/* Succeeded: the touch screen is registered. */
		return 0;
	}

	/* Anything else must be a pen, which reports no fingers. */
	if (setup.kind != INPUT_INJECT_KIND_PEN)
		return EINVAL;
	if (setup.report_contacts != 0U)
		return EINVAL;
	state->kind = INPUT_INJECT_KIND_PEN;

	/* Describes the axes: position, 4096 pressure levels and tilt. */
	inject_axis(&state->axes[0], ABS_X, 0, setup.x_max, 0);
	inject_axis(&state->axes[1], ABS_Y, 0, setup.y_max, 0);
	inject_axis(&state->axes[2], ABS_PRESSURE, 0,
		    INPUT_INJECT_PRESSURE_MAX, 0);
	inject_axis(&state->axes[3], ABS_TILT_X, -INPUT_INJECT_TILT_MAX,
		    INPUT_INJECT_TILT_MAX, INJECT_TILT_RESOLUTION);
	inject_axis(&state->axes[4], ABS_TILT_Y, -INPUT_INJECT_TILT_MAX,
		    INPUT_INJECT_TILT_MAX, INJECT_TILT_RESOLUTION);

	/* Registers the pen as an ordinary input device. */
	kern_memset(&info, 0, sizeof(info));
	info.name = "Test pen (input-inject)";
	info.physical_path = "input-inject";
	info.id.bustype = BUS_VIRTUAL;
	info.capabilities = inject_capabilities;
	info.capability_count =
		sizeof(inject_capabilities) / sizeof(inject_capabilities[0]);
	info.absolute_axes = state->axes;
	info.absolute_axis_count = INJECT_AXIS_COUNT;
	return drv_input_device_register(&info, &state->device);
}

/* Tests whether an event is one the declared pen has, within its range. */
static int
inject_event_valid(
	const struct inject_open *state,
	const struct input_event *event)
{
	size_t index;

	/* Admits only the report boundary among the synchronization events. */
	if (event->type == EV_SYN)
		return event->code == SYN_REPORT && event->value == 0;

	/* Admits the pen's buttons with 0 or 1. */
	if (event->type == EV_KEY)
		return inject_key_valid(event->code) &&
		       (event->value == 0 || event->value == 1);

	/* Admits the pen's axes within their declared ranges. */
	if (event->type == EV_ABS) {
		for (index = 0; index < INJECT_AXIS_COUNT; index++) {
			if (state->axes[index].code != event->code)
				continue;
			return event->value >= state->axes[index].info.minimum &&
			       event->value <= state->axes[index].info.maximum;
		}
	}

	/* Refuses every other event. */
	return 0;
}

/* Tests whether a key code is one of the pen's buttons. */
static int
inject_key_valid(
	uint16_t code)
{
	/* Admits exactly the declared buttons. */
	switch (code) {
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOUCH:
	case BTN_STYLUS:
	case BTN_STYLUS2:
		return 1;
	default:
		return 0;
	}
}

/* Fills one absolute axis description. */
static void
inject_axis(
	struct input_abs_axis *axis,
	uint16_t code,
	int32_t minimum,
	int32_t maximum,
	int32_t resolution)
{
	/* Starts at the minimum with no fuzz or flat zone. */
	kern_memset(axis, 0, sizeof(*axis));
	axis->code = code;
	axis->info.value = minimum < 0 ? 0 : minimum;
	axis->info.minimum = minimum;
	axis->info.maximum = maximum;
	axis->info.resolution = resolution;
}

/*
 * Registers the touch screen a setup record declares: ten slots, fingers in
 * 0..x_max and 0..y_max, reports of report_contacts fingers each.
 */
static int
inject_declare_touch(
	struct inject_open *state,
	const struct input_inject_setup *setup)
{
	struct hid_report_touch_info touch;
	struct input_device_info info;
	int error;

	/* Refuses reports of no finger, or of more than a frame holds. */
	if (setup->report_contacts < 1U)
		return EINVAL;
	if (setup->report_contacts > INPUT_INJECT_TOUCH_CONTACTS)
		return EINVAL;

	/*
	 * Describes the screen as a USB one that counts its fingers, with the
	 * fingers' position over the declared area.
	 */
	kern_memset(&touch, 0, sizeof(touch));
	touch.contacts = setup->report_contacts;
	touch.count_present = 1;
	touch.x.minimum = 0;
	touch.x.maximum = setup->x_max;
	touch.y.minimum = 0;
	touch.y.maximum = setup->y_max;
	error = drv_hid_touch_describe(&touch, &state->touch_description);
	if (error != 0)
		return error;

	/* No finger touches yet. */
	drv_hid_touch_reset(&state->touch, state->touch_description.slots);
	state->report_contacts = setup->report_contacts;
	state->touch_x_max = setup->x_max;
	state->touch_y_max = setup->y_max;

	/* Registers the touch screen as an ordinary input device. */
	kern_memset(&info, 0, sizeof(info));
	info.name = "Test touchscreen (input-inject)";
	info.physical_path = "input-inject";
	info.id.bustype = BUS_VIRTUAL;
	info.capabilities = state->touch_description.capabilities;
	info.capability_count = state->touch_description.capability_count;
	info.absolute_axes = state->touch_description.axes;
	info.absolute_axis_count = state->touch_description.axis_count;
	error = drv_input_device_register(&info, &state->device);
	if (error != 0)
		return error;

	/* Succeeded: later writes are frames of fingers. */
	state->kind = INPUT_INJECT_KIND_TOUCH;
	return 0;
}

/*
 * Takes one frame of fingers: checks it whole, then splits it into reports
 * of report_contacts fingers (the first with the frame's Contact Count, the
 * later ones with 0), runs each through the touch state machine and emits
 * what it gives.  Returns the size taken or a negative error.
 */
static ssize_t
inject_write_touch(
	struct inject_open *state,
	const void *buffer,
	size_t size)
{
	struct input_inject_touch_frame frame;
	const struct hid_touch_event *event;
	uint32_t first;
	uint32_t count;
	size_t index;
	int valid;
	int error;

	/* Refuses anything but one whole, sane frame. */
	if (size != sizeof(frame))
		return -EINVAL;
	kern_memcpy(&frame, buffer, sizeof(frame));
	valid = inject_frame_valid(state, &frame);
	if (!valid)
		return -EINVAL;

	/* Sends the frame's fingers a report at a time; a frame of no finger is one report. */
	first = 0;
	do {
		/* The fingers of this report. */
		count = frame.count - first;
		if (count > state->report_contacts)
			count = state->report_contacts;

		/* Runs the report through the touch state machine. */
		error = inject_touch_report(state, &frame, first, count);
		if (error != 0)
			return -error;

		/* Emits what the report gave, in order. */
		for (index = 0; index < state->output.event_count; index++) {
			event = &state->output.events[index];
			drv_input_device_emit(state->device, event->type,
					      event->code, event->value);
		}

		/* The next report starts after these fingers. */
		first += count;
	} while (first < frame.count);

	/* Succeeded: the whole frame was taken. */
	return (ssize_t)size;
}

/* Tells whether a frame is one the touch screen can take. */
static int
inject_frame_valid(
	const struct inject_open *state,
	const struct input_inject_touch_frame *frame)
{
	const struct input_inject_contact *contact;
	uint32_t index;

	/* At most a frame of fingers, and nothing in the reserved word. */
	if (frame->count > INPUT_INJECT_TOUCH_CONTACTS)
		return 0;
	if (frame->reserved != 0U)
		return 0;

	/* Every finger of the frame names itself, touches or lifts, and lies on the screen. */
	for (index = 0; index < frame->count; index++) {
		contact = &frame->contacts[index];
		if (contact->contact_id < 0)
			return 0;
		if (contact->contact_id > INPUT_INJECT_CONTACT_ID_MAX)
			return 0;
		if (contact->tip != 0 && contact->tip != 1)
			return 0;
		if (contact->x < 0 || contact->x > state->touch_x_max)
			return 0;
		if (contact->y < 0 || contact->y > state->touch_y_max)
			return 0;
	}

	/* The frame is sane. */
	return 1;
}

/*
 * Builds the report of count fingers from first on, as a USB touch screen
 * that counts its fingers sends it, and runs it through the touch state
 * machine into state->output.
 */
static int
inject_touch_report(
	struct inject_open *state,
	const struct input_inject_touch_frame *frame,
	uint32_t first,
	uint32_t count)
{
	const struct input_inject_contact *contact;
	struct hid_report_input *report;
	uint32_t finger;
	int32_t contact_count;
	int error;

	/* The first report of the frame counts its fingers; the later ones count 0. */
	report = &state->report;
	kern_memset(report, 0, sizeof(*report));
	contact_count = 0;
	if (first == 0U)
		contact_count = (int32_t)frame->count;
	inject_report_value(report, HID_TOUCH_CONTACT_COUNT_CODE, contact_count);

	/* Each finger in its place in the report, confident. */
	for (finger = 0; finger < count; finger++) {
		contact = &frame->contacts[first + finger];
		inject_report_value(report, HID_TOUCH_CODE(finger, HID_TOUCH_ITEM_TIP), contact->tip);
		inject_report_value(report, HID_TOUCH_CODE(finger, HID_TOUCH_ITEM_CONFIDENCE), 1);
		inject_report_value(report, HID_TOUCH_CODE(finger, HID_TOUCH_ITEM_CONTACT_ID), contact->contact_id);
		inject_report_value(report, HID_TOUCH_CODE(finger, HID_TOUCH_ITEM_X), contact->x);
		inject_report_value(report, HID_TOUCH_CODE(finger, HID_TOUCH_ITEM_Y), contact->y);
	}

	/* Turns the report into protocol B events. */
	error = drv_hid_touch_translate(&state->touch, report, &state->output);
	if (error != 0)
		return error;

	/* Succeeded: the events are in state->output. */
	return 0;
}

/* Appends one touch value to a report (a report holds far more than a frame needs). */
static void
inject_report_value(
	struct hid_report_input *report,
	uint16_t code,
	int32_t value)
{
	struct hid_report_value *entry;

	/* Stores the value after the ones already in the report. */
	entry = &report->values[report->value_count];
	entry->type = HID_REPORT_TYPE_TOUCH;
	entry->code = code;
	entry->value = value;
	report->value_count++;
}
