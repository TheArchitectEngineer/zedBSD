/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The layers' pictures (ws211-p002, plan/ws211/phase001/phase.md): App
 * Home's content and the desktop layer, each drawn once into an image of
 * the output's size, so that App Home's opening and closing can show them
 * as two pictures instead of drawing every shape of both in each frame (on
 * the Raspberry Pi 4 a frame of both is 460 to 900 draws).
 *
 * Both images are made with the output, in its format, so that the glass
 * look's pipelines draw into them as into the output; a device that cannot
 * make both keeps neither (KWL LAYER unavailable) and the compositor draws
 * as before.  Home's picture holds its content without the stage, on
 * transparent black, and without an icon lit under the pointer; its key
 * (kwl_home_picture_key) says what it shows.  While Home is closed, a
 * picture whose key changed is drawn again as a frame of its own that
 * presents nothing (compose->frame_bake): it is submitted and completed as
 * a frame, so that the host images it samples are copied first, in the
 * same submission.  It waits for a quiet moment (no frame and no
 * input for LAYER_QUIET_MS) and for its turn (two a second at most), so
 * that it does not hold up a frame the user waits for.
 *
 * App Home's opening and closing show the pictures (shell.c,
 * kwl_layers_showing) once both show what is there; the logs say when a
 * picture was drawn (KWL LAYER baked) and when an animation shows them
 * (KWL LAYER shown).
 */

#include "compose.h"
#include "glass.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* How long the compositor has been without a frame and without input before a picture is drawn again. */
#define LAYER_QUIET_MS		100U

/* The fewest milliseconds between the first and the third drawing of the pictures: two a second at most. */
#define LAYER_TURN_MS		1000U

static VkResult layers_pass(struct kwl_compose *compose);
static VkResult layers_image(struct kwl_compose *compose, struct kwl_layer_image *target);
static void layers_image_destroy(struct kwl_compose *compose, struct kwl_layer_image *target);
static void layers_destroy(struct kwl_compose *compose);
static VkResult layers_record(struct kwl_server *server, unsigned which, struct kwl_object **windows, unsigned count);
static int layers_bake(struct kwl_server *server, unsigned which, uint64_t key, struct kwl_object **windows, unsigned count);
static uint64_t layers_desktop_key(struct kwl_server *server, struct kwl_object **windows, unsigned count);
static uint64_t layers_hash(uint64_t hash, const void *bytes, size_t length);

/*
 * Makes the layers' pictures for the output just opened: the pass and both
 * images.  Without both the pictures are unavailable for the output's life.
 */
void
kwl_layers_open(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	VkResult result;
	unsigned index;

	/* Nothing is drawn yet; the size is the output's. */
	compose = server->compose;
	layers = &compose->layers;
	memset(layers, 0, sizeof(*layers));
	layers->width = compose->output.width;
	layers->height = compose->output.height;

	/* The pass, then each image. */
	result = layers_pass(compose);
	for (index = 0U; index < KWL_LAYERS && result == VK_SUCCESS; index++)
		result = layers_image(compose, &layers->images[index]);

	/* Both or neither. */
	if (result != VK_SUCCESS) {
		layers_destroy(compose);
		layers->failed = 1U;
		printf("KWL LAYER unavailable result=%d\n", (int)result);
		return;
	}

	/* Succeeded: the pictures can be drawn. */
	layers->ready = 1U;
	printf("KWL LAYER ready width=%u height=%u images=%u\n", layers->width, layers->height, KWL_LAYERS);
}

/* Destroys the layers' pictures with the output (the device is idle). */
void
kwl_layers_close(
	struct kwl_compose *compose)
{
	/* Whatever was made. */
	layers_destroy(compose);
	memset(&compose->layers, 0, sizeof(compose->layers));
}

/*
 * Draws a layer's picture again when it is stale and the moment is right.
 * The event loop calls it before each frame it would draw and when it has
 * nothing to draw.  While App Home is closed a stale picture is drawn in a
 * quiet moment; while Home's own animation runs, a picture stale at its
 * start is drawn once, after the animation's first frame (review-2 N9), and
 * the frames after it show the pictures.  Returns 1 when a drawing went in
 * flight (the frame waits for it), 0 when none was due, and an errno
 * value's negation when the drawing could not be submitted.
 */
int
kwl_layers_tick(
	struct kwl_server *server)
{
	struct kwl_object *windows[KWL_FRAME_WINDOWS];
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	uint64_t now;
	uint64_t keys[KWL_LAYERS];
	unsigned stale[KWL_LAYERS];
	unsigned count;
	unsigned which;
	float home;

	/* Only with the pictures, the output, no frame in flight, in the glass look, with nothing else on the screen. */
	compose = server->compose;
	if (compose == NULL || !compose->output_open || compose->output_lost || compose->in_flight)
		return 0;
	layers = &compose->layers;
	if (!layers->ready)
		return 0;
	if (!server->glass || server->greeter || server->locked || server->screen_off)
		return 0;

	/* Home opening or closing on its own decides only after its first frame; otherwise only in a quiet moment, twice a second at most. */
	home = kwl_home_progress(server);
	now = kwl_milliseconds();
	if (home > 0.0f) {
		if (!server->home_moving || server->home_dragging || server->home_start_pending)
			return 0;
	} else if (server->home_dragging ||
	    server->dirty ||
	    server->damaged ||
	    now - server->frame_done_ms < LAYER_QUIET_MS ||
	    now - server->lock_input_ms < LAYER_QUIET_MS ||
	    (layers->bake_times[0] != 0U && now - layers->bake_times[0] < LAYER_TURN_MS)) {
		return 0;
	}

	/* What the pictures would show now, and which are stale. */
	count = kwl_compose_layer_windows(server, windows, KWL_FRAME_WINDOWS);
	keys[KWL_LAYER_HOME] = kwl_home_picture_key(server);
	keys[KWL_LAYER_DESKTOP] = layers_desktop_key(server, windows, count);
	stale[KWL_LAYER_HOME] = !layers->valid[KWL_LAYER_HOME] || layers->keys[KWL_LAYER_HOME] != keys[KWL_LAYER_HOME];
	stale[KWL_LAYER_DESKTOP] = !layers->valid[KWL_LAYER_DESKTOP] ||
	    layers->keys[KWL_LAYER_DESKTOP] != keys[KWL_LAYER_DESKTOP] ||
	    layers->commits != server->commit_count;

	/* Home's animation draws each stale picture once (the windows' contents then stay as drawn); Home closed draws Home's first. */
	for (which = 0U; which < KWL_LAYERS; which++) {
		if (!stale[which])
			continue;
		if (home > 0.0f && layers->anim_baked[which] == server->home_start_ms)
			continue;
		if (home > 0.0f)
			layers->anim_baked[which] = server->home_start_ms;
		return layers_bake(server, which, keys[which], windows, count);
	}

	/* Nothing stale. */
	return 0;
}

/*
 * Tells whether this frame of App Home's animation shows the pictures
 * (shell.c): Home opening or closing on its own, not followed by a finger,
 * without a search, both pictures drawn and showing what is there now (the
 * desktop's windows as they are placed, the window an animation moves left
 * out of it; their contents as at the picture's drawing, the change the
 * user accepted on 2026-10-12).
 */
int
kwl_layers_showing(
	struct kwl_server *server)
{
	struct kwl_object *windows[KWL_FRAME_WINDOWS];
	struct kwl_layers *layers;
	unsigned count;

	/* Only Home's own animation, with both pictures. */
	layers = &server->compose->layers;
	if (!layers->ready || !server->home_moving || server->home_dragging)
		return 0;
	if (server->home_query_length != 0U || server->home_preedit[0] != '\0')
		return 0;
	if (!layers->valid[KWL_LAYER_HOME] || !layers->valid[KWL_LAYER_DESKTOP])
		return 0;

	/* Both show what is there now. */
	if (layers->keys[KWL_LAYER_HOME] != kwl_home_picture_key(server))
		return 0;
	count = kwl_compose_layer_windows(server, windows, KWL_FRAME_WINDOWS);
	if (layers->keys[KWL_LAYER_DESKTOP] != layers_desktop_key(server, windows, count))
		return 0;

	/* Succeeded: the frame shows the pictures; the log says so once an animation. */
	if (layers->shown_start != server->home_start_ms) {
		layers->shown_start = server->home_start_ms;
		printf("KWL LAYER shown frame=%llu at_us=%llu\n", (unsigned long long)server->frame + 1U, (unsigned long long)kwl_microseconds());
	}
	return 1;
}

/* Gives a picture's descriptor set, to sample it (KWL_LAYER_HOME or KWL_LAYER_DESKTOP). */
VkDescriptorSet
kwl_layers_set(
	struct kwl_server *server,
	unsigned which)
{
	/* The picture's set. */
	return server->compose->layers.images[which].set;
}

/* Ends a picture's drawing whose fence signaled: the picture shows its key from now on. */
void
kwl_layers_complete(
	struct kwl_server *server)
{
	struct kwl_layers *layers;

	/* The picture drawn, its key, and the windows' commits it shows. */
	layers = &server->compose->layers;
	layers->keys[layers->baking] = layers->baking_key;
	layers->valid[layers->baking] = 1U;
	if (layers->baking == KWL_LAYER_DESKTOP)
		layers->commits = layers->baking_commits;

	/* The log says what was drawn, its draws, and how long from its recording to its completion. */
	printf(
		"KWL LAYER baked what=%s draws=%u us=%llu at_us=%llu\n",
		layers->baking == KWL_LAYER_HOME ? "home" : "desktop",
		layers->bake_draws,
		(unsigned long long)(kwl_microseconds() - layers->bake_started_us),
		(unsigned long long)kwl_microseconds());
}

/*
 * Draws one picture again: records it, holds what it samples, and submits
 * it as a frame that presents nothing.  Returns 1 in flight, or -EIO.
 */
static int
layers_bake(
	struct kwl_server *server,
	unsigned which,
	uint64_t key,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	VkResult result;
	uint64_t now;

	/* Recorded, its windows' buffers held, then submitted as a frame that presents nothing. */
	compose = server->compose;
	layers = &compose->layers;
	now = kwl_milliseconds();
	layers->bake_started_us = kwl_microseconds();
	result = layers_record(server, which, windows, count);
	if (result == VK_SUCCESS && which == KWL_LAYER_DESKTOP)
		kwl_compose_bake_hold(server, windows, count);
	if (result == VK_SUCCESS)
		result = kwl_compose_submit_quiet(server);
	if (result != VK_SUCCESS) {
		printf("KWL VULKAN_ERROR operation=layer result=%d\n", (int)result);
		return -EIO;
	}

	/* The new images' layouts went with it. */
	if (which == KWL_LAYER_DESKTOP)
		kwl_import_layouts_done(compose);

	/* In flight: its turn is counted, and its key is the picture's once it completes. */
	compose->frame_bake = 1U;
	layers->baking = which;
	layers->baking_key = key;
	layers->baking_commits = server->commit_count;
	layers->valid[which] = 0U;
	layers->bake_times[0] = layers->bake_times[1];
	layers->bake_times[1] = now;
	return 1;
}

/*
 * Gives the key of what the desktop's picture shows (review-2 N6): the
 * desktop shown, the windows drawn in it in their order, with their places,
 * sizes, states and titles, the window an animation moves (left
 * out of the picture), and the wallpaper, the appearance, the accent and
 * the output's size.  The windows' contents are not in it: the windows'
 * commits are counted apart (kwl_server's commit_count), so that a picture
 * drawn before them is drawn again while Home is closed but still shown
 * during Home's animation.
 */
static uint64_t
layers_desktop_key(
	struct kwl_server *server,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_object *surface;
	uint64_t hash;
	uint64_t values[8];
	unsigned index;

	/* The state of the whole layer. */
	hash = 14695981039346656037ULL;
	values[0] = server->desktop;
	values[1] = (uint64_t)(uintptr_t)server->anim;
	values[2] = glass_wallpaper_version(server);
	values[3] = (uint64_t)(int64_t)server->dark;
	values[4] = (uint64_t)(int64_t)server->accent;
	values[5] = ((uint64_t)server->width << 32) | server->height;
	values[6] = count;
	values[7] = 0U;
	hash = layers_hash(hash, values, sizeof(values));

	/* Each window, bottom to top. */
	for (index = 0U; index < count; index++) {
		surface = windows[index];
		values[0] = (uint64_t)(uintptr_t)surface;
		values[1] = ((uint64_t)(uint32_t)surface->x << 32) | (uint32_t)surface->y;
		values[2] = ((uint64_t)surface->window_width << 32) | surface->window_height;
		values[3] = surface->desktop;
		values[4] = ((uint64_t)surface->minimized << 2) | ((uint64_t)surface->maximized << 1) | surface->fullscreen;
		values[5] = 0U;
		values[6] = surface->map_order;
		values[7] = 0U;
		hash = layers_hash(hash, values, sizeof(values));
		hash = layers_hash(hash, surface->title, strnlen(surface->title, sizeof(surface->title)));
	}

	/* Succeeded: the key. */
	return hash;
}

/* Adds bytes to a 64-bit FNV-1a hash. */
static uint64_t
layers_hash(
	uint64_t hash,
	const void *bytes,
	size_t length)
{
	const uint8_t *byte;
	size_t index;

	/* Each byte in turn. */
	byte = bytes;
	for (index = 0; index < length; index++) {
		hash ^= byte[index];
		hash *= 1099511628211ULL;
	}

	/* The hash with the bytes. */
	return hash;
}

/*
 * Records a picture into compose->command: the layers' pass on its image,
 * cleared to transparent black, and Home's content drawn as it is open,
 * without placing it, in its own colours, with no icon lit; or the desktop
 * layer as Home has it, in the appearance's colours (the new client images'
 * layouts first, as a frame's).  Every state the glass look's drawing reads
 * is set for the pass and given back (review-2 N5).
 */
static VkResult
layers_record(
	struct kwl_server *server,
	unsigned which,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	struct kwl_draw_counts counts;
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkViewport viewport;
	VkRect2D scissor;
	VkRect2D scissor_saved;
	VkFramebuffer framebuffer_saved;
	VkDescriptorSet backdrop_saved;
	VkDeviceSize offset;
	VkResult result;
	unsigned recording_saved;
	unsigned keep_saved;
	unsigned section_saved;
	unsigned backdrop_recording_saved;
	unsigned layer_saved;
	int32_t view_x_saved;
	int32_t view_y_saved;
	uint32_t view_width_saved;
	uint32_t view_height_saved;

	/* A fresh recording. */
	compose = server->compose;
	layers = &compose->layers;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(compose->command, &begin);
	if (result != VK_SUCCESS)
		return result;

	/* The clients' images imported since the last frame go to the layout they are sampled in. */
	if (which == KWL_LAYER_DESKTOP)
		kwl_import_layouts_record(compose, compose->command);

	/* The pass on the picture's image, cleared to transparent black. */
	memset(&clear, 0, sizeof(clear));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = layers->pass;
	pass.framebuffer = layers->images[which].framebuffer;
	pass.renderArea.extent.width = layers->width;
	pass.renderArea.extent.height = layers->height;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(compose->command, &pass, VK_SUBPASS_CONTENTS_INLINE);

	/* The pass's own viewport, scissor and quad corners. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)layers->width;
	viewport.height = (float)layers->height;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(compose->command, 0U, 1U, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = layers->width;
	scissor.extent.height = layers->height;
	vkCmdSetScissor(compose->command, 0U, 1U, &scissor);
	offset = 0U;
	vkCmdBindVertexBuffers(compose->command, 0U, 1U, &compose->corners, &offset);

	/* The state the drawing reads, kept to be given back. */
	scissor_saved = compose->scissor_now;
	framebuffer_saved = compose->framebuffer_now;
	backdrop_saved = compose->backdrop_set;
	backdrop_recording_saved = compose->backdrop_recording;
	recording_saved = server->hover_recording;
	keep_saved = server->keep_colours;
	section_saved = server->draw_section;
	layer_saved = server->layer_on;
	view_x_saved = server->view_x;
	view_y_saved = server->view_y;
	view_width_saved = server->view_width;
	view_height_saved = server->view_height;
	counts = server->draw_counts;

	/* The whole image, unplaced, without the backdrop or hover tests; Home in its own colours. */
	compose->scissor_now = scissor;
	compose->framebuffer_now = VK_NULL_HANDLE;
	compose->backdrop_set = VK_NULL_HANDLE;
	compose->backdrop_recording = 0U;
	server->hover_recording = 0U;
	server->keep_colours = which == KWL_LAYER_HOME ? 1U : 0U;
	server->draw_section = which == KWL_LAYER_HOME ? KWL_SECTION_HOME : KWL_SECTION_LAYER;
	server->layer_on = 0U;
	server->view_x = 0;
	server->view_y = 0;
	server->view_width = 0U;
	server->view_height = 0U;
	memset(&server->draw_counts, 0, sizeof(server->draw_counts));

	/* Home's content as it is open, no icon lit; or the desktop layer. */
	if (which == KWL_LAYER_HOME)
		kwl_home_draw_content(server, compose->command, 1.0f, 1U);
	else
		kwl_glass_draw_desktop_picture(server, compose->command, windows, count);
	layers->bake_draws = server->draw_counts.shapes + server->draw_counts.quads;

	/* The state given back. */
	compose->scissor_now = scissor_saved;
	compose->framebuffer_now = framebuffer_saved;
	compose->backdrop_set = backdrop_saved;
	compose->backdrop_recording = backdrop_recording_saved;
	server->hover_recording = recording_saved;
	server->keep_colours = keep_saved;
	server->draw_section = section_saved;
	server->layer_on = layer_saved;
	server->view_x = view_x_saved;
	server->view_y = view_y_saved;
	server->view_width = view_width_saved;
	server->view_height = view_height_saved;
	server->draw_counts = counts;

	/* The pass ends with the image ready to be sampled. */
	vkCmdEndRenderPass(compose->command);

	/* Succeeded: the recording is complete. */
	return vkEndCommandBuffer(compose->command);
}

/*
 * Makes the layers' pass: one attachment of the output's format, cleared,
 * drawn and left to be sampled; written after the last reads of the image,
 * and read after the writes.
 */
static VkResult
layers_pass(
	struct kwl_compose *compose)
{
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkSubpassDependency dependencies[2];
	VkRenderPassCreateInfo pass;

	/* One colour attachment of the output's format. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = compose->format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&reference, 0, sizeof(reference));
	reference.attachment = 0U;
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1U;
	subpass.pColorAttachments = &reference;

	/* Written after the last reads (and writes) of the image. */
	memset(dependencies, 0, sizeof(dependencies));
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0U;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	/* Read after the writes. */
	dependencies[1].srcSubpass = 0U;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	/* The pass. */
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass.attachmentCount = 1U;
	pass.pAttachments = &attachment;
	pass.subpassCount = 1U;
	pass.pSubpasses = &subpass;
	pass.dependencyCount = 2U;
	pass.pDependencies = dependencies;
	return vkCreateRenderPass(compose->device, &pass, NULL, &compose->layers.pass);
}

/* Makes one picture: an image of the output's size and format, its memory, view, framebuffer and linearly sampled set. */
static VkResult
layers_image(
	struct kwl_compose *compose,
	struct kwl_layer_image *target)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryRequirements requirements;
	VkImageCreateInfo image;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkFramebufferCreateInfo framebuffer;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet write;
	uint32_t index;
	VkResult result;

	/* The image: drawn into and sampled. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = compose->format;
	image.extent.width = compose->layers.width;
	image.extent.height = compose->layers.height;
	image.extent.depth = 1U;
	image.mipLevels = 1U;
	image.arrayLayers = 1U;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(compose->device, &image, NULL, &target->image);
	if (result != VK_SUCCESS)
		return result;

	/* Device memory for it: the device's own type first, else any it takes. */
	vkGetImageMemoryRequirements(compose->device, target->image, &requirements);
	vkGetPhysicalDeviceMemoryProperties(compose->physical, &memory);
	for (index = 0; index < memory.memoryTypeCount; index++) {
		if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
		    (memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U)
			break;
	}
	if (index == memory.memoryTypeCount) {
		for (index = 0; index < memory.memoryTypeCount; index++) {
			if ((requirements.memoryTypeBits & (1U << index)) != 0U)
				break;
		}
	}
	if (index == memory.memoryTypeCount)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* The memory, bound. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = index;
	result = vkAllocateMemory(compose->device, &allocate, NULL, &target->memory);
	if (result != VK_SUCCESS)
		return result;
	result = vkBindImageMemory(compose->device, target->image, target->memory, 0U);
	if (result != VK_SUCCESS)
		return result;

	/* Its view. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = target->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = compose->format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(compose->device, &view, NULL, &target->view);
	if (result != VK_SUCCESS)
		return result;

	/* The framebuffer of the layers' pass. */
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = compose->layers.pass;
	framebuffer.attachmentCount = 1U;
	framebuffer.pAttachments = &target->view;
	framebuffer.width = compose->layers.width;
	framebuffer.height = compose->layers.height;
	framebuffer.layers = 1U;
	result = vkCreateFramebuffer(compose->device, &framebuffer, NULL, &target->framebuffer);
	if (result != VK_SUCCESS)
		return result;

	/* The descriptor set, sampled linearly (the pictures are drawn smaller and larger). */
	result = kwl_compose_set_get(compose, &target->set);
	if (result != VK_SUCCESS)
		return result;
	memset(&image_info, 0, sizeof(image_info));
	image_info.sampler = compose->linear_sampler;
	image_info.imageView = target->view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = target->set;
	write.dstBinding = 0U;
	write.descriptorCount = 1U;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image_info;
	vkUpdateDescriptorSets(compose->device, 1U, &write, 0U, NULL);

	/* Succeeded. */
	return VK_SUCCESS;
}

/* Destroys what was made of one picture. */
static void
layers_image_destroy(
	struct kwl_compose *compose,
	struct kwl_layer_image *target)
{
	/* In the reverse order of making. */
	if (target->set != VK_NULL_HANDLE)
		kwl_compose_set_put(compose, target->set);
	if (target->framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(compose->device, target->framebuffer, NULL);
	if (target->view != VK_NULL_HANDLE)
		vkDestroyImageView(compose->device, target->view, NULL);
	if (target->image != VK_NULL_HANDLE)
		vkDestroyImage(compose->device, target->image, NULL);
	if (target->memory != VK_NULL_HANDLE)
		vkFreeMemory(compose->device, target->memory, NULL);
	memset(target, 0, sizeof(*target));
}

/* Destroys both pictures and the pass (the device is idle). */
static void
layers_destroy(
	struct kwl_compose *compose)
{
	unsigned index;

	/* Each picture, then the pass. */
	for (index = 0U; index < KWL_LAYERS; index++)
		layers_image_destroy(compose, &compose->layers.images[index]);
	if (compose->layers.pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(compose->device, compose->layers.pass, NULL);
	compose->layers.pass = VK_NULL_HANDLE;
	compose->layers.ready = 0U;
}
