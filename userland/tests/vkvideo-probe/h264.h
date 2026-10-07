/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The H.264 elementary stream reader of vkvideo-probe.
 *
 * Vulkan Video leaves the bitstream to the application: it hands the
 * decoder the parameter sets and each picture's information as StdVideo
 * structures, and the slices as offsets into a buffer.  This reader splits
 * an Annex B stream into its NAL units, parses the sequence and picture
 * parameter sets into StdVideo form, groups the slices into pictures and
 * works out each picture's frame number and order counts from its first
 * slice header.  It covers what the probe decodes: progressive frames of
 * 4:2:0 8-bit Baseline, Main and High streams, one slice group, the order
 * count types 0 and 2.
 */

#ifndef VKVIDEO_PROBE_H264_H
#define VKVIDEO_PROBE_H264_H

#include <stddef.h>
#include <stdint.h>

/* The StdVideo H.264 structures, through the Vulkan header (zedBSD's and Khronos' both carry them). */
#include <vulkan/vulkan.h>

/* The parameter set id ranges of H.264. */
#define H264_SPS_IDS		32U
#define H264_PPS_IDS		256U

/* The most slices of a picture the probe hands the decoder. */
#define H264_MAX_SLICES		256U

/* The slice types of a picture the probe decodes: I and SI only (P and B are ws083-p006's). */
#define H264_SLICE_P		0U
#define H264_SLICE_B		1U
#define H264_SLICE_I		2U
#define H264_SLICE_SP		3U
#define H264_SLICE_SI		4U

/* One NAL unit of the stream: its type, its nal_ref_idc and its bytes after the start code. */
struct h264_nal {
	uint32_t type;
	uint32_t ref_idc;
	size_t offset;
	size_t size;
};

/*
 * One picture: its StdVideoDecodeH264PictureInfo, whether all its slices
 * are intra, and its slices, the NAL units after their start codes.
 */
struct h264_picture {
	StdVideoDecodeH264PictureInfo info;
	int intra;
	uint32_t slice_count;
	size_t slice_offsets[H264_MAX_SLICES];
	size_t slice_sizes[H264_MAX_SLICES];
};

/*
 * A stream being read: the bytes, the parameter sets seen (the last of
 * each id), and the order count state the pictures carry over.
 */
struct h264_stream {
	const uint8_t *data;
	size_t size;
	size_t cursor;

	/* The sequence parameter sets, with the lists their pointers name. */
	int has_sps[H264_SPS_IDS];
	StdVideoH264SequenceParameterSet sps[H264_SPS_IDS];
	StdVideoH264ScalingLists sps_scaling[H264_SPS_IDS];
	int32_t sps_offsets[H264_SPS_IDS][255];

	/* The picture parameter sets, with the lists their pointers name. */
	int has_pps[H264_PPS_IDS];
	StdVideoH264PictureParameterSet pps[H264_PPS_IDS];
	StdVideoH264ScalingLists pps_scaling[H264_PPS_IDS];

	/* The order count state: the last reference picture's MSB and LSB (type 0), frame number and offset (type 2). */
	int32_t previous_msb;
	int32_t previous_lsb;
	uint32_t previous_frame_num;
	uint32_t previous_frame_offset;
};

int h264_open(struct h264_stream *stream, const uint8_t *data, size_t size);
int h264_next_picture(struct h264_stream *stream, struct h264_picture *picture, const char **reason);
int h264_nal_next(const uint8_t *data, size_t size, size_t *cursor, struct h264_nal *nal);

#endif
