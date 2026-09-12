/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Measures the selected public platform ABI against a pinned Khronos header. */

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <stddef.h>

/* The binary table compares every field, public scalar width and declaration constant. */
const unsigned long long wayland_vulkan_measurements[] __attribute__((used)) = {
	sizeof(VkWaylandSurfaceCreateInfoKHR),
	offsetof(VkWaylandSurfaceCreateInfoKHR, sType),
	offsetof(VkWaylandSurfaceCreateInfoKHR, pNext),
	offsetof(VkWaylandSurfaceCreateInfoKHR, flags),
	offsetof(VkWaylandSurfaceCreateInfoKHR, display),
	offsetof(VkWaylandSurfaceCreateInfoKHR, surface),
	sizeof(VkWaylandSurfaceCreateFlagsKHR),
	VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
	VK_KHR_WAYLAND_SURFACE_SPEC_VERSION
};

#ifndef VK_NO_PROTOTYPES
/* These typed initializers also reject a public prototype/PFN signature disagreement. */
PFN_vkCreateWaylandSurfaceKHR wayland_create_signature = vkCreateWaylandSurfaceKHR;
PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR wayland_support_signature = vkGetPhysicalDeviceWaylandPresentationSupportKHR;
#endif
