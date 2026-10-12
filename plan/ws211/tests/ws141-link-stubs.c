/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The two symbols the WS141 driver host test's link lacks (ws211-p010).
 *
 * libvulkan's swapchain-content notes live in content.c, which the test
 * does not build; the driver's decoder reads the fine counter only for its
 * cost report, and a host without one reports none.
 */

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan_core.h>

struct VkCommandBuffer_T;

void vulkan_content_note_image(struct VkCommandBuffer_T *command, VkImage image);
bool kern_rtc_read_counter(uint64_t *counter, uint64_t *frequency_hz);

/*
 * Records nothing: the test's command buffers write no swapchain image.
 */
void
vulkan_content_note_image(
	struct VkCommandBuffer_T *command,
	VkImage image)
{
	/* No swapchain to note. */
	(void)command;
	(void)image;
}

/*
 * Reports no fine counter, so the decoder's counter report stays empty.
 */
bool
kern_rtc_read_counter(
	uint64_t *counter,
	uint64_t *frequency_hz)
{
	/* No counter on this host model. */
	*counter = 0U;
	*frequency_hz = 0U;

	/* The counter is absent. */
	return false;
}
