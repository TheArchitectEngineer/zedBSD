/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises real cached renderer/display pairing against an independently ordered native inventory.
 */

#include "wsi-internal.h"
#include <uapi/gpu-scanout.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Owns one finite native inventory and its ordinary application allocator callbacks. */
static struct VkInstance_T instance;
/* Represents exactly two actual renderers; display-only peers never enter this array. */
static struct VkPhysicalDevice_T physicals[2];
/* Provides the public instance discovery order independently from native device identity ordering. */
static struct VkPhysicalDevice_T *physical_list[2];
/* Retains renderer native paths for pairing until both inventories have been destroyed. */
static struct vulkan_context contexts[2];
/* Counts native discovery opens, which ordinary repeated queries must not increase. */
static unsigned opened;
/* Counts final closes and must match opens after each instance inventory teardown. */
static unsigned closed;
/* Counts full directory snapshots independently from output or mode queries. */
static unsigned traversals;
/* Advances only within the finite host directory peer, reset by its next open. */
static unsigned cursor;
/* Counts each implicit instance-owned inventory or node allocation. */
static unsigned allocations;
/* Counts each allocator retirement; live inventories must preserve unmatched allocations. */
static unsigned frees;
/* Selects the second finite inventory profile, whose hint names no actual renderer. */
static unsigned stale_companion;
/* Supplies a stable opaque directory identity without borrowing a host directory descriptor. */
static int directory_token;
/* Carries one borrowed entry until the serial directory peer advances again. */
static struct dirent directory_entry;

static void *node_allocate(void *user, size_t bytes, size_t alignment, VkSystemAllocationScope scope);
static void node_free(void *user, void *pointer);

/*
 * Renderer-only and display-only nodes are paired without fabricating physical renderer objects.
 */
int
main(
	void)
{
	VkAllocationCallbacks callbacks;
	struct gpu_display_info request;
	struct vulkan_wsi_output output;
	struct gpu_display_mode mode;
	char path[262];
	uint64_t device_id;
	uint32_t count;
	uint32_t index;
	VkResult error;
	int status;

	/* Native inventory allocations must retain and use the instance's supplied callback policy. */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.pfnAllocation = node_allocate;
	callbacks.pfnFree = node_free;
	instance.object.allocator.has_callbacks = VK_TRUE;
	instance.object.allocator.callbacks = callbacks;
	instance.physical_devices = physical_list;
	instance.physical_device_count = 2U;

	/* Only the two independently available rendering contexts enter physical-device discovery. */
	for (index = 0U; index < 2U; index++) {
		/* Each physical renderer retains its own native path through instance lifetime. */
		physicals[index].instance = &instance;
		physicals[index].object.context = &contexts[index];
		physical_list[index] = &physicals[index];
	}

	strcpy(contexts[0].device_path, "/dev/gpu3");
	strcpy(contexts[1].device_path, "/dev/gpu2");

	/* The renderer with no native outputs receives the unhinted display-only node by stable identity. */
	error = vulkan_wsi_display_node_query(&physicals[0], UINT32_MAX, &count, &request, &device_id, path);
	assert(error == VK_SUCCESS && count == 1U);
	error = vulkan_wsi_display_node_query(&physicals[0], 0U, &count, &request, &device_id, path);
	assert(error == VK_SUCCESS && device_id == 700U && request.display_id == 1U);
	assert(strcmp(path, "/dev/gpu7") == 0);

	/* A preferred companion adds a second native device to the renderer's own two outputs. */
	error = vulkan_wsi_display_node_query(&physicals[1], UINT32_MAX, &count, &request, &device_id, path);
	assert(error == VK_SUCCESS && count == 3U);

	/* Existing outputs and the companion display occupy distinct stable ordinal ranges. */
	for (index = 0U; index < 3U; index++) {
		/* Query every output through the actual production pairing traversal. */
		error = vulkan_wsi_display_node_query(&physicals[1], index, &count, &request, &device_id, path);
		assert(error == VK_SUCCESS && count == 3U);

		/* The renderer's own two local outputs precede the separate companion display device. */
		if (index < 2U) {
			assert(device_id == 200U && request.display_id == index + 1U);
		} else {
			assert(device_id == 900U && request.display_id == 1U);
		}
	}

	/* Equal local display IDs still route mode requests to the exact selected device. */
	memset(&output, 0, sizeof(output));
	output.device_identifier = 900U;
	memset(&mode, 0, sizeof(mode));
	status = vulkan_wsi_display_node_ioctl(&physicals[1], &output, GPU_DISPLAY_MODE, &mode);
	assert(status == 0 && mode.width == 900U);
	status = vulkan_wsi_display_node_ioctl(&physicals[0], &output, GPU_DISPLAY_MODE, &mode);
	assert(status == -1 && errno == ENODEV);
	assert(opened == 4U && closed == 0U && traversals == 1U);
	vulkan_wsi_display_nodes_finish(&instance);
	assert(opened == closed && allocations == frees);

	/* A missing companion falls back deterministically rather than hiding a usable copied display. */
	stale_companion = 1U;
	error = vulkan_wsi_display_node_query(&physicals[0], UINT32_MAX, &count, &request, &device_id, path);
	assert(error == VK_SUCCESS && count == 2U);
	error = vulkan_wsi_display_node_query(&physicals[1], UINT32_MAX, &count, &request, &device_id, path);
	assert(error == VK_SUCCESS && count == 2U);
	vulkan_wsi_display_nodes_finish(&instance);
	assert(opened == closed && allocations == frees && traversals == 2U);

	/* Succeeded: repeated native discovery preserved identity, pairing and exact ownership. */
	puts("PASS WSI native nodes: renderer-only, display-only, companion/default pairing, multiple display devices, duplicate local IDs, cached discovery and exact close");
	return 0;
}

/*
 * Deliberately unordered node names expose any accidental dependence on readdir ordering.
 */
DIR *
opendir(
	const char *path)
{
	assert(strcmp(path, "/dev") == 0);
	cursor = 0U;
	traversals++;

	/* Succeeded: this snapshot owns one independently observable directory traversal. */
	return (DIR *)&directory_token;
}

/*
 * A bounded native inventory includes an unrelated endpoint which must never be opened.
 */
struct dirent *
readdir(
	DIR *directory)
{
	static const char *names[] = { "gpu9", "tty0", "gpu7", "gpu2", "gpu3" };

	assert(directory == (DIR *)&directory_token);
	/* Exhaustion ends this finite snapshot without manufacturing another GPU node. */
	if (cursor == sizeof(names) / sizeof(names[0]))
		return NULL;

	/* The next read supersedes this borrowed entry, just as the directory API specifies. */
	strcpy(directory_entry.d_name, names[cursor]);
	cursor++;

	/* Succeeded: the caller borrows exactly one next native directory entry. */
	return &directory_entry;
}

/*
 * No directory ownership remains once one private inventory has been constructed.
 */
int
closedir(
	DIR *directory)
{
	assert(directory == (DIR *)&directory_token);
	return 0;
}

/*
 * Descriptor numbers encode only the independent peer node identity.
 */
int
open(
	const char *path,
	int flags,
	...)
{
	int node;

	assert(flags == (O_RDWR | O_CLOEXEC));
	assert(strncmp(path, "/dev/gpu", 8U) == 0);
	node = path[8] - '0';
	assert(path[9] == '\0' && (node == 2 || node == 3 || node == 7 || node == 9));
	opened++;

	/* Succeeded: the exact native node identity has one additional discovery-open owner. */
	return 100 + node;
}

/*
 * Every native open is independently retained until inventory teardown.
 */
int
close(
	int fd)
{
	assert(fd == 102 || fd == 103 || fd == 107 || fd == 109);
	closed++;
	return 0;
}

/*
 * Native roles, companion hints and output counts are independent from the library's pairing state.
 */
int
ioctl(
	int fd,
	unsigned long command,
	...)
{
	struct gpu_device_info *device;
	struct gpu_display_info *display;
	struct gpu_display_mode *mode;
	va_list arguments;
	void *pointer;
	uint64_t identity;

	assert(fd == 102 || fd == 103 || fd == 107 || fd == 109);
	identity = (uint64_t)(fd - 100) * 100U;

	/* Directory order and native identity order deliberately differ for the renderer-only node. */
	if (fd == 103)
		identity = 100U;
	va_start(arguments, command);
	pointer = va_arg(arguments, void *);
	va_end(arguments);

	/* Stable identity and role discovery cannot call into any rendering protocol. */
	if (command == GPU_DEVICE_QUERY) {
		device = pointer;
		assert(device->version == GPU_ABI_VERSION && device->size == sizeof(*device));
		device->device_id = identity;
		device->roles = GPU_DEVICE_DISPLAY;

		/* One native node combines an actual renderer with its own two outputs. */
		if (fd == 102)
			device->roles |= GPU_DEVICE_RENDER;

		/* The second renderer has no physical output of its own. */
		if (fd == 103)
			device->roles = GPU_DEVICE_RENDER;

		/* Only this display-only node supplies an optional rendering companion hint. */
		if (fd == 109)
			device->companion_id = stale_companion != 0U ? 999U : 200U;
		return 0;
	}

	/* Renderer-only nodes cannot receive native display requests. */
	assert(fd != 103);

	/* Native output count remains separate from the number of actual Vulkan rendering devices. */
	if (command == GPU_DISPLAY_QUERY) {
		display = pointer;
		display->count = fd == 102 ? 2U : 1U;

		/* Indexed queries return one local identity; count-only requests do not construct one. */
		if (display->index != UINT32_MAX) {
			assert(display->index < display->count);
			display->display_id = display->index + 1U;
			display->generation = 1U;
		}

		return 0;
	}

	assert(command == GPU_DISPLAY_MODE);
	mode = pointer;
	mode->width = (uint32_t)identity;

	/* Succeeded: the response proves which exact native node received the mode request. */
	return 0;
}

/* Application allocation callbacks must own every implicit inventory allocation exactly once. */
static void *
node_allocate(
	void *user,
	size_t bytes,
	size_t alignment,
	VkSystemAllocationScope scope)
{
	void *pointer;

	(void)user;
	assert(alignment <= 16U && scope == VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	pointer = malloc(bytes);
	assert(pointer != NULL);
	allocations++;

	/* Succeeded: one instance-owned implicit object acquired separate host storage. */
	return pointer;
}

/* Paired nodes have no allocator-independent lifetime after instance destruction. */
static void
node_free(
	void *user,
	void *pointer)
{
	(void)user;
	assert(pointer != NULL);
	frees++;
	free(pointer);

	/* Succeeded: this callback consumed exactly one earlier implicit allocation. */
	return;
}
