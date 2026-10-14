/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The small part of H.264's bitstream syntax the VideoCore decoder's
 * driver needs: finding access units in an Annex B stream, and rewriting a
 * sequence parameter set with VUI that asks for no output reordering (so a
 * decoder that honours it outputs each picture as soon as it is decoded).
 *
 * Written from the H.264 specification (ITU-T H.264, 7.3.2.1.1 and E.1.1).
 */

#ifndef KERN_DRIVERS_RPI4_H264_H
#define KERN_DRIVERS_RPI4_H264_H

#include <stddef.h>
#include <stdint.h>

/* The NAL unit types the access unit split looks at. */
#define DRV_RPI4_H264_NAL_SLICE		1U
#define DRV_RPI4_H264_NAL_IDR		5U
#define DRV_RPI4_H264_NAL_SEI		6U
#define DRV_RPI4_H264_NAL_SPS		7U
#define DRV_RPI4_H264_NAL_PPS		8U
#define DRV_RPI4_H264_NAL_AUD		9U

/* The most bytes a rewritten parameter set may take. */
#define DRV_RPI4_H264_SPS_MAX		512U

/*
 * Finds the next NAL unit of an Annex B stream at or after offset.
 *
 * start receives the offset of the NAL unit's first byte (after its start
 * code) and end the offset of the next start code or the stream's end.
 * Returns ENOENT when no NAL unit follows.
 */
int drv_rpi4_h264_next_nal(const uint8_t *stream, size_t bytes, size_t offset, size_t *start, size_t *end);

/*
 * Finds the end of the access unit that starts at offset.
 *
 * An access unit ends before the NAL unit that starts the next one: an
 * AUD, SPS, PPS or SEI after a slice, or a slice whose first macroblock is
 * zero after a slice.  end receives the offset of that NAL unit's start
 * code, or the stream's end.
 */
int drv_rpi4_h264_access_unit(const uint8_t *stream, size_t bytes, size_t offset, size_t *end);

/*
 * Rewrites one SPS NAL unit with VUI that asks for no reordering.
 *
 * nal is the NAL unit without its start code; output receives a start
 * code and the rewritten NAL unit, with emulation prevention.  Every field
 * before the VUI is kept bit for bit; the VUI is replaced by one whose only
 * content is the bitstream restriction: no limit on the bytes or bits of a
 * picture, motion vectors over picture boundaries allowed, 2^15 quarter
 * samples of motion, max_num_reorder_frames 0 and max_dec_frame_buffering
 * equal to max_num_ref_frames (at least 1).
 */
int drv_rpi4_h264_sps_no_reorder(const uint8_t *nal, size_t bytes, uint8_t *output, size_t capacity, size_t *written);

#endif
