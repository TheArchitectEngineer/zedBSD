/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The video decoder (NVDEC) of the nvrtx driver.
 *
 * The decoder runs on a channel of its own (nvdec.c) in the work channel's
 * address space, so the memory the executor's users bind is at the same
 * GPU address for the decoder as for the graphics engine.  The firmware the
 * decoder runs is the GSP's: the driver only has the resource manager make
 * the channel, its engine context and the decoder's object.
 *
 * The decoder is stateless.  Each picture is one push buffer that names the
 * picture's parameters (a structure in memory, filled by
 * nvrtx_nvdec_h264_pic()), the bitstream and its slice offsets, the
 * session's three work buffers, and the luma and chroma planes of up to
 * seventeen surfaces: the picture being decoded and the pictures it
 * refers to.  The parts here touch no hardware (a host test runs them).
 */

#ifndef DRIVERS_GPU_NVRTX_NVDEC_H
#define DRIVERS_GPU_NVRTX_NVDEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

struct nvrtx_gpu;

/*
 * The decoder's classes: Turing's (TU10x and TU11x), Ampere's GA10x and
 * Ada's.  Their methods and the picture structure are the same.
 *
 * Values referenced: NVIDIA open-gpu-kernel-modules 570.144
 * src/nvidia/generated/g_gpu_class_list.c (the NVDEC classes of each
 * chip) and src/common/sdk/nvidia/inc/class/clc4b0.h
 * (NVC4B0_VIDEO_DECODER).
 */
#define NVRTX_NVDEC_CLASS_TURING	0xc4b0U
#define NVRTX_NVDEC_CLASS_AMPERE	0xc7b0U
#define NVRTX_NVDEC_CLASS_ADA		0xc9b0U

/*
 * The resource manager's engine type of the first decoder (the "BSP").
 *
 * Value referenced: NVIDIA open-gpu-kernel-modules 570.144
 * src/common/sdk/nvidia/inc/class/cl2080_notification.h
 * (NV2080_ENGINE_TYPE_NVDEC0).
 */
#define NVRTX_ENGINE_NVDEC0		0x13U

/* The subchannel the decoder's class is bound to on its channel. */
#define NVRTX_NVDEC_SUBCHANNEL		4U

/*
 * The surfaces one picture names at most (sixteen references and the
 * picture itself), and the reference entries of the picture structure.
 *
 * Values referenced: NVIDIA open-gpu-doc classes/video/clc5b0.h
 * (NVC5B0_SET_PICTURE_LUMA_OFFSET0 to 16) and classes/video/nvdec_drv.h
 * (nvdec_h264_pic_s.dpb).
 */
#define NVRTX_NVDEC_SURFACES		17U
#define NVRTX_NVDEC_REFERENCES		16U

/*
 * The bytes of the H.264 picture structure, of the decoder's status, and
 * the words of the slice offset table (one for each slice and one after
 * the last).
 *
 * Values referenced: NVIDIA open-gpu-doc classes/video/nvdec_drv.h
 * (nvdec_h264_pic_s, 764 bytes; nvdec_status_s, 56 bytes), and Mesa
 * src/nouveau/vulkan/rust/video/decode/h264.rs (the 256-word slice
 * offset table).
 */
#define NVRTX_NVDEC_PIC_BYTES		764U
#define NVRTX_NVDEC_STATUS_BYTES	56U
#define NVRTX_NVDEC_SLICE_WORDS		256U

/* The most slices one picture has (one word of the table is the end). */
#define NVRTX_NVDEC_SLICES_MAX		(NVRTX_NVDEC_SLICE_WORDS - 1U)

/*
 * The decoder reads every address it is given as the GPU address shifted
 * right by 8: the buffers and planes it names lie on 256-byte boundaries.
 */
#define NVRTX_NVDEC_ADDRESS_ALIGN	256U

/* The most bytes the push buffer of one picture takes (nvrtx_nvdec_push_decode). */
#define NVRTX_NVDEC_DECODE_PUSH_BYTES	256U

/* The status word of one decode's status: the macroblocks it found in error. */
#define NVRTX_NVDEC_STATUS_ERRORS_AT	4U

/*
 * The resource manager's parameters the decoder's channel needs: the
 * decoder's object, the promote of its engine context and the list of the
 * falcons' context sizes.
 *
 * Values referenced: NVIDIA open-gpu-kernel-modules 570.144
 * src/common/sdk/nvidia/inc/nvos.h (NV_BSP_ALLOCATION_PARAMETERS) and
 * src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h
 * (NV2080_CTRL_CMD_GPU_PROMOTE_CTX, NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS,
 * NV2080_CTRL_CMD_GPU_GET_CONSTRUCTED_FALCON_INFO,
 * NV2080_CTRL_GPU_GET_CONSTRUCTED_FALCON_INFO_PARAMS).
 */
#define NVRTX_NVDEC_ALLOC_PARAMS_BYTES	12U
#define NVRTX_NVDEC_PROMOTE_BYTES	560U
#define NVRTX_CTL_FALCON_INFO		0x208001b0U
#define NVRTX_FALCON_INFO_PARAMS_BYTES	1284U

/*
 * Where the decoder finds one picture's inputs and outputs: GPU addresses
 * in the decoder's space, each on a 256-byte boundary (0 for a surface the
 * picture does not name).  serial numbers the picture in the session;
 * error_frame is the surface the decoder conceals errors from.
 */
struct nvrtx_nvdec_frame {
	uint64_t pic;
	uint64_t bitstream;
	uint64_t slices;
	uint64_t coloc;
	uint64_t history;
	uint64_t status;
	uint64_t mbhist;
	uint64_t luma[NVRTX_NVDEC_SURFACES];
	uint64_t chroma[NVRTX_NVDEC_SURFACES];
	uint32_t serial;
	uint32_t error_frame;
};

/*
 * One picture a decode refers to, as the picture structure lists it: its
 * surface, whether it is a long-term reference, whether it is a frame the
 * bitstream skipped (non-existing), its two field order counts and its
 * frame number (or long-term frame index).
 */
struct nvrtx_nvdec_h264_reference {
	uint32_t surface;
	uint32_t long_term;
	uint32_t non_existing;
	int32_t field_order[2];
	uint32_t frame_index;
};

/*
 * One progressive H.264 picture as the decoder takes it: the fields of its
 * sequence and picture parameter sets, of the picture itself, its
 * references (each in the entry dpb_entry[n] names) and its scaling
 * matrices in raster order.  The sizes are those of the session's buffers
 * and of the planes' rows.
 */
struct nvrtx_nvdec_h264_picture {
	/* From the sequence parameter set. */
	uint32_t log2_max_poc_lsb_minus4;
	uint32_t delta_poc_always_zero;
	uint32_t frame_mbs_only;
	uint32_t width_mbs;
	uint32_t height_mbs;
	uint32_t direct_8x8_inference;
	uint32_t log2_max_frame_num_minus4;
	uint32_t chroma_format_idc;
	uint32_t poc_type;
	uint32_t transform_bypass;
	/* From the picture parameter set. */
	uint32_t entropy_coding;
	uint32_t bottom_field_poc_present;
	uint32_t ref_l0_minus1;
	uint32_t ref_l1_minus1;
	uint32_t deblocking_control_present;
	uint32_t redundant_pic_cnt_present;
	uint32_t transform_8x8;
	uint32_t weighted_pred;
	uint32_t weighted_bipred_idc;
	uint32_t constrained_intra_pred;
	int32_t pic_init_qp_minus26;
	int32_t chroma_qp_offset;
	int32_t second_chroma_qp_offset;
	/* From the picture. */
	uint32_t is_reference;
	uint32_t frame_num;
	int32_t field_order[2];
	uint32_t surface;
	/* The references, each placed in the picture structure's entry dpb_entry[n]. */
	uint32_t reference_count;
	struct nvrtx_nvdec_h264_reference references[NVRTX_NVDEC_REFERENCES];
	uint32_t dpb_entry[NVRTX_NVDEC_REFERENCES];
	/* The scaling matrices in raster order. */
	uint8_t scaling_4x4[6][16];
	uint8_t scaling_8x8[2][64];
	/* The bitstream's bytes and slices, the work buffers' and the planes' row bytes. */
	uint32_t stream_bytes;
	uint32_t slice_count;
	uint32_t mbhist_bytes;
	uint32_t history_bytes;
	uint32_t pitch;
};

/* The sizes of a session's three work buffers: the co-located motion data, the macroblock history and the history. */
struct nvrtx_nvdec_buffers {
	uint32_t coloc;
	uint32_t mbhist;
	uint32_t history;
};

uint32_t nvrtx_nvdec_class(enum nvrtx_family family);
size_t nvrtx_nvdec_push_object(uint8_t *push, uint32_t nvdec_class);
size_t nvrtx_nvdec_push_decode(uint8_t *push, const struct nvrtx_nvdec_frame *frame);
void nvrtx_nvdec_h264_pic(uint8_t *pic, const struct nvrtx_nvdec_h264_picture *picture);
void nvrtx_nvdec_slices(uint32_t *table, const uint32_t *offsets, uint32_t count, uint32_t stream_bytes);
void nvrtx_nvdec_buffer_sizes(uint32_t width_mbs, uint32_t height_map_units, uint32_t reference_frames, struct nvrtx_nvdec_buffers *buffers);
void nvrtx_nvdec_alloc_params(uint8_t *params);
void nvrtx_nvdec_promote_params(uint8_t *params, uint32_t client, uint32_t chid, uint32_t channel, uint64_t va, uint64_t size);
int nvrtx_nvdec_context_bytes(const uint8_t *reply, size_t length, uint32_t engine_descriptor, uint32_t *bytes);

int nvrtx_nvdec_start(struct nvrtx_gpu *gpu);
void nvrtx_nvdec_stop(struct nvrtx_gpu *gpu);
void nvrtx_nvdec_release(struct nvrtx_gpu *gpu);
bool nvrtx_nvdec_ready(const struct nvrtx_gpu *gpu);
uint32_t nvrtx_nvdec_class_of(const struct nvrtx_gpu *gpu);
int nvrtx_nvdec_run(struct nvrtx_gpu *gpu, uint64_t push_va, size_t length, uint32_t timeout_us);

#endif
