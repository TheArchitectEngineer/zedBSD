/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Supplies the empty OS hooks required by zedBSD.
 *
 * sessiond hands over the seat before the compositor starts; libvulkan
 * reaches the display itself. There is no separate OS descriptor to poll.
 */

#include "userland/desktop/wayland/zwl-os.h"

/*
 * Takes the OS resources needed before Vulkan opens.
 *
 * zedBSD needs no additional OS operation here.
 */
int
zwl_os_open(
	struct zwl_server *server)
{
	(void)server;

	/* Succeeded: sessiond has already supplied seat access. */
	return 0;
}

/*
 * Returns the compositor resources to the OS.
 *
 * zedBSD needs no additional OS operation here.
 */
void
zwl_os_close(
	struct zwl_server *server)
{
	(void)server;

	/* Succeeded: sessiond retains responsibility for seat ownership. */
	return;
}

/*
 * Counts the OS descriptors needed in the next poll.
 *
 * zedBSD needs no additional OS operation here.
 */
size_t
zwl_os_poll_count(
	const struct zwl_server *server)
{
	(void)server;

	/* Succeeded: no additional OS descriptors need polling. */
	return 0;
}

/*
 * Fills the OS module's poll descriptors.
 *
 * zedBSD needs no additional OS operation here.
 */
void
zwl_os_poll_fill(
	struct zwl_server *server,
	struct pollfd *descriptors)
{
	(void)server;
	(void)descriptors;

	/* Succeeded: the OS has no poll descriptors to add. */
	return;
}

/*
 * Handles the OS module's reported poll events.
 *
 * zedBSD needs no additional OS operation here.
 */
void
zwl_os_poll_done(
	struct zwl_server *server,
	const struct pollfd *descriptors)
{
	(void)server;
	(void)descriptors;

	/* Succeeded: no separate OS events require dispatch. */
	return;
}

/*
 * Gives Vulkan permission to acquire the chosen display.
 *
 * zedBSD needs no additional OS operation here.
 */
VkResult
zwl_os_display_acquire(
	struct zwl_server *server,
	VkPhysicalDevice physical,
	VkDisplayKHR display)
{
	(void)server;
	(void)physical;
	(void)display;

	/* Succeeded: libvulkan reaches the display without an OS seat descriptor. */
	return VK_SUCCESS;
}

/*
 * Returns an acquired display after its swapchain is destroyed.
 *
 * zedBSD needs no additional OS operation here.
 */
void
zwl_os_display_release(
	struct zwl_server *server,
	VkPhysicalDevice physical,
	VkDisplayKHR display)
{
	(void)server;
	(void)physical;
	(void)display;

	/* Succeeded: libvulkan releases the display with the swapchain. */
	return;
}
