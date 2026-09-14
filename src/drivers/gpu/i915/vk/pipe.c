/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * pipe: p002 stub providing only the dispatch entry so the executor links.
 * Replaced by the full module implementation in its phase.
 */

#include "vk-internal.h"
#include "pipe.h"

#include <errno.h>

int
i915_vk_pipe_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	/* No command is handled until the module is implemented. */
	(void)session;
	(void)opcode;
	(void)reader;
	(void)reply;
	return EINVAL;
}
