/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's H.264 decode: the parameter sets as the executor keeps
 * them, and the arithmetic that turns a decode into the video decoder's
 * picture description (vk-video-h264.c, which touches no hardware: a host
 * test runs it).  The wire, the objects and the run are vk-video.c's.
 */

#ifndef DRIVERS_GPU_NVRTX_VK_VIDEO_H
#define DRIVERS_GPU_NVRTX_VK_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-nvdec.h"

/* The DPB slots a session has at most, and the references one decode names at most. */
#define NVRTX_VIDEO_SLOTS		17U
#define NVRTX_VIDEO_REFERENCES		16U

/*
 * The flags of the parameter sets and of a picture as libvulkan packs
 * them (userland/desktop/libvulkan/video.c: video_sps_flags,
 * video_pps_flags, video_picture_flags, video_reference_flags), each the
 * declared order of its StdVideo structure from bit 0.
 */
#define NVRTX_VIDEO_SPS_DIRECT_8X8	(1U << 6)
#define NVRTX_VIDEO_SPS_MBAFF		(1U << 7)
#define NVRTX_VIDEO_SPS_FRAME_MBS_ONLY	(1U << 8)
#define NVRTX_VIDEO_SPS_DELTA_POC_ZERO	(1U << 9)
#define NVRTX_VIDEO_SPS_SEPARATE_PLANES	(1U << 10)
#define NVRTX_VIDEO_SPS_BYPASS		(1U << 12)
#define NVRTX_VIDEO_SPS_SCALING		(1U << 14)
#define NVRTX_VIDEO_PPS_TRANSFORM_8X8	(1U << 0)
#define NVRTX_VIDEO_PPS_REDUNDANT	(1U << 1)
#define NVRTX_VIDEO_PPS_CONSTRAINED	(1U << 2)
#define NVRTX_VIDEO_PPS_DEBLOCKING	(1U << 3)
#define NVRTX_VIDEO_PPS_WEIGHTED	(1U << 4)
#define NVRTX_VIDEO_PPS_BOTTOM_POC	(1U << 5)
#define NVRTX_VIDEO_PPS_ENTROPY		(1U << 6)
#define NVRTX_VIDEO_PPS_SCALING		(1U << 7)
#define NVRTX_VIDEO_PICTURE_FIELD	(1U << 0)
#define NVRTX_VIDEO_PICTURE_REFERENCE	(1U << 4)
#define NVRTX_VIDEO_PICTURE_PAIR	(1U << 5)
#define NVRTX_VIDEO_REFERENCE_LONG_TERM	(1U << 2)
#define NVRTX_VIDEO_REFERENCE_MISSING	(1U << 3)

/*
 * The scaling lists of a parameter set as sent: which lists are present
 * and which of those take the default matrix, and the lists in their scan
 * (zig-zag) order, six 4x4 lists and six 8x8 lists.
 */
struct nvrtx_video_scaling {
	uint32_t present_mask;
	uint32_t default_mask;
	uint8_t list4[6][16];
	uint8_t list8[6][64];
};

/* One H.264 sequence parameter set: the fields the decode reads, and its own scaling lists if any. */
struct nvrtx_video_sps {
	uint32_t flags;
	uint32_t profile_idc;
	uint32_t chroma_format_idc;
	uint32_t id;
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
	uint32_t log2_max_frame_num_minus4;
	uint32_t pic_order_cnt_type;
	uint32_t log2_max_pic_order_cnt_lsb_minus4;
	uint32_t max_num_ref_frames;
	uint32_t pic_width_in_mbs_minus1;
	uint32_t pic_height_in_map_units_minus1;
	bool has_scaling;
	struct nvrtx_video_scaling scaling;
};

/* One H.264 picture parameter set: the fields the decode reads, and its own scaling lists if any. */
struct nvrtx_video_pps {
	uint32_t flags;
	uint32_t sps_id;
	uint32_t pps_id;
	uint32_t num_ref_idx_l0_default_active_minus1;
	uint32_t num_ref_idx_l1_default_active_minus1;
	uint32_t weighted_bipred_idc;
	int32_t pic_init_qp_minus26;
	int32_t chroma_qp_index_offset;
	int32_t second_chroma_qp_index_offset;
	bool has_scaling;
	struct nvrtx_video_scaling scaling;
};

/*
 * The H.264 information of the picture a decode writes: its flags, the
 * sets it names, its frame number and its two picture order counts.
 */
struct nvrtx_video_picture {
	uint32_t flags;
	uint32_t sps_id;
	uint32_t pps_id;
	uint32_t frame_num;
	int32_t poc[2];
};

/*
 * The H.264 information of one reference picture: its flags, its frame
 * number (or long-term frame index) and its picture order counts.
 */
struct nvrtx_video_reference {
	uint32_t flags;
	uint32_t frame_num;
	int32_t poc[2];
};

/*
 * Which of the decoder's reference entries each DPB slot's picture has:
 * entry[n] for slot n, or -1 for none.  An entry stays with its picture
 * for as long as the picture stays a reference, as the decoder expects.
 */
struct nvrtx_video_dpb {
	int32_t entry[NVRTX_VIDEO_SLOTS];
};

void nvrtx_video_scaling_resolve(const struct nvrtx_video_sps *sps, const struct nvrtx_video_pps *pps, uint8_t scaling_4x4[6][16], uint8_t scaling_8x8[2][64]);
void nvrtx_video_dpb_reset(struct nvrtx_video_dpb *dpb);
void nvrtx_video_dpb_keep(struct nvrtx_video_dpb *dpb, const bool *active);
void nvrtx_video_dpb_forget(struct nvrtx_video_dpb *dpb, uint32_t slot);
int nvrtx_video_dpb_entry(struct nvrtx_video_dpb *dpb, uint32_t slot, uint32_t *entry);
uint32_t nvrtx_video_free_surface(const struct nvrtx_video_dpb *dpb, const uint32_t *referenced, uint32_t count);
void nvrtx_video_describe(const struct nvrtx_video_sps *sps, const struct nvrtx_video_pps *pps, const struct nvrtx_video_picture *picture, struct nvrtx_nvdec_h264_picture *description);
int32_t nvrtx_video_order(int32_t own, int32_t other);

/* The NV12 layout of a video image: its rows' bytes, and each plane's offset and rows. */
struct nvrtx_video_layout {
	uint32_t pitch;
	uint64_t plane_offset[2];
	uint32_t plane_rows[2];
	uint64_t bytes;
};

void nvrtx_video_layout(uint32_t width, uint32_t height, struct nvrtx_video_layout *layout);

#endif
