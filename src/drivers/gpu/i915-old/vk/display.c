/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * KMS/modeset and scanout for the eDP panel.
 *
 * Setting a mode drives the PLL, DDI, transcoder and pipe display registers,
 * and a flip points the primary plane at a GGTT-mapped surface.  That register
 * sequence is completed during the on-hardware bring-up (the display is only
 * observable there); this module fixes the display interface and the default
 * mode so the WSI and pipeline build against it.
 */

#include "vk-internal.h"
#include "display.h"

#include <errno.h>

/* Detects the panel and sets a mode; the register sequence lands at bring-up. */
int
i915_vk_display_init(
	struct i915_vk_device *vk)
{
	/* A default mode lets the swapchain size its images before bring-up. */
	vk->display_width = 1920U;
	vk->display_height = 1080U;
	vk->display_stride = 1920U * 4U;
	return 0;
}

/* Reports the active mode. */
int
i915_vk_display_mode(
	struct i915_vk_device *vk,
	uint32_t *width,
	uint32_t *height,
	uint32_t *stride)
{
	*width = vk->display_width;
	*height = vk->display_height;
	*stride = vk->display_stride;
	return 0;
}

/* Points the primary plane at an image; the plane registers land at bring-up. */
int
i915_vk_display_flip(
	struct i915_vk_device *vk,
	struct i915_vk_image *scanout)
{
	(void)vk;
	(void)scanout;
	return 0;
}

/* Releases the display path. */
void
i915_vk_display_fini(
	struct i915_vk_device *vk)
{
	vk->display_width = 0U;
	vk->display_height = 0U;
	vk->display_stride = 0U;
}
