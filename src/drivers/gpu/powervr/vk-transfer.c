/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's transfer commands: vkCmdCopyBufferToImage,
 * vkCmdCopyImageToBuffer, vkCmdCopyImage, vkCmdBlitImage of the same size
 * and format, and vkCmdClearColorImage, recorded with their regions and
 * run as transfers on the session's transfer context.
 *
 * Each region names level 0 and layer 0 of its image; other subresources
 * and scaled or converting blits are refused (ENOTSUP) when recorded.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/powervr/transfer-job.h"
#include "drivers/gpu/powervr/vk.h"
#include "drivers/gpu/i915/render/vulkan-codec.inc"

/* The most regions one transfer command records. */
#define TRANSFER_REGIONS_MAX		8U

/* What a transfer record keeps: its regions, and a clear's colour. */
struct transfer_data {
	uint32_t count;
	uint32_t colour[4];
	union {
		VkImageCopy copies[TRANSFER_REGIONS_MAX];
		VkImageBlit blits[TRANSFER_REGIONS_MAX];
		VkBufferImageCopy buffers[TRANSFER_REGIONS_MAX];
		VkImageSubresourceRange ranges[TRANSFER_REGIONS_MAX];
	} regions;
};

/* The working state of one run: the request and the views it names. */
struct transfer_work {
	struct powervr_transfer_request request;
	struct powervr_view_shape view;
	struct powervr_image_layout layout;
};

static int hold(struct powervr_vk_session *session, struct powervr_vk_record *entry, enum i915_vk_object_kind kind, uint64_t identity);
static int first_subresource(uint32_t level, uint32_t layer, uint32_t count);
static int image_surface(const struct powervr_vk_image *image, struct powervr_render_surface *surface);
static int image_view(const struct powervr_vk_image *image, struct powervr_view_shape *view);
static int run_one(struct powervr_vk_session *session, struct powervr_transfer_request *request);

/*
 * Records one transfer command: its objects (the source as object 0,
 * the destination as object 1; a clear's image alone) and its regions.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/amdrdna/vk/vk-record.c
 * (record_copy, record_clear: the same wire forms).
 */
int
powervr_vk_record_transfer(
	struct powervr_vk_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader,
	struct powervr_vk_record *entry)
{
	struct transfer_data *data;
	enum i915_vk_object_kind source_kind;
	enum i915_vk_object_kind destination_kind;
	uint64_t source;
	uint64_t destination;
	uint64_t count;
	uint64_t index;
	VkImageBlit *blit;
	int refused;
	int error;

	/* The record's data. */
	data = kern_calloc(1, sizeof(*data));
	if (data == NULL)
		return ENOMEM;
	entry->data = data;

	/* A clear: [image][layout][present]{colour}[count][count]{range}. */
	if (opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
		destination = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		for (index = 0; index < 4U; index++)
			data->colour[index] = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		if (reader->error != 0 || count > TRANSFER_REGIONS_MAX)
			return EINVAL;
		for (index = 0; index < count; index++)
			i915_vkc_dec_VkImageSubresourceRange(reader, &session->arena, &data->regions.ranges[index]);
		data->count = (uint32_t)count;
		if (reader->error != 0)
			return EINVAL;
		error = hold(session, entry, I915_VK_OBJ_IMAGE, destination);
		if (error != 0)
			return error;

		/* Each range: level 0, layer 0 only. */
		for (index = 0; index < count; index++) {
			error = first_subresource(data->regions.ranges[index].baseMipLevel, data->regions.ranges[index].baseArrayLayer, data->regions.ranges[index].layerCount);
			if (error != 0)
				return error;
		}
		return 0;
	}

	/* The two resources, as each command lays them out. */
	source_kind = I915_VK_OBJ_IMAGE;
	destination_kind = I915_VK_OBJ_IMAGE;
	if (opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE) {
		source_kind = I915_VK_OBJ_BUFFER;
		source = drv_i915_wire_read_u64(reader);
		destination = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
	} else if (opcode == GPU_OP_CMD_COPY_IMAGE_TO_BUFFER) {
		destination_kind = I915_VK_OBJ_BUFFER;
		source = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		destination = drv_i915_wire_read_u64(reader);
	} else {
		source = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		destination = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
	}

	/* The regions. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > TRANSFER_REGIONS_MAX)
		return EINVAL;
	for (index = 0; index < count; index++) {
		if (opcode == GPU_OP_CMD_COPY_IMAGE) {
			i915_vkc_dec_VkImageCopy(reader, &session->arena, &data->regions.copies[index]);
		} else if (opcode == GPU_OP_CMD_BLIT_IMAGE) {
			i915_vkc_dec_VkImageBlit(reader, &session->arena, &data->regions.blits[index]);
		} else {
			i915_vkc_dec_VkBufferImageCopy(reader, &session->arena, &data->regions.buffers[index]);
		}
	}
	data->count = (uint32_t)count;

	/* A blit's filter, which a blit of the same size does not use. */
	if (opcode == GPU_OP_CMD_BLIT_IMAGE)
		(void)drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Both resources held. */
	error = hold(session, entry, source_kind, source);
	if (error == 0)
		error = hold(session, entry, destination_kind, destination);
	if (error != 0)
		return error;

	/* Level 0 and layer 0 of each image; a blit only of the same size, unflipped. */
	refused = 0;
	for (index = 0; refused == 0 && index < count; index++) {
		if (opcode == GPU_OP_CMD_COPY_IMAGE) {
			refused = first_subresource(data->regions.copies[index].srcSubresource.mipLevel, data->regions.copies[index].srcSubresource.baseArrayLayer, data->regions.copies[index].srcSubresource.layerCount);
			if (refused == 0)
				refused = first_subresource(data->regions.copies[index].dstSubresource.mipLevel, data->regions.copies[index].dstSubresource.baseArrayLayer, data->regions.copies[index].dstSubresource.layerCount);
		} else if (opcode == GPU_OP_CMD_BLIT_IMAGE) {
			blit = &data->regions.blits[index];
			refused = first_subresource(blit->srcSubresource.mipLevel, blit->srcSubresource.baseArrayLayer, blit->srcSubresource.layerCount);
			if (refused == 0)
				refused = first_subresource(blit->dstSubresource.mipLevel, blit->dstSubresource.baseArrayLayer, blit->dstSubresource.layerCount);
			if (refused == 0 && (blit->srcOffsets[1].x - blit->srcOffsets[0].x != blit->dstOffsets[1].x - blit->dstOffsets[0].x ||
			    blit->srcOffsets[1].y - blit->srcOffsets[0].y != blit->dstOffsets[1].y - blit->dstOffsets[0].y ||
			    blit->srcOffsets[1].x <= blit->srcOffsets[0].x || blit->srcOffsets[1].y <= blit->srcOffsets[0].y ||
			    blit->srcOffsets[0].x < 0 || blit->srcOffsets[0].y < 0 || blit->dstOffsets[0].x < 0 || blit->dstOffsets[0].y < 0))
				refused = ENOTSUP;
		} else {
			refused = first_subresource(data->regions.buffers[index].imageSubresource.mipLevel, data->regions.buffers[index].imageSubresource.baseArrayLayer, data->regions.buffers[index].imageSubresource.layerCount);
		}
	}
	if (refused != 0)
		return refused;

	/* Succeeded: the transfer is recorded. */
	return 0;
}

/*
 * Runs one recorded transfer command, region by region; ETIMEDOUT when
 * one did not finish, the session then abandoned.
 */
int
powervr_vk_transfer_run(
	struct powervr_vk_session *session,
	const struct powervr_vk_record *entry)
{
	struct transfer_work *work;
	const struct transfer_data *data;
	const struct powervr_vk_image *image;
	const struct powervr_vk_image *other;
	const struct powervr_vk_buffer *buffer;
	const VkBufferImageCopy *copy;
	uint64_t address;
	uint32_t row;
	uint32_t height;
	uint32_t index;
	int error;

	/* The working state, and the record's data. */
	work = kern_calloc(1, sizeof(*work));
	if (work == NULL)
		return ENOMEM;
	data = entry->data;
	error = 0;
	for (index = 0; error == 0 && index < data->count; index++) {
		kern_memset(&work->request, 0, sizeof(work->request));
		if (entry->opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
			/* The whole of level 0 to the colour. */
			image = entry->objects[0]->payload;
			work->request.kind = POWERVR_TRANSFER_CLEAR;
			error = image_surface(image, &work->request.destination);
			work->request.area.width = image->shape.width;
			work->request.area.height = image->shape.height;
			kern_memcpy(work->request.colour, data->colour, sizeof(work->request.colour));
		} else if (entry->opcode == GPU_OP_CMD_COPY_IMAGE || entry->opcode == GPU_OP_CMD_BLIT_IMAGE) {
			/* From the source image's view to the destination's rectangle. */
			image = entry->objects[1]->payload;
			other = entry->objects[0]->payload;
			work->request.kind = POWERVR_TRANSFER_COPY;
			error = image_surface(image, &work->request.destination);
			if (error == 0)
				error = image_view(other, &work->view);
			if (error == 0 && other->format != image->format)
				error = ENOTSUP;
			if (entry->opcode == GPU_OP_CMD_COPY_IMAGE) {
				work->request.area.x = (uint32_t)data->regions.copies[index].dstOffset.x;
				work->request.area.y = (uint32_t)data->regions.copies[index].dstOffset.y;
				work->request.area.width = data->regions.copies[index].extent.width;
				work->request.area.height = data->regions.copies[index].extent.height;
				work->request.source_x = (uint32_t)data->regions.copies[index].srcOffset.x;
				work->request.source_y = (uint32_t)data->regions.copies[index].srcOffset.y;
			} else {
				work->request.area.x = (uint32_t)data->regions.blits[index].dstOffsets[0].x;
				work->request.area.y = (uint32_t)data->regions.blits[index].dstOffsets[0].y;
				work->request.area.width = (uint32_t)(data->regions.blits[index].dstOffsets[1].x - data->regions.blits[index].dstOffsets[0].x);
				work->request.area.height = (uint32_t)(data->regions.blits[index].dstOffsets[1].y - data->regions.blits[index].dstOffsets[0].y);
				work->request.source_x = (uint32_t)data->regions.blits[index].srcOffsets[0].x;
				work->request.source_y = (uint32_t)data->regions.blits[index].srcOffsets[0].y;
			}
			work->request.source = &work->view;
		} else {
			/* Between a buffer laid out as linear rows of the image's texels and the image. */
			copy = &data->regions.buffers[index];
			if (entry->opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE) {
				buffer = entry->objects[0]->payload;
				image = entry->objects[1]->payload;
			} else {
				image = entry->objects[0]->payload;
				buffer = entry->objects[1]->payload;
			}
			row = copy->bufferRowLength != 0 ? copy->bufferRowLength : copy->imageExtent.width;
			height = copy->bufferImageHeight != 0 ? copy->bufferImageHeight : copy->imageExtent.height;
			error = powervr_vk_buffer_address(buffer, &address);
			address += copy->bufferOffset;
			work->request.kind = POWERVR_TRANSFER_COPY;
			if (error == 0 && entry->opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE) {
				/* The buffer as a linear view, read from its first texel. */
				error = image_surface(image, &work->request.destination);
				kern_memset(&work->layout, 0, sizeof(work->layout));
				work->layout.row_pitch = row * image->format->texel_bytes;
				work->layout.padded_width = row;
				work->layout.padded_height = height;
				work->layout.padded_depth = 1;
				work->layout.layer_bytes = (uint64_t)work->layout.row_pitch * height;
				work->layout.bytes = work->layout.layer_bytes;
				kern_memset(&work->view, 0, sizeof(work->view));
				work->view.format = image->format;
				work->view.layout = &work->layout;
				work->view.address = address;
				work->view.width = row;
				work->view.height = height;
				work->view.image_levels = 1;
				work->view.level_count = 1;
				work->view.layer_count = 1;
				work->view.linear = 1;
				work->request.area.x = (uint32_t)copy->imageOffset.x;
				work->request.area.y = (uint32_t)copy->imageOffset.y;
				work->request.source = &work->view;
			} else if (error == 0) {
				/* The buffer as a linear destination, written from its first texel. */
				error = image_view(image, &work->view);
				work->request.destination.format = image->format;
				work->request.destination.address = address;
				work->request.destination.width = row;
				work->request.destination.height = height;
				work->request.destination.linear = 1;
				work->request.destination.row_pitch = row * image->format->texel_bytes;
				work->request.destination.padded_width = row;
				work->request.source_x = (uint32_t)copy->imageOffset.x;
				work->request.source_y = (uint32_t)copy->imageOffset.y;
				work->request.source = &work->view;
			}
			work->request.area.width = copy->imageExtent.width;
			work->request.area.height = copy->imageExtent.height;
		}

		/* The region's transfer. */
		if (error == 0)
			error = run_one(session, &work->request);
	}

	/* The working state goes; a transfer that could not run. */
	kern_free(work);
	if (error != 0)
		return error;

	/* Succeeded: every region is done. */
	return 0;
}

/* Holds an object of a kind for a record, refusing an unknown one. */
static int
hold(
	struct powervr_vk_session *session,
	struct powervr_vk_record *entry,
	enum i915_vk_object_kind kind,
	uint64_t identity)
{
	struct powervr_vk_object *object;

	/* The object, held. */
	object = powervr_vk_object_find(session, kind, identity);
	if (object == NULL || powervr_vk_object_retain(object) != 0)
		return EINVAL;
	entry->objects[entry->object_count++] = object;

	/* Succeeded: the object is the record's. */
	return 0;
}

/* Refuses a subresource beyond level 0 and layer 0. */
static int
first_subresource(
	uint32_t level,
	uint32_t layer,
	uint32_t count)
{
	/* Level 0, layer 0, one layer. */
	if (level != 0 || layer != 0 || count != 1)
		return ENOTSUP;

	/* Succeeded: the first subresource. */
	return 0;
}

/* Describes level 0 of an image as a destination. */
static int
image_surface(
	const struct powervr_vk_image *image,
	struct powervr_render_surface *surface)
{
	uint64_t address;
	int error;

	/* The image's memory. */
	error = powervr_vk_image_address(image, &address);
	if (error != 0)
		return error;

	/* Its format, address, edges and rows. */
	surface->format = image->format;
	surface->address = address;
	surface->width = image->shape.width;
	surface->height = image->shape.height;
	surface->linear = image->shape.linear;
	surface->row_pitch = image->layout.row_pitch;
	surface->padded_width = image->layout.padded_width;

	/* Succeeded: the surface. */
	return 0;
}

/* Describes level 0 of an image as a source view. */
static int
image_view(
	const struct powervr_vk_image *image,
	struct powervr_view_shape *view)
{
	uint64_t address;
	int error;

	/* The image's memory. */
	error = powervr_vk_image_address(image, &address);
	if (error != 0)
		return error;

	/* All of level 0 and layer 0, the identity mapping. */
	kern_memset(view, 0, sizeof(*view));
	view->format = image->format;
	view->layout = &image->layout;
	view->address = address;
	view->width = image->shape.width;
	view->height = image->shape.height;
	view->image_levels = image->shape.levels;
	view->level_count = 1;
	view->layer_count = 1;
	view->linear = image->shape.linear;

	/* Succeeded: the view. */
	return 0;
}

/* Runs one region's transfer; one that did not finish abandons the session. */
static int
run_one(
	struct powervr_vk_session *session,
	struct powervr_transfer_request *request)
{
	struct powervr_session *owner;
	int error;

	/* The transfer on the session's transfer context. */
	owner = session->owner;
	error = powervr_transfer_run(owner->render, owner, request);
	if (error == ETIMEDOUT)
		session->abandoned = true;
	if (error != 0) {
		kern_logf("powervr: vk: a transfer did not run (%d)\n", error);
		return error;
	}

	/* Succeeded: the region is done. */
	return 0;
}
