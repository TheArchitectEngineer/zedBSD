/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The firmware's H.264 decoder over MMAL (see rpi4-mmal.h).
 *
 * The protocol follows the Raspberry Pi userland's BSD-3-Clause MMAL
 * client (interface/mmal/vc/), written anew: requests carry a waiter number
 * the reply echoes; buffers travel as messages whose payload follows as a
 * bulk transfer; a received buffer's bulk is requested in the order the
 * firmware's messages came, its length rounded up to four bytes (eight
 * bytes for an empty end of stream); an input port that has carried bulks
 * is flushed with PORT_FLUSH and an eight-byte dummy bulk.
 *
 * One VCHIQ service serves every decoder.  VCHIQ's receive thread only
 * queues what arrives (replies, returned buffers, events) and counts bulk
 * completions; the caller's thread does everything else, so the client
 * never sends from the receive thread.
 */

#include "drivers/platform/rpi4/rpi4-mmal.h"
#include "drivers/platform/rpi4/rpi4-mmal-wire.h"
#include "drivers/platform/rpi4/rpi4-vchiq.h"
#include <drivers/generic/dma.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/waitq.h>
#include <uapi/errno.h>

/* The decoders one boot may have at once. */
#define MMAL_DECODERS			4U

/* The arrivals a decoder queues before its caller reads them. */
#define MMAL_QUEUE			16U

/* The most input and output buffers a decoder gives the firmware. */
#define MMAL_INPUT_BUFFERS		16U
#define MMAL_OUTPUT_BUFFERS		8U

/* The output buffers a decoder gives at least, so the firmware never waits for the caller. */
#define MMAL_OUTPUT_MINIMUM		3U

/* The bytes of scratch memory: the dummy bulk and an event's bulk data. */
#define MMAL_SCRATCH_BYTES		4096U

/* How long a request, a bulk, or a free buffer is waited for. */
#define MMAL_WAIT_MS			1000U

/* The VPU's window onto the first 1 GiB of RAM, and the bus address of physical zero in it. */
#define MMAL_BUS_RAM			0xc0000000U
#define MMAL_BUS_RAM_LIMIT		0x3fffffffU

/* The largest piece of DMA memory, which must hold a 1080p frame and a staging area. */
#define MMAL_MAX_SEGMENT		(16U * 1024U * 1024U)

/*
 * The client's names for its buffers and components, which the firmware
 * echoes: a tag in the top byte, the decoder, the kind, and the index.
 */
#define MMAL_TOKEN			0x5a000000U
#define MMAL_TOKEN_MASK			0xff000000U
#define MMAL_KIND_INPUT			1U
#define MMAL_KIND_OUTPUT		2U
#define MMAL_KIND_COMPONENT		3U

/* The firmware component that decodes video. */
#define MMAL_COMPONENT_NAME		"ril.video_decode"

/* The parameters the decoder turns off: they would tie time stamps to input order, not to pictures. */
#define MMAL_PARAMETER_TIMESTAMP_FIFO	0x20024U
#define MMAL_PARAMETER_INTERPOLATE	0x2002fU

/* What a bulk transfer's completion belongs to. */
enum mmal_bulk_kind {
	MMAL_BULK_STAGING,
	MMAL_BULK_FRAME,
	MMAL_BULK_SCRATCH
};

/* One message the receive thread queued, with its length. */
struct mmal_message {
	uint32_t size;
	uint8_t bytes[DRV_RPI4_MMAL_EVENT_BYTES];
};

/* The completion tag of one kind of bulk of one decoder. */
struct mmal_bulk_tag {
	struct drv_rpi4_mmal_decoder *decoder;
	enum mmal_bulk_kind kind;
	unsigned index;
};

/*
 * One of the two input staging areas: the input buffers and the bulks of
 * the access unit in it that the firmware has not given back.
 */
struct mmal_staging {
	uint8_t *cpu;
	uint64_t physical;
	uint32_t buffers;
	uint32_t bulks;
};

/*
 * One output buffer: its bounce memory, and whether the firmware has it,
 * a received frame waits in it, or the caller holds it.
 */
struct mmal_output {
	struct drv_dma_buffer memory;
	bool with_firmware;
	bool ready;
	bool held;
	bool in_target;
	uint32_t length;
	uint32_t flags;
	int64_t pts;
	uint32_t planes;
	uint32_t plane_offset[2];
	uint32_t plane_pitch[2];
};

/*
 * One decoder.
 *
 * The decoders live in a static table, so a late bulk completion always
 * finds a valid decoder; a decoder destroyed with a bulk outstanding is
 * retired and its entry is never reused.  The queue and the bulk counts are
 * shared with the receive thread under mmal_lock; everything else belongs
 * to the caller's thread.
 */
struct drv_rpi4_mmal_decoder {
	bool used;
	bool retired;
	bool broken;
	unsigned slot;
	uint32_t component;
	uint8_t input_info[DRV_RPI4_MMAL_INFO_BYTES];
	uint8_t output_info[DRV_RPI4_MMAL_INFO_BYTES];
	uint32_t input_handle;
	uint32_t output_handle;
	uint32_t input_size;
	uint32_t input_count;
	uint32_t output_size;
	uint32_t output_count;
	bool input_free[MMAL_INPUT_BUFFERS];
	unsigned input_half[MMAL_INPUT_BUFFERS];
	struct drv_dma_buffer staging_memory;
	struct mmal_staging half[2];
	unsigned current;
	uint32_t input_bytes;
	struct drv_dma_buffer scratch;
	struct mmal_output outputs[MMAL_OUTPUT_BUFFERS];
	unsigned held_output;
	bool output_enabled;
	uint64_t target_physical;
	uint32_t target_capacity;
	uint32_t target_pitch;
	uint32_t target_chroma;
	uint32_t encoding;
	uint32_t width;
	uint32_t height;
	uint32_t crop_width;
	uint32_t crop_height;
	struct mmal_bulk_tag staging_tags[2];
	struct mmal_bulk_tag frame_tag;
	struct mmal_bulk_tag scratch_tag;
	bool frame_pending;
	bool scratch_pending;
	int32_t frame_actual;
	struct mmal_message queue[MMAL_QUEUE];
	unsigned queue_head;
	unsigned queue_count;
	bool queue_overflow;
};

/*
 * The decoders.  An entry is taken under mmal_open_lock and returned by
 * destroy; see struct drv_rpi4_mmal_decoder for their lifetime.
 */
static struct drv_rpi4_mmal_decoder mmal_decoders[MMAL_DECODERS];

/*
 * The VCHIQ service, the DMA window and the open's serialization.
 *
 * Opened by the first decoder's creation under mmal_open_lock and kept for
 * the kernel's lifetime; NULL until then.
 */
static struct drv_rpi4_vchiq_service *mmal_service;
static struct drv_dma_device *mmal_dma;
static struct mutex mmal_open_lock;
static bool mmal_ready;

/*
 * The one request in flight, its reply and the next waiter number.
 *
 * mmal_request_lock serializes requests; the reply's fields change under
 * mmal_lock.  reply_waiter is zero when no request waits.
 */
static struct mutex mmal_request_lock;
static uint32_t mmal_reply_waiter;
static bool mmal_reply_ready;
static struct mmal_message mmal_reply;
static uint32_t mmal_next_waiter;

/*
 * The lock and wait queue of everything the receive thread hands over:
 * replies, the decoders' queues and their bulk counts.
 */
static struct spinlock mmal_lock;
static struct wait_queue mmal_wake;

/*
 * Whether the locks above have been set up: done once, at the first
 * decoder creation, before anything else of this file runs.
 */
static unsigned mmal_locks_ready;

static void service_message(void *context, const void *data, uint32_t size);
static void service_bulk_done(void *context, void *bulk_context, int32_t actual);
static void service_closed(void *context);
static int service_open(void);
static void locks_init(void);
static int request(uint8_t *message, size_t size, uint8_t *reply, size_t capacity);
static int request_with_bulk(uint8_t *message, size_t size, uint64_t physical, struct mmal_bulk_tag *tag, uint8_t *reply, size_t capacity);
static int configure(struct drv_rpi4_mmal_decoder *decoder, uint32_t width, uint32_t height);
static int port_info(struct drv_rpi4_mmal_decoder *decoder, uint32_t type, uint8_t *info);
static int port_set(struct drv_rpi4_mmal_decoder *decoder, uint8_t *info);
static int port_action(struct drv_rpi4_mmal_decoder *decoder, uint32_t port_handle, uint32_t action, const uint8_t *info);
static int set_boolean(struct drv_rpi4_mmal_decoder *decoder, uint32_t port_handle, uint32_t id, uint32_t value);
static int enable_output(struct drv_rpi4_mmal_decoder *decoder);
static int give_output(struct drv_rpi4_mmal_decoder *decoder, unsigned index);
static int pump(struct drv_rpi4_mmal_decoder *decoder);
static int take_message(struct drv_rpi4_mmal_decoder *decoder, struct mmal_message *message);
static int handle_buffer(struct drv_rpi4_mmal_decoder *decoder, const struct mmal_message *message);
static int handle_event(struct drv_rpi4_mmal_decoder *decoder, const struct mmal_message *message);
static int format_changed(struct drv_rpi4_mmal_decoder *decoder, const uint8_t *data);
static int receive_bulk(struct drv_rpi4_mmal_decoder *decoder, uint64_t physical, uint32_t bytes, bool frame);
static int wait_bulk(struct drv_rpi4_mmal_decoder *decoder, bool frame);
static int wait_queue_or(struct drv_rpi4_mmal_decoder *decoder, uint64_t deadline);
static int dma_alloc(size_t bytes, struct drv_dma_buffer *buffer);
static uint64_t bus_to_physical(uint64_t bus);
static uint32_t token(struct drv_rpi4_mmal_decoder *decoder, uint32_t kind, uint32_t index);
static struct drv_rpi4_mmal_decoder *decoder_of(uint32_t value);
static uint32_t maximum(uint32_t first, uint32_t second);
static void release_memory(struct drv_rpi4_mmal_decoder *decoder);
static bool bulks_outstanding(struct drv_rpi4_mmal_decoder *decoder);

/* The client's part of the VCHIQ service. */
static const struct drv_rpi4_vchiq_service_ops mmal_service_ops = {
	service_message,
	service_bulk_done,
	service_closed
};

/*
 * Creates a decoder for H.264 pictures of up to width by height samples.
 */
int
drv_rpi4_mmal_decoder_create(
	uint32_t width,
	uint32_t height,
	uint32_t input_bytes,
	struct drv_rpi4_mmal_decoder **result)
{
	struct drv_rpi4_mmal_decoder *decoder;
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	unsigned index;
	size_t size;
	int error;

	/* The caller receives nothing on failure. */
	*result = NULL;
	if (width == 0 || height == 0 || width > 4096U || height > 4096U)
		return EINVAL;
	if (input_bytes < 4096U || input_bytes > MMAL_MAX_SEGMENT / 2U)
		return EINVAL;

	/* Opens the service the first time and takes a free decoder entry. */
	locks_init();
	mutex_lock(&mmal_open_lock);

	error = service_open();
	decoder = NULL;
	if (error == 0) {
		for (index = 0; index < MMAL_DECODERS; index++) {
			if (!mmal_decoders[index].used && !mmal_decoders[index].retired) {
				decoder = &mmal_decoders[index];
				kern_memset(decoder, 0, sizeof(*decoder));
				decoder->used = true;
				decoder->slot = index;
				break;
			}
		}
		if (decoder == NULL)
			error = EBUSY;
	}

	mutex_unlock(&mmal_open_lock);

	if (error != 0)
		return error;

	/* The tags the bulk completions name. */
	decoder->staging_tags[0].decoder = decoder;
	decoder->staging_tags[0].kind = MMAL_BULK_STAGING;
	decoder->staging_tags[0].index = 0;
	decoder->staging_tags[1].decoder = decoder;
	decoder->staging_tags[1].kind = MMAL_BULK_STAGING;
	decoder->staging_tags[1].index = 1;
	decoder->frame_tag.decoder = decoder;
	decoder->frame_tag.kind = MMAL_BULK_FRAME;
	decoder->scratch_tag.decoder = decoder;
	decoder->scratch_tag.kind = MMAL_BULK_SCRATCH;

	/* The two input staging areas (each with room to round a bulk up to four bytes) and the scratch. */
	decoder->input_bytes = input_bytes;
	error = dma_alloc((size_t)(input_bytes + 64U) * 2U, &decoder->staging_memory);
	if (error == 0)
		error = dma_alloc(MMAL_SCRATCH_BYTES, &decoder->scratch);
	if (error != 0) {
		release_memory(decoder);
		decoder->used = false;
		return error;
	}
	decoder->half[0].cpu = decoder->staging_memory.address;
	decoder->half[0].physical = bus_to_physical(decoder->staging_memory.device_address);
	decoder->half[1].cpu = decoder->half[0].cpu + input_bytes + 64U;
	decoder->half[1].physical = decoder->half[0].physical + input_bytes + 64U;

	/* Creates the firmware's decoder component. */
	size = drv_rpi4_mmal_build_create(message, 0, token(decoder, MMAL_KIND_COMPONENT, 0), MMAL_COMPONENT_NAME);
	error = request(message, size, reply, sizeof(reply));
	if (error == 0 && drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_CREATE_REPLY_STATUS) != 0)
		error = EIO;
	if (error != 0) {
		kern_logf("rpi4-mmal: %s not created (%d)\n", MMAL_COMPONENT_NAME, error);
		release_memory(decoder);
		decoder->used = false;
		return error;
	}
	decoder->component = drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_CREATE_REPLY_HANDLE);

	/* Sets the ports up and starts the component. */
	error = configure(decoder, width, height);
	if (error != 0) {
		kern_logf("rpi4-mmal: decoder not configured (%d)\n", error);
		drv_rpi4_mmal_decoder_destroy(decoder);
		return error;
	}

	/* Succeeded: the decoder takes access units. */
	*result = decoder;
	return 0;
}

/*
 * Reports the input staging the next access unit goes into.
 *
 * The staging alternates between two areas; an area is reused only after
 * the firmware has given back every input buffer and completed every bulk
 * of the access unit last sent from it.
 */
int
drv_rpi4_mmal_decoder_input(
	struct drv_rpi4_mmal_decoder *decoder,
	uint8_t **staging,
	uint32_t *capacity)
{
	struct mmal_staging *half;
	unsigned long enabled;
	uint64_t deadline;
	bool busy;
	int error;

	/* A broken decoder takes nothing more. */
	if (decoder->broken)
		return EIO;
	half = &decoder->half[decoder->current];

	/* Waits until the firmware is done with the area. */
	deadline = sched_ticks() + kern_ms_to_ticks(MMAL_WAIT_MS) + 1U;
	for (;;) {
		error = pump(decoder);
		if (error != 0)
			return error;
		enabled = spin_lock_irqsave(&mmal_lock);

		busy = half->buffers != 0 || half->bulks != 0;

		spin_unlock_irqrestore(&mmal_lock, enabled);

		if (!busy)
			break;
		error = wait_queue_or(decoder, deadline);
		if (error != 0) {
			kern_logf("rpi4-mmal: input staging not given back in %u ms\n", MMAL_WAIT_MS);
			return error;
		}
	}

	/* Succeeded: the caller writes the access unit here. */
	*staging = half->cpu;
	*capacity = decoder->input_bytes;
	return 0;
}

/*
 * Sends the access unit written into the staging the last input call gave.
 */
int
drv_rpi4_mmal_decoder_send(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t bytes,
	int64_t pts)
{
	struct mmal_staging *half;
	uint8_t message[DRV_RPI4_MMAL_BUFFER_BYTES];
	unsigned long enabled;
	uint64_t deadline;
	uint32_t offset;
	uint32_t chunk;
	uint32_t flags;
	unsigned index;
	unsigned half_index;
	int error;

	/* Refuses an empty or oversized unit and a broken decoder. */
	if (decoder->broken)
		return EIO;
	if (bytes == 0 || bytes > decoder->input_bytes)
		return EINVAL;
	half_index = decoder->current;
	half = &decoder->half[half_index];

	/* Sends the unit in pieces of the input buffer size; the last ends the frame. */
	deadline = sched_ticks() + kern_ms_to_ticks(MMAL_WAIT_MS) + 1U;
	offset = 0;
	while (offset < bytes) {
		/* Waits for an input buffer the firmware has given back. */
		for (;;) {
			error = pump(decoder);
			if (error != 0)
				return error;
			for (index = 0; index < decoder->input_count; index++) {
				if (decoder->input_free[index])
					break;
			}
			if (index < decoder->input_count)
				break;
			error = wait_queue_or(decoder, deadline);
			if (error != 0) {
				kern_logf("rpi4-mmal: no input buffer back in %u ms\n", MMAL_WAIT_MS);
				return error;
			}
		}

		/* The piece, and the end-of-frame flag on the last one. */
		chunk = bytes - offset;
		if (chunk > decoder->input_size)
			chunk = decoder->input_size;
		flags = 0;
		if (offset + chunk == bytes)
			flags = DRV_RPI4_MMAL_FLAG_FRAME_END;

		/* The buffer is the firmware's until it comes back; the area until its bulk completes. */
		decoder->input_free[index] = false;
		decoder->input_half[index] = half_index;
		enabled = spin_lock_irqsave(&mmal_lock);

		half->buffers++;
		half->bulks++;

		spin_unlock_irqrestore(&mmal_lock, enabled);

		/* The buffer message, then the piece as a bulk of a multiple of four bytes. */
		(void)drv_rpi4_mmal_build_buffer(message, DRV_RPI4_MMAL_MSG_BUFFER_FROM_HOST, 0, decoder->component,
						 decoder->input_handle, token(decoder, MMAL_KIND_INPUT, index),
						 decoder->input_size, chunk, flags, pts);
		error = drv_rpi4_vchiq_send_bulk(mmal_service, message, sizeof(message), DRV_RPI4_VCHIQ_BULK_TRANSMIT,
						 half->physical + offset, (chunk + 3U) & ~3U, &decoder->staging_tags[half_index]);
		if (error != 0) {
			decoder->broken = true;
			kern_logf("rpi4-mmal: input not sent (%d); decoder stopped\n", error);
			return error;
		}
		offset += chunk;
	}

	/* The next unit goes into the other area. */
	decoder->current = 1U - half_index;

	/* Succeeded: the firmware has the unit. */
	return 0;
}

/*
 * Names memory the next decoded frame goes straight into.
 */
void
drv_rpi4_mmal_decoder_target(
	struct drv_rpi4_mmal_decoder *decoder,
	uint64_t physical,
	uint32_t capacity,
	uint32_t pitch,
	uint32_t chroma_offset)
{
	/* Only the caller's thread reads the target, in pump. */
	decoder->target_physical = physical;
	decoder->target_capacity = capacity;
	decoder->target_pitch = pitch;
	decoder->target_chroma = chroma_offset;
}

/*
 * Receives the next decoded frame.
 *
 * The output buffer the caller held since the last receive goes back to
 * the firmware first.
 */
int
drv_rpi4_mmal_decoder_receive(
	struct drv_rpi4_mmal_decoder *decoder,
	unsigned timeout_ms,
	struct drv_rpi4_mmal_frame *frame,
	const uint8_t **pixels)
{
	struct mmal_output *output;
	uint64_t deadline;
	unsigned index;
	int error;

	/* A broken decoder gives nothing more. */
	if (decoder->broken)
		return EIO;

	/* Gives the buffer the caller held back to the firmware. */
	if (decoder->held_output != 0) {
		index = decoder->held_output - 1U;
		decoder->held_output = 0;
		decoder->outputs[index].held = false;
		error = give_output(decoder, index);
		if (error != 0)
			return error;
	}

	/* Waits for the oldest received frame. */
	deadline = sched_ticks() + kern_ms_to_ticks(timeout_ms) + 1U;
	for (;;) {
		error = pump(decoder);
		if (error != 0)
			return error;
		for (index = 0; index < decoder->output_count; index++) {
			if (decoder->outputs[index].ready)
				break;
		}
		if (index < decoder->output_count)
			break;
		error = wait_queue_or(decoder, deadline);
		if (error != 0)
			return error;
	}

	/* The frame stays in its buffer until the next receive. */
	output = &decoder->outputs[index];
	output->ready = false;
	output->held = true;
	decoder->held_output = index + 1U;

	/* Describes the frame. */
	kern_memset(frame, 0, sizeof(*frame));
	frame->pts = output->pts;
	frame->flags = output->flags;
	frame->length = output->length;
	frame->encoding = decoder->encoding;
	frame->width = decoder->width;
	frame->height = decoder->height;
	frame->crop_width = decoder->crop_width;
	frame->crop_height = decoder->crop_height;
	frame->planes = output->planes;
	frame->plane_offset[0] = output->plane_offset[0];
	frame->plane_offset[1] = output->plane_offset[1];
	frame->plane_pitch[0] = output->plane_pitch[0];
	frame->plane_pitch[1] = output->plane_pitch[1];
	frame->in_target = output->in_target;
	*pixels = output->memory.address;
	if (output->in_target)
		*pixels = NULL;

	/* Succeeded: the frame is the caller's until the next receive. */
	return 0;
}

/*
 * Reports whether a bulk transfer of the decoder may still move memory.
 */
int
drv_rpi4_mmal_decoder_busy(
	struct drv_rpi4_mmal_decoder *decoder)
{
	bool outstanding;

	/* Any staging, frame or scratch bulk not completed. */
	outstanding = bulks_outstanding(decoder);
	if (outstanding)
		return 1;

	/* Succeeded: nothing of the decoder's moves. */
	return 0;
}

/*
 * Drops every picture and reference the decoder holds.
 *
 * The input port has carried bulks, so it is flushed with PORT_FLUSH and a
 * dummy bulk that keeps the flush in order behind them; the output port
 * with a plain flush.  Every buffer the firmware gives back goes back to it
 * (output) or is free again (input).
 */
int
drv_rpi4_mmal_decoder_flush(
	struct drv_rpi4_mmal_decoder *decoder)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	unsigned long enabled;
	unsigned index;
	size_t size;
	int error;

	/* A broken decoder cannot be flushed. */
	if (decoder->broken)
		return EIO;

	/* The input port, with the dummy bulk from the scratch. */
	size = drv_rpi4_mmal_build_buffer(message, DRV_RPI4_MMAL_MSG_PORT_FLUSH, 0, decoder->component,
					  decoder->input_handle, token(decoder, MMAL_KIND_INPUT, 0xfffU), 0, 0, 0,
					  DRV_RPI4_MMAL_TIME_UNKNOWN);
	enabled = spin_lock_irqsave(&mmal_lock);

	decoder->scratch_pending = true;

	spin_unlock_irqrestore(&mmal_lock, enabled);

	error = request_with_bulk(message, size, bus_to_physical(decoder->scratch.device_address), &decoder->scratch_tag,
				  reply, sizeof(reply));
	if (error == 0)
		error = wait_bulk(decoder, false);
	if (error == 0 && drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_REPLY_STATUS) != 0)
		error = EIO;
	if (error != 0)
		return error;

	/* The output port. */
	error = port_action(decoder, decoder->output_handle, DRV_RPI4_MMAL_ACTION_FLUSH, NULL);
	if (error != 0)
		return error;

	/* Frames received before the flush are dropped and their buffers go back. */
	for (index = 0; index < decoder->output_count; index++) {
		if (decoder->outputs[index].ready || decoder->outputs[index].held) {
			decoder->outputs[index].ready = false;
			decoder->outputs[index].held = false;
			error = give_output(decoder, index);
			if (error != 0)
				return error;
		}
	}
	decoder->held_output = 0;

	/* Takes in what the flush gave back. */
	error = pump(decoder);
	if (error != 0)
		return error;

	/* Succeeded: the decoder starts again at the next key frame. */
	return 0;
}

/*
 * Destroys a decoder.
 */
void
drv_rpi4_mmal_decoder_destroy(
	struct drv_rpi4_mmal_decoder *decoder)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	bool outstanding;
	size_t size;

	/* Stops the ports and the component, and destroys it, unless the link is broken. */
	if (!decoder->broken && decoder->component != 0) {
		(void)port_action(decoder, decoder->input_handle, DRV_RPI4_MMAL_ACTION_DISABLE, NULL);
		(void)port_action(decoder, decoder->output_handle, DRV_RPI4_MMAL_ACTION_DISABLE, NULL);
		size = drv_rpi4_mmal_build_component(message, DRV_RPI4_MMAL_MSG_COMPONENT_DISABLE, 0, decoder->component);
		(void)request(message, size, reply, sizeof(reply));
		size = drv_rpi4_mmal_build_component(message, DRV_RPI4_MMAL_MSG_COMPONENT_DESTROY, 0, decoder->component);
		(void)request(message, size, reply, sizeof(reply));
		(void)pump(decoder);
	}

	/* Memory a bulk still holds is kept, with the entry, for good. */
	outstanding = bulks_outstanding(decoder);
	mutex_lock(&mmal_open_lock);

	if (outstanding) {
		decoder->retired = true;
		kern_logf("rpi4-mmal: decoder %u retired with a bulk outstanding\n", decoder->slot);
	} else {
		release_memory(decoder);
	}
	decoder->used = false;

	mutex_unlock(&mmal_open_lock);
}

/*
 * Takes what the receive thread hands over: a reply wakes its request, a
 * buffer or an event joins its decoder's queue.
 */
static void
service_message(
	void *context,
	const void *data,
	uint32_t size)
{
	struct drv_rpi4_mmal_decoder *decoder;
	struct mmal_message *slot;
	const uint8_t *bytes;
	unsigned long enabled;
	uint32_t msgid;
	uint32_t owner;
	unsigned index;

	/* Ignores anything that is not an MMAL message. */
	(void)context;
	bytes = data;
	if (size < DRV_RPI4_MMAL_HEADER_BYTES)
		return;
	if (drv_rpi4_mmal_get32(bytes, DRV_RPI4_MMAL_HEADER_MAGIC) != DRV_RPI4_MMAL_MAGIC)
		return;
	if (size > DRV_RPI4_MMAL_EVENT_BYTES)
		size = DRV_RPI4_MMAL_EVENT_BYTES;
	msgid = drv_rpi4_mmal_get32(bytes, DRV_RPI4_MMAL_HEADER_MSGID);

	/* Buffers name their decoder in the driver area, events in the client field. */
	owner = 0;
	if (msgid == DRV_RPI4_MMAL_MSG_BUFFER_TO_HOST && size >= DRV_RPI4_MMAL_BUFFER_CONTEXT + 4U)
		owner = drv_rpi4_mmal_get32(bytes, DRV_RPI4_MMAL_BUFFER_CONTEXT);
	if (msgid == DRV_RPI4_MMAL_MSG_EVENT_TO_HOST && size >= DRV_RPI4_MMAL_EVENT_CLIENT + 4U)
		owner = drv_rpi4_mmal_get32(bytes, DRV_RPI4_MMAL_EVENT_CLIENT);
	decoder = decoder_of(owner);
	enabled = spin_lock_irqsave(&mmal_lock);

	if (decoder != NULL) {
		/* Queues the arrival for the decoder's caller. */
		if (decoder->queue_count < MMAL_QUEUE) {
			index = (decoder->queue_head + decoder->queue_count) % MMAL_QUEUE;
			slot = &decoder->queue[index];
			slot->size = size;
			kern_memcpy(slot->bytes, bytes, size);
			decoder->queue_count++;
		} else {
			decoder->queue_overflow = true;
		}
	} else if (mmal_reply_waiter != 0 &&
		   drv_rpi4_mmal_get32(bytes, DRV_RPI4_MMAL_HEADER_WAITER) == mmal_reply_waiter) {
		/* The reply to the request in flight. */
		mmal_reply.size = size;
		kern_memcpy(mmal_reply.bytes, bytes, size);
		mmal_reply_ready = true;
	}
	waitq_wake_all(&mmal_wake);

	spin_unlock_irqrestore(&mmal_lock, enabled);
}

/* Counts one completed bulk for its decoder. */
static void
service_bulk_done(
	void *context,
	void *bulk_context,
	int32_t actual)
{
	struct mmal_bulk_tag *tag;
	struct drv_rpi4_mmal_decoder *decoder;
	unsigned long enabled;

	/* The tag names the decoder and what the bulk was for. */
	(void)context;
	tag = bulk_context;
	if (tag == NULL)
		return;
	decoder = tag->decoder;
	enabled = spin_lock_irqsave(&mmal_lock);

	if (tag->kind == MMAL_BULK_STAGING) {
		decoder->half[tag->index].bulks--;
	} else if (tag->kind == MMAL_BULK_FRAME) {
		decoder->frame_pending = false;
		decoder->frame_actual = actual;
	} else {
		decoder->scratch_pending = false;
	}
	waitq_wake_all(&mmal_wake);

	spin_unlock_irqrestore(&mmal_lock, enabled);
}

/* Notes that the firmware closed the service: nothing more can be sent. */
static void
service_closed(
	void *context)
{
	/* Every later request and buffer fails on the closed service. */
	(void)context;
	kern_logf("rpi4-mmal: the firmware closed the MMAL service\n");
}

/*
 * Opens the MMAL service and the DMA window once.
 *
 * The caller holds mmal_open_lock.
 */
static int
service_open(void)
{
	struct drv_dma_constraints constraints;
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	int error;

	/* Done once. */
	if (mmal_ready)
		return 0;

	/* The window decoders allocate their bulk memory in. */
	if (mmal_dma == NULL) {
		kern_memset(&constraints, 0, sizeof(constraints));
		constraints.address_bits = 32U;
		constraints.max_segment_size = MMAL_MAX_SEGMENT;
		constraints.coherent = 0;
		error = drv_dma_device_create_window(&constraints, MMAL_BUS_RAM, MMAL_BUS_RAM_LIMIT, &mmal_dma);
		if (error != 0)
			return error;
	}

	/* The service, which waits for VCHIQ's connection. */
	error = drv_rpi4_vchiq_open(DRV_RPI4_MMAL_SERVICE_FOURCC, DRV_RPI4_MMAL_VERSION, DRV_RPI4_MMAL_VERSION_MIN,
				    &mmal_service_ops, NULL, &mmal_service);
	if (error != 0)
		return error;

	/* Logs the firmware's MMAL version. */
	drv_rpi4_mmal_header(message, DRV_RPI4_MMAL_MSG_GET_VERSION, 0);
	error = request(message, DRV_RPI4_MMAL_HEADER_BYTES, reply, sizeof(reply));
	if (error == 0) {
		kern_logf("rpi4-mmal: firmware MMAL %u.%u (service version %u)\n",
			  (unsigned)drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_VERSION_MAJOR),
			  (unsigned)drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_VERSION_MINOR),
			  (unsigned)drv_rpi4_vchiq_peer_version(mmal_service));
	}

	/* Succeeded: decoders can be created. */
	mmal_ready = true;
	return 0;
}

/* Sets the locks and the wait queue up the first time. */
static void
locks_init(void)
{
	unsigned expected;
	int claimed;

	/* Exactly one caller sets them up; decoders are created from thread context. */
	expected = 0;
	claimed = __atomic_compare_exchange_n(&mmal_locks_ready, &expected, 1U, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	if (!claimed) {
		while (__atomic_load_n(&mmal_locks_ready, __ATOMIC_ACQUIRE) != 2U)
			kern_usleep_range(10U, 20U);
		return;
	}
	spin_init(&mmal_lock, LOCK_RANK_DEVICE, "rpi4 mmal");
	waitq_init(&mmal_wake, "rpi4 mmal");
	(void)mutex_init(&mmal_open_lock, LOCK_RANK_DEVICE, "rpi4 mmal open");
	(void)mutex_init(&mmal_request_lock, LOCK_RANK_DEVICE, "rpi4 mmal request");

	/* Publishes the set-up locks. */
	__atomic_store_n(&mmal_locks_ready, 2U, __ATOMIC_RELEASE);
}

/*
 * Sends one request and waits for its reply.
 */
static int
request(
	uint8_t *message,
	size_t size,
	uint8_t *reply,
	size_t capacity)
{
	/* A request without a bulk is a request with none. */
	return request_with_bulk(message, size, 0, NULL, reply, capacity);
}

/*
 * Sends one request, optionally followed by an eight-byte bulk from
 * physical whose completion tag is tag, and waits up to MMAL_WAIT_MS for
 * its reply.
 */
static int
request_with_bulk(
	uint8_t *message,
	size_t size,
	uint64_t physical,
	struct mmal_bulk_tag *tag,
	uint8_t *reply,
	size_t capacity)
{
	unsigned long enabled;
	uint64_t deadline;
	uint64_t observed;
	uint32_t waiter;
	size_t copied;
	int error;

	/* One request at a time; it is numbered, and its reply is awaited. */
	mutex_lock(&mmal_request_lock);

	enabled = spin_lock_irqsave(&mmal_lock);

	mmal_next_waiter++;
	if (mmal_next_waiter == 0)
		mmal_next_waiter = 1;
	waiter = mmal_next_waiter;
	mmal_reply_waiter = waiter;
	mmal_reply_ready = false;

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* Sends the request, with its bulk when it has one. */
	drv_rpi4_mmal_put32(message, DRV_RPI4_MMAL_HEADER_WAITER, waiter);
	if (physical != 0) {
		error = drv_rpi4_vchiq_send_bulk(mmal_service, message, (uint32_t)size, DRV_RPI4_VCHIQ_BULK_TRANSMIT,
						 physical, 8U, tag);
	} else {
		error = drv_rpi4_vchiq_send(mmal_service, message, (uint32_t)size);
	}

	/* Waits for the reply. */
	deadline = sched_ticks() + kern_ms_to_ticks(MMAL_WAIT_MS) + 1U;
	enabled = spin_lock_irqsave(&mmal_lock);

	while (error == 0 && !mmal_reply_ready) {
		observed = waitq_sequence(&mmal_wake);
		error = waitq_sleep(&mmal_wake, &mmal_lock, observed, deadline, 0);
		if (error == ETIMEDOUT && mmal_reply_ready)
			error = 0;
		if (error == ETIMEDOUT)
			break;
		error = 0;
	}
	copied = 0;
	if (error == 0) {
		copied = mmal_reply.size;
		if (copied > capacity)
			copied = capacity;
		kern_memcpy(reply, mmal_reply.bytes, copied);
		kern_memset(reply + copied, 0, capacity - copied);
	}
	mmal_reply_waiter = 0;

	spin_unlock_irqrestore(&mmal_lock, enabled);

	mutex_unlock(&mmal_request_lock);

	/* Reports a request that went unanswered. */
	if (error != 0) {
		kern_logf("rpi4-mmal: request %u not answered (%d)\n",
			  (unsigned)drv_rpi4_mmal_get32(message, DRV_RPI4_MMAL_HEADER_MSGID), error);
		return error;
	}

	/* Succeeded: reply holds the answer. */
	return 0;
}

/*
 * Sets the ports up and starts the component: H.264 in, NV12 out, the time
 * stamps tied to pictures, the buffers counted and sized, and the output
 * buffers given to the firmware.
 */
static int
configure(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t width,
	uint32_t height)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t *format;
	uint8_t *es;
	uint32_t count;
	size_t size;
	unsigned index;
	int error;

	/* Reads both ports. */
	error = port_info(decoder, DRV_RPI4_MMAL_PORT_INPUT, decoder->input_info);
	if (error != 0)
		return error;
	error = port_info(decoder, DRV_RPI4_MMAL_PORT_OUTPUT, decoder->output_info);
	if (error != 0)
		return error;
	decoder->input_handle = drv_rpi4_mmal_get32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT_HANDLE);
	decoder->output_handle = drv_rpi4_mmal_get32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT_HANDLE);

	/* The input: H.264 video of the stream's size. */
	format = decoder->input_info + DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES;
	es = format + DRV_RPI4_MMAL_FORMAT_BYTES;
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_TYPE, DRV_RPI4_MMAL_ES_VIDEO);
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_ENCODING, DRV_RPI4_MMAL_ENCODING_H264);
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_VARIANT, 0);
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_EXTRADATA_SIZE, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_WIDTH, width);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_HEIGHT, height);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_X, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_Y, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_WIDTH, width);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_HEIGHT, height);
	error = port_set(decoder, decoder->input_info);
	if (error != 0)
		return error;

	/* The output: NV12 of the stream's size, rows in 32-byte strides and 16-row heights. */
	format = decoder->output_info + DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES;
	es = format + DRV_RPI4_MMAL_FORMAT_BYTES;
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_TYPE, DRV_RPI4_MMAL_ES_VIDEO);
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_ENCODING, DRV_RPI4_MMAL_ENCODING_NV12);
	drv_rpi4_mmal_put32(format, DRV_RPI4_MMAL_FORMAT_VARIANT, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_WIDTH, (width + 31U) & ~31U);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_HEIGHT, (height + 15U) & ~15U);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_X, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_Y, 0);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_WIDTH, width);
	drv_rpi4_mmal_put32(es, DRV_RPI4_MMAL_ES_CROP_HEIGHT, height);
	error = port_set(decoder, decoder->output_info);
	if (error != 0)
		return error;

	/* Time stamps follow pictures, not input order, and none are invented (best effort). */
	(void)set_boolean(decoder, decoder->input_handle, MMAL_PARAMETER_TIMESTAMP_FIFO, 0);
	(void)set_boolean(decoder, decoder->input_handle, MMAL_PARAMETER_INTERPOLATE, 0);

	/* Reads the ports again for the buffer sizes the formats imply. */
	error = port_info(decoder, DRV_RPI4_MMAL_PORT_INPUT, decoder->input_info);
	if (error != 0)
		return error;
	error = port_info(decoder, DRV_RPI4_MMAL_PORT_OUTPUT, decoder->output_info);
	if (error != 0)
		return error;

	/* The input buffers: the firmware's recommendation, within the client's table. */
	decoder->input_size = maximum(drv_rpi4_mmal_get32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE_RECOMMENDED),
				      drv_rpi4_mmal_get32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE_MIN));
	if (decoder->input_size < 4096U)
		decoder->input_size = 65536U;
	count = maximum(drv_rpi4_mmal_get32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_NUM_RECOMMENDED),
			drv_rpi4_mmal_get32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_NUM_MIN));
	if (count < 2U)
		count = 2U;
	if (count > MMAL_INPUT_BUFFERS)
		count = MMAL_INPUT_BUFFERS;
	decoder->input_count = count;
	drv_rpi4_mmal_put32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_NUM, count);
	drv_rpi4_mmal_put32(decoder->input_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE, decoder->input_size);
	for (index = 0; index < count; index++)
		decoder->input_free[index] = true;

	/* Enables the input, then the output with its bounce buffers, then the component. */
	error = port_action(decoder, decoder->input_handle, DRV_RPI4_MMAL_ACTION_ENABLE, decoder->input_info);
	if (error != 0)
		return error;
	error = enable_output(decoder);
	if (error != 0)
		return error;
	size = drv_rpi4_mmal_build_component(message, DRV_RPI4_MMAL_MSG_COMPONENT_ENABLE, 0, decoder->component);
	error = request(message, size, reply, sizeof(reply));
	if (error == 0 && drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_REPLY_STATUS) != 0)
		error = EIO;
	if (error != 0)
		return error;

	/* Succeeded: the decoder runs. */
	kern_logf("rpi4-mmal: decoder %u: %ux%u, input %u x %u bytes, output %u x %u bytes\n",
		  decoder->slot, (unsigned)width, (unsigned)height,
		  (unsigned)decoder->input_count, (unsigned)decoder->input_size,
		  (unsigned)decoder->output_count, (unsigned)decoder->output_size);
	return 0;
}

/* Reads one port's info into a port info reply. */
static int
port_info(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t type,
	uint8_t *info)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	size_t size;
	int error;

	/* Asks for the first port of the type. */
	size = drv_rpi4_mmal_build_info_get(message, 0, decoder->component, type, 0);
	error = request(message, size, info, DRV_RPI4_MMAL_INFO_BYTES);
	if (error != 0)
		return error;

	/* Refuses a port the component does not have. */
	if (drv_rpi4_mmal_get32(info, DRV_RPI4_MMAL_INFO_STATUS) != 0 ||
	    drv_rpi4_mmal_get32(info, DRV_RPI4_MMAL_INFO_FOUND) == 0)
		return ENODEV;

	/* Succeeded: info describes the port. */
	return 0;
}

/* Sets one port's format from an edited port info reply. */
static int
port_set(
	struct drv_rpi4_mmal_decoder *decoder,
	uint8_t *info)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint32_t status;
	size_t size;
	int error;

	/* Sends the edited blocks back. */
	size = drv_rpi4_mmal_build_info_set(message, 0, decoder->component, info);
	error = request(message, size, reply, sizeof(reply));
	if (error != 0)
		return error;

	/* Reports a format the firmware refused. */
	status = drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_INFO_STATUS);
	if (status != 0) {
		kern_logf("rpi4-mmal: port format %08x refused (status %u)\n",
			  (unsigned)drv_rpi4_mmal_get32(info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_ENCODING),
			  (unsigned)status);
		return EIO;
	}

	/* Succeeded: the port has the format. */
	return 0;
}

/* Runs one port action. */
static int
port_action(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t port_handle,
	uint32_t action,
	const uint8_t *info)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	size_t size;
	int error;

	/* Sends the action and checks its status. */
	size = drv_rpi4_mmal_build_action(message, 0, decoder->component, port_handle, action, info);
	error = request(message, size, reply, sizeof(reply));
	if (error == 0 && drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_REPLY_STATUS) != 0)
		error = EIO;
	if (error != 0) {
		kern_logf("rpi4-mmal: port action %u failed (%d)\n", (unsigned)action, error);
		return error;
	}

	/* Succeeded: the action is done. */
	return 0;
}

/* Sets one boolean parameter of a port, logging a refusal. */
static int
set_boolean(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t port_handle,
	uint32_t id,
	uint32_t value)
{
	uint8_t message[DRV_RPI4_MMAL_MAX_MESSAGE];
	uint8_t reply[DRV_RPI4_MMAL_MAX_MESSAGE];
	size_t size;
	int error;

	/* Sends the parameter and checks its status. */
	size = drv_rpi4_mmal_build_parameter32(message, 0, decoder->component, port_handle, id, value);
	error = request(message, size, reply, sizeof(reply));
	if (error == 0 && drv_rpi4_mmal_get32(reply, DRV_RPI4_MMAL_REPLY_STATUS) != 0)
		error = EIO;
	if (error != 0) {
		kern_logf("rpi4-mmal: parameter %x not set (%d)\n", (unsigned)id, error);
		return error;
	}

	/* Succeeded: the parameter is set. */
	return 0;
}

/*
 * Enables the output port with as many bounce buffers as the firmware
 * wants (at least three) of the size it wants, and gives them all to it.
 */
static int
enable_output(
	struct drv_rpi4_mmal_decoder *decoder)
{
	uint8_t *es;
	uint32_t size;
	uint32_t count;
	uint32_t frame;
	unsigned index;
	int error;

	/* The size: the firmware's, but at least a whole NV12 frame of the format. */
	es = decoder->output_info + DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_BYTES;
	frame = drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_WIDTH) * drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_HEIGHT) * 3U / 2U;
	size = maximum(drv_rpi4_mmal_get32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE_RECOMMENDED),
		       drv_rpi4_mmal_get32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE_MIN));
	size = maximum(size, frame);
	size = (size + 4095U) & ~4095U;
	count = maximum(drv_rpi4_mmal_get32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_NUM_MIN),
			MMAL_OUTPUT_MINIMUM);
	if (count > MMAL_OUTPUT_BUFFERS)
		count = MMAL_OUTPUT_BUFFERS;

	/* Replaces the bounce buffers when the size grew; none may be with the firmware. */
	if (size > decoder->output_size || count != decoder->output_count) {
		for (index = 0; index < decoder->output_count; index++) {
			if (decoder->outputs[index].with_firmware) {
				kern_logf("rpi4-mmal: output buffer %u not given back before a resize\n", index);
				return EIO;
			}
		}
		for (index = 0; index < MMAL_OUTPUT_BUFFERS; index++) {
			if (decoder->outputs[index].memory.address != NULL)
				drv_dma_free_coherent(mmal_dma, &decoder->outputs[index].memory);
			kern_memset(&decoder->outputs[index], 0, sizeof(decoder->outputs[index]));
		}
		for (index = 0; index < count; index++) {
			error = dma_alloc(size, &decoder->outputs[index].memory);
			if (error != 0)
				return error;
		}
		decoder->output_size = size;
		decoder->output_count = count;
	}

	/* The format the frames will have, until a format change says otherwise. */
	decoder->encoding = drv_rpi4_mmal_get32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_ENCODING);
	decoder->width = drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_WIDTH);
	decoder->height = drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_HEIGHT);
	decoder->crop_width = drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_CROP_WIDTH);
	decoder->crop_height = drv_rpi4_mmal_get32(es, DRV_RPI4_MMAL_ES_CROP_HEIGHT);

	/* Enables the port with the count and size. */
	drv_rpi4_mmal_put32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_NUM, count);
	drv_rpi4_mmal_put32(decoder->output_info, DRV_RPI4_MMAL_INFO_PORT + DRV_RPI4_MMAL_PORT_BUFFER_SIZE, size);
	error = port_action(decoder, decoder->output_handle, DRV_RPI4_MMAL_ACTION_ENABLE, decoder->output_info);
	if (error != 0)
		return error;
	decoder->output_enabled = true;

	/* Gives every buffer to the firmware. */
	for (index = 0; index < count; index++) {
		decoder->outputs[index].with_firmware = false;
		decoder->outputs[index].ready = false;
		decoder->outputs[index].held = false;
		error = give_output(decoder, index);
		if (error != 0)
			return error;
	}
	decoder->held_output = 0;

	/* Succeeded: the firmware can output frames. */
	return 0;
}

/* Gives one empty output buffer to the firmware. */
static int
give_output(
	struct drv_rpi4_mmal_decoder *decoder,
	unsigned index)
{
	uint8_t message[DRV_RPI4_MMAL_BUFFER_BYTES];
	int error;

	/* A disabled port takes no buffers; it gets them all when enabled again. */
	if (!decoder->output_enabled)
		return 0;

	/* An empty buffer message without a bulk. */
	(void)drv_rpi4_mmal_build_buffer(message, DRV_RPI4_MMAL_MSG_BUFFER_FROM_HOST, 0, decoder->component,
					 decoder->output_handle, token(decoder, MMAL_KIND_OUTPUT, index),
					 decoder->output_size, 0, 0, DRV_RPI4_MMAL_TIME_UNKNOWN);
	error = drv_rpi4_vchiq_send(mmal_service, message, sizeof(message));
	if (error != 0) {
		decoder->broken = true;
		return error;
	}

	/* Succeeded: the firmware has the buffer. */
	decoder->outputs[index].with_firmware = true;
	return 0;
}

/*
 * Handles everything queued for the decoder, in the order it came.
 */
static int
pump(
	struct drv_rpi4_mmal_decoder *decoder)
{
	struct mmal_message message;
	uint32_t msgid;
	int error;

	/* Handles one arrival at a time until the queue is empty. */
	for (;;) {
		error = take_message(decoder, &message);
		if (error == EAGAIN)
			break;
		if (error != 0)
			return error;
		msgid = drv_rpi4_mmal_get32(message.bytes, DRV_RPI4_MMAL_HEADER_MSGID);
		if (msgid == DRV_RPI4_MMAL_MSG_BUFFER_TO_HOST) {
			error = handle_buffer(decoder, &message);
		} else {
			error = handle_event(decoder, &message);
		}
		if (error != 0) {
			decoder->broken = true;
			return error;
		}
	}

	/* Succeeded: nothing waits. */
	return 0;
}

/* Takes the oldest queued arrival; EAGAIN when there is none. */
static int
take_message(
	struct drv_rpi4_mmal_decoder *decoder,
	struct mmal_message *message)
{
	unsigned long enabled;
	bool overflow;
	bool empty;

	/* Copies the oldest arrival out of the queue. */
	enabled = spin_lock_irqsave(&mmal_lock);

	overflow = decoder->queue_overflow;
	empty = decoder->queue_count == 0;
	if (!empty && !overflow) {
		*message = decoder->queue[decoder->queue_head];
		decoder->queue_head = (decoder->queue_head + 1U) % MMAL_QUEUE;
		decoder->queue_count--;
	}

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* A lost arrival leaves the decoder's state unknown. */
	if (overflow) {
		kern_logf("rpi4-mmal: decoder %u queue overflow; decoder stopped\n", decoder->slot);
		decoder->broken = true;
		return EIO;
	}
	if (empty)
		return EAGAIN;

	/* Succeeded: message holds the arrival. */
	return 0;
}

/*
 * Takes one buffer the firmware gave back.
 *
 * An input buffer is free again.  An output buffer with data (or an empty
 * end of stream) has its bytes received now, in arrival order, into its
 * bounce memory and waits for the caller; an empty one (a flush, a
 * disable) goes back to the firmware while the port is enabled.
 */
static int
handle_buffer(
	struct drv_rpi4_mmal_decoder *decoder,
	const struct mmal_message *message)
{
	struct mmal_output *output;
	unsigned long enabled;
	uint32_t value;
	uint32_t kind;
	uint32_t index;
	uint32_t length;
	uint32_t offset;
	uint32_t flags;
	uint32_t bytes;
	uint32_t pitch;
	uint32_t chroma;
	int error;

	/* The buffer's name, and what it carries. */
	value = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_CONTEXT);
	kind = (value >> 12) & 0xfU;
	index = value & 0xfffU;
	length = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_LENGTH);
	offset = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_OFFSET);
	flags = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_FLAGS);

	/* An input buffer comes back empty: it and its staging area's count are free. */
	if (kind == MMAL_KIND_INPUT) {
		if (index >= decoder->input_count)
			return 0;
		if (length != 0) {
			/* Unexpected data for an input buffer: received into the scratch and dropped. */
			error = receive_bulk(decoder, bus_to_physical(decoder->scratch.device_address), 8U, false);
			if (error != 0)
				return error;
		}
		decoder->input_free[index] = true;
		enabled = spin_lock_irqsave(&mmal_lock);

		decoder->half[decoder->input_half[index]].buffers--;

		spin_unlock_irqrestore(&mmal_lock, enabled);

		return 0;
	}

	/* Anything else must be one of the output buffers. */
	if (kind != MMAL_KIND_OUTPUT || index >= decoder->output_count)
		return 0;
	output = &decoder->outputs[index];
	output->with_firmware = false;

	/* An empty buffer that is not an end of stream goes straight back. */
	if (length == 0 && (flags & DRV_RPI4_MMAL_FLAG_EOS) == 0) {
		error = give_output(decoder, index);
		return error;
	}

	/* The bulk: four-byte multiples, eight bytes for an empty end of stream. */
	bytes = (length + 3U) & ~3U;
	if (bytes == 0)
		bytes = 8U;
	if ((uint64_t)offset + bytes > decoder->output_size) {
		kern_logf("rpi4-mmal: frame of %u bytes at %u overruns its %u-byte buffer\n",
			  (unsigned)length, (unsigned)offset, (unsigned)decoder->output_size);
		return EIO;
	}
	/*
	 * A frame laid out as the target expects, and fitting it, goes there
	 * (once); any other goes to the buffer's bounce memory.
	 */
	output->in_target = false;
	pitch = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_PITCH);
	chroma = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_OFFSET + 4U);
	if (drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANES) < 2U || pitch == 0) {
		/* Without plane information the format's stride and height give the layout. */
		pitch = decoder->width;
		chroma = decoder->width * decoder->height;
	}
	if (decoder->target_capacity != 0 &&
	    offset == 0 &&
	    bytes <= decoder->target_capacity &&
	    pitch == decoder->target_pitch &&
	    chroma == decoder->target_chroma) {
		output->in_target = true;
		error = receive_bulk(decoder, decoder->target_physical, bytes, true);
		decoder->target_capacity = 0;
	} else {
		error = receive_bulk(decoder, bus_to_physical(output->memory.device_address) + offset, bytes, true);
	}
	if (error != 0)
		return error;

	/* The frame waits for the caller, with its layout. */
	output->ready = true;
	output->length = length;
	output->flags = flags;
	output->pts = (int64_t)drv_rpi4_mmal_get64(message->bytes, DRV_RPI4_MMAL_BUFFER_PTS);
	output->planes = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANES);
	output->plane_offset[0] = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_OFFSET) + offset;
	output->plane_offset[1] = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_OFFSET + 4U) + offset;
	output->plane_pitch[0] = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_PITCH);
	output->plane_pitch[1] = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_BUFFER_PLANE_PITCH + 4U);

	/* Succeeded: the frame is received. */
	return 0;
}

/*
 * Takes one event: its data comes in the message, or (beyond 256 bytes)
 * as a bulk received now into the scratch.  A format change reconfigures
 * the output; other events are logged.
 */
static int
handle_event(
	struct drv_rpi4_mmal_decoder *decoder,
	const struct mmal_message *message)
{
	const uint8_t *data;
	uint32_t command;
	uint32_t length;
	int error;

	/* The event's command and data. */
	command = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_EVENT_CMD);
	length = drv_rpi4_mmal_get32(message->bytes, DRV_RPI4_MMAL_EVENT_LENGTH);
	data = message->bytes + DRV_RPI4_MMAL_EVENT_DATA;
	if (length > DRV_RPI4_MMAL_EVENT_DATA_BYTES) {
		if (length > MMAL_SCRATCH_BYTES)
			return EIO;
		error = receive_bulk(decoder, bus_to_physical(decoder->scratch.device_address), (length + 3U) & ~3U, false);
		if (error != 0)
			return error;
		data = decoder->scratch.address;
	}

	/* A format change needs its whole description. */
	if (command == DRV_RPI4_MMAL_EVENT_FORMAT_CHANGED && length >= DRV_RPI4_MMAL_CHANGED_BYTES) {
		error = format_changed(decoder, data);
		return error;
	}

	/* Succeeded: any other event is only reported. */
	kern_logf("rpi4-mmal: decoder %u event %08x (%u bytes)\n", decoder->slot, (unsigned)command, (unsigned)length);
	return 0;
}

/*
 * Reconfigures the output for a new format: disables the port (the
 * firmware gives every buffer back), sets the format and the buffer needs
 * the event gives, and enables it again with its buffers.
 */
static int
format_changed(
	struct drv_rpi4_mmal_decoder *decoder,
	const uint8_t *data)
{
	uint8_t *port;
	int error;

	/* Disables the output; its buffers come back empty and stay with the client. */
	decoder->output_enabled = false;
	error = port_action(decoder, decoder->output_handle, DRV_RPI4_MMAL_ACTION_DISABLE, NULL);
	if (error != 0)
		return error;

	/* Takes the new format and buffer needs into the output's port info. */
	port = decoder->output_info + DRV_RPI4_MMAL_INFO_PORT;
	kern_memcpy(port + DRV_RPI4_MMAL_PORT_BYTES, data + DRV_RPI4_MMAL_CHANGED_FORMAT, DRV_RPI4_MMAL_FORMAT_BYTES);
	kern_memcpy(port + DRV_RPI4_MMAL_PORT_BYTES + DRV_RPI4_MMAL_FORMAT_BYTES, data + DRV_RPI4_MMAL_CHANGED_ES, DRV_RPI4_MMAL_ES_BYTES);
	drv_rpi4_mmal_put32(port + DRV_RPI4_MMAL_PORT_BYTES, DRV_RPI4_MMAL_FORMAT_EXTRADATA_SIZE, 0);
	drv_rpi4_mmal_put32(port, DRV_RPI4_MMAL_PORT_BUFFER_SIZE_MIN, drv_rpi4_mmal_get32(data, DRV_RPI4_MMAL_CHANGED_SIZE_MIN));
	drv_rpi4_mmal_put32(port, DRV_RPI4_MMAL_PORT_BUFFER_NUM_MIN, drv_rpi4_mmal_get32(data, DRV_RPI4_MMAL_CHANGED_NUM_MIN));
	drv_rpi4_mmal_put32(port, DRV_RPI4_MMAL_PORT_BUFFER_SIZE_RECOMMENDED, drv_rpi4_mmal_get32(data, DRV_RPI4_MMAL_CHANGED_SIZE_RECOMMENDED));
	drv_rpi4_mmal_put32(port, DRV_RPI4_MMAL_PORT_BUFFER_NUM_RECOMMENDED, drv_rpi4_mmal_get32(data, DRV_RPI4_MMAL_CHANGED_NUM_RECOMMENDED));
	error = port_set(decoder, decoder->output_info);
	if (error != 0)
		return error;

	/* Absorbs the buffers the disable gave back before reusing their memory. */
	error = pump(decoder);
	if (error != 0)
		return error;

	/* Enables the output again with buffers of the new size. */
	error = enable_output(decoder);
	if (error != 0)
		return error;

	/* Succeeded: frames of the new format follow. */
	kern_logf("rpi4-mmal: decoder %u output %08x %ux%u (crop %ux%u), %u x %u bytes\n",
		  decoder->slot, (unsigned)decoder->encoding, (unsigned)decoder->width, (unsigned)decoder->height,
		  (unsigned)decoder->crop_width, (unsigned)decoder->crop_height,
		  (unsigned)decoder->output_count, (unsigned)decoder->output_size);
	return 0;
}

/*
 * Receives one bulk the firmware is about to send, and waits for it.
 *
 * frame says whether it lands in an output buffer (else the scratch).
 */
static int
receive_bulk(
	struct drv_rpi4_mmal_decoder *decoder,
	uint64_t physical,
	uint32_t bytes,
	bool frame)
{
	struct mmal_bulk_tag *tag;
	unsigned long enabled;
	int error;

	/* Marks the bulk outstanding before it can complete. */
	tag = &decoder->scratch_tag;
	if (frame)
		tag = &decoder->frame_tag;
	enabled = spin_lock_irqsave(&mmal_lock);

	if (frame) {
		decoder->frame_pending = true;
		decoder->frame_actual = 0;
	} else {
		decoder->scratch_pending = true;
	}

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* Queues the receive without a message. */
	error = drv_rpi4_vchiq_send_bulk(mmal_service, NULL, 0, DRV_RPI4_VCHIQ_BULK_RECEIVE, physical, bytes, tag);
	if (error != 0) {
		enabled = spin_lock_irqsave(&mmal_lock);

		decoder->frame_pending = false;
		decoder->scratch_pending = false;

		spin_unlock_irqrestore(&mmal_lock, enabled);

		return error;
	}

	/* Waits for the firmware's transfer. */
	error = wait_bulk(decoder, frame);
	if (error != 0)
		return error;

	/* Succeeded: the bytes are in memory. */
	return 0;
}

/* Waits for the outstanding frame or scratch bulk; a timeout leaves it outstanding. */
static int
wait_bulk(
	struct drv_rpi4_mmal_decoder *decoder,
	bool frame)
{
	unsigned long enabled;
	uint64_t deadline;
	uint64_t observed;
	bool pending;
	int32_t actual;
	int error;

	/* Sleeps until the completion or the deadline. */
	deadline = sched_ticks() + kern_ms_to_ticks(MMAL_WAIT_MS) + 1U;
	error = 0;
	enabled = spin_lock_irqsave(&mmal_lock);

	for (;;) {
		pending = decoder->scratch_pending;
		if (frame)
			pending = decoder->frame_pending;
		if (!pending)
			break;
		observed = waitq_sequence(&mmal_wake);
		error = waitq_sleep(&mmal_wake, &mmal_lock, observed, deadline, 0);
		if (error == ETIMEDOUT)
			break;
		error = 0;
	}
	actual = decoder->frame_actual;

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* A bulk that did not complete keeps its memory for good. */
	if (error != 0) {
		kern_logf("rpi4-mmal: bulk not completed in %u ms; decoder stopped\n", MMAL_WAIT_MS);
		decoder->broken = true;
		return error;
	}

	/* An aborted frame bulk is a lost frame. */
	if (frame && actual < 0)
		return EIO;

	/* Succeeded: the transfer completed. */
	return 0;
}

/* Sleeps until something is queued for the decoder or the deadline passes. */
static int
wait_queue_or(
	struct drv_rpi4_mmal_decoder *decoder,
	uint64_t deadline)
{
	unsigned long enabled;
	uint64_t observed;
	int error;

	/* Sleeps only while the queue is empty. */
	error = 0;
	enabled = spin_lock_irqsave(&mmal_lock);

	if (decoder->queue_count == 0 && !decoder->queue_overflow) {
		observed = waitq_sequence(&mmal_wake);
		error = waitq_sleep(&mmal_wake, &mmal_lock, observed, deadline, 0);
		if (error != ETIMEDOUT)
			error = 0;
		if (decoder->queue_count != 0)
			error = 0;
	}

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* Reports a deadline that passed with nothing queued. */
	if (error != 0)
		return error;

	/* Succeeded: the caller looks at the queue again. */
	return 0;
}

/* Allocates zeroed DMA memory in the VPU's window. */
static int
dma_alloc(
	size_t bytes,
	struct drv_dma_buffer *buffer)
{
	int error;

	/* Page-aligned uncached memory below 1 GiB. */
	error = drv_dma_alloc_coherent(mmal_dma, bytes, 4096U, buffer);
	if (error != 0) {
		kern_memset(buffer, 0, sizeof(*buffer));
		kern_logf("rpi4-mmal: no %u bytes of DMA memory (%d)\n", (unsigned)bytes, error);
		return error;
	}

	/* Succeeded: the memory is zero. */
	kern_memset(buffer->address, 0, bytes);
	return 0;
}

/* Converts a bus address in the VPU's window to the physical address. */
static uint64_t
bus_to_physical(
	uint64_t bus)
{
	/* The window maps physical zero at MMAL_BUS_RAM. */
	return bus - MMAL_BUS_RAM;
}

/* Makes the client's name for a decoder's buffer or component. */
static uint32_t
token(
	struct drv_rpi4_mmal_decoder *decoder,
	uint32_t kind,
	uint32_t index)
{
	/* The tag, the decoder's slot, the kind and the index. */
	return MMAL_TOKEN | ((uint32_t)decoder->slot << 16) | (kind << 12) | (index & 0xfffU);
}

/* Finds the decoder a name belongs to, or NULL. */
static struct drv_rpi4_mmal_decoder *
decoder_of(
	uint32_t value)
{
	uint32_t slot;

	/* Only the client's names carry the tag. */
	if ((value & MMAL_TOKEN_MASK) != MMAL_TOKEN)
		return NULL;
	slot = (value >> 16) & 0xffU;
	if (slot >= MMAL_DECODERS)
		return NULL;

	/* Succeeded: the decoder of the slot (a retired one still counts its bulks). */
	return &mmal_decoders[slot];
}

/* Reports the larger of two values. */
static uint32_t
maximum(
	uint32_t first,
	uint32_t second)
{
	/* The first unless the second is larger. */
	if (second > first)
		return second;

	/* Succeeded: the first is the larger. */
	return first;
}

/* Frees a decoder's DMA memory; nothing of it may be outstanding. */
static void
release_memory(
	struct drv_rpi4_mmal_decoder *decoder)
{
	unsigned index;

	/* The staging, the scratch and the bounce buffers. */
	if (decoder->staging_memory.address != NULL)
		drv_dma_free_coherent(mmal_dma, &decoder->staging_memory);
	if (decoder->scratch.address != NULL)
		drv_dma_free_coherent(mmal_dma, &decoder->scratch);
	for (index = 0; index < MMAL_OUTPUT_BUFFERS; index++) {
		if (decoder->outputs[index].memory.address != NULL)
			drv_dma_free_coherent(mmal_dma, &decoder->outputs[index].memory);
	}
}

/* Reports whether any bulk of the decoder may still move memory. */
static bool
bulks_outstanding(
	struct drv_rpi4_mmal_decoder *decoder)
{
	unsigned long enabled;
	bool outstanding;

	/* The staging bulks and the frame and scratch receives. */
	enabled = spin_lock_irqsave(&mmal_lock);

	outstanding = decoder->half[0].bulks != 0 || decoder->half[1].bulks != 0;
	if (decoder->frame_pending || decoder->scratch_pending)
		outstanding = true;

	spin_unlock_irqrestore(&mmal_lock, enabled);

	/* Succeeded: whether memory is still the firmware's. */
	return outstanding;
}
