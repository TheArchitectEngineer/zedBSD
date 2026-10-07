/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkvideo-probe: lists what a Vulkan device offers for video decode, and
 * decodes an H.264 elementary stream with Vulkan Video, printing the
 * SHA-256 of each frame (ws083).
 *
 *   vkvideo-probe --list
 *   vkvideo-probe [--frames=N] [--expect=FILE.sha256] STREAM.h264
 *
 * The list names each queue family with its flags and video codec
 * operations and the device's video extensions; on a device without video
 * decode it shows no video family and no video extension.  The decode
 * reads the stream's parameter sets and pictures (h264.c), decodes each
 * picture into one NV12 image (its DPB slot 0, the session reset before
 * each picture), reads the image's planes through the image's subresource
 * layout (zedBSD's promise for an optimal NV12 image) and hashes the
 * display window as ffmpeg hashes a raw NV12 frame (frame.c).  The probe
 * decodes intra pictures only (each an IDR picture); P and B pictures
 * stop it (ws083-p006).  With --expect, each frame is compared with the
 * file's line, and the exit status says whether all matched.
 */

#include "h264.h"
#include "frame.h"

#include <vulkan/vulkan.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How long a decode may take before the probe gives up, in nanoseconds. */
#define PROBE_WAIT_NS		5000000000ULL

/* The most session memory bindings the probe binds. */
#define PROBE_BINDINGS		32U

/* The most families and extensions the list shows. */
#define PROBE_FAMILIES		16U
#define PROBE_EXTENSIONS	512U

/* What one run asked for. */
struct probe_options {
	int list;
	const char *stream;
	const char *expect;
	uint32_t frames;
};

/*
 * The Vulkan objects of a decode, made in order and destroyed in reverse
 * by probe_close; a handle never made is VK_NULL_HANDLE.
 */
struct probe {
	VkInstance instance;
	VkPhysicalDevice physical;
	uint32_t family;
	VkDevice device;
	VkQueue queue;
	VkPhysicalDeviceMemoryProperties memory;

	/* The H.264 decode profile, and the list of it buffers and images name. */
	VkVideoDecodeH264ProfileInfoKHR h264_profile;
	VkVideoProfileInfoKHR profile;
	VkVideoProfileListInfoKHR profile_list;

	/* The largest coded picture of the stream. */
	VkExtent2D extent;

	/* The session, its memory and its parameters. */
	VkVideoSessionKHR session;
	VkDeviceMemory session_memory[PROBE_BINDINGS];
	uint32_t session_memory_count;
	VkVideoSessionParametersKHR parameters;

	/* The picture: its image, memory (mapped) and view. */
	VkImage image;
	VkDeviceMemory image_memory;
	uint8_t *image_map;
	VkImageView view;

	/* The bitstream buffer and its memory (mapped). */
	VkBuffer buffer;
	VkDeviceMemory buffer_memory;
	uint8_t *buffer_map;
	VkDeviceSize buffer_size;

	/* The command buffer of the video family, and the fence a decode is waited on with. */
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;
};

static int probe_arguments(int argc, char **argv, struct probe_options *options);
static int probe_instance(struct probe *probe);
static int probe_list(struct probe *probe);
static int probe_video_family(struct probe *probe, uint32_t *family);
static int probe_device(struct probe *probe);
static int probe_capabilities(struct probe *probe);
static int probe_memory_type(const struct probe *probe, uint32_t bits, uint32_t *index);
static int probe_allocate(struct probe *probe, const VkMemoryRequirements *requirements, VkDeviceMemory *memory);
static int probe_picture(struct probe *probe);
static int probe_bitstream(struct probe *probe, size_t bytes);
static int probe_session(struct probe *probe);
static int probe_parameters(struct probe *probe, const struct h264_stream *stream);
static int probe_commands(struct probe *probe);
static int probe_decode(struct probe *probe, const struct h264_stream *stream, const struct h264_picture *picture, int first);
static void probe_hash(struct probe *probe, const StdVideoH264SequenceParameterSet *sps, char text[65]);
static int probe_run(struct probe *probe, const struct probe_options *options);
static void probe_close(struct probe *probe);
static int probe_failed(VkResult result, const char *what);
static uint8_t *probe_read(const char *path, size_t *size);

/*
 * Lists the device's video decode, or decodes a stream and prints its frames' hashes.
 */
int
main(
	int argc,
	char **argv)
{
	struct probe_options options;
	struct probe probe;
	int status;

	/* The options. */
	memset(&options, 0, sizeof(options));
	status = probe_arguments(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: vkvideo-probe --list | vkvideo-probe [--frames=N] [--expect=FILE.sha256] STREAM.h264\n");
		return 2;
	}

	/* The instance and its first physical device. */
	memset(&probe, 0, sizeof(probe));
	status = probe_instance(&probe);
	if (status != 0) {
		probe_close(&probe);
		return 1;
	}

	/* The list, or the decode. */
	if (options.list)
		status = probe_list(&probe);
	else
		status = probe_run(&probe, &options);
	probe_close(&probe);

	/* Reports a list or a decode that failed. */
	if (status != 0)
		return status;

	/* Succeeded: listed, or every frame decoded (and matched). */
	return 0;
}

/* Reads the options: --list, or a stream with --frames and --expect. */
static int
probe_arguments(
	int argc,
	char **argv,
	struct probe_options *options)
{
	char *end;
	int index;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--list") == 0) {
			options->list = 1;
		} else if (strncmp(argv[index], "--frames=", 9U) == 0) {
			errno = 0;
			options->frames = (uint32_t)strtoul(argv[index] + 9, &end, 10);
			if (errno != 0 || *end != '\0')
				return -1;
		} else if (strncmp(argv[index], "--expect=", 9U) == 0) {
			options->expect = argv[index] + 9;
		} else if (argv[index][0] != '-' && options->stream == NULL) {
			options->stream = argv[index];
		} else {
			return -1;
		}
	}

	/* Exactly one of the list and a stream. */
	if (options->list && options->stream != NULL)
		return -1;
	if (!options->list && options->stream == NULL)
		return -1;

	/* Succeeded: the options are read. */
	return 0;
}

/* Makes the instance (with the properties2 extension the video queries need) and takes its first device. */
static int
probe_instance(
	struct probe *probe)
{
	static const char *const extensions[] = { "VK_KHR_get_physical_device_properties2" };
	VkApplicationInfo application;
	VkInstanceCreateInfo info;
	VkResult result;
	uint32_t count;

	/* The instance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkvideo-probe";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = &application;
	info.enabledExtensionCount = 1U;
	info.ppEnabledExtensionNames = extensions;
	result = vkCreateInstance(&info, NULL, &probe->instance);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateInstance");

	/* The first physical device. */
	count = 1U;
	result = vkEnumeratePhysicalDevices(probe->instance, &count, &probe->physical);
	if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0U)
		return probe_failed(result, "vkEnumeratePhysicalDevices");
	vkGetPhysicalDeviceMemoryProperties(probe->physical, &probe->memory);

	/* Succeeded: a device to ask. */
	return 0;
}

/*
 * Prints the device, each queue family with its flags and video codec
 * operations, and the device's video extensions.
 */
static int
probe_list(
	struct probe *probe)
{
	VkPhysicalDeviceProperties properties;
	VkQueueFamilyProperties2 families[PROBE_FAMILIES];
	VkQueueFamilyVideoPropertiesKHR video[PROBE_FAMILIES];
	static VkExtensionProperties extensions[PROBE_EXTENSIONS];
	VkResult result;
	uint32_t count;
	uint32_t index;
	uint32_t video_families;
	uint32_t video_extensions;

	/* The device. */
	vkGetPhysicalDeviceProperties(probe->physical, &properties);
	printf("vkvideo-probe: device %s\n", properties.deviceName);

	/* The queue families, each with its video properties chained. */
	count = PROBE_FAMILIES;
	memset(families, 0, sizeof(families));
	memset(video, 0, sizeof(video));
	for (index = 0U; index < PROBE_FAMILIES; index++) {
		families[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		families[index].pNext = &video[index];
		video[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
	}
	vkGetPhysicalDeviceQueueFamilyProperties2KHR(probe->physical, &count, families);
	video_families = 0U;
	for (index = 0U; index < count; index++) {
		printf("vkvideo-probe: family %u flags 0x%x queues %u codecs 0x%x\n",
		       index,
		       families[index].queueFamilyProperties.queueFlags,
		       families[index].queueFamilyProperties.queueCount,
		       video[index].videoCodecOperations);
		if ((families[index].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0U)
			video_families++;
	}

	/* The video extensions and synchronization2. */
	count = PROBE_EXTENSIONS;
	result = vkEnumerateDeviceExtensionProperties(probe->physical, NULL, &count, extensions);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return probe_failed(result, "vkEnumerateDeviceExtensionProperties");
	video_extensions = 0U;
	for (index = 0U; index < count; index++) {
		if (strstr(extensions[index].extensionName, "video") == NULL &&
		    strcmp(extensions[index].extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) != 0)
			continue;
		printf("vkvideo-probe: extension %s %u\n", extensions[index].extensionName, extensions[index].specVersion);
		if (strstr(extensions[index].extensionName, "video") != NULL)
			video_extensions++;
	}

	/* The summary line. */
	printf("vkvideo-probe: video families %u, video extensions %u\n", video_families, video_extensions);
	return 0;
}

/* Finds the queue family that decodes H.264. */
static int
probe_video_family(
	struct probe *probe,
	uint32_t *family)
{
	VkQueueFamilyProperties2 families[PROBE_FAMILIES];
	VkQueueFamilyVideoPropertiesKHR video[PROBE_FAMILIES];
	uint32_t count;
	uint32_t index;

	/* The families with their video properties. */
	count = PROBE_FAMILIES;
	memset(families, 0, sizeof(families));
	memset(video, 0, sizeof(video));
	for (index = 0U; index < PROBE_FAMILIES; index++) {
		families[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		families[index].pNext = &video[index];
		video[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
	}
	vkGetPhysicalDeviceQueueFamilyProperties2KHR(probe->physical, &count, families);

	/* The first one with video decode and H.264. */
	for (index = 0U; index < count; index++) {
		if ((families[index].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) == 0U)
			continue;
		if ((video[index].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) == 0U)
			continue;
		*family = index;
		return 0;
	}

	/* None. */
	fprintf(stderr, "vkvideo-probe: no queue family decodes H.264\n");
	return 1;
}

/* Makes the device with one queue of the video family and the four video extensions. */
static int
probe_device(
	struct probe *probe)
{
	static const char *const extensions[] = {
		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
		VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
		VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
		VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME
	};
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo info;
	VkResult result;
	float priority;
	int error;

	/* The video family. */
	error = probe_video_family(probe, &probe->family);
	if (error != 0)
		return error;

	/* The device and its one video queue. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = probe->family;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.queueCreateInfoCount = 1U;
	info.pQueueCreateInfos = &queue;
	info.enabledExtensionCount = 4U;
	info.ppEnabledExtensionNames = extensions;
	result = vkCreateDevice(probe->physical, &info, NULL, &probe->device);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateDevice");
	vkGetDeviceQueue(probe->device, probe->family, 0U, &probe->queue);

	/* Succeeded: a device that decodes. */
	return 0;
}

/* Asks the device's H.264 decode capabilities and NV12 output format for the profile, and prints them. */
static int
probe_capabilities(
	struct probe *probe)
{
	VkVideoDecodeH264CapabilitiesKHR h264;
	VkVideoDecodeCapabilitiesKHR decode;
	VkVideoCapabilitiesKHR capabilities;
	VkPhysicalDeviceVideoFormatInfoKHR format_info;
	VkVideoFormatPropertiesKHR formats[4];
	VkResult result;
	uint32_t count;
	uint32_t index;

	/* The capabilities, with the decode and H.264 ones chained. */
	memset(&h264, 0, sizeof(h264));
	h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;
	memset(&decode, 0, sizeof(decode));
	decode.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
	decode.pNext = &h264;
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
	capabilities.pNext = &decode;
	result = vkGetPhysicalDeviceVideoCapabilitiesKHR(probe->physical, &probe->profile, &capabilities);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkGetPhysicalDeviceVideoCapabilitiesKHR");
	printf("vkvideo-probe: capabilities max %ux%u slots %u references %u level %u flags 0x%x decode 0x%x\n",
	       capabilities.maxCodedExtent.width,
	       capabilities.maxCodedExtent.height,
	       capabilities.maxDpbSlots,
	       capabilities.maxActiveReferencePictures,
	       (unsigned)h264.maxLevelIdc,
	       capabilities.flags,
	       decode.flags);

	/* The stream must fit. */
	if (probe->extent.width > capabilities.maxCodedExtent.width || probe->extent.height > capabilities.maxCodedExtent.height) {
		fprintf(stderr, "vkvideo-probe: the stream's %ux%u is larger than the decoder's\n", probe->extent.width, probe->extent.height);
		return 1;
	}

	/* The output and reference format: NV12. */
	memset(&format_info, 0, sizeof(format_info));
	format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
	format_info.pNext = &probe->profile_list;
	format_info.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
	memset(formats, 0, sizeof(formats));
	for (index = 0U; index < 4U; index++)
		formats[index].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
	count = 4U;
	result = vkGetPhysicalDeviceVideoFormatPropertiesKHR(probe->physical, &format_info, &count, formats);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return probe_failed(result, "vkGetPhysicalDeviceVideoFormatPropertiesKHR");
	for (index = 0U; index < count; index++) {
		if (formats[index].format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM)
			return 0;
	}

	/* No NV12. */
	fprintf(stderr, "vkvideo-probe: the decoder does not write NV12\n");
	return 1;
}

/* Finds a host-visible memory type among the allowed ones. */
static int
probe_memory_type(
	const struct probe *probe,
	uint32_t bits,
	uint32_t *index)
{
	uint32_t type;

	/* The first allowed host-visible type. */
	for (type = 0U; type < probe->memory.memoryTypeCount; type++) {
		if ((bits & (1U << type)) == 0U)
			continue;
		if ((probe->memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0U)
			continue;
		*index = type;
		return 0;
	}

	/* None. */
	fprintf(stderr, "vkvideo-probe: no host-visible memory type in 0x%x\n", bits);
	return 1;
}

/* Allocates memory for a requirement. */
static int
probe_allocate(
	struct probe *probe,
	const VkMemoryRequirements *requirements,
	VkDeviceMemory *memory)
{
	VkMemoryAllocateInfo info;
	VkResult result;
	uint32_t type;
	int error;

	/* A host-visible type. */
	error = probe_memory_type(probe, requirements->memoryTypeBits, &type);
	if (error != 0)
		return error;

	/* The allocation. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.allocationSize = requirements->size;
	info.memoryTypeIndex = type;
	result = vkAllocateMemory(probe->device, &info, NULL, memory);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkAllocateMemory");

	/* Succeeded: the memory. */
	return 0;
}

/* Makes the NV12 picture the decodes write: the image, its memory (mapped) and its view. */
static int
probe_picture(
	struct probe *probe)
{
	VkImageCreateInfo image;
	VkImageViewCreateInfo view;
	VkMemoryRequirements requirements;
	VkResult result;
	void *map;
	int error;

	/* The image: NV12, optimal, the decoder's output and reference picture, of the profile. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.pNext = &probe->profile_list;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	image.extent.width = probe->extent.width;
	image.extent.height = probe->extent.height;
	image.extent.depth = 1U;
	image.mipLevels = 1U;
	image.arrayLayers = 1U;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
	image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(probe->device, &image, NULL, &probe->image);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateImage");

	/* Its memory, bound and mapped. */
	vkGetImageMemoryRequirements(probe->device, probe->image, &requirements);
	error = probe_allocate(probe, &requirements, &probe->image_memory);
	if (error != 0)
		return error;
	result = vkBindImageMemory(probe->device, probe->image, probe->image_memory, 0U);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkBindImageMemory");
	result = vkMapMemory(probe->device, probe->image_memory, 0U, VK_WHOLE_SIZE, 0U, &map);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkMapMemory");
	probe->image_map = map;

	/* Its view. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = probe->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(probe->device, &view, NULL, &probe->view);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateImageView");

	/* Succeeded: the picture. */
	return 0;
}

/* Makes the bitstream buffer of a picture's bytes, and its memory (mapped). */
static int
probe_bitstream(
	struct probe *probe,
	size_t bytes)
{
	VkBufferCreateInfo buffer;
	VkMemoryRequirements requirements;
	VkResult result;
	void *map;
	int error;

	/* The buffer: whole pages, a decode's source, of the profile. */
	probe->buffer_size = ((VkDeviceSize)bytes + 4095U) & ~(VkDeviceSize)4095U;
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.pNext = &probe->profile_list;
	buffer.size = probe->buffer_size;
	buffer.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
	buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(probe->device, &buffer, NULL, &probe->buffer);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateBuffer");

	/* Its memory, bound and mapped. */
	vkGetBufferMemoryRequirements(probe->device, probe->buffer, &requirements);
	error = probe_allocate(probe, &requirements, &probe->buffer_memory);
	if (error != 0)
		return error;
	result = vkBindBufferMemory(probe->device, probe->buffer, probe->buffer_memory, 0U);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkBindBufferMemory");
	result = vkMapMemory(probe->device, probe->buffer_memory, 0U, VK_WHOLE_SIZE, 0U, &map);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkMapMemory");
	probe->buffer_map = map;

	/* Succeeded: the buffer. */
	return 0;
}

/* Makes the video session (one DPB slot, no reference read) and binds every memory it asks for. */
static int
probe_session(
	struct probe *probe)
{
	static const VkExtensionProperties header = {
		VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
		VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION
	};
	VkVideoSessionCreateInfoKHR info;
	VkVideoSessionMemoryRequirementsKHR requirements[PROBE_BINDINGS];
	VkBindVideoSessionMemoryInfoKHR binds[PROBE_BINDINGS];
	VkResult result;
	uint32_t count;
	uint32_t index;
	int error;

	/* The session. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
	info.queueFamilyIndex = probe->family;
	info.pVideoProfile = &probe->profile;
	info.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	info.maxCodedExtent = probe->extent;
	info.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	info.maxDpbSlots = 1U;
	info.maxActiveReferencePictures = 0U;
	info.pStdHeaderVersion = &header;
	result = vkCreateVideoSessionKHR(probe->device, &info, NULL, &probe->session);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateVideoSessionKHR");

	/* What memory it asks for. */
	memset(requirements, 0, sizeof(requirements));
	for (index = 0U; index < PROBE_BINDINGS; index++)
		requirements[index].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
	count = PROBE_BINDINGS;
	result = vkGetVideoSessionMemoryRequirementsKHR(probe->device, probe->session, &count, requirements);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkGetVideoSessionMemoryRequirementsKHR");

	/* One allocation for each binding, then all bound at once. */
	memset(binds, 0, sizeof(binds));
	for (index = 0U; index < count; index++) {
		error = probe_allocate(probe, &requirements[index].memoryRequirements, &probe->session_memory[index]);
		if (error != 0)
			return error;
		probe->session_memory_count = index + 1U;
		binds[index].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
		binds[index].memoryBindIndex = requirements[index].memoryBindIndex;
		binds[index].memory = probe->session_memory[index];
		binds[index].memoryOffset = 0U;
		binds[index].memorySize = requirements[index].memoryRequirements.size;
	}
	result = vkBindVideoSessionMemoryKHR(probe->device, probe->session, count, binds);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkBindVideoSessionMemoryKHR");
	printf("vkvideo-probe: session %ux%u, %u memory bindings\n", probe->extent.width, probe->extent.height, count);

	/* Succeeded: the session is bound. */
	return 0;
}

/* Makes the session's parameters from every parameter set of the stream. */
static int
probe_parameters(
	struct probe *probe,
	const struct h264_stream *stream)
{
	static StdVideoH264SequenceParameterSet sps[H264_SPS_IDS];
	static StdVideoH264PictureParameterSet pps[H264_PPS_IDS];
	VkVideoDecodeH264SessionParametersAddInfoKHR add;
	VkVideoDecodeH264SessionParametersCreateInfoKHR h264;
	VkVideoSessionParametersCreateInfoKHR info;
	VkResult result;
	uint32_t sps_count;
	uint32_t pps_count;
	uint32_t index;

	/* The sets the stream has. */
	sps_count = 0U;
	for (index = 0U; index < H264_SPS_IDS; index++) {
		if (!stream->has_sps[index])
			continue;
		sps[sps_count] = stream->sps[index];
		sps_count++;
	}
	pps_count = 0U;
	for (index = 0U; index < H264_PPS_IDS; index++) {
		if (!stream->has_pps[index])
			continue;
		pps[pps_count] = stream->pps[index];
		pps_count++;
	}

	/* The parameters object holding them. */
	memset(&add, 0, sizeof(add));
	add.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
	add.stdSPSCount = sps_count;
	add.pStdSPSs = sps;
	add.stdPPSCount = pps_count;
	add.pStdPPSs = pps;
	memset(&h264, 0, sizeof(h264));
	h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
	h264.maxStdSPSCount = sps_count;
	h264.maxStdPPSCount = pps_count;
	h264.pParametersAddInfo = &add;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
	info.pNext = &h264;
	info.videoSession = probe->session;
	result = vkCreateVideoSessionParametersKHR(probe->device, &info, NULL, &probe->parameters);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateVideoSessionParametersKHR");
	printf("vkvideo-probe: parameters %u SPS, %u PPS\n", sps_count, pps_count);

	/* Succeeded: the parameters. */
	return 0;
}

/* Makes the command pool and buffer of the video family, and the fence. */
static int
probe_commands(
	struct probe *probe)
{
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo command;
	VkFenceCreateInfo fence;
	VkResult result;

	/* The pool, whose buffer is reset before each picture. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = probe->family;
	result = vkCreateCommandPool(probe->device, &pool, NULL, &probe->pool);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateCommandPool");

	/* The buffer. */
	memset(&command, 0, sizeof(command));
	command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command.commandPool = probe->pool;
	command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command.commandBufferCount = 1U;
	result = vkAllocateCommandBuffers(probe->device, &command, &probe->command);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkAllocateCommandBuffers");

	/* The fence. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	result = vkCreateFence(probe->device, &fence, NULL, &probe->fence);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkCreateFence");

	/* Succeeded: the commands. */
	return 0;
}

/*
 * Decodes one intra picture into the image and waits for it: the slices
 * copied into the buffer each behind a three-byte start code, a coding
 * scope binding the image without a slot, the session reset, the decode
 * setting the image up as slot 0, and the scope's end.  The first picture
 * moves the image into the DPB layout.
 */
static int
probe_decode(
	struct probe *probe,
	const struct h264_stream *stream,
	const struct h264_picture *picture,
	int first)
{
	static uint32_t offsets[H264_MAX_SLICES];
	StdVideoDecodeH264ReferenceInfo reference;
	VkVideoDecodeH264DpbSlotInfoKHR slot_info;
	VkVideoDecodeH264PictureInfoKHR h264;
	VkVideoPictureResourceInfoKHR resource;
	VkVideoReferenceSlotInfoKHR bound;
	VkVideoReferenceSlotInfoKHR setup;
	VkVideoBeginCodingInfoKHR begin;
	VkVideoCodingControlInfoKHR control;
	VkVideoDecodeInfoKHR decode;
	VkVideoEndCodingInfoKHR end;
	VkCommandBufferBeginInfo record;
	VkImageMemoryBarrier barrier;
	VkSubmitInfo submit;
	const StdVideoH264SequenceParameterSet *sps;
	VkResult result;
	size_t at;
	uint32_t slice;

	/* The slices, each behind a start code, into the buffer. */
	at = 0U;
	for (slice = 0U; slice < picture->slice_count; slice++) {
		if (at + 3U + picture->slice_sizes[slice] > probe->buffer_size) {
			fprintf(stderr, "vkvideo-probe: a picture larger than the buffer\n");
			return 1;
		}
		offsets[slice] = (uint32_t)at;
		probe->buffer_map[at] = 0U;
		probe->buffer_map[at + 1U] = 0U;
		probe->buffer_map[at + 2U] = 1U;
		memcpy(probe->buffer_map + at + 3U, stream->data + picture->slice_offsets[slice], picture->slice_sizes[slice]);
		at += 3U + picture->slice_sizes[slice];
	}

	/* The picture resource: the image's coded picture of the sequence. */
	sps = &stream->sps[picture->info.seq_parameter_set_id];
	memset(&resource, 0, sizeof(resource));
	resource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
	resource.codedExtent.width = (sps->pic_width_in_mbs_minus1 + 1U) * 16U;
	resource.codedExtent.height = (sps->pic_height_in_map_units_minus1 + 1U) * 16U;
	resource.imageViewBinding = probe->view;

	/* The command buffer from its start. */
	memset(&record, 0, sizeof(record));
	record.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	record.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(probe->command, &record);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkBeginCommandBuffer");

	/* The first picture moves the image into the DPB layout. */
	if (first) {
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = probe->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1U;
		barrier.subresourceRange.layerCount = 1U;
		vkCmdPipelineBarrier(probe->command,
				     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				     0U,
				     0U,
				     NULL,
				     0U,
				     NULL,
				     1U,
				     &barrier);
	}

	/* The scope binds the image without a slot, and the session is reset. */
	memset(&bound, 0, sizeof(bound));
	bound.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	bound.slotIndex = -1;
	bound.pPictureResource = &resource;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
	begin.videoSession = probe->session;
	begin.videoSessionParameters = probe->parameters;
	begin.referenceSlotCount = 1U;
	begin.pReferenceSlots = &bound;
	vkCmdBeginVideoCodingKHR(probe->command, &begin);
	memset(&control, 0, sizeof(control));
	control.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
	control.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
	vkCmdControlVideoCodingKHR(probe->command, &control);

	/* The decode: the slices, the image as its output and as slot 0's picture. */
	memset(&reference, 0, sizeof(reference));
	reference.FrameNum = picture->info.frame_num;
	reference.PicOrderCnt[0] = picture->info.PicOrderCnt[0];
	reference.PicOrderCnt[1] = picture->info.PicOrderCnt[1];
	memset(&slot_info, 0, sizeof(slot_info));
	slot_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
	slot_info.pStdReferenceInfo = &reference;
	memset(&setup, 0, sizeof(setup));
	setup.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
	setup.pNext = &slot_info;
	setup.slotIndex = 0;
	setup.pPictureResource = &resource;
	memset(&h264, 0, sizeof(h264));
	h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
	h264.pStdPictureInfo = &picture->info;
	h264.sliceCount = picture->slice_count;
	h264.pSliceOffsets = offsets;
	memset(&decode, 0, sizeof(decode));
	decode.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
	decode.pNext = &h264;
	decode.srcBuffer = probe->buffer;
	decode.srcBufferOffset = 0U;
	decode.srcBufferRange = at;
	decode.dstPictureResource = resource;
	decode.pSetupReferenceSlot = &setup;
	vkCmdDecodeVideoKHR(probe->command, &decode);

	/* The scope's end. */
	memset(&end, 0, sizeof(end));
	end.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
	vkCmdEndVideoCodingKHR(probe->command, &end);
	result = vkEndCommandBuffer(probe->command);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkEndCommandBuffer");

	/* Submits it and waits for it. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &probe->command;
	result = vkQueueSubmit(probe->queue, 1U, &submit, probe->fence);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkQueueSubmit");
	result = vkWaitForFences(probe->device, 1U, &probe->fence, VK_TRUE, PROBE_WAIT_NS);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkWaitForFences");

	/* The fence and the buffer for the next picture. */
	result = vkResetFences(probe->device, 1U, &probe->fence);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkResetFences");
	result = vkResetCommandBuffer(probe->command, 0U);
	if (result != VK_SUCCESS)
		return probe_failed(result, "vkResetCommandBuffer");

	/* Succeeded: the picture is in the image. */
	return 0;
}

/* Hashes the image's display window of a sequence: its planes from the image's layout, the cropping of the sequence. */
static void
probe_hash(
	struct probe *probe,
	const StdVideoH264SequenceParameterSet *sps,
	char text[65])
{
	VkImageSubresource subresource;
	VkSubresourceLayout luma;
	VkSubresourceLayout chroma;
	struct frame_planes planes;
	struct frame_window window;

	/* The two planes. */
	memset(&subresource, 0, sizeof(subresource));
	subresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
	vkGetImageSubresourceLayout(probe->device, probe->image, &subresource, &luma);
	subresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
	vkGetImageSubresourceLayout(probe->device, probe->image, &subresource, &chroma);
	planes.luma = probe->image_map + luma.offset;
	planes.luma_pitch = (size_t)luma.rowPitch;
	planes.chroma = probe->image_map + chroma.offset;
	planes.chroma_pitch = (size_t)chroma.rowPitch;

	/* The window: the coded picture less the cropping, two pixels a crop unit (4:2:0 frames). */
	window.x = 2U * sps->frame_crop_left_offset;
	window.y = 2U * sps->frame_crop_top_offset;
	window.width = (sps->pic_width_in_mbs_minus1 + 1U) * 16U - 2U * (sps->frame_crop_left_offset + sps->frame_crop_right_offset);
	window.height = (sps->pic_height_in_map_units_minus1 + 1U) * 16U - 2U * (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset);
	frame_hash(&planes, &window, text);
}

/*
 * Decodes the stream: the device, the decoder's objects, then picture
 * after picture, printing each frame's hash (and whether it matches the
 * expected one).
 */
static int
probe_run(
	struct probe *probe,
	const struct probe_options *options)
{
	static struct h264_stream stream;
	static struct h264_picture picture;
	const char *reason;
	uint8_t *data;
	size_t size;
	uint32_t index;
	uint32_t frames;
	uint32_t matched;
	char expected[80];
	char text[65];
	FILE *expect;
	int found;
	int first_sps;
	int error;

	/* The stream and its parameter sets. */
	data = probe_read(options->stream, &size);
	if (data == NULL)
		return 1;
	error = h264_open(&stream, data, size);
	if (error != 0) {
		fprintf(stderr, "vkvideo-probe: %s: a parameter set the probe cannot read\n", options->stream);
		free(data);
		return 1;
	}

	/* The profile of the first sequence set, and the largest coded picture of all of them. */
	first_sps = -1;
	for (index = 0U; index < H264_SPS_IDS; index++) {
		if (!stream.has_sps[index])
			continue;
		if (first_sps < 0)
			first_sps = (int)index;
		if ((stream.sps[index].pic_width_in_mbs_minus1 + 1U) * 16U > probe->extent.width)
			probe->extent.width = (stream.sps[index].pic_width_in_mbs_minus1 + 1U) * 16U;
		if ((stream.sps[index].pic_height_in_map_units_minus1 + 1U) * 16U > probe->extent.height)
			probe->extent.height = (stream.sps[index].pic_height_in_map_units_minus1 + 1U) * 16U;
	}
	if (first_sps < 0) {
		fprintf(stderr, "vkvideo-probe: %s: no sequence parameter set\n", options->stream);
		free(data);
		return 1;
	}
	memset(&probe->h264_profile, 0, sizeof(probe->h264_profile));
	probe->h264_profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
	probe->h264_profile.stdProfileIdc = stream.sps[first_sps].profile_idc;
	probe->h264_profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
	memset(&probe->profile, 0, sizeof(probe->profile));
	probe->profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
	probe->profile.pNext = &probe->h264_profile;
	probe->profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
	probe->profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
	probe->profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
	probe->profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
	memset(&probe->profile_list, 0, sizeof(probe->profile_list));
	probe->profile_list.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
	probe->profile_list.profileCount = 1U;
	probe->profile_list.pProfiles = &probe->profile;
	printf("vkvideo-probe: stream %s profile %u, %ux%u\n", options->stream, (unsigned)probe->h264_profile.stdProfileIdc, probe->extent.width, probe->extent.height);

	/* The device and the decoder's objects. */
	error = probe_device(probe);
	if (error == 0)
		error = probe_capabilities(probe);
	if (error == 0)
		error = probe_picture(probe);
	if (error == 0)
		error = probe_bitstream(probe, size + 3U * H264_MAX_SLICES);
	if (error == 0)
		error = probe_session(probe);
	if (error == 0)
		error = probe_parameters(probe, &stream);
	if (error == 0)
		error = probe_commands(probe);
	if (error != 0) {
		free(data);
		return 1;
	}

	/* The expected hashes, when given. */
	expect = NULL;
	if (options->expect != NULL) {
		expect = fopen(options->expect, "r");
		if (expect == NULL) {
			fprintf(stderr, "vkvideo-probe: %s: %s\n", options->expect, strerror(errno));
			free(data);
			return 1;
		}
	}

	/* Picture after picture. */
	frames = 0U;
	matched = 0U;
	for (;;) {
		/* The next picture, or the end; a picture the probe cannot decode stops it. */
		if (options->frames != 0U && frames >= options->frames)
			break;
		found = h264_next_picture(&stream, &picture, &reason);
		if (found == 0)
			break;
		if (found < 0) {
			fprintf(stderr, "vkvideo-probe: picture %u: %s\n", frames, reason);
			error = 3;
			break;
		}
		if (!picture.intra || !picture.info.flags.IdrPicFlag) {
			fprintf(stderr, "vkvideo-probe: picture %u is not an IDR intra picture (P and B pictures are ws083-p006's)\n", frames);
			error = 3;
			break;
		}

		/* Decodes it and hashes the frame. */
		error = probe_decode(probe, &stream, &picture, frames == 0U);
		if (error != 0)
			break;
		probe_hash(probe, &stream.sps[picture.info.seq_parameter_set_id], text);

		/* Compares it with the expected line. */
		if (expect != NULL && fgets(expected, sizeof(expected), expect) != NULL) {
			expected[strcspn(expected, "\n")] = '\0';
			if (strcmp(expected, text) == 0) {
				matched++;
				printf("vkvideo-probe: frame %u %s match\n", frames, text);
			} else {
				printf("vkvideo-probe: frame %u %s MISMATCH (expected %s)\n", frames, text, expected);
			}
		} else {
			printf("vkvideo-probe: frame %u %s\n", frames, text);
		}
		frames++;
	}
	if (expect != NULL)
		fclose(expect);
	free(data);

	/* The summary: every frame decoded, and matched when expected. */
	printf("vkvideo-probe: %u frames decoded", frames);
	if (options->expect != NULL)
		printf(", %u match the reference", matched);
	printf("\n");
	if (error != 0)
		return error;
	if (options->expect != NULL && (matched != frames || frames == 0U))
		return 4;

	/* Succeeded: the stream is decoded. */
	return 0;
}

/* Destroys what a run made, in reverse. */
static void
probe_close(
	struct probe *probe)
{
	uint32_t index;

	/* The device's objects, then the device. */
	if (probe->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(probe->device);
		if (probe->fence != VK_NULL_HANDLE)
			vkDestroyFence(probe->device, probe->fence, NULL);
		if (probe->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(probe->device, probe->pool, NULL);
		if (probe->parameters != VK_NULL_HANDLE)
			vkDestroyVideoSessionParametersKHR(probe->device, probe->parameters, NULL);
		if (probe->session != VK_NULL_HANDLE)
			vkDestroyVideoSessionKHR(probe->device, probe->session, NULL);
		for (index = 0U; index < probe->session_memory_count; index++)
			vkFreeMemory(probe->device, probe->session_memory[index], NULL);
		if (probe->buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(probe->device, probe->buffer, NULL);
		if (probe->buffer_memory != VK_NULL_HANDLE)
			vkFreeMemory(probe->device, probe->buffer_memory, NULL);
		if (probe->view != VK_NULL_HANDLE)
			vkDestroyImageView(probe->device, probe->view, NULL);
		if (probe->image != VK_NULL_HANDLE)
			vkDestroyImage(probe->device, probe->image, NULL);
		if (probe->image_memory != VK_NULL_HANDLE)
			vkFreeMemory(probe->device, probe->image_memory, NULL);
		vkDestroyDevice(probe->device, NULL);
	}

	/* The instance. */
	if (probe->instance != VK_NULL_HANDLE)
		vkDestroyInstance(probe->instance, NULL);
}

/* Reports a failed Vulkan call and gives the run's failure status. */
static int
probe_failed(
	VkResult result,
	const char *what)
{
	/* The call and its result. */
	fprintf(stderr, "vkvideo-probe: %s failed: %d\n", what, (int)result);
	return 1;
}

/* Reads a whole file; NULL (with a message) when it cannot. */
static uint8_t *
probe_read(
	const char *path,
	size_t *size)
{
	FILE *file;
	uint8_t *data;
	long length;
	size_t got;

	/* Opens it and finds its length. */
	file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "vkvideo-probe: %s: %s\n", path, strerror(errno));
		return NULL;
	}
	fseek(file, 0L, SEEK_END);
	length = ftell(file);
	fseek(file, 0L, SEEK_SET);
	if (length <= 0) {
		fprintf(stderr, "vkvideo-probe: %s: empty\n", path);
		fclose(file);
		return NULL;
	}

	/* Its bytes. */
	data = malloc((size_t)length);
	if (data == NULL) {
		fclose(file);
		return NULL;
	}
	got = fread(data, 1U, (size_t)length, file);
	fclose(file);
	if (got != (size_t)length) {
		free(data);
		return NULL;
	}

	/* Succeeded: the file's bytes. */
	*size = got;
	return data;
}
