/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The libvulkan side of the video wire round trip: libvulkan's own video
 * encoders (userland/desktop/libvulkan/video.c, included so its static
 * encoders can be called) write a decode, a begin and a parameters
 * create into byte runs the executor's readers then read (wire-decode.c).
 * The context and object calls the encoders do not reach are stubs.
 */

#include "userland/desktop/libvulkan/video.c"

#include <stdlib.h>

#include "plan/tools/rtx-video/wire-test.h"

static size_t encode_take(struct vulkan_writer *writer, uint8_t *out, size_t capacity);

/* A handle stands for its own identity on the wire. */
struct vulkan_object *
vulkan_nondispatchable_object(
	uint64_t handle)
{
	struct vulkan_object *object;

	/* Takes the handle's value as the object, so the wire id reads it back. */
	object = (struct vulkan_object *)(uintptr_t)handle;

	/* Reports the object that stands for the handle. */
	return object;
}

/* Reports the wire id of an object, which is the handle it stands for. */
uint64_t
vulkan_object_wire_id(
	const struct vulkan_object *object)
{
	uint64_t id;

	/* Takes the object's address back as the handle. */
	id = (uint64_t)(uintptr_t)object;

	/* Reports the wire id. */
	return id;
}

/* Drops the error the encoders report, which this test never reaches. */
void
vulkan_context_error(
	struct vulkan_context *context,
	VkResult error)
{
	(void)context;
	(void)error;
}

/* Stops the run if an encoder tries to talk to a real executor, which the test never wires up. */
VkResult
vulkan_context_execute(
	struct vulkan_context *context,
	const struct vulkan_writer *writer,
	size_t reply_capacity,
	struct vulkan_reader *reader)
{
	(void)context;
	(void)writer;
	(void)reply_capacity;
	(void)reader;

	/* Aborts: no call under test is meant to reach the executor. */
	abort();
}

/* Encodes a decode: [present][VkVideoDecodeInfoKHR and its chain], as vkCmdDecodeVideoKHR records it. */
size_t
wire_encode_decode(
	uint8_t *out,
	size_t capacity)
{
	StdVideoDecodeH264PictureInfo std_picture;
	VkVideoDecodeH264PictureInfoKHR picture;
	StdVideoDecodeH264ReferenceInfo std_references[2];
	VkVideoDecodeH264DpbSlotInfoKHR dpb[2];
	VkVideoPictureResourceInfoKHR resources[3];
	VkVideoReferenceSlotInfoKHR slots[2];
	VkVideoReferenceSlotInfoKHR setup;
	VkVideoDecodeInfoKHR decode;
	struct vulkan_writer writer;
	uint32_t slices[3] = { 0U, 1000U, 2048U };
	uint32_t index;
	size_t length;

	/* Describes the picture: an IDR reference with its ids, number and order counts. */
	memset(&std_picture, 0, sizeof(std_picture));
	std_picture.flags.is_reference = 1U;
	std_picture.flags.IdrPicFlag = 1U;
	std_picture.seq_parameter_set_id = 1U;
	std_picture.pic_parameter_set_id = 2U;
	std_picture.frame_num = 7U;
	std_picture.PicOrderCnt[0] = 10;
	std_picture.PicOrderCnt[1] = INT32_MAX;
	memset(&picture, 0, sizeof(picture));
	picture.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
	picture.pStdPictureInfo = &std_picture;
	picture.sliceCount = 3U;
	picture.pSliceOffsets = slices;

	/* Describes the three picture resources: the output and the two references. */
	for (index = 0U; index < 3U; index++) {
		memset(&resources[index], 0, sizeof(resources[index]));
		resources[index].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
		resources[index].codedExtent.width = 1920U;
		resources[index].codedExtent.height = 1088U;
		resources[index].imageViewBinding = (VkImageView)(uintptr_t)(0x50U + index);
	}

	/* Describes the two reference slots with their DPB info and resources. */
	for (index = 0U; index < 2U; index++) {
		memset(&std_references[index], 0, sizeof(std_references[index]));
		std_references[index].FrameNum = 5U + (uint16_t)index;
		std_references[index].PicOrderCnt[0] = 2 * (int32_t)index;
		std_references[index].PicOrderCnt[1] = 2 * (int32_t)index + 1;
		memset(&dpb[index], 0, sizeof(dpb[index]));
		dpb[index].sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
		dpb[index].pStdReferenceInfo = &std_references[index];
		memset(&slots[index], 0, sizeof(slots[index]));
		slots[index].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
		slots[index].pNext = &dpb[index];
		slots[index].slotIndex = (int32_t)index + 1;
		slots[index].pPictureResource = &resources[index + 1U];
	}

	/* Marks the second reference as a long-term reference. */
	std_references[1].flags.used_for_long_term_reference = 1U;

	/* Describes the setup slot, which the decoded picture becomes. */
	memset(&setup, 0, sizeof(setup));
	setup.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	setup.slotIndex = 3;
	setup.pPictureResource = &resources[0];

	/* Describes the decode: the stream range, the output, the setup slot and the references. */
	memset(&decode, 0, sizeof(decode));
	decode.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
	decode.pNext = &picture;
	decode.srcBuffer = (VkBuffer)(uintptr_t)0x40U;
	decode.srcBufferOffset = 256U;
	decode.srcBufferRange = 4096U;
	decode.dstPictureResource = resources[0];
	decode.pSetupReferenceSlot = &setup;
	decode.referenceSlotCount = 2U;
	decode.pReferenceSlots = slots;

	/* Writes the present flag and the encoded decode. */
	vulkan_writer_init(&writer);
	vulkan_write_u64(&writer, 1);
	video_encode_struct(&writer, (const VkBaseInStructure *)&decode);

	/* Takes the bytes out of the writer. */
	length = encode_take(&writer, out, capacity);

	/* Reports the length of the encoded decode. */
	return length;
}

/* Encodes a begin: [present][VkVideoBeginCodingInfoKHR], three slots (one without a picture, one without a slot). */
size_t
wire_encode_begin(
	uint8_t *out,
	size_t capacity)
{
	VkVideoPictureResourceInfoKHR resource;
	VkVideoReferenceSlotInfoKHR slots[3];
	VkVideoBeginCodingInfoKHR begin;
	struct vulkan_writer writer;
	size_t length;

	/* Describes the picture resource the first and the third slot name. */
	memset(&resource, 0, sizeof(resource));
	resource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
	resource.codedExtent.width = 640U;
	resource.codedExtent.height = 480U;
	resource.imageViewBinding = (VkImageView)(uintptr_t)0x60U;

	/* Describes the slots: one with a picture, one without, one without a slot. */
	memset(slots, 0, sizeof(slots));
	slots[0].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	slots[0].slotIndex = 4;
	slots[0].pPictureResource = &resource;
	slots[1].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	slots[1].slotIndex = 6;
	slots[2].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	slots[2].slotIndex = -1;
	slots[2].pPictureResource = &resource;

	/* Describes the begin with its session, its parameters and the three slots. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
	begin.videoSession = (VkVideoSessionKHR)(uintptr_t)0x70U;
	begin.videoSessionParameters = (VkVideoSessionParametersKHR)(uintptr_t)0x71U;
	begin.referenceSlotCount = 3U;
	begin.pReferenceSlots = slots;

	/* Writes the present flag and the encoded begin. */
	vulkan_writer_init(&writer);
	vulkan_write_u64(&writer, 1);
	video_encode_struct(&writer, (const VkBaseInStructure *)&begin);

	/* Takes the bytes out of the writer. */
	length = encode_take(&writer, out, capacity);

	/* Reports the length of the encoded begin. */
	return length;
}

/*
 * Encodes a parameters create: [present][VkVideoSessionParametersCreateInfoKHR
 * and its chain]: one SPS with scaling lists, one PPS.
 */
size_t
wire_encode_parameters(
	uint8_t *out,
	size_t capacity)
{
	StdVideoH264ScalingLists scaling;
	StdVideoH264SequenceParameterSet sps;
	StdVideoH264PictureParameterSet pps;
	VkVideoDecodeH264SessionParametersAddInfoKHR add;
	VkVideoDecodeH264SessionParametersCreateInfoKHR h264;
	VkVideoSessionParametersCreateInfoKHR create;
	struct vulkan_writer writer;
	int32_t cycle[2] = { 3, -4 };
	uint32_t index;
	size_t length;

	/* Describes the scaling lists: one list present, one using the default matrix. */
	memset(&scaling, 0, sizeof(scaling));
	scaling.scaling_list_present_mask = 0x41U;
	scaling.use_default_scaling_matrix_mask = 0x40U;
	for (index = 0U; index < 16U; index++)
		scaling.ScalingList4x4[0][index] = (uint8_t)(index + 1U);

	/* Describes the sequence parameter set with its order count cycle and scaling lists. */
	memset(&sps, 0, sizeof(sps));
	sps.flags.frame_mbs_only_flag = 1U;
	sps.flags.direct_8x8_inference_flag = 1U;
	sps.flags.seq_scaling_matrix_present_flag = 1U;
	sps.profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
	sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
	sps.seq_parameter_set_id = 1U;
	sps.log2_max_frame_num_minus4 = 4U;
	sps.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_1;
	sps.log2_max_pic_order_cnt_lsb_minus4 = 2U;
	sps.num_ref_frames_in_pic_order_cnt_cycle = 2U;
	sps.max_num_ref_frames = 4U;
	sps.pic_width_in_mbs_minus1 = 119U;
	sps.pic_height_in_map_units_minus1 = 67U;
	sps.pOffsetForRefFrame = cycle;
	sps.pScalingLists = &scaling;

	/* Describes the picture parameter set. */
	memset(&pps, 0, sizeof(pps));
	pps.flags.entropy_coding_mode_flag = 1U;
	pps.flags.transform_8x8_mode_flag = 1U;
	pps.seq_parameter_set_id = 1U;
	pps.pic_parameter_set_id = 2U;
	pps.num_ref_idx_l0_default_active_minus1 = 3U;
	pps.weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_IMPLICIT;
	pps.pic_init_qp_minus26 = -5;
	pps.chroma_qp_index_offset = -2;
	pps.second_chroma_qp_index_offset = 3;

	/* Describes the sets to add, then the H.264 create, then the create itself. */
	memset(&add, 0, sizeof(add));
	add.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
	add.stdSPSCount = 1U;
	add.pStdSPSs = &sps;
	add.stdPPSCount = 1U;
	add.pStdPPSs = &pps;
	memset(&h264, 0, sizeof(h264));
	h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
	h264.maxStdSPSCount = 4U;
	h264.maxStdPPSCount = 8U;
	h264.pParametersAddInfo = &add;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
	create.pNext = &h264;
	create.videoSession = (VkVideoSessionKHR)(uintptr_t)0x70U;

	/* Writes the present flag and the encoded create. */
	vulkan_writer_init(&writer);
	vulkan_write_u64(&writer, 1);
	video_encode_struct(&writer, (const VkBaseInStructure *)&create);

	/* Takes the bytes out of the writer. */
	length = encode_take(&writer, out, capacity);

	/* Reports the length of the encoded create. */
	return length;
}

/* Copies a writer's bytes out (0 when it failed or does not fit) and frees it. */
static size_t
encode_take(
	struct vulkan_writer *writer,
	uint8_t *out,
	size_t capacity)
{
	size_t bytes;

	/* Takes the bytes only when the writer succeeded and they fit. */
	bytes = 0U;
	if (writer->error == VK_SUCCESS && writer->bytes <= capacity) {
		memcpy(out, writer->data, writer->bytes);
		bytes = writer->bytes;
	}

	/* Frees the writer's buffer. */
	vulkan_writer_finish(writer);

	/* Reports how many bytes were taken. */
	return bytes;
}
