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

#include <stdint.h>

/* The frame flags a receive reports (MMAL's buffer flags). */
#define DRV_RPI4_MMAL_FRAME_EOS		0x0001U
#define DRV_RPI4_MMAL_FRAME_CORRUPTED	0x0200U

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
 * The unit is split into the firmware's input buffers; the last carries
 * the end-of-frame flag and every one the time stamp.
 */
int drv_rpi4_mmal_decoder_send(struct drv_rpi4_mmal_decoder *decoder, uint32_t bytes, int64_t pts);

/*
 * Receives the next decoded frame, waiting up to timeout_ms.
 *
 * pixels receives the frame's bytes in the decoder's bounce memory, valid
 * until the next receive.  ETIMEDOUT means no frame came in time.
 */
int drv_rpi4_mmal_decoder_receive(struct drv_rpi4_mmal_decoder *decoder, unsigned timeout_ms, struct drv_rpi4_mmal_frame *frame, const uint8_t **pixels);

/* Drops every picture and reference the decoder holds, as at a seek. */
int drv_rpi4_mmal_decoder_flush(struct drv_rpi4_mmal_decoder *decoder);

/* Destroys a decoder; memory a bulk transfer still holds is kept, not freed. */
void drv_rpi4_mmal_decoder_destroy(struct drv_rpi4_mmal_decoder *decoder);

#endif
