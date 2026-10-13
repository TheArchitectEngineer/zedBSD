/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the dock animation's pictures (ws211-p007,
 * userland/desktop/wayland/layer-cache.c): the output's pictures are made
 * three (Home's, the desktop's, the window's); an animation's first frame
 * draws the desktop's and the window's pictures once when shell.c allows
 * it, and none when it does not; its frames show them while both are drawn
 * and shell.c still allows it, and once a frame could not, the rest of the
 * animation draws everything; a new animation decides again; the window's
 * picture is sampled at the rectangle its body was drawn in.
 */

#include "userland/desktop/wayland/compose.h"
#include "userland/desktop/wayland/glass.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What shell.c answers (kwl_glass_anim_picturable), and what the fakes counted. */
static int picturable;
static unsigned passes;
static unsigned window_pictures;
static unsigned desktop_pictures;
static unsigned images_made;

/* Answers whether the animation's frames may show the pictures, as the test sets it. */
int
kwl_glass_anim_picturable(
	struct kwl_server *server,
	struct kwl_object **windows,
	unsigned count)
{
	(void)server;
	(void)windows;
	(void)count;
	return picturable;
}

/* Counts the window's picture drawn. */
void
kwl_glass_draw_anim_picture(
	struct kwl_server *server,
	struct kwl_list *command)
{
	(void)server;
	(void)command;
	window_pictures++;
}

/* Counts the desktop's picture drawn. */
void
kwl_glass_draw_desktop_picture(
	struct kwl_server *server,
	struct kwl_list *command,
	struct kwl_object **windows,
	unsigned count)
{
	(void)server;
	(void)command;
	(void)windows;
	(void)count;
	desktop_pictures++;
}

/* Draws nothing of Home. */
void
kwl_home_draw_content(
	struct kwl_server *server,
	struct kwl_list *command,
	float progress,
	unsigned neutral)
{
	(void)server;
	(void)command;
	(void)progress;
	(void)neutral;
}

/* Gives a fixed key of Home. */
uint64_t
kwl_home_picture_key(
	struct kwl_server *server)
{
	(void)server;
	return 1U;
}

/* Gives a fixed wallpaper version. */
uint32_t
glass_wallpaper_version(
	struct kwl_server *server)
{
	(void)server;
	return 1U;
}

/* Gives the tiles as ready. */
uint32_t
glass_tiles_ready(
	struct kwl_server *server)
{
	(void)server;
	return 1U;
}

/* Guards no glyph. */
void
glass_glyph_guard_begin(
	struct kwl_server *server)
{
	(void)server;
}

/* Reports no eviction. */
uint32_t
glass_glyph_guard_end(
	struct kwl_server *server)
{
	(void)server;
	return 0U;
}

/* Forgets no bind. */
void
kwl_compose_bind_forget(
	struct kwl_compose *compose)
{
	(void)compose;
}

/* Begins no batch. */
void
kwl_batch_begin(
	struct kwl_compose *compose)
{
	(void)compose;
}

/* Begins no list (layer-cache.c's quiet picture, ws213-p003). */
void
kwl_compose_list_begin(
	struct kwl_compose *compose)
{
	(void)compose;
}

/* Flushes no batch. */
void
kwl_batch_flush(
	struct kwl_server *server,
	struct kwl_list *command)
{
	(void)server;
	(void)command;
}

/* Gives a set. */
VkResult
kwl_compose_set_get(
	struct kwl_compose *compose,
	VkDescriptorSet *result)
{
	(void)compose;
	*result = (VkDescriptorSet)(uintptr_t)(0x100U + images_made);
	return VK_SUCCESS;
}

/* Takes a set back. */
void
kwl_compose_set_put(
	struct kwl_compose *compose,
	VkDescriptorSet set)
{
	(void)compose;
	(void)set;
}

/* Grants a quiet picture at once, as an idle render thread does (ws213-p004). */
int
kwl_compose_want_layer(
	struct kwl_server *server)
{
	(void)server;
	return 1;
}

/* Submits nothing. */
VkResult
kwl_compose_submit_quiet(
	struct kwl_server *server)
{
	(void)server;
	return VK_SUCCESS;
}

/* Gives Home closed. */
float
kwl_home_progress(
	struct kwl_server *server)
{
	(void)server;
	return 0.0f;
}

/* Gives a fixed time. */
uint64_t
kwl_milliseconds(
	void)
{
	return 1000U;
}

/* Gives a fixed time. */
uint64_t
kwl_microseconds(
	void)
{
	return 1000000U;
}

/* Makes a fake image. */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(
	VkDevice device,
	const VkImageCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkImage *image)
{
	(void)device;
	(void)info;
	(void)allocator;
	images_made++;
	*image = (VkImage)(uintptr_t)(0x200U + images_made);
	return VK_SUCCESS;
}

/* Gives the image's needs: one memory type. */
VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements(
	VkDevice device,
	VkImage image,
	VkMemoryRequirements *requirements)
{
	(void)device;
	(void)image;
	memset(requirements, 0, sizeof(*requirements));
	requirements->size = 4096U;
	requirements->memoryTypeBits = 1U;
}

/* Gives one device-local memory type. */
VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties(
	VkPhysicalDevice physical,
	VkPhysicalDeviceMemoryProperties *properties)
{
	(void)physical;
	memset(properties, 0, sizeof(*properties));
	properties->memoryTypeCount = 1U;
	properties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

/* Allocates fake memory. */
VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateMemory(
	VkDevice device,
	const VkMemoryAllocateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkDeviceMemory *memory)
{
	(void)device;
	(void)info;
	(void)allocator;
	*memory = (VkDeviceMemory)(uintptr_t)0x300U;
	return VK_SUCCESS;
}

/* Binds nothing. */
VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory(
	VkDevice device,
	VkImage image,
	VkDeviceMemory memory,
	VkDeviceSize offset)
{
	(void)device;
	(void)image;
	(void)memory;
	(void)offset;
	return VK_SUCCESS;
}

/* Makes a fake view. */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(
	VkDevice device,
	const VkImageViewCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkImageView *view)
{
	(void)device;
	(void)info;
	(void)allocator;
	*view = (VkImageView)(uintptr_t)0x400U;
	return VK_SUCCESS;
}

/* Makes a fake framebuffer. */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(
	VkDevice device,
	const VkFramebufferCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkFramebuffer *framebuffer)
{
	(void)device;
	(void)info;
	(void)allocator;
	*framebuffer = (VkFramebuffer)(uintptr_t)0x500U;
	return VK_SUCCESS;
}

/* Makes a fake pass. */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(
	VkDevice device,
	const VkRenderPassCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkRenderPass *pass)
{
	(void)device;
	(void)info;
	(void)allocator;
	*pass = (VkRenderPass)(uintptr_t)0x600U;
	return VK_SUCCESS;
}

/* Writes no set. */
VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(
	VkDevice device,
	uint32_t write_count,
	const VkWriteDescriptorSet *writes,
	uint32_t copy_count,
	const VkCopyDescriptorSet *copies)
{
	(void)device;
	(void)write_count;
	(void)writes;
	(void)copy_count;
	(void)copies;
}

/* Destroys nothing. */
VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(
	VkDevice device,
	VkFramebuffer framebuffer,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)framebuffer;
	(void)allocator;
}

/* Destroys nothing. */
VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(
	VkDevice device,
	VkImageView view,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)view;
	(void)allocator;
}

/* Destroys nothing. */
VKAPI_ATTR void VKAPI_CALL
vkDestroyImage(
	VkDevice device,
	VkImage image,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)image;
	(void)allocator;
}

/* Frees nothing. */
VKAPI_ATTR void VKAPI_CALL
vkFreeMemory(
	VkDevice device,
	VkDeviceMemory memory,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)memory;
	(void)allocator;
}

/* Destroys nothing. */
VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(
	VkDevice device,
	VkRenderPass pass,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)pass;
	(void)allocator;
}

/* Begins a fake recording. */
VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(
	VkCommandBuffer command,
	const VkCommandBufferBeginInfo *info)
{
	(void)command;
	(void)info;
	return VK_SUCCESS;
}

/* Ends a fake recording. */
VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(
	VkCommandBuffer command)
{
	(void)command;
	return VK_SUCCESS;
}

/* Counts a pass begun. */
void
kwl_list_begin_render_pass(
	struct kwl_list *command,
	const VkRenderPassBeginInfo *info,
	VkSubpassContents contents)
{
	(void)command;
	(void)info;
	(void)contents;
	passes++;
}

/* Ends a pass. */
void
kwl_list_end_render_pass(
	struct kwl_list *command)
{
	(void)command;
}

/* Takes a viewport. */
void
kwl_list_set_viewport(
	struct kwl_list *command,
	uint32_t first,
	uint32_t count,
	const VkViewport *viewports)
{
	(void)command;
	(void)first;
	(void)count;
	(void)viewports;
}

/* Takes a scissor. */
void
kwl_list_set_scissor(
	struct kwl_list *command,
	uint32_t first,
	uint32_t count,
	const VkRect2D *scissors)
{
	(void)command;
	(void)first;
	(void)count;
	(void)scissors;
}

/* Takes vertex buffers. */
void
kwl_list_bind_vertex_buffers(
	struct kwl_list *command,
	uint32_t first,
	uint32_t count,
	const VkBuffer *buffers,
	const VkDeviceSize *offsets)
{
	(void)command;
	(void)first;
	(void)count;
	(void)buffers;
	(void)offsets;
}

/* Runs the cases. */
int
main(
	void)
{
	struct kwl_server *server;
	struct kwl_compose *compose;
	struct kwl_object window;
	VkDescriptorSet set;
	float uv[4];
	int shown;

	/* A server with an output of 1000 by 500 and its pictures. */
	server = calloc(1U, sizeof(*server));
	compose = calloc(1U, sizeof(*compose));
	assert(server != NULL && compose != NULL);
	memset(&window, 0, sizeof(window));
	window.id = 7U;
	server->compose = compose;
	server->width = 1000U;
	server->height = 500U;
	compose->output.width = 1000U;
	compose->output.height = 500U;
	compose->target_width = 1000U;
	compose->target_height = 500U;
	kwl_layers_open(server);
	assert(compose->layers.ready);
	assert(images_made == KWL_LAYERS);
	assert(KWL_LAYERS == 3U);

	/* A dock that shell.c allows: the first frame draws both pictures once. */
	picturable = 1;
	server->anim = &window;
	server->anim_serial = 1U;
	server->anim_from[0] = 100;
	server->anim_from[1] = 50;
	server->anim_from[2] = 400;
	server->anim_from[3] = 250;
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 2U);
	assert(desktop_pictures == 1U);
	assert(window_pictures == 1U);
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 2U);

	/* Its frames show them, and the window's is sampled where its body was drawn. */
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 1);
	set = kwl_layers_anim_picture(server, uv);
	assert(set == compose->layers.images[KWL_LAYER_WINDOW].set);
	assert(uv[0] == 0.1f && uv[1] == 0.1f && uv[2] == 0.5f && uv[3] == 0.6f);

	/* The window's rectangle is the one drawn, not a later animation's start. */
	server->anim_from[0] = 0;
	(void)kwl_layers_anim_picture(server, uv);
	assert(uv[0] == 0.1f);

	/* A frame shell.c no longer allows takes the rest back to drawing everything. */
	picturable = 0;
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 0);
	picturable = 1;
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 0);
	assert(compose->layers.use_pictures == 1U);
	assert(compose->layers.use_drawn == 2U);

	/* A new animation decides again; one shell.c does not allow draws nothing. */
	picturable = 0;
	server->anim_serial = 2U;
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 2U);
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 0);

	/* A third that it allows, but whose window's picture was dropped, draws everything. */
	picturable = 1;
	server->anim_serial = 3U;
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 4U);
	compose->layers.valid[KWL_LAYER_WINDOW] = 0U;
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 0);

	/* No animation: nothing drawn, nothing shown. */
	server->anim = NULL;
	server->anim_serial = 4U;
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 4U);
	shown = kwl_layers_anim_showing(server, NULL, 0U);
	assert(shown == 0);

	/* The output's size changed without new pictures: nothing drawn. */
	server->anim = &window;
	server->anim_serial = 5U;
	server->width = 800U;
	kwl_layers_anim_inband(server, &compose->list, NULL, 0U);
	assert(passes == 4U);

	/* Succeeded. */
	kwl_layers_close(compose);
	free(compose);
	free(server);
	printf("PASS anim-host-test\n");
	return 0;
}
