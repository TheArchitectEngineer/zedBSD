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
 * outputs in decode order without delay.  The same is judged again by the
 * output's time stamp (its unit's decode index), which holds even for a
 * picture whose bytes differ; a picture whose luma alone matches points
 * at a chroma layout fault.  Everything is written to the kernel log, one
 * line per output and one summary per run.
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

/* The encoding of an I420 frame (the decoder's default output), summed as NV12. */
#define PROBE_ENCODING_I420		0x30323449U

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
	const uint32_t *luma_sums;
	const uint8_t *decode_order;
	unsigned frames;
};

/*
 * What one run saw: the orders judged by the frames' checksums, and again
 * by their time stamps (each unit's decode index), which a frame keeps
 * even when its pixels differ from the reference; and the time stamps in
 * output order.
 */
struct probe_result {
	unsigned outputs;
	unsigned in_decode_order;
	unsigned in_display_order;
	unsigned unknown;
	unsigned luma_only;
	unsigned max_lag;
	int last_display;
	unsigned pts_in_decode_order;
	unsigned pts_in_display_order;
	unsigned pts_unknown;
	unsigned pts_max_lag;
	int pts_last_display;
	int pts[PROBE_MAX_FRAMES];
	int error;
};

static void probe_main(void *argument);
static void probe_run(const struct probe_stream *stream, int rewrite);
static int send_unit(struct drv_rpi4_mmal_decoder *decoder, const struct probe_stream *stream, size_t start, size_t end, int rewrite, int64_t pts);
static void collect(struct drv_rpi4_mmal_decoder *decoder, const struct probe_stream *stream, const char *run, unsigned sent, unsigned timeout_ms, struct probe_result *result);
static uint32_t frame_sum(const struct drv_rpi4_mmal_frame *frame, const uint8_t *pixels, uint32_t *luma);
static const char *order_name(unsigned outputs, unsigned in_decode_order, unsigned max_lag, unsigned in_display_order);
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
	uint64_t now;
	bool connected;

	/* Says that it runs, then waits for the firmware's connection. */
	(void)argument;
	kern_logf("rpi4-mmal-probe: started; waiting up to %u ms for VCHIQ\n", PROBE_CONNECT_MS);
	deadline = sched_ticks() + kern_ms_to_ticks(PROBE_CONNECT_MS);
	connected = drv_rpi4_vchiq_connected();
	now = sched_ticks();
	while (!connected && now < deadline) {
		kern_usleep_range(100000U, 110000U);
		connected = drv_rpi4_vchiq_connected();
		now = sched_ticks();
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
	small.luma_sums = probe_small_luma_sums;
	small.decode_order = probe_small_decode_order;
	small.frames = sizeof(probe_small_decode_order);
	large.name = "1080";
	large.data = probe_large_stream;
	large.bytes = sizeof(probe_large_stream);
	large.width = probe_large_width;
	large.height = probe_large_height;
	large.display_sums = probe_large_display_sums;
	large.luma_sums = probe_large_luma_sums;
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
	char sequence[PROBE_MAX_FRAMES * 4U + 1U];
	size_t offset;
	size_t end;
	size_t used;
	unsigned sent;
	unsigned index;
	int error;

	/* The run's name: a for the control, b for the rewritten stream. */
	run = "a";
	if (rewrite)
		run = "b";
	kern_memset(&result, 0, sizeof(result));
	result.last_display = -1;
	result.pts_last_display = -1;

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

	/* The time stamps in output order. */
	used = 0;
	sequence[0] = '\0';
	for (index = 0; index < result.outputs && index < PROBE_MAX_FRAMES; index++) {
		(void)kern_snprintf(sequence + used, sizeof(sequence) - used, "%s%d", index == 0 ? "" : ",", result.pts[index]);
		used += kern_strlen(sequence + used);
	}

	/*
	 * The run's order, by the checksums and by the time stamps: decode
	 * order without delay, display order, or neither.
	 */
	kern_logf("rpi4-mmal-probe: %s run=%s order=%s order_pts=%s inputs=%u outputs=%u missing=%u unknown=%u luma_only=%u lag=%u lag_pts=%u pts_unknown=%u error=%d pts_seq=%s\n",
		  stream->name, run,
		  order_name(result.outputs, result.in_decode_order, result.max_lag, result.in_display_order),
		  order_name(result.outputs, result.pts_in_decode_order, result.pts_max_lag, result.pts_in_display_order),
		  sent, result.outputs, stream->frames - result.outputs, result.unknown, result.luma_only,
		  result.max_lag, result.pts_max_lag, result.pts_unknown, result.error, sequence);
}

/* Names an order from the counts that agree with each. */
static const char *
order_name(
	unsigned outputs,
	unsigned in_decode_order,
	unsigned max_lag,
	unsigned in_display_order)
{
	/* Decode order without delay, display order, or neither. */
	if (outputs != 0 && in_decode_order == outputs && max_lag == 0)
		return "decode";
	if (outputs != 0 && in_display_order == outputs)
		return "display";

	/* Succeeded: neither. */
	return "other";
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
	uint32_t luma;
	unsigned index;
	unsigned lag;
	unsigned pts_lag;
	int display;
	int decode;
	int pts_display;
	int pts_decode;
	const char *match;
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

		/* Identifies the picture by its checksum, else by its luma's (a chroma layout fault). */
		sum = frame_sum(&frame, pixels, &luma);
		display = -1;
		match = "none";
		for (index = 0; index < stream->frames; index++) {
			if (stream->display_sums[index] == sum) {
				display = (int)index;
				match = "whole";
				break;
			}
		}
		for (index = 0; index < stream->frames && display < 0; index++) {
			if (stream->luma_sums[index] == luma) {
				display = (int)index;
				match = "luma";
				result->luma_only++;
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

		/* Identifies the picture by its time stamp, the decode index of the unit it came from. */
		pts_decode = -1;
		pts_display = -1;
		if (frame.pts >= 0 && frame.pts < (int64_t)stream->frames) {
			pts_decode = (int)frame.pts;
			pts_display = (int)stream->decode_order[pts_decode];
		}
		pts_lag = 0;
		if (pts_decode >= 0 && sent > (unsigned)pts_decode + 1U)
			pts_lag = sent - (unsigned)pts_decode - 1U;
		if (pts_lag > result->pts_max_lag)
			result->pts_max_lag = pts_lag;

		/* Counts the orders the output agrees with, by checksum and by time stamp. */
		if (decode < 0) {
			result->unknown++;
		} else {
			if (decode == (int)result->outputs)
				result->in_decode_order++;
			if (display == result->last_display + 1)
				result->in_display_order++;
			result->last_display = display;
		}
		if (pts_decode < 0) {
			result->pts_unknown++;
		} else {
			if (pts_decode == (int)result->outputs)
				result->pts_in_decode_order++;
			if (pts_display == result->pts_last_display + 1)
				result->pts_in_display_order++;
			result->pts_last_display = pts_display;
		}
		if (result->outputs < PROBE_MAX_FRAMES)
			result->pts[result->outputs] = pts_decode;
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

		kern_logf("rpi4-mmal-probe: %s run=%s out=%u pts=%lld pts_display=%d display=%d decode=%d match=%s sent=%u lag=%u sum=%08x luma=%08x flags=%x len=%u fmt=%08x %ux%u crop %ux%u pitch %u/%u copy_us=%u\n",
			  stream->name, run, result->outputs - 1U, (long long)frame.pts, pts_display, display, decode, match, sent, lag,
			  (unsigned)sum, (unsigned)luma, (unsigned)frame.flags, (unsigned)frame.length, (unsigned)frame.encoding,
			  (unsigned)frame.width, (unsigned)frame.height, (unsigned)frame.crop_width, (unsigned)frame.crop_height,
			  (unsigned)frame.plane_pitch[0], (unsigned)frame.plane_pitch[1], (unsigned)copy_us);
	}
}

/*
 * Computes FNV-1a 32 over a frame's visible NV12 bytes: the luma rows,
 * then the interleaved chroma rows.  luma receives the checksum of the
 * luma rows alone.
 */
static uint32_t
frame_sum(
	const struct drv_rpi4_mmal_frame *frame,
	const uint8_t *pixels,
	uint32_t *luma)
{
	uint32_t sum;
	uint32_t pitch_y;
	uint32_t pitch_uv;
	uint32_t offset_uv;
	uint32_t row;
	uint32_t column;
	uint32_t offset_v;
	const uint8_t *line;
	const uint8_t *line_v;

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
	*luma = sum;

	/* An I420 frame's chroma is two planes; it is summed interleaved, as NV12's. */
	if (frame->encoding == PROBE_ENCODING_I420) {
		pitch_uv = frame->width / 2U;
		offset_v = frame->width * frame->height + pitch_uv * (frame->height / 2U);
		for (row = 0; row < frame->crop_height / 2U; row++) {
			line = pixels + (size_t)frame->width * frame->height + (size_t)row * pitch_uv;
			line_v = pixels + offset_v + (size_t)row * pitch_uv;
			for (column = 0; column < frame->crop_width / 2U; column++) {
				sum ^= line[column];
				sum *= PROBE_FNV_PRIME;
				sum ^= line_v[column];
				sum *= PROBE_FNV_PRIME;
			}
		}

		/* Succeeded: the checksum. */
		return sum;
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
	int error;

	/* The monotonic clock, or zero when it cannot be read. */
	error = kern_clock_gettime(CLOCK_MONOTONIC, &now);
	if (error != 0)
		return 0;

	/* Succeeded: microseconds since boot. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}
