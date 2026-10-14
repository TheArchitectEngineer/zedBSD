/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Standard-command stand-ins inspect production decode/readback recording; they never emulate GPU decoding. */
#include "userland/desktop/libmedia/vkvideo-runtime.c"
#include <assert.h>
#include <stdio.h>

/* One serial host test owns this recording observation state until its final assertions. */
static struct vkvideo_runtime *observed;
/* Recorded transfers and queue waits are counted only for this host observation. */
static unsigned copies_seen;
static unsigned submissions;
static unsigned video_waits;
static unsigned transfer_waits;
static int wait_failure;
/* The create flags the format query reports, and the GPU decodes submitted with their own fence (ws236-p011). */
static VkImageCreateFlags format_flags;
static unsigned gpu_submissions;

static void host_admission(void);
static VkResult host_capabilities(VkPhysicalDevice physical, const VkVideoProfileInfoKHR *profile, VkVideoCapabilitiesKHR *capabilities);
static VkResult host_formats(VkPhysicalDevice physical, const VkPhysicalDeviceVideoFormatInfoKHR *info, uint32_t *count, VkVideoFormatPropertiesKHR *formats);

/* Capability masks belong only to the bounded host query fixture. */
static VkVideoDecodeCapabilityFlagsKHR available_models;
static VkImageUsageFlags refused_usage;

static VkResult host_begin(VkCommandBuffer command, const VkCommandBufferBeginInfo *info);
static VkResult host_end(VkCommandBuffer command);
static VkResult host_reset_command(VkCommandBuffer command, VkCommandBufferResetFlags flags);
static VkResult host_reset_fence(VkDevice device, uint32_t count, const VkFence *fences);
static VkResult host_wait(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout);
static VkResult host_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submit, VkFence fence);
static void host_begin_video(VkCommandBuffer command, const VkVideoBeginCodingInfoKHR *info);
static void host_control(VkCommandBuffer command, const VkVideoCodingControlInfoKHR *info);
static void host_decode(VkCommandBuffer command, const VkVideoDecodeInfoKHR *info);
static void host_end_video(VkCommandBuffer command, const VkVideoEndCodingInfoKHR *info);
static void host_barrier(VkCommandBuffer command, const VkDependencyInfo *info);
static void host_copy(VkCommandBuffer command, VkImage image, VkImageLayout layout, VkBuffer buffer, uint32_t count, const VkBufferImageCopy *copy);
static void host_gpu_admission(void);
static void host_gpu_pictures(struct h264_stream *stream, struct h264_picture *picture, struct h264_dpb_plan *plan);

/*
 * Exercises coincident and distinct output recording with separate video and transfer queues.
 */
int
main(
	void)
{
	struct vkvideo_runtime video;
	struct h264_stream *stream;
	struct h264_picture *picture;
	struct h264_dpb_plan plan;
	struct media_picture_pool *pool;
	struct media_picture *output;
	struct vkvideo_flight *flight;
	uint8_t bytes[4];
	uint32_t ticket;
	unsigned mode;
	unsigned frame;
	int error;

	/* Query-driven allocation must admit complete usages and honor profile limits. */
	host_admission();

	/* Production parsing and driver pixel decoding are tested elsewhere; this fixture observes API contracts. */
	stream = calloc(1U, sizeof(*stream));
	assert(stream != NULL);
	picture = calloc(1U, sizeof(*picture));
	assert(picture != NULL);
	pool = media_picture_pool_create(32U, 16U, 2U);
	assert(pool != NULL);
	output = media_picture_pool_get(pool);
	assert(output != NULL);
	bytes[0] = 0x65U;
	bytes[1] = 0x88U;
	bytes[2] = 0x80U;
	bytes[3] = 0x80U;
	stream->data = bytes;
	stream->size = sizeof(bytes);
	stream->sps[0].pic_width_in_mbs_minus1 = 1U;
	stream->sps[0].pic_height_in_map_units_minus1 = 0U;
	picture->slice_count = 1U;
	picture->slice_sizes[0] = sizeof(bytes);
	picture->info.flags.is_reference = 1;
	memset(&plan, 0, sizeof(plan));
	plan.decode = 1;
	plan.setup = 0;
	plan.reset = 1;
	copies_seen = 0U;
	submissions = 0U;
	video_waits = 0U;
	transfer_waits = 0U;
	for (mode = 0U; mode < 2U; mode++) {
		memset(&video, 0, sizeof(video));
		observed = &video;
		video.extent.width = 32U;
		video.extent.height = 16U;
		video.slots = 2U;
		video.distinct = (int)mode;
		video.capabilities.minBitstreamBufferSizeAlignment = 32U;
		video.flights = 1U;
		flight = &video.flight[0];
		flight->buffer_size = 4096U;
		flight->buffer_map = calloc(1U, 4096U);
		assert(flight->buffer_map != NULL);
		flight->readback_map = calloc(1U, 768U);
		assert(flight->readback_map != NULL);
		flight->buffer_coherent = 1;
		flight->readback_coherent = 1;
		flight->command = (VkCommandBuffer)(uintptr_t)1U;
		flight->transfer_command = (VkCommandBuffer)(uintptr_t)2U;
		video.queue = (VkQueue)(uintptr_t)3U;
		video.transfer_queue = (VkQueue)(uintptr_t)4U;
		flight->decoded = (VkSemaphore)5U;
		flight->fence = (VkFence)7U;
		video.fn.vkBeginCommandBuffer = host_begin;
		video.fn.vkEndCommandBuffer = host_end;
		video.fn.vkResetCommandBuffer = host_reset_command;
		video.fn.vkResetFences = host_reset_fence;
		video.fn.vkWaitForFences = host_wait;
		video.fn.vkQueueSubmit = host_submit;
		video.fn.vkCmdBeginVideoCodingKHR = host_begin_video;
		video.fn.vkCmdControlVideoCodingKHR = host_control;
		video.fn.vkCmdDecodeVideoKHR = host_decode;
		video.fn.vkCmdEndVideoCodingKHR = host_end_video;
		video.fn.vkCmdPipelineBarrier2KHR = host_barrier;
		video.fn.vkCmdCopyImageToBuffer = host_copy;
		for (frame = 0U; frame < 3U; frame++) {
			plan.reset = 0;
			if (frame == 0U)
				plan.reset = 1;
			error = media_vkvideo_runtime_submit(&video, stream, picture, &plan, output, &ticket);
			assert(error == 0);
			assert(ticket == frame + 1U);
			error = media_vkvideo_runtime_finish(&video, ticket);
			assert(error == 0);
			assert(flight->state == VKVIDEO_FREE);
			assert(output->luma[31] == 47U);
			assert(output->chroma[0] == 100U);
			assert(output->chroma[1] == 150U);
		}

		/* A fence timeout must leave the submitted runtime resources owned and in flight. */
		wait_failure = 1;
		error = media_vkvideo_runtime_submit(&video, stream, picture, &plan, output, &ticket);
		assert(error == 0);
		error = media_vkvideo_runtime_finish(&video, ticket);
		assert(error == ETIMEDOUT);
		assert(flight->state == VKVIDEO_READING);
		assert(video.failed == ETIMEDOUT);
		wait_failure = 0;
		free(flight->readback_map);
		free(flight->buffer_map);
	}

	/* With one picture in flight, each decode's readback is submitted at once, behind the decode's semaphore; no reverse dependency is needed. */
	assert(copies_seen == 8U);
	assert(submissions == 16U);
	assert(video_waits == 0U);
	assert(transfer_waits == 8U);
	media_picture_unref(output);
	media_picture_pool_close(pool);

	/* Pictures the GPU holds: their admission, and their decode with its own fence and the hold of their images. */
	host_gpu_admission();
	host_gpu_pictures(stream, picture, &plan);
	free(picture);
	free(stream);
	puts("Vulkan Video runtime host PASS (coincident/distinct, two queues, standard planes, timeout retention, GPU pictures)");
	return 0;
}

/*
 * The host harness consumes library diagnostics without requiring a desktop engine.
 */
void
media_log(
	const char *format,
	...)
{
	(void)format;
}

/*
 * Asks for pictures in the CPU's memory, as the host harness reads them back.
 */
unsigned
media_decoder_gpu_hold(
	void)
{
	return 0U;
}

/* A command must be recorded in its own family-specific pool. */
static VkResult
host_begin(
	VkCommandBuffer command,
	const VkCommandBufferBeginInfo *info)
{
	assert(command == observed->flight[0].command || command == observed->flight[0].transfer_command);
	assert(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	return VK_SUCCESS;
}

/* End recording without executing fake commands on a physical device. */
static VkResult
host_end(
	VkCommandBuffer command)
{
	(void)command;
	return VK_SUCCESS;
}

/* Command reset occurs only after the transfer fence completed. */
static VkResult
host_reset_command(
	VkCommandBuffer command,
	VkCommandBufferResetFlags flags)
{
	(void)command;
	(void)flags;
	assert(observed->flight[0].state == VKVIDEO_READING);
	return VK_SUCCESS;
}

/* Fence reset also requires retired memory. */
static VkResult
host_reset_fence(
	VkDevice device,
	uint32_t count,
	const VkFence *fences)
{
	(void)device;
	(void)fences;
	assert(count == 1U);
	assert(observed->flight[0].state == VKVIDEO_READING);
	return VK_SUCCESS;
}

/* A failed wait must preserve ownership instead of exposing the recording stand-in's bytes as GPU output. */
static VkResult
host_wait(
	VkDevice device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout)
{
	(void)device;
	(void)fences;
	assert(count == 1U);
	assert(all == VK_TRUE);
	assert(timeout == VKVIDEO_WAIT_NS);
	if (wait_failure)
		return VK_TIMEOUT;
	return VK_SUCCESS;
}

/* Inspect binary semaphore direction and ensure only the transfer submission signals the host fence. */
static VkResult
host_submit(
	VkQueue queue,
	uint32_t count,
	const VkSubmitInfo *submit,
	VkFence fence)
{
	assert(count == 1U);
	assert(submit->commandBufferCount == 1U);
	if (queue == observed->queue && observed->gpu) {
		/* A GPU picture's decode carries the fence and nothing waits for it. */
		assert(fence == observed->flight[0].fence);
		assert(submit->pCommandBuffers[0] == observed->flight[0].command);
		assert(submit->signalSemaphoreCount == 0U);
		assert(submit->waitSemaphoreCount == 0U);
		gpu_submissions++;
	} else if (queue == observed->queue) {
		assert(fence == VK_NULL_HANDLE);
		assert(submit->pCommandBuffers[0] == observed->flight[0].command);
		assert(submit->signalSemaphoreCount == 1U);
		assert(submit->pSignalSemaphores[0] == observed->flight[0].decoded);
		if (submit->waitSemaphoreCount != 0U)
			video_waits++;
	} else {
		assert(queue == observed->transfer_queue);
		assert(fence == observed->flight[0].fence);
		assert(submit->pCommandBuffers[0] == observed->flight[0].transfer_command);
		assert(submit->signalSemaphoreCount == 0U);
		assert(submit->waitSemaphoreCount == 1U);
		assert(submit->pWaitSemaphores[0] == observed->flight[0].decoded);
		assert(submit->pWaitDstStageMask[0] == VK_PIPELINE_STAGE_TRANSFER_BIT);
		transfer_waits++;
	}

	/* Count each observed standard queue submission. */
	submissions++;
	return VK_SUCCESS;
}

/* Video coding scope is recorded only on the video command. */
static void
host_begin_video(
	VkCommandBuffer command,
	const VkVideoBeginCodingInfoKHR *info)
{
	assert(command == observed->flight[0].command);
	assert(info->referenceSlotCount >= 1U);
}

/* Initial and seek reset remain standard coding controls. */
static void
host_control(
	VkCommandBuffer command,
	const VkVideoCodingControlInfoKHR *info)
{
	assert(command == observed->flight[0].command);
	assert(info->flags == VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR);
}

/* Capture aligned source range and the separate destination view used by distinct-output devices. */
static void
host_decode(
	VkCommandBuffer command,
	const VkVideoDecodeInfoKHR *info)
{
	assert(command == observed->flight[0].command);
	assert(info->srcBufferOffset == 0U);
	assert(info->srcBufferRange == 32U);
	assert(info->pSetupReferenceSlot->slotIndex == 0);
	assert(info->dstPictureResource.codedExtent.width == 32U);
	assert(info->dstPictureResource.codedExtent.height == 16U);
}

/* End the decode scope before recording any transfer command. */
static void
host_end_video(
	VkCommandBuffer command,
	const VkVideoEndCodingInfoKHR *info)
{
	assert(command == observed->flight[0].command);
	assert(info->sType == VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR);
}

/* Dedicated transfer commands may never name video stages; semaphore dependencies cross those queues. */
static void
host_barrier(
	VkCommandBuffer command,
	const VkDependencyInfo *info)
{
	unsigned index;

	/* Check each image barrier's stage validity for its recorded queue. */
	for (index = 0U; index < info->imageMemoryBarrierCount; index++) {
		if (command == observed->flight[0].transfer_command) {
			assert((info->pImageMemoryBarriers[index].srcStageMask & VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR) == 0U);
			assert((info->pImageMemoryBarriers[index].dstStageMask & VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR) == 0U);
		}
	}

	/* Require buffer host-visibility barriers to be recorded on the transfer command. */
	if (info->bufferMemoryBarrierCount != 0U) {
		assert(command == observed->flight[0].transfer_command);
		assert(info->pBufferMemoryBarriers[0].dstAccessMask == VK_ACCESS_2_HOST_READ_BIT);
	}
}

/* Populate a transfer buffer stand-in with a visible pattern after checking exact standard plane/sample coordinates. */
static void
host_copy(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	VkBuffer buffer,
	uint32_t count,
	const VkBufferImageCopy *copy)
{
	unsigned index;

	/* Inspect exact plane-copy geometry before writing only host stand-in output. */
	(void)image;
	(void)buffer;
	assert(command == observed->flight[0].transfer_command);
	assert(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	assert(count == 2U);
	assert(copy[0].imageSubresource.aspectMask == VK_IMAGE_ASPECT_PLANE_0_BIT);
	assert(copy[0].imageExtent.width == 32U);
	assert(copy[0].imageExtent.height == 16U);
	assert(copy[1].imageSubresource.aspectMask == VK_IMAGE_ASPECT_PLANE_1_BIT);
	assert(copy[1].imageExtent.width == 16U);
	assert(copy[1].imageExtent.height == 8U);
	assert(copy[1].bufferOffset == 512U);
	for (index = 0U; index < 512U; index++)
		observed->flight[0].readback_map[index] = (uint8_t)(16U + index % 32U);
	for (index = 512U; index < 768U; index += 2U) {
		observed->flight[0].readback_map[index] = 100U;
		observed->flight[0].readback_map[index + 1U] = 150U;
	}

	/* Count completed copy recordings independently of submission retirement. */
	copies_seen++;
}

/* Check the actual production capability/format path with standard API responses. */
static void
host_admission(
	void)
{
	struct vkvideo_runtime video;
	int error;

	/* A coincident image requires all three usages in one query. */
	memset(&video, 0, sizeof(video));
	video.fn.vkGetPhysicalDeviceVideoCapabilitiesKHR = host_capabilities;
	video.fn.vkGetPhysicalDeviceVideoFormatPropertiesKHR = host_formats;
	video.extent.width = 320U;
	video.extent.height = 192U;
	video.slots = 4U;
	video.max_references = 3U;
	video.level = STD_VIDEO_H264_LEVEL_IDC_4_1;
	available_models = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
	refused_usage = 0U;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.distinct == 0);
	assert((video.reference_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0U);

	/* Reject the combined allocation, then admit distinct reference and transferable output images. */
	available_models |= VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;
	refused_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.distinct == 1);
	assert(video.reference_usage == VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR);

	/* A missing transferable output and a level beyond the device's maximum are profile errors, not GPU-specific fallbacks. */
	available_models = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;
	refused_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	error = vkvideo_capabilities(&video);
	assert(error == ENOTSUP);
	refused_usage = 0U;
	video.level = STD_VIDEO_H264_LEVEL_IDC_5_1;
	error = vkvideo_capabilities(&video);
	assert(error == ENOTSUP);
}

/* Publish one progressive eight-bit profile's standard extent, DPB and level limits. */
static VkResult
host_capabilities(
	VkPhysicalDevice physical,
	const VkVideoProfileInfoKHR *profile,
	VkVideoCapabilitiesKHR *capabilities)
{
	VkVideoDecodeCapabilitiesKHR *decode;
	VkVideoDecodeH264CapabilitiesKHR *h264;

	/* The chain is caller-owned and retains its declared structure identities. */
	(void)physical;
	(void)profile;
	decode = (VkVideoDecodeCapabilitiesKHR *)capabilities->pNext;
	h264 = (VkVideoDecodeH264CapabilitiesKHR *)decode->pNext;
	decode->flags = available_models;
	h264->maxLevelIdc = STD_VIDEO_H264_LEVEL_IDC_4_1;
	capabilities->minCodedExtent.width = 16U;
	capabilities->minCodedExtent.height = 16U;
	capabilities->maxCodedExtent.width = 4096U;
	capabilities->maxCodedExtent.height = 4096U;
	capabilities->maxDpbSlots = 17U;
	capabilities->maxActiveReferencePictures = 16U;
	capabilities->minBitstreamBufferSizeAlignment = 64U;
	capabilities->pictureAccessGranularity.width = 16U;
	capabilities->pictureAccessGranularity.height = 16U;
	return VK_SUCCESS;
}

/* Return a format only for the complete image usage requested by production code. */
static VkResult
host_formats(
	VkPhysicalDevice physical,
	const VkPhysicalDeviceVideoFormatInfoKHR *info,
	uint32_t *count,
	VkVideoFormatPropertiesKHR *formats)
{
	/* Counting and filling have identical admission semantics. */
	(void)physical;
	*count = 0U;
	if (info->imageUsage == refused_usage)
		return VK_SUCCESS;
	*count = 1U;
	if (formats == NULL)
		return VK_SUCCESS;
	formats->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	formats->imageType = VK_IMAGE_TYPE_2D;
	formats->imageTiling = VK_IMAGE_TILING_OPTIMAL;
	formats->imageUsageFlags = info->imageUsage;
	formats->imageCreateFlags = format_flags;
	return VK_SUCCESS;
}

/* Checks when a context gives pictures the GPU holds and how many slots it then has. */
static void
host_gpu_admission(
	void)
{
	struct vkvideo_runtime video;
	int error;

	/* Asked for, exportable and with plane views: the slots hold every picture. */
	memset(&video, 0, sizeof(video));
	video.fn.vkGetPhysicalDeviceVideoCapabilitiesKHR = host_capabilities;
	video.fn.vkGetPhysicalDeviceVideoFormatPropertiesKHR = host_formats;
	video.extent.width = 320U;
	video.extent.height = 192U;
	video.max_references = 3U;
	video.reorder = 2U;
	video.gpu_hold = 4U;
	video.gpu_extensions = 1;
	video.level = STD_VIDEO_H264_LEVEL_IDC_4_1;
	available_models = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
	refused_usage = 0U;
	format_flags = VKVIDEO_PLANE_FLAGS;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.gpu == 1);
	assert(video.slots == 3U + 2U + video.flights + 4U + 1U);
	assert((video.reference_usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U);

	/* Without the plane views' flags the pictures are read back. */
	format_flags = 0U;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.gpu == 0);
	assert(video.slots == video.max_references + video.flights);

	/* Without the export, or when the held pictures need more slots than the device has, too. */
	format_flags = VKVIDEO_PLANE_FLAGS;
	video.gpu_extensions = 0;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.gpu == 0);
	video.gpu_extensions = 1;
	video.gpu_hold = 12U;
	error = vkvideo_capabilities(&video);
	assert(error == 0 && video.gpu == 0);
	format_flags = 0U;
}

/*
 * Decodes pictures the GPU holds: no readback is recorded, the decode
 * carries the fence, and each picture holds its image until it is freed.
 */
static void
host_gpu_pictures(
	struct h264_stream *stream,
	struct h264_picture *picture,
	struct h264_dpb_plan *plan)
{
	struct vkvideo_runtime video;
	struct media_picture_pool *pool;
	struct media_picture *output;
	struct media_gpu_picture described;
	struct vkvideo_flight *flight;
	uint32_t ticket;
	unsigned copies;
	unsigned transfers;
	int error;

	/* One flight, two images, the first being decoded into. */
	memset(&video, 0, sizeof(video));
	observed = &video;
	video.extent.width = 32U;
	video.extent.height = 16U;
	video.image_extent = video.extent;
	video.slots = 2U;
	video.gpu = 1;
	video.capabilities.minBitstreamBufferSizeAlignment = 32U;
	video.flights = 1U;
	video.gpu_slots = media_gpu_slots_create(2U);
	assert(video.gpu_slots != NULL);
	video.gpu_slots->identity = 7U;
	video.gpu_slots->memory_bytes = 4096U;
	video.gpu_slots->image_width = 32U;
	video.gpu_slots->image_height = 16U;
	flight = &video.flight[0];
	flight->buffer_size = 4096U;
	flight->buffer_map = calloc(1U, 4096U);
	assert(flight->buffer_map != NULL);
	flight->buffer_coherent = 1;
	flight->command = (VkCommandBuffer)(uintptr_t)1U;
	flight->transfer_command = (VkCommandBuffer)(uintptr_t)2U;
	video.queue = (VkQueue)(uintptr_t)3U;
	video.transfer_queue = (VkQueue)(uintptr_t)3U;
	flight->decoded = (VkSemaphore)5U;
	flight->fence = (VkFence)7U;
	video.fn.vkBeginCommandBuffer = host_begin;
	video.fn.vkEndCommandBuffer = host_end;
	video.fn.vkResetCommandBuffer = host_reset_command;
	video.fn.vkResetFences = host_reset_fence;
	video.fn.vkWaitForFences = host_wait;
	video.fn.vkQueueSubmit = host_submit;
	video.fn.vkCmdBeginVideoCodingKHR = host_begin_video;
	video.fn.vkCmdControlVideoCodingKHR = host_control;
	video.fn.vkCmdDecodeVideoKHR = host_decode;
	video.fn.vkCmdEndVideoCodingKHR = host_end_video;
	video.fn.vkCmdPipelineBarrier2KHR = host_barrier;
	video.fn.vkCmdCopyImageToBuffer = host_copy;
	pool = media_picture_pool_create_gpu(32U, 16U, 2U);
	assert(pool != NULL);
	output = media_picture_pool_get(pool);
	assert(output != NULL && output->luma == NULL);

	/* The decode: its fence, no copy, and its image held from the submission. */
	copies = copies_seen;
	transfers = transfer_waits;
	plan->setup = 0;
	plan->reset = 1;
	error = media_vkvideo_runtime_submit(&video, stream, picture, plan, output, &ticket);
	assert(error == 0);
	assert(media_vkvideo_runtime_busy(&video) == 1U);
	error = media_vkvideo_runtime_finish(&video, ticket);
	assert(error == 0);
	assert(flight->state == VKVIDEO_FREE);
	assert(gpu_submissions == 1U);
	assert(copies_seen == copies && transfer_waits == transfers);

	/* The picture describes its image, and holds it until it is freed. */
	error = media_picture_gpu(output, &described);
	assert(error == 0);
	assert(described.identity == 7U && described.slot == 0U);
	assert(described.image_width == 32U && described.width == 32U);
	assert(media_vkvideo_runtime_busy(&video) == 1U);
	media_picture_unref(output);
	assert(media_vkvideo_runtime_busy(&video) == 0U);

	/* The context's reference is the last one. */
	media_gpu_slots_unref(video.gpu_slots);
	media_picture_pool_close(pool);
	free(flight->buffer_map);
}
