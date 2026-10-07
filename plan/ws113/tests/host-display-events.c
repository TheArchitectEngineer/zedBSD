/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libvulkan's display events and power (ws113-p003).
 *
 * It links wsi-display-control.c with stand-ins for the kernel requests and
 * the rest of the library, and checks the per-fence cursors of hotplug and
 * refresh events, the rebase after a generation change, the power state
 * translation, the empty surface counters and the counter read.
 */

#include "wsi-internal.h"
#include "sync-internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fences the stand-in vkCreateFence hands out, in order. */
static struct vulkan_sync test_fences[8];

/* How many of test_fences were handed out. */
static uint32_t test_fence_count;

/* The topology sum the stand-in node query reports, and whether it fails. */
static uint64_t test_topology;
static VkBool32 test_topology_fails;

/* The display's snapshot the stand-ins report and refresh. */
static struct vulkan_wsi_output test_output;

/* The refresh count of the display, and the errno a refresh request fails with (0: none). */
static uint64_t test_refresh_count;
static int test_refresh_error;

/* The generation the display takes when its snapshot is refreshed. */
static uint64_t test_refreshed_generation;

/* How many times the display's snapshot was refreshed. */
static uint32_t test_refreshes;

/* The last power state asked of the kernel. */
static uint32_t test_power_state;

/* The number of failed checks. */
static int test_failures;

static void check(int condition, const char *what);

/*
 * Stands in for the device handle resolution.
 */
struct VkDevice_T *
vulkan_device(
	VkDevice device)
{
	/* Succeeded: the test's handles are the objects. */
	return (struct VkDevice_T *)device;
}

/*
 * Stands in for the display handle resolution.
 */
struct vulkan_display *
vulkan_wsi_display(
	VkDisplayKHR handle)
{
	/* Succeeded: the test's handles are the objects. */
	return (struct vulkan_display *)(uintptr_t)handle;
}

/*
 * Stands in for the fence handle resolution.
 */
struct vulkan_sync *
vulkan_sync_object(
	uint64_t handle)
{
	/* Succeeded: the test's handles are the objects. */
	return (struct vulkan_sync *)(uintptr_t)handle;
}

/*
 * Stands in for fence creation with a zeroed unsubmitted fence.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(
	VkDevice device,
	const VkFenceCreateInfo *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkFence *pFence)
{
	struct vulkan_sync *sync;

	/* The stand-in uses neither the device nor the callbacks. */
	(void)device;
	(void)pAllocator;

	/* An event fence starts unsignaled. */
	check(pCreateInfo->flags == 0U, "event fences are created unsignaled");
	if (test_fence_count == sizeof(test_fences) / sizeof(test_fences[0]))
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	/* Hands out the next zeroed fence. */
	sync = &test_fences[test_fence_count];
	test_fence_count++;
	memset(sync, 0, sizeof(*sync));
	sync->native_unsubmitted = VK_TRUE;
	*pFence = (VkFence)(uintptr_t)sync;

	/* Succeeded: the fence is the caller's. */
	return VK_SUCCESS;
}

/*
 * Stands in for the ordinary surface capability query.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
	(void)physicalDevice;
	(void)surface;

	/* Fills a recognizable answer. */
	memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
	pSurfaceCapabilities->minImageCount = 2U;
	pSurfaceCapabilities->maxImageCount = 3U;
	pSurfaceCapabilities->currentExtent.width = 1920U;
	pSurfaceCapabilities->currentExtent.height = 1080U;
	pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	/* Succeeded: the record is filled. */
	return VK_SUCCESS;
}

/*
 * Stands in for the topology sum of the device's display nodes.
 */
VkResult
vulkan_wsi_display_node_topology(
	struct VkPhysicalDevice_T *physical,
	uint64_t *sequence)
{
	(void)physical;

	/* A failing node query. */
	if (test_topology_fails)
		return VK_ERROR_SURFACE_LOST_KHR;

	/* Succeeded: the current sum. */
	*sequence = test_topology;
	return VK_SUCCESS;
}

/*
 * Stands in for the display's snapshot.
 */
VkResult
vulkan_wsi_display_snapshot(
	struct vulkan_display *display,
	struct vulkan_wsi_output *output)
{
	(void)display;

	/* Succeeded: the test's current snapshot. */
	*output = test_output;
	return VK_SUCCESS;
}

/*
 * Stands in for refreshing the display's snapshot to its new generation.
 */
VkResult
vulkan_wsi_display_refresh(
	struct vulkan_display *display)
{
	(void)display;

	/* The display takes its new generation. */
	test_refreshes++;
	test_output.generation = test_refreshed_generation;

	/* Succeeded: the snapshot is current. */
	return VK_SUCCESS;
}

/*
 * Stands in for the kernel's refresh request on the display's discovery open.
 */
int
vulkan_wsi_display_node_ioctl(
	struct VkPhysicalDevice_T *physical,
	const struct vulkan_wsi_output *output,
	unsigned long command,
	void *argument)
{
	struct gpu_display_refresh *request;

	/* The stand-in has one node. */
	(void)physical;

	/* Only the refresh request is made by the code under test. */
	check(command == GPU_DISPLAY_REFRESH, "only refresh requests reach the node");
	request = argument;
	check(request->timeout_ns == 0U, "a refresh observation never waits");
	check(request->display_id == (uint32_t)output->identifier, "the refresh names the display");
	check(request->generation == output->generation, "the refresh names the snapshot's generation");

	/* An old generation or another failure the test chose. */
	if (test_refresh_error != 0) {
		errno = test_refresh_error;
		return -1;
	}

	/* The kernel's answer of an old generation. */
	if (request->generation != test_refreshed_generation) {
		errno = ESTALE;
		return -1;
	}

	/* Cursor zero reports the count at once; otherwise only a later count answers. */
	if (request->cursor != 0U && test_refresh_count <= request->cursor) {
		errno = ETIMEDOUT;
		return -1;
	}

	/* Succeeded: the count. */
	request->sequence = test_refresh_count;
	return 0;
}

/*
 * Stands in for the lease's power request.
 */
VkResult
vulkan_wsi_display_power(
	struct VkDevice_T *device,
	struct vulkan_display *display,
	uint32_t state)
{
	(void)device;
	(void)display;

	/* Succeeded: records the kernel state asked for. */
	test_power_state = state;
	return VK_SUCCESS;
}

/*
 * Runs the checks.
 */
int
main(void)
{
	struct VkPhysicalDevice_T physical;
	struct VkPhysicalDevice_T other_physical;
	struct VkDevice_T device;
	struct vulkan_display display;
	struct vulkan_sync *first;
	struct vulkan_sync *second;
	struct vulkan_sync *refresh;
	VkDeviceEventInfoEXT device_event;
	VkDisplayEventInfoEXT display_event;
	VkDisplayPowerInfoEXT power;
	VkSurfaceCapabilities2EXT capabilities;
	VkFence fence;
	VkDevice device_handle;
	VkDisplayKHR display_handle;
	VkResult error;
	VkBool32 fired;
	uint64_t counter;

	/* The device and its display. */
	memset(&physical, 0, sizeof(physical));
	memset(&other_physical, 0, sizeof(other_physical));
	memset(&device, 0, sizeof(device));
	memset(&display, 0, sizeof(display));
	device.physical = &physical;
	display.physical = &physical;
	device_handle = (VkDevice)&device;
	display_handle = (VkDisplayKHR)(uintptr_t)&display;
	memset(&test_output, 0, sizeof(test_output));
	test_output.identifier = 7U;
	test_output.generation = 1U;
	test_refreshed_generation = 1U;

	/* Hotplug: two fences keep their own cursors. */
	memset(&device_event, 0, sizeof(device_event));
	device_event.sType = VK_STRUCTURE_TYPE_DEVICE_EVENT_INFO_EXT;
	device_event.deviceEvent = VK_DEVICE_EVENT_TYPE_DISPLAY_HOTPLUG_EXT;
	test_topology = 5U;
	error = vkRegisterDeviceEventEXT(device_handle, &device_event, NULL, &fence);
	check(error == VK_SUCCESS, "a hotplug fence registers");
	first = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	check(first->event == VULKAN_SYNC_EVENT_HOTPLUG && first->event_cursor == 5U, "the hotplug cursor is the sequence at registration");
	fired = vulkan_display_event_poll(&device, first);
	check(!fired, "an unchanged topology signals nothing");
	test_topology = 6U;
	error = vkRegisterDeviceEventEXT(device_handle, &device_event, NULL, &fence);
	check(error == VK_SUCCESS, "a second hotplug fence registers");
	second = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	fired = vulkan_display_event_poll(&device, first);
	check(fired, "the first fence sees the change after its cursor");
	fired = vulkan_display_event_poll(&device, second);
	check(!fired, "the second fence, registered after the change, does not");
	test_topology = 7U;
	fired = vulkan_display_event_poll(&device, second);
	check(fired, "the second fence sees the next change");

	/* Hotplug: a registration whose sample failed takes its cursor at the first observation. */
	test_topology_fails = VK_TRUE;
	error = vkRegisterDeviceEventEXT(device_handle, &device_event, NULL, &fence);
	check(error == VK_SUCCESS, "a hotplug fence registers even when the sample fails");
	first = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	check(!first->event_based, "a failed sample leaves no cursor");
	test_topology_fails = VK_FALSE;
	fired = vulkan_display_event_poll(&device, first);
	check(!fired && first->event_based && first->event_cursor == 7U, "the first observation takes the cursor without signaling");
	test_topology = 8U;
	fired = vulkan_display_event_poll(&device, first);
	check(fired, "a change after the late cursor signals");

	/* Hotplug: another event type and a foreign record are refused. */
	device_event.deviceEvent = VK_DEVICE_EVENT_TYPE_MAX_ENUM_EXT;
	error = vkRegisterDeviceEventEXT(device_handle, &device_event, NULL, &fence);
	check(error == VK_ERROR_UNKNOWN, "an unknown device event is refused");

	/* Refresh: the cursor is the count at registration; a later count signals. */
	memset(&display_event, 0, sizeof(display_event));
	display_event.sType = VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT;
	display_event.displayEvent = VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT;
	test_refresh_count = 100U;
	error = vkRegisterDisplayEventEXT(device_handle, display_handle, &display_event, NULL, &fence);
	check(error == VK_SUCCESS, "a refresh fence registers");
	refresh = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	check(refresh->event_based && refresh->event_cursor == 100U && refresh->event_generation == 1U, "the refresh cursor is the count of the generation");
	fired = vulkan_display_event_poll(&device, refresh);
	check(!fired, "no boundary after the cursor signals nothing");
	test_refresh_count = 101U;
	fired = vulkan_display_event_poll(&device, refresh);
	check(fired, "the next boundary signals");

	/* Refresh: a reconnection with a new generation takes a new cursor, then the next real boundary signals. */
	test_refresh_count = 100U;
	error = vkRegisterDisplayEventEXT(device_handle, display_handle, &display_event, NULL, &fence);
	refresh = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	test_refreshed_generation = 2U;
	test_refresh_count = 5U;
	fired = vulkan_display_event_poll(&device, refresh);
	check(!fired && test_refreshes == 1U && !refresh->event_based, "an old generation refreshes the snapshot without signaling");
	fired = vulkan_display_event_poll(&device, refresh);
	check(!fired && refresh->event_based && refresh->event_cursor == 5U && refresh->event_generation == 2U, "the new generation takes a cursor without signaling");
	test_refresh_count = 6U;
	fired = vulkan_display_event_poll(&device, refresh);
	check(fired, "the next boundary of the new generation signals");

	/* Refresh: a disconnected output stays pending. */
	test_refresh_error = ENXIO;
	error = vkRegisterDisplayEventEXT(device_handle, display_handle, &display_event, NULL, &fence);
	refresh = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	fired = vulkan_display_event_poll(&device, refresh);
	check(error == VK_SUCCESS && !fired && !refresh->event_based, "a disconnected display leaves the fence pending");
	test_refresh_error = 0;

	/* Refresh: a count of zero at registration is passed by any count above zero. */
	test_refresh_count = 0U;
	error = vkRegisterDisplayEventEXT(device_handle, display_handle, &display_event, NULL, &fence);
	refresh = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	fired = vulkan_display_event_poll(&device, refresh);
	check(!fired, "an output that has not scanned out signals nothing");
	test_refresh_count = 3U;
	fired = vulkan_display_event_poll(&device, refresh);
	check(fired, "its first boundary signals");

	/* Refresh: a display of another physical device is refused. */
	display.physical = &other_physical;
	error = vkRegisterDisplayEventEXT(device_handle, display_handle, &display_event, NULL, &fence);
	check(error == VK_ERROR_UNKNOWN, "a foreign display's event is refused");
	display.physical = &physical;

	/* Power: the standard states become the kernel's. */
	memset(&power, 0, sizeof(power));
	power.sType = VK_STRUCTURE_TYPE_DISPLAY_POWER_INFO_EXT;
	power.powerState = VK_DISPLAY_POWER_STATE_OFF_EXT;
	error = vkDisplayPowerControlEXT(device_handle, display_handle, &power);
	check(error == VK_SUCCESS && test_power_state == GPU_DISPLAY_POWER_OFF, "OFF is the kernel's OFF");
	power.powerState = VK_DISPLAY_POWER_STATE_SUSPEND_EXT;
	error = vkDisplayPowerControlEXT(device_handle, display_handle, &power);
	check(error == VK_SUCCESS && test_power_state == GPU_DISPLAY_POWER_SUSPEND, "SUSPEND is the kernel's SUSPEND");
	power.powerState = VK_DISPLAY_POWER_STATE_ON_EXT;
	error = vkDisplayPowerControlEXT(device_handle, display_handle, &power);
	check(error == VK_SUCCESS && test_power_state == GPU_DISPLAY_POWER_ON, "ON is the kernel's ON");
	power.powerState = VK_DISPLAY_POWER_STATE_MAX_ENUM_EXT;
	error = vkDisplayPowerControlEXT(device_handle, display_handle, &power);
	check(error == VK_ERROR_UNKNOWN, "an unknown power state is refused");

	/* Surface counters: none, and the counter cannot be read. */
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT;
	capabilities.supportedSurfaceCounters = 0xffU;
	error = vkGetPhysicalDeviceSurfaceCapabilities2EXT(NULL, VK_NULL_HANDLE, &capabilities);
	check(error == VK_SUCCESS, "the extended capabilities answer");
	check(capabilities.minImageCount == 2U && capabilities.currentExtent.width == 1920U, "the ordinary capabilities are copied");
	check(capabilities.supportedSurfaceCounters == 0U, "no surface counter is offered");
	capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
	error = vkGetPhysicalDeviceSurfaceCapabilities2EXT(NULL, VK_NULL_HANDLE, &capabilities);
	check(error == VK_ERROR_INITIALIZATION_FAILED, "a foreign record is refused");
	counter = 42U;
	error = vkGetSwapchainCounterEXT(device_handle, VK_NULL_HANDLE, VK_SURFACE_COUNTER_VBLANK_BIT_EXT, &counter);
	check(error == VK_ERROR_OUT_OF_DATE_KHR && counter == 42U, "the counter read is out of date and writes nothing");

	/* Reports the outcome. */
	if (test_failures != 0) {
		fprintf(stderr, "host-display-events: %d checks failed\n", test_failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-display-events: PASS\n");
	return 0;
}

/* Counts and reports one failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed check is reported and counted. */
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", what);
		test_failures++;
	}
}
