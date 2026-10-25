/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One frame of commands: its decode, its transport commands, the staging
 * of the commands it relays, and its submission.
 *
 * A frame selects its reply region in a blob of the session's
 * (SET_REPLY_STREAM), moves the cursor in it (SEEK_REPLY_STREAM), may run
 * a command stream carried in a blob (EXECUTE_STREAMS) and ends with the
 * trailer (ENUMERATE_INSTANCE_VERSION), which libvulkan polls for.  Every
 * other command goes to the handler of its group.  The relayed records
 * collect in a buffer of the frame's; once the whole frame decoded, they
 * are copied into the staging ring after the frame's result page and a
 * single GHCI_FRAME packet points the firmware at them.  A frame that
 * relays nothing and has no frame of its session ahead of it completes
 * at once.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/powervr/vk/vk-stream.c
 * (the framing of a frame and its transport commands).
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-op.h>

#include "../packets.h"
#include "../frame.h"
#include "vk.h"
#include "vk-engine.h"

/* The smallest reply region: the trailer's echoed number, result, presence and version. */
#define REPLY_MIN			20U

/* The dwords of the GHCI_FRAME packet: the header and its eight. */
#define FRAME_PACKET_DWORDS		(1U + GHCI_FRAME_DWORDS)

/* The words of a staged record's head: the number, the flags and the result index. */
#define RECORD_HEAD_BYTES		12U

/* The first stage, patch, release and fence room of a frame, doubled as it grows. */
#define STAGE_FIRST_BYTES		1024U
#define LIST_FIRST			16U

/* How long a submission waits for room in the staging ring, and for a session's frames at its close. */
#define STAGING_WAIT_MS			2000U
#define DRAIN_WAIT_MS			5000U

/* The routes, tried in order; each takes only its own numbers. */
static const ghci_vk_route routes[] = {
	ghci_vk_device_route,
	ghci_vk_memory_route,
	ghci_vk_program_route,
	ghci_vk_relay_route
};

static int run_stream(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int run_command(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int dispatch(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int select_reply(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int seek_reply(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int trailer_reply(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int nested_stream(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int finish_now(struct ghci_device *device, struct ghci_vk_build *build, struct drv_gpu_completion *completion);
static int finish_queued(struct ghci_device *device, struct ghci_vk_build *build, struct drv_gpu_completion *completion, int wait_fence);
static int stage_frame(struct ghci_device *device, struct ghci_vk_build *build, struct ghci_vk_frame *frame);
static void after_submit(struct ghci_device *device, struct ghci_vk_build *build, const struct ghci_ticket *ticket);
static void build_free(struct ghci_vk_build *build);
static int grow(void **items, unsigned *capacity, unsigned count, size_t element);
static void store_word(uint8_t *bytes, uint32_t offset, uint32_t value);

/*
 * Decodes, stages and submits one frame of commands of a session.
 */
int
ghci_vk_submit(
	struct ghci_device *device,
	struct ghci_session *owner,
	const void *wire,
	uint32_t bytes,
	uint32_t flags,
	struct drv_gpu_completion *completion)
{
	struct ghci_vk_session *session;
	struct ghci_vk_build build;
	struct i915_wire_reader reader;
	int faulted;
	int error;

	/* Only the context's fence flag, and whole words within the bound. */
	if ((flags & ~GPU_COMMAND_CONTEXT_FENCE) != 0U)
		return EINVAL;
	if ((bytes & 3U) != 0U)
		return EINVAL;
	if (bytes > GPU_COMMAND_MAX)
		return EINVAL;
	if (bytes != 0U && wire == NULL)
		return EINVAL;

	/* An empty frame is only a fence after the session's work. */
	if (bytes == 0U && flags == 0U)
		return EINVAL;

	/* A device the core faulted, or a session whose firmware objects a reset took, takes nothing. */
	session = owner->vk;
	faulted = ghci_vk_engine_faulted(device);
	if (faulted)
		return EIO;
	if (session == NULL)
		return ENODEV;
	if (session->lost)
		return EIO;

	/* No reply region yet; the copy budget covers the whole frame. */
	kern_memset(&build, 0, sizeof(build));
	build.session = session;
	build.copy_budget = GHCI_VK_STREAM_COPY_BYTES;

	/* An empty frame completes once the session's work before it has. */
	if (bytes == 0U) {
		error = finish_queued(device, &build, completion, 1);
		build_free(&build);
		if (error != 0)
			return error;

		/* Succeeded: the fence follows the session's work. */
		return 0;
	}

	/* Decodes the frame. */
	kern_memset(&reader, 0, sizeof(reader));
	reader.base = wire;
	reader.size = bytes;
	error = run_stream(&build, &reader);
	if (error != 0) {
		kern_logf("ghci: %s: vk: frame refused at command %u (%d)\n", device->name, (unsigned)session->last_opcode, error);
		build_free(&build);
		return error;
	}

	/* Nothing for the firmware, nothing to wait for and nothing of the session ahead: the frame is done now. */
	if (build.records == 0U && session->pending == 0U && !build.wait_fence) {
		error = finish_now(device, &build, completion);
		build_free(&build);
		if (error != 0)
			return error;

		/* Succeeded: the replies and the trailer are written. */
		return 0;
	}

	/* Otherwise the frame goes in flight. */
	error = finish_queued(device, &build, completion, build.wait_fence);
	build_free(&build);
	if (error != 0)
		return error;

	/* Succeeded: the frame is in the ring, or waits for the frames ahead of it. */
	return 0;
}

/*
 * Waits, a bounded time, for every frame of a session to complete.
 */
void
ghci_vk_drain(
	struct ghci_device *device,
	struct ghci_session *owner)
{
	struct ghci_ticket ticket;
	unsigned waited;
	unsigned pending;
	int outstanding;
	int done;

	/* Looks every millisecond, letting the service thread complete the frames between looks. */
	for (waited = 0U; waited < DRAIN_WAIT_MS; waited++) {
		mutex_lock(&device->lock);

		pending = 0U;
		if (owner->vk != NULL)
			pending = owner->vk->pending;
		outstanding = owner->outstanding;
		ticket.fence = owner->last_fence;
		ticket.generation = owner->last_generation;

		mutex_unlock(&device->lock);

		/* The session's work is done when no frame is in flight and its last fence passed. */
		done = 1;
		if (outstanding)
			done = ghci_cp_done(device, ticket.fence, ticket.generation);
		if (pending == 0U && done)
			return;

		ghci_service_wake(device, GHCI_WORK_REPLY);
		ghci_sleep_ms(1U);
	}

	/* A session whose work never finished is reported; its memory waits on the deferred list. */
	kern_logf("ghci: %s: vk: session %u still busy at its close\n", device->name, (unsigned)owner->tag);
}

/*
 * Appends a record to the frame's stage.
 */
int
ghci_vk_stage(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	const void *arguments,
	uint32_t bytes,
	uint32_t *index)
{
	uint8_t *grown;
	uint32_t need;
	uint32_t capacity;

	/* Whole words, inside what one frame stages. */
	if ((bytes & 3U) != 0U)
		return EINVAL;
	need = RECORD_HEAD_BYTES + bytes;
	if (bytes > GHCI_VK_FRAME_BYTES_MAX)
		return E2BIG;
	if (need > GHCI_VK_FRAME_BYTES_MAX - build->stage_bytes)
		return E2BIG;
	if (build->records >= GHCI_VK_FRAME_RECORDS_MAX)
		return E2BIG;

	/* A larger buffer when the record does not fit. */
	if (need > build->stage_capacity - build->stage_bytes) {
		capacity = build->stage_capacity;
		if (capacity == 0U)
			capacity = STAGE_FIRST_BYTES;
		while (need > capacity - build->stage_bytes)
			capacity *= 2U;

		/* The new buffer holds what the old one held. */
		grown = kern_malloc(capacity);
		if (grown == NULL)
			return ENOMEM;
		if (build->stage != NULL) {
			kern_memcpy(grown, build->stage, build->stage_bytes);
			kern_free(build->stage);
		}
		build->stage = grown;
		build->stage_capacity = capacity;
	}

	/* The record's head: the number, the flags and its result index, which is its place in the frame. */
	store_word(build->stage, build->stage_bytes, opcode);
	store_word(build->stage, build->stage_bytes + 4U, flags);
	store_word(build->stage, build->stage_bytes + 8U, build->records);
	build->stage_bytes += RECORD_HEAD_BYTES;

	/* The arguments in the wire form they came in. */
	if (bytes != 0U)
		kern_memcpy(build->stage + build->stage_bytes, arguments, bytes);
	build->stage_bytes += bytes;

	/* Succeeded: the record's index names its result. */
	*index = build->records;
	build->records++;
	return 0;
}

/*
 * Relays the span of the reader from start as one record.
 */
int
ghci_vk_relay(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	size_t start,
	uint32_t *index)
{
	int error;

	/* A command that did not decode is not relayed. */
	if (reader->error != 0)
		return EINVAL;

	/* The arguments as they came, from start to where the decode stopped. */
	error = ghci_vk_stage(build, opcode, flags, reader->base + start, (uint32_t)(reader->offset - start), index);
	if (error != 0)
		return error;

	/* Succeeded: the command is staged. */
	return 0;
}

/*
 * Writes a VkResult word to the reply that the record's result fills in later.
 */
int
ghci_vk_reply_result(
	struct ghci_vk_build *build,
	uint32_t index)
{
	struct ghci_vk_patch *patch;
	uint32_t offset;
	int error;

	/* The word's place in the blob, and the word, pending until the result comes. */
	offset = (uint32_t)(build->reply.base - build->reply_blob->view) + (uint32_t)build->reply.offset;
	drv_i915_wire_reply_u32(&build->reply, GHCI_VK_RESULT_PENDING);
	if (build->reply.error != 0)
		return EINVAL;

	/* The patch that fills it. */
	error = ghci_vk_patch_add(build, &patch);
	if (error != 0)
		return error;
	patch->kind = GHCI_VK_PATCH_RESULT;
	patch->index = index;
	patch->offset = offset;

	/* Succeeded: the word waits for the record's result. */
	return 0;
}

/*
 * Asks for a record's aux word to be kept as the address of an event or a query pool.
 */
int
ghci_vk_patch_address(
	struct ghci_vk_build *build,
	enum ghci_vk_patch_kind kind,
	uint32_t index,
	uint64_t identity)
{
	struct ghci_vk_patch *patch;
	int error;

	/* A new patch. */
	error = ghci_vk_patch_add(build, &patch);
	if (error != 0)
		return error;

	/* It keeps the address. */
	patch->kind = kind;
	patch->index = index;
	patch->identity = identity;

	/* Succeeded: the address is kept when the result comes. */
	return 0;
}

/*
 * Adds a patch to the frame.
 */
int
ghci_vk_patch_add(
	struct ghci_vk_build *build,
	struct ghci_vk_patch **patch)
{
	int error;

	/* Room for one more patch. */
	error = grow((void **)&build->patches, &build->patch_capacity, build->patch_count, sizeof(*build->patches));
	if (error != 0)
		return error;

	/* The new patch, cleared. */
	*patch = &build->patches[build->patch_count];
	kern_memset(*patch, 0, sizeof(**patch));
	build->patch_count++;

	/* Succeeded: the caller fills it. */
	return 0;
}

/*
 * Gives an allocation back once the frame is in the ring.
 */
int
ghci_vk_release_later(
	struct ghci_vk_build *build,
	struct ghci_allocation *allocation)
{
	int error;

	/* Room for one more. */
	error = grow((void **)&build->releases, &build->release_capacity, build->release_count, sizeof(*build->releases));
	if (error != 0)
		return error;

	/* Kept until the frame's fence is the session's last. */
	build->releases[build->release_count] = allocation;
	build->release_count++;

	/* Succeeded: it goes back after the frame. */
	return 0;
}

/*
 * Binds a VkFence to the fence that follows the frame.
 */
int
ghci_vk_fence_later(
	struct ghci_vk_build *build,
	uint64_t identity)
{
	int error;

	/* Room for one more. */
	error = grow((void **)&build->fences, &build->fence_capacity, build->fence_count, sizeof(*build->fences));
	if (error != 0)
		return error;

	/* Bound once the frame's ticket is known. */
	build->fences[build->fence_count] = identity;
	build->fence_count++;

	/* Succeeded: the fence signals with the frame's work. */
	return 0;
}

/* Runs each command of a stream; the scratch is emptied before each. */
static int
run_stream(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	int error;

	/* One command at a time to the stream's end. */
	while (reader->offset < reader->size) {
		build->session->arena.used = 0U;
		error = run_command(build, reader);
		if (error != 0)
			return error;

		/* A command that ran past its end or past its reply fails the frame. */
		if (reader->error != 0)
			return EINVAL;
		if (build->reply.error != 0)
			return EINVAL;
	}

	/* Succeeded: the stream is decoded. */
	return 0;
}

/* Reads one command's header and runs its body. */
static int
run_command(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	uint32_t opcode;
	uint32_t flags;
	int error;

	/* The number and whether a reply is wanted. */
	opcode = drv_i915_wire_read_u32(reader);
	flags = drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;
	if (flags > GPU_OP_FLAG_REPLY)
		return EINVAL;
	build->session->last_opcode = opcode;

	/* The transport's own commands never reply (the trailer does). */
	if (flags != 0U &&
	    (opcode == GPU_OP_SET_REPLY_STREAM ||
	     opcode == GPU_OP_SEEK_REPLY_STREAM ||
	     opcode == GPU_OP_EXECUTE_STREAMS))
		return EINVAL;

	/* The driver's own gpu-ops never come from userland this way. */
	if (opcode >= GPU_OP_GHCI_SESSION_OPEN)
		return EINVAL;

	/* A reply starts with the echoed number in the selected region. */
	if (flags != 0U) {
		if (build->reply_blob == NULL)
			return EINVAL;
		drv_i915_wire_reply_u32(&build->reply, opcode);
		if (build->reply.error != 0)
			return EINVAL;
	}

	/* The transport's commands, then the routes. */
	switch (opcode) {
	case GPU_OP_SET_REPLY_STREAM:
		error = select_reply(build, reader);
		break;
	case GPU_OP_SEEK_REPLY_STREAM:
		error = seek_reply(build, reader);
		break;
	case GPU_OP_ENUMERATE_INSTANCE_VERSION:
		error = trailer_reply(build, reader);
		break;
	case GPU_OP_EXECUTE_STREAMS:
		error = nested_stream(build, reader);
		break;
	default:
		error = dispatch(build, opcode, flags, reader);
		break;
	}

	/* Reports a command that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/* Hands a command to the route that owns its number; ENOTSUP when none does. */
static int
dispatch(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	unsigned index;
	int handled;
	int error;

	/* Each route in turn, until one takes the number. */
	for (index = 0U; index < sizeof(routes) / sizeof(routes[0]); index++) {
		handled = 0;
		error = routes[index](build, opcode, flags, reader, &handled);
		if (!handled)
			continue;

		/* Reports the route's refusal. */
		if (error != 0)
			return error;

		/* Succeeded: the route ran the command. */
		return 0;
	}

	/* A number no route owns: the stream stops here. */
	return ENOTSUP;
}

/*
 * Selects the reply region: [u64 1][u32 resource id][u64 offset]
 * [u64 capacity], in a mappable blob of the session's.  A frame keeps one
 * blob for its replies: the blob it chose first.
 */
static int
select_reply(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	struct ghci_allocation *blob;
	uint64_t present;
	uint64_t offset;
	uint64_t capacity;
	uint32_t identifier;

	/* The region. */
	present = drv_i915_wire_read_u64(reader);
	identifier = drv_i915_wire_read_u32(reader);
	offset = drv_i915_wire_read_u64(reader);
	capacity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (present != 1U)
		return EINVAL;

	/* A blob of the session's the processor reaches. */
	blob = ghci_allocation_blob(build->session->owner, identifier);
	if (blob == NULL)
		return EINVAL;
	if (blob->view == NULL)
		return EINVAL;

	/* Refuses a second blob once replies of the first wait for results. */
	if (build->reply_blob != NULL &&
	    build->reply_blob != blob &&
	    build->patch_count != 0U)
		return EINVAL;

	/* The whole region inside the blob, word aligned, room for the trailer at least. */
	if ((offset & 3U) != 0U)
		return EINVAL;
	if (offset > blob->requested)
		return EINVAL;
	if (capacity > blob->requested - offset)
		return EINVAL;
	if (capacity < REPLY_MIN)
		return EINVAL;

	/* Replies go there from now on. */
	build->reply_blob = blob;
	build->reply.base = blob->view + (size_t)offset;
	build->reply.size = (size_t)capacity;
	build->reply.offset = 0U;
	build->reply.error = 0;

	/* Succeeded: the region is selected. */
	return 0;
}

/* Moves the reply cursor within the region: [u64 position]. */
static int
seek_reply(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	uint64_t position;

	/* A word-aligned place inside the selected region. */
	position = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (build->reply_blob == NULL)
		return EINVAL;
	if (position > build->reply.size)
		return EINVAL;
	if ((position & 3U) != 0U)
		return EINVAL;

	/* The next reply starts there. */
	build->reply.offset = (size_t)position;

	/* Succeeded: the cursor moved. */
	return 0;
}

/*
 * Takes the trailer, [u64 1] with a reply: its place is kept, and it is
 * written once every result of the frame is in.
 */
static int
trailer_reply(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	uint64_t present;

	/* The version's output pointer, with the reply the transport echoed. */
	present = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (present != 1U)
		return EINVAL;
	if (build->reply_blob == NULL)
		return EINVAL;

	/* Room for the result, the presence word and the version after the echo. */
	if ((build->reply.offset & 3U) != 0U)
		return EINVAL;
	if (build->reply.size - build->reply.offset < 16U)
		return EINVAL;

	/* The trailer's place in the blob; the cursor passes it. */
	build->trailer = 1;
	build->trailer_offset = (uint32_t)(build->reply.base - build->reply_blob->view) + (uint32_t)build->reply.offset;
	build->reply.offset += 16U;

	/* Succeeded: the trailer is written at the frame's end. */
	return 0;
}

/*
 * Runs a command stream carried in a blob: [u32 1][u64 1][u32 resource]
 * [u64 offset][u64 bytes][u64 reply offsets 0][u32 dependencies 0]
 * [u64 0][u32 flags 0], copied first so the client cannot change it while
 * it is decoded.
 */
static int
nested_stream(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	struct i915_wire_reader nested;
	struct ghci_allocation *blob;
	uint8_t *copy;
	uint64_t present;
	uint64_t offset;
	uint64_t size;
	uint64_t reply_offsets;
	uint64_t dependency_pointer;
	uint32_t count;
	uint32_t identifier;
	uint32_t dependencies;
	uint32_t stream_flags;
	int error;

	/* The wrapper. */
	count = drv_i915_wire_read_u32(reader);
	present = drv_i915_wire_read_u64(reader);
	identifier = drv_i915_wire_read_u32(reader);
	offset = drv_i915_wire_read_u64(reader);
	size = drv_i915_wire_read_u64(reader);
	reply_offsets = drv_i915_wire_read_u64(reader);
	dependencies = drv_i915_wire_read_u32(reader);
	dependency_pointer = drv_i915_wire_read_u64(reader);
	stream_flags = drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* One stream, without reply offsets, dependencies or flags. */
	if (count != 1U || present != 1U)
		return ENOTSUP;
	if (reply_offsets != 0U ||
	    dependencies != 0U ||
	    dependency_pointer != 0U ||
	    stream_flags != 0U)
		return ENOTSUP;

	/* Within the copy budget and the depth, in whole words. */
	if (size == 0U || size > build->copy_budget)
		return E2BIG;
	if ((size & 3U) != 0U)
		return EINVAL;
	if (build->depth == GHCI_VK_STREAM_DEPTH)
		return E2BIG;

	/* A blob of the session's the processor reaches, which holds the stream. */
	blob = ghci_allocation_blob(build->session->owner, identifier);
	if (blob == NULL)
		return EINVAL;
	if (blob->view == NULL)
		return EINVAL;
	if (offset > blob->requested)
		return EINVAL;
	if (size > blob->requested - offset)
		return EINVAL;

	/* The copy. */
	copy = kern_malloc((size_t)size);
	if (copy == NULL)
		return ENOMEM;
	kern_memcpy(copy, blob->view + (size_t)offset, (size_t)size);
	build->copy_budget -= size;
	build->depth++;

	/* Decodes the copy; the commands of the stream reply as usual. */
	kern_memset(&nested, 0, sizeof(nested));
	nested.base = copy;
	nested.size = (size_t)size;
	error = run_stream(build, &nested);
	build->depth--;
	kern_free(copy);
	if (error != 0)
		return error;

	/* Succeeded: the stream is decoded. */
	return 0;
}

/*
 * Finishes a frame that relays nothing and has nothing of its session
 * ahead of it: its trailer is written now and its callback completes.
 */
static int
finish_now(
	struct ghci_device *device,
	struct ghci_vk_build *build,
	struct drv_gpu_completion *completion)
{
	uint8_t *view;
	uint32_t *version;
	unsigned index;

	/* The allocations the frame gave back go now, or after the session's last fence. */
	for (index = 0U; index < build->release_count; index++)
		ghci_allocation_drop(device, build->session->owner, build->releases[index]);

	/* The trailer: [VkResult 0][u64 1], then the version, released last. */
	if (build->trailer) {
		view = build->reply_blob->view;
		store_word(view, build->trailer_offset, 0U);
		store_word(view, build->trailer_offset + 4U, 1U);
		store_word(view, build->trailer_offset + 8U, 0U);
		version = (uint32_t *)(void *)(view + build->trailer_offset + 12U);
		__atomic_store_n(version, (1U << 22) | (1U << 12), __ATOMIC_RELEASE);
	}

	/* The callback says the replies are complete. */
	drv_gpu_complete(completion, 0);

	/* Succeeded: the frame is done. */
	return 0;
}

/*
 * Puts a frame in flight: its records go to the staging ring and a
 * GHCI_FRAME packet to the ring, or, without records, it waits for the
 * frames ahead of it (and, with wait_fence, for the session's last fence).
 */
static int
finish_queued(
	struct ghci_device *device,
	struct ghci_vk_build *build,
	struct drv_gpu_completion *completion,
	int wait_fence)
{
	struct ghci_vk_frame *frame;
	struct ghci_session *owner;
	int error;

	/* The frame's record. */
	frame = kern_calloc(1U, sizeof(*frame));
	if (frame == NULL)
		return ENOMEM;

	/* What completes it: its callback, its trailer and its patches, which it takes over. */
	owner = build->session->owner;
	frame->session = build->session;
	frame->completion = completion;
	frame->trailer = build->trailer;
	frame->trailer_offset = build->trailer_offset;
	frame->records = build->records;
	frame->patches = build->patches;
	frame->patch_count = build->patch_count;
	build->patches = NULL;
	build->patch_count = 0U;

	/* An empty frame of a context fence waits for the session's last fence. */
	if (wait_fence && owner->outstanding) {
		frame->wait_fence = 1;
		frame->ticket.fence = owner->last_fence;
		frame->ticket.generation = owner->last_generation;
	}

	/* The records go to the GPU. */
	if (build->records != 0U) {
		error = stage_frame(device, build, frame);
		if (error != 0) {
			kern_free(frame->patches);
			kern_free(frame);
			return error;
		}
	}

	/* The reply blob stays while the frame writes into it. */
	frame->reply_blob = build->reply_blob;
	if (frame->reply_blob != NULL)
		ghci_memory_hold(frame->reply_blob);

	/* The frame is in flight; the service thread completes it. */
	ghci_vk_frame_queue(device, frame);
	ghci_service_wake(device, GHCI_WORK_REPLY);

	/* Succeeded: the frame completes later. */
	return 0;
}

/*
 * Copies a frame's records into the staging ring after its result page,
 * puts the GHCI_FRAME packet in the ring and does what follows the
 * submission.
 */
static int
stage_frame(
	struct ghci_device *device,
	struct ghci_vk_build *build,
	struct ghci_vk_frame *frame)
{
	struct ghci_vk_engine *engine;
	struct ghci_ticket ticket;
	uint32_t packet[FRAME_PACKET_DWORDS];
	volatile uint32_t *page;
	uint8_t *staging;
	uint32_t page_bytes;
	uint32_t offset;
	uint32_t consumed;
	uint32_t page_address;
	uint32_t records_address;
	uint32_t skip;
	uint32_t head_before;
	uint32_t used_before;
	uint32_t index;
	unsigned waited;
	int error;

	/* The result page: a record per staged command and the frame's own, in whole lines. */
	engine = device->vk;
	page_bytes = (8U * (build->records + 1U) + 63U) & ~63U;

	/* Room in the staging ring, which the frames ahead of this one give back as they complete. */
	error = EAGAIN;
	for (waited = 0U; waited < STAGING_WAIT_MS; waited++) {
		error = ghci_vk_staging_take(device, page_bytes + build->stage_bytes, &offset, &consumed);
		if (error != EAGAIN)
			break;

		/* The service thread needs the lock to complete them. */
		mutex_unlock(&device->lock);
		ghci_service_wake(device, GHCI_WORK_REPLY);
		ghci_sleep_ms(1U);
		mutex_lock(&device->lock);
	}
	if (error == EAGAIN)
		return EBUSY;
	if (error != 0)
		return error;

	/*
	 * What the ring was before the run, should the submission fail: the
	 * run started at the head, or at 0 after skipping the ring's end.
	 */
	skip = consumed - (((page_bytes + build->stage_bytes) + 63U) & ~63U);
	head_before = offset;
	if (skip != 0U)
		head_before = GHCI_VK_STAGING_BYTES - skip;
	used_before = engine->used - consumed;

	/* Every result starts pending, so the service sees which the firmware wrote. */
	staging = engine->staging->view + offset;
	page = (volatile uint32_t *)(void *)staging;
	for (index = 0U; index <= build->records; index++) {
		page[2U * index] = GHCI_VK_RESULT_PENDING;
		page[2U * index + 1U] = 0U;
	}

	/* The records after the page. */
	kern_memcpy(staging + page_bytes, build->stage, build->stage_bytes);
	kern_io_write_barrier();

	/*
	 * GHCI_FRAME: the frame's own result after the records' results, the
	 * staging address, the frame's bytes, the result page and the record
	 * count.
	 */
	page_address = engine->staging->gpu_address + offset;
	records_address = page_address + page_bytes;
	packet[0] = GHCI_PKT_V(build->session->owner->tag, GHCI_FRAME_DWORDS);
	packet[1] = GPU_OP_GHCI_FRAME;
	packet[2] = GHCI_V_RESULT;
	packet[3] = page_address + 8U * build->records;
	packet[4] = records_address;
	packet[5] = 0U;
	packet[6] = build->stage_bytes;
	packet[7] = page_address;
	packet[8] = build->records;

	/* Into the ring, with the device's fence after it. */
	error = ghci_cp_submit(device, build->session->owner, packet, FRAME_PACKET_DWORDS, &ticket);
	if (error != 0) {
		engine->head = head_before;
		engine->used = used_before;
		return error;
	}

	/* The frame waits for its results there. */
	frame->page = page;
	frame->staging_bytes = consumed;
	after_submit(device, build, &ticket);

	/* Succeeded: the frame is in the ring. */
	return 0;
}

/*
 * Does what follows a frame's submission: the VkFences its submissions
 * signal are bound to the fence after it, and the allocations it gave
 * back wait for that fence.
 */
static void
after_submit(
	struct ghci_device *device,
	struct ghci_vk_build *build,
	const struct ghci_ticket *ticket)
{
	struct ghci_vk_object *object;
	unsigned index;

	/* Each fence signals when the frame's work has finished. */
	for (index = 0U; index < build->fence_count; index++) {
		object = ghci_vk_object_find(build->session, GHCI_VK_FENCE, build->fences[index]);
		if (object == NULL)
			continue;

		object->as.fence.signaled = 0;
		object->as.fence.pending = 1;
		object->as.fence.ticket = *ticket;
	}

	/* Each allocation goes back after the session's last fence, which is now the frame's. */
	for (index = 0U; index < build->release_count; index++)
		ghci_allocation_drop(device, build->session->owner, build->releases[index]);
	build->release_count = 0U;
}

/* Frees what a build holds that no frame took over. */
static void
build_free(
	struct ghci_vk_build *build)
{
	/* The stage, the patches, the releases and the fences. */
	if (build->stage != NULL)
		kern_free(build->stage);
	if (build->patches != NULL)
		kern_free(build->patches);
	if (build->releases != NULL)
		kern_free(build->releases);
	if (build->fences != NULL)
		kern_free(build->fences);
}

/* Makes room for one more element of a list that doubles as it grows. */
static int
grow(
	void **items,
	unsigned *capacity,
	unsigned count,
	size_t element)
{
	void *grown;
	unsigned larger;

	/* A list with room is left as it is. */
	if (count < *capacity)
		return 0;

	/* Twice the room, or the first room. */
	larger = *capacity * 2U;
	if (larger == 0U)
		larger = LIST_FIRST;

	/* The new list holds what the old one held. */
	grown = kern_malloc((size_t)larger * element);
	if (grown == NULL)
		return ENOMEM;
	if (*items != NULL) {
		kern_memcpy(grown, *items, (size_t)count * element);
		kern_free(*items);
	}

	/* Succeeded: the list has room. */
	*items = grown;
	*capacity = larger;
	return 0;
}

/* Stores a 32-bit word into bytes at an offset, the lowest byte first. */
static void
store_word(
	uint8_t *bytes,
	uint32_t offset,
	uint32_t value)
{
	/* One byte at a time, whatever the alignment. */
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
	bytes[offset + 2U] = (uint8_t)(value >> 16);
	bytes[offset + 3U] = (uint8_t)(value >> 24);
}
