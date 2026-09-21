/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * KMS/modeset and scanout for the eDP panel.  Drives display registers to set
 * a mode and flip a plane to a GGTT-mapped surface.  Contract for p010; see
 * external-design.md section 4.10.  Register layouts are transcribed with
 * attribution into vk/linux/display-gen12.inc.
 */

#ifndef I915_VK_DISPLAY_H
#define I915_VK_DISPLAY_H

#include "vk-internal.h"

/* Detects the panel, reads EDID and sets a mode on the eDP transcoder. */
int
i915_vk_display_init(
	struct i915_vk_device *vk);

/* Reports the active mode. */
int
i915_vk_display_mode(
	struct i915_vk_device *vk,
	uint32_t *width,
	uint32_t *height,
	uint32_t *stride);

/* Points the primary plane at an image's GGTT offset and scans it out. */
int
i915_vk_display_flip(
	struct i915_vk_device *vk,
	struct i915_vk_image *scanout);

/* Releases the display path. */
void
i915_vk_display_fini(
	struct i915_vk_device *vk);

#endif /* I915_VK_DISPLAY_H */
