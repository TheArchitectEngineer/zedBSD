/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's dispatcher (ws037-p007 stage D2): each command goes to
 * the one handler that owns its opcode.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/*
 * The handlers, each owning a group of opcodes.  The list grows with the
 * executor's stages (D2: the instance and device; D3: the objects and the
 * recorded work); it is fixed for the node's life.
 */
static const nvrtx_vk_route dispatch_routes[] = {
	nvrtx_vk_device_route
};

/*
 * Runs one command through the handler that owns its opcode.
 *
 * Returns ENOTSUP for an opcode no handler owns (its body cannot be
 * skipped: commands carry no length), or the handler's error.
 */
int
nvrtx_vk_dispatch(
	struct nvrtx_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	size_t index;
	int handled;
	int error;

	/* Asks each handler until one takes the command. */
	handled = 0;
	for (index = 0U; index < sizeof(dispatch_routes) / sizeof(dispatch_routes[0]); index++) {
		error = dispatch_routes[index](session, opcode, requested, reader, reply, &handled);
		if (error != 0)
			return error;
		if (handled != 0)
			break;
	}

	/* Refuses a command no handler owns. */
	if (handled == 0)
		return ENOTSUP;

	/* Succeeded: one handler ran the command. */
	return 0;
}
