/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The layers' pictures (ws211-p002, plan/ws211/phase001/phase.md, design
 * version 4): App Home's content and the desktop layer, each drawn into an
 * image of the output's size, so that App Home's opening and closing show
 * them as two pictures instead of drawing every shape of both in each frame
 * (on the Raspberry Pi 4 a frame of both is 460 to 900 draws).
 *
 * Both images are made with the output, in its format, so that the glass
 * look's pipelines draw into them as into the output; a device that cannot
 * make both keeps neither (KWL LAYER unavailable) and the compositor draws
 * as before.
 *
 * The desktop's picture is drawn in the first frame of each opening and
 * closing, in the frame's own commands before its pass (in-band): that
 * frame draws the desktop once, as every frame did before, and the frames
 * after it show the picture.  It shows the windows as they were then (the
 * change the user accepted on 2026-10-12); a window mapped later is drawn
 * over it, and a window in it that an animation starts to move takes the
 * frames back to drawing everything.
 *
 * Home's picture holds its content without the stage and without the clock
 * (drawn over it each frame), on transparent black, as Home opens (its
 * page's first application selected, no icon lit); its key
 * (kwl_home_picture_key) says what it shows, and is made again only when
 * something it shows may have changed (home_picture_stale).  A stale
 * picture is drawn again in-band in an animation's frame, or, while Home is
 * closed, as a frame of its own that presents nothing (compose->frame_bake)
 * when there has been no input and no window commit for LAYER_QUIET_MS, at
 * most twice a second.
 *
 * A glyph the atlas evicted after a picture was recorded (a text measured
 * outside a frame) may have reached the picture: a picture whose drawing
 * saw an eviction before it completed is drawn again (review-3 A1).  A
 * drawing that completes after the output was opened again is not taken
 * (the generation, review-3 A3).
 */

#include "compose.h"
#include "glass.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* How long the compositor has been without input and without a window commit before Home's picture is drawn on its own. */
#define LAYER_QUIET_MS		500U

/* How long since the last frame completed before it is. */
#define LAYER_FRAME_QUIET_MS	100U

/* The fewest milliseconds between the first and the third drawing on its own: two a second at most. */
#define LAYER_TURN_MS		1000U

/* The generations of the outputs' pictures (one each time an output opens). */
static uint64_t layers_generations;

static VkResult layers_pass(struct kwl_compose *compose);
static VkResult layers_image(struct kwl_compose *compose, struct kwl_layer_image *target);
static void layers_image_destroy(struct kwl_compose *compose, struct kwl_layer_image *target);
static void layers_destroy(struct kwl_compose *compose);
static void layers_record_pass(struct kwl_server *server, VkCommandBuffer command, unsigned which, struct kwl_object **windows, unsigned count);
static int layers_home_current(struct kwl_server *server);
static int layers_animating(struct kwl_server *server);
static void layers_remember(struct kwl_server *server, struct kwl_object **windows, unsigned count);
static void layers_log_use(struct kwl_layers *layers);

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

	/* Nothing is drawn yet; the size is the output's, the generation a new one. */
	compose = server->compose;
	layers = &compose->layers;
	memset(layers, 0, sizeof(*layers));
	layers->width = compose->output.width;
	layers->height = compose->output.height;
	layers_generations++;
	layers->generation = layers_generations;
	server->home_picture_stale = 1U;

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

/* Destroys the layers' pictures with the output (the device is idle); a drawing still to complete is not taken. */
void
kwl_layers_close(
	struct kwl_compose *compose)
{
	/* Whatever was made. */
	layers_destroy(compose);
	memset(&compose->layers, 0, sizeof(compose->layers));
}

/*
 * Draws Home's stale picture as a frame of its own that presents nothing,
 * when App Home is closed and the compositor has been quiet (the event
 * loop calls it when it has nothing to draw).  Returns 1 when the drawing
 * went in flight, 0 when none was due, and an errno value's negation when
 * it could not be submitted.
 */
int
kwl_layers_tick(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	VkResult result;
	uint64_t now;
	uint64_t key;

	/* Only with the pictures, the output, no frame in flight, in the glass look, with nothing else on the screen. */
	compose = server->compose;
	if (compose == NULL || !compose->output_open || compose->output_lost || compose->in_flight)
		return 0;
	layers = &compose->layers;
	if (!layers->ready)
		return 0;
	if (!server->glass || server->greeter || server->locked || server->screen_off)
		return 0;

	/* Only with Home closed and still; an animation ended says how it was drawn. */
	if (kwl_home_progress(server) > 0.0f || server->home_moving || server->home_dragging)
		return 0;
	layers_log_use(layers);

	/* Only when what Home shows may have changed, or what it is drawn from did. */
	if (!server->home_picture_stale &&
	    layers->valid[KWL_LAYER_HOME] &&
	    layers->home_wallpaper == glass_wallpaper_version(server) &&
	    layers->home_tiles == glass_tiles_ready(server))
		return 0;

	/* Only in the quiet: no input and no window commit for a while, no frame just done, two drawings a second at most. */
	now = kwl_milliseconds();
	if (server->dirty || server->damaged ||
	    now - server->input_ms < LAYER_QUIET_MS ||
	    now - server->commit_ms < LAYER_QUIET_MS ||
	    now - server->frame_done_ms < LAYER_FRAME_QUIET_MS ||
	    (layers->bake_times[0] != 0U && now - layers->bake_times[0] < LAYER_TURN_MS))
		return 0;

	/* The key made again: a picture that shows it already is kept. */
	server->home_picture_stale = 0U;
	key = kwl_home_picture_key(server);
	if (layers->valid[KWL_LAYER_HOME] && layers->keys[KWL_LAYER_HOME] == key &&
	    layers->home_wallpaper == glass_wallpaper_version(server) &&
	    layers->home_tiles == glass_tiles_ready(server))
		return 0;

	/* Recorded and submitted as a frame that presents nothing. */
	result = kwl_layers_begin_quiet(server);
	if (result == VK_SUCCESS) {
		layers_record_pass(server, compose->command, KWL_LAYER_HOME, NULL, 0U);
		result = vkEndCommandBuffer(compose->command);
	}
	if (result == VK_SUCCESS)
		result = kwl_compose_submit_quiet(server);
	if (result != VK_SUCCESS) {
		printf("KWL VULKAN_ERROR operation=layer result=%d\n", (int)result);
		return -EIO;
	}

	/* In flight: the picture is taken when it completes, if nothing changed under it. */
	compose->frame_bake = 1U;
	layers->keys[KWL_LAYER_HOME] = key;
	layers->bake_times[0] = layers->bake_times[1];
	layers->bake_times[1] = now;
	printf("KWL LAYER baked what=home inband=0 draws=%u at_us=%llu\n", layers->bake_draws, (unsigned long long)kwl_microseconds());
	return 1;
}

/* Begins a fresh recording of compose->command for a frame that presents nothing. */
VkResult
kwl_layers_begin_quiet(
	struct kwl_server *server)
{
	VkCommandBufferBeginInfo begin;

	/* Beginning the command buffer resets it. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	return vkBeginCommandBuffer(server->compose->command, &begin);
}

/*
 * Draws, in the frame being recorded and before its pass, the pictures an
 * animation of App Home needs and lacks (review-3, Q1's choice): the
 * desktop's picture in the first frame of each opening and closing, and
 * Home's picture when what it shows changed.  The frame's hold covers the
 * windows' images the desktop's picture samples.
 */
void
kwl_layers_inband(
	struct kwl_server *server,
	VkCommandBuffer command,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_layers *layers;
	uint64_t key;

	/* Only Home's own animation, with the pictures. */
	layers = &server->compose->layers;
	if (!layers->ready || !layers_animating(server))
		return;

	/* The desktop's picture once an animation, with the windows it shows. */
	if (layers->desktop_anim != server->home_start_ms || !layers->valid[KWL_LAYER_DESKTOP]) {
		layers_record_pass(server, command, KWL_LAYER_DESKTOP, windows, count);
		layers_remember(server, windows, count);
		layers->desktop_anim = server->home_start_ms;
		printf("KWL LAYER baked what=desktop inband=1 draws=%u at_us=%llu\n", layers->bake_draws, (unsigned long long)kwl_microseconds());
	}

	/* Home's picture when it does not show what is there. */
	if (!layers_home_current(server)) {
		key = kwl_home_picture_key(server);
		layers_record_pass(server, command, KWL_LAYER_HOME, NULL, 0U);
		layers->keys[KWL_LAYER_HOME] = key;
		printf("KWL LAYER baked what=home inband=1 draws=%u at_us=%llu\n", layers->bake_draws, (unsigned long long)kwl_microseconds());
	}
}

/*
 * Tells whether this frame of App Home's animation shows the pictures
 * (shell.c): Home opening or closing on its own, not followed by a finger,
 * without a search, both pictures drawn for it, and no window of the
 * desktop's picture moved by an animation since.  The frames are counted
 * for the log (KWL LAYER use).
 */
int
kwl_layers_showing(
	struct kwl_server *server)
{
	struct kwl_layers *layers;
	unsigned index;
	int shown;

	/* Only Home's own animation, with both pictures drawn for it. */
	layers = &server->compose->layers;
	shown = 0;
	if (layers->ready && layers_animating(server) &&
	    layers->valid[KWL_LAYER_DESKTOP] && layers->desktop_anim == server->home_start_ms &&
	    layers->valid[KWL_LAYER_HOME] && layers_home_current(server))
		shown = 1;

	/* A window of the picture an animation moves is drawn as it moves: everything is drawn. */
	if (shown && server->anim != NULL && server->anim->client != NULL) {
		for (index = 0U; index < layers->window_count; index++) {
			if (layers->window_clients[index] == server->anim->client->number && layers->window_ids[index] == server->anim->id)
				shown = 0;
		}
	}

	/* The frames of an animation, counted. */
	if (layers_animating(server)) {
		if (shown)
			layers->use_pictures++;
		else
			layers->use_drawn++;
	}

	/* Succeeded: whether the frame shows the pictures. */
	return shown;
}

/* Tells whether a window is in the desktop's picture (the windows mapped since are drawn over it). */
int
kwl_layers_pictured(
	struct kwl_server *server,
	const struct kwl_object *surface)
{
	struct kwl_layers *layers;
	unsigned index;

	/* By its client's number and its id, not by where it is in memory (review-3 B5). */
	layers = &server->compose->layers;
	if (surface->client == NULL)
		return 0;
	for (index = 0U; index < layers->window_count; index++) {
		if (layers->window_clients[index] == surface->client->number && layers->window_ids[index] == surface->id)
			return 1;
	}

	/* Not in it. */
	return 0;
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

/*
 * Ends a submission that drew pictures (a frame, or one that presents
 * nothing): they are taken unless the output was opened again since, or a
 * glyph was evicted from the atlas while they waited (review-3 A1, A3).
 */
void
kwl_layers_complete(
	struct kwl_server *server,
	uint64_t generation)
{
	struct kwl_layers *layers;
	uint32_t evictions;
	unsigned which;

	/* Nothing of another output's pictures. */
	layers = &server->compose->layers;
	if (!layers->ready || generation != layers->generation)
		return;

	/* Each picture this submission drew, taken or drawn again later. */
	evictions = glass_glyph_evictions(server);
	for (which = 0U; which < KWL_LAYERS; which++) {
		if ((layers->inflight & (1U << which)) == 0U)
			continue;
		layers->valid[which] = evictions == layers->evictions[which] ? 1U : 0U;
		if (!layers->valid[which]) {
			server->home_picture_stale = 1U;
			printf("KWL LAYER dropped what=%s reason=eviction\n", which == KWL_LAYER_HOME ? "home" : "desktop");
		}
	}
	layers->inflight = 0U;
}

/* Tells whether App Home's own animation runs: opening or closing, not followed by a finger, without a search. */
static int
layers_animating(
	struct kwl_server *server)
{
	/* Moving on its own, its first frame recorded. */
	if (!server->home_moving || server->home_dragging || server->home_start_pending)
		return 0;

	/* No search typed or being composed. */
	if (server->home_query_length != 0U || server->home_preedit[0] != '\0')
		return 0;

	/* Succeeded. */
	return 1;
}

/*
 * Tells whether Home's picture shows what Home would show: drawn, from the
 * wallpaper and the tiles as they are, and with the key made again when
 * something it shows may have changed.
 */
static int
layers_home_current(
	struct kwl_server *server)
{
	struct kwl_layers *layers;
	uint64_t key;

	/* Drawn, from the wallpaper and the tiles as they are. */
	layers = &server->compose->layers;
	if (!layers->valid[KWL_LAYER_HOME])
		return 0;
	if (layers->home_wallpaper != glass_wallpaper_version(server) || layers->home_tiles != glass_tiles_ready(server))
		return 0;

	/* Nothing it shows changed since its key was made. */
	if (!server->home_picture_stale)
		return 1;

	/* The key made again. */
	server->home_picture_stale = 0U;
	key = kwl_home_picture_key(server);
	return key == layers->keys[KWL_LAYER_HOME];
}

/* Remembers the windows the desktop's picture shows, by their clients' numbers and ids (the one an animation moves left out). */
static void
layers_remember(
	struct kwl_server *server,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_layers *layers;
	unsigned index;

	/* Each window but the animated one, while there is room. */
	layers = &server->compose->layers;
	layers->window_count = 0U;
	for (index = 0U; index < count && layers->window_count < KWL_FRAME_WINDOWS; index++) {
		if (windows[index] == server->anim || windows[index]->client == NULL)
			continue;
		layers->window_clients[layers->window_count] = windows[index]->client->number;
		layers->window_ids[layers->window_count] = windows[index]->id;
		layers->window_count++;
	}
}

/* Logs how the frames of the animation that ended were drawn, once (KWL LAYER use). */
static void
layers_log_use(
	struct kwl_layers *layers)
{
	/* Nothing counted since the last. */
	if (layers->use_pictures == 0U && layers->use_drawn == 0U)
		return;
	printf("KWL LAYER use pictures=%u drawn=%u\n", layers->use_pictures, layers->use_drawn);
	layers->use_pictures = 0U;
	layers->use_drawn = 0U;
}

/*
 * Records a picture into a command buffer, outside any pass: the layers'
 * pass on its image, cleared to transparent black, and Home's content drawn
 * as it opens, without placing it, in its own colours, with no icon lit and
 * without the clock; or the desktop layer as Home has it, in the
 * appearance's colours.  Every state the glass look's drawing reads is set
 * for the pass and given back (review-2 N5).  The submission that carries
 * it takes the picture when it completes (kwl_layers_complete).
 */
static void
layers_record_pass(
	struct kwl_server *server,
	VkCommandBuffer command,
	unsigned which,
	struct kwl_object **windows,
	unsigned count)
{
	struct kwl_compose *compose;
	struct kwl_layers *layers;
	struct kwl_draw_counts counts;
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkViewport viewport;
	VkRect2D scissor;
	VkRect2D scissor_saved;
	VkFramebuffer framebuffer_saved;
	VkDescriptorSet backdrop_saved;
	VkDeviceSize offset;
	unsigned recording_saved;
	unsigned keep_saved;
	unsigned section_saved;
	unsigned backdrop_recording_saved;
	unsigned layer_saved;
	int32_t view_x_saved;
	int32_t view_y_saved;
	uint32_t view_width_saved;
	uint32_t view_height_saved;

	/* The pass on the picture's image, cleared to transparent black. */
	compose = server->compose;
	layers = &compose->layers;
	memset(&clear, 0, sizeof(clear));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = layers->pass;
	pass.framebuffer = layers->images[which].framebuffer;
	pass.renderArea.extent.width = layers->width;
	pass.renderArea.extent.height = layers->height;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);

	/* The pass's own viewport, scissor and quad corners. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)layers->width;
	viewport.height = (float)layers->height;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command, 0U, 1U, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = layers->width;
	scissor.extent.height = layers->height;
	vkCmdSetScissor(command, 0U, 1U, &scissor);
	offset = 0U;
	vkCmdBindVertexBuffers(command, 0U, 1U, &compose->corners, &offset);

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

	/* Home's content as it opens, no icon lit, no clock; or the desktop layer. */
	if (which == KWL_LAYER_HOME)
		kwl_home_draw_content(server, command, 1.0f, 1U);
	else
		kwl_glass_draw_desktop_picture(server, command, windows, count);
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
	vkCmdEndRenderPass(command);

	/*
	 * The picture is drawn as far as the frames after it in the queue are
	 * concerned (the pass's dependency); it is taken when the submission
	 * completes without an eviction (kwl_layers_complete).
	 */
	layers->valid[which] = 1U;
	layers->inflight |= 1U << which;
	layers->evictions[which] = glass_glyph_evictions(server);
	if (which == KWL_LAYER_HOME) {
		layers->home_wallpaper = glass_wallpaper_version(server);
		layers->home_tiles = glass_tiles_ready(server);
	}
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
