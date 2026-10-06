/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's screen capture for the Agent Acceptance Test
 * (ws173-p002).  Test images only: this file is built into the compositor
 * only when the image's configuration sets ZEDBSD_TEST_SCREEN_CAPTURE=y;
 * otherwise shot-none.c, whose functions do nothing, takes its place, so a
 * release image has no capture at all.
 *
 * The compositor listens on $XDG_RUNTIME_DIR/keiland-shot.sock, or on
 * /tmp/keiland-shot.<uid>.sock when XDG_RUNTIME_DIR is not set (the login
 * screen), mode 0600 and owned by the compositor's user: only that user
 * and root can connect.  A connection sends one line:
 *
 *   PING   answered "ACTIVE" when this compositor shows the display now,
 *          "INACTIVE" otherwise
 *   SHOT   the next frame is drawn whole and copied from its swapchain
 *          image (Vulkan readback, on i915 and on Venus alike); answered
 *          "OK width height format" (a VkFormat number) and then
 *          width x height x 4 bytes of pixels, rows top to bottom, or
 *          "ERROR why"
 *
 * keiland-shot (userland/tests/keiland-shot) turns the pixels into a PNG.
 * One request is served at a time; the wait for the frame is at most
 * SHOT_WAIT_MS.  The pixels are written on the event loop's thread with a
 * send timeout, which a test image can afford.
 */

#include "kwl.h"
#include "compose.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

/* How long a request waits for its frame, and the longest write of the pixels. */
#define SHOT_WAIT_MS		3000U
#define SHOT_SEND_SECONDS	10

/* The stages of a request. */
#define SHOT_IDLE		0
#define SHOT_REQUESTED		1
#define SHOT_RECORDED		2

/*
 * The capture's state, one for the compositor's life: the listening
 * socket and its path, the connection waiting for its frame, the request's
 * stage and time, and the host-visible buffer the frame is copied into.
 */
struct shot_state {
	int listener;
	char path[108];
	int client;
	int stage;
	uint64_t asked_ms;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize size;
	uint32_t width;
	uint32_t height;
};

/* The state; listener -1 until the socket is open. */
static struct shot_state shot = { -1, { 0 }, -1, SHOT_IDLE, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, 0 };

static void shot_answer(int connection, const char *line);
static void shot_finish(struct kwl_server *server, const char *error);
static int shot_buffer(struct kwl_compose *compose);
static void shot_buffer_free(struct kwl_compose *compose);

/* Tells whether this compositor was built with the capture (it was). */
int
kwl_shot_enabled(
	void)
{
	return 1;
}

/* Tells whether a capture waits for the next composed frame (the game mode composes one for it, scanout.c). */
int
kwl_shot_waiting(
	void)
{
	/* A request not yet copied. */
	if (shot.stage == SHOT_REQUESTED)
		return 1;

	/* None. */
	return 0;
}

/* Opens the capture's socket. */
void
kwl_shot_open(
	struct kwl_server *server)
{
	struct sockaddr_un address;
	const char *runtime;
	int status;

	/* The path: the runtime directory, else /tmp with the user's ID. */
	(void)server;
	runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime != NULL && runtime[0] == '/')
		snprintf(shot.path, sizeof(shot.path), "%s/keiland-shot.sock", runtime);
	else
		snprintf(shot.path, sizeof(shot.path), "/tmp/keiland-shot.%u.sock", (unsigned)getuid());

	/* A listening socket that does not wait, the user's alone. */
	shot.listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (shot.listener < 0)
		return;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", shot.path);
	(void)unlink(shot.path);
	status = bind(shot.listener, (struct sockaddr *)&address, sizeof(address));
	if (status == 0)
		status = listen(shot.listener, 2);
	if (status != 0) {
		(void)close(shot.listener);
		shot.listener = -1;
		return;
	}

	/* The user's alone (root may connect too). */
	(void)chmod(shot.path, 0600);
	printf("KWL SHOT listening path=%s\n", shot.path);
}

/* Closes the capture's socket and gives back its buffer. */
void
kwl_shot_close(
	struct kwl_server *server)
{
	/* A request still waiting is answered. */
	if (shot.client >= 0)
		shot_finish(server, "closing");

	/* The socket and its name. */
	if (shot.listener >= 0) {
		(void)close(shot.listener);
		shot.listener = -1;
		(void)unlink(shot.path);
	}
}

/*
 * Serves the socket once a pass: a new connection's line, and the time
 * limit of a request waiting for its frame.
 */
void
kwl_shot_tick(
	struct kwl_server *server)
{
	struct timeval timeout;
	char line[16];
	uint64_t now;
	ssize_t count;
	int connection;
	int active;
	int match;
	int error;

	/* A request whose frame did not come. */
	now = kwl_milliseconds();
	if (shot.client >= 0 && now - shot.asked_ms > SHOT_WAIT_MS) {
		shot_finish(server, "timeout");
		return;
	}

	/* A new connection, one at a time. */
	if (shot.listener < 0 || shot.client >= 0)
		return;
	connection = accept(shot.listener, NULL, NULL);
	if (connection < 0)
		return;

	/* Its line, which comes at once (a short wait), and a send that does not hang the compositor for long. */
	timeout.tv_sec = 0;
	timeout.tv_usec = 200000;
	(void)setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	timeout.tv_sec = SHOT_SEND_SECONDS;
	timeout.tv_usec = 0;
	(void)setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	memset(line, 0, sizeof(line));
	count = read(connection, line, sizeof(line) - 1U);
	if (count <= 0) {
		(void)close(connection);
		return;
	}

	/* PING: whether this compositor shows the display now. */
	active = server->compose != NULL && server->compose->output_open && server->os_paused == 0;
	match = strncmp(line, "PING", 4U);
	if (match == 0) {
		if (active)
			shot_answer(connection, "ACTIVE\n");
		else
			shot_answer(connection, "INACTIVE\n");
		(void)close(connection);
		return;
	}

	/* Anything but SHOT. */
	match = strncmp(line, "SHOT", 4U);
	if (match != 0) {
		shot_answer(connection, "ERROR unknown-request\n");
		(void)close(connection);
		return;
	}

	/* SHOT needs the display, and readable swapchain images. */
	if (!active) {
		shot_answer(connection, "ERROR inactive\n");
		(void)close(connection);
		return;
	}

	/* Swapchain images a copy can be made from. */
	if (!server->compose->readback) {
		shot_answer(connection, "ERROR unreadable\n");
		(void)close(connection);
		return;
	}

	/* The buffer the frame is copied into. */
	error = shot_buffer(server->compose);
	if (error != 0) {
		shot_answer(connection, "ERROR buffer\n");
		(void)close(connection);
		return;
	}

	/* The next frame copies its image; a frame is asked for. */
	shot.client = connection;
	shot.stage = SHOT_REQUESTED;
	shot.asked_ms = kwl_milliseconds();
	server->dirty = 1;
	printf("KWL SHOT requested width=%u height=%u\n", shot.width, shot.height);
}

/*
 * Records the copy of a frame's swapchain image into the buffer, after
 * the frame's render pass (which leaves the image for presentation), when a
 * request waits; the image goes back to the presentation layout.
 */
void
kwl_shot_record(
	struct kwl_server *server,
	VkCommandBuffer command,
	VkImage image)
{
	VkImageMemoryBarrier barrier;
	VkBufferMemoryBarrier visible;
	VkBufferImageCopy copy;

	/* Only a request waiting for a frame. */
	(void)server;
	if (shot.stage != SHOT_REQUESTED)
		return;

	/* The image, from presentation to the copy's source. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U,
	    0U, NULL, 0U, NULL, 1U, &barrier);

	/* The whole image into the buffer, rows packed. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = shot.width;
	copy.imageExtent.height = shot.height;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot.buffer, 1U, &copy);

	/* The copy visible to the host after the fence. */
	memset(&visible, 0, sizeof(visible));
	visible.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	visible.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.buffer = shot.buffer;
	visible.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 0U, NULL, 1U,
	    &visible, 0U, NULL);

	/* The image back to presentation. */
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = 0U;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL,
	    0U, NULL, 1U, &barrier);

	/* The frame carries the copy. */
	shot.stage = SHOT_RECORDED;
}

/* Sends the copied frame once its fence signaled. */
void
kwl_shot_complete(
	struct kwl_server *server)
{
	char header[96];
	void *map;
	const uint8_t *bytes;
	size_t sent;
	ssize_t count;
	VkResult result;

	/* Only a frame that carried the copy. */
	if (shot.stage != SHOT_RECORDED || shot.client < 0)
		return;

	/* The pixels. */
	result = vkMapMemory(server->compose->device, shot.memory, 0U, shot.size, 0U, &map);
	if (result != VK_SUCCESS) {
		shot_finish(server, "map");
		return;
	}

	/* The header, then every byte. */
	snprintf(header, sizeof(header), "OK %u %u %d\n", shot.width, shot.height, (int)server->compose->format);
	shot_answer(shot.client, header);
	bytes = map;
	sent = 0U;
	while (sent < (size_t)shot.size) {
		count = write(shot.client, bytes + sent, (size_t)shot.size - sent);
		if (count <= 0)
			break;
		sent += (size_t)count;
	}

	/* The memory is given back to the device. */
	vkUnmapMemory(server->compose->device, shot.memory);
	printf("KWL SHOT sent width=%u height=%u bytes=%llu\n", shot.width, shot.height, (unsigned long long)sent);

	/* The request is done. */
	shot_finish(server, NULL);
}

/* Writes a short answer. */
static void
shot_answer(
	int connection,
	const char *line)
{
	(void)write(connection, line, strlen(line));
}

/* Ends the request: an error's answer when there is one, the connection closed, the buffer given back. */
static void
shot_finish(
	struct kwl_server *server,
	const char *error)
{
	char line[64];

	/* The error. */
	if (error != NULL && shot.client >= 0) {
		snprintf(line, sizeof(line), "ERROR %s\n", error);
		shot_answer(shot.client, line);
		printf("KWL SHOT error=%s\n", error);
	}

	/* The connection and the buffer. */
	if (shot.client >= 0)
		(void)close(shot.client);
	shot.client = -1;
	shot.stage = SHOT_IDLE;
	if (server->compose != NULL && !server->compose->in_flight)
		shot_buffer_free(server->compose);
}

/* Makes the host-visible buffer for one frame of the output (kept from an earlier request of the same size). */
static int
shot_buffer(
	struct kwl_compose *compose)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryRequirements requirements;
	VkBufferCreateInfo create;
	VkMemoryAllocateInfo allocate;
	VkMemoryPropertyFlags wanted;
	uint32_t index;
	VkResult result;

	/* One kept from before, for the same output. */
	if (shot.buffer != VK_NULL_HANDLE && shot.width == compose->output.width && shot.height == compose->output.height)
		return 0;
	shot_buffer_free(compose);

	/* The buffer: four bytes a pixel. */
	shot.width = compose->output.width;
	shot.height = compose->output.height;
	shot.size = (VkDeviceSize)shot.width * shot.height * 4U;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = shot.size;
	create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(compose->device, &create, NULL, &shot.buffer);
	if (result != VK_SUCCESS) {
		shot.buffer = VK_NULL_HANDLE;
		return EIO;
	}

	/* Host-visible, coherent memory for it. */
	vkGetBufferMemoryRequirements(compose->device, shot.buffer, &requirements);
	vkGetPhysicalDeviceMemoryProperties(compose->physical, &memory);
	wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (index = 0U; index < memory.memoryTypeCount; index++) {
		if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
		    (memory.memoryTypes[index].propertyFlags & wanted) == wanted)
			break;
	}

	/* Without such memory there is no capture. */
	if (index == memory.memoryTypeCount) {
		shot_buffer_free(compose);
		return EIO;
	}

	/* The memory, bound. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = index;
	result = vkAllocateMemory(compose->device, &allocate, NULL, &shot.memory);
	if (result == VK_SUCCESS)
		result = vkBindBufferMemory(compose->device, shot.buffer, shot.memory, 0U);
	if (result != VK_SUCCESS) {
		shot_buffer_free(compose);
		return EIO;
	}

	/* Succeeded: the buffer. */
	return 0;
}

/* Gives back the buffer (no frame in flight uses it). */
static void
shot_buffer_free(
	struct kwl_compose *compose)
{
	/* The buffer and its memory. */
	if (shot.buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(compose->device, shot.buffer, NULL);
	if (shot.memory != VK_NULL_HANDLE)
		vkFreeMemory(compose->device, shot.memory, NULL);
	shot.buffer = VK_NULL_HANDLE;
	shot.memory = VK_NULL_HANDLE;
	shot.width = 0U;
	shot.height = 0U;
}
