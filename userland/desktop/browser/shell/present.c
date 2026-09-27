/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The presenter: the page drawn by the GPU renderer straight into the
 * window's swapchain images, with standard Vulkan and Wayland WSI.
 *
 * The same way of opening the device and the swapchain as files'
 * presenter, but where the file manager copies a CPU canvas onto the image,
 * the browser draws its display list there (design.md §8.4).
 */

#include "shell/internal.h"

#include <stdlib.h>
#include <string.h>

/* How long an acquire may wait for the compositor to give an image back, in nanoseconds. */
#define PRESENT_TIMEOUT		10000000000ULL

/* The most swapchain images the presenter takes. */
#define PRESENT_IMAGES_MAX	8U

static VkResult present_device(struct shell_present *present);
static VkResult present_swapchain(struct shell_present *present, uint32_t width, uint32_t height, VkSwapchainKHR old);
static VkResult present_targets(struct shell_present *present);
static void present_targets_free(struct shell_present *present);

/*
 * Makes the Vulkan objects of the window: the instance and surface, the
 * device, the swapchain and the GPU renderer that draws into it.
 */
VkResult
shell_present_open(
	struct shell_present *present,
	struct shell_window *window)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkWaylandSurfaceCreateInfoKHR surface;
	VkSemaphoreCreateInfo semaphore;
	const char *extensions[2];
	VkResult error;

	/* Nothing is owned yet. */
	memset(present, 0, sizeof(*present));

	/* The instance, with the surface extensions a Wayland window needs. */
	extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
	extensions[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "browser";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	instance.enabledExtensionCount = 2U;
	instance.ppEnabledExtensionNames = extensions;
	present->operation = "vkCreateInstance";
	error = vkCreateInstance(&instance, NULL, &present->instance);
	if (error != VK_SUCCESS)
		return error;

	/* The window's surface. */
	memset(&surface, 0, sizeof(surface));
	surface.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	surface.display = window->display;
	surface.surface = window->surface;
	present->operation = "vkCreateWaylandSurfaceKHR";
	error = vkCreateWaylandSurfaceKHR(present->instance, &surface, NULL, &present->surface);
	if (error != VK_SUCCESS)
		return error;

	/* A device with a queue that draws and presents to the surface. */
	error = present_device(present);
	if (error != VK_SUCCESS)
		return error;

	/* The swapchain at the window's size. */
	error = present_swapchain(present, window->width, window->height, VK_NULL_HANDLE);
	if (error != VK_SUCCESS)
		return error;

	/* The GPU renderer, drawing images that are then presented. */
	error = paint_gpu_open(
		&present->gpu,
		present->instance,
		present->physical,
		present->family,
		present->device,
		present->format,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	present->gpu_open = 1;
	if (error != VK_SUCCESS) {
		present->operation = present->gpu.operation;
		return error;
	}

	/* A framebuffer for each swapchain image, in the renderer's pass. */
	error = present_targets(present);
	if (error != VK_SUCCESS)
		return error;

	/* The semaphore the acquire signals. */
	memset(&semaphore, 0, sizeof(semaphore));
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	present->operation = "vkCreateSemaphore";
	error = vkCreateSemaphore(present->device, &semaphore, NULL, &present->acquired);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: pages can be shown. */
	return VK_SUCCESS;
}

/*
 * Replaces the swapchain with one of a new size.
 */
VkResult
shell_present_resize(
	struct shell_present *present,
	uint32_t width,
	uint32_t height)
{
	VkSwapchainKHR old;
	VkResult error;

	/* Nothing may still use the old images. */
	present->operation = "vkDeviceWaitIdle";
	error = vkDeviceWaitIdle(present->device);
	if (error != VK_SUCCESS)
		return error;

	/* The old targets go, and the new chain replaces the old one. */
	present_targets_free(present);
	old = present->swapchain;
	present->swapchain = VK_NULL_HANDLE;
	error = present_swapchain(present, width, height, old);
	vkDestroySwapchainKHR(present->device, old, NULL);
	if (error != VK_SUCCESS)
		return error;

	/* Framebuffers for the new images. */
	error = present_targets(present);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the next frame is shown at the new size. */
	return VK_SUCCESS;
}

/*
 * Draws a display list, scrolled up by scroll_y, into the next swapchain
 * image and presents it.
 *
 * Returns VK_ERROR_OUT_OF_DATE_KHR when the swapchain no longer matches
 * the window; the caller resizes and draws again.
 */
VkResult
shell_present_frame(
	struct shell_present *present,
	const struct paint_list *list,
	struct text_system *text,
	layout_unit scroll_y)
{
	VkPresentInfoKHR info;
	uint32_t image;
	VkResult error;

	/* The image to draw into, once the compositor has given one back. */
	present->operation = "vkAcquireNextImageKHR";
	error = vkAcquireNextImageKHR(present->device, present->swapchain, PRESENT_TIMEOUT, present->acquired, VK_NULL_HANDLE, &image);
	if (error != VK_SUCCESS && error != VK_SUBOPTIMAL_KHR)
		return error;

	/* Draws the page into it after the acquire, signalling the image's present semaphore. */
	error = paint_gpu_draw(
		&present->gpu,
		list,
		text,
		scroll_y,
		present->targets[image].framebuffer,
		present->extent,
		present->acquired,
		present->targets[image].rendered);
	if (error != VK_SUCCESS) {
		present->operation = present->gpu.operation;
		return error;
	}

	/* Presents the image to the window. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	info.waitSemaphoreCount = 1U;
	info.pWaitSemaphores = &present->targets[image].rendered;
	info.swapchainCount = 1U;
	info.pSwapchains = &present->swapchain;
	info.pImageIndices = &image;
	present->operation = "vkQueuePresentKHR";
	error = vkQueuePresentKHR(present->queue, &info);
	if (error != VK_SUCCESS && error != VK_SUBOPTIMAL_KHR)
		return error;

	/* Succeeded: the frame is in the window. */
	return VK_SUCCESS;
}

/*
 * Releases every Vulkan object, children before their parents.
 */
void
shell_present_close(
	struct shell_present *present)
{
	/* The device's objects, once nothing runs. */
	if (present->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(present->device);
		present_targets_free(present);

		/* The renderer's objects (it does not own the device). */
		if (present->gpu_open)
			paint_gpu_close(&present->gpu);

		/* The acquire semaphore, the swapchain and the device itself. */
		if (present->acquired != VK_NULL_HANDLE)
			vkDestroySemaphore(present->device, present->acquired, NULL);
		if (present->swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(present->device, present->swapchain, NULL);
		vkDestroyDevice(present->device, NULL);
	}

	/* The surface and the instance. */
	if (present->surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(present->instance, present->surface, NULL);
	if (present->instance != VK_NULL_HANDLE)
		vkDestroyInstance(present->instance, NULL);

	/* Nothing is owned any more. */
	memset(present, 0, sizeof(*present));
}

/* Chooses a physical device and a queue family that draws and presents to the surface, and makes the device. */
static VkResult
present_device(
	struct shell_present *present)
{
	VkPhysicalDevice devices[8];
	VkQueueFamilyProperties families[16];
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo create;
	const char *extension;
	float priority;
	uint32_t count;
	uint32_t family_count;
	uint32_t index;
	uint32_t family;
	VkBool32 supported;
	VkResult error;

	/* The physical devices (the first eight are enough). */
	count = 8U;
	present->operation = "vkEnumeratePhysicalDevices";
	error = vkEnumeratePhysicalDevices(present->instance, &count, devices);
	if (error != VK_SUCCESS && error != VK_INCOMPLETE)
		return error;

	/* The first family of any device that draws and presents to this surface. */
	for (index = 0U; index < count && present->physical == VK_NULL_HANDLE; index++) {
		family_count = 16U;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &family_count, families);
		for (family = 0U; family < family_count; family++) {
			/* A family must draw and have a queue. */
			if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U)
				continue;
			if (families[family].queueCount == 0U)
				continue;

			/* And present to this very surface. */
			supported = VK_FALSE;
			error = vkGetPhysicalDeviceSurfaceSupportKHR(devices[index], family, present->surface, &supported);
			if (error != VK_SUCCESS)
				continue;
			if (supported == VK_FALSE)
				continue;

			/* This family of this device draws the window. */
			present->physical = devices[index];
			present->family = family;
			break;
		}
	}

	/* No device can draw this window. */
	if (present->physical == VK_NULL_HANDLE) {
		present->operation = "finding a device that presents to the window";
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	/* One queue of that family and the swapchain extension. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = present->family;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create.queueCreateInfoCount = 1U;
	create.pQueueCreateInfos = &queue;
	create.enabledExtensionCount = 1U;
	create.ppEnabledExtensionNames = &extension;
	present->operation = "vkCreateDevice";
	error = vkCreateDevice(present->physical, &create, NULL, &present->device);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the device and its queue. */
	vkGetDeviceQueue(present->device, present->family, 0U, &present->queue);
	return VK_SUCCESS;
}

/* Makes the swapchain at a size, in an 8-bit UNORM format, presenting in FIFO order. */
static VkResult
present_swapchain(
	struct shell_present *present,
	uint32_t width,
	uint32_t height,
	VkSwapchainKHR old)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR formats[16];
	VkSwapchainCreateInfoKHR create;
	VkFormat format;
	uint32_t count;
	uint32_t index;
	VkResult error;

	/* The formats the surface offers. */
	count = 16U;
	present->operation = "vkGetPhysicalDeviceSurfaceFormatsKHR";
	error = vkGetPhysicalDeviceSurfaceFormatsKHR(present->physical, present->surface, &count, formats);
	if (error != VK_SUCCESS && error != VK_INCOMPLETE)
		return error;

	/* The first of those formats that is 8-bit UNORM (not sRGB: the renderers blend the stored values). */
	format = VK_FORMAT_UNDEFINED;
	for (index = 0U; index < count; index++) {
		/* Blue first, as zdesktop offers. */
		if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM) {
			format = formats[index].format;
			break;
		}

		/* Or red first. */
		if (formats[index].format == VK_FORMAT_R8G8B8A8_UNORM) {
			format = formats[index].format;
			break;
		}
	}

	/* A surface without one cannot show the colors as they are. */
	if (format == VK_FORMAT_UNDEFINED) {
		present->operation = "finding an 8-bit UNORM surface format";
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	/* A new chain keeps the format the renderer's pass was made for. */
	if (present->format != VK_FORMAT_UNDEFINED && format != present->format) {
		present->operation = "keeping the surface format";
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	/* The chain's format. */
	present->format = format;

	/* The surface's limits. */
	present->operation = "vkGetPhysicalDeviceSurfaceCapabilitiesKHR";
	error = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(present->physical, present->surface, &capabilities);
	if (error != VK_SUCCESS)
		return error;

	/* The window's size, clamped to the surface's range. */
	if (width < capabilities.minImageExtent.width)
		width = capabilities.minImageExtent.width;
	if (height < capabilities.minImageExtent.height)
		height = capabilities.minImageExtent.height;
	if (width > capabilities.maxImageExtent.width)
		width = capabilities.maxImageExtent.width;
	if (height > capabilities.maxImageExtent.height)
		height = capabilities.maxImageExtent.height;
	present->extent.width = width;
	present->extent.height = height;

	/* Three images when the surface allows, replacing the old chain. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create.surface = present->surface;
	create.minImageCount = 3U;
	if (create.minImageCount < capabilities.minImageCount)
		create.minImageCount = capabilities.minImageCount;
	if (capabilities.maxImageCount != 0U && create.minImageCount > capabilities.maxImageCount)
		create.minImageCount = capabilities.maxImageCount;
	create.imageFormat = present->format;
	create.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	create.imageExtent = present->extent;
	create.imageArrayLayers = 1U;
	create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.preTransform = capabilities.currentTransform;
	create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	create.clipped = VK_TRUE;
	create.oldSwapchain = old;

	/* Opaque when the surface takes it; the page is opaque, so premultiplied alpha shows the same. */
	create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0U)
		create.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

	/* The chain. */
	present->operation = "vkCreateSwapchainKHR";
	error = vkCreateSwapchainKHR(present->device, &create, NULL, &present->swapchain);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the chain at the window's size. */
	return VK_SUCCESS;
}

/* Makes a view, a framebuffer and a present semaphore for each swapchain image. */
static VkResult
present_targets(
	struct shell_present *present)
{
	VkImage images[PRESENT_IMAGES_MAX];
	VkImageViewCreateInfo view;
	VkFramebufferCreateInfo framebuffer;
	VkSemaphoreCreateInfo semaphore;
	uint32_t index;
	VkResult error;

	/* The swapchain's images. */
	present->count = PRESENT_IMAGES_MAX;
	present->operation = "vkGetSwapchainImagesKHR";
	error = vkGetSwapchainImagesKHR(present->device, present->swapchain, &present->count, images);
	if (error != VK_SUCCESS && error != VK_INCOMPLETE)
		return error;

	/* A zeroed table, so that a failure part-way leaves only made objects to release. */
	present->targets = calloc(present->count, sizeof(present->targets[0]));
	if (present->targets == NULL) {
		present->operation = "allocating the swapchain's targets";
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	/* Each image's objects. */
	for (index = 0U; index < present->count; index++) {
		/* The view of the image. */
		present->targets[index].image = images[index];
		memset(&view, 0, sizeof(view));
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.image = images[index];
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = present->format;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.levelCount = 1U;
		view.subresourceRange.layerCount = 1U;
		present->operation = "vkCreateImageView";
		error = vkCreateImageView(present->device, &view, NULL, &present->targets[index].view);
		if (error != VK_SUCCESS)
			return error;

		/* The framebuffer over it, in the renderer's pass. */
		memset(&framebuffer, 0, sizeof(framebuffer));
		framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer.renderPass = present->gpu.pass;
		framebuffer.attachmentCount = 1U;
		framebuffer.pAttachments = &present->targets[index].view;
		framebuffer.width = present->extent.width;
		framebuffer.height = present->extent.height;
		framebuffer.layers = 1U;
		present->operation = "vkCreateFramebuffer";
		error = vkCreateFramebuffer(present->device, &framebuffer, NULL, &present->targets[index].framebuffer);
		if (error != VK_SUCCESS)
			return error;

		/* The semaphore its present waits for. */
		memset(&semaphore, 0, sizeof(semaphore));
		semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		present->operation = "vkCreateSemaphore";
		error = vkCreateSemaphore(present->device, &semaphore, NULL, &present->targets[index].rendered);
		if (error != VK_SUCCESS)
			return error;
	}

	/* Succeeded: every image can be drawn into. */
	return VK_SUCCESS;
}

/* Releases the objects of the swapchain's images (not the images, which are the swapchain's). */
static void
present_targets_free(
	struct shell_present *present)
{
	uint32_t index;

	/* Each image's semaphore, framebuffer and view, where made. */
	for (index = 0U; present->targets != NULL && index < present->count; index++) {
		if (present->targets[index].rendered != VK_NULL_HANDLE)
			vkDestroySemaphore(present->device, present->targets[index].rendered, NULL);
		if (present->targets[index].framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(present->device, present->targets[index].framebuffer, NULL);
		if (present->targets[index].view != VK_NULL_HANDLE)
			vkDestroyImageView(present->device, present->targets[index].view, NULL);
	}

	/* The table itself. */
	free(present->targets);
	present->targets = NULL;
	present->count = 0U;
}
