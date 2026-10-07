/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The libvulkan half of the video round trip (ws083-p003b).
 *
 * Runs libvulkan's video.c against a stand-in transport and writes every
 * request and every recorded command it produces, byte for byte, into a
 * file of its own in the directory given on the command line.  The
 * executor half (host-video-executor.c) feeds the files to the i915 Vulkan
 * executor.  The stand-in answers each request with a well-formed reply so
 * that libvulkan goes on; what the replies say is not this half's test.
 *
 * The objects the commands name have fixed wire identities that the
 * executor half creates under the same numbers.
 */

#include "internal.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The wire identities shared with the executor half. */
#define WIRE_PHYSICAL	0x1111ULL
#define WIRE_DEVICE	0xd0ULL
#define WIRE_CMDBUF	0xa00ULL
#define WIRE_MEMORY	0x100ULL
#define WIRE_BUFFER	0x200ULL
#define WIRE_VIEW_A	0x400ULL
#define WIRE_VIEW_B	0x401ULL

/* The directory the files go to, and the name the next request or record is written under. */
static const char *directory;
static const char *next_name;

/* The session, physical device, device and command buffer of libvulkan. */
static struct vulkan_context context;
static struct VkInstance_T instance;
static struct VkPhysicalDevice_T physical;
static struct VkDevice_T device;
static struct VkCommandBuffer_T command;

/* The stand-in objects the commands name by handle. */
static struct vulkan_object memory_object;
static struct vulkan_object buffer_object;
static struct vulkan_object view_a;
static struct vulkan_object view_b;

/* The reply being built. */
static uint8_t reply[8192];
static size_t reply_bytes;

static void save(const char *name, const uint8_t *bytes, size_t count);
static void put32(uint32_t value);
static void put64(uint64_t value);
static void profile_init(VkVideoProfileInfoKHR *profile, VkVideoDecodeH264ProfileInfoKHR *h264);
static void picture_init(VkVideoPictureResourceInfoKHR *picture, const struct vulkan_object *view);

/* Writes one stream into its file. */
static void
save(
	const char *name,
	const uint8_t *bytes,
	size_t count)
{
	char path[512];
	FILE *file;

	/* <directory>/<name>.bin */
	assert(name != NULL);
	snprintf(path, sizeof(path), "%s/%s.bin", directory, name);
	file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1, count, file) == count);
	assert(fclose(file) == 0);
}

/* Appends a little-endian word to the reply. */
static void
put32(
	uint32_t value)
{
	reply[reply_bytes++] = (uint8_t)value;
	reply[reply_bytes++] = (uint8_t)(value >> 8);
	reply[reply_bytes++] = (uint8_t)(value >> 16);
	reply[reply_bytes++] = (uint8_t)(value >> 24);
}

/* Appends a little-endian double word to the reply. */
static void
put64(
	uint64_t value)
{
	put32((uint32_t)value);
	put32((uint32_t)(value >> 32));
}

/*
 * The stand-in transport: writes the request and answers it.
 */
VkResult
vulkan_context_execute(
	struct vulkan_context *ctx,
	const struct vulkan_writer *writer,
	size_t capacity,
	struct vulkan_reader *reader)
{
	uint32_t opcode;
	uint32_t index;
	uint64_t identity;

	/* Writes the request as libvulkan made it. */
	assert(ctx == &context);
	save(next_name, writer->data, writer->bytes);
	next_name = NULL;

	/* Answers with the opcode and what libvulkan reads next. */
	memcpy(&opcode, writer->data, 4);
	reply_bytes = 0;
	put32(opcode);
	switch (opcode) {
	case GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES:
		/* The capabilities with the decode and H.264 records nested. */
		put32(VK_SUCCESS);
		put64(1);
		put32(VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR);
		put64(1);
		put32(VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR);
		put64(1);
		put32(VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR);
		put64(0);
		put32(14);
		put32(0);
		put32(0);
		put32(1);
		put32(2);
		put64(32);
		put64(1);
		for (index = 0; index < 6; index++)
			put32(16);
		put32(17);
		put32(16);
		put64(256);
		memset(reply + reply_bytes, 0, 256);
		reply_bytes += 256;
		put32(0);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_FORMAT_PROPERTIES:
		/* One format. */
		put32(VK_SUCCESS);
		put64(1);
		put32(1);
		put64(1);
		put32(VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR);
		put64(0);
		for (index = 0; index < 10; index++)
			put32(0);
		break;
	case GPU_OP_CREATE_VIDEO_SESSION:
	case GPU_OP_CREATE_VIDEO_SESSION_PARAMETERS:
		/* The identity, the request's last double word. */
		memcpy(&identity, writer->data + writer->bytes - 8, 8);
		put32(VK_SUCCESS);
		put64(1);
		put64(identity);
		break;
	case GPU_OP_GET_VIDEO_SESSION_MEMORY_REQUIREMENTS:
		/* Eight bindings of a page each. */
		put32(VK_SUCCESS);
		put64(1);
		put32(8);
		put64(8);
		for (index = 0; index < 8; index++) {
			put32(VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR);
			put64(0);
			put32(index);
			put64(4096);
			put64(4096);
			put32(1);
		}
		break;
	case GPU_OP_BIND_VIDEO_SESSION_MEMORY:
	case GPU_OP_UPDATE_VIDEO_SESSION_PARAMETERS:
		/* Accepted. */
		put32(VK_SUCCESS);
		break;
	default:
		/* A destroy is answered with its opcode. */
		break;
	}

	/* Hands libvulkan its copy. */
	assert(reply_bytes <= capacity);
	memset(reader, 0, sizeof(*reader));
	reader->data = malloc(reply_bytes);
	assert(reader->data != NULL);
	memcpy(reader->data, reply, reply_bytes);
	reader->bytes = reply_bytes;
	return VK_SUCCESS;
}

/* The ordinary recording framing of commands.c: opcode, no reply, command buffer. */
VkBool32
vulkan_command_record_begin(
	struct VkCommandBuffer_T *buffer,
	struct vulkan_writer *writer,
	uint32_t opcode)
{
	vulkan_writer_init_for_object(writer, &buffer->object);
	writer->opcode = opcode;
	vulkan_write_u32(writer, opcode);
	vulkan_write_u32(writer, 0);
	vulkan_write_u64(writer, buffer->object.wire_id);
	return VK_TRUE;
}

/* Writes a finished record into its file. */
void
vulkan_command_record_finish(
	struct VkCommandBuffer_T *buffer,
	struct vulkan_writer *writer)
{
	(void)buffer;
	assert(writer->error == VK_SUCCESS);
	save(next_name, writer->data, writer->bytes);
	next_name = NULL;
	vulkan_writer_finish(writer);
}

/* An H.264 High progressive 8-bit 4:2:0 profile. */
static void
profile_init(
	VkVideoProfileInfoKHR *profile,
	VkVideoDecodeH264ProfileInfoKHR *h264)
{
	memset(h264, 0, sizeof(*h264));
	h264->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
	h264->stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
	memset(profile, 0, sizeof(*profile));
	profile->sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
	profile->pNext = h264;
	profile->videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
	profile->chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
	profile->lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
	profile->chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
}

/* A whole 64x64 picture of a view. */
static void
picture_init(
	VkVideoPictureResourceInfoKHR *picture,
	const struct vulkan_object *view)
{
	memset(picture, 0, sizeof(*picture));
	picture->sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
	picture->codedExtent.width = 64;
	picture->codedExtent.height = 64;
	picture->imageViewBinding = (VkImageView)(uintptr_t)view;
}

/*
 * Makes every stream of the round trip.
 */
int
main(
	int argc,
	char **argv)
{
	VkVideoProfileInfoKHR profile;
	VkVideoDecodeH264ProfileInfoKHR h264;
	VkVideoProfileListInfoKHR list;
	VkPhysicalDeviceVideoFormatInfoKHR format_info;
	VkVideoFormatPropertiesKHR format;
	VkVideoDecodeH264CapabilitiesKHR h264_capabilities;
	VkVideoDecodeCapabilitiesKHR decode_capabilities;
	VkVideoCapabilitiesKHR capabilities;
	VkExtensionProperties header;
	VkVideoSessionCreateInfoKHR session_info;
	VkVideoSessionKHR session;
	VkVideoSessionMemoryRequirementsKHR requirements[8];
	VkBindVideoSessionMemoryInfoKHR binds[8];
	StdVideoH264SequenceParameterSet sps;
	StdVideoH264PictureParameterSet pps[2];
	VkVideoDecodeH264SessionParametersAddInfoKHR add;
	VkVideoDecodeH264SessionParametersCreateInfoKHR h264_parameters;
	VkVideoSessionParametersCreateInfoKHR parameters_info;
	VkVideoSessionParametersUpdateInfoKHR update;
	VkVideoSessionParametersKHR parameters;
	VkVideoPictureResourceInfoKHR picture_a;
	VkVideoPictureResourceInfoKHR picture_b;
	StdVideoDecodeH264ReferenceInfo reference;
	VkVideoDecodeH264DpbSlotInfoKHR dpb;
	VkVideoReferenceSlotInfoKHR slots[2];
	VkVideoReferenceSlotInfoKHR setup;
	VkVideoBeginCodingInfoKHR begin;
	VkVideoCodingControlInfoKHR control;
	StdVideoDecodeH264PictureInfo std_picture;
	uint32_t slice_offsets[2];
	VkVideoDecodeH264PictureInfoKHR picture;
	VkVideoDecodeInfoKHR decode;
	VkVideoEndCodingInfoKHR end;
	VkResult status;
	uint32_t count;
	uint32_t index;

	/* The output directory. */
	assert(argc == 2);
	directory = argv[1];

	/* libvulkan's objects: a video session context, a device that enabled the extensions. */
	memset(&context, 0, sizeof(context));
	context.video_h264 = VK_TRUE;
	instance.enabled_extensions = VULKAN_INSTANCE_PROPERTIES2;
	physical.object.context = &context;
	physical.object.wire_id = WIRE_PHYSICAL;
	physical.instance = &instance;
	physical.supported_extensions = VULKAN_DEVICE_SYNCHRONIZATION2 | VULKAN_DEVICE_VIDEO_QUEUE | VULKAN_DEVICE_VIDEO_DECODE_QUEUE | VULKAN_DEVICE_VIDEO_DECODE_H264;
	device.object.context = &context;
	device.object.wire_id = WIRE_DEVICE;
	device.physical = &physical;
	device.enabled_extensions = physical.supported_extensions;
	command.object.context = &context;
	command.object.wire_id = WIRE_CMDBUF;

	/* The objects the commands name. */
	memory_object.wire_id = WIRE_MEMORY;
	buffer_object.wire_id = WIRE_BUFFER;
	view_a.wire_id = WIRE_VIEW_A;
	view_b.wire_id = WIRE_VIEW_B;

	/* caps: the capabilities of H.264 High with the decode and H.264 records chained. */
	profile_init(&profile, &h264);
	memset(&h264_capabilities, 0, sizeof(h264_capabilities));
	h264_capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;
	memset(&decode_capabilities, 0, sizeof(decode_capabilities));
	decode_capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
	decode_capabilities.pNext = &h264_capabilities;
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
	capabilities.pNext = &decode_capabilities;
	next_name = "caps";
	status = vkGetPhysicalDeviceVideoCapabilitiesKHR(&physical, &profile, &capabilities);
	assert(status == VK_SUCCESS);

	/* format: the formats of the profile for decode output and reference. */
	memset(&list, 0, sizeof(list));
	list.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
	list.profileCount = 1;
	list.pProfiles = &profile;
	memset(&format_info, 0, sizeof(format_info));
	format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
	format_info.pNext = &list;
	format_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
	memset(&format, 0, sizeof(format));
	format.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
	count = 1;
	next_name = "format";
	status = vkGetPhysicalDeviceVideoFormatPropertiesKHR(&physical, &format_info, &count, &format);
	assert(status == VK_SUCCESS);

	/* session: a 64x64 session of three slots and two references on family 1. */
	memset(&header, 0, sizeof(header));
	strcpy(header.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
	header.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
	memset(&session_info, 0, sizeof(session_info));
	session_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
	session_info.queueFamilyIndex = 1;
	session_info.pVideoProfile = &profile;
	session_info.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	session_info.maxCodedExtent.width = 64;
	session_info.maxCodedExtent.height = 64;
	session_info.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	session_info.maxDpbSlots = 3;
	session_info.maxActiveReferencePictures = 2;
	session_info.pStdHeaderVersion = &header;
	next_name = "session";
	status = vkCreateVideoSessionKHR(&device, &session_info, NULL, &session);
	assert(status == VK_SUCCESS);

	/* requirements and bind: every binding a page of the memory, in order. */
	count = 8;
	memset(requirements, 0, sizeof(requirements));
	for (index = 0; index < 8; index++)
		requirements[index].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
	next_name = "requirements";
	status = vkGetVideoSessionMemoryRequirementsKHR(&device, session, &count, requirements);
	assert(status == VK_SUCCESS);
	memset(binds, 0, sizeof(binds));
	for (index = 0; index < 8; index++) {
		binds[index].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
		binds[index].memoryBindIndex = index;
		binds[index].memory = (VkDeviceMemory)(uintptr_t)&memory_object;
		binds[index].memoryOffset = 4096U * index;
		binds[index].memorySize = 4096;
	}
	next_name = "bind";
	status = vkBindVideoSessionMemoryKHR(&device, session, 8, binds);
	assert(status == VK_SUCCESS);

	/* parameters: SPS 0 (4x4 macroblocks, frames only) and PPS (0, 0). */
	memset(&sps, 0, sizeof(sps));
	sps.flags.frame_mbs_only_flag = 1;
	sps.flags.direct_8x8_inference_flag = 1;
	sps.profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
	sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_0;
	sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
	sps.log2_max_frame_num_minus4 = 0;
	sps.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_0;
	sps.log2_max_pic_order_cnt_lsb_minus4 = 2;
	sps.max_num_ref_frames = 2;
	sps.pic_width_in_mbs_minus1 = 3;
	sps.pic_height_in_map_units_minus1 = 3;
	memset(pps, 0, sizeof(pps));
	pps[0].flags.entropy_coding_mode_flag = 1;
	pps[0].flags.deblocking_filter_control_present_flag = 1;
	pps[1] = pps[0];
	pps[1].pic_parameter_set_id = 1;
	memset(&add, 0, sizeof(add));
	add.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
	add.stdSPSCount = 1;
	add.pStdSPSs = &sps;
	add.stdPPSCount = 1;
	add.pStdPPSs = &pps[0];
	memset(&h264_parameters, 0, sizeof(h264_parameters));
	h264_parameters.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
	h264_parameters.maxStdSPSCount = 4;
	h264_parameters.maxStdPPSCount = 4;
	h264_parameters.pParametersAddInfo = &add;
	memset(&parameters_info, 0, sizeof(parameters_info));
	parameters_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
	parameters_info.pNext = &h264_parameters;
	parameters_info.videoSession = session;
	next_name = "parameters";
	status = vkCreateVideoSessionParametersKHR(&device, &parameters_info, NULL, &parameters);
	assert(status == VK_SUCCESS);

	/* update: PPS (0, 1) as update 1. */
	add.stdSPSCount = 0;
	add.pStdPPSs = &pps[1];
	memset(&update, 0, sizeof(update));
	update.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_UPDATE_INFO_KHR;
	update.pNext = &add;
	update.updateSequenceCount = 1;
	next_name = "update";
	status = vkUpdateVideoSessionParametersKHR(&device, parameters, &update);
	assert(status == VK_SUCCESS);

	/* begin1: slot 0 bound to picture A, which it does not hold yet. */
	picture_init(&picture_a, &view_a);
	picture_init(&picture_b, &view_b);
	memset(slots, 0, sizeof(slots));
	slots[0].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	slots[0].slotIndex = 0;
	slots[0].pPictureResource = &picture_a;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
	begin.videoSession = session;
	begin.videoSessionParameters = parameters;
	begin.referenceSlotCount = 1;
	begin.pReferenceSlots = slots;
	next_name = "begin1";
	vkCmdBeginVideoCodingKHR(&command, &begin);

	/* control: the reset. */
	memset(&control, 0, sizeof(control));
	control.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
	control.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
	next_name = "control";
	vkCmdControlVideoCodingKHR(&command, &control);

	/* decode1: a reference IDR picture of two slices into A, set up in slot 0. */
	memset(&std_picture, 0, sizeof(std_picture));
	std_picture.flags.is_intra = 1;
	std_picture.flags.IdrPicFlag = 1;
	std_picture.flags.is_reference = 1;
	slice_offsets[0] = 0;
	slice_offsets[1] = 64;
	memset(&picture, 0, sizeof(picture));
	picture.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
	picture.pStdPictureInfo = &std_picture;
	picture.sliceCount = 2;
	picture.pSliceOffsets = slice_offsets;
	memset(&setup, 0, sizeof(setup));
	setup.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	setup.slotIndex = 0;
	setup.pPictureResource = &picture_a;
	memset(&decode, 0, sizeof(decode));
	decode.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
	decode.pNext = &picture;
	decode.srcBuffer = (VkBuffer)(uintptr_t)&buffer_object;
	decode.srcBufferOffset = 0;
	decode.srcBufferRange = 128;
	decode.dstPictureResource = picture_a;
	decode.pSetupReferenceSlot = &setup;
	next_name = "decode1";
	vkCmdDecodeVideoKHR(&command, &decode);

	/* end. */
	memset(&end, 0, sizeof(end));
	end.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
	next_name = "end";
	vkCmdEndVideoCodingKHR(&command, &end);

	/* begin2: slot 0 holding A, slot 1 bound to B. */
	slots[1].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	slots[1].slotIndex = 1;
	slots[1].pPictureResource = &picture_b;
	begin.referenceSlotCount = 2;
	next_name = "begin2";
	vkCmdBeginVideoCodingKHR(&command, &begin);

	/* decode2: a non-reference P picture into B, set up in slot 1, reading slot 0. */
	memset(&reference, 0, sizeof(reference));
	reference.PicOrderCnt[0] = 0;
	memset(&dpb, 0, sizeof(dpb));
	dpb.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
	dpb.pStdReferenceInfo = &reference;
	slots[0].pNext = &dpb;
	std_picture.flags.is_intra = 0;
	std_picture.flags.IdrPicFlag = 0;
	std_picture.flags.is_reference = 0;
	std_picture.frame_num = 1;
	std_picture.PicOrderCnt[0] = 2;
	picture.sliceCount = 1;
	setup.slotIndex = 1;
	setup.pPictureResource = &picture_b;
	decode.dstPictureResource = picture_b;
	decode.referenceSlotCount = 1;
	decode.pReferenceSlots = &slots[0];
	next_name = "decode2";
	vkCmdDecodeVideoKHR(&command, &decode);

	/* decode3: the same picture reading slot 1, which decode2 left inactive. */
	slots[1].pNext = &dpb;
	decode.pReferenceSlots = &slots[1];
	next_name = "decode3";
	vkCmdDecodeVideoKHR(&command, &decode);

	/* begin3: slot 0 deactivated (no picture). */
	slots[0].pNext = NULL;
	slots[0].pPictureResource = NULL;
	begin.referenceSlotCount = 1;
	next_name = "begin3";
	vkCmdBeginVideoCodingKHR(&command, &begin);

	/* destroy_parameters and destroy_session. */
	next_name = "destroy_parameters";
	vkDestroyVideoSessionParametersKHR(&device, parameters, NULL);
	next_name = "destroy_session";
	vkDestroyVideoSessionKHR(&device, session, NULL);

	/* Every stream is written. */
	vulkan_writer_finish(&command.recording);
	printf("ws083 video wire dump: %s\n", directory);
	return 0;
}
