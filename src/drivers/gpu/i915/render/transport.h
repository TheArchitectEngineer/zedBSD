/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The transport of the Vulkan executor.
 *
 * The submit ioctl returns no reply inline.  A stream opens with
 * vkSetReplyCommandStreamMESA (178), which names a session blob by its
 * resource id; the replies are written there, and the stream ends with
 * vkSeekReplyCommandStreamMESA (179) and vkEnumerateInstanceVersion (137),
 * whose 20-byte reply at a fixed position is the completion trailer
 * libvulkan polls.  A command too long for one submission travels in a
 * session blob and is named by vkExecuteCommandStreamsMESA (180).
 */

#ifndef DRIVERS_GPU_I915_RENDER_TRANSPORT_H
#define DRIVERS_GPU_I915_RENDER_TRANSPORT_H

#include "internal.h"

#include <stddef.h>
#include <stdint.h>

void *drv_i915_render_transport_reply(struct i915_session *session, const void *wire, uint32_t bytes, size_t *capacity);
int drv_i915_render_transport_execute(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
int drv_i915_render_transport_dispatch(struct i915_render_session *session, uint32_t opcode, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

#endif
