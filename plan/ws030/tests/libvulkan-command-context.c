/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Joins actual command recording and context framing through independent native and kernel peers.
 */

#define COMMANDS_REAL_CONTEXT
#define main command_peer_main
#define vulkan_context_execute command_peer_execute
#include "libvulkan-commands.c"
#undef vulkan_context_execute
#undef main

#define main context_peer_main
#include "libvulkan-context.c"
#undef main

static void decode_command_stream(const uint8_t *bytes, size_t count, uint8_t *reply, size_t capacity);

/*
 * Exercises repeated flush and a legal large copy through the actual mapped context transport.
 */
int
main(
	void)
{
	int status;

	/* The kernel peer hands the exact shared stream to an independent Vulkan decoder. */
	context_command_peer = decode_command_stream;
	status = command_peer_main();
	if (status != 0)
		return status;

	/* Real context ownership must have used shared streaming and returned all mappings. */
	assert(exported_stream_seen != 0);
	assert(map_calls == unmap_calls);
	puts("PASS actual command/context: End-Reset-rerecord no-reply flush and legal 1152000-byte copy payload");

	/* Succeeded: both production layers preserved zero-reply framing and final End validation. */
	return 0;
}

/* Decodes actual transported bytes and writes only the reply produced by the independent native peer. */
static void
decode_command_stream(
	const uint8_t *bytes,
	size_t count,
	uint8_t *reply,
	size_t capacity)
{
	struct vulkan_context peer_context;
	struct vulkan_writer command;
	struct vulkan_reader response;
	VkResult status;

	/* The peer receives bytes from the kernel-owned mapped allocation, never the caller's source pointer. */
	memset(&peer_context, 0, sizeof(peer_context));
	memset(&command, 0, sizeof(command));
	command.data = (uint8_t *)bytes;
	command.bytes = count;
	status = command_peer_execute(&peer_context, &command, capacity, &response);
	assert(status == VK_SUCCESS);

	/* No-reply flushes leave the application reply empty while context still appends its trailer. */
	assert(response.bytes <= capacity);
	if (response.bytes != 0)
		memcpy(reply, response.data, response.bytes);

	/* Peer response ownership ends before the kernel publishes the decoder trailer. */
	vulkan_reader_finish(&response);

	/* Succeeded: the exact native response precedes its independent context completion trailer. */
	return;
}
