/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Access units and the no-reorder SPS rewrite (see rpi4-h264.h).
 */

#include "drivers/platform/rpi4/rpi4-h264.h"
#include <kern/kcrt.h>
#include <uapi/errno.h>

/* The most RBSP bytes an SPS may have here (far more than any real one). */
#define H264_RBSP_MAX			DRV_RPI4_H264_SPS_MAX

/* The low five bits of a NAL unit's first byte: its type. */
#define H264_NAL_TYPE_MASK		0x1fU

/* The value written for both log2_max_mv_length fields. */
#define H264_MAX_MV_LENGTH		15U

/*
 * Reads bits of an RBSP, most significant first.
 *
 * error is set, and stays set, when a read runs past the end; the reads
 * then return zero.
 */
struct h264_reader {
	const uint8_t *data;
	size_t bits;
	size_t position;
	int error;
};

/*
 * Writes bits of an RBSP, most significant first, into a zeroed buffer.
 *
 * error is set, and stays set, when a write would run past the capacity.
 */
struct h264_writer {
	uint8_t *data;
	size_t capacity_bits;
	size_t position;
	int error;
};

static int is_slice(uint8_t type);
static int starts_new_unit(const uint8_t *stream, size_t start, size_t end, int seen_slice);
static size_t unescape(const uint8_t *nal, size_t bytes, uint8_t *rbsp, size_t capacity);
static uint32_t read_bits(struct h264_reader *reader, unsigned count);
static uint32_t read_ue(struct h264_reader *reader);
static int32_t read_se(struct h264_reader *reader);
static void skip_scaling_list(struct h264_reader *reader, unsigned size);
static void write_bits(struct h264_writer *writer, uint32_t value, unsigned count);
static void write_ue(struct h264_writer *writer, uint32_t value);
static int high_profile(uint32_t profile);
static void write_se(struct h264_writer *writer, int32_t value);
static void write_vui_no_reorder(struct h264_writer *writer, uint32_t references);
static void write_trailing(struct h264_writer *writer);
static void write_scaling(struct h264_writer *writer, const struct drv_rpi4_h264_scaling *scaling, uint32_t lists);
static uint32_t level_number(uint32_t level);
static int pack_nal(uint8_t header, const uint8_t *rbsp, size_t bytes, uint8_t *output, size_t capacity, size_t *written);

/*
 * Writes an SPS NAL unit from its fields.
 *
 * The syntax is 7.3.2.1.1's, field by field; the scaling lists are coded
 * as present, default or explicit with every delta written.
 */
int
drv_rpi4_h264_write_sps(
	const struct drv_rpi4_h264_sps *sps,
	int no_reorder,
	uint8_t *output,
	size_t capacity,
	size_t *written)
{
	uint8_t rbsp[H264_RBSP_MAX + 2048U];
	struct h264_writer writer;
	uint32_t constraints;
	uint32_t lists;
	uint32_t index;
	int error;

	/* Refuses fields H.264 cannot code (7.4.2.1.1's ranges, and what Exp-Golomb here holds). */
	if (sps->offset_count > 255U || sps->id > 31U || sps->level > 18U)
		return EINVAL;
	if (sps->chroma_format_idc > 3U || sps->bit_depth_luma_minus8 > 6U || sps->bit_depth_chroma_minus8 > 6U)
		return EINVAL;
	if (sps->log2_max_frame_num_minus4 > 12U || sps->pic_order_cnt_type > 2U || sps->log2_max_pic_order_cnt_lsb_minus4 > 12U)
		return EINVAL;
	if (sps->max_num_ref_frames > 16U || sps->pic_width_in_mbs_minus1 > 1023U || sps->pic_height_in_map_units_minus1 > 1023U)
		return EINVAL;
	for (index = 0; index < 4U; index++) {
		if (sps->crop[index] > 0xffffU)
			return EINVAL;
	}
	if (sps->offset_for_non_ref_pic == INT32_MIN || sps->offset_for_top_to_bottom_field == INT32_MIN)
		return EINVAL;
	for (index = 0; index < sps->offset_count; index++) {
		if (sps->offsets[index] == INT32_MIN)
			return EINVAL;
	}
	kern_memset(rbsp, 0, sizeof(rbsp));
	writer.data = rbsp;
	writer.capacity_bits = sizeof(rbsp) * 8U;
	writer.position = 0;
	writer.error = 0;

	/* profile_idc, constraint_set0..5 and two reserved zeros, level_idc, the id. */
	constraints = 0;
	for (index = 0; index < 6U; index++) {
		if ((sps->flags & (DRV_RPI4_H264_SPS_CONSTRAINT_SET0 << index)) != 0)
			constraints |= 0x80U >> index;
	}
	write_bits(&writer, sps->profile_idc, 8);
	write_bits(&writer, constraints, 8);
	write_bits(&writer, level_number(sps->level), 8);
	write_ue(&writer, sps->id);

	/* The high profiles' chroma format, bit depths and scaling matrix. */
	if (high_profile(sps->profile_idc)) {
		write_ue(&writer, sps->chroma_format_idc);
		if (sps->chroma_format_idc == 3U)
			write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_SEPARATE_PLANES) != 0, 1);
		write_ue(&writer, sps->bit_depth_luma_minus8);
		write_ue(&writer, sps->bit_depth_chroma_minus8);
		write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_QPPRIME_BYPASS) != 0, 1);
		write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_SCALING) != 0, 1);
		if ((sps->flags & DRV_RPI4_H264_SPS_SCALING) != 0) {
			lists = 8U;
			if (sps->chroma_format_idc == 3U)
				lists = 12U;
			write_scaling(&writer, &sps->scaling, lists);
		}
	}

	/* The frame number and the picture order count syntax. */
	write_ue(&writer, sps->log2_max_frame_num_minus4);
	write_ue(&writer, sps->pic_order_cnt_type);
	if (sps->pic_order_cnt_type == 0) {
		write_ue(&writer, sps->log2_max_pic_order_cnt_lsb_minus4);
	} else if (sps->pic_order_cnt_type == 1U) {
		write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_DELTA_POC_ZERO) != 0, 1);
		write_se(&writer, sps->offset_for_non_ref_pic);
		write_se(&writer, sps->offset_for_top_to_bottom_field);
		write_ue(&writer, sps->offset_count);
		for (index = 0; index < sps->offset_count; index++)
			write_se(&writer, sps->offsets[index]);
	}

	/* The references, the size, the field flags and the cropping. */
	write_ue(&writer, sps->max_num_ref_frames);
	write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_GAPS_ALLOWED) != 0, 1);
	write_ue(&writer, sps->pic_width_in_mbs_minus1);
	write_ue(&writer, sps->pic_height_in_map_units_minus1);
	write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_FRAME_MBS_ONLY) != 0, 1);
	if ((sps->flags & DRV_RPI4_H264_SPS_FRAME_MBS_ONLY) == 0)
		write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_MB_ADAPTIVE) != 0, 1);
	write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_DIRECT_8X8) != 0, 1);
	write_bits(&writer, (sps->flags & DRV_RPI4_H264_SPS_CROPPING) != 0, 1);
	if ((sps->flags & DRV_RPI4_H264_SPS_CROPPING) != 0) {
		for (index = 0; index < 4U; index++)
			write_ue(&writer, sps->crop[index]);
	}

	/* The VUI asking for no reordering, or none. */
	if (no_reorder) {
		write_vui_no_reorder(&writer, sps->max_num_ref_frames);
	} else {
		write_bits(&writer, 0, 1);
	}
	write_trailing(&writer);
	if (writer.error != 0)
		return ENOSPC;

	/* The start code, the header (nal_ref_idc 3, type 7) and the escaped body. */
	error = pack_nal(0x67U, rbsp, writer.position / 8U, output, capacity, written);
	if (error != 0)
		return error;

	/* Succeeded: output holds the unit. */
	return 0;
}

/*
 * Writes a PPS NAL unit from its fields (7.3.2.2), with one slice group.
 *
 * The optional tail (the 8x8 transform, the scaling lists and the second
 * chroma offset) is written only when one of them differs from what its
 * absence means.
 */
int
drv_rpi4_h264_write_pps(
	const struct drv_rpi4_h264_pps *pps,
	const struct drv_rpi4_h264_sps *sps,
	uint8_t *output,
	size_t capacity,
	size_t *written)
{
	uint8_t rbsp[H264_RBSP_MAX + 2048U];
	struct h264_writer writer;
	uint32_t lists;
	int tail;
	int error;

	/* Refuses fields H.264 cannot code (7.4.2.2's ranges). */
	if (pps->pps_id > 255U || pps->sps_id > 31U || pps->weighted_bipred_idc > 2U)
		return EINVAL;
	if (pps->num_ref_idx_l0_default_active_minus1 > 31U || pps->num_ref_idx_l1_default_active_minus1 > 31U)
		return EINVAL;
	if (pps->pic_init_qp_minus26 < -64 || pps->pic_init_qp_minus26 > 25 ||
	    pps->pic_init_qs_minus26 < -26 || pps->pic_init_qs_minus26 > 25)
		return EINVAL;
	if (pps->chroma_qp_index_offset < -12 || pps->chroma_qp_index_offset > 12 ||
	    pps->second_chroma_qp_index_offset < -12 || pps->second_chroma_qp_index_offset > 12)
		return EINVAL;
	kern_memset(rbsp, 0, sizeof(rbsp));
	writer.data = rbsp;
	writer.capacity_bits = sizeof(rbsp) * 8U;
	writer.position = 0;
	writer.error = 0;

	/* The ids, the entropy coder, the field order flag and one slice group. */
	write_ue(&writer, pps->pps_id);
	write_ue(&writer, pps->sps_id);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_CABAC) != 0, 1);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_BOTTOM_FIELD_POC) != 0, 1);
	write_ue(&writer, 0);

	/* The default references, the weighted prediction and the quantizers. */
	write_ue(&writer, pps->num_ref_idx_l0_default_active_minus1);
	write_ue(&writer, pps->num_ref_idx_l1_default_active_minus1);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_WEIGHTED_PRED) != 0, 1);
	write_bits(&writer, pps->weighted_bipred_idc, 2);
	write_se(&writer, pps->pic_init_qp_minus26);
	write_se(&writer, pps->pic_init_qs_minus26);
	write_se(&writer, pps->chroma_qp_index_offset);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_DEBLOCKING_CONTROL) != 0, 1);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_CONSTRAINED_INTRA) != 0, 1);
	write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_REDUNDANT_PIC_CNT) != 0, 1);

	/* The optional tail, when any of it says something. */
	tail = 0;
	if ((pps->flags & (DRV_RPI4_H264_PPS_TRANSFORM_8X8 | DRV_RPI4_H264_PPS_SCALING)) != 0)
		tail = 1;
	if (pps->second_chroma_qp_index_offset != pps->chroma_qp_index_offset)
		tail = 1;
	if (tail) {
		write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_TRANSFORM_8X8) != 0, 1);
		write_bits(&writer, (pps->flags & DRV_RPI4_H264_PPS_SCALING) != 0, 1);
		if ((pps->flags & DRV_RPI4_H264_PPS_SCALING) != 0) {
			lists = 6U;
			if ((pps->flags & DRV_RPI4_H264_PPS_TRANSFORM_8X8) != 0) {
				lists += 2U;
				if (sps != NULL && sps->chroma_format_idc == 3U)
					lists += 4U;
			}
			write_scaling(&writer, &pps->scaling, lists);
		}
		write_se(&writer, pps->second_chroma_qp_index_offset);
	}
	write_trailing(&writer);
	if (writer.error != 0)
		return ENOSPC;

	/* The start code, the header (nal_ref_idc 3, type 8) and the escaped body. */
	error = pack_nal(0x68U, rbsp, writer.position / 8U, output, capacity, written);
	if (error != 0)
		return error;

	/* Succeeded: output holds the unit. */
	return 0;
}

/*
 * Finds the next NAL unit of an Annex B stream at or after offset.
 */
int
drv_rpi4_h264_next_nal(
	const uint8_t *stream,
	size_t bytes,
	size_t offset,
	size_t *start,
	size_t *end)
{
	size_t index;

	/* Finds the next three-byte start code. */
	index = offset;
	while (index + 3U <= bytes) {
		if (stream[index] == 0 && stream[index + 1U] == 0 && stream[index + 2U] == 1)
			break;
		index++;
	}

	/* Reports a stream with no more NAL units. */
	if (index + 3U > bytes)
		return ENOENT;
	*start = index + 3U;

	/* The unit runs to the next start code (a four-byte one keeps its leading zero out). */
	index = *start;
	while (index + 3U <= bytes) {
		if (stream[index] == 0 && stream[index + 1U] == 0 && stream[index + 2U] == 1)
			break;
		index++;
	}
	if (index + 3U > bytes) {
		index = bytes;
	} else if (index > *start && stream[index - 1U] == 0) {
		index--;
	}
	*end = index;

	/* Succeeded: the unit's bytes are [start, end). */
	return 0;
}

/*
 * Finds the end of the access unit that starts at offset.
 */
int
drv_rpi4_h264_access_unit(
	const uint8_t *stream,
	size_t bytes,
	size_t offset,
	size_t *end)
{
	size_t start;
	size_t stop;
	size_t cursor;
	int seen_slice;
	int error;
	int begins;

	/* Walks the units until one starts the next access unit. */
	cursor = offset;
	seen_slice = 0;
	for (;;) {
		error = drv_rpi4_h264_next_nal(stream, bytes, cursor, &start, &stop);
		if (error != 0)
			break;

		/* The unit that begins the next access unit ends this one at its start code. */
		begins = starts_new_unit(stream, start, stop, seen_slice);
		if (begins) {
			*end = start - 3U;
			if (*end > cursor && stream[*end - 1U] == 0)
				(*end)--;
			return 0;
		}

		/* A slice makes the next first slice or parameter set a boundary. */
		if (start < stop && is_slice(stream[start]))
			seen_slice = 1;
		cursor = stop;
	}

	/* Reports a stream without any unit after offset. */
	if (cursor == offset)
		return ENOENT;

	/* Succeeded: the last access unit runs to the stream's end. */
	*end = bytes;
	return 0;
}

/*
 * Rewrites one SPS NAL unit with VUI that asks for no reordering.
 */
int
drv_rpi4_h264_sps_no_reorder(
	const uint8_t *nal,
	size_t bytes,
	uint8_t *output,
	size_t capacity,
	size_t *written)
{
	uint8_t rbsp[H264_RBSP_MAX];
	uint8_t rewritten[H264_RBSP_MAX];
	struct h264_reader reader;
	struct h264_writer writer;
	uint32_t profile;
	uint32_t chroma;
	uint32_t lists;
	uint32_t list;
	uint32_t present;
	uint32_t poc_type;
	uint32_t cycle;
	uint32_t index;
	uint32_t references;
	uint32_t frame_mbs_only;
	uint32_t cropping;
	size_t rbsp_bytes;
	size_t prefix_bits;
	int error;

	/* Requires an SPS NAL unit with a body. */
	if (bytes < 4U || (nal[0] & H264_NAL_TYPE_MASK) != DRV_RPI4_H264_NAL_SPS)
		return EINVAL;

	/* Removes the emulation prevention bytes of the body. */
	rbsp_bytes = unescape(nal + 1, bytes - 1U, rbsp, sizeof(rbsp));
	if (rbsp_bytes == 0)
		return EINVAL;
	reader.data = rbsp;
	reader.bits = rbsp_bytes * 8U;
	reader.position = 0;
	reader.error = 0;

	/* profile_idc, the constraint flags, level_idc and seq_parameter_set_id. */
	profile = read_bits(&reader, 8);
	(void)read_bits(&reader, 8);
	(void)read_bits(&reader, 8);
	(void)read_ue(&reader);

	/* The high profiles' chroma format, bit depths and scaling matrix. */
	chroma = 1;
	if (high_profile(profile)) {
		chroma = read_ue(&reader);
		if (chroma == 3U)
			(void)read_bits(&reader, 1);
		(void)read_ue(&reader);
		(void)read_ue(&reader);
		(void)read_bits(&reader, 1);
		present = read_bits(&reader, 1);
		if (present != 0) {
			lists = 8U;
			if (chroma == 3U)
				lists = 12U;
			for (list = 0; list < lists; list++) {
				if (read_bits(&reader, 1) == 0)
					continue;
				if (list < 6U) {
					skip_scaling_list(&reader, 16);
				} else {
					skip_scaling_list(&reader, 64);
				}
			}
		}
	}

	/* The frame number and picture order count syntax. */
	(void)read_ue(&reader);
	poc_type = read_ue(&reader);
	if (poc_type == 0) {
		(void)read_ue(&reader);
	} else if (poc_type == 1U) {
		(void)read_bits(&reader, 1);
		(void)read_se(&reader);
		(void)read_se(&reader);
		cycle = read_ue(&reader);
		if (cycle > 255U)
			return EINVAL;
		for (index = 0; index < cycle; index++)
			(void)read_se(&reader);
	}

	/* The reference frames, the picture size, the field flags and the cropping. */
	references = read_ue(&reader);
	(void)read_bits(&reader, 1);
	(void)read_ue(&reader);
	(void)read_ue(&reader);
	frame_mbs_only = read_bits(&reader, 1);
	if (frame_mbs_only == 0)
		(void)read_bits(&reader, 1);
	(void)read_bits(&reader, 1);
	cropping = read_bits(&reader, 1);
	if (cropping != 0) {
		(void)read_ue(&reader);
		(void)read_ue(&reader);
		(void)read_ue(&reader);
		(void)read_ue(&reader);
	}

	/* Refuses an SPS shorter than its own syntax. */
	if (reader.error != 0)
		return EINVAL;
	prefix_bits = reader.position;

	/* Copies every bit before vui_parameters_present_flag. */
	kern_memset(rewritten, 0, sizeof(rewritten));
	writer.data = rewritten;
	writer.capacity_bits = sizeof(rewritten) * 8U;
	writer.position = 0;
	writer.error = 0;
	reader.position = 0;
	while (reader.position < prefix_bits)
		write_bits(&writer, read_bits(&reader, 1), 1);

	/* VUI present, with nothing but the bitstream restriction, then the trailing bits. */
	write_vui_no_reorder(&writer, references);
	write_trailing(&writer);
	if (writer.error != 0)
		return ENOSPC;

	/* The start code, the NAL header byte and the escaped body. */
	error = pack_nal(nal[0], rewritten, writer.position / 8U, output, capacity, written);
	if (error != 0)
		return error;

	/* Succeeded: output holds the rewritten unit. */
	return 0;
}

/* Reports whether a NAL unit header byte is a slice's. */
static int
is_slice(
	uint8_t type)
{
	uint8_t kind;

	/* Coded slices of non-IDR and IDR pictures. */
	kind = type & H264_NAL_TYPE_MASK;
	if (kind == DRV_RPI4_H264_NAL_SLICE)
		return 1;
	if (kind == DRV_RPI4_H264_NAL_IDR)
		return 1;

	/* Succeeded: not a slice. */
	return 0;
}

/* Reports whether a NAL unit begins a new access unit after the units seen so far. */
static int
starts_new_unit(
	const uint8_t *stream,
	size_t start,
	size_t end,
	int seen_slice)
{
	uint8_t kind;

	/* Nothing before the first slice ends the unit. */
	if (!seen_slice || start >= end)
		return 0;
	kind = stream[start] & H264_NAL_TYPE_MASK;

	/* A delimiter, a parameter set or SEI after a slice starts the next unit. */
	if (kind == DRV_RPI4_H264_NAL_AUD || kind == DRV_RPI4_H264_NAL_SPS ||
	    kind == DRV_RPI4_H264_NAL_PPS || kind == DRV_RPI4_H264_NAL_SEI)
		return 1;

	/* A slice whose first_mb_in_slice is zero (a leading one bit) starts the next picture. */
	if (is_slice(stream[start]) && start + 1U < end && (stream[start + 1U] & 0x80U) != 0)
		return 1;

	/* Succeeded: the unit belongs to the current access unit. */
	return 0;
}

/* Removes emulation prevention bytes; reports the RBSP's length, or zero when it does not fit. */
static size_t
unescape(
	const uint8_t *nal,
	size_t bytes,
	uint8_t *rbsp,
	size_t capacity)
{
	size_t index;
	size_t out;
	size_t zeros;

	/* Copies every byte but a 3 that follows two zeros. */
	out = 0;
	zeros = 0;
	for (index = 0; index < bytes; index++) {
		if (zeros >= 2U && nal[index] == 3U) {
			zeros = 0;
			continue;
		}
		if (out >= capacity)
			return 0;
		rbsp[out] = nal[index];
		out++;
		if (nal[index] == 0) {
			zeros++;
		} else {
			zeros = 0;
		}
	}

	/* Succeeded: the RBSP's length. */
	return out;
}

/* Reads up to 32 bits. */
static uint32_t
read_bits(
	struct h264_reader *reader,
	unsigned count)
{
	uint32_t value;
	unsigned index;
	uint8_t byte;

	/* A read past the end sets the error and gives zero. */
	if (reader->error != 0 || reader->position + count > reader->bits) {
		reader->error = 1;
		return 0;
	}

	/* Reads one bit at a time, most significant first. */
	value = 0;
	for (index = 0; index < count; index++) {
		byte = reader->data[reader->position / 8U];
		value = (value << 1) | (uint32_t)((byte >> (7U - reader->position % 8U)) & 1U);
		reader->position++;
	}

	/* Succeeded: the bits as a number. */
	return value;
}

/* Reads an unsigned Exp-Golomb code. */
static uint32_t
read_ue(
	struct h264_reader *reader)
{
	unsigned zeros;
	uint32_t suffix;

	/* Counts the leading zeros before the one. */
	zeros = 0;
	while (read_bits(reader, 1) == 0) {
		if (reader->error != 0 || zeros >= 31U) {
			reader->error = 1;
			return 0;
		}
		zeros++;
	}

	/* The value is 2^zeros - 1 plus the suffix. */
	suffix = read_bits(reader, zeros);
	return ((uint32_t)1U << zeros) - 1U + suffix;
}

/* Reads a signed Exp-Golomb code. */
static int32_t
read_se(
	struct h264_reader *reader)
{
	uint32_t code;

	/* Odd codes are positive, even ones negative. */
	code = read_ue(reader);
	if ((code & 1U) != 0)
		return (int32_t)((code + 1U) / 2U);

	/* Succeeded: a negative or zero value. */
	return -(int32_t)(code / 2U);
}

/* Moves past one scaling list of a given size. */
static void
skip_scaling_list(
	struct h264_reader *reader,
	unsigned size)
{
	int32_t last;
	int32_t next;
	unsigned index;

	/* delta_scale values until the list repeats its last scale. */
	last = 8;
	next = 8;
	for (index = 0; index < size; index++) {
		if (next != 0) {
			next = (last + read_se(reader) + 256) % 256;
		}
		if (next != 0)
			last = next;
		if (reader->error != 0)
			return;
	}
}

/* Writes up to 32 bits. */
static void
write_bits(
	struct h264_writer *writer,
	uint32_t value,
	unsigned count)
{
	unsigned index;
	uint32_t bit;

	/* A write past the capacity sets the error and writes nothing. */
	if (writer->error != 0 || writer->position + count > writer->capacity_bits) {
		writer->error = 1;
		return;
	}

	/* Writes one bit at a time, most significant first, into the zeroed buffer. */
	for (index = 0; index < count; index++) {
		bit = (value >> (count - 1U - index)) & 1U;
		if (bit != 0)
			writer->data[writer->position / 8U] |= (uint8_t)(0x80U >> (writer->position % 8U));
		writer->position++;
	}
}

/* Writes an unsigned Exp-Golomb code. */
static void
write_ue(
	struct h264_writer *writer,
	uint32_t value)
{
	uint32_t code;
	unsigned length;

	/* value + 1 in binary, after as many zeros as it has bits less one. */
	code = value + 1U;
	length = 0;
	while ((code >> length) > 1U)
		length++;
	write_bits(writer, 0, length);
	write_bits(writer, code, length + 1U);
}

/* Reports whether a profile carries the chroma format and scaling matrix fields. */
static int
high_profile(
	uint32_t profile)
{
	/* The profiles 7.3.2.1.1 lists. */
	switch (profile) {
	case 100:
	case 110:
	case 122:
	case 244:
	case 44:
	case 83:
	case 86:
	case 118:
	case 128:
	case 138:
	case 139:
	case 134:
	case 135:
		return 1;
	default:
		return 0;
	}
}

/* Writes a signed Exp-Golomb code. */
static void
write_se(
	struct h264_writer *writer,
	int32_t value)
{
	/* Positive values take the odd codes, the others the even ones. */
	if (value > 0) {
		write_ue(writer, (uint32_t)value * 2U - 1U);
	} else {
		write_ue(writer, (uint32_t)(-(int64_t)value) * 2U);
	}
}

/*
 * Writes vui_parameters_present_flag and VUI whose only content is the
 * bitstream restriction asking for no reordering.
 */
static void
write_vui_no_reorder(
	struct h264_writer *writer,
	uint32_t references)
{
	/* VUI present; no aspect, overscan, signal, chroma location, timing, HRD or picture structure. */
	write_bits(writer, 1, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);
	write_bits(writer, 0, 1);

	/* The restriction: no size limits, the widest motion, no reordering. */
	write_bits(writer, 1, 1);
	write_bits(writer, 1, 1);
	write_ue(writer, 0);
	write_ue(writer, 0);
	write_ue(writer, H264_MAX_MV_LENGTH);
	write_ue(writer, H264_MAX_MV_LENGTH);
	write_ue(writer, 0);
	if (references == 0)
		references = 1;
	write_ue(writer, references);
}

/* Writes rbsp_trailing_bits: a one, then zeros to the byte boundary. */
static void
write_trailing(
	struct h264_writer *writer)
{
	/* The stop bit and the alignment. */
	write_bits(writer, 1, 1);
	while ((writer->position % 8U) != 0 && writer->error == 0)
		write_bits(writer, 0, 1);
}

/*
 * Writes the scaling lists of a set: a present flag per list, and for a
 * present one either the default marker or every delta.
 */
static void
write_scaling(
	struct h264_writer *writer,
	const struct drv_rpi4_h264_scaling *scaling,
	uint32_t lists)
{
	const uint8_t *list;
	uint32_t index;
	uint32_t size;
	uint32_t entry;
	int32_t last;
	int32_t delta;

	/* Each list in coding order: the six 4x4 lists, then the 8x8 ones. */
	for (index = 0; index < lists; index++) {
		if ((scaling->present_mask & (1U << index)) == 0) {
			write_bits(writer, 0, 1);
			continue;
		}
		write_bits(writer, 1, 1);

		/* A default list is the delta that makes the first scale zero. */
		if ((scaling->default_mask & (1U << index)) != 0) {
			write_se(writer, -8);
			continue;
		}

		/* An explicit list: each coefficient's delta from the one before, wrapped to -128..127. */
		if (index < 6U) {
			list = scaling->list4[index];
			size = 16U;
		} else {
			list = scaling->list8[index - 6U];
			size = 64U;
		}
		last = 8;
		for (entry = 0; entry < size; entry++) {
			delta = ((int32_t)list[entry] - last + 256) % 256;
			if (delta > 127)
				delta -= 256;
			write_se(writer, delta);
			last = list[entry];
		}
	}
}

/* Converts StdVideoH264LevelIdc to level_idc. */
static uint32_t
level_number(
	uint32_t level)
{
	/* The enum counts the levels 1.0 to 6.2 in order. */
	static const uint8_t numbers[19] = {
		10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62
	};

	/* Succeeded: the level's number. */
	return numbers[level];
}

/*
 * Writes a start code, a NAL header byte and an RBSP with emulation
 * prevention: a 0x03 goes in after any two zeros that precede a byte of
 * three or less.
 */
static int
pack_nal(
	uint8_t header,
	const uint8_t *rbsp,
	size_t bytes,
	uint8_t *output,
	size_t capacity,
	size_t *written)
{
	size_t index;
	size_t zeros;
	size_t out;

	/* The four-byte start code and the header. */
	if (capacity < 5U)
		return ENOSPC;
	output[0] = 0;
	output[1] = 0;
	output[2] = 0;
	output[3] = 1;
	output[4] = header;
	out = 5U;

	/* The body, escaped. */
	zeros = 0;
	for (index = 0; index < bytes; index++) {
		if (zeros >= 2U && rbsp[index] <= 3U) {
			if (out >= capacity)
				return ENOSPC;
			output[out] = 3;
			out++;
			zeros = 0;
		}
		if (out >= capacity)
			return ENOSPC;
		output[out] = rbsp[index];
		out++;
		if (rbsp[index] == 0) {
			zeros++;
		} else {
			zeros = 0;
		}
	}

	/* Succeeded: output holds the unit. */
	*written = out;
	return 0;
}
