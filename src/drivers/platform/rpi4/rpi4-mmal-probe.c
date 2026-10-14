/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The boot-time probe of the VideoCore H.264 decoder (ws236 design §12),
 * built only by test kernels (CONFIG_DRIVER_BCM2711_VIDEO_PROBE).
 *
 * It decodes two embedded streams with B frames, each twice: as encoded
 * (the control) and with every SPS rewritten to ask for no reordering.
 * Each output picture is identified by the checksum of its visible bytes
 * against the reference decode, and its place in decode order is compared
 * with the input that was last sent, which shows whether the firmware
 * outputs in decode order without delay.  Everything is written to the
 * kernel log, one line per output and one summary per run.
 */

#include "drivers/platform/rpi4/rpi4-mmal-probe.h"
#include "drivers/platform/rpi4/rpi4-mmal.h"
#include "drivers/platform/rpi4/rpi4-h264.h"
#include "drivers/platform/rpi4/rpi4-vchiq.h"
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <uapi/errno.h>
#include <uapi/time.h>

#include "drivers/platform/rpi4/rpi4-mmal-probe-data.inc"

/* How long the probe waits for VCHIQ to connect after boot. */
#define PROBE_CONNECT_MS		15000U

/* How long it waits for an output after each input, and at the end. */
#define PROBE_OUTPUT_MS			100U
#define PROBE_DRAIN_MS			500U

/* The largest access unit of the embedded streams, with room for a rewritten SPS. */
#define PROBE_INPUT_BYTES		(1024U * 1024U)

/* The most frames a stream has. */
#define PROBE_MAX_FRAMES		32U

/* The FNV-1a 32 offset basis and prime. */
#define PROBE_FNV_BASIS			0x811c9dc5U
#define PROBE_FNV_PRIME			0x01000193U

/* One embedded stream and its reference. */
struct probe_stream {
	const char *name;
	const uint8_t *data;
	size_t bytes;
	uint32_t width;
	uint32_t height;
	const uint32_t *display_sums;
	const uint8_t *decode_order;
	unsigned frames;
};

/* What one run saw. */
struct probe_result {
	unsigned outputs;
	unsigned in_decode_order;
	unsigned in_display_order;
	unsigned unknown;
	unsigned max_lag;
	int last_display;
	int error;
};

static void probe_main(void *argument);
static void probe_run(const struct probe_stream *stream, int rewrite);
static int send_unit(struct drv_rpi4_mmal_decoder *decoder, const struct probe_stream *stream, size_t start, size_t end, int rewrite, int64_t pts);
static void collect(struct drv_rpi4_mmal_decoder *decoder, const struct probe_stream *stream, const char *run, unsigned sent, unsigned timeout_ms, struct probe_result *result);
static uint32_t frame_sum(const struct drv_rpi4_mmal_frame *frame, const uint8_t *pixels);
static uint64_t now_us(void);

/*
 * Starts the probe's thread.
 */
int
drv_rpi4_mmal_probe_start(void)
{
	struct thread *thread;
	int error;

	/* The probe runs once, in its own thread, after VCHIQ connects. */
	error = kthread_create(probe_main, NULL, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0) {
		kern_logf("rpi4-mmal-probe: no thread (%d)\n", error);
		return error;
	}
	thread_start(thread);

	/* Succeeded: the probe reports in the kernel log. */
	return 0;
}

/* Waits for VCHIQ, then runs both streams both ways. */
static void
probe_main(
	void *argument)
{
	struct probe_stream small;
	struct probe_stream large;
	uint64_t deadline;
	bool connected;

	/* Says that it runs, then waits for the firmware's connection. */
	(void)argument;
	kern_logf("rpi4-mmal-probe: started; waiting up to %u ms for VCHIQ\n", PROBE_CONNECT_MS);
	deadline = sched_ticks() + kern_ms_to_ticks(PROBE_CONNECT_MS);
	connected = drv_rpi4_vchiq_connected();
	while (!connected && sched_ticks() < deadline) {
		kern_usleep_range(100000U, 110000U);
		connected = drv_rpi4_vchiq_connected();
	}
	if (!connected) {
		kern_logf("rpi4-mmal-probe: VCHIQ did not connect in %u ms\n", PROBE_CONNECT_MS);
		return;
	}

	/* The two streams. */
	small.name = "352";
	small.data = probe_small_stream;
	small.bytes = sizeof(probe_small_stream);
	small.width = probe_small_width;
	small.height = probe_small_height;
	small.display_sums = probe_small_display_sums;
	small.decode_order = probe_small_decode_order;
	small.frames = sizeof(probe_small_decode_order);
	large.name = "1080";
	large.data = probe_large_stream;
	large.bytes = sizeof(probe_large_stream);
	large.width = probe_large_width;
	large.height = probe_large_height;
	large.display_sums = probe_large_display_sums;
	large.decode_order = probe_large_decode_order;
	large.frames = sizeof(probe_large_decode_order);

	/* The control and the rewritten stream of each. */
	probe_run(&small, 0);
	probe_run(&small, 1);
	probe_run(&large, 0);
	probe_run(&large, 1);
	kern_logf("rpi4-mmal-probe: done\n");
}

/* Decodes one stream once and logs what came out. */
static void
probe_run(
	const struct probe_stream *stream,
	int rewrite)
{
	struct drv_rpi4_mmal_decoder *decoder;
	struct probe_result result;
	const char *run;
	const char *order;
	size_t offset;
	size_t end;
	unsigned sent;
	int error;

	/* The run's name: a for the control, b for the rewritten stream. */
	run = "a";
	if (rewrite)
		run = "b";
	kern_memset(&result, 0, sizeof(result));
	result.last_display = -1;

	/* A decoder of the stream's size. */
	error = drv_rpi4_mmal_decoder_create(stream->width, stream->height, PROBE_INPUT_BYTES, &decoder);
	if (error != 0) {
		kern_logf("rpi4-mmal-probe: %s run=%s decoder not created (%d)\n", stream->name, run, error);
		return;
	}

	/* Sends each access unit and takes whatever comes out right after it. */
	offset = 0;
	sent = 0;
	while (sent < PROBE_MAX_FRAMES) {
		error = drv_rpi4_h264_access_unit(stream->data, stream->bytes, offset, &end);
		if (error != 0)
			break;
		error = send_unit(decoder, stream, offset, end, rewrite, (int64_t)sent);
		if (error != 0) {
			kern_logf("rpi4-mmal-probe: %s run=%s input %u not sent (%d)\n", stream->name, run, sent, error);
			result.error = error;
			break;
		}
		sent++;
		offset = end;
		collect(decoder, stream, run, sent, PROBE_OUTPUT_MS, &result);
		if (result.error != 0)
			break;
	}

	/* Takes what is left (without an end of stream the last held pictures may stay). */
	if (result.error == 0)
		collect(decoder, stream, run, sent, PROBE_DRAIN_MS, &result);
	drv_rpi4_mmal_decoder_destroy(decoder);

	/* The run's order: decode order without delay, display order, or neither. */
	order = "other";
	if (result.outputs != 0 && result.in_decode_order == result.outputs && result.max_lag == 0)
		order = "decode";
	else if (result.outputs != 0 && result.in_display_order == result.outputs)
		order = "display";
	kern_logf("rpi4-mmal-probe: %s run=%s order=%s inputs=%u outputs=%u missing=%u unknown=%u lag=%u error=%d\n",
		  stream->name, run, order, sent, result.outputs, stream->frames - result.outputs,
		  result.unknown, result.max_lag, result.error);
}

/*
 * Copies one access unit into the decoder's staging, rewriting its SPS
 * when asked, and sends it.
 */
static int
send_unit(
	struct drv_rpi4_mmal_decoder *decoder,
	const struct probe_stream *stream,
	size_t start,
	size_t end,
	int rewrite,
	int64_t pts)
{
	uint8_t *staging;
	uint32_t capacity;
	size_t nal_start;
	size_t nal_end;
	size_t written;
	size_t used;
	size_t cursor;
	int error;

	/* The staging the unit goes into. */
	error = drv_rpi4_mmal_decoder_input(decoder, &staging, &capacity);
	if (error != 0)
		return error;

	/* Copies each NAL unit with a four-byte start code; an SPS is rewritten in the second run. */
	used = 0;
	cursor = start;
	for (;;) {
		error = drv_rpi4_h264_next_nal(stream->data, end, cursor, &nal_start, &nal_end);
		if (error != 0)
			break;
		if (rewrite && (stream->data[nal_start] & 0x1fU) == DRV_RPI4_H264_NAL_SPS) {
			if (capacity - used < DRV_RPI4_H264_SPS_MAX)
				return ENOSPC;
			error = drv_rpi4_h264_sps_no_reorder(stream->data + nal_start, nal_end - nal_start,
							     staging + used, DRV_RPI4_H264_SPS_MAX, &written);
			if (error != 0)
				return error;
			used += written;
		} else {
			if (capacity - used < 4U + (nal_end - nal_start))
				return ENOSPC;
			staging[used] = 0;
			staging[used + 1U] = 0;
			staging[used + 2U] = 0;
			staging[used + 3U] = 1;
			kern_memcpy(staging + used + 4U, stream->data + nal_start, nal_end - nal_start);
			used += 4U + (nal_end - nal_start);
		}
		cursor = nal_end;
	}

	/* Sends the unit with its decode index as the time stamp. */
	error = drv_rpi4_mmal_decoder_send(decoder, (uint32_t)used, pts);
	if (error != 0)
		return error;

	/* Succeeded: the firmware has the unit. */
	return 0;
}

/*
 * Receives every frame that comes within timeout_ms, identifies it and
 * logs it.  sent is the number of inputs sent so far.
 */
static void
collect(
	struct drv_rpi4_mmal_decoder *decoder,
	const struct probe_stream *stream,
	const char *run,
	unsigned sent,
	unsigned timeout_ms,
	struct probe_result *result)
{
	struct drv_rpi4_mmal_frame frame;
	const uint8_t *pixels;
	uint8_t *copy;
	uint64_t started;
	uint64_t copy_us;
	uint32_t sum;
	unsigned index;
	unsigned lag;
	int display;
	int decode;
	int error;

	/* Takes frames until none comes in time. */
	for (;;) {
		error = drv_rpi4_mmal_decoder_receive(decoder, timeout_ms, &frame, &pixels);
		if (error == ETIMEDOUT)
			return;
		if (error != 0) {
			result->error = error;
			return;
		}

		/* An empty end of stream carries no picture. */
		if (frame.length == 0)
			continue;

		/* Identifies the picture by its checksum. */
		sum = frame_sum(&frame, pixels);
		display = -1;
		for (index = 0; index < stream->frames; index++) {
			if (stream->display_sums[index] == sum) {
				display = (int)index;
				break;
			}
		}
		decode = -1;
		for (index = 0; index < stream->frames; index++) {
			if (display >= 0 && stream->decode_order[index] == (uint8_t)display) {
				decode = (int)index;
				break;
			}
		}

		/* The lag: inputs sent after the picture's own. */
		lag = 0;
		if (decode >= 0 && sent > (unsigned)decode + 1U)
			lag = sent - (unsigned)decode - 1U;
		if (lag > result->max_lag)
			result->max_lag = lag;

		/* Counts the orders the output agrees with. */
		if (decode < 0) {
			result->unknown++;
		} else {
			if (decode == (int)result->outputs)
				result->in_decode_order++;
			if (display == result->last_display + 1)
				result->in_display_order++;
			result->last_display = display;
		}
		result->outputs++;

		/* The time to copy a frame out of the uncached bounce (the large stream only). */
		copy_us = 0;
		if (frame.length > 1024U * 1024U) {
			copy = kern_malloc(frame.length);
			if (copy != NULL) {
				started = now_us();
				kern_memcpy(copy, pixels, frame.length);
				copy_us = now_us() - started;
				kern_free(copy);
			}
		}

		kern_logf("rpi4-mmal-probe: %s run=%s out=%u pts=%lld display=%d decode=%d sent=%u lag=%u sum=%08x flags=%x len=%u fmt=%08x %ux%u crop %ux%u pitch %u/%u copy_us=%u\n",
			  stream->name, run, result->outputs - 1U, (long long)frame.pts, display, decode, sent, lag,
			  (unsigned)sum, (unsigned)frame.flags, (unsigned)frame.length, (unsigned)frame.encoding,
			  (unsigned)frame.width, (unsigned)frame.height, (unsigned)frame.crop_width, (unsigned)frame.crop_height,
			  (unsigned)frame.plane_pitch[0], (unsigned)frame.plane_pitch[1], (unsigned)copy_us);
	}
}

/*
 * Computes FNV-1a 32 over a frame's visible NV12 bytes: the luma rows,
 * then the interleaved chroma rows.
 */
static uint32_t
frame_sum(
	const struct drv_rpi4_mmal_frame *frame,
	const uint8_t *pixels)
{
	uint32_t sum;
	uint32_t pitch_y;
	uint32_t pitch_uv;
	uint32_t offset_uv;
	uint32_t row;
	uint32_t column;
	const uint8_t *line;

	/* The planes' layout, from the buffer, else the format's stride and height. */
	pitch_y = frame->plane_pitch[0];
	pitch_uv = frame->plane_pitch[1];
	offset_uv = frame->plane_offset[1];
	if (frame->planes < 2U || pitch_y == 0) {
		pitch_y = frame->width;
		pitch_uv = frame->width;
		offset_uv = frame->width * frame->height;
	}

	/* The luma rows. */
	sum = PROBE_FNV_BASIS;
	for (row = 0; row < frame->crop_height; row++) {
		line = pixels + frame->plane_offset[0] + (size_t)row * pitch_y;
		for (column = 0; column < frame->crop_width; column++) {
			sum ^= line[column];
			sum *= PROBE_FNV_PRIME;
		}
	}

	/* The chroma rows. */
	for (row = 0; row < frame->crop_height / 2U; row++) {
		line = pixels + offset_uv + (size_t)row * pitch_uv;
		for (column = 0; column < frame->crop_width; column++) {
			sum ^= line[column];
			sum *= PROBE_FNV_PRIME;
		}
	}

	/* Succeeded: the checksum. */
	return sum;
}

/* Reports the monotonic time in microseconds. */
static uint64_t
now_us(void)
{
	struct timespec now;

	/* The monotonic clock, or zero when it cannot be read. */
	if (kern_clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;

	/* Succeeded: microseconds since boot. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}
