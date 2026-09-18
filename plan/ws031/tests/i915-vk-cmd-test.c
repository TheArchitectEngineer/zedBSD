/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the native Vulkan executor command framework (p002).
 * Exercises the object table, the little-endian wire reader/writer, opcode
 * routing to the module dispatchers, the builtin commands, the capset and the
 * attach/open/close lifetime, without any hardware or kernel.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernel allocator the executor uses, backed by the host heap. */
static unsigned fixture_live;

void *
kern_calloc(size_t count, size_t size)
{
	void *pointer = calloc(count, size);
	if (pointer != NULL)
		fixture_live++;
	return pointer;
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		fixture_live--;
	free(pointer);
}

/* The executor under test. */
void kern_io_write_barrier(void) { }

#include "../../../src/drivers/gpu/i915/vk/cmd.c"
#include "../../../src/drivers/gpu/i915/vk/vk.c"

/* Records the last routed dispatch so the routing can be checked. */
static uint32_t routed_opcode;
static int routed_module;

int
i915_vk_res_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)r; (void)w; routed_opcode = op; routed_module = 1; return 0;
}

int
i915_vk_pipe_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)r; (void)w; routed_opcode = op; routed_module = 2; return 0;
}

int
i915_vk_cmdbuf_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)r; (void)w; routed_opcode = op; routed_module = 3; return 0;
}

int
i915_vk_sync_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)r; (void)w; routed_opcode = op; routed_module = 4; return 0;
}

int
i915_vk_wsi_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)r; (void)w; routed_opcode = op; routed_module = 5; return 0;
}

/* Encodes a command header the way libvulkan frames one. */
static size_t
put_command(uint8_t *buffer, uint32_t opcode)
{
	buffer[0] = (uint8_t)opcode;
	buffer[1] = (uint8_t)(opcode >> 8);
	buffer[2] = (uint8_t)(opcode >> 16);
	buffer[3] = (uint8_t)(opcode >> 24);
	buffer[4] = 1; buffer[5] = 0; buffer[6] = 0; buffer[7] = 0;
	return 8U;
}

static void
test_object_table(void)
{
	struct i915_vk_device vk;
	int marker[300];
	unsigned index;
	int error;

	memset(&vk, 0, sizeof(vk));
	error = i915_vk_object_table_create(&vk.objects);
	assert(error == 0);

	/* Insert grows past the initial capacity and every handle resolves. */
	for (index = 0U; index < 300U; index++) {
		error = i915_vk_obj_insert(&vk, I915_VK_OBJ_BUFFER, index + 1U, &marker[index]);
		assert(error == 0);
	}
	for (index = 0U; index < 300U; index++)
		assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, index + 1U) == &marker[index]);

	/* A handle of another kind does not collide. */
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_IMAGE, 1U) == NULL);

	/* Reinserting a handle replaces the object in place. */
	error = i915_vk_obj_insert(&vk, I915_VK_OBJ_BUFFER, 1U, &marker[7]);
	assert(error == 0);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, 1U) == &marker[7]);

	/* Remove drops exactly one handle and keeps the rest resolvable. */
	i915_vk_obj_remove(&vk, I915_VK_OBJ_BUFFER, 1U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, 1U) == NULL);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, 2U) == &marker[1]);

	i915_vk_object_table_destroy(vk.objects);
}

static void
test_reader_writer(void)
{
	uint8_t buffer[24];
	uint8_t reply[32];
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	uint32_t word;
	uint64_t wide;

	/* A writer lays down little-endian words a reader recovers exactly. */
	writer.base = buffer; writer.size = sizeof(buffer); writer.offset = 0U; writer.error = 0;
	i915_vk_reply_u32(&writer, 0x01020304U);
	i915_vk_reply_u64(&writer, 0x1122334455667788ULL);
	assert(writer.error == 0);
	assert(writer.offset == 12U);
	assert(buffer[0] == 0x04 && buffer[3] == 0x01);

	reader.base = buffer; reader.size = writer.offset; reader.offset = 0U; reader.error = 0;
	word = i915_vk_read_u32(&reader);
	wide = i915_vk_read_u64(&reader);
	assert(reader.error == 0);
	assert(word == 0x01020304U);
	assert(wide == 0x1122334455667788ULL);

	/* Reading past the end latches the error and stops advancing. */
	word = i915_vk_read_u32(&reader);
	assert(reader.error != 0);
	assert(word == 0U);

	/* A reply that overflows its buffer latches the writer error. */
	writer.base = reply; writer.size = 6U; writer.offset = 0U; writer.error = 0;
	i915_vk_reply_u64(&writer, 0U);
	assert(writer.error != 0);
}

static void
test_routing(void)
{
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	static const uint32_t opcodes[12] = {21U, 50U, 70U, 59U, 65U, 82U, 85U, 106U, 133U, 35U, 40U, 47U};
	static const int modules[12] = {1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4};
	uint8_t command[32];
	uint8_t reply[32];
	unsigned index;
	int error;

	memset(&vk, 0, sizeof(vk));
	memset(&session, 0, sizeof(session));
	session.vk = &vk;

	/* Each opcode reaches exactly the module its range names. */
	for (index = 0U; index < 12U; index++) {
		routed_module = 0;
		routed_opcode = 0;
		put_command(command, opcodes[index]);
		reader.base = command; reader.size = 8U; reader.offset = 0U; reader.error = 0;
		writer.base = reply; writer.size = sizeof(reply); writer.offset = 0U; writer.error = 0;
		error = i915_vk_cmd_dispatch(&session, &reader, &writer);
		assert(error == 0);
		assert(routed_module == modules[index]);
		assert(routed_opcode == opcodes[index]);
	}

	/*
	 * The builtin version command doubles as the reply-stream completion probe:
	 * its request carries a pApiVersion present word and its reply is the fixed
	 * 20-byte trailer [opcode][result][present][apiVersion].
	 */
	routed_module = 0;
	put_command(command, 137U);
	memset(command + 8, 0, 8);
	command[8] = 1U;		/* pApiVersion present */
	reader.base = command; reader.size = 16U; reader.offset = 0U; reader.error = 0;
	writer.base = reply; writer.size = sizeof(reply); writer.offset = 0U; writer.error = 0;
	error = i915_vk_cmd_dispatch(&session, &reader, &writer);
	assert(error == 0);
	assert(routed_module == 0);
	assert(writer.offset == 20U);
	assert((uint32_t)reply[0] == 137U);		/* echoed opcode */
	assert(reply[4] == 0U);				/* VK_SUCCESS */
	assert(reply[8] == 1U);				/* present low */
	assert(reply[12] == 0U);			/* present high */

	/*
	 * An unimplemented builtin command is refused, not accepted as an empty
	 * success: vkCreateInstance (0) with a payload, followed by a valid version
	 * probe.  The decoder must stop at the first command; the probe that follows
	 * must NOT run (its bytes are payload as far as this decoder can tell).
	 */
	{
		static const uint32_t refused[5] = {0U, 1U, 17U, 148U, 180U};
		unsigned which;

		for (which = 0U; which < 5U; which++) {
			size_t reply_bytes = sizeof(reply);

			memset(command, 0, sizeof(command));
			put_command(command, refused[which]);
			command[4] = 1U;		/* reply requested */
			command[8] = 137U;		/* payload that happens to look like an opcode */
			put_command(command + 16, 137U);
			command[24] = 1U;
			memset(reply, 0xee, sizeof(reply));
			error = drv_i915_vk_command(&session, command, 32U, reply, &reply_bytes);
			assert(error == ENOTSUP);
			assert(reply_bytes == sizeof(reply));	/* no reply length published */
			assert((uint8_t)reply[8] == 0xeeU);	/* the trailing probe never ran */
		}
	}
}

static void
test_lifetime(void)
{
	struct i915_vk_device *vk;
	struct i915_vk_session *session;
	unsigned before;
	int error;

	before = fixture_live;

	/* Attach builds the executor and a non-empty capset. */
	error = drv_i915_vk_attach((struct i915_device *)0x1, &vk);
	assert(error == 0);
	assert(vk->capset_bytes >= 156U);

	/* Open and close a session, then detach; nothing leaks. */
	error = drv_i915_vk_open(vk, (struct i915_session *)0x2, &session);
	assert(error == 0);
	assert(session->vk == vk);
	drv_i915_vk_close(session);
	drv_i915_vk_detach(vk);

	assert(fixture_live == before);
}

int
main(void)
{
	test_object_table();
	test_reader_writer();
	test_routing();
	test_lifetime();
	printf("i915 vk cmd host test PASS\n");
	return 0;
}
