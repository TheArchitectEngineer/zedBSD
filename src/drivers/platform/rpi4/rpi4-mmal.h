/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The VideoCore firmware's H.264 decoder (MMAL's ril.video_decode), driven
 * over VCHIQ.
 *
 * A decoder is fed whole access units and hands back decoded NV12 frames.
 * The client is synchronous and simple: a caller fills the decoder's input
 * staging, sends it, and receives frames one at a time into the decoder's
 * own bounce memory.  Every bulk transfer's memory stays the decoder's until
 * the firmware has completed it.
 */

#ifndef KERN_DRIVERS_RPI4_MMAL_H
#define KERN_DRIVERS_RPI4_MMAL_H

#include <stdbool.h>
#include <stdint.h>

/* The frame flags a receive reports (MMAL's buffer flags). */
#define DRV_RPI4_MMAL_FRAME_EOS		0x0001U
#define DRV_RPI4_MMAL_FRAME_CORRUPTED	0x0200U

/* A frame's time stamp when the firmware gave none, and the encoding of an NV12 frame. */
#define DRV_RPI4_MMAL_FRAME_NO_TIME	((int64_t)((uint64_t)1U << 63))
#define DRV_RPI4_MMAL_FRAME_NV12	0x3231564eU

struct drv_rpi4_mmal_decoder;

/*
 * One decoded frame: its time stamp and flags, the bytes received, and the
 * layout of the planes in them.
 */
struct drv_rpi4_mmal_frame {
	int64_t pts;
	uint32_t flags;
	uint32_t length;
	uint32_t encoding;
	uint32_t width;
	uint32_t height;
	uint32_t crop_width;
	uint32_t crop_height;
	uint32_t planes;
	uint32_t plane_offset[2];
	uint32_t plane_pitch[2];
	int in_target;
};

/*
 * Creates a decoder for H.264 pictures of up to width by height samples.
 *
 * input_bytes is the size of each of the two input staging areas, the
 * largest access unit the decoder takes.  Waits for VCHIQ's connection.
 * Thread context only.
 */
int drv_rpi4_mmal_decoder_create(uint32_t width, uint32_t height, uint32_t input_bytes, struct drv_rpi4_mmal_decoder **result);

/*
 * Reports the input staging the next access unit goes into, waiting up to
 * one second until the firmware has finished with it.
 */
int drv_rpi4_mmal_decoder_input(struct drv_rpi4_mmal_decoder *decoder, uint8_t **staging, uint32_t *capacity);

/*
 * Sends the access unit written into the staging the last input call gave.
 *
 * The unit is split into the firmware's input buffers; the first carries
 * the start-of-frame flag, the last the end-of-frame flag, and every one
 * the time stamp.  With config, the staging holds parameter sets only and
 * goes as codec configuration instead (no frame flags).
 */
int drv_rpi4_mmal_decoder_send(struct drv_rpi4_mmal_decoder *decoder, uint32_t bytes, int64_t pts, int config);

/*
 * Sends an end of stream (an empty buffer): the firmware outputs every
 * picture it holds, then an empty frame with the end-of-stream flag.
 */
int drv_rpi4_mmal_decoder_end(struct drv_rpi4_mmal_decoder *decoder);

/*
 * Names memory the next decoded frame goes straight into.
 *
 * physical is physically contiguous memory of capacity bytes whose plane
 * layout is pitch bytes a row and the chroma plane at chroma_offset.  The
 * next frame whose layout and size match is received there instead of the
 * bounce memory (its frame reports in_target), and the target is used up;
 * a frame that does not match goes to the bounce memory as before.  The
 * memory must stay until the frame is received or the target is cleared
 * with a zero capacity.
 */
void drv_rpi4_mmal_decoder_target(struct drv_rpi4_mmal_decoder *decoder, uint64_t physical, uint32_t capacity, uint32_t pitch, uint32_t chroma_offset);

/*
 * Receives the next decoded frame, waiting up to timeout_ms.
 *
 * pixels receives the frame's bytes in the decoder's bounce memory, valid
 * until the next receive, or NULL when the frame went into the target.
 * ETIMEDOUT means no frame came in time.
 */
int drv_rpi4_mmal_decoder_receive(struct drv_rpi4_mmal_decoder *decoder, unsigned timeout_ms, struct drv_rpi4_mmal_frame *frame, const uint8_t **pixels);

/*
 * Reports whether a bulk transfer of the decoder may still move memory
 * (one that did not complete in time): its target and bounce memory must
 * then never be reused.
 */
int drv_rpi4_mmal_decoder_busy(struct drv_rpi4_mmal_decoder *decoder);

/* Drops every picture and reference the decoder holds, as at a seek. */
int drv_rpi4_mmal_decoder_flush(struct drv_rpi4_mmal_decoder *decoder);

/* Destroys a decoder; memory a bulk transfer still holds is kept, not freed. */
void drv_rpi4_mmal_decoder_destroy(struct drv_rpi4_mmal_decoder *decoder);

#endif
