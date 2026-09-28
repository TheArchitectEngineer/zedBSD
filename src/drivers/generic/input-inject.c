/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * /dev/input-inject: a test-only pen for machines without one (QEMU).
 *
 * Only root may open the node, and only one open at a time.  The first
 * write declares the pen (include/uapi/input-inject.h); the device is a
 * fixed pen shape, so no other device type can be made.  Later writes pass
 * checked events to the ordinary input layer, so the pen reaches readers as
 * /dev/input/eventN like a USB one.  Closing removes the pen.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/input-inject.h>
#include <uapi/input.h>

#include <drivers/generic/input-inject.h>

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

/* One open of the node: the pen it declared, if any. */
struct inject_open {
	struct mutex lock;
	struct input_device *device;
	struct input_abs_axis axes[INJECT_AXIS_COUNT];
};

static int inject_open(struct file *file);
static int inject_close(struct file *file);
static ssize_t inject_write(struct file *file, const void *buffer, size_t size);
static int inject_declare(struct inject_open *state, const void *buffer, size_t size);
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
	int error;

	/* Declares the pen when none is declared yet. */
	state = file->f_data;
	mutex_lock(&state->lock);
	if (state->device == NULL) {
		error = inject_declare(state, buffer, size);
		mutex_unlock(&state->lock);
		if (error != 0)
			return -error;
		return (ssize_t)size;
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

	/* Refuses anything but one pen setup record with sane ranges. */
	if (size != sizeof(setup))
		return EINVAL;
	kern_memcpy(&setup, buffer, sizeof(setup));
	if (setup.magic != INPUT_INJECT_MAGIC ||
	    setup.kind != INPUT_INJECT_KIND_PEN)
		return EINVAL;
	if (setup.x_max < 1 || setup.x_max > INPUT_INJECT_AXIS_MAX ||
	    setup.y_max < 1 || setup.y_max > INPUT_INJECT_AXIS_MAX)
		return EINVAL;

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
