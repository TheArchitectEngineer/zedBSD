/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Display power, hotplug and refresh events (VK_EXT_display_control) and the
 * surface counters they depend on (VK_EXT_display_surface_counter).
 *
 * A fence registered for an event is an ordinary fence whose payload this
 * library signals.  No thread watches the kernel: each observation of a
 * pending event fence asks the kernel once, without waiting, whether the
 * event happened after the cursor the fence took, and a fence that saw its
 * event stays signaled until it is reset.  Waiting on such a fence uses the
 * fence wait's bounded polling.  Each fence keeps its own cursor, so one
 * client's observation never consumes another's, and no request
 * acknowledges the topology on any open.
 */

#include "wsi-internal.h"
#include "sync-internal.h"

#include <errno.h>
#include <string.h>

static VkResult event_fence_create(VkDevice device_handle, const VkAllocationCallbacks *allocator, VkFence *fence, struct vulkan_sync **sync);
static VkBool32 event_hotplug_poll(struct VkDevice_T *device, struct vulkan_sync *sync);
static VkBool32 event_refresh_poll(struct VkDevice_T *device, struct vulkan_sync *sync);
static void event_refresh_base(struct VkPhysicalDevice_T *physical, struct vulkan_sync *sync, const struct vulkan_wsi_output *output);

/*
 * Retrieves a surface's capabilities together with its counters.
 *
 * The library offers no surface counter, so the counter set is empty.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilities2EXT(
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	VkSurfaceCapabilities2EXT *pSurfaceCapabilities)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkResult error;

	/* Only the extension's own output record can receive the answer. */
	if (pSurfaceCapabilities == NULL ||
	    pSurfaceCapabilities->sType != VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT)
		return VK_ERROR_INITIALIZATION_FAILED;

	/* The ordinary query answers every field the counters do not add. */
	error = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
	if (error != VK_SUCCESS)
		return error;

	/* Copies the ordinary capabilities into the extended record, keeping its chain. */
	pSurfaceCapabilities->minImageCount = capabilities.minImageCount;
	pSurfaceCapabilities->maxImageCount = capabilities.maxImageCount;
	pSurfaceCapabilities->currentExtent = capabilities.currentExtent;
	pSurfaceCapabilities->minImageExtent = capabilities.minImageExtent;
	pSurfaceCapabilities->maxImageExtent = capabilities.maxImageExtent;
	pSurfaceCapabilities->maxImageArrayLayers = capabilities.maxImageArrayLayers;
	pSurfaceCapabilities->supportedTransforms = capabilities.supportedTransforms;
	pSurfaceCapabilities->currentTransform = capabilities.currentTransform;
	pSurfaceCapabilities->supportedCompositeAlpha = capabilities.supportedCompositeAlpha;
	pSurfaceCapabilities->supportedUsageFlags = capabilities.supportedUsageFlags;

	/* No vertical-blank counter is kept for any surface. */
	pSurfaceCapabilities->supportedSurfaceCounters = 0U;

	/* Succeeded: the record describes the surface and its empty counter set. */
	return VK_SUCCESS;
}

/*
 * Powers a display this device presents to on, off or into suspend.
 *
 * The device needs a swapchain on the display, whose lease the kernel
 * request names; without one, or on an output without power control, the
 * request is refused.  The release of the lease powers the display on again.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkDisplayPowerControlEXT(
	VkDevice device,
	VkDisplayKHR display,
	const VkDisplayPowerInfoEXT *pDisplayPowerInfo)
{
	struct VkDevice_T *owner;
	struct vulkan_display *target;
	uint32_t state;
	VkResult error;

	/* Resolves the device and the display the request names. */
	owner = vulkan_device(device);
	target = vulkan_wsi_display(display);
	if (owner == NULL ||
	    target == NULL ||
	    pDisplayPowerInfo == NULL)
		return VK_ERROR_UNKNOWN;

	/* A display of another physical device or a foreign record names nothing here. */
	if (target->physical != owner->physical ||
	    pDisplayPowerInfo->sType != VK_STRUCTURE_TYPE_DISPLAY_POWER_INFO_EXT)
		return VK_ERROR_UNKNOWN;

	/* Translates the standard power state into the kernel's. */
	switch (pDisplayPowerInfo->powerState) {
	case VK_DISPLAY_POWER_STATE_OFF_EXT:
		state = GPU_DISPLAY_POWER_OFF;
		break;
	case VK_DISPLAY_POWER_STATE_SUSPEND_EXT:
		state = GPU_DISPLAY_POWER_SUSPEND;
		break;
	case VK_DISPLAY_POWER_STATE_ON_EXT:
		state = GPU_DISPLAY_POWER_ON;
		break;
	default:
		return VK_ERROR_UNKNOWN;
	}

	/* Asks the kernel under this device's lease on the display. */
	error = vulkan_wsi_display_power(owner, target, state);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the display is in the requested power state. */
	return VK_SUCCESS;
}

/*
 * Registers a fence signaled when a display of the device is plugged or unplugged.
 *
 * The fence takes the topology sequence of the device's display nodes now;
 * it is signaled once a later observation finds the sequence above it, and
 * stays signaled until it is reset.  A client registers a new fence before it
 * enumerates the displays again, so a change between the two is not missed.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkRegisterDeviceEventEXT(
	VkDevice device,
	const VkDeviceEventInfoEXT *pDeviceEventInfo,
	const VkAllocationCallbacks *pAllocator,
	VkFence *pFence)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	uint64_t sequence;
	VkResult error;

	/* Resolves the device; the hotplug is the only device event. */
	owner = vulkan_device(device);
	if (owner == NULL ||
	    pDeviceEventInfo == NULL ||
	    pFence == NULL)
		return VK_ERROR_UNKNOWN;

	/* A foreign record or another event type names nothing this library reports. */
	if (pDeviceEventInfo->sType != VK_STRUCTURE_TYPE_DEVICE_EVENT_INFO_EXT ||
	    pDeviceEventInfo->deviceEvent != VK_DEVICE_EVENT_TYPE_DISPLAY_HOTPLUG_EXT)
		return VK_ERROR_UNKNOWN;

	/* Creates the ordinary unsignaled fence the event will signal. */
	error = event_fence_create(device, pAllocator, pFence, &sync);
	if (error != VK_SUCCESS)
		return error;

	/* No other thread knows the fence yet, so the fields need no lock. */
	sync->event = VULKAN_SYNC_EVENT_HOTPLUG;
	sync->event_based = VK_FALSE;

	/*
	 * The cursor is the topology sequence now.  A failed sample leaves the
	 * fence without a cursor; its first successful observation takes one.
	 */
	error = vulkan_wsi_display_node_topology(owner->physical, &sequence);
	if (error == VK_SUCCESS) {
		sync->event_cursor = sequence;
		sync->event_based = VK_TRUE;
	}

	/* Succeeded: the caller owns a fence the next hotplug signals. */
	return VK_SUCCESS;
}

/*
 * Registers a fence signaled by a display's next refresh boundary.
 *
 * The first-pixel-out event is the first boundary after the registration.
 * A display that does not scan out makes no boundary, and a display of an
 * old generation takes a new cursor once its snapshot is current again, so
 * a reconnected display signals at its next real boundary, never at the
 * reconnection itself.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkRegisterDisplayEventEXT(
	VkDevice device,
	VkDisplayKHR display,
	const VkDisplayEventInfoEXT *pDisplayEventInfo,
	const VkAllocationCallbacks *pAllocator,
	VkFence *pFence)
{
	struct VkDevice_T *owner;
	struct vulkan_display *target;
	struct vulkan_sync *sync;
	struct vulkan_wsi_output output;
	VkResult error;

	/* Resolves the device and the display the event belongs to. */
	owner = vulkan_device(device);
	target = vulkan_wsi_display(display);
	if (owner == NULL ||
	    target == NULL ||
	    pDisplayEventInfo == NULL ||
	    pFence == NULL)
		return VK_ERROR_UNKNOWN;

	/* A display of another physical device or another event type is not reported. */
	if (target->physical != owner->physical ||
	    pDisplayEventInfo->sType != VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT ||
	    pDisplayEventInfo->displayEvent != VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT)
		return VK_ERROR_UNKNOWN;

	/* Creates the ordinary unsignaled fence the boundary will signal. */
	error = event_fence_create(device, pAllocator, pFence, &sync);
	if (error != VK_SUCCESS)
		return error;

	/* No other thread knows the fence yet, so the fields need no lock. */
	sync->event = VULKAN_SYNC_EVENT_REFRESH;
	sync->event_based = VK_FALSE;
	sync->event_display = target;

	/* Takes the cursor now when the display's current generation answers. */
	error = vulkan_wsi_display_snapshot(target, &output);
	if (error == VK_SUCCESS)
		event_refresh_base(owner->physical, sync, &output);

	/* Succeeded: the caller owns a fence the display's next boundary signals. */
	return VK_SUCCESS;
}

/*
 * Reads a swapchain's surface counter.
 *
 * No surface offers a counter, so no swapchain is created with one and the
 * value cannot be read.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetSwapchainCounterEXT(
	VkDevice device,
	VkSwapchainKHR swapchain,
	VkSurfaceCounterFlagBitsEXT counter,
	uint64_t *pCounterValue)
{
	/* No counter exists to read; the value is left as it was. */
	(void)device;
	(void)swapchain;
	(void)counter;
	(void)pCounterValue;

	/* The swapchain has no such counter: an out-of-date answer, not a device loss. */
	return VK_ERROR_OUT_OF_DATE_KHR;
}

/*
 * Reports whether a pending display-event fence's event has happened.
 *
 * The caller owns the device mutex.  The kernel is asked once without waiting.
 */
VkBool32
vulkan_display_event_poll(
	struct VkDevice_T *device,
	struct vulkan_sync *sync)
{
	VkBool32 fired;

	/* Asks the kernel in the way the fence's event needs. */
	fired = VK_FALSE;
	if (sync->event == VULKAN_SYNC_EVENT_HOTPLUG) {
		fired = event_hotplug_poll(device, sync);
	} else if (sync->event == VULKAN_SYNC_EVENT_REFRESH) {
		fired = event_refresh_poll(device, sync);
	}

	/* Succeeded: reports whether the event happened after the fence's cursor. */
	return fired;
}

/* Creates one unsignaled ordinary fence for an event registration. */
static VkResult
event_fence_create(
	VkDevice device_handle,
	const VkAllocationCallbacks *allocator,
	VkFence *fence,
	struct vulkan_sync **sync)
{
	VkFenceCreateInfo create;
	VkResult error;

	/* An unsignaled fence with no chain: only the event signals it. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	create.pNext = NULL;
	create.flags = 0U;
	error = vkCreateFence(device_handle, &create, allocator, fence);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the caller fills in the fence's event before publishing it. */
	*sync = vulkan_sync_object((uint64_t)(uintptr_t)*fence);
	return VK_SUCCESS;
}

/* Reports whether the device's display topology changed after the fence's cursor. */
static VkBool32
event_hotplug_poll(
	struct VkDevice_T *device,
	struct vulkan_sync *sync)
{
	uint64_t sequence;
	VkResult error;

	/* Samples the topology sequences of the device's display nodes. */
	error = vulkan_wsi_display_node_topology(device->physical, &sequence);
	if (error != VK_SUCCESS)
		return VK_FALSE;

	/* A fence whose registration could not sample takes its cursor now. */
	if (!sync->event_based) {
		sync->event_cursor = sequence;
		sync->event_based = VK_TRUE;
		return VK_FALSE;
	}

	/* The topology has not moved since the cursor. */
	if (sequence <= sync->event_cursor)
		return VK_FALSE;

	/* Succeeded: a display was plugged or unplugged after the registration. */
	return VK_TRUE;
}

/* Reports whether the fence's display passed a refresh boundary after the cursor. */
static VkBool32
event_refresh_poll(
	struct VkDevice_T *device,
	struct vulkan_sync *sync)
{
	struct vulkan_wsi_output output;
	struct gpu_display_refresh request;
	VkResult error;
	int status;
	int native_error;

	/* The display's current identity and generation name the output to the kernel. */
	error = vulkan_wsi_display_snapshot(sync->event_display, &output);
	if (error != VK_SUCCESS)
		return VK_FALSE;

	/* A cursor not taken yet, or taken in another generation, is taken again: no boundary yet. */
	if (!sync->event_based || sync->event_generation != output.generation) {
		event_refresh_base(device->physical, sync, &output);
		return VK_FALSE;
	}

	/*
	 * Asks for a boundary after the cursor without waiting.  A cursor of
	 * zero asks for the count at once, which is a boundary when it is above
	 * zero.
	 */
	memset(&request, 0, sizeof(request));
	request.version = GPU_ABI_VERSION;
	request.size = sizeof(request);
	request.display_id = (uint32_t)output.identifier;
	request.generation = output.generation;
	request.cursor = sync->event_cursor;
	request.timeout_ns = 0U;
	status = vulkan_wsi_display_node_ioctl(device->physical, &output, GPU_DISPLAY_REFRESH, &request);
	native_error = errno;
	if (status != 0) {
		/* An old generation: the snapshot is refreshed and a later observation takes a new cursor. */
		if (native_error == ESTALE) {
			error = vulkan_wsi_display_refresh(sync->event_display);
			(void)error;
			sync->event_based = VK_FALSE;
		}

		/* No boundary came, or the output does not scan out now. */
		return VK_FALSE;
	}

	/* The count has not moved past the cursor. */
	if (request.sequence <= sync->event_cursor)
		return VK_FALSE;

	/* Succeeded: the display passed a refresh boundary after the registration. */
	return VK_TRUE;
}

/* Takes the display's current refresh count as the fence's cursor. */
static void
event_refresh_base(
	struct VkPhysicalDevice_T *physical,
	struct vulkan_sync *sync,
	const struct vulkan_wsi_output *output)
{
	struct gpu_display_refresh request;
	VkResult error;
	int status;
	int native_error;

	/* A cursor of zero reports the current count at once. */
	memset(&request, 0, sizeof(request));
	request.version = GPU_ABI_VERSION;
	request.size = sizeof(request);
	request.display_id = (uint32_t)output->identifier;
	request.generation = output->generation;
	request.cursor = 0U;
	request.timeout_ns = 0U;
	status = vulkan_wsi_display_node_ioctl(physical, output, GPU_DISPLAY_REFRESH, &request);
	native_error = errno;
	if (status != 0) {
		/* An old generation refreshes the snapshot; the fence stays without a cursor. */
		if (native_error == ESTALE) {
			error = vulkan_wsi_display_refresh(sync->event_display);
			(void)error;
		}

		/* The fence keeps observing without a cursor. */
		return;
	}

	/*
	 * The count and its generation are the cursor: a boundary of this
	 * generation above the count signals the fence.
	 */
	sync->event_cursor = request.sequence;
	sync->event_generation = output->generation;
	sync->event_based = VK_TRUE;

	/* Succeeded: the fence has a cursor in the display's current generation. */
	return;
}
