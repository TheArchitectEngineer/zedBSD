/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Defines the OS resources and display ownership used by the compositor. */
#ifndef ZWL_OS_H
#define ZWL_OS_H

#include <poll.h>
#include <stddef.h>
#include <vulkan/vulkan.h>

struct zwl_server;

/*
 * Close is called even after open failed and must tolerate partial startup.
 * Poll pointers address the OS module's first entry in the current snapshot.
 * Display release pairs with a successful acquire, including failed startup.
 */

/* Takes the OS resources needed before Vulkan opens. */
int zwl_os_open(struct zwl_server *server);

/* Returns the compositor resources to the OS. */
void zwl_os_close(struct zwl_server *server);

/* Counts the OS descriptors needed in the next poll. */
size_t zwl_os_poll_count(const struct zwl_server *server);

/* Fills the OS module's poll descriptors. */
void zwl_os_poll_fill(struct zwl_server *server, struct pollfd *descriptors);

/* Handles the OS module's reported poll events. */
void zwl_os_poll_done(struct zwl_server *server, const struct pollfd *descriptors);

/* Gives Vulkan permission to acquire the chosen display. */
VkResult zwl_os_display_acquire(struct zwl_server *server, VkPhysicalDevice physical, VkDisplayKHR display);

/* Returns an acquired display after its swapchain is destroyed. */
void zwl_os_display_release(struct zwl_server *server, VkPhysicalDevice physical, VkDisplayKHR display);

#endif
