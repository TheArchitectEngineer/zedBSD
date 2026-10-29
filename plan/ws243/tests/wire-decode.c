/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's side of the WS243 wire round trip: the bytes libvulkan's
 * own encoders wrote (wire-encode.c) are read by the executor's video
 * readers (src/drivers/gpu/nvrtx/vk/vk-video.c, included so its static
 * readers can be called), which must take every byte and every field.
 * The node's and the registry's calls the readers do not reach are stubs.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/drivers/gpu/nvrtx/vk/vk-video.c"

#include "plan/ws243/tests/wire-test.h"

static void check(int ok, const char *what);
static void reader_on(struct i915_wire_reader *reader, const uint8_t *bytes, size_t length);
static void test_decode(void);
static void test_begin(void);
static void test_parameters(void);

/* The checks run and failed. */
static int checks;
static int failures;

void *
kern_malloc(size_t bytes)
{
	return malloc(bytes);
}

void *
kern_calloc(size_t count, size_t bytes)
{
	return calloc(count, bytes);
}

void
kern_free(void *pointer)
{
	free(pointer);
}

void
kern_logf(const char *format, ...)
{
	(void)format;
}

bool
nvrtx_vk_video_available(void *owner)
{
	(void)owner;
	return true;
}

int
nvrtx_vk_video_decode(void *owner, uint64_t va, uint32_t bytes)
{
	(void)owner;
	(void)va;
	(void)bytes;
	return EIO;
}

int
nvrtx_vk_gpu_alloc_system(void *owner, uint64_t bytes, void **token, uint8_t **cpu, uint64_t *va)
{
	(void)owner;
	(void)bytes;
	(void)token;
	(void)cpu;
	(void)va;
	return ENOMEM;
}

void
nvrtx_vk_gpu_free(void *owner, void *token)
{
	(void)owner;
	(void)token;
}

int
nvrtx_vk_buffer_address(const struct nvrtx_vk_buffer *buffer, uint64_t *va, uint8_t **cpu)
{
	(void)buffer;
	(void)va;
	(void)cpu;
	return EIO;
}

int
nvrtx_vk_image_address(const struct nvrtx_vk_image *image, uint64_t *va, uint8_t **cpu)
{
	(void)image;
	(void)va;
	(void)cpu;
	return EIO;
}

/* Runs the three round trips. */
int
main(void)
{
	test_decode();
	test_begin();
	test_parameters();
	printf("ws243 wire round trip: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;
	return 0;
}

/* Counts one check and reports it when it fails. */
static void
check(int ok, const char *what)
{
	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

/* Points a reader at a run of bytes. */
static void
reader_on(struct i915_wire_reader *reader, const uint8_t *bytes, size_t length)
{
	memset(reader, 0, sizeof(*reader));
	reader->base = bytes;
	reader->size = length;
}

/* A decode with its picture, slices, setup slot and two references. */
static void
test_decode(void)
{
	static uint8_t bytes[4096];
	struct i915_wire_reader reader;
	struct video_record *video;
	size_t length;
	int error;

	length = wire_encode_decode(bytes, sizeof(bytes));
	check(length != 0U, "decode: libvulkan encoded it");
	video = calloc(1U, sizeof(*video));
	video->opcode = GPU_OP_CMD_DECODE_VIDEO;
	reader_on(&reader, bytes, length);
	error = video_record_coding(NULL, NULL, GPU_OP_CMD_DECODE_VIDEO, &reader, video);
	check(error == 0 && reader.error == 0, "decode: read");
	check(reader.offset == length, "decode: every byte taken");
	check(video->offset == 256U && video->range == 4096U, "decode: bitstream range");
	check(video->destination.view == 0x50U && video->destination.width == 1920U && video->destination.height == 1088U, "decode: output");
	check(video->has_setup && video->setup.index == 3 && video->setup.has_picture, "decode: setup slot");
	check(video->slot_count == 2U && video->slots[0].index == 1 && video->slots[1].index == 2, "decode: references");
	check(video->slots[1].resource.view == 0x52U, "decode: reference view");
	check(video->slots[0].has_reference && video->slots[0].reference.frame_num == 5U && video->slots[0].reference.poc[1] == 1, "decode: reference 0 info");
	check((video->slots[1].reference.flags & NVRTX_VIDEO_REFERENCE_LONG_TERM) != 0U && video->slots[1].reference.poc[0] == 2, "decode: reference 1 long term");
	check(video->has_picture && video->picture.sps_id == 1U && video->picture.pps_id == 2U && video->picture.frame_num == 7U, "decode: picture ids");
	check((video->picture.flags & NVRTX_VIDEO_PICTURE_REFERENCE) != 0U && video->picture.poc[0] == 10 && video->picture.poc[1] == INT32_MAX, "decode: picture flags and order");
	check(video->slice_count == 3U && video->slices[1] == 1000U && video->slices[2] == 2048U, "decode: slices");
	free(video);
}

/* A begin with a slot and picture, a slot without a picture, a picture without a slot. */
static void
test_begin(void)
{
	static uint8_t bytes[2048];
	struct i915_wire_reader reader;
	struct video_record *video;
	size_t length;
	int error;

	length = wire_encode_begin(bytes, sizeof(bytes));
	check(length != 0U, "begin: libvulkan encoded it");
	video = calloc(1U, sizeof(*video));
	reader_on(&reader, bytes, length);
	error = video_record_coding(NULL, NULL, GPU_OP_CMD_BEGIN_VIDEO_CODING, &reader, video);
	check(error == 0 && reader.error == 0 && reader.offset == length, "begin: read whole");
	check(video->slot_count == 3U, "begin: three slots");
	check(video->slots[0].index == 4 && video->slots[0].has_picture && video->slots[0].resource.view == 0x60U, "begin: bound slot");
	check(video->slots[1].index == 6 && !video->slots[1].has_picture, "begin: deactivated slot");
	check(video->slots[2].index == -1 && video->slots[2].has_picture, "begin: picture without a slot");
	free(video);
}

/* A parameters create carrying one SPS with scaling lists and one PPS. */
static void
test_parameters(void)
{
	static uint8_t bytes[4096];
	struct i915_wire_reader reader;
	struct video_chain chain;
	const struct nvrtx_video_sps *sps;
	const struct nvrtx_video_pps *pps;
	uint64_t present;
	uint64_t session_id;
	uint32_t type;
	size_t length;

	length = wire_encode_parameters(bytes, sizeof(bytes));
	check(length != 0U, "parameters: libvulkan encoded it");
	reader_on(&reader, bytes, length);
	memset(&chain, 0, sizeof(chain));
	present = drv_i915_wire_read_u64(&reader);
	type = drv_i915_wire_read_u32(&reader);
	video_read_chain(&reader, &chain, 0U);
	(void)drv_i915_wire_read_u32(&reader);
	(void)drv_i915_wire_read_u64(&reader);
	session_id = drv_i915_wire_read_u64(&reader);
	check(present == 1U && type == VIDEO_TYPE_PARAMETERS_CREATE && reader.error == 0, "parameters: read");
	check(reader.offset == length && session_id == 0x70U, "parameters: every byte taken");
	check(chain.has_h264_create && chain.max_sps == 4U && chain.max_pps == 8U && chain.add_error == 0, "parameters: H.264 create");
	check(chain.new_sps_count == 1U && chain.new_pps != NULL && chain.new_pps->next == NULL, "parameters: one set each");
	if (chain.new_sps_count == 1U && chain.new_pps != NULL) {
		sps = chain.new_sps[0];
		pps = &chain.new_pps->pps;
		check(sps->id == 1U && sps->profile_idc == 100U && sps->chroma_format_idc == 1U, "parameters: SPS ids");
		check((sps->flags & NVRTX_VIDEO_SPS_FRAME_MBS_ONLY) != 0U && (sps->flags & NVRTX_VIDEO_SPS_SCALING) != 0U, "parameters: SPS flags");
		check(sps->log2_max_frame_num_minus4 == 4U && sps->pic_order_cnt_type == 1U && sps->log2_max_pic_order_cnt_lsb_minus4 == 2U, "parameters: SPS order");
		check(sps->max_num_ref_frames == 4U && sps->pic_width_in_mbs_minus1 == 119U && sps->pic_height_in_map_units_minus1 == 67U, "parameters: SPS sizes");
		check(sps->has_scaling && sps->scaling.present_mask == 0x41U && sps->scaling.default_mask == 0x40U && sps->scaling.list4[0][15] == 16U, "parameters: SPS scaling");
		check(pps->sps_id == 1U && pps->pps_id == 2U && pps->num_ref_idx_l0_default_active_minus1 == 3U && pps->weighted_bipred_idc == 2U, "parameters: PPS ids");
		check(pps->pic_init_qp_minus26 == -5 && pps->chroma_qp_index_offset == -2 && pps->second_chroma_qp_index_offset == 3, "parameters: PPS quantizers");
		check((pps->flags & NVRTX_VIDEO_PPS_ENTROPY) != 0U && (pps->flags & NVRTX_VIDEO_PPS_TRANSFORM_8X8) != 0U && !pps->has_scaling, "parameters: PPS flags");
	}
	video_chain_free(&chain);
}
