/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the display list's replay (ws213-p003, p001 9 (a)).
 *
 * The scenes of the compositor's frames are recorded as their commands
 * follow one another in compose_record and its callees: the plain look's
 * desktop with two windows, a popup and the cursor; App Home's picture with
 * its floor lines joined (batch.c) and drawn alone; a drag's partial redraw
 * with the cut scissor; the backdrop's small passes; two heads (a mirror's
 * blit and an extended head's pass); a layer's quiet picture; the scene's
 * capture and restoration (scene.c); and a test image's capture.  Each
 * scene is written into a display list (draw-list.c) and replayed into a
 * fake command buffer, whose Vulkan writes every command with its arguments
 * as a line, descriptor sets by the (image, kind) they stand for; each
 * scene ends with a sum of the joined quads' vertices the replay wrote.
 * The lines are compared with the golden file taken from the direct
 * recording (the first commit of ws213-p003).
 */

#include "userland/desktop/wayland/compose.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Marks a parameter a fake command does not look at. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* How many handles the test names. */
#define TEST_NAMES		64U

/* The output's size in the scenes. */
#define TEST_WIDTH		1920U
#define TEST_HEIGHT		1080U

/* A recording the scenes write into: the display list. */
typedef struct kwl_list *test_target;

/* One handle the lines show by a name. */
struct test_name {
	uint64_t handle;
	const char *name;
};

/*
 * The handles named so far, filled by test_handle; the lines print a
 * handle by its name, a set by the (image, kind) it stands for.
 */
static struct test_name test_names[TEST_NAMES];
static unsigned test_name_count;

/* The file the lines go to (the output compared with the golden file). */
static FILE *test_out;

/* The joined quads' vertex buffer the replay writes (batch.c maps it in the compositor). */
static uint8_t test_vertices[1024U * 1024U];

/* The fake command buffer the lists are replayed into. */
static VkCommandBuffer test_command;

static void test_line(const char *format, ...);
static const char *test_handle_name(uint64_t handle);
static uint64_t test_handle(const char *name);
static void test_floats(const char *what, const float *values, unsigned count);
static void test_image_barriers(uint32_t count, const VkImageMemoryBarrier *barriers);
static void test_scene_begin(struct kwl_server *server, test_target target, const char *name);
static void test_scene_end(struct kwl_server *server, test_target target);
static void test_pass(test_target target, const char *pass, const char *framebuffer, const VkRect2D *area, unsigned clear);
static void test_view(test_target target, uint32_t width, uint32_t height, const VkRect2D *scissor);
static void test_corners(struct kwl_server *server, test_target target);
static void test_quad(struct kwl_server *server, test_target target, const char *set, float x, float y);
static void test_shape(struct kwl_server *server, test_target target, const char *set, unsigned mode, float x, float y, float side);
static void scene_plain(struct kwl_server *server, test_target target);
static void scene_home(struct kwl_server *server, test_target target);
static void scene_drag(struct kwl_server *server, test_target target);
static void scene_backdrop(struct kwl_server *server, test_target target);
static void scene_heads(struct kwl_server *server, test_target target);
static void scene_bake(struct kwl_server *server, test_target target);
static void scene_capture(struct kwl_server *server, test_target target);
static void scene_shot(struct kwl_server *server, test_target target);
static void test_growth(struct kwl_server *server);

/* Writes the beginning of a render pass. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass(
	VkCommandBuffer command,
	const VkRenderPassBeginInfo *begin,
	VkSubpassContents contents)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The pass, its framebuffer, its area and its clear colours. */
	test_line(
		"begin pass=%s framebuffer=%s area=%d,%d,%u,%u contents=%d clears=%u",
		test_handle_name((uint64_t)(uintptr_t)begin->renderPass),
		test_handle_name((uint64_t)(uintptr_t)begin->framebuffer),
		begin->renderArea.offset.x,
		begin->renderArea.offset.y,
		begin->renderArea.extent.width,
		begin->renderArea.extent.height,
		(int)contents,
		begin->clearValueCount);

	/* Each clear colour. */
	for (index = 0U; index < begin->clearValueCount; index++)
		test_floats("  clear", begin->pClearValues[index].color.float32, 4U);
}

/* Writes the end of a render pass. */
VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(
	VkCommandBuffer command)
{
	UNUSED_PARAMETER(command);

	/* The pass ends. */
	test_line("end pass");
}

/* Writes a viewport. */
VKAPI_ATTR void VKAPI_CALL
vkCmdSetViewport(
	VkCommandBuffer command,
	uint32_t first,
	uint32_t count,
	const VkViewport *viewports)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* Each viewport. */
	for (index = 0U; index < count; index++) {
		test_line(
			"viewport %u %g,%g,%g,%g depth=%g,%g",
			first + index,
			(double)viewports[index].x,
			(double)viewports[index].y,
			(double)viewports[index].width,
			(double)viewports[index].height,
			(double)viewports[index].minDepth,
			(double)viewports[index].maxDepth);
	}
}

/* Writes a scissor. */
VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissor(
	VkCommandBuffer command,
	uint32_t first,
	uint32_t count,
	const VkRect2D *scissors)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* Each scissor. */
	for (index = 0U; index < count; index++) {
		test_line(
			"scissor %u %d,%d,%u,%u",
			first + index,
			scissors[index].offset.x,
			scissors[index].offset.y,
			scissors[index].extent.width,
			scissors[index].extent.height);
	}
}

/* Writes a pipeline's bind. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(
	VkCommandBuffer command,
	VkPipelineBindPoint point,
	VkPipeline pipeline)
{
	UNUSED_PARAMETER(command);

	/* The pipeline. */
	test_line("pipeline point=%d %s", (int)point, test_handle_name((uint64_t)(uintptr_t)pipeline));
}

/* Writes a descriptor set's bind, the set by what it stands for. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(
	VkCommandBuffer command,
	VkPipelineBindPoint point,
	VkPipelineLayout layout,
	uint32_t first,
	uint32_t count,
	const VkDescriptorSet *sets,
	uint32_t dynamic_count,
	const uint32_t *dynamic_offsets)
{
	uint32_t index;

	UNUSED_PARAMETER(command);
	UNUSED_PARAMETER(dynamic_offsets);

	/* Each set. */
	for (index = 0U; index < count; index++) {
		test_line(
			"set point=%d layout=%s %u %s dynamic=%u",
			(int)point,
			test_handle_name((uint64_t)(uintptr_t)layout),
			first + index,
			test_handle_name((uint64_t)(uintptr_t)sets[index]),
			dynamic_count);
	}
}

/* Writes push constants with their values. */
VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(
	VkCommandBuffer command,
	VkPipelineLayout layout,
	VkShaderStageFlags stages,
	uint32_t offset,
	uint32_t size,
	const void *values)
{
	UNUSED_PARAMETER(command);

	/* The range, then the floats. */
	test_line("push layout=%s stages=0x%x offset=%u size=%u", test_handle_name((uint64_t)(uintptr_t)layout), (unsigned)stages, offset, size);
	test_floats("  values", values, size / (uint32_t)sizeof(float));
}

/* Writes a vertex buffer's bind. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(
	VkCommandBuffer command,
	uint32_t first,
	uint32_t count,
	const VkBuffer *buffers,
	const VkDeviceSize *offsets)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* Each buffer and offset. */
	for (index = 0U; index < count; index++)
		test_line("vertices %u %s offset=%llu", first + index, test_handle_name((uint64_t)(uintptr_t)buffers[index]), (unsigned long long)offsets[index]);
}

/* Writes a draw. */
VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(
	VkCommandBuffer command,
	uint32_t vertices,
	uint32_t instances,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	UNUSED_PARAMETER(command);

	/* The counts. */
	test_line("draw %u %u %u %u", vertices, instances, first_vertex, first_instance);
}

/* Writes a pipeline barrier with each of its barriers. */
VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(
	VkCommandBuffer command,
	VkPipelineStageFlags source,
	VkPipelineStageFlags destination,
	VkDependencyFlags dependency,
	uint32_t memory_count,
	const VkMemoryBarrier *memory,
	uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers,
	uint32_t image_count,
	const VkImageMemoryBarrier *images)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The stages and the counts. */
	test_line("barrier 0x%x->0x%x dependency=0x%x memory=%u buffers=%u images=%u", (unsigned)source, (unsigned)destination, (unsigned)dependency, memory_count, buffer_count, image_count);

	/* Each memory barrier. */
	for (index = 0U; index < memory_count; index++)
		test_line("  memory 0x%x->0x%x", (unsigned)memory[index].srcAccessMask, (unsigned)memory[index].dstAccessMask);

	/* Each buffer barrier. */
	for (index = 0U; index < buffer_count; index++) {
		test_line(
			"  buffer %s 0x%x->0x%x queues=%u,%u offset=%llu size=%llu",
			test_handle_name((uint64_t)(uintptr_t)buffers[index].buffer),
			(unsigned)buffers[index].srcAccessMask,
			(unsigned)buffers[index].dstAccessMask,
			buffers[index].srcQueueFamilyIndex,
			buffers[index].dstQueueFamilyIndex,
			(unsigned long long)buffers[index].offset,
			(unsigned long long)buffers[index].size);
	}

	/* Each image barrier. */
	test_image_barriers(image_count, images);
}

/* Writes an image's copy. */
VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageCopy *regions)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The images and each region. */
	test_line("copy-image %s %d -> %s %d regions=%u", test_handle_name((uint64_t)(uintptr_t)source), (int)source_layout, test_handle_name((uint64_t)(uintptr_t)destination), (int)destination_layout, count);
	for (index = 0U; index < count; index++)
		test_line("  extent %u,%u,%u", regions[index].extent.width, regions[index].extent.height, regions[index].extent.depth);
}

/* Writes a buffer's copy into an image. */
VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBufferToImage(
	VkCommandBuffer command,
	VkBuffer buffer,
	VkImage image,
	VkImageLayout layout,
	uint32_t count,
	const VkBufferImageCopy *regions)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The buffer, the image and each region. */
	test_line("copy-buffer-image %s -> %s %d regions=%u", test_handle_name((uint64_t)(uintptr_t)buffer), test_handle_name((uint64_t)(uintptr_t)image), (int)layout, count);
	for (index = 0U; index < count; index++) {
		test_line(
			"  offset=%llu row=%u height=%u at %d,%d extent %u,%u",
			(unsigned long long)regions[index].bufferOffset,
			regions[index].bufferRowLength,
			regions[index].bufferImageHeight,
			regions[index].imageOffset.x,
			regions[index].imageOffset.y,
			regions[index].imageExtent.width,
			regions[index].imageExtent.height);
	}
}

/* Writes an image's copy into a buffer. */
VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	VkBuffer buffer,
	uint32_t count,
	const VkBufferImageCopy *regions)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The image, the buffer and each region. */
	test_line("copy-image-buffer %s %d -> %s regions=%u", test_handle_name((uint64_t)(uintptr_t)image), (int)layout, test_handle_name((uint64_t)(uintptr_t)buffer), count);
	for (index = 0U; index < count; index++) {
		test_line(
			"  offset=%llu row=%u height=%u at %d,%d extent %u,%u",
			(unsigned long long)regions[index].bufferOffset,
			regions[index].bufferRowLength,
			regions[index].bufferImageHeight,
			regions[index].imageOffset.x,
			regions[index].imageOffset.y,
			regions[index].imageExtent.width,
			regions[index].imageExtent.height);
	}
}

/* Writes an image's blit. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageBlit *regions,
	VkFilter filter)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The images, the filter and each region's corners. */
	test_line("blit %s %d -> %s %d regions=%u filter=%d", test_handle_name((uint64_t)(uintptr_t)source), (int)source_layout, test_handle_name((uint64_t)(uintptr_t)destination), (int)destination_layout, count, (int)filter);
	for (index = 0U; index < count; index++) {
		test_line(
			"  from %d,%d-%d,%d to %d,%d-%d,%d",
			regions[index].srcOffsets[0].x,
			regions[index].srcOffsets[0].y,
			regions[index].srcOffsets[1].x,
			regions[index].srcOffsets[1].y,
			regions[index].dstOffsets[0].x,
			regions[index].dstOffsets[0].y,
			regions[index].dstOffsets[1].x,
			regions[index].dstOffsets[1].y);
	}
}

/* Writes an image's clear. */
VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	const VkClearColorValue *color,
	uint32_t count,
	const VkImageSubresourceRange *ranges)
{
	uint32_t index;

	UNUSED_PARAMETER(command);

	/* The image, the colour and each range. */
	test_line("clear %s %d ranges=%u", test_handle_name((uint64_t)(uintptr_t)image), (int)layout, count);
	test_floats("  colour", color->float32, 4U);
	for (index = 0U; index < count; index++)
		test_line("  range aspect=0x%x levels=%u layers=%u", (unsigned)ranges[index].aspectMask, ranges[index].levelCount, ranges[index].layerCount);
}

/* The making of the batch's pipeline and buffer and of the scene's buffer is not exercised here. */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *m) { (void)d; (void)i; (void)a; (void)m; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *b) { (void)d; (void)i; (void)a; (void)b; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r) { (void)d; (void)b; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *m) { (void)p; (void)m; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *m) { (void)d; (void)i; (void)a; (void)m; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) { (void)d; (void)b; (void)m; (void)o; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p) { (void)d; (void)m; (void)o; (void)s; (void)f; (void)p; return VK_ERROR_INITIALIZATION_FAILED; }
VkResult kwl_compose_pipeline_input(struct kwl_compose *c, VkShaderModule v, VkShaderModule f, VkPipelineLayout l, const VkPipelineVertexInputStateCreateInfo *i, VkPipeline *p) { (void)c; (void)v; (void)f; (void)l; (void)i; (void)p; return VK_ERROR_INITIALIZATION_FAILED; }

/* Records every scene and writes its commands to the file named, or to the standard output. */
int
main(
	int argc,
	char **argv)
{
	static struct kwl_server server;
	static struct kwl_compose compose;
	static struct kwl_list list;
	struct kwl_list *command;

	/* The lines' file. */
	test_out = stdout;
	if (argc > 1) {
		test_out = fopen(argv[1], "w");
		if (test_out == NULL) {
			perror(argv[1]);
			return 1;
		}
	}

	/* The compositor's objects the drawing uses, by name. */
	server.compose = &compose;
	server.log_frames = 1U;
	server.width = TEST_WIDTH;
	server.height = TEST_HEIGHT;
	compose.corners = (VkBuffer)(uintptr_t)test_handle("corners");
	compose.layout = (VkPipelineLayout)(uintptr_t)test_handle("image-layout");
	compose.pipelines[0] = (VkPipeline)(uintptr_t)test_handle("image-opaque");
	compose.pipelines[1] = (VkPipeline)(uintptr_t)test_handle("image-alpha");
	compose.panel_layout = (VkPipelineLayout)(uintptr_t)test_handle("panel-layout");
	compose.panel_pipelines[0] = (VkPipeline)(uintptr_t)test_handle("panel-glass");
	compose.panel_pipelines[2] = (VkPipeline)(uintptr_t)test_handle("panel-image");
	compose.panel_pipelines[3] = (VkPipeline)(uintptr_t)test_handle("panel-solid");
	compose.panel_pipelines[5] = (VkPipeline)(uintptr_t)test_handle("panel-text");
	compose.panel_pipelines[6] = (VkPipeline)(uintptr_t)test_handle("panel-blur");
	compose.batch.pipeline = (VkPipeline)(uintptr_t)test_handle("batch-pipeline");
	compose.batch.buffer = (VkBuffer)(uintptr_t)test_handle("batch-buffer");
	compose.batch.map = test_vertices;
	compose.batch.capacity = sizeof(test_vertices);
	compose.scene.buffer = (VkBuffer)(uintptr_t)test_handle("scene-buffer");
	compose.scene.width = TEST_WIDTH;
	compose.scene.height = TEST_HEIGHT;
	test_command = (VkCommandBuffer)(uintptr_t)test_handle("frame-command");
	command = &list;

	/* Each scene, with the batch on, then App Home with it off. */
	compose.batch.ready = 1U;
	scene_plain(&server, command);
	scene_home(&server, command);
	scene_drag(&server, command);
	scene_backdrop(&server, command);
	scene_heads(&server, command);
	scene_bake(&server, command);
	scene_capture(&server, command);
	scene_shot(&server, command);
	compose.batch.ready = 0U;
	scene_home(&server, command);

	/* A list that grows past its first room keeps every operation, array and vertex (not in the golden file). */
	compose.batch.ready = 1U;
	test_growth(&server);

	/* The lines are written, and the list's memory freed. */
	kwl_list_free(&list);
	if (test_out != stdout)
		fclose(test_out);

	/* Succeeded. */
	return 0;
}

/* Writes one line of the recording. */
static void
test_line(
	const char *format,
	...)
{
	va_list arguments;

	/* The line and its end. */
	va_start(arguments, format);
	vfprintf(test_out, format, arguments);
	va_end(arguments);
	fputc('\n', test_out);
}

/* Gives a handle's name, or its value for one the test did not name. */
static const char *
test_handle_name(
	uint64_t handle)
{
	static char text[32];
	unsigned index;

	/* The null handle. */
	if (handle == 0U)
		return "null";

	/* A named handle. */
	for (index = 0U; index < test_name_count; index++) {
		/* The name of this handle. */
		if (test_names[index].handle == handle)
			return test_names[index].name;
	}

	/* Another handle, by its value. */
	snprintf(text, sizeof(text), "0x%llx", (unsigned long long)handle);
	return text;
}

/* Makes a handle the lines show by its name (a set's name says its image and kind). */
static uint64_t
test_handle(
	const char *name)
{
	unsigned index;
	int order;

	/* An existing name keeps its handle. */
	for (index = 0U; index < test_name_count; index++) {
		/* The handle of this name. */
		order = strcmp(test_names[index].name, name);
		if (order == 0)
			return test_names[index].handle;
	}

	/* A new handle: a distinct, aligned address-like value. */
	assert(test_name_count < TEST_NAMES);
	test_names[test_name_count].handle = 0x10000U + 0x100U * (uint64_t)test_name_count;
	test_names[test_name_count].name = name;
	test_name_count++;

	/* The new handle. */
	return test_names[test_name_count - 1U].handle;
}

/* Writes a run of floats. */
static void
test_floats(
	const char *what,
	const float *values,
	unsigned count)
{
	unsigned index;

	/* The label, each value, the end of the line. */
	fputs(what, test_out);
	for (index = 0U; index < count; index++)
		fprintf(test_out, " %g", (double)values[index]);
	fputc('\n', test_out);
}

/* Writes each image barrier of a pipeline barrier. */
static void
test_image_barriers(
	uint32_t count,
	const VkImageMemoryBarrier *barriers)
{
	uint32_t index;

	/* Each barrier's image, accesses, layouts and range. */
	for (index = 0U; index < count; index++) {
		test_line(
			"  image %s 0x%x->0x%x layout %d->%d queues=%u,%u aspect=0x%x levels=%u layers=%u",
			test_handle_name((uint64_t)(uintptr_t)barriers[index].image),
			(unsigned)barriers[index].srcAccessMask,
			(unsigned)barriers[index].dstAccessMask,
			(int)barriers[index].oldLayout,
			(int)barriers[index].newLayout,
			barriers[index].srcQueueFamilyIndex,
			barriers[index].dstQueueFamilyIndex,
			(unsigned)barriers[index].subresourceRange.aspectMask,
			barriers[index].subresourceRange.levelCount,
			barriers[index].subresourceRange.layerCount);
	}
}

/* Starts a scene's recording, as kwl_compose_list_begin does. */
static void
test_scene_begin(
	struct kwl_server *server,
	test_target target,
	const char *name)
{
	/* The scene's name, then an empty list with nothing bound and no quad waiting. */
	test_line("scene %s", name);
	memset(test_vertices, 0, sizeof(test_vertices));
	kwl_list_reset(target);
	kwl_compose_bind_forget(server->compose);
	kwl_batch_begin(server->compose);
}

/*
 * Ends a scene's recording as compose_replay does: the list's vertices
 * where the batch's draws read them, its operations into the fake command
 * buffer; then the vertices are summed as the GPU would read them.
 */
static void
test_scene_end(
	struct kwl_server *server,
	test_target target)
{
	uint32_t sum;
	uint32_t index;

	/* The list grew as it was written, and the batch's offsets are the list's. */
	assert(!target->failed);
	assert(target->vertex_used == server->compose->batch.used);

	/* The replay. */
	memcpy(test_vertices, target->vertices, target->vertex_used);
	kwl_list_replay(target, test_command);

	/* A sum of the bytes the batch's draws read. */
	sum = 0U;
	for (index = 0U; index < server->compose->batch.used; index++)
		sum = sum * 31U + test_vertices[index];
	test_line("vertices used=%u sum=0x%08x", server->compose->batch.used, sum);
}

/* Begins a pass with the output's clear colour, or keeping the pixels. */
static void
test_pass(
	test_target target,
	const char *pass,
	const char *framebuffer,
	const VkRect2D *area,
	unsigned clear)
{
	VkRenderPassBeginInfo begin;
	VkClearValue colour;

	/* The background's colour, when the pass clears. */
	memset(&colour, 0, sizeof(colour));
	colour.color.float32[0] = KWL_BACKGROUND_RED;
	colour.color.float32[1] = KWL_BACKGROUND_GREEN;
	colour.color.float32[2] = KWL_BACKGROUND_BLUE;
	colour.color.float32[3] = 1.0f;

	/* The pass over the area. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = (VkRenderPass)(uintptr_t)test_handle(pass);
	begin.framebuffer = (VkFramebuffer)(uintptr_t)test_handle(framebuffer);
	begin.renderArea = *area;
	begin.clearValueCount = clear;
	begin.pClearValues = &colour;
	kwl_list_begin_render_pass(target, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

/* Places the whole target and keeps the drawing to a scissor. */
static void
test_view(
	test_target target,
	uint32_t width,
	uint32_t height,
	const VkRect2D *scissor)
{
	VkViewport viewport;

	/* The viewport, then the scissor. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.maxDepth = 1.0f;
	kwl_list_set_viewport(target, 0U, 1U, &viewport);
	kwl_list_set_scissor(target, 0U, 1U, scissor);
}

/* Binds every quad's corners and forgets the binds, as a pass's start does. */
static void
test_corners(
	struct kwl_server *server,
	test_target target)
{
	VkDeviceSize offset;

	/* The corners from the buffer's start. */
	offset = 0U;
	kwl_list_bind_vertex_buffers(target, 0U, 1U, &server->compose->corners, &offset);
	kwl_compose_bind_forget(server->compose);
}

/* Draws a client's image as a quad, as compose_quad_part does. */
static void
test_quad(
	struct kwl_server *server,
	test_target target,
	const char *set,
	float x,
	float y)
{
	float constants[8];

	/* The rectangle and the whole image. */
	constants[0] = x;
	constants[1] = y;
	constants[2] = x + 0.5f;
	constants[3] = y + 0.5f;
	constants[4] = 0.0f;
	constants[5] = 0.0f;
	constants[6] = 1.0f;
	constants[7] = 1.0f;

	/* The waiting quads first, the image's pipeline and set, the constants, the draw. */
	kwl_batch_flush(server, target);
	kwl_compose_bind(server, target, server->compose->pipelines[1], server->compose->layout, (VkDescriptorSet)(uintptr_t)test_handle(set));
	kwl_list_push_constants(target, server->compose->layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(constants), constants);
	kwl_list_draw(target, KWL_QUAD_VERTICES, 1U, 0U, 0U);
}

/* Draws a glass shape, joined to the batch when it is a small solid quad, as glass_shape_draw does. */
static void
test_shape(
	struct kwl_server *server,
	test_target target,
	const char *set,
	unsigned mode,
	float x,
	float y,
	float side)
{
	float constants[KWL_PANEL_CONSTANTS];
	VkDescriptorSet handle;
	VkPipeline pipeline;
	unsigned index;
	int joined;

	/* The corners, the box, the colour, the radius and the mode, the screen, the opacity. */
	for (index = 0U; index < KWL_PANEL_CONSTANTS; index++)
		constants[index] = 0.0f;
	constants[0] = x;
	constants[1] = y;
	constants[2] = x + side;
	constants[3] = y + side;
	constants[8] = x * 100.0f;
	constants[9] = y * 100.0f;
	constants[10] = side * 100.0f;
	constants[11] = side * 100.0f;
	constants[12] = 0.86f;
	constants[13] = 0.90f;
	constants[14] = 1.0f;
	constants[15] = 0.5f;
	constants[16] = 3.0f;
	constants[17] = (float)mode;
	constants[20] = (float)TEST_WIDTH;
	constants[21] = (float)TEST_HEIGHT;
	constants[23] = 0.75f;

	/* A small solid quad is joined when the batch takes it. */
	handle = (VkDescriptorSet)(uintptr_t)test_handle(set);
	if (mode == 3U && side < 0.05f) {
		joined = kwl_batch_add(server, target, constants, handle);
		if (joined)
			return;
	}

	/* Anything else draws the waiting quads, then itself. */
	kwl_batch_flush(server, target);
	pipeline = server->compose->panel_pipelines[mode];
	kwl_compose_bind(server, target, pipeline, server->compose->panel_layout, handle);
	kwl_list_push_constants(target, server->compose->panel_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(constants), constants);
	kwl_list_draw(target, KWL_QUAD_VERTICES, 1U, 0U, 0U);
}

/* The plain look: the desktop, two windows between their sub-surfaces, a popup and the cursor. */
static void
scene_plain(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D whole;

	/* The whole output, cleared. */
	test_scene_begin(server, target, "plain");
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = TEST_WIDTH;
	whole.extent.height = TEST_HEIGHT;
	test_pass(target, "output-pass", "output-framebuffer-0", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);

	/* The desktop's icon, two windows (one with a sub-surface), a popup, the cursor. */
	test_quad(server, target, "set:icon:0", -0.9f, -0.9f);
	test_quad(server, target, "set:window-a:0", -0.5f, -0.5f);
	test_quad(server, target, "set:window-b:0", 0.0f, -0.2f);
	test_quad(server, target, "set:window-b-sub:0", 0.1f, 0.0f);
	test_quad(server, target, "set:window-b:0", 0.0f, -0.2f);
	test_quad(server, target, "set:popup:0", 0.2f, 0.2f);
	test_quad(server, target, "set:arrow:0", 0.3f, 0.3f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* App Home: its picture in the layer's pass (floor lines joined), then the frame sampling it. */
static void
scene_home(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D whole;
	unsigned index;
	float place;

	/* The layer's pass over the whole picture. */
	test_scene_begin(server, target, "home");
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = TEST_WIDTH;
	whole.extent.height = TEST_HEIGHT;
	test_pass(target, "layers-pass", "layer-home-framebuffer", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);

	/* The tiles' glass and their labels. */
	for (index = 0U; index < 12U; index++) {
		/* One tile and its label. */
		place = -0.9f + 0.15f * (float)index;
		test_shape(server, target, "set:blurred:0", 0U, place, -0.5f, 0.1f);
		test_shape(server, target, "set:atlas:0", 5U, place, -0.35f, 0.02f);
	}

	/* The floor lines, a large quad between them and five more lines, a label. */
	for (index = 0U; index < 80U; index++)
		test_shape(server, target, "set:blurred:0", 3U, -0.9f + 0.02f * (float)index, 0.6f, 0.01f);
	test_shape(server, target, "set:blurred:0", 3U, -0.9f, 0.7f, 0.4f);
	for (index = 0U; index < 5U; index++)
		test_shape(server, target, "set:blurred:0", 3U, -0.5f + 0.02f * (float)index, 0.8f, 0.01f);
	test_shape(server, target, "set:atlas:0", 5U, 0.0f, 0.9f, 0.02f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);

	/* The frame: the wallpaper, Home's picture over it, the cursor. */
	test_pass(target, "output-pass", "output-framebuffer-1", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);
	test_shape(server, target, "set:wallpaper:0", 2U, -1.0f, -1.0f, 2.0f);
	test_shape(server, target, "set:layer-home:0", 2U, -1.0f, -1.0f, 2.0f);
	test_quad(server, target, "set:arrow:0", 0.3f, 0.3f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* A drag's partial redraw: the pass that keeps the pixels over the damage, a cut scissor and its restoration. */
static void
scene_drag(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D region;
	VkRect2D cut;

	/* The damaged part, its pixels kept. */
	test_scene_begin(server, target, "drag");
	memset(&region, 0, sizeof(region));
	region.offset.x = 400;
	region.offset.y = 300;
	region.extent.width = 640U;
	region.extent.height = 480U;
	test_pass(target, "output-pass-load", "output-framebuffer-2", &region, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &region);
	test_corners(server, target);

	/* The window's title bar cut to its body (shell.c), then the scissor back. */
	test_shape(server, target, "set:blurred:0", 0U, -0.4f, -0.4f, 0.5f);
	cut = region;
	cut.extent.height = 44U;
	kwl_list_set_scissor(target, 0U, 1U, &cut);
	test_shape(server, target, "set:atlas:0", 5U, -0.39f, -0.39f, 0.02f);
	kwl_list_set_scissor(target, 0U, 1U, &region);
	test_quad(server, target, "set:window-a:0", -0.4f, -0.35f);
	test_quad(server, target, "set:arrow:0", 0.3f, 0.3f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* The backdrop: the frame's pass ended, the scene drawn small and blurred twice, the pass taken up again. */
static void
scene_backdrop(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D whole;
	VkRect2D small;

	/* The frame up to the window with the glass. */
	test_scene_begin(server, target, "backdrop");
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = TEST_WIDTH;
	whole.extent.height = TEST_HEIGHT;
	test_pass(target, "output-pass", "output-framebuffer-0", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);
	test_shape(server, target, "set:wallpaper:0", 2U, -1.0f, -1.0f, 2.0f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);

	/* The small pass, then the two blur steps between the targets. */
	memset(&small, 0, sizeof(small));
	small.extent.width = TEST_WIDTH / 4U;
	small.extent.height = TEST_HEIGHT / 4U;
	test_pass(target, "backdrop-pass", "backdrop-framebuffer-0", &small, 0U);
	test_view(target, TEST_WIDTH / 4U, TEST_HEIGHT / 4U, &small);
	test_corners(server, target);
	test_shape(server, target, "set:wallpaper:0", 2U, -1.0f, -1.0f, 2.0f);
	kwl_list_end_render_pass(target);
	test_pass(target, "backdrop-pass", "backdrop-framebuffer-1", &small, 0U);
	test_view(target, TEST_WIDTH / 4U, TEST_HEIGHT / 4U, &small);
	test_shape(server, target, "set:backdrop-0:0", 6U, -1.0f, -1.0f, 2.0f);
	kwl_list_end_render_pass(target);
	test_pass(target, "backdrop-pass", "backdrop-framebuffer-0", &small, 0U);
	test_view(target, TEST_WIDTH / 4U, TEST_HEIGHT / 4U, &small);
	test_shape(server, target, "set:backdrop-1:0", 6U, -1.0f, -1.0f, 2.0f);
	kwl_list_end_render_pass(target);

	/* The frame's pass again, keeping its pixels, the glass over the blurred scene. */
	test_pass(target, "output-pass-load", "output-framebuffer-0", &whole, 0U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);
	test_shape(server, target, "set:backdrop-0:0", 0U, -0.4f, -0.4f, 0.5f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* Two heads: a mirror's cleared image and blit, then an extended head's pass and its move to presentation. */
static void
scene_heads(
	struct kwl_server *server,
	test_target target)
{
	VkImageMemoryBarrier barriers[2];
	VkImageSubresourceRange range;
	VkClearColorValue black;
	VkImageBlit blit;
	VkRect2D whole;

	/* The output's image to a copy's source, the mirror's to a copy's destination. */
	test_scene_begin(server, target, "heads");
	memset(barriers, 0, sizeof(barriers));
	barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].image = (VkImage)(uintptr_t)test_handle("output-image-0");
	barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barriers[0].subresourceRange.levelCount = 1U;
	barriers[0].subresourceRange.layerCount = 1U;
	barriers[1] = barriers[0];
	barriers[1].srcAccessMask = 0U;
	barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].image = (VkImage)(uintptr_t)test_handle("head-0-image-1");
	kwl_list_pipeline_barrier(target, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL, 2U, barriers);

	/* The mirror cleared black, then the output's picture scaled into it. */
	memset(&black, 0, sizeof(black));
	black.float32[3] = 1.0f;
	memset(&range, 0, sizeof(range));
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1U;
	range.layerCount = 1U;
	kwl_list_clear_color_image(target, barriers[1].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1U, &range);
	memset(&blit, 0, sizeof(blit));
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1U;
	blit.dstSubresource = blit.srcSubresource;
	blit.srcOffsets[1].x = (int32_t)TEST_WIDTH;
	blit.srcOffsets[1].y = (int32_t)TEST_HEIGHT;
	blit.srcOffsets[1].z = 1;
	blit.dstOffsets[0].x = 160;
	blit.dstOffsets[1].x = 1120;
	blit.dstOffsets[1].y = 540;
	blit.dstOffsets[1].z = 1;
	kwl_list_blit_image(target, barriers[0].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, barriers[1].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &blit, VK_FILTER_LINEAR);

	/* The extended head's own pass: the wallpaper and its windows. */
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = 1280U;
	whole.extent.height = 720U;
	test_pass(target, "output-pass", "head-1-framebuffer-0", &whole, 1U);
	test_view(target, 1280U, 720U, &whole);
	test_corners(server, target);
	test_shape(server, target, "set:wallpaper:0", 2U, -1.0f, -1.0f, 2.0f);
	test_quad(server, target, "set:window-c:1", -0.5f, -0.5f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);

	/* Both images back to presentation. */
	barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].dstAccessMask = 0U;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	kwl_list_pipeline_barrier(target, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL, 0U, NULL, 2U, barriers);
	test_scene_end(server, target);
}

/* A layer's quiet picture: Home's content drawn alone, presenting nothing. */
static void
scene_bake(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D whole;
	unsigned index;

	/* The layer's pass and Home's content with a few floor lines. */
	test_scene_begin(server, target, "bake");
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = TEST_WIDTH;
	whole.extent.height = TEST_HEIGHT;
	test_pass(target, "layers-pass", "layer-home-framebuffer", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);
	for (index = 0U; index < 3U; index++)
		test_shape(server, target, "set:blurred:0", 3U, -0.9f + 0.02f * (float)index, 0.6f, 0.01f);
	test_shape(server, target, "set:atlas:0", 5U, 0.0f, 0.9f, 0.02f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* The scene's restoration before a cursor-only frame, and its capture after a redrawn one (scene.c). */
static void
scene_capture(
	struct kwl_server *server,
	test_target target)
{
	VkRect2D region;
	VkRect2D whole;
	VkImage image;

	/* The damaged part restored, outside any pass. */
	test_scene_begin(server, target, "capture");
	image = (VkImage)(uintptr_t)test_handle("output-image-1");
	memset(&region, 0, sizeof(region));
	region.offset.x = 64;
	region.offset.y = 32;
	region.extent.width = 48U;
	region.extent.height = 48U;
	kwl_scene_copy(&server->compose->scene, target, image, &region, 1U);

	/* A whole frame's pass, its capture, and the pass that keeps the pixels for the cursor. */
	memset(&whole, 0, sizeof(whole));
	whole.extent.width = TEST_WIDTH;
	whole.extent.height = TEST_HEIGHT;
	test_pass(target, "output-pass", "output-framebuffer-1", &whole, 1U);
	test_view(target, TEST_WIDTH, TEST_HEIGHT, &whole);
	test_corners(server, target);
	test_quad(server, target, "set:window-a:0", -0.5f, -0.5f);
	kwl_batch_flush(server, target);
	kwl_list_end_render_pass(target);
	kwl_scene_copy(&server->compose->scene, target, image, NULL, 0U);
	test_pass(target, "output-pass-load", "output-framebuffer-1", &whole, 1U);
	kwl_compose_bind_forget(server->compose);
	test_quad(server, target, "set:arrow:0", 0.3f, 0.3f);
	kwl_list_end_render_pass(target);
	test_scene_end(server, target);
}

/* A test image's capture of the finished image (shot.c's barriers and copy). */
static void
scene_shot(
	struct kwl_server *server,
	test_target target)
{
	VkImageMemoryBarrier barrier;
	VkBufferMemoryBarrier visible;
	VkMemoryBarrier memory;
	VkBufferImageCopy copy;

	/* The image to the copy's source. */
	test_scene_begin(server, target, "shot");
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = (VkImage)(uintptr_t)test_handle("output-image-2");
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	kwl_list_pipeline_barrier(target, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* The whole image into the buffer. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = TEST_WIDTH;
	copy.imageExtent.height = TEST_HEIGHT;
	copy.imageExtent.depth = 1U;
	kwl_list_copy_image_to_buffer(target, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, (VkBuffer)(uintptr_t)test_handle("shot-buffer"), 1U, &copy);

	/* The copy visible to the host (with a memory barrier, which the list keeps too), and the image back to presentation. */
	memset(&memory, 0, sizeof(memory));
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	memset(&visible, 0, sizeof(visible));
	visible.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	visible.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.buffer = (VkBuffer)(uintptr_t)test_handle("shot-buffer");
	visible.size = VK_WHOLE_SIZE;
	kwl_list_pipeline_barrier(target, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &memory, 1U, &visible, 0U, NULL);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = 0U;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	kwl_list_pipeline_barrier(target, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
	test_scene_end(server, target);
}

/*
 * Writes into one list more operations, more array bytes and more vertices
 * than it first makes room for (1024 operations, 64 KiB of each), and
 * checks that the replay gives every one back in order: the push
 * constants' last values, the number of lines, and the vertices.
 */
static void
test_growth(
	struct kwl_server *server)
{
	static struct kwl_list list;
	float constants[KWL_PANEL_CONSTANTS];
	char line[256];
	char *read;
	FILE *saved;
	FILE *lines;
	size_t operations;
	unsigned checked;
	unsigned count;
	unsigned index;
	int joined;

	/* The replay's lines go to a file of their own. */
	saved = test_out;
	lines = tmpfile();
	assert(lines != NULL);
	test_out = lines;

	/* 3000 pushes and draws of distinct values (more than 1024 operations and 64 KiB of values). */
	kwl_list_reset(&list);
	kwl_compose_bind_forget(server->compose);
	kwl_batch_begin(server->compose);
	memset(constants, 0, sizeof(constants));
	constants[20] = (float)TEST_WIDTH;
	constants[21] = (float)TEST_HEIGHT;
	for (index = 0U; index < 3000U; index++) {
		/* One draw alone, its first value its number. */
		constants[0] = (float)index;
		kwl_list_push_constants(&list, server->compose->panel_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(constants), constants);
		kwl_list_draw(&list, KWL_QUAD_VERTICES, 1U, 0U, 0U);
	}

	/* The joined quads, drawn by the flush. */
	for (index = 0U; index < 600U; index++) {
		/* One quad joined, its corner its number. */
		constants[0] = (float)index;
		joined = kwl_batch_add(server, &list, constants, (VkDescriptorSet)(uintptr_t)test_handle("set:blurred:0"));
		assert(joined == 1);
	}

	/* The quads waiting, drawn. */
	kwl_batch_flush(server, &list);

	/* The list grew past its first room and kept every operation, array and vertex. */
	assert(!list.failed);
	assert(list.op_count > 1024U);
	assert(list.data_used > 64U * 1024U);
	assert(list.vertex_used == 600U * 6U * 11U * sizeof(float));
	assert(list.vertex_used == server->compose->batch.used);
	assert(kwl_list_things(&list) == 3600U);

	/* The replay, counted: two lines a push (its values), a line a draw, so the 3000th push's values are line 8998. */
	kwl_list_replay(&list, test_command);
	rewind(lines);
	checked = 0U;
	count = 0U;
	for (;;) {
		/* The next line, until the end. */
		read = fgets(line, sizeof(line), lines);
		if (read == NULL)
			break;

		/* The 3000th push's values start with its number. */
		if (count == 3U * 2999U + 1U) {
			assert(strncmp(line, "  values 2999 ", 14U) == 0);
			checked = 1U;
		}

		/* One line more. */
		count++;
	}

	/* The 3000th push was found where it belongs, after the rest. */
	assert(checked);
	assert(count >= 3U * 3000U);

	/* The vertices kept: the last quad's first vertex starts at its corner. */
	assert(*(const float *)(list.vertices + 599U * 6U * 11U * sizeof(float)) == 599.0f);

	/* The lines go back to the golden file's output. */
	operations = list.op_count;
	fclose(lines);
	test_out = saved;
	kwl_list_free(&list);
	fprintf(stderr, "list-replay-host-test: growth ok (%u lines, %zu operations)\n", count, operations);
}
