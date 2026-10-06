/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display of libkeiland-backend (ws131-p008): the primary display node
 * and the Vulkan display's acquisition and release.
 *
 * The compositor composes with Vulkan into a VK_KHR_display swapchain.  On
 * Linux and FreeBSD the display belongs to the seat's primary node (the DRM
 * master file), which Vulkan is given through VK_EXT_acquire_drm_display;
 * on zedBSD libvulkan reaches the display itself and there is nothing to
 * hand over.  The backend uses only the Vulkan instance and the procedure
 * lookup the compositor passes; it makes no Vulkan object of its own.
 */

#ifndef KL_BACKEND_DISPLAY_H
#define KL_BACKEND_DISPLAY_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

struct kl_backend;

/*
 * The compositor's Vulkan, as far as the display needs it: its instance
 * and the procedure lookup that resolves the extensions on it.
 */
struct kl_backend_vulkan {
	VkInstance instance;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
};

/*
 * The primary display node the seat holds: its descriptor (-1 while
 * paused) and its path, which the compositor's Vulkan inquiry must open
 * too.  Returns 0, or ENOTSUP where the display has no node to share
 * (zedBSD).
 */
int kl_backend_display_node(const struct kl_backend *backend, int *descriptor, const char **path);

/*
 * Lets Vulkan acquire the display chosen on physical, with its own
 * duplicate of the primary node (the seat keeps the original).  Returns
 * VK_SUCCESS or the failure (VK_ERROR_INITIALIZATION_FAILED without a node,
 * VK_ERROR_EXTENSION_NOT_PRESENT without the extension).
 */
VkResult kl_backend_display_acquire(struct kl_backend *backend, const struct kl_backend_vulkan *vulkan, VkPhysicalDevice physical, VkDisplayKHR display);

/*
 * Releases Vulkan's duplicate after the swapchain has gone.  Returns
 * VK_SUCCESS or the failure of the release.
 */
VkResult kl_backend_display_release(struct kl_backend *backend, const struct kl_backend_vulkan *vulkan, VkPhysicalDevice physical, VkDisplayKHR display);

/*
 * The direct scanout of a client's buffer (ws122-p005b, the compositor's
 * game mode, the 2026-10-06 user decision B and the Guardrail's exception
 * to "the compositor uses only libvulkan"): while a fullscreen video or
 * game is all that shows, the compositor closes its swapchain, and the
 * backend shows the client's GPU buffer on the display without composing
 * it.  Only zedBSD has it (its kernel's display ioctls, which stay in the
 * backend's zedBSD tree); elsewhere it is ENOTSUP and the compositor keeps
 * composing.
 *
 *   kl_backend_scanout_open     claims the display for an output of the
 *       given size (after the swapchain has gone); 0, ENOTSUP, or the
 *       claim's failure
 *   kl_backend_scanout_present  shows a buffer (one of the GPU buffers'
 *       protocol, keiland-backend-gpu.h, whose rendering is done) as the
 *       whole output; the one it replaces is no longer read when it
 *       returns.  0, EINVAL for a buffer that cannot be scanned out (not a
 *       GPU buffer, another size), or the present's failure
 *   kl_backend_scanout_close    releases the display (the swapchain may be
 *       made again); NULL does nothing
 */
struct kl_backend_scanout;
struct kl_backend_protocol_host;
struct kl_backend_resource;
int kl_backend_scanout_open(struct kl_backend *backend, uint32_t width, uint32_t height, struct kl_backend_scanout **scanout);
int kl_backend_scanout_present(struct kl_backend_scanout *scanout, const struct kl_backend_protocol_host *host, struct kl_backend_resource *buffer);
void kl_backend_scanout_close(struct kl_backend_scanout *scanout);

/*
 * Fills path with the compositor's default socket for this system (the
 * user's runtime directory, else /tmp, then wayland-keiland).  Returns 0,
 * ENAMETOOLONG, or ENOTSUP where the compositor keeps its own default
 * (zedBSD).
 */
int kl_backend_display_socket(char *path, size_t size);

#endif
