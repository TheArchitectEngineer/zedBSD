/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the uvc driver's descriptor parser, PROBE record, alternate
 * choice (usb-uvc-desc.c) and frame assembler (usb-uvc-payload.c) on the
 * host.
 *
 * The descriptors are written here by hand in the layout of the class
 * specification; the ones of the real devices are to be added from their
 * dumps.  A stand-in framework records what the assembler does with its
 * slots.
 */

#include <drivers/camera/cam.h>
#include <drivers/usb/usb-uvc-desc.h>
#include <drivers/usb/usb-uvc-payload.h>
#include <uapi/camera.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The stand-in framework's slots. */
#define FAKE_SLOTS		4U
#define FAKE_SLOT_BYTES		64U

/* One slot of the stand-in framework. */
struct drv_cam_slot {
	unsigned index;
	unsigned active;
	uint8_t bytes[FAKE_SLOT_BYTES];
};

/* One frame the stand-in framework was handed. */
struct fake_done {
	uint32_t sequence;
	size_t used;
	uint32_t flags;
	uint32_t pts;
	uint8_t bytes[FAKE_SLOT_BYTES];
};

/*
 * The stand-in framework's state: the free slots, the frames done, the
 * sequence and the drops.  free_slots limits how many takes succeed.
 */
static struct drv_cam_slot slots[FAKE_SLOTS];
static uint32_t slot_sequence[FAKE_SLOTS];
static unsigned free_slots;
static uint32_t sequence;
static struct fake_done done[16];
static unsigned done_count;
static unsigned dropped_no_buffer;
static unsigned dropped_overflow;
static unsigned discards;

/* Reports a failed assertion of the code under test. */
void
__libc_assert_fail(
	const char *expression,
	const char *file,
	int line)
{
	fprintf(stderr, "ASSERT %s:%d: %s\n", file, line, expression);
	abort();
}

/* Begins a frame. */
uint32_t
drv_cam_frame_begin(
	struct drv_cam_device *device)
{
	(void)device;
	sequence++;
	return sequence;
}

/* Takes a free slot while the test allows one. */
int
drv_cam_slot_take(
	struct drv_cam_device *device,
	uint32_t frame_sequence,
	struct drv_cam_slot **slot)
{
	unsigned index;

	(void)device;
	if (free_slots == 0U)
		return EAGAIN;

	for (index = 0U; index < FAKE_SLOTS; index++) {
		if (slots[index].active)
			continue;
		slots[index].active = 1U;
		memset(slots[index].bytes, 0, sizeof(slots[index].bytes));
		slot_sequence[index] = frame_sequence;
		free_slots--;
		*slot = &slots[index];
		return 0;
	}

	return EAGAIN;
}

/* Reports a slot's memory. */
void *
drv_cam_slot_address(
	const struct drv_cam_slot *slot)
{
	return (void *)slot->bytes;
}

/* Reports a slot's size. */
size_t
drv_cam_slot_capacity(
	const struct drv_cam_slot *slot)
{
	(void)slot;
	return FAKE_SLOT_BYTES;
}

/* Records a finished frame; the slot is free again at once. */
void
drv_cam_slot_done(
	struct drv_cam_device *device,
	struct drv_cam_slot *slot,
	size_t used_bytes,
	const struct drv_cam_frame_info *info)
{
	struct fake_done *frame;

	(void)device;
	assert(slot->active);
	assert(done_count < sizeof(done) / sizeof(done[0]));
	frame = &done[done_count++];
	frame->sequence = slot_sequence[slot->index];
	frame->used = used_bytes;
	frame->flags = info->flags;
	frame->pts = info->device_pts;
	memcpy(frame->bytes, slot->bytes, sizeof(frame->bytes));
	slot->active = 0U;
	free_slots++;
}

/* Gives a slot back unfilled. */
void
drv_cam_slot_discard(
	struct drv_cam_device *device,
	struct drv_cam_slot *slot)
{
	(void)device;
	assert(slot->active);
	slot->active = 0U;
	free_slots++;
	discards++;
}

/* Counts a dropped frame. */
void
drv_cam_frame_dropped(
	struct drv_cam_device *device,
	uint32_t reason)
{
	(void)device;
	if (reason == CAM_DROP_NO_BUFFER)
		dropped_no_buffer++;
	if (reason == CAM_DROP_OVERFLOW)
		dropped_overflow++;
}

/* Resets the stand-in framework with a number of free slots. */
static void
fake_reset(
	unsigned slot_count)
{
	unsigned index;

	memset(slots, 0, sizeof(slots));
	for (index = 0U; index < FAKE_SLOTS; index++)
		slots[index].index = index;
	free_slots = slot_count;
	sequence = 0U;
	done_count = 0U;
	dropped_no_buffer = 0U;
	dropped_overflow = 0U;
	discards = 0U;
}

/* Builds a payload of a header and data bytes. */
static size_t
payload_make(
	uint8_t *payload,
	uint8_t info,
	const uint8_t *data,
	size_t length)
{
	payload[0] = 2U;
	payload[1] = info;
	memcpy(payload + 2U, data, length);
	return 2U + length;
}

/* ---------------------------------------------------------------- */
/* Descriptors.                                                     */

/* A VideoControl header of UVC 1.10 with a 48 MHz clock and streaming interface 1. */
static const uint8_t vc_header[] = {
	13, 0x24, 0x01, 0x10, 0x01, 0x28, 0x00, 0x00, 0x6c, 0xdc, 0x02, 0x01, 0x01
};

/* A VS input header: two formats, endpoint 0x81, terminal 3. */
static const uint8_t vs_header[] = {
	15, 0x24, 0x01, 0x02, 0x00, 0x01, 0x81, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

/* An MJPEG format of 11 bytes: index 1, two frames, default frame 1. */
static const uint8_t vs_mjpeg[] = {
	11, 0x24, 0x06, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00
};

/* An MJPEG frame 1: 640x480, buffer 614400, default 333333, two intervals 333333 and 666666. */
static const uint8_t vs_mjpeg_frame1[] = {
	34, 0x24, 0x07, 0x01, 0x00, 0x80, 0x02, 0xe0, 0x01,
	0, 0, 0, 0, 0, 0, 0, 0,
	0x00, 0x60, 0x09, 0x00,
	0x15, 0x16, 0x05, 0x00,
	0x02,
	0x15, 0x16, 0x05, 0x00,
	0x2a, 0x2c, 0x0a, 0x00
};

/* An MJPEG frame 2: 1920x1080 continuous from 333333 to 1000000 step 333333. */
static const uint8_t vs_mjpeg_frame2[] = {
	38, 0x24, 0x07, 0x02, 0x00, 0x80, 0x07, 0x38, 0x04,
	0, 0, 0, 0, 0, 0, 0, 0,
	0x00, 0x00, 0x00, 0x00,
	0x15, 0x16, 0x05, 0x00,
	0x00,
	0x15, 0x16, 0x05, 0x00,
	0x40, 0x42, 0x0f, 0x00,
	0x15, 0x16, 0x05, 0x00
};

/* A YUY2 format of 27 bytes: index 2, one frame, 16 bpp. */
static const uint8_t vs_yuy2[] = {
	27, 0x24, 0x04, 0x02, 0x01,
	0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
	16, 1, 0, 0, 0, 0
};

/* A YUY2 frame: 320x240, buffer 153600, one interval 666666. */
static const uint8_t vs_yuy2_frame[] = {
	30, 0x24, 0x05, 0x01, 0x00, 0x40, 0x01, 0xf0, 0x00,
	0, 0, 0, 0, 0, 0, 0, 0,
	0x00, 0x58, 0x02, 0x00,
	0x2a, 0x2c, 0x0a, 0x00,
	0x01,
	0x2a, 0x2c, 0x0a, 0x00
};

/* A colour descriptor: BT.709 primaries and transfer, BT.601 matrix. */
static const uint8_t vs_color[] = {
	6, 0x24, 0x0d, 1, 1, 4
};

/* A frame whose intervals are cut off. */
static const uint8_t vs_short_frame[] = {
	27, 0x24, 0x05, 0x02, 0x00, 0x40, 0x01, 0xf0, 0x00,
	0, 0, 0, 0, 0, 0, 0, 0,
	0x00, 0x58, 0x02, 0x00,
	0x2a, 0x2c, 0x0a, 0x00,
	0x02, 0x00
};

/* Checks the VideoControl header. */
static void
case_control(
	void)
{
	struct uvc_control_desc control;
	int error;

	memset(&control, 0, sizeof(control));
	error = uvc_desc_control_feed(&control, vc_header, sizeof(vc_header));
	assert(error == 0);
	assert(control.has_header);
	assert(control.bcd_uvc == 0x0110U);
	assert(control.clock_hz == 48000000U);
	assert(control.stream_count == 1U);
	assert(control.streams[0] == 1U);
	assert(uvc_probe_length(control.bcd_uvc) == UVC_PROBE_LENGTH_1_1);
	assert(uvc_probe_length(0x0100U) == UVC_PROBE_LENGTH_1_0);
	assert(uvc_probe_length(0x0150U) == UVC_PROBE_LENGTH_1_5);
	printf("control header: PASS\n");
}

/*
 * Reads the processing unit's and the camera terminal's bmControls, and
 * the controls' coding: the lookup, the presence bits, signed and
 * unsigned fields, the range of a field and the auto-exposure switch.
 */
static void
case_controls(
	void)
{
	/* A processing unit (ID 2): brightness, contrast, gain, power line, WB auto. */
	static const uint8_t pu[] = {
		11, 0x24, 0x05, 2, 1, 0x00, 0x00, 2, 0x03, 0x16, 0
	};
	/* A camera terminal (ID 1): AE mode, exposure time, focus auto (bit 17). */
	static const uint8_t ct[] = {
		18, 0x24, 0x02, 1, 0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0x0a, 0x00, 0x02
	};
	/* An input terminal that is not a camera (a composite input, 0x0401). */
	static const uint8_t other[] = {
		8, 0x24, 0x02, 5, 0x01, 0x04, 0, 0
	};
	/* A processing unit cut short before its bitmap. */
	static const uint8_t short_pu[] = {
		8, 0x24, 0x05, 4, 1, 0x00, 0x00, 2
	};
	const struct uvc_control_map *map;
	struct uvc_control_desc control;
	uint8_t bytes[4];
	uint8_t unit_id;
	int error;

	/* The bitmaps and the IDs. */
	memset(&control, 0, sizeof(control));
	error = uvc_desc_control_feed(&control, other, sizeof(other));
	assert(error == 0);
	assert(control.camera_terminal == 0U);
	error = uvc_desc_control_feed(&control, pu, sizeof(pu));
	assert(error == 0);
	error = uvc_desc_control_feed(&control, ct, sizeof(ct));
	assert(error == 0);
	assert(control.processing_unit_id == 2U);
	assert(control.processing_controls == 0x1603U);
	assert(control.camera_terminal_id == 1U);
	assert(control.camera_controls == 0x02000aU);

	/* Presence follows the bits and names the unit. */
	map = uvc_control_lookup(CAM_CONTROL_BRIGHTNESS);
	assert(map != NULL && uvc_control_present(&control, map, &unit_id) && unit_id == 2U);
	map = uvc_control_lookup(CAM_CONTROL_HUE);
	assert(map != NULL && !uvc_control_present(&control, map, &unit_id));
	map = uvc_control_lookup(CAM_CONTROL_POWER_LINE);
	assert(map != NULL && uvc_control_present(&control, map, &unit_id));
	map = uvc_control_lookup(CAM_CONTROL_WHITE_BALANCE_AUTO);
	assert(map != NULL && uvc_control_present(&control, map, &unit_id));
	map = uvc_control_lookup(CAM_CONTROL_FOCUS_AUTO);
	assert(map != NULL && uvc_control_present(&control, map, &unit_id) && unit_id == 1U);
	map = uvc_control_lookup(CAM_CONTROL_EXPOSURE);
	assert(map != NULL && uvc_control_present(&control, map, &unit_id));
	map = uvc_control_lookup(CAM_CONTROL_ZOOM);
	assert(map != NULL && !uvc_control_present(&control, map, &unit_id));
	assert(uvc_control_lookup(CAM_CONTROL_SENSOR_GAIN) == NULL);
	assert(uvc_control_at(0U) != NULL);
	assert(uvc_control_at(64U) == NULL);

	/* A cut-short unit has no controls. */
	memset(&control, 0, sizeof(control));
	error = uvc_desc_control_feed(&control, short_pu, sizeof(short_pu));
	assert(error == 0);
	assert(control.processing_unit == 1U && control.processing_controls == 0U);

	/* Signed brightness, unsigned contrast, the exposure's 32 bits. */
	map = uvc_control_lookup(CAM_CONTROL_BRIGHTNESS);
	error = uvc_control_encode(map, -64, bytes);
	assert(error == 0 && bytes[0] == 0xc0U && bytes[1] == 0xffU);
	assert(uvc_control_decode(map, bytes) == -64);
	assert(uvc_control_encode(map, 40000, bytes) == EINVAL);
	assert(uvc_control_encode(map, -32768, bytes) == 0);
	map = uvc_control_lookup(CAM_CONTROL_CONTRAST);
	assert(uvc_control_encode(map, -1, bytes) == EINVAL);
	assert(uvc_control_encode(map, 65535, bytes) == 0 && uvc_control_decode(map, bytes) == 65535);
	map = uvc_control_lookup(CAM_CONTROL_EXPOSURE);
	assert(uvc_control_encode(map, 156, bytes) == 0);
	assert(bytes[0] == 156U && bytes[1] == 0U && bytes[2] == 0U && bytes[3] == 0U);
	assert(uvc_control_decode(map, bytes) == 156);
	map = uvc_control_lookup(CAM_CONTROL_POWER_LINE);
	assert(map->size == 1U && map->minimum == 0 && map->maximum == 2);

	/* The auto-exposure switch and the device's modes. */
	assert(uvc_ae_mode_choose(0x09U, 1) == UVC_AE_APERTURE_PRIORITY);
	assert(uvc_ae_mode_choose(0x03U, 1) == UVC_AE_AUTO);
	assert(uvc_ae_mode_choose(0x09U, 0) == UVC_AE_MANUAL);
	assert(uvc_ae_mode_choose(0x01U, 1) == 0U);
	assert(uvc_ae_mode_choose(0x08U, 0) == 0U);
	assert(uvc_ae_mode_automatic(UVC_AE_APERTURE_PRIORITY) == 1);
	assert(uvc_ae_mode_automatic(UVC_AE_AUTO) == 1);
	assert(uvc_ae_mode_automatic(UVC_AE_MANUAL) == 0);
	assert(uvc_ae_mode_automatic(UVC_AE_SHUTTER_PRIORITY) == 0);
	printf("controls: PASS\n");
}

/* Parses the streaming descriptors into stream. */
static void
stream_parse(
	struct uvc_stream_desc *stream)
{
	int error;

	memset(stream, 0, sizeof(*stream));
	error = uvc_desc_stream_feed(stream, vs_header, sizeof(vs_header));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_mjpeg, sizeof(vs_mjpeg));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_mjpeg_frame1, sizeof(vs_mjpeg_frame1));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_mjpeg_frame2, sizeof(vs_mjpeg_frame2));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_color, sizeof(vs_color));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_yuy2, sizeof(vs_yuy2));
	assert(error == 0);
	error = uvc_desc_stream_feed(stream, vs_yuy2_frame, sizeof(vs_yuy2_frame));
	assert(error == 0);
}

/* Checks the formats, frames and intervals, and that a short descriptor stops the parsing. */
static void
case_stream(
	void)
{
	struct uvc_stream_desc stream;
	const struct uvc_format *mjpeg;
	const struct uvc_format *yuyv;
	const struct uvc_frame *frame;
	unsigned position;
	int error;

	stream_parse(&stream);
	assert(stream.has_header);
	assert(stream.endpoint_address == 0x81U);
	assert(stream.format_count == 2U);

	/* MJPEG, read with its own offsets. */
	mjpeg = uvc_desc_find_format(&stream, CAM_FORMAT_MJPEG);
	assert(mjpeg != NULL);
	assert(mjpeg->kind == UVC_FORMAT_MJPEG);
	assert(mjpeg->index == 1U);
	assert(mjpeg->default_frame == 1U);
	assert(mjpeg->frame_count == 2U);
	assert(mjpeg->has_color);
	assert(mjpeg->color_matrix == 4U);
	frame = uvc_desc_find_frame(mjpeg, 640U, 480U, &position);
	assert(frame != NULL && position == 0U);
	assert(frame->interval_type == 2U && frame->interval_count == 2U);
	assert(frame->intervals[1] == 666666U);
	assert(frame->max_frame_bytes == 614400U);
	frame = uvc_desc_find_frame(mjpeg, 1920U, 1080U, &position);
	assert(frame != NULL && position == 1U);
	assert(frame->interval_type == 0U);
	assert(frame->interval_max == 1000000U);

	/* YUY2 by its GUID. */
	yuyv = uvc_desc_find_format(&stream, CAM_FORMAT_YUYV);
	assert(yuyv != NULL);
	assert(yuyv->bits_per_pixel == 16U);
	frame = uvc_desc_find_frame(yuyv, 320U, 240U, NULL);
	assert(frame != NULL);
	assert(uvc_desc_stride(yuyv, frame) == 640U);
	assert(uvc_desc_image_bytes(yuyv, frame, 0U) == 153600U);
	assert(uvc_desc_offered_format(&stream, 0U) == mjpeg);
	assert(uvc_desc_offered_format(&stream, 1U) == yuyv);
	assert(uvc_desc_offered_format(&stream, 2U) == NULL);

	/* An MJPEG frame size from the answer, the buffer, or two bytes a pixel. */
	frame = uvc_desc_find_frame(mjpeg, 640U, 480U, NULL);
	assert(uvc_desc_image_bytes(mjpeg, frame, 200000U) == 200000U);
	assert(uvc_desc_image_bytes(mjpeg, frame, 0U) == 614400U);
	frame = uvc_desc_find_frame(mjpeg, 1920U, 1080U, NULL);
	assert(uvc_desc_image_bytes(mjpeg, frame, 0U) == 1920U * 1080U * 2U);

	/* A frame cut off stops the parsing, and the formats read before stay. */
	error = uvc_desc_stream_feed(&stream, vs_short_frame, sizeof(vs_short_frame));
	assert(error == EINVAL);
	assert(stream.stopped);
	error = uvc_desc_stream_feed(&stream, vs_yuy2_frame, sizeof(vs_yuy2_frame));
	assert(error == EINVAL);
	assert(stream.format_count == 2U);
	printf("streaming descriptors: PASS\n");
}

/* Checks the PROBE request and the corrections of its answer. */
static void
case_probe(
	void)
{
	struct uvc_stream_desc stream;
	const struct uvc_format *mjpeg;
	const struct uvc_format *yuyv;
	const struct uvc_frame *frame;
	uint8_t record[UVC_PROBE_MAX];
	unsigned changed;

	stream_parse(&stream);
	mjpeg = uvc_desc_find_format(&stream, CAM_FORMAT_MJPEG);
	yuyv = uvc_desc_find_format(&stream, CAM_FORMAT_YUYV);

	/* The request carries the hint, the indices and the interval. */
	uvc_probe_request(record, UVC_PROBE_LENGTH_1_1, 1U, 2U, 333333U);
	assert(uvc_get16(record, UVC_PROBE_HINT) == UVC_PROBE_HINT_FRAME_INTERVAL);
	assert(record[UVC_PROBE_FORMAT_INDEX] == 1U);
	assert(record[UVC_PROBE_FRAME_INDEX] == 2U);
	assert(uvc_get32(record, UVC_PROBE_FRAME_INTERVAL) == 333333U);

	/* A sign-extended payload, then estimated from the largest alternate for MJPEG. */
	frame = uvc_desc_find_frame(mjpeg, 640U, 480U, NULL);
	uvc_put32(record, UVC_PROBE_MAX_PAYLOAD, 0xffff0c00U);
	changed = uvc_probe_fixup(record, mjpeg, frame, 0x0110U, 1U, UVC_SPEED_HIGH, 3072U);
	assert((changed & UVC_FIXUP_SIGN_EXTENDED) != 0U);
	assert(uvc_get32(record, UVC_PROBE_MAX_PAYLOAD) == 3072U);

	/* A payload larger than the device's largest alternate is clamped. */
	uvc_put32(record, UVC_PROBE_MAX_PAYLOAD, 5000U);
	changed = uvc_probe_fixup(record, mjpeg, frame, 0x0110U, 1U, UVC_SPEED_HIGH, 3072U);
	assert((changed & UVC_FIXUP_CLAMPED) != 0U);
	assert(uvc_get32(record, UVC_PROBE_MAX_PAYLOAD) == 3072U);

	/* An uncompressed frame size is the descriptor's; a zero payload is estimated from the pixels. */
	frame = uvc_desc_find_frame(yuyv, 320U, 240U, NULL);
	uvc_probe_request(record, UVC_PROBE_LENGTH_1_1, 2U, 1U, 666666U);
	uvc_put32(record, UVC_PROBE_MAX_FRAME_SIZE, 1U);
	changed = uvc_probe_fixup(record, yuyv, frame, 0x0110U, 1U, UVC_SPEED_HIGH, 3072U);
	assert((changed & UVC_FIXUP_FRAME_SIZE) != 0U);
	assert(uvc_get32(record, UVC_PROBE_MAX_FRAME_SIZE) == 153600U);
	assert((changed & UVC_FIXUP_ESTIMATED) != 0U);
	assert(uvc_get32(record, UVC_PROBE_MAX_PAYLOAD) == 1024U);
	printf("probe: PASS\n");
}

/* Checks the alternates: the host's mark, the smallest that fits, high bandwidth, and ENOSPC. */
static void
case_alternates(
	void)
{
	struct uvc_alternate alternates[5];
	unsigned position;
	int error;

	uvc_alternate_describe(&alternates[0], 1U, 0x81U, 128U, 1U, UVC_SPEED_HIGH, 0U);
	uvc_alternate_describe(&alternates[1], 2U, 0x81U, 1024U, 1U, UVC_SPEED_HIGH, 0U);
	uvc_alternate_describe(&alternates[2], 3U, 0x81U, 512U, 1U, UVC_SPEED_HIGH, 0U);
	uvc_alternate_describe(&alternates[3], 4U, 0x81U, (uint16_t)(0x1000U | 1024U), 1U, UVC_SPEED_HIGH, 0U);
	uvc_alternate_describe(&alternates[4], 5U, 0x81U, (uint16_t)(0x1800U | 1024U), 1U, UVC_SPEED_HIGH, 0U);
	assert(alternates[0].accepted && alternates[1].accepted && alternates[2].accepted);
	assert(alternates[3].accepted && alternates[3].transactions == 3U);
	assert(alternates[3].capacity == 3072U);
	assert(!alternates[4].accepted);
	assert(uvc_alternate_host_max(alternates, 5U) == 3072U);
	assert(uvc_alternate_device_max(alternates, 5U) == 4096U);

	/* The smallest that holds the payload. */
	error = uvc_alternate_choose(alternates, 5U, 300U, &position);
	assert(error == 0 && position == 2U);
	error = uvc_alternate_choose(alternates, 5U, 1024U, &position);
	assert(error == 0 && position == 1U);

	/* A high-bandwidth payload takes the three-transaction alternate. */
	error = uvc_alternate_choose(alternates, 5U, 2048U, &position);
	assert(error == 0 && position == 3U);

	/* None the host takes holds more than three transactions of 1024. */
	error = uvc_alternate_choose(alternates, 5U, 3073U, &position);
	assert(error == ENOSPC);

	/* SuperSpeed is not taken; full speed is, but not with extra transactions; a packet over 1024 is not. */
	uvc_alternate_describe(&alternates[0], 1U, 0x81U, 1024U, 1U, UVC_SPEED_SUPER, 49152U);
	assert(!alternates[0].accepted && alternates[0].capacity == 49152U);
	uvc_alternate_describe(&alternates[0], 1U, 0x81U, 1023U, 1U, UVC_SPEED_FULL, 0U);
	assert(alternates[0].accepted);
	uvc_alternate_describe(&alternates[0], 1U, 0x81U, (uint16_t)(0x0800U | 512U), 1U, UVC_SPEED_FULL, 0U);
	assert(!alternates[0].accepted);
	uvc_alternate_describe(&alternates[0], 1U, 0x81U, 1100U, 1U, UVC_SPEED_HIGH, 0U);
	assert(!alternates[0].accepted);
	printf("alternates: PASS\n");
}

/* ---------------------------------------------------------------- */
/* The assembler.                                                   */

/* Checks FID toggling, EOF, the same FID after EOF, and the sequence. */
static void
case_frames(
	void)
{
	struct uvc_assembler assembler;
	const uint8_t data[4] = { 1, 2, 3, 4 };
	uint8_t payload[32];
	size_t length;

	fake_reset(FAKE_SLOTS);
	memset(&assembler, 0, sizeof(assembler));
	uvc_assembler_start(&assembler, NULL, CAM_FORMAT_YUYV, 8U, 0U);

	/* Frame 1: two payloads with FID 0, EOF on the second. */
	length = payload_make(payload, 0x00U, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 1U);
	length = payload_make(payload, UVC_PAYLOAD_EOF, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 2U);
	assert(done_count == 1U);
	assert(done[0].used == 8U);
	assert((done[0].flags & (CAM_FRAME_ERROR | CAM_FRAME_INCOMPLETE)) == 0U);
	assert(done[0].sequence == 1U);

	/* A payload with the same FID after EOF is thrown away. */
	length = payload_make(payload, 0x00U, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 3U);
	assert(done_count == 1U);

	/* Frame 2 ends by the FID change alone, short: incomplete, not an error. */
	length = payload_make(payload, UVC_PAYLOAD_FID, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 4U);
	length = payload_make(payload, 0x00U, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 5U);
	assert(done_count == 2U);
	assert(done[1].used == 4U);
	assert((done[1].flags & CAM_FRAME_INCOMPLETE) != 0U);
	assert((done[1].flags & CAM_FRAME_ERROR) == 0U);
	assert(done[1].sequence == 2U);

	/* The FID change ends frame 3; the new frame 4 gets no data. */
	length = payload_make(payload, UVC_PAYLOAD_FID, data, 0U);
	uvc_assembler_payload(&assembler, payload, length, 6U);
	assert(done_count == 3U);

	/* The empty frame 4 is not delivered: its slot is given back and taken again for frame 5. */
	length = payload_make(payload, UVC_PAYLOAD_EOF, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 7U);
	assert(discards == 1U);
	assert(done_count == 4U);
	assert(done[3].sequence == 5U);
	printf("frames: PASS\n");
}

/* Checks broken headers, ERR, missed packets, PTS, overflow and running out of slots. */
static void
case_errors(
	void)
{
	struct uvc_assembler assembler;
	uint8_t data[80];
	uint8_t payload[100];
	size_t length;

	fake_reset(1U);
	memset(data, 7, sizeof(data));
	memset(&assembler, 0, sizeof(assembler));
	uvc_assembler_start(&assembler, NULL, CAM_FORMAT_YUYV, 8U, 0U);

	/* A header longer than its payload is counted and spoils the frame. */
	length = payload_make(payload, 0x00U, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 1U);
	payload[0] = 40U;
	uvc_assembler_payload(&assembler, payload, 6U, 2U);
	assert(assembler.invalid_header == 1U);
	uvc_assembler_packet_lost(&assembler, UVC_PACKET_MISSED);
	assert(assembler.missed_packets == 1U);
	length = payload_make(payload, UVC_PAYLOAD_EOF, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 3U);
	assert(done_count == 1U);
	assert((done[0].flags & CAM_FRAME_ERROR) != 0U);

	/* PTS announced in a two-byte header is not read. */
	payload[0] = 2U;
	payload[1] = UVC_PAYLOAD_FID | UVC_PAYLOAD_PTS;
	uvc_assembler_payload(&assembler, payload, 6U, 4U);
	assert(assembler.short_header == 1U);

	/* A full PTS header is read. */
	payload[0] = 6U;
	payload[1] = UVC_PAYLOAD_FID | UVC_PAYLOAD_PTS | UVC_PAYLOAD_EOF;
	payload[2] = 0x78U;
	payload[3] = 0x56U;
	payload[4] = 0x34U;
	payload[5] = 0x12U;
	payload[6] = 9U;
	uvc_assembler_payload(&assembler, payload, 7U, 5U);
	assert(done_count == 2U);
	assert((done[1].flags & CAM_METADATA_PTS) != 0U);
	assert(done[1].pts == 0x12345678U);

	/* An overflow ends the frame with what fits and drops the rest. */
	length = payload_make(payload, 0x00U, data, 80U);
	uvc_assembler_payload(&assembler, payload, length, 6U);
	assert(dropped_overflow == 1U);
	assert(done_count == 3U);
	assert(done[2].used == FAKE_SLOT_BYTES);
	assert((done[2].flags & CAM_FRAME_ERROR) != 0U);

	/* Without a free slot the frame is dropped once and the sequence still moves. */
	free_slots = 0U;
	length = payload_make(payload, UVC_PAYLOAD_FID, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 7U);
	uvc_assembler_payload(&assembler, payload, length, 8U);
	assert(dropped_no_buffer == 1U);
	free_slots = 1U;
	length = payload_make(payload, UVC_PAYLOAD_EOF, data, 4U);
	payload[1] = UVC_PAYLOAD_EOF;
	uvc_assembler_payload(&assembler, payload, length, 9U);
	assert(done_count == 4U);
	assert(done[3].sequence == done[2].sequence + 2U);
	printf("errors: PASS\n");
}

/* Checks the MJPEG markers: SOI, EOI with padding, and no EOI. */
static void
case_mjpeg(
	void)
{
	struct uvc_assembler assembler;
	uint8_t jpeg[16];
	uint8_t payload[32];
	size_t length;

	fake_reset(FAKE_SLOTS);
	memset(&assembler, 0, sizeof(assembler));
	uvc_assembler_start(&assembler, NULL, CAM_FORMAT_MJPEG, FAKE_SLOT_BYTES, 0U);

	/* SOI, data, EOI and zero padding: a clean frame. */
	memset(jpeg, 0, sizeof(jpeg));
	jpeg[0] = 0xffU;
	jpeg[1] = 0xd8U;
	jpeg[6] = 0xffU;
	jpeg[7] = 0xd9U;
	length = payload_make(payload, UVC_PAYLOAD_EOF, jpeg, 12U);
	uvc_assembler_payload(&assembler, payload, length, 1U);
	assert(done_count == 1U);
	assert((done[0].flags & (CAM_FRAME_ERROR | CAM_FRAME_INCOMPLETE)) == 0U);

	/* No EOI: incomplete. */
	jpeg[7] = 0x00U;
	length = payload_make(payload, UVC_PAYLOAD_FID | UVC_PAYLOAD_EOF, jpeg, 12U);
	uvc_assembler_payload(&assembler, payload, length, 2U);
	assert((done[1].flags & CAM_FRAME_INCOMPLETE) != 0U);

	/* No SOI: an error. */
	jpeg[0] = 0x00U;
	jpeg[7] = 0xd9U;
	length = payload_make(payload, UVC_PAYLOAD_EOF, jpeg, 12U);
	uvc_assembler_payload(&assembler, payload, length, 3U);
	assert((done[2].flags & CAM_FRAME_ERROR) != 0U);
	printf("mjpeg: PASS\n");
}

/* Checks a device that does not toggle FID: EOF ends each frame. */
static void
case_no_fid(
	void)
{
	struct uvc_assembler assembler;
	const uint8_t data[4] = { 1, 2, 3, 4 };
	uint8_t payload[16];
	size_t length;

	fake_reset(FAKE_SLOTS);
	memset(&assembler, 0, sizeof(assembler));
	uvc_assembler_start(&assembler, NULL, CAM_FORMAT_YUYV, 4U, 1U);
	length = payload_make(payload, UVC_PAYLOAD_EOF, data, 4U);
	uvc_assembler_payload(&assembler, payload, length, 1U);
	uvc_assembler_payload(&assembler, payload, length, 2U);
	uvc_assembler_payload(&assembler, payload, length, 3U);
	assert(done_count == 3U);
	printf("no FID toggle: PASS\n");
}

/* Checks bulk payloads that span transfers and end short or by their length. */
static void
case_bulk(
	void)
{
	struct uvc_assembler assembler;
	uint8_t transfer[16];

	fake_reset(FAKE_SLOTS);
	memset(&assembler, 0, sizeof(assembler));
	uvc_assembler_start(&assembler, NULL, CAM_FORMAT_YUYV, 20U, 0U);

	/* A payload of 26 bytes over two full transfers of 16 and a short one. */
	memset(transfer, 5, sizeof(transfer));
	transfer[0] = 2U;
	transfer[1] = UVC_PAYLOAD_EOF;
	uvc_assembler_bulk(&assembler, transfer, 16U, 16U, 0U, 1U);
	assert(done_count == 0U);
	memset(transfer, 6, sizeof(transfer));
	uvc_assembler_bulk(&assembler, transfer, 6U, 16U, 0U, 2U);
	assert(done_count == 1U);
	assert(done[0].used == 20U);

	/* A payload that ends when payload_bytes have come, with full transfers. */
	transfer[0] = 2U;
	transfer[1] = UVC_PAYLOAD_FID | UVC_PAYLOAD_EOF;
	uvc_assembler_bulk(&assembler, transfer, 16U, 16U, 32U, 3U);
	uvc_assembler_bulk(&assembler, transfer, 16U, 16U, 32U, 4U);
	assert(done_count == 2U);

	/* An empty transfer between payloads is nothing. */
	uvc_assembler_bulk(&assembler, transfer, 0U, 16U, 32U, 5U);
	assert(assembler.invalid_header == 0U);
	printf("bulk: PASS\n");
}

/* Runs every case. */
int
main(
	void)
{
	case_control();
	case_controls();
	case_stream();
	case_probe();
	case_alternates();
	case_frames();
	case_errors();
	case_mjpeg();
	case_no_fid();
	case_bulk();
	printf("uvc-host-test: PASS\n");

	return 0;
}
