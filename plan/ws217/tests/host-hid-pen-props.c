/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the properties a HID pen's device is registered with
 * (ws217-p003, src/drivers/generic/hid-input.c): a pen on a display (the
 * Pen collection) says INPUT_PROP_DIRECT, a pen tablet (the Digitizer
 * collection alone) INPUT_PROP_POINTER unless the same descriptor's touch
 * screen makes it a display's, and a device without a pen says nothing.
 *
 * The user's pen's interface also declares a mouse (Generic Desktop 0x02,
 * report ID 1): its main device, the pen and the mouse, is direct too.
 *
 *   host-hid-pen-props IF0 IF1
 *       IF0 and IF1 are the user's touch LCD's two interfaces
 *       (plan/bugs/BUG-267/touchscreen-if0.rdesc, the touch screen, and
 *       touchscreen-if1.rdesc, the pen).
 *
 * The kernel's HID files are compiled freestanding as the kernel compiles
 * them; this file, compiled for the host, supplies the kernel's allocator,
 * formatter and log, and an input layer that records each registration's
 * name and properties.
 */

#include <drivers/generic/hid-input.h>
#include <kern/input-device.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/errno.h>

/* The registrations one publication records: a main device and a touch device. */
#define FAKE_DEVICES_MAX	4U

/* The longest descriptor read from a file. */
#define DESCRIPTOR_BYTES_MAX	8192U

/* The properties of a pen on a display and of one on a tablet. */
#define PROPERTY_DIRECT		(1U << INPUT_PROP_DIRECT)
#define PROPERTY_POINTER	(1U << INPUT_PROP_POINTER)

/* What one registration declared: the name the identity gave it (main or touch) and its properties. */
struct fake_device {
	int used;
	char name[16];
	uint32_t properties;
};

/* The registrations of the publication under test; slot i stands for device i + 1. */
static struct fake_device fake_devices[FAKE_DEVICES_MAX];

/* The number of failed checks. */
static unsigned failures;

/* A pen on a tablet (report ID 2, the Digitizer collection): In Range and Tip Switch, X, Y and Tip Pressure. */
static const uint8_t tablet_pen[] = {
	0x05, 0x0d, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x20, 0xa1, 0x00,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x09, 0x32, 0x09, 0x42,
	0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x75, 0x10, 0x95, 0x01,
	0x26, 0x60, 0x54, 0x09, 0x30, 0x81, 0x02, 0x26, 0xbc, 0x34, 0x09, 0x31,
	0x81, 0x02, 0x05, 0x0d, 0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0xc0,
	0xc0
};

/* The same pen on a display: the application collection is Pen (0x0d, 0x02) instead of Digitizer. */
static const uint8_t display_pen[] = {
	0x05, 0x0d, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x20, 0xa1, 0x00,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x09, 0x32, 0x09, 0x42,
	0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x75, 0x10, 0x95, 0x01,
	0x26, 0x60, 0x54, 0x09, 0x30, 0x81, 0x02, 0x26, 0xbc, 0x34, 0x09, 0x31,
	0x81, 0x02, 0x05, 0x0d, 0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0xc0,
	0xc0
};

/*
 * The Digitizer pen and a touch screen in one descriptor (report IDs 2 and
 * 1): two fingers, each Tip Switch, seven bits of padding, a Contact
 * Identifier, X and Y.
 */
static const uint8_t tablet_pen_and_touch_screen[] = {
	0x05, 0x0d, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x20, 0xa1, 0x00,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x09, 0x32, 0x09, 0x42,
	0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x75, 0x10, 0x95, 0x01,
	0x26, 0x60, 0x54, 0x09, 0x30, 0x81, 0x02, 0x26, 0xbc, 0x34, 0x09, 0x31,
	0x81, 0x02, 0x05, 0x0d, 0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0xc0,
	0xc0,
	0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01, 0x85, 0x01,
	0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x01, 0x09, 0x42, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0x75, 0x08,
	0x95, 0x01, 0x25, 0x7f, 0x09, 0x51, 0x81, 0x02, 0x05, 0x01, 0x75, 0x10,
	0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xc0,
	0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x01, 0x09, 0x42, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0x75, 0x08,
	0x95, 0x01, 0x25, 0x7f, 0x09, 0x51, 0x81, 0x02, 0x05, 0x01, 0x75, 0x10,
	0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xc0,
	0xc0
};

/* The Digitizer pen and a touch pad (the same fingers, the Touch Pad collection, 0x0d 0x05) in one descriptor. */
static const uint8_t tablet_pen_and_touch_pad[] = {
	0x05, 0x0d, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x20, 0xa1, 0x00,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x09, 0x32, 0x09, 0x42,
	0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x75, 0x10, 0x95, 0x01,
	0x26, 0x60, 0x54, 0x09, 0x30, 0x81, 0x02, 0x26, 0xbc, 0x34, 0x09, 0x31,
	0x81, 0x02, 0x05, 0x0d, 0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0xc0,
	0xc0,
	0x05, 0x0d, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
	0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x01, 0x09, 0x42, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0x75, 0x08,
	0x95, 0x01, 0x25, 0x7f, 0x09, 0x51, 0x81, 0x02, 0x05, 0x01, 0x75, 0x10,
	0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xc0,
	0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
	0x95, 0x01, 0x09, 0x42, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0x75, 0x08,
	0x95, 0x01, 0x25, 0x7f, 0x09, 0x51, 0x81, 0x02, 0x05, 0x01, 0x75, 0x10,
	0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xc0,
	0xc0
};

/* A wheel mouse: three buttons, five bits of padding, X, Y and the wheel, a byte each. */
static const uint8_t wheel_mouse[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09,
	0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
	0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
	0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xc0, 0xc0
};

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void *kern_memcpy(void *destination, const void *source, size_t count);
void *kern_memset(void *destination, int value, size_t count);
size_t kern_strnlen(const char *text, size_t maximum);
int kern_snprintf(char *buffer, size_t capacity, const char *format, ...);
void kern_logf(const char *format, ...);
static void check(int condition, const char *what);
static int publish(const uint8_t *descriptor, size_t size, const char *what);
static const struct fake_device *device_named(const char *name);
static void expect(const uint8_t *descriptor, size_t size, const char *what, int main_present, uint32_t main_properties, int touch_present, uint32_t touch_properties);
static size_t read_file(const char *path, uint8_t *bytes, size_t capacity);

/* Runs the checks. */
int
main(
	int count,
	char **arguments)
{
	static uint8_t first[DESCRIPTOR_BYTES_MAX];
	static uint8_t second[DESCRIPTOR_BYTES_MAX];
	size_t first_size;
	size_t second_size;

	/* The two interfaces of the user's touch LCD. */
	if (count != 3) {
		fprintf(stderr, "usage: host-hid-pen-props IF0 IF1\n");
		return 2;
	}

	/* Their descriptors. */
	first_size = read_file(arguments[1], first, sizeof(first));
	second_size = read_file(arguments[2], second, sizeof(second));
	if (first_size == 0U || second_size == 0U)
		return 2;

	/* The fixed descriptors. */
	expect(display_pen, sizeof(display_pen), "a pen on a display", 1, PROPERTY_DIRECT, 0, 0U);
	expect(tablet_pen, sizeof(tablet_pen), "a pen tablet", 1, PROPERTY_POINTER, 0, 0U);
	expect(tablet_pen_and_touch_screen, sizeof(tablet_pen_and_touch_screen), "a Digitizer pen with a touch screen", 1, PROPERTY_DIRECT, 1, PROPERTY_DIRECT);
	expect(tablet_pen_and_touch_pad, sizeof(tablet_pen_and_touch_pad), "a Digitizer pen with a touch pad", 1, PROPERTY_POINTER, 1, PROPERTY_POINTER);
	expect(wheel_mouse, sizeof(wheel_mouse), "a mouse", 1, 0U, 0, 0U);

	/* The user's LCD: its touch screen's node is direct as before, its pen's node now too. */
	expect(first, first_size, "BUG-267 interface 0 (the touch screen)", 0, 0U, 1, PROPERTY_DIRECT);
	expect(second, second_size, "BUG-267 interface 1 (the pen)", 1, PROPERTY_DIRECT, 0, 0U);

	/* The verdict. */
	if (failures != 0U) {
		printf("host-hid-pen-props: %u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-hid-pen-props: PASS\n");
	return 0;
}

/* Allocates memory for the kernel's files. */
void *
kern_malloc(
	size_t size)
{
	/* The host's heap. */
	return malloc(size);
}

/* Allocates zeroed memory for the kernel's files. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	/* The host's heap. */
	return calloc(count, size);
}

/* Frees memory of the kernel's files. */
void
kern_free(
	void *pointer)
{
	/* The host's heap. */
	free(pointer);
}

/* Copies bytes for the kernel's files. */
void *
kern_memcpy(
	void *destination,
	const void *source,
	size_t count)
{
	/* The host's copy. */
	return memcpy(destination, source, count);
}

/* Fills bytes for the kernel's files. */
void *
kern_memset(
	void *destination,
	int value,
	size_t count)
{
	/* The host's fill. */
	return memset(destination, value, count);
}

/* Measures a text for the kernel's files. */
size_t
kern_strnlen(
	const char *text,
	size_t maximum)
{
	/* The host's measure. */
	return strnlen(text, maximum);
}

/* Formats text for the kernel's files. */
int
kern_snprintf(
	char *buffer,
	size_t capacity,
	const char *format,
	...)
{
	va_list arguments;
	int length;

	/* The host's formatter. */
	va_start(arguments, format);
	length = vsnprintf(buffer, capacity, format, arguments);
	va_end(arguments);

	/* Succeeded: the length the whole text needs. */
	return length;
}

/* Takes a line for the kernel's log, which the test does not print. */
void
kern_logf(
	const char *format,
	...)
{
	/* Nothing. */
	(void)format;
}

/* Records a registration's name and properties. */
int
drv_input_device_register(
	const struct input_device_info *info,
	struct input_device **result)
{
	unsigned slot;

	/* A free record. */
	for (slot = 0U; slot < FAKE_DEVICES_MAX; slot++) {
		if (!fake_devices[slot].used)
			break;
	}

	/* None free: the layer is full. */
	if (slot == FAKE_DEVICES_MAX)
		return ENOSPC;

	/* What it declared. */
	fake_devices[slot].used = 1;
	(void)snprintf(fake_devices[slot].name, sizeof(fake_devices[slot].name), "%s", info->name);
	fake_devices[slot].properties = info->properties;

	/* Succeeded: the record's number stands for the device. */
	*result = (struct input_device *)(uintptr_t)(slot + 1U);
	return 0;
}

/* Takes a device out of the records. */
void
drv_input_device_unregister(
	struct input_device *device)
{
	unsigned slot;

	/* The record is free again. */
	slot = (unsigned)(uintptr_t)device - 1U;
	if (slot < FAKE_DEVICES_MAX)
		fake_devices[slot].used = 0;
}

/* Takes an event, which this test does not look at. */
void
drv_input_device_emit_at(
	struct input_device *device,
	uint16_t type,
	uint16_t code,
	int32_t value,
	uint64_t milliseconds)
{
	/* Nothing. */
	(void)device;
	(void)type;
	(void)code;
	(void)value;
	(void)milliseconds;
}

/* Reports a device's node number: its record's. */
unsigned
drv_input_device_number(
	const struct input_device *device)
{
	/* The record stands for the node. */
	return (unsigned)(uintptr_t)device;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* One that fails is named. */
	printf("FAIL: %s\n", what);
	failures++;
}

/* Parses and publishes a descriptor under the names main and touch; 0 or the glue's error. */
static int
publish(
	const uint8_t *descriptor,
	size_t size,
	const char *what)
{
	struct hid_input_identity identity;
	struct hid_input *input;
	int error;

	/* No registration yet. */
	memset(fake_devices, 0, sizeof(fake_devices));

	/* The descriptor's devices. */
	error = drv_hid_input_prepare(descriptor, size, &input);
	if (error != 0) {
		printf("FAIL: %s: prepare error=%d\n", what, error);
		failures++;
		return error;
	}

	/* Registered under the two names. */
	memset(&identity, 0, sizeof(identity));
	identity.name = "main";
	identity.physical_path = "usb-test";
	identity.unique_id = "";
	identity.touch_name = "touch";
	identity.touch_physical_path = "usb-test-touch";
	error = drv_hid_input_publish(input, &identity);
	if (error != 0) {
		printf("FAIL: %s: publish error=%d\n", what, error);
		failures++;
	}

	/* The records stay; the glue goes. */
	drv_hid_input_destroy(input);

	/* Succeeded or not: the glue's answer. */
	return error;
}

/* Finds the registration of a name, or none. */
static const struct fake_device *
device_named(
	const char *name)
{
	unsigned slot;
	int differs;

	/* Each record ever made in this publication. */
	for (slot = 0U; slot < FAKE_DEVICES_MAX; slot++) {
		differs = strcmp(fake_devices[slot].name, name);
		if (differs == 0)
			return &fake_devices[slot];
	}

	/* None. */
	return NULL;
}

/* Publishes a descriptor and checks which devices it registered with which properties. */
static void
expect(
	const uint8_t *descriptor,
	size_t size,
	const char *what,
	int main_present,
	uint32_t main_properties,
	int touch_present,
	uint32_t touch_properties)
{
	const struct fake_device *main_device;
	const struct fake_device *touch_device;
	char text[160];
	int error;

	/* The publication. */
	error = publish(descriptor, size, what);
	if (error != 0)
		return;

	/* The main device and its properties. */
	main_device = device_named("main");
	(void)snprintf(text, sizeof(text), "%s: the main device is registered as expected", what);
	check((main_device != NULL) == (main_present != 0), text);
	if (main_device != NULL) {
		(void)snprintf(text, sizeof(text), "%s: the main device's properties are 0x%x (got 0x%x)", what, main_properties, main_device->properties);
		check(main_device->properties == main_properties, text);
	}

	/* The touch device and its properties. */
	touch_device = device_named("touch");
	(void)snprintf(text, sizeof(text), "%s: the touch device is registered as expected", what);
	check((touch_device != NULL) == (touch_present != 0), text);
	if (touch_device != NULL) {
		(void)snprintf(text, sizeof(text), "%s: the touch device's properties are 0x%x (got 0x%x)", what, touch_properties, touch_device->properties);
		check(touch_device->properties == touch_properties, text);
	}

	/* The case is told. */
	printf("ok: %s\n", what);
}

/* Reads a descriptor file; its size, or 0 with a line on failure. */
static size_t
read_file(
	const char *path,
	uint8_t *bytes,
	size_t capacity)
{
	FILE *file;
	size_t size;

	/* The whole file. */
	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		return 0U;
	}

	/* Its bytes. */
	size = fread(bytes, 1U, capacity, file);
	fclose(file);

	/* Succeeded: its bytes. */
	return size;
}
