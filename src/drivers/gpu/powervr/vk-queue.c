/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's submissions, fences and semaphores, and the work it
 * hands the core.
 *
 * vkQueueSubmit builds one compute control stream for all its command
 * buffers: for each workgroup of each dispatch a kernel whose shading
 * cluster program the driver builds from the pipeline's shader
 * (compute-program.c), and after each dispatch a fence kernel, so the
 * dispatches run in order.  One compute command on the session's
 * firmware context runs the stream; the submission waits, bounded, for
 * the session's timeline before it answers, so its fence is signaled
 * then, and the marker libvulkan reserved for it completes after it.  A
 * semaphore orders nothing more than the one queue already does.
 *
 * The executor's own memory (the programs and the stream) is made at the
 * first submission and reused by each: the earlier one has finished.
 * When a submission does not finish in time, the core may still read and
 * write that memory, so it stays with the core (abandoned).
 *
 * Procedure referenced: zedBSD src/drivers/gpu/powervr/compute-check.c
 * (the places of the programs, the kick program per kernel, the fence
 * kernel and the end of the stream, the compute command and its wait)
 * and src/drivers/gpu/bcm2711/vulkan-queue.c and vulkan-sync.c (the wire
 * of each command).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/compute-program.h"
#include "drivers/gpu/powervr/vk.h"

/* The most submits of one vkQueueSubmit, and the most command buffers and semaphores of one submit. */
#define QUEUE_SUBMITS_MAX		8U
#define QUEUE_ITEMS_MAX			32U

/* The most kernels (workgroups) one submission runs. */
#define QUEUE_KERNELS_MAX		64U

/*
 * The executor's memory: the shading cluster programs (one 512-byte room
 * per kernel) and the data sequencer programs (one 64-byte slot per
 * program, data then code; slot 0 the fence's), each at its place in its
 * heap, and the stream buffer (the resume area first, then the stream).
 */
#define QUEUE_USC_OFFSET		0x10000U
#define QUEUE_USC_ROOM			0x400U
#define QUEUE_USC_BYTES			(QUEUE_USC_ROOM * QUEUE_KERNELS_MAX)
#define QUEUE_PDS_OFFSET		0x10000U
#define QUEUE_PDS_SLOT			0x40U
#define QUEUE_PDS_CODE			0x10U
#define QUEUE_PDS_BYTES			(QUEUE_PDS_SLOT * (QUEUE_KERNELS_MAX + 1U))
#define QUEUE_STREAM_BYTES		0x10000U
#define QUEUE_STREAM_FIRST		0x1000U

_Static_assert(QUEUE_USC_OFFSET == POWERVR_PROGRAM_HEAP_START && QUEUE_USC_BYTES <= POWERVR_EXECUTOR_USC_BYTES, "the executor's programs are in the place the session holds for them");
_Static_assert(QUEUE_PDS_OFFSET == POWERVR_PROGRAM_HEAP_START && QUEUE_PDS_BYTES <= POWERVR_EXECUTOR_PDS_BYTES, "the executor's programs are in the place the session holds for them");

/* The data sequencer temporaries a compute kernel's inputs take, and the fence kernel's task size. */
#define QUEUE_PDS_INPUTS		3U
#define QUEUE_FENCE_INSTANCES		8U

/* How long a submission waits for the core. */
#define QUEUE_WAIT_MS			2000U

/* Which of the executor's buffers are mapped at their fixed places. */
#define MAPPED_USC			0x1U
#define MAPPED_PDS			0x2U

/*
 * The executor's memory on the core, made at the first submission: the
 * program buffers mapped at their places, and the stream's resource.
 */
struct powervr_vk_executor {
	struct powervr_buffer *usc;
	struct powervr_buffer *pds;
	struct powervr_resource *stream;
	unsigned mapped;
};

/* One submission being built: where the stream is written and how many kernels it has. */
struct submission {
	struct powervr_vk_session *session;
	struct powervr_vk_executor *executor;
	uint32_t stream_at;
	uint32_t kernels;
	uint32_t dispatches;
	const struct powervr_vk_pipeline *pipeline;
	const struct powervr_vk_descriptor_set *sets[POWERVR_VK_SETS_MAX];
};

static int submit(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int read_submit(struct i915_wire_reader *reader, uint64_t *commands, uint32_t *command_count);
static int read_identities(struct i915_wire_reader *reader, bool stages);
static uint32_t submit_result(int error);
static int create_sync(struct powervr_vk_session *session, enum i915_vk_object_kind kind, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int reset_fences(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int fence_status(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int executor_make(struct powervr_vk_session *session);
static int map_program_buffer(struct powervr_session *owner, uint64_t bytes, uint64_t address, struct powervr_buffer **result);
static int build_command_buffer(struct submission *build, const struct powervr_vk_command_buffer *command);
static int build_dispatch(struct submission *build, uint32_t groups);
static int build_shared(struct submission *build, const struct powervr_shader_program *program, uint32_t groups, uint32_t *values);
static int build_kernel(struct submission *build, const uint8_t *code, size_t length, uint32_t temps, uint32_t instances);
static int build_fence(struct submission *build);
static void put_stream_words(struct submission *build, const uint32_t *words, unsigned count);
static void put_word(struct powervr_buffer *buffer, uint32_t offset, uint32_t word);
static int run(struct submission *build);

/*
 * Runs a submission, fence or semaphore command; *handled stays clear for
 * another handler's opcode.
 */
int
powervr_vk_queue_route(
	struct powervr_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another handler. */
	(void)requested;
	*handled = 1;
	switch (opcode) {
	case GPU_OP_QUEUE_SUBMIT:
		error = submit(session, reader, reply);
		break;
	case GPU_OP_CREATE_FENCE:
		error = create_sync(session, I915_VK_OBJ_FENCE, reader, reply);
		break;
	case GPU_OP_DESTROY_FENCE:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_FENCE);
		break;
	case GPU_OP_RESET_FENCES:
		error = reset_fences(session, reader, reply);
		break;
	case GPU_OP_GET_FENCE_STATUS:
		error = fence_status(session, reader, reply);
		break;
	case GPU_OP_CREATE_SEMAPHORE:
		error = create_sync(session, I915_VK_OBJ_SEMAPHORE, reader, reply);
		break;
	case GPU_OP_DESTROY_SEMAPHORE:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_SEMAPHORE);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/*
 * Returns the executor's memory: unmapped and released, unless a
 * submission was abandoned or the core may still reach the session, when
 * it stays with the core until the device is reset.
 */
void
powervr_vk_executor_free(
	struct powervr_vk_session *session)
{
	struct powervr_vk_executor *executor;
	struct powervr_render *render;
	struct powervr_session *owner;
	unsigned long enabled;
	unsigned keep;

	/* No executor memory was made. */
	executor = session->executor;
	if (executor == NULL)
		return;
	session->executor = NULL;
	owner = session->owner;
	render = owner->render;

	/* Whether the core may still reach the memory. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	keep = 0;
	if (render->faulted || owner->isolated)
		keep = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	if (session->abandoned)
		keep = 1;

	/* Left with the core: nothing of it is touched again. */
	if (keep) {
		kern_logf("powervr: vk: the executor's memory stays with the core\n");
		return;
	}

	/* Unmaps the program buffers and has the core drop the entries. */
	mutex_lock(&render->core->lock);

	if ((executor->mapped & MAPPED_USC) != 0)
		powervr_gpu_mmu_unmap(&owner->mmu, POWERVR_USC_HEAP_BASE + QUEUE_USC_OFFSET, executor->usc->run.size);
	if ((executor->mapped & MAPPED_PDS) != 0)
		powervr_gpu_mmu_unmap(&owner->mmu, POWERVR_PDS_HEAP_BASE + QUEUE_PDS_OFFSET, executor->pds->run.size);
	if (executor->mapped != 0)
		(void)powervr_command_mmu_flush(render->core);

	mutex_unlock(&render->core->lock);

	/* The buffers and the stream's resource. */
	if (executor->usc != NULL)
		powervr_buffer_put(render, executor->usc);
	if (executor->pds != NULL)
		powervr_buffer_put(render, executor->pds);
	if (executor->stream != NULL)
		powervr_resource_unmake(owner, executor->stream);
	kern_free(executor);
}

/*
 * vkQueueSubmit: [queue][count][count]{submit}[fence] -> [result].  Every
 * submit is read first; then all command buffers run in one stream, and
 * the fence is signaled once the core finished it.
 */
static int
submit(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint64_t commands[QUEUE_SUBMITS_MAX * QUEUE_ITEMS_MAX];
	struct powervr_vk_command_buffer *command;
	struct powervr_vk_fence *fence;
	struct submission build;
	uint32_t command_count;
	uint32_t taken;
	uint64_t submits;
	uint64_t index;
	uint64_t fence_id;
	uint32_t result;
	int error;

	/* The queue and how many submits follow. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	submits = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || submits > QUEUE_SUBMITS_MAX)
		return EINVAL;

	/* Each submit's command buffers, in order. */
	command_count = 0;
	for (index = 0; index < submits; index++) {
		error = read_submit(reader, commands + command_count, &taken);
		if (error != 0)
			return error;
		command_count += taken;
	}

	/* The fence. */
	fence_id = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The executor's memory, made at the first submission. */
	error = 0;
	if (session->abandoned)
		error = EIO;
	if (error == 0 && session->executor == NULL)
		error = executor_make(session);

	/* The stream of every command buffer's dispatches. */
	kern_memset(&build, 0, sizeof(build));
	build.session = session;
	build.executor = session->executor;
	build.stream_at = QUEUE_STREAM_FIRST;
	for (index = 0; error == 0 && index < command_count; index++) {
		command = powervr_vk_payload(session, I915_VK_OBJ_COMMAND_BUFFER, commands[index]);
		if (command == NULL || command->state != POWERVR_VK_COMMAND_EXECUTABLE) {
			error = EINVAL;
			break;
		}
		error = build_command_buffer(&build, command);
	}

	/* The core runs it, when it has any kernel. */
	if (error == 0 && build.kernels != 0)
		error = run(&build);
	if (error != 0)
		kern_logf("powervr: vk: a submission did not run (%d)\n", error);

	/* The work is done: the fence is signaled. */
	fence = powervr_vk_payload(session, I915_VK_OBJ_FENCE, fence_id);
	if (error == 0 && fence != NULL)
		fence->signaled = true;

	/* The submission's result. */
	result = submit_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * Reads one submit: [sType][pNext][wait count][count]{semaphore}[count]
 * {stage}[command count][count]{command buffer}[signal count][count]
 * {semaphore}; the command buffers' identities are kept.
 */
static int
read_submit(
	struct i915_wire_reader *reader,
	uint64_t *commands,
	uint32_t *command_count)
{
	uint64_t count;
	uint64_t index;
	int error;

	/* sType and pNext. */
	*command_count = 0;
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);

	/* The waits with their stages. */
	error = read_identities(reader, true);
	if (error != 0)
		return error;

	/* The command buffers. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
		return EINVAL;
	for (index = 0; index < count; index++)
		commands[index] = drv_i915_wire_read_u64(reader);
	*command_count = (uint32_t)count;

	/* The signals. */
	error = read_identities(reader, false);
	if (error != 0)
		return error;

	/* Succeeded: the submit is read. */
	return 0;
}

/* Reads a semaphore list [count][count]{identity}, and with stages its [count]{stage}. */
static int
read_identities(
	struct i915_wire_reader *reader,
	bool stages)
{
	uint64_t count;
	uint64_t index;

	/* The semaphores. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
		return EINVAL;
	for (index = 0; index < count; index++)
		(void)drv_i915_wire_read_u64(reader);

	/* Their stages. */
	if (stages) {
		count = drv_i915_wire_read_u64(reader);
		if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
			return EINVAL;
		for (index = 0; index < count; index++)
			(void)drv_i915_wire_read_u32(reader);
	}

	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the list is read. */
	return 0;
}

/* Reports a submission's VkResult: work the core did not finish loses the device. */
static uint32_t
submit_result(
	int error)
{
	uint32_t result;

	/* Work that did not finish. */
	if (error == EIO || error == ETIMEDOUT)
		return (uint32_t)VK_ERROR_DEVICE_LOST;

	/* Any other error as the executor maps it. */
	result = powervr_vk_result(error);

	/* Succeeded: the result. */
	return result;
}

/*
 * vkCreateFence and vkCreateSemaphore: [device][present][sType][pNext]
 * [flags] and the create's tail -> the create's reply.  A fence created
 * signaled starts signaled.
 */
static int
create_sync(
	struct powervr_vk_session *session,
	enum i915_vk_object_kind kind,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct powervr_vk_fence *fence;
	uint64_t identity;
	uint32_t flags;

	/* The device, the presence word, sType, pNext and the flags, then the tail. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	flags = drv_i915_wire_read_u32(reader);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A fence's state; a semaphore's record is nothing but itself. */
	fence = kern_calloc(1, sizeof(*fence));
	if (fence != NULL && kind == I915_VK_OBJ_FENCE && (flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0)
		fence->signaled = true;

	/* Publishes it and answers. */
	powervr_vk_create_reply(session, reply, kind, identity, fence, powervr_vk_free_payload, 0);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* vkResetFences: [device][count][count]{fence} -> [result]. */
static int
reset_fences(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct powervr_vk_fence *fence;
	uint64_t count;
	uint64_t index;
	uint64_t identity;

	/* How many fences follow. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_SUBMITS_MAX * QUEUE_ITEMS_MAX)
		return EINVAL;

	/* Each known fence is unsignaled. */
	for (index = 0; index < count; index++) {
		identity = drv_i915_wire_read_u64(reader);
		fence = powervr_vk_payload(session, I915_VK_OBJ_FENCE, identity);
		if (fence != NULL)
			fence->signaled = false;
	}

	if (reader->error != 0)
		return EINVAL;

	/* Success. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/* vkGetFenceStatus: [device][fence] -> [VK_SUCCESS or VK_NOT_READY]. */
static int
fence_status(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	const struct powervr_vk_fence *fence;
	uint64_t identity;
	uint32_t result;

	/* The fence behind the device. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Signaled, or not yet. */
	result = (uint32_t)VK_NOT_READY;
	fence = powervr_vk_payload(session, I915_VK_OBJ_FENCE, identity);
	if (fence != NULL && fence->signaled)
		result = (uint32_t)VK_SUCCESS;
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the status. */
	return 0;
}

/*
 * Makes the executor's memory: the program buffers mapped at their
 * places in the session's address space, the stream's resource, and the
 * fence program in its slot.
 */
static int
executor_make(
	struct powervr_vk_session *session)
{
	struct powervr_vk_executor *executor;
	struct powervr_render *render;
	struct powervr_buffer *stream;
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX];
	unsigned count;
	unsigned index;
	int error;

	/* The record. */
	render = session->owner->render;
	executor = kern_calloc(1, sizeof(*executor));
	if (executor == NULL)
		return ENOMEM;
	session->executor = executor;

	/* The shading cluster and the data sequencer programs, at their places in their heaps. */
	error = map_program_buffer(session->owner, QUEUE_USC_BYTES, POWERVR_USC_HEAP_BASE + QUEUE_USC_OFFSET, &executor->usc);
	if (error != 0)
		return error;
	executor->mapped |= MAPPED_USC;

	error = map_program_buffer(session->owner, QUEUE_PDS_BYTES, POWERVR_PDS_HEAP_BASE + QUEUE_PDS_OFFSET, &executor->pds);
	if (error != 0)
		return error;
	executor->mapped |= MAPPED_PDS;

	/* The stream's buffer, at an address of the session's heap; the resource takes the reference. */
	error = powervr_buffer_create(render, QUEUE_STREAM_BYTES, &stream);
	if (error != 0)
		return error;
	error = powervr_resource_make(session->owner, stream, 1, 0, &executor->stream);
	powervr_buffer_put(render, stream);
	if (error != 0)
		return error;

	/* Has the core drop stale table entries before it walks the program mappings. */
	mutex_lock(&render->core->lock);

	error = powervr_command_mmu_flush(render->core);

	mutex_unlock(&render->core->lock);
	if (error != 0)
		return error;

	/* The fence program, in slot 0. */
	error = powervr_compute_fence_program(data, code, &count);
	if (error != 0)
		return error;
	put_word(executor->pds, 0, data[0]);
	put_word(executor->pds, 4, data[1]);
	for (index = 0; index < count; index++)
		put_word(executor->pds, QUEUE_PDS_CODE + index * 4U, code[index]);

	/* Succeeded: submissions can build their streams. */
	return 0;
}

/* Takes a program buffer and maps it at its place, around the system level cache. */
static int
map_program_buffer(
	struct powervr_session *owner,
	uint64_t bytes,
	uint64_t address,
	struct powervr_buffer **result)
{
	struct powervr_buffer *buffer;
	unsigned flags;
	int error;

	/* A zeroed, contiguous buffer with its uncached view. */
	*result = NULL;
	error = powervr_buffer_create(owner->render, bytes, &buffer);
	if (error != 0)
		return error;

	/* The attributes of a buffer the processor writes. */
	flags = powervr_gpu_buffer_flags(1, 0);

	/* Maps it at its place. */
	mutex_lock(&owner->render->core->lock);

	error = powervr_gpu_mmu_map(&owner->mmu, address, buffer->run.paddr, buffer->run.size, flags);

	mutex_unlock(&owner->render->core->lock);

	/* Not mapped: the buffer goes back. */
	if (error != 0) {
		powervr_buffer_put(owner->render, buffer);
		return error;
	}

	/* Succeeded: the buffer is mapped. */
	*result = buffer;
	return 0;
}

/* Adds one command buffer's dispatches to the stream, with the pipeline and sets each one finds bound. */
static int
build_command_buffer(
	struct submission *build,
	const struct powervr_vk_command_buffer *command)
{
	const struct powervr_vk_record *entry;
	uint32_t index;
	uint32_t set;
	int error;

	/* Nothing is bound at a command buffer's start. */
	build->pipeline = NULL;
	for (set = 0; set < POWERVR_VK_SETS_MAX; set++)
		build->sets[set] = NULL;

	/* Each record in order. */
	for (index = 0; index < command->record_count; index++) {
		entry = &command->records[index];
		if (entry->opcode == GPU_OP_CMD_BIND_PIPELINE) {
			build->pipeline = entry->objects[0]->payload;
		} else if (entry->opcode == GPU_OP_CMD_BIND_DESCRIPTOR_SETS) {
			/* Object i is set first + i; a null set leaves its slot as it was. */
			for (set = 0; set < entry->object_count; set++) {
				if (entry->objects[set] != NULL)
					build->sets[entry->first + set] = entry->objects[set]->payload;
			}
		} else if (entry->opcode == GPU_OP_CMD_DISPATCH) {
			error = build_dispatch(build, entry->values[0]);
			if (error != 0)
				return error;
		}
	}

	/* Succeeded: the command buffer's dispatches are in the stream. */
	return 0;
}

/*
 * Adds one dispatch: a kernel per workgroup running the pipeline's
 * shader compiled for that workgroup, then a fence kernel.
 *
 * The executor has no data sequencer program that fills shared
 * registers yet, so each workgroup's program is compiled with its
 * shared register words (the bound buffers' addresses, the workgroup's
 * id and count) as constants.  A shader that reads past its buffers
 * faults on the core's MMU rather than being refused here.
 */
static int
build_dispatch(
	struct submission *build,
	uint32_t groups)
{
	struct powervr_shader_workspace *workspace;
	struct powervr_shader_program *program;
	struct powervr_ir_shader *shader;
	const struct powervr_vk_pipeline *pipeline;
	uint32_t values[POWERVR_SHADER_REQUESTS * 4U];
	uint8_t *code;
	uint32_t group;
	uint32_t index;
	int error;

	/* No workgroup, no work. */
	if (groups == 0)
		return 0;

	/* A bound pipeline, and room for its workgroups. */
	pipeline = build->pipeline;
	if (pipeline == NULL)
		return EINVAL;
	if (groups > QUEUE_KERNELS_MAX - build->kernels)
		return E2BIG;

	/* The compiler's working memory, the program's description and the code's room. */
	workspace = kern_malloc(powervr_shader_workspace_size());
	program = kern_malloc(sizeof(*program));
	code = kern_malloc(QUEUE_USC_ROOM);
	error = 0;
	if (workspace == NULL || program == NULL || code == NULL)
		error = ENOMEM;

	/* What the shader asks for, and a workgroup the task holds. */
	if (error == 0)
		error = powervr_shader_read(pipeline->words, pipeline->word_count, POWERVR_SHADER_COMPUTE, NULL, workspace, program, &shader);
	if (error == 0 && (program->workgroup[0] == 0 || program->workgroup[0] > POWERVR_COMPUTE_INSTANCES))
		error = ENOTSUP;
	if (error == 0 && program->shareds > POWERVR_SHADER_REQUESTS * 4U)
		error = ENOTSUP;
	if (error == 0)
		error = build_shared(build, program, groups, values);

	/* One kernel per workgroup, its id in the program. */
	for (group = 0; error == 0 && group < groups; group++) {
		for (index = 0; index < program->request_count; index++) {
			if (program->requests[index].kind == POWERVR_SHADER_REQUEST_WORKGROUP_ID)
				values[program->requests[index].shared] = group;
		}
		error = powervr_shader_compile_inline(pipeline->words, pipeline->word_count, POWERVR_SHADER_COMPUTE, NULL, values, program->shareds, workspace, program, code, QUEUE_USC_ROOM);
		if (error != 0) {
			kern_logf("powervr: vk: the compute shader did not compile (%d)\n", error);
			break;
		}
		if (group == 0)
			kern_logf("powervr: vk: dispatch of %u workgroups: the compute shader compiled to %u bytes, %u temps, %u shared words\n", groups, (unsigned)program->length, program->temps, program->shareds);
		error = build_kernel(build, code, program->length, program->temps, program->workgroup[0]);
	}

	/* The working memory goes. */
	kern_free(code);
	kern_free(program);
	kern_free(workspace);
	if (error != 0)
		return error;

	/* The fence: the next dispatch starts after this one's work. */
	error = build_fence(build);
	if (error != 0)
		return error;
	build->dispatches++;

	/* Succeeded: the dispatch is in the stream. */
	return 0;
}

/*
 * Works out the shared register words a program asks for: the bound
 * buffers' addresses, the workgroup count (and zero for the workgroup
 * id, which the caller sets per workgroup).
 */
static int
build_shared(
	struct submission *build,
	const struct powervr_shader_program *program,
	uint32_t groups,
	uint32_t *values)
{
	const struct powervr_shader_request *request;
	const struct powervr_vk_descriptor_set *set;
	const struct powervr_vk_descriptor_slot *slot;
	const struct powervr_vk_buffer *buffer;
	uint64_t address;
	uint32_t index;
	uint32_t word;
	int error;

	/* Each request's words. */
	for (index = 0; index < program->request_count; index++) {
		request = &program->requests[index];
		for (word = 0; word < request->words; word++)
			values[request->shared + word] = 0;

		switch (request->kind) {
		case POWERVR_SHADER_REQUEST_DESCRIPTOR:
			/* Only a buffer's address: the bound set's binding, its first byte. */
			if (request->part != POWERVR_SHADER_PART_BUFFER_ADDRESS)
				return ENOTSUP;
			if (request->set >= POWERVR_VK_SETS_MAX || request->binding >= POWERVR_VK_BINDINGS_MAX)
				return ENOTSUP;
			set = build->sets[request->set];
			if (set == NULL)
				return EINVAL;
			slot = &set->slots[request->binding];
			if (slot->buffer == NULL)
				return EINVAL;
			buffer = slot->buffer->payload;
			if (slot->offset > buffer->size)
				return EINVAL;
			error = powervr_vk_buffer_address(buffer, &address);
			if (error != 0)
				return error;
			address += slot->offset;
			values[request->shared] = (uint32_t)address;
			values[request->shared + 1U] = (uint32_t)(address >> 32);
			break;
		case POWERVR_SHADER_REQUEST_WORKGROUP_ID:
			/* Set per workgroup; one deep in y and z. */
			break;
		case POWERVR_SHADER_REQUEST_NUM_WORKGROUPS:
			values[request->shared] = groups;
			for (word = 1; word < request->words; word++)
				values[request->shared + word] = 1;
			break;
		default:
			/* Push constants and the rest wait for the data sequencer's shared loads. */
			return ENOTSUP;
		}
	}

	/* Succeeded: the words. */
	return 0;
}

/*
 * Adds one kernel: its shading cluster program in its room, the data
 * sequencer program that issues it in its slot, and its control stream
 * words.
 */
static int
build_kernel(
	struct submission *build,
	const uint8_t *code,
	size_t length,
	uint32_t temps,
	uint32_t instances)
{
	struct powervr_compute_kernel kernel;
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t pds[POWERVR_COMPUTE_PDS_CODE_MAX];
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS];
	uint32_t room;
	uint32_t slot;
	unsigned field;
	unsigned count;
	size_t index;
	int error;

	/* The program, written through the uncached view into the kernel's room. */
	if (length > QUEUE_USC_ROOM)
		return E2BIG;
	room = build->kernels * QUEUE_USC_ROOM;
	for (index = 0; index < length; index++)
		build->executor->usc->view[room + index] = code[index];

	/*
	 * The program that issues it, in the slot after the fence's.  The
	 * temporaries are counted in twos: if the core counts in fours, that
	 * only gives more than the program uses (plan/ws269 p008).
	 */
	error = powervr_compute_temporaries_field(temps, 2, &field);
	if (error != 0)
		return error;
	error = powervr_compute_kick_program(QUEUE_USC_OFFSET + room, field, data, pds, &count);
	if (error != 0)
		return error;
	slot = (build->kernels + 1U) * QUEUE_PDS_SLOT;
	put_word(build->executor->pds, slot, data[0]);
	put_word(build->executor->pds, slot + 4U, data[1]);
	for (index = 0; index < count; index++)
		put_word(build->executor->pds, slot + QUEUE_PDS_CODE + (uint32_t)index * 4U, pds[index]);

	/* The kernel: one workgroup of the given instances, on any cluster, no shared registers. */
	kern_memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = QUEUE_PDS_OFFSET + slot;
	kernel.code_offset = QUEUE_PDS_OFFSET + slot + QUEUE_PDS_CODE;
	kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	kernel.pds_temporaries = QUEUE_PDS_INPUTS;
	kernel.usc_temporaries = temps;
	kernel.instances = instances;
	kernel.max_instances = 0;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	if (error != 0)
		return error;
	put_stream_words(build, words, POWERVR_COMPUTE_KERNEL_WORDS);
	build->kernels++;

	/* Succeeded: the kernel is in the stream. */
	return 0;
}

/* Adds a fence kernel: it waits for the work before it and frees the shared registers. */
static int
build_fence(
	struct submission *build)
{
	struct powervr_compute_kernel kernel;
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS];
	int error;

	/* The fence program of slot 0. */
	kern_memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = QUEUE_PDS_OFFSET;
	kernel.code_offset = QUEUE_PDS_OFFSET + QUEUE_PDS_CODE;
	kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	kernel.instances = 1;
	kernel.max_instances = QUEUE_FENCE_INSTANCES;
	kernel.fence = 1;
	kernel.shared_from_pds = 1;
	kernel.common_shared = 1;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	if (error != 0)
		return error;
	put_stream_words(build, words, POWERVR_COMPUTE_KERNEL_WORDS);

	/* Succeeded: the fence is in the stream. */
	return 0;
}

/* Appends words to the stream through the stream buffer's uncached view. */
static void
put_stream_words(
	struct submission *build,
	const uint32_t *words,
	unsigned count)
{
	unsigned index;

	/* Each word at the stream's end. */
	for (index = 0; index < count; index++) {
		put_word(build->executor->stream->buffer, build->stream_at, words[index]);
		build->stream_at += 4U;
	}
}

/* Writes one little-endian word into a buffer through its uncached view. */
static void
put_word(
	struct powervr_buffer *buffer,
	uint32_t offset,
	uint32_t word)
{
	/* One aligned store through the view. */
	*(volatile uint32_t *)(buffer->view + offset) = word;
}

/*
 * Ends the stream and has the core run it: one compute command with the
 * session's next timeline value, then a bounded wait for it.  A wait that
 * runs out abandons the executor's memory to the core.
 */
static int
run(
	struct submission *build)
{
	struct powervr_session *owner;
	struct powervr_render *render;
	uint32_t command[POWERVR_COMPUTE_COMMAND_WORDS];
	uint32_t terminate;
	uint64_t stream;
	uint32_t value;
	int error;

	/* The end of the stream. */
	terminate = powervr_compute_terminate_word();
	put_stream_words(build, &terminate, 1);

	/* The command names the stream and the resume area before it. */
	owner = build->session->owner;
	render = owner->render;
	stream = build->executor->stream->address;
	powervr_compute_command(stream + QUEUE_STREAM_FIRST, stream, command);

	/* Sends it with the next timeline value. */
	mutex_lock(&render->core->lock);

	value = owner->submitted + 1U;
	error = powervr_session_kick_compute(render, owner, value, command, POWERVR_COMPUTE_COMMAND_WORDS);
	if (error == 0)
		owner->submitted = value;

	mutex_unlock(&render->core->lock);
	if (error != 0) {
		powervr_stage_fail(POWERVR_STAGE_COMPUTE, powervr_stage_reason_of(error));
		return error;
	}

	/* Waits for the timeline to reach it; the core may still run work that did not. */
	error = powervr_session_wait(owner, value, QUEUE_WAIT_MS);
	if (error != 0) {
		build->session->abandoned = true;
		owner->abandoned = 1;
		powervr_stage_fail(POWERVR_STAGE_COMPUTE, powervr_stage_reason_of(error));
		return error;
	}

	/* Succeeded: the core finished the stream; the first time is the compute stage's ok. */
	powervr_stage_ok(POWERVR_STAGE_COMPUTE);
	return 0;
}
