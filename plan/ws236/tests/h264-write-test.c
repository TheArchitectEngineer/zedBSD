/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p004: the SPS/PPS writers of rpi4-h264.c against libmedia's parser
 * on the host.  "rewrite FILE OUT REORDER0" parses FILE's parameter sets
 * with libmedia (as the Vulkan Video path receives them, StdVideo), writes
 * them back with drv_rpi4_h264_write_sps/pps and replaces every SPS and PPS
 * NAL unit of FILE with the written one.
 */

#include "drivers/platform/rpi4/rpi4-h264.h"
#include "h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_memset(void *destination, int value, size_t bytes) { return memset(destination, value, bytes); }

static void scaling_from(const StdVideoH264ScalingLists *lists, struct drv_rpi4_h264_scaling *scaling)
{
	scaling->present_mask = lists->scaling_list_present_mask;
	scaling->default_mask = lists->use_default_scaling_matrix_mask;
	memcpy(scaling->list4, lists->ScalingList4x4, sizeof(scaling->list4));
	memcpy(scaling->list8, lists->ScalingList8x8, sizeof(scaling->list8));
}

static void sps_from(const StdVideoH264SequenceParameterSet *in, struct drv_rpi4_h264_sps *out)
{
	uint32_t i;

	memset(out, 0, sizeof(*out));
	out->flags |= in->flags.constraint_set0_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET0 : 0;
	out->flags |= in->flags.constraint_set1_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET1 : 0;
	out->flags |= in->flags.constraint_set2_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET2 : 0;
	out->flags |= in->flags.constraint_set3_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET3 : 0;
	out->flags |= in->flags.constraint_set4_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET4 : 0;
	out->flags |= in->flags.constraint_set5_flag ? DRV_RPI4_H264_SPS_CONSTRAINT_SET5 : 0;
	out->flags |= in->flags.direct_8x8_inference_flag ? DRV_RPI4_H264_SPS_DIRECT_8X8 : 0;
	out->flags |= in->flags.mb_adaptive_frame_field_flag ? DRV_RPI4_H264_SPS_MB_ADAPTIVE : 0;
	out->flags |= in->flags.frame_mbs_only_flag ? DRV_RPI4_H264_SPS_FRAME_MBS_ONLY : 0;
	out->flags |= in->flags.delta_pic_order_always_zero_flag ? DRV_RPI4_H264_SPS_DELTA_POC_ZERO : 0;
	out->flags |= in->flags.separate_colour_plane_flag ? DRV_RPI4_H264_SPS_SEPARATE_PLANES : 0;
	out->flags |= in->flags.gaps_in_frame_num_value_allowed_flag ? DRV_RPI4_H264_SPS_GAPS_ALLOWED : 0;
	out->flags |= in->flags.qpprime_y_zero_transform_bypass_flag ? DRV_RPI4_H264_SPS_QPPRIME_BYPASS : 0;
	out->flags |= in->flags.frame_cropping_flag ? DRV_RPI4_H264_SPS_CROPPING : 0;
	out->flags |= in->flags.seq_scaling_matrix_present_flag ? DRV_RPI4_H264_SPS_SCALING : 0;
	out->profile_idc = in->profile_idc;
	out->level = in->level_idc;
	out->chroma_format_idc = in->chroma_format_idc;
	out->id = in->seq_parameter_set_id;
	out->bit_depth_luma_minus8 = in->bit_depth_luma_minus8;
	out->bit_depth_chroma_minus8 = in->bit_depth_chroma_minus8;
	out->log2_max_frame_num_minus4 = in->log2_max_frame_num_minus4;
	out->pic_order_cnt_type = in->pic_order_cnt_type;
	out->offset_for_non_ref_pic = in->offset_for_non_ref_pic;
	out->offset_for_top_to_bottom_field = in->offset_for_top_to_bottom_field;
	out->log2_max_pic_order_cnt_lsb_minus4 = in->log2_max_pic_order_cnt_lsb_minus4;
	out->max_num_ref_frames = in->max_num_ref_frames;
	out->pic_width_in_mbs_minus1 = in->pic_width_in_mbs_minus1;
	out->pic_height_in_map_units_minus1 = in->pic_height_in_map_units_minus1;
	out->crop[0] = in->frame_crop_left_offset;
	out->crop[1] = in->frame_crop_right_offset;
	out->crop[2] = in->frame_crop_top_offset;
	out->crop[3] = in->frame_crop_bottom_offset;
	out->offset_count = in->num_ref_frames_in_pic_order_cnt_cycle;
	for (i = 0; i < out->offset_count; i++)
		out->offsets[i] = in->pOffsetForRefFrame[i];
	if (in->pScalingLists != NULL) {
		out->has_scaling = 1;
		scaling_from(in->pScalingLists, &out->scaling);
	}
}

static void pps_from(const StdVideoH264PictureParameterSet *in, struct drv_rpi4_h264_pps *out)
{
	memset(out, 0, sizeof(*out));
	out->flags |= in->flags.transform_8x8_mode_flag ? DRV_RPI4_H264_PPS_TRANSFORM_8X8 : 0;
	out->flags |= in->flags.redundant_pic_cnt_present_flag ? DRV_RPI4_H264_PPS_REDUNDANT_PIC_CNT : 0;
	out->flags |= in->flags.constrained_intra_pred_flag ? DRV_RPI4_H264_PPS_CONSTRAINED_INTRA : 0;
	out->flags |= in->flags.deblocking_filter_control_present_flag ? DRV_RPI4_H264_PPS_DEBLOCKING_CONTROL : 0;
	out->flags |= in->flags.weighted_pred_flag ? DRV_RPI4_H264_PPS_WEIGHTED_PRED : 0;
	out->flags |= in->flags.bottom_field_pic_order_in_frame_present_flag ? DRV_RPI4_H264_PPS_BOTTOM_FIELD_POC : 0;
	out->flags |= in->flags.entropy_coding_mode_flag ? DRV_RPI4_H264_PPS_CABAC : 0;
	out->flags |= in->flags.pic_scaling_matrix_present_flag ? DRV_RPI4_H264_PPS_SCALING : 0;
	out->sps_id = in->seq_parameter_set_id;
	out->pps_id = in->pic_parameter_set_id;
	out->num_ref_idx_l0_default_active_minus1 = in->num_ref_idx_l0_default_active_minus1;
	out->num_ref_idx_l1_default_active_minus1 = in->num_ref_idx_l1_default_active_minus1;
	out->weighted_bipred_idc = in->weighted_bipred_idc;
	out->pic_init_qp_minus26 = in->pic_init_qp_minus26;
	out->pic_init_qs_minus26 = in->pic_init_qs_minus26;
	out->chroma_qp_index_offset = in->chroma_qp_index_offset;
	out->second_chroma_qp_index_offset = in->second_chroma_qp_index_offset;
	if (in->pScalingLists != NULL) {
		out->has_scaling = 1;
		scaling_from(in->pScalingLists, &out->scaling);
	}
}

int main(int argc, char **argv)
{
	static struct h264_stream stream;
	static struct h264_picture picture;
	struct drv_rpi4_h264_sps sps;
	struct drv_rpi4_h264_pps pps;
	uint8_t unit[4096];
	uint8_t *data;
	size_t bytes;
	size_t written;
	size_t offset;
	size_t start;
	size_t stop;
	const char *reason;
	FILE *file;
	long size;
	int reorder0;
	int error;
	uint32_t id;

	if (argc != 5 || strcmp(argv[1], "rewrite") != 0)
		return 2;
	reorder0 = atoi(argv[4]);
	file = fopen(argv[2], "rb");
	if (file == NULL)
		return 2;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (fread(data, 1, (size_t)size, file) != (size_t)size)
		return 2;
	fclose(file);
	bytes = (size_t)size;

	/* libmedia parses every set (the pictures advance it through the stream). */
	if (h264_open(&stream, data, bytes) != 0)
		return 3;
	while (h264_next_picture(&stream, &picture, &reason) == 0)
		;

	/* Each SPS and PPS unit is replaced by the one written from its parsed fields. */
	file = fopen(argv[3], "wb");
	offset = 0;
	while (drv_rpi4_h264_next_nal(data, bytes, offset, &start, &stop) == 0) {
		if ((data[start] & 0x1f) == 7) {
			id = 0;
			for (id = 0; id < H264_SPS_IDS && !stream.has_sps[id]; id++)
				;
			sps_from(&stream.sps[id], &sps);
			error = drv_rpi4_h264_write_sps(&sps, reorder0, unit, sizeof(unit), &written);
			if (error != 0) {
				printf("sps write failed %d\n", error);
				return 1;
			}
			fwrite(unit, 1, written, file);
		} else if ((data[start] & 0x1f) == 8) {
			for (id = 0; id < H264_PPS_IDS && !stream.has_pps[id]; id++)
				;
			pps_from(&stream.pps[id], &pps);
			sps_from(&stream.sps[stream.pps[id].seq_parameter_set_id], &sps);
			error = drv_rpi4_h264_write_pps(&pps, &sps, unit, sizeof(unit), &written);
			if (error != 0) {
				printf("pps write failed %d\n", error);
				return 1;
			}
			fwrite(unit, 1, written, file);
		} else {
			fwrite("\0\0\0\1", 1, 4, file);
			fwrite(data + start, 1, stop - start, file);
		}
		offset = stop;
	}
	fclose(file);
	return 0;
}
