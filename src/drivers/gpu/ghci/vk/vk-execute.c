/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's conversion of recorded commands into GHCI packets.
 *
 * A submission walks each command buffer's recording (the commands as
 * they came on the wire, vk-command.c) and turns each command into the
 * packets of the hardware: binds are made lazily before a draw or a
 * dispatch (the pipeline, then the state its record does not keep, the
 * vertex and index buffers, and the hardware tables and constant bank
 * words of ghci_vk_layout_tables), render passes become one render pass
 * description per subpass, and transfers, queries, events and clears
 * become the blitter's, the query unit's and the fence's packets.  Each
 * command buffer starts with nothing bound: the processor keeps its
 * binds per session, and another submission of the session may change
 * them.
 *
 * What the packets read (tables, descriptions, clear colours, index
 * lists, data) is written to temporary memory of the device's data heap,
 * which goes back once the submission's ticket has passed.
 *
 * A command that cannot be run as recorded is refused in one of two
 * ways (design section 11.4): a draw or a dispatch whose pipeline, sets,
 * buffers or dynamic state were never bound refuses the whole submission
 * (EINVAL); a command the device cannot do (an indirect draw without
 * CAP_INDIRECT_DRAW, a partial clear) is skipped, and the log shows it
 * once per command buffer.  A transfer on a device without the blitter
 * refuses the submission (ENOTSUP).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "vk.h"

/* The bytes of a recorded command's header and its command buffer's identity. */
#define COMMAND_HEAD_BYTES		16U

/* The first room of a submission's packets, and the most dwords one submission takes. */
#define WORDS_FIRST			1024U
#define WORDS_MAX			(4U << 20)

/* The least run of the data heap one chunk of temporary memory takes. */
#define TEMP_CHUNK_BYTES		(64U * 1024U)

/* The alignment of a render pass description, a hardware table and a clear colour table. */
#define DESC_ALIGN			64U
#define TABLE_ALIGN			64U
#define CLEAR_TABLE_BYTES		(GHCI_COLOR_ATTACHMENTS * 16U)

/* The attachments whose clear values a render pass begin keeps. */
#define CLEAR_VALUES_MAX		32U

/* The tags of a clear value on the wire. */
#define CLEAR_VALUE_COLOR		0U
#define CLEAR_VALUE_DEPTH		1U
#define CLEAR_COLOR_TAG			2U
#define CLEAR_COLOR_WORDS		4U

/* The events of one submission whose words its packets write. */
#define EVENTS_MAX			GHCI_VK_SUBMIT_EVENTS

/* The bytes of a query's slot (its result, then its availability word). */
#define QUERY_SLOT_BYTES		16U

/* The bytes vkCmdUpdateBuffer may write. */
#define UPDATE_BYTES_MAX		65536U

/* The most bytes one COPY_BUFFER or FILL_BUFFER moves (a 4-byte multiple below 4 GiB). */
#define TRANSFER_BYTES_MAX		0xfffff000U

/* The dynamic states a command buffer records, as bits of their VkDynamicState values. */
#define DYNAMIC_BIT(state)		(1U << (uint32_t)(state))

/* The bind points of a command buffer. */
enum bind_point {
	BIND_GRAPHICS = 0,
	BIND_COMPUTE,
	BIND_POINTS
};

/*
 * What one bind point has bound: the pipeline (its object's identity and
 * record), the descriptor sets by number, and the dynamic offsets by
 * their index in the pipeline layout.  pipeline_dirty says that the
 * pipeline has not been bound in the ring since it changed.
 */
struct bind_state {
	uint64_t pipeline_id;
	struct ghci_vk_pipeline *pipeline;
	uint64_t sets[GHCI_VK_SETS];
	uint32_t dynamic[GHCI_GSI1_DYNAMIC_OFFSETS];
	int pipeline_dirty;
};

/* One vertex buffer binding: its address and the bytes from there to the buffer's end. */
struct vertex_state {
	uint64_t va;
	uint64_t bytes;
};

/*
 * The state one command buffer records and the ring holds for it: the
 * bind points, which of them the tables in the ring belong to (or none,
 * BIND_POINTS), the push constants, the vertex buffers (bound is a bit
 * per binding), the index buffer, and the dynamic state (set has a bit
 * per VkDynamicState recorded, dirty a bit per value not yet in the
 * ring since it changed).  logged says that the log has shown a skipped
 * command of the buffer, and secondary that it is a secondary buffer.
 */
struct buffer_state {
	struct bind_state binds[BIND_POINTS];
	uint32_t tables_owner;
	int tables_dirty;
	uint8_t push[GSI1_ABI_PUSH_BYTES];
	struct vertex_state vertex[GHCI_VERTEX_BINDINGS];
	uint32_t vertex_bound;
	int vertex_dirty;
	uint64_t index_va;
	uint32_t index_32bit;
	int index_bound;
	int index_dirty;
	uint32_t viewport[6];
	int32_t scissor[4];
	uint32_t line_width;
	uint32_t depth_bias[3];
	uint32_t blend_constants[4];
	uint32_t stencil_compare[2];
	uint32_t stencil_write[2];
	uint32_t stencil_reference[2];
	uint32_t set;
	uint32_t dirty;
	int logged;
	int secondary;
};

/*
 * The render pass a command buffer is in: the pass and framebuffer
 * records, the subpass, the render area ([x, y, width, height]), and the
 * clear values of the attachments ([4 words] of a colour, or the depth's
 * bits and the stencil in the first two).
 */
struct pass_state {
	int active;
	const struct ghci_vk_render_pass *pass;
	const struct ghci_vk_framebuffer *framebuffer;
	uint32_t subpass;
	uint32_t area[4];
	uint32_t clear[CLEAR_VALUES_MAX][4];
	uint32_t clear_count;
};

/*
 * A clear of attachments a subpass is begun again with (vkCmdClearAttachments):
 * the colour attachments by their index in the subpass, depth and stencil,
 * and the values.
 */
struct pass_clear {
	uint32_t colors;
	uint32_t color[GHCI_VK_COLOR_ATTACHMENTS][4];
	int depth;
	int stencil;
	uint32_t depth_value;
	uint32_t stencil_value;
};

/* The last value one submission's packets wrote to an event's word, at the point being made. */
struct event_write {
	uint64_t identity;
	uint32_t value;
};

/*
 * One submission being made: the session and device, the submission's
 * packets and temporary memory, the state of the command buffer being
 * walked and its render pass, the hardware tables of the bind being
 * made, the events written, and the first error, which refuses the
 * submission.
 */
struct exec {
	struct ghci_vk_session *session;
	struct ghci_vk_device *device;
	struct ghci_vk_submission *submission;
	struct ghci_vk_heap_block *chunk_block;
	uint32_t chunk_offset;
	struct buffer_state *state;
	struct pass_state pass;
	struct ghci_vk_tables tables;
	struct event_write events[EVENTS_MAX];
	uint32_t event_count;
	int error;
};

static int buffer_convert(struct exec *exec, struct ghci_vk_object *object, int secondary);
static void buffer_state_reset(struct buffer_state *state);
static void buffer_state_dirty(struct buffer_state *state);
static void command_convert(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void skip_command(struct exec *exec, const char *reason);
static void refuse(struct exec *exec, int error, const char *reason);
static void put(struct exec *exec, const uint32_t *words, uint32_t count);
static int temp_take(struct exec *exec, uint32_t bytes, uint32_t alignment, uint64_t *va, uint8_t **view);
static uint32_t read32(struct i915_wire_reader *reader);
static uint64_t read64(struct i915_wire_reader *reader);
static void read_skip(struct i915_wire_reader *reader, uint64_t bytes);
static uint64_t read_count(struct i915_wire_reader *reader);
static void bind_pipeline(struct exec *exec, struct i915_wire_reader *reader);
static void bind_sets(struct exec *exec, struct i915_wire_reader *reader);
static void bind_vertex_buffers(struct exec *exec, struct i915_wire_reader *reader);
static void bind_index_buffer(struct exec *exec, struct i915_wire_reader *reader);
static void push_constants(struct exec *exec, struct i915_wire_reader *reader);
static void dynamic_state(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static int buffer_va(struct exec *exec, uint64_t identity, uint64_t offset, uint64_t *va, uint64_t *remaining);
static int draw_prepare(struct exec *exec, int indexed);
static int graphics_state(struct exec *exec, struct ghci_vk_pipeline *pipeline);
static void dynamic_place(struct exec *exec, struct ghci_vk_pipeline *pipeline, uint32_t which);
static int vertex_place(struct exec *exec, struct ghci_vk_pipeline *pipeline);
static int tables_place(struct exec *exec, enum bind_point point);
static uint32_t pipeline_sets(const struct ghci_vk_pipeline *pipeline);
static void draw(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void draw_fan(struct exec *exec, uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance);
static void draw_indirect(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void dispatch(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static int dispatch_prepare(struct exec *exec);
static void barrier(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void barriers_read(struct i915_wire_reader *reader, uint32_t *source, uint32_t *destination);
static uint32_t stages_of(uint32_t vulkan);
static uint32_t barrier_flags(uint32_t source_access, uint32_t destination_access, uint32_t source_stages, uint32_t destination_stages);
static void begin_pass(struct exec *exec, struct i915_wire_reader *reader);
static void read_clear_value(struct i915_wire_reader *reader, uint32_t *value);
static void subpass_begin(struct exec *exec, const struct pass_clear *clear);
static void subpass_end(struct exec *exec);
static int view_of(struct exec *exec, uint32_t attachment, const struct ghci_vk_image_view **view);
static uint32_t pass_load(const struct ghci_vk_pass_attachment *attachment, uint32_t subpass, uint32_t load);
static uint32_t pass_store(const struct ghci_vk_pass_attachment *attachment, uint32_t subpass, uint32_t store);
static void clear_attachments(struct exec *exec, struct i915_wire_reader *reader);
static void clear_image(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void clear_slice(struct exec *exec, uint32_t format, uint64_t va, uint32_t pitch, uint32_t width, uint32_t height, uint32_t samples, int depth_attachment, const uint32_t *value, uint32_t aspects);
static int image_of(struct exec *exec, uint64_t identity, const struct ghci_vk_image **image, uint64_t *va);
static uint64_t image_slice(const struct ghci_vk_image *image, uint64_t base, uint32_t level, uint32_t layer, uint32_t z);
static int transfer_allowed(struct exec *exec);
static void copy_buffer(struct exec *exec, struct i915_wire_reader *reader);
static void copy_bytes(struct exec *exec, uint64_t source, uint64_t destination, uint64_t bytes);
static void fill_buffer(struct exec *exec, struct i915_wire_reader *reader);
static void update_buffer(struct exec *exec, struct i915_wire_reader *reader);
static void copy_image(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void copy_buffer_image(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void read_layers(struct i915_wire_reader *reader, VkImageSubresourceLayers *layers);
static void read_offset(struct i915_wire_reader *reader, int32_t *offset);
static void event_write(struct exec *exec, struct i915_wire_reader *reader, uint32_t value);
static void wait_events(struct exec *exec, struct i915_wire_reader *reader);
static void query(struct exec *exec, uint32_t number, struct i915_wire_reader *reader);
static void execute_commands(struct exec *exec, struct i915_wire_reader *reader);

/*
 * Makes the packets of a submission's command buffers: a barrier at the
 * head that shows the GPU what the host wrote (code, descriptors,
 * buffers), each command buffer in order, and a barrier at the tail that
 * puts the GPU's writes in memory before the fence after them.  The
 * submissions were checked (vk-queue.c).  Returns 0 with the packets and
 * temporary memory in submission, or the error that refuses the
 * submission (EINVAL, ENOTSUP, ENOMEM); the caller ends the submission
 * either way.
 */
int
ghci_vk_execute(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device,
	const VkSubmitInfo *submits,
	uint64_t count,
	struct ghci_vk_submission *submission)
{
	struct ghci_vk_object *object;
	struct exec *exec;
	uint32_t words[GHCI_DW_BARRIER];
	uint64_t index;
	uint32_t at;
	uint32_t flags;
	int error;

	/* The submission's record. */
	exec = kern_calloc(1U, sizeof(*exec));
	if (exec == NULL)
		return ENOMEM;
	exec->session = session;
	exec->device = device;
	exec->submission = submission;

	/* The head: what the host wrote is seen. */
	flags = GHCI_BARRIER_INVAL_ICACHE | GHCI_BARRIER_INVAL_TEX | GHCI_BARRIER_INVAL_L1_DATA | GHCI_BARRIER_INVAL_DESCRIPTOR;
	put(exec, words, ghci_cmd_barrier(words, 0U, 0U, flags));

	/* Each command buffer of each submission, in order, until one refuses the submission. */
	for (index = 0U; index < count && exec->error == 0; index++) {
		for (at = 0U; at < submits[index].commandBufferCount && submits[index].pCommandBuffers != NULL && exec->error == 0; at++) {
			object = ghci_vk_object_find(session, GHCI_VK_COMMAND_BUFFER, (uint64_t)(uintptr_t)submits[index].pCommandBuffers[at]);
			if (object == NULL) {
				exec->error = EINVAL;
				break;
			}
			(void)buffer_convert(exec, object, 0);
		}
	}

	/* The tail: the GPU's writes are in memory before the fence. */
	flags = GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_FLUSH_L2;
	put(exec, words, ghci_cmd_barrier(words, 0U, 0U, flags));

	/* The events the packets write wait for the submission's ticket. */
	submission->event_count = exec->event_count;
	for (at = 0U; at < exec->event_count; at++)
		submission->events[at] = exec->events[at].identity;

	/* The record goes; the packets and memory are the submission's. */
	error = exec->error;
	kern_free(exec);
	if (error != 0)
		return error;

	/* Succeeded: the packets are made. */
	return 0;
}

/*
 * Ends a submission: its temporary memory goes back once ticket has
 * passed (with no ticket, a submission that was not placed, at once; one
 * placed in part stays until the device's heap goes), the events its
 * packets write wait for the ticket, and the packets' room is freed.
 */
void
ghci_vk_submission_end(
	struct ghci_vk_device *device,
	struct ghci_vk_submission *submission,
	const struct ghci_ticket *ticket,
	int placed)
{
	struct ghci_vk_object *object;
	uint32_t index;

	/* The temporary memory: after the ticket, at once, or with the heap. */
	for (index = 0U; index < submission->temp_count; index++) {
		if (ticket != NULL)
			ghci_vk_heap_retire(device, submission->temps[index].block, submission->temps[index].offset, ticket);
		else if (!placed)
			ghci_vk_heap_give(submission->temps[index].block, submission->temps[index].offset);
	}

	/* The events' last writes. */
	for (index = 0U; index < submission->event_count && ticket != NULL; index++) {
		object = ghci_vk_object_find(device->session, GHCI_VK_EVENT, submission->events[index]);
		if (object == NULL)
			continue;
		object->as.event.written = *ticket;
		object->as.event.gpu_written = 1;
	}

	/* The arrays. */
	kern_free(submission->words);
	kern_free(submission->temps);
	kern_memset(submission, 0, sizeof(*submission));
}

/*
 * Converts one command buffer's recording: each command in turn, from a
 * state where nothing is bound.  A secondary command buffer (secondary
 * set) runs within the render pass of the one that executes it.  Returns
 * the submission's error.
 */
static int
buffer_convert(
	struct exec *exec,
	struct ghci_vk_object *object,
	int secondary)
{
	struct ghci_vk_command_buffer *commands;
	struct i915_wire_reader reader;
	struct buffer_state *saved;
	struct buffer_state *state;
	uint32_t number;

	/* A state of its own, nothing bound. */
	commands = &object->as.commands;
	state = kern_calloc(1U, sizeof(*state));
	if (state == NULL) {
		exec->error = ENOMEM;
		return exec->error;
	}
	buffer_state_reset(state);
	state->secondary = secondary;
	saved = exec->state;
	exec->state = state;
	if (!secondary)
		kern_memset(&exec->pass, 0, sizeof(exec->pass));

	/* Each command: [u32 number][u32 flags][u64 command buffer], then its arguments. */
	kern_memset(&reader, 0, sizeof(reader));
	reader.base = commands->bytes;
	reader.size = commands->used;
	while (reader.offset < reader.size && reader.error == 0 && exec->error == 0) {
		number = read32(&reader);
		read_skip(&reader, COMMAND_HEAD_BYTES - 4U);
		command_convert(exec, number, &reader);
	}
	if (reader.error != 0 && exec->error == 0)
		refuse(exec, EINVAL, "a recording that does not decode");

	/* The state goes; the one that executed a secondary buffer finds the ring changed. */
	exec->state = saved;
	kern_free(state);
	if (saved != NULL)
		buffer_state_dirty(saved);

	/* Succeeded: the buffer is converted, or the error says why not. */
	return exec->error;
}

/* Makes a command buffer's state empty: nothing bound, the tables of no bind point. */
static void
buffer_state_reset(
	struct buffer_state *state)
{
	/* Everything zero, then the marks that are not. */
	kern_memset(state, 0, sizeof(*state));
	buffer_state_dirty(state);
}

/* Marks everything a command buffer has bound as not in the ring (another buffer ran between). */
static void
buffer_state_dirty(
	struct buffer_state *state)
{
	/* The pipelines, tables, buffers and dynamic state are placed again before the next use. */
	state->binds[BIND_GRAPHICS].pipeline_dirty = 1;
	state->binds[BIND_COMPUTE].pipeline_dirty = 1;
	state->tables_owner = BIND_POINTS;
	state->tables_dirty = 1;
	state->vertex_dirty = 1;
	state->index_dirty = 1;
	state->dirty = UINT32_MAX;
}

/* Converts one recorded command (the reader after its head). */
static void
command_convert(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	/* The command's kind. */
	switch (number) {
	case GPU_OP_CMD_BIND_PIPELINE:
		bind_pipeline(exec, reader);
		break;
	case GPU_OP_CMD_SET_VIEWPORT:
	case GPU_OP_CMD_SET_SCISSOR:
	case GPU_OP_CMD_SET_LINE_WIDTH:
	case GPU_OP_CMD_SET_DEPTH_BIAS:
	case GPU_OP_CMD_SET_BLEND_CONSTANTS:
	case GPU_OP_CMD_SET_DEPTH_BOUNDS:
	case GPU_OP_CMD_SET_STENCIL_COMPARE_MASK:
	case GPU_OP_CMD_SET_STENCIL_WRITE_MASK:
	case GPU_OP_CMD_SET_STENCIL_REFERENCE:
		dynamic_state(exec, number, reader);
		break;
	case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
		bind_sets(exec, reader);
		break;
	case GPU_OP_CMD_BIND_INDEX_BUFFER:
		bind_index_buffer(exec, reader);
		break;
	case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
		bind_vertex_buffers(exec, reader);
		break;
	case GPU_OP_CMD_DRAW:
	case GPU_OP_CMD_DRAW_INDEXED:
		draw(exec, number, reader);
		break;
	case GPU_OP_CMD_DRAW_INDIRECT:
	case GPU_OP_CMD_DRAW_INDEXED_INDIRECT:
		draw_indirect(exec, number, reader);
		break;
	case GPU_OP_CMD_DISPATCH:
	case GPU_OP_CMD_DISPATCH_INDIRECT:
		dispatch(exec, number, reader);
		break;
	case GPU_OP_CMD_COPY_BUFFER:
		copy_buffer(exec, reader);
		break;
	case GPU_OP_CMD_COPY_IMAGE:
	case GPU_OP_CMD_BLIT_IMAGE:
	case GPU_OP_CMD_RESOLVE_IMAGE:
		copy_image(exec, number, reader);
		break;
	case GPU_OP_CMD_COPY_BUFFER_TO_IMAGE:
	case GPU_OP_CMD_COPY_IMAGE_TO_BUFFER:
		copy_buffer_image(exec, number, reader);
		break;
	case GPU_OP_CMD_UPDATE_BUFFER:
		update_buffer(exec, reader);
		break;
	case GPU_OP_CMD_FILL_BUFFER:
		fill_buffer(exec, reader);
		break;
	case GPU_OP_CMD_CLEAR_COLOR_IMAGE:
	case GPU_OP_CMD_CLEAR_DEPTH_STENCIL_IMAGE:
		clear_image(exec, number, reader);
		break;
	case GPU_OP_CMD_CLEAR_ATTACHMENTS:
		clear_attachments(exec, reader);
		break;
	case GPU_OP_CMD_SET_EVENT:
		event_write(exec, reader, 1U);
		break;
	case GPU_OP_CMD_RESET_EVENT:
		event_write(exec, reader, 0U);
		break;
	case GPU_OP_CMD_WAIT_EVENTS:
		wait_events(exec, reader);
		break;
	case GPU_OP_CMD_PIPELINE_BARRIER:
		barrier(exec, number, reader);
		break;
	case GPU_OP_CMD_BEGIN_QUERY:
	case GPU_OP_CMD_END_QUERY:
	case GPU_OP_CMD_RESET_QUERY_POOL:
	case GPU_OP_CMD_WRITE_TIMESTAMP:
	case GPU_OP_CMD_COPY_QUERY_POOL_RESULTS:
		query(exec, number, reader);
		break;
	case GPU_OP_CMD_PUSH_CONSTANTS:
		push_constants(exec, reader);
		break;
	case GPU_OP_CMD_BEGIN_RENDER_PASS:
		begin_pass(exec, reader);
		break;
	case GPU_OP_CMD_NEXT_SUBPASS:
		/* [contents]: the subpass ends and the next begins. */
		(void)read32(reader);
		if (!exec->pass.active || exec->pass.subpass + 1U >= exec->pass.pass->subpass_count) {
			refuse(exec, EINVAL, "a next subpass outside a render pass");
			break;
		}
		subpass_end(exec);
		exec->pass.subpass++;
		subpass_begin(exec, NULL);
		break;
	case GPU_OP_CMD_END_RENDER_PASS:
		/* The last subpass ends. */
		if (!exec->pass.active) {
			refuse(exec, EINVAL, "an end of a render pass outside one");
			break;
		}
		subpass_end(exec);
		exec->pass.active = 0;
		break;
	case GPU_OP_CMD_EXECUTE_COMMANDS:
		execute_commands(exec, reader);
		break;
	default:
		refuse(exec, EINVAL, "a recorded command of no known number");
		break;
	}
}

/*
 * Skips a command the device cannot do as recorded; the log shows the
 * first of each command buffer (and only the device's first refusals).
 */
static void
skip_command(
	struct exec *exec,
	const char *reason)
{
	struct ghci_vk *vk;

	/* One line per command buffer. */
	if (exec->state->logged)
		return;
	exec->state->logged = 1;

	/* The device's first refusals are shown. */
	vk = exec->session->device->vk;
	if (vk->refusals >= 64U)
		return;
	vk->refusals++;
	kern_logf("ghci: %s: vk: command skipped: %s\n", exec->session->device->name, reason);
}

/* Refuses the submission with error (the first one stays); the log shows why. */
static void
refuse(
	struct exec *exec,
	int error,
	const char *reason)
{
	struct ghci_vk *vk;

	/* The first error is the submission's. */
	if (exec->error != 0)
		return;
	exec->error = error;

	/* The device's first refusals are shown. */
	vk = exec->session->device->vk;
	if (vk->refusals >= 64U)
		return;
	vk->refusals++;
	kern_logf("ghci: %s: vk: submission refused: %s\n", exec->session->device->name, reason);
}

/*
 * Appends a packet (count dwords; 0 from an encoder that refused its
 * arguments refuses the submission) to the submission's packets,
 * doubling their room up to WORDS_MAX.
 */
static void
put(
	struct exec *exec,
	const uint32_t *words,
	uint32_t count)
{
	struct ghci_vk_submission *submission;
	uint32_t *grown;
	uint32_t capacity;

	/* An encoder that refused, or a submission already refused. */
	if (exec->error != 0)
		return;
	if (count == 0U) {
		refuse(exec, EINVAL, "a packet the encoder refused");
		return;
	}

	/* The room, doubled as needed. */
	submission = exec->submission;
	capacity = submission->capacity;
	if (capacity == 0U)
		capacity = WORDS_FIRST;
	while (capacity < submission->used + count)
		capacity <<= 1;
	if (capacity > WORDS_MAX) {
		refuse(exec, ENOMEM, "a submission past the packets one takes");
		return;
	}
	if (capacity != submission->capacity) {
		grown = kern_malloc((size_t)capacity * 4U);
		if (grown == NULL) {
			exec->error = ENOMEM;
			return;
		}
		if (submission->used != 0U)
			kern_memcpy(grown, submission->words, (size_t)submission->used * 4U);
		kern_free(submission->words);
		submission->words = grown;
		submission->capacity = capacity;
	}

	/* The packet after the others. */
	kern_memcpy(submission->words + submission->used, words, (size_t)count * 4U);
	submission->used += count;
}

/*
 * Takes bytes of the submission's temporary memory, aligned to alignment
 * (at most DESC_ALIGN), from the chunk being filled or a new chunk of the
 * data heap.  Returns 0 with the GPU address and the CPU view, or the
 * error, which also refuses the submission.
 */
static int
temp_take(
	struct exec *exec,
	uint32_t bytes,
	uint32_t alignment,
	uint64_t *va,
	uint8_t **view)
{
	struct ghci_vk_submission *submission;
	struct ghci_vk_temp *grown;
	uint32_t start;
	uint32_t chunk;
	uint32_t capacity;
	int error;

	/* Room in the chunk being filled. */
	submission = exec->submission;
	start = (submission->chunk_used + alignment - 1U) & ~(alignment - 1U);
	if (exec->chunk_block == NULL || start + bytes > submission->chunk_bytes) {
		/* A record of one more run. */
		if (submission->temp_count == submission->temp_capacity) {
			capacity = submission->temp_capacity * 2U;
			if (capacity == 0U)
				capacity = 8U;
			grown = kern_calloc(capacity, sizeof(*grown));
			if (grown == NULL) {
				exec->error = ENOMEM;
				return ENOMEM;
			}
			if (submission->temp_count != 0U)
				kern_memcpy(grown, submission->temps, submission->temp_count * sizeof(*grown));
			kern_free(submission->temps);
			submission->temps = grown;
			submission->temp_capacity = capacity;
		}

		/* A new chunk of the data heap. */
		chunk = TEMP_CHUNK_BYTES;
		while (chunk < bytes)
			chunk <<= 1;
		error = ghci_vk_heap_take(exec->session, exec->device, GHCI_VK_HEAP_DATA, chunk, DESC_ALIGN, &exec->chunk_block, &exec->chunk_offset);
		if (error != 0) {
			exec->chunk_block = NULL;
			refuse(exec, error == ENOMEM ? ENOMEM : EIO, "no temporary memory for a submission");
			return error;
		}
		submission->temps[submission->temp_count].block = exec->chunk_block;
		submission->temps[submission->temp_count].offset = exec->chunk_offset;
		submission->temp_count++;
		submission->chunk_used = 0U;
		submission->chunk_bytes = chunk;
		start = 0U;
	}

	/* The bytes, cleared. */
	*va = exec->chunk_block->mapping->va + exec->chunk_offset + start;
	*view = exec->chunk_block->allocation->view + exec->chunk_offset + start;
	kern_memset(*view, 0, bytes);
	submission->chunk_used = start + bytes;

	/* Succeeded: the bytes are the submission's. */
	return 0;
}

/* Reads a 32-bit word of a recording. */
static uint32_t
read32(
	struct i915_wire_reader *reader)
{
	/* The codec's read. */
	return drv_i915_wire_read_u32(reader);
}

/* Reads a 64-bit word of a recording. */
static uint64_t
read64(
	struct i915_wire_reader *reader)
{
	/* The codec's read. */
	return drv_i915_wire_read_u64(reader);
}

/* Skips bytes of a recording. */
static void
read_skip(
	struct i915_wire_reader *reader,
	uint64_t bytes)
{
	/* The shared skip. */
	ghci_vk_skip(reader, bytes);
}

/* Reads an array's u64 count. */
static uint64_t
read_count(
	struct i915_wire_reader *reader)
{
	/* The shared count. */
	return ghci_vk_read_count(reader);
}

/* vkCmdBindPipeline: [bind point][pipeline]; placed before the next draw or dispatch. */
static void
bind_pipeline(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	struct bind_state *bind;
	uint32_t point;
	uint64_t identity;

	/* The bind point and the pipeline, of the session and of that kind. */
	point = read32(reader);
	identity = read64(reader);
	object = ghci_vk_object_find(exec->session, GHCI_VK_PIPELINE, identity);
	if (object == NULL || object->as.pipeline == NULL || object->as.pipeline->bind_point != point) {
		refuse(exec, EINVAL, "a bind of a pipeline the session does not have");
		return;
	}
	if (point != (uint32_t)VK_PIPELINE_BIND_POINT_GRAPHICS && point != (uint32_t)VK_PIPELINE_BIND_POINT_COMPUTE) {
		refuse(exec, EINVAL, "a bind point Vulkan 1.0 does not have");
		return;
	}

	/* The bind point's pipeline; the tables and (for graphics) the state follow it. */
	bind = &exec->state->binds[point == (uint32_t)VK_PIPELINE_BIND_POINT_COMPUTE ? BIND_COMPUTE : BIND_GRAPHICS];
	if (bind->pipeline_id != identity || bind->pipeline != object->as.pipeline) {
		bind->pipeline_id = identity;
		bind->pipeline = object->as.pipeline;
		bind->pipeline_dirty = 1;
		exec->state->tables_dirty = 1;
		exec->state->vertex_dirty = 1;
	}
}

/*
 * vkCmdBindDescriptorSets: [bind point][layout][first][count][u64 count]
 * [sets][dynamic count][u64 count][offsets].  The dynamic offsets go to
 * the places of the layout's sets first and on.
 */
static void
bind_sets(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	const struct ghci_vk_pipeline_layout *layout;
	struct ghci_vk_object *object;
	struct bind_state *bind;
	uint64_t sets[GHCI_VK_SETS];
	uint32_t offsets[GHCI_GSI1_DYNAMIC_OFFSETS];
	uint32_t point;
	uint32_t first;
	uint32_t count;
	uint32_t dynamic_count;
	uint32_t expected;
	uint32_t index;
	uint64_t wire;

	/* The bind point, the layout and the sets. */
	point = read32(reader);
	object = ghci_vk_object_find(exec->session, GHCI_VK_PIPELINE_LAYOUT, read64(reader));
	first = read32(reader);
	count = read32(reader);
	wire = read_count(reader);
	if (reader->error != 0 || wire != count || first >= GHCI_VK_SETS || count > GHCI_VK_SETS - first) {
		refuse(exec, EINVAL, "a bind of sets past the four");
		return;
	}
	for (index = 0U; index < count; index++)
		sets[index] = read64(reader);

	/* The dynamic offsets. */
	dynamic_count = read32(reader);
	wire = read_count(reader);
	if (reader->error != 0 || wire != dynamic_count || dynamic_count > GHCI_GSI1_DYNAMIC_OFFSETS) {
		refuse(exec, EINVAL, "dynamic offsets past the bank's");
		return;
	}
	for (index = 0U; index < dynamic_count; index++)
		offsets[index] = read32(reader);

	/* A layout of the session whose sets take that many dynamic offsets. */
	if (object == NULL || object->as.pipeline_layout == NULL) {
		refuse(exec, EINVAL, "a bind of sets with a pipeline layout the session does not have");
		return;
	}
	layout = object->as.pipeline_layout;
	expected = 0U;
	for (index = first; index < first + count; index++)
		expected += layout->dynamic_sets[index];
	if (expected != dynamic_count || layout->dynamic_base[first] + dynamic_count > GHCI_GSI1_DYNAMIC_OFFSETS) {
		refuse(exec, EINVAL, "dynamic offsets that are not the sets' count");
		return;
	}
	for (index = 0U; index < count; index++) {
		if (ghci_vk_object_find(exec->session, GHCI_VK_DESCRIPTOR_SET, sets[index]) == NULL) {
			refuse(exec, EINVAL, "a bind of a set the session does not have");
			return;
		}
	}

	/* The bind point's sets and offsets; its tables are placed again. */
	bind = &exec->state->binds[point == (uint32_t)VK_PIPELINE_BIND_POINT_COMPUTE ? BIND_COMPUTE : BIND_GRAPHICS];
	for (index = 0U; index < count; index++)
		bind->sets[first + index] = sets[index];
	for (index = 0U; index < dynamic_count; index++)
		bind->dynamic[layout->dynamic_base[first] + index] = offsets[index];
	exec->state->tables_dirty = 1;
}

/* vkCmdBindVertexBuffers: [first][count][u64 count][buffers][u64 count][offsets]. */
static void
bind_vertex_buffers(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	uint64_t buffers[GHCI_VERTEX_BINDINGS];
	uint64_t offsets[GHCI_VERTEX_BINDINGS];
	uint64_t va;
	uint64_t remaining;
	uint32_t first;
	uint32_t count;
	uint32_t index;
	uint64_t wire;

	/* The bindings, within the sixteen. */
	first = read32(reader);
	count = read32(reader);
	wire = read_count(reader);
	if (reader->error != 0 || wire != count || first >= GHCI_VERTEX_BINDINGS || count > GHCI_VERTEX_BINDINGS - first) {
		refuse(exec, EINVAL, "vertex buffers past the sixteen bindings");
		return;
	}
	for (index = 0U; index < count; index++)
		buffers[index] = read64(reader);
	wire = read_count(reader);
	if (reader->error != 0 || wire != count) {
		refuse(exec, EINVAL, "vertex buffer offsets that are not the buffers'");
		return;
	}
	for (index = 0U; index < count; index++)
		offsets[index] = read64(reader);

	/* Each binding's address and the bytes to the buffer's end (the vertex fetch reads 0 past them). */
	for (index = 0U; index < count; index++) {
		if (buffer_va(exec, buffers[index], offsets[index], &va, &remaining) != 0) {
			refuse(exec, EINVAL, "a vertex buffer the session does not have bound to memory");
			return;
		}
		exec->state->vertex[first + index].va = va;
		exec->state->vertex[first + index].bytes = remaining;
		exec->state->vertex_bound |= 1U << (first + index);
	}
	exec->state->vertex_dirty = 1;
}

/* vkCmdBindIndexBuffer: [buffer][u64 offset][index type]. */
static void
bind_index_buffer(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	uint64_t identity;
	uint64_t offset;
	uint64_t va;
	uint64_t remaining;
	uint32_t type;

	/* The buffer, the offset and a type of Vulkan 1.0. */
	identity = read64(reader);
	offset = read64(reader);
	type = read32(reader);
	if (type != (uint32_t)VK_INDEX_TYPE_UINT16 && type != (uint32_t)VK_INDEX_TYPE_UINT32) {
		refuse(exec, EINVAL, "an index type Vulkan 1.0 does not have");
		return;
	}
	if (buffer_va(exec, identity, offset, &va, &remaining) != 0) {
		refuse(exec, EINVAL, "an index buffer the session does not have bound to memory");
		return;
	}

	/* The binding, placed before the next indexed draw. */
	exec->state->index_va = va;
	exec->state->index_32bit = type == (uint32_t)VK_INDEX_TYPE_UINT32 ? 1U : 0U;
	exec->state->index_bound = 1;
	exec->state->index_dirty = 1;
}

/* vkCmdPushConstants: [layout][stages][offset][size][u64 count][the bytes, padded to words]. */
static void
push_constants(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	const uint8_t *bytes;
	uint32_t offset;
	uint32_t size;
	uint64_t count;

	/* The range, within the 128 bytes. */
	read_skip(reader, 8U + 4U);
	offset = read32(reader);
	size = read32(reader);
	count = read_count(reader);
	bytes = drv_i915_wire_read_array(reader, (size_t)((count + 3U) & ~(uint64_t)3U), 1U);
	if (reader->error != 0 || bytes == NULL || count != size || offset > GSI1_ABI_PUSH_BYTES || size > GSI1_ABI_PUSH_BYTES - offset) {
		refuse(exec, EINVAL, "push constants past the 128 bytes");
		return;
	}

	/* The bytes; both bind points load their bank again. */
	kern_memcpy(exec->state->push + offset, bytes, size);
	exec->state->tables_dirty = 1;
}

/*
 * The dynamic state commands: the viewport and scissor (index 0, one),
 * the line width, depth bias ([constant][clamp][slope]), blend
 * constants, depth bounds (not a feature: skipped), and the stencil
 * compare mask, write mask and reference ([faces][value]).
 */
static void
dynamic_state(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	struct buffer_state *state;
	uint32_t first;
	uint32_t count;
	uint32_t faces;
	uint32_t value;
	uint32_t index;
	uint64_t wire;

	/* The command's values, into the buffer's state. */
	state = exec->state;
	switch (number) {
	case GPU_OP_CMD_SET_VIEWPORT:
	case GPU_OP_CMD_SET_SCISSOR:
		/* [first][count][u64 count], then six floats or [x][y][width][height] each; only index 0 exists. */
		first = read32(reader);
		count = read32(reader);
		wire = read_count(reader);
		if (reader->error != 0 || wire != count || first != 0U || count != 1U) {
			refuse(exec, EINVAL, "a viewport or scissor other than the one");
			return;
		}
		if (number == GPU_OP_CMD_SET_VIEWPORT) {
			for (index = 0U; index < 6U; index++)
				state->viewport[index] = read32(reader);
			state->set |= DYNAMIC_BIT(VK_DYNAMIC_STATE_VIEWPORT);
			state->dirty |= DYNAMIC_BIT(VK_DYNAMIC_STATE_VIEWPORT);
		} else {
			for (index = 0U; index < 4U; index++)
				state->scissor[index] = (int32_t)read32(reader);
			state->set |= DYNAMIC_BIT(VK_DYNAMIC_STATE_SCISSOR);
			state->dirty |= DYNAMIC_BIT(VK_DYNAMIC_STATE_SCISSOR);
		}
		break;
	case GPU_OP_CMD_SET_LINE_WIDTH:
		state->line_width = read32(reader);
		state->set |= DYNAMIC_BIT(VK_DYNAMIC_STATE_LINE_WIDTH);
		state->dirty |= DYNAMIC_BIT(VK_DYNAMIC_STATE_LINE_WIDTH);
		break;
	case GPU_OP_CMD_SET_DEPTH_BIAS:
		/* The wire's constant, clamp and slope, kept in the packet's order: constant, slope, clamp. */
		state->depth_bias[0] = read32(reader);
		state->depth_bias[2] = read32(reader);
		state->depth_bias[1] = read32(reader);
		state->set |= DYNAMIC_BIT(VK_DYNAMIC_STATE_DEPTH_BIAS);
		state->dirty |= DYNAMIC_BIT(VK_DYNAMIC_STATE_DEPTH_BIAS);
		break;
	case GPU_OP_CMD_SET_BLEND_CONSTANTS:
		/* [u64 4][4 x float]. */
		if (read_count(reader) != 4U) {
			refuse(exec, EINVAL, "blend constants that are not four");
			return;
		}
		for (index = 0U; index < 4U; index++)
			state->blend_constants[index] = read32(reader);
		state->set |= DYNAMIC_BIT(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
		state->dirty |= DYNAMIC_BIT(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
		break;
	case GPU_OP_CMD_SET_DEPTH_BOUNDS:
		/* Depth bounds are not a feature of the device. */
		read_skip(reader, 8U);
		skip_command(exec, "depth bounds (not a feature of the device)");
		break;
	default:
		/* The stencil values of the faces named. */
		faces = read32(reader);
		value = read32(reader) & 0xffU;
		for (index = 0U; index < 2U; index++) {
			if ((faces & (1U << index)) == 0U)
				continue;
			if (number == GPU_OP_CMD_SET_STENCIL_COMPARE_MASK)
				state->stencil_compare[index] = value;
			else if (number == GPU_OP_CMD_SET_STENCIL_WRITE_MASK)
				state->stencil_write[index] = value;
			else
				state->stencil_reference[index] = value;
		}
		if (number == GPU_OP_CMD_SET_STENCIL_COMPARE_MASK)
			index = DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);
		else if (number == GPU_OP_CMD_SET_STENCIL_WRITE_MASK)
			index = DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);
		else
			index = DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
		state->set |= index;
		state->dirty |= index;
		break;
	}
}

/*
 * Finds a buffer of the session bound to memory and the GPU address of
 * offset in it, with the bytes from there to the buffer's end.  Returns
 * 0, or ENOENT.
 */
static int
buffer_va(
	struct exec *exec,
	uint64_t identity,
	uint64_t offset,
	uint64_t *va,
	uint64_t *remaining)
{
	struct ghci_vk_object *object;
	int error;

	/* A buffer of the session, bound, and an offset within it. */
	object = ghci_vk_object_find(exec->session, GHCI_VK_BUFFER, identity);
	if (object == NULL || object->as.buffer.memory == 0U || offset > object->as.buffer.size)
		return ENOENT;
	error = ghci_vk_bound_va(exec->session, object->as.buffer.memory, object->as.buffer.offset + offset, va);
	if (error != 0)
		return error;

	/* Succeeded: the address and the bytes left. */
	*remaining = object->as.buffer.size - offset;
	return 0;
}

/*
 * Makes the ring hold what a draw reads: the render pass, the graphics
 * pipeline and its state, the vertex buffers and (indexed) the index
 * buffer, and the tables.  Returns 0, ENOENT for a draw to skip, or
 * EINVAL (the submission is refused).
 */
static int
draw_prepare(
	struct exec *exec,
	int indexed)
{
	struct ghci_vk_pipeline *pipeline;
	struct buffer_state *state;
	uint32_t words[GHCI_DW_BIND_INDEX_BUFFER];
	int error;

	/* Within a render pass, with a graphics pipeline bound. */
	state = exec->state;
	if (!exec->pass.active) {
		refuse(exec, EINVAL, "a draw outside a render pass");
		return EINVAL;
	}
	pipeline = state->binds[BIND_GRAPHICS].pipeline;
	if (pipeline == NULL) {
		refuse(exec, EINVAL, "a draw without a graphics pipeline bound");
		return EINVAL;
	}

	/* The pipeline and its state, then the vertex buffers. */
	error = graphics_state(exec, pipeline);
	if (error != 0)
		return error;
	error = vertex_place(exec, pipeline);
	if (error != 0)
		return error;

	/* The index buffer of an indexed draw. */
	if (indexed) {
		if (!state->index_bound) {
			refuse(exec, EINVAL, "an indexed draw without an index buffer bound");
			return EINVAL;
		}
		if (state->index_dirty) {
			put(exec, words, ghci_cmd_bind_index_buffer(words, state->index_va, state->index_32bit));
			state->index_dirty = 0;
		}
	}

	/* The tables and constants. */
	error = tables_place(exec, BIND_GRAPHICS);
	if (error != 0)
		return error;

	/* Succeeded: the draw may be placed. */
	return 0;
}

/*
 * Places the graphics pipeline when it changed, with the state its record
 * does not keep (design R07): the viewport and scissor (an empty scissor
 * for a pipeline that discards its primitives), depth bias, blend
 * constants, the stencil words, a dynamic line width, and each
 * attachment's blend word on a device with independent blending; and
 * after that the dynamic values recorded since.  A dynamic state the
 * pipeline needs that the buffer never recorded refuses the submission.
 */
static int
graphics_state(
	struct exec *exec,
	struct ghci_vk_pipeline *pipeline)
{
	struct buffer_state *state;
	struct bind_state *bind;
	uint32_t words[GHCI_DW_BIND_PIPELINE];
	uint32_t needed;

	/* Every dynamic state the pipeline takes from the buffer was recorded. */
	state = exec->state;
	bind = &state->binds[BIND_GRAPHICS];
	needed = pipeline->state.dynamic & (DYNAMIC_BIT(VK_DYNAMIC_STATE_VIEWPORT) | DYNAMIC_BIT(VK_DYNAMIC_STATE_SCISSOR)
		| DYNAMIC_BIT(VK_DYNAMIC_STATE_LINE_WIDTH) | DYNAMIC_BIT(VK_DYNAMIC_STATE_DEPTH_BIAS)
		| DYNAMIC_BIT(VK_DYNAMIC_STATE_BLEND_CONSTANTS) | DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)
		| DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK) | DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_REFERENCE));
	if ((needed & ~state->set) != 0U) {
		refuse(exec, EINVAL, "a draw without the dynamic state its pipeline needs");
		return EINVAL;
	}

	/* A new pipeline: its record, then all of its state. */
	if (bind->pipeline_dirty) {
		put(exec, words, ghci_cmd_bind_pipeline(words, pipeline->pso_va));
		dynamic_place(exec, pipeline, UINT32_MAX);
		bind->pipeline_dirty = 0;
		state->dirty = 0U;
		return exec->error;
	}

	/* The same pipeline: the dynamic values recorded since. */
	if ((state->dirty & pipeline->state.dynamic) != 0U)
		dynamic_place(exec, pipeline, state->dirty & pipeline->state.dynamic);
	state->dirty = 0U;

	/* Succeeded: the pipeline and its state are in the ring. */
	return exec->error;
}

/*
 * Places the pipeline's state of which (bits of VkDynamicState; all for a
 * pipeline just bound): each value is the recorded one when the
 * pipeline made it dynamic, the pipeline's own otherwise.
 */
static void
dynamic_place(
	struct exec *exec,
	struct ghci_vk_pipeline *pipeline,
	uint32_t which)
{
	const struct ghci_vk_static_state *fixed;
	struct buffer_state *state;
	uint32_t words[GHCI_DW_SET_VIEWPORT];
	uint32_t payload[4];
	uint32_t dynamic;
	uint32_t index;
	int32_t scissor[4];

	/* The values of each state. */
	state = exec->state;
	fixed = &pipeline->state;
	dynamic = fixed->dynamic;

	/* The viewport. */
	if ((which & DYNAMIC_BIT(VK_DYNAMIC_STATE_VIEWPORT)) != 0U) {
		if ((dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_VIEWPORT)) != 0U)
			put(exec, words, ghci_cmd_set_viewport(words, state->viewport));
		else
			put(exec, words, ghci_cmd_set_viewport(words, fixed->viewport));
	}

	/* The scissor; empty for a pipeline that discards its primitives. */
	if ((which & DYNAMIC_BIT(VK_DYNAMIC_STATE_SCISSOR)) != 0U) {
		if ((dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_SCISSOR)) != 0U) {
			kern_memcpy(scissor, state->scissor, sizeof(scissor));
		} else {
			scissor[0] = fixed->scissor_offset[0];
			scissor[1] = fixed->scissor_offset[1];
			scissor[2] = (int32_t)fixed->scissor_extent[0];
			scissor[3] = (int32_t)fixed->scissor_extent[1];
		}
		if (fixed->rasterizer_discard != 0U) {
			scissor[2] = 0;
			scissor[3] = 0;
		}
		put(exec, words, ghci_cmd_set_scissor(words, scissor[0], scissor[1], (uint32_t)scissor[2], (uint32_t)scissor[3]));
	}

	/* The line width, only when dynamic (the record's dw9 holds the fixed one). */
	if ((which & DYNAMIC_BIT(VK_DYNAMIC_STATE_LINE_WIDTH)) != 0U && (dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_LINE_WIDTH)) != 0U) {
		kern_memset(payload, 0, sizeof(payload));
		payload[0] = state->line_width;
		put(exec, words, ghci_cmd_set_dynamic_state(words, GHCI_DYNAMIC_LINE_WIDTH, payload));
	}

	/* Depth bias: constant, slope, clamp. */
	if ((which & DYNAMIC_BIT(VK_DYNAMIC_STATE_DEPTH_BIAS)) != 0U) {
		kern_memset(payload, 0, sizeof(payload));
		for (index = 0U; index < 3U; index++)
			payload[index] = (dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_DEPTH_BIAS)) != 0U ? state->depth_bias[index] : fixed->depth_bias[index];
		put(exec, words, ghci_cmd_set_dynamic_state(words, GHCI_DYNAMIC_DEPTH_BIAS, payload));
	}

	/* The blend constants. */
	if ((which & DYNAMIC_BIT(VK_DYNAMIC_STATE_BLEND_CONSTANTS)) != 0U) {
		for (index = 0U; index < 4U; index++)
			payload[index] = (dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_BLEND_CONSTANTS)) != 0U ? state->blend_constants[index] : fixed->blend_constants[index];
		put(exec, words, ghci_cmd_set_dynamic_state(words, GHCI_DYNAMIC_BLEND_CONSTANTS, payload));
	}

	/* The stencil words of the two faces, each value recorded or the pipeline's. */
	if ((which & (DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK) | DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)
		| DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_REFERENCE))) != 0U) {
		kern_memset(payload, 0, sizeof(payload));
		for (index = 0U; index < 2U; index++) {
			payload[index] = fixed->stencil[index];
			if ((dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)) != 0U)
				payload[index] = (payload[index] & ~0xffU) | state->stencil_compare[index];
			if ((dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) != 0U)
				payload[index] = (payload[index] & ~0xff00U) | (state->stencil_write[index] << 8);
			if ((dynamic & DYNAMIC_BIT(VK_DYNAMIC_STATE_STENCIL_REFERENCE)) != 0U)
				payload[index] = (payload[index] & ~0xff0000U) | (state->stencil_reference[index] << 16);
		}
		put(exec, words, ghci_cmd_set_dynamic_state(words, GHCI_DYNAMIC_STENCIL_PARAMS, payload));
	}

	/* Each attachment's blend word (only a device with independent blending has them). */
	if (which == UINT32_MAX) {
		for (index = 0U; index < fixed->blend_count && index < GHCI_VK_COLOR_ATTACHMENTS; index++) {
			kern_memset(payload, 0, sizeof(payload));
			payload[0] = fixed->blend_attachment[index];
			put(exec, words, ghci_cmd_set_dynamic_state(words, GHCI_DYNAMIC_BLEND_ATTACHMENT_0 + index, payload));
		}
	}
}

/*
 * Places the vertex buffers the pipeline reads, one packet per run of
 * bindings, with the pipeline's strides; a binding it reads that the
 * buffer never bound refuses the submission.
 */
static int
vertex_place(
	struct exec *exec,
	struct ghci_vk_pipeline *pipeline)
{
	struct ghci_vertex_binding bindings[GHCI_VERTEX_BINDINGS];
	uint32_t words[2U + 4U * GHCI_VERTEX_BINDINGS];
	struct buffer_state *state;
	uint32_t first;
	uint32_t count;
	uint32_t binding;

	/* Every binding the pipeline reads is bound. */
	state = exec->state;
	if ((pipeline->vertex_bindings & ~state->vertex_bound) != 0U) {
		refuse(exec, EINVAL, "a draw without the vertex buffers its pipeline reads");
		return EINVAL;
	}
	if (!state->vertex_dirty)
		return 0;

	/* Each run of bindings the pipeline reads. */
	binding = 0U;
	while (binding < GHCI_VERTEX_BINDINGS) {
		if ((pipeline->vertex_bindings & (1U << binding)) == 0U) {
			binding++;
			continue;
		}
		first = binding;
		count = 0U;
		while (binding < GHCI_VERTEX_BINDINGS && (pipeline->vertex_bindings & (1U << binding)) != 0U) {
			bindings[count].va = state->vertex[binding].va;
			bindings[count].bytes = state->vertex[binding].bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)state->vertex[binding].bytes;
			bindings[count].stride = pipeline->strides[binding];
			count++;
			binding++;
		}
		put(exec, words, ghci_cmd_bind_vertex_buffers(words, first, count, bindings));
	}
	state->vertex_dirty = 0;

	/* Succeeded: the buffers are in the ring. */
	return exec->error;
}

/* Reports the descriptor sets a pipeline's tables read, a bit per set. */
static uint32_t
pipeline_sets(
	const struct ghci_vk_pipeline *pipeline)
{
	const struct ghci_gsi1_bindings *bindings;
	uint32_t sets;
	uint32_t index;

	/* The sets of the texture and sampler slots and of the resource table's entries. */
	bindings = &pipeline->bindings;
	sets = 0U;
	for (index = 0U; index < bindings->texture_count && index < GHCI_GSI1_TEXTURE_SLOTS; index++) {
		if (bindings->texture[index].kind != 0U)
			sets |= 1U << bindings->texture[index].set;
	}
	for (index = 0U; index < bindings->sampler_count && index < GHCI_GSI1_SAMPLER_SLOTS; index++) {
		if (bindings->sampler[index].kind != 0U)
			sets |= 1U << bindings->sampler[index].set;
	}
	for (index = 0U; index < bindings->resource_count && index < GHCI_GSI1_RESOURCES; index++)
		sets |= 1U << bindings->resource[index].set;

	/* Succeeded: the sets. */
	return sets;
}

/*
 * Places the hardware tables and constant bank words of a bind point's
 * pipeline when they changed or the other bind point's are in the ring
 * (the two share the four tables and the bank): sets 0 to 2 in temporary
 * memory, set 3 (the pipeline's driver table) when it has one, and
 * LOAD_CONST of c0-c23 and c32 on (never c24-c31).  A set the pipeline
 * reads that was never bound refuses the submission; a binding the
 * tables cannot be made of (ENOENT) skips the draw or dispatch.
 */
static int
tables_place(
	struct exec *exec,
	enum bind_point point)
{
	struct ghci_vk_pipeline *pipeline;
	struct ghci_vk_tables *tables;
	struct buffer_state *state;
	struct bind_state *bind;
	uint32_t words[3U + GHCI_CONST_LOAD_MAX];
	uint32_t used;
	uint32_t set;
	uint32_t low;
	uint32_t at;
	uint32_t count;
	uint64_t va;
	uint8_t *view;
	int error;

	/* Nothing changed since the bind point's tables were placed. */
	state = exec->state;
	bind = &state->binds[point];
	pipeline = bind->pipeline;
	if (!state->tables_dirty && state->tables_owner == (uint32_t)point)
		return 0;

	/* Every set the pipeline reads is bound. */
	used = pipeline_sets(pipeline);
	for (set = 0U; set < GHCI_VK_SETS; set++) {
		if ((used & (1U << set)) != 0U && bind->sets[set] == 0U) {
			refuse(exec, EINVAL, "a draw or dispatch without the descriptor sets its pipeline reads");
			return EINVAL;
		}
	}

	/* The tables and the bank of the bind. */
	tables = &exec->tables;
	error = ghci_vk_layout_tables(exec->session, pipeline, bind->sets, bind->dynamic, pipeline->dynamic_count, state->push, GSI1_ABI_PUSH_BYTES, tables);
	if (error == ENOENT) {
		skip_command(exec, "a draw or dispatch whose descriptors cannot be made into the tables");
		return ENOENT;
	}
	if (error != 0) {
		refuse(exec, EINVAL, "a bind whose dynamic offsets are not its pipeline's");
		return EINVAL;
	}

	/* Set 0: the textures by slot. */
	if (tables->texture_count != 0U) {
		error = temp_take(exec, tables->texture_count * GHCI_TEXTURE_DESC_BYTES, TABLE_ALIGN, &va, &view);
		if (error != 0)
			return error;
		kern_memcpy(view, tables->textures, tables->texture_count * GHCI_TEXTURE_DESC_BYTES);
		put(exec, words, ghci_cmd_bind_descriptor_table(words, GSI1_ABI_SET_TEXTURES, va, tables->texture_count));
	}

	/* Set 1: the samplers by slot. */
	if (tables->sampler_count != 0U) {
		error = temp_take(exec, tables->sampler_count * GHCI_SAMPLER_DESC_BYTES, TABLE_ALIGN, &va, &view);
		if (error != 0)
			return error;
		kern_memcpy(view, tables->samplers, tables->sampler_count * GHCI_SAMPLER_DESC_BYTES);
		put(exec, words, ghci_cmd_bind_descriptor_table(words, GSI1_ABI_SET_SAMPLERS, va, tables->sampler_count));
	}

	/* Set 2: the resource table, in 16-byte units. */
	if (tables->resource_bytes != 0U) {
		error = temp_take(exec, tables->resource_bytes, TABLE_ALIGN, &va, &view);
		if (error != 0)
			return error;
		kern_memcpy(view, tables->resources, tables->resource_bytes);
		put(exec, words, ghci_cmd_bind_descriptor_table(words, GSI1_ABI_SET_RESOURCES, va, tables->resource_bytes / GSI1_ABI_RESOURCE_UNIT));
	}

	/* Set 3: the pipeline's driver table (scratch). */
	if (pipeline->table_va != 0U)
		put(exec, words, ghci_cmd_bind_descriptor_table(words, GSI1_ABI_SET_DRIVER, pipeline->table_va, GSI1_ABI_DRIVER_TABLE_BYTES / GSI1_ABI_RESOURCE_UNIT));

	/* The push constants of c0-c23, then c32 to the bank's end (push 96-127 and the dynamic offsets). */
	low = (pipeline->push_bytes + 3U) / 4U;
	if (low > GSI1_ABI_PUSH_LOW_WORDS)
		low = GSI1_ABI_PUSH_LOW_WORDS;
	if (low != 0U)
		put(exec, words, ghci_cmd_load_const(words, GSI1_ABI_BANK_PUSH_LOW, low, tables->bank));
	at = GSI1_ABI_BANK_PUSH_HIGH;
	if (pipeline->push_bytes <= GSI1_ABI_PUSH_LOW_WORDS * 4U)
		at = GSI1_ABI_BANK_DYNAMIC;
	while (at < tables->bank_count) {
		count = tables->bank_count - at;
		if (count > GHCI_CONST_LOAD_MAX)
			count = GHCI_CONST_LOAD_MAX;
		put(exec, words, ghci_cmd_load_const(words, at, count, tables->bank + at));
		at += count;
	}

	/* The tables are the bind point's. */
	state->tables_owner = (uint32_t)point;
	state->tables_dirty = 0;

	/* Succeeded: the tables are in the ring. */
	return exec->error;
}

/*
 * vkCmdDraw: [vertices][instances][first vertex][first instance];
 * vkCmdDrawIndexed: [indices][instances][first index][vertex offset]
 * [first instance].  A triangle fan draws from an index list.
 */
static void
draw(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_pipeline *pipeline;
	uint32_t words[GHCI_DW_DRAW_INDEXED];
	uint32_t arguments[5];
	uint32_t count;
	uint32_t index;
	int indexed;
	int error;

	/* The arguments. */
	indexed = number == GPU_OP_CMD_DRAW_INDEXED;
	count = indexed ? 5U : 4U;
	for (index = 0U; index < count; index++)
		arguments[index] = read32(reader);
	if (reader->error != 0)
		return;

	/* An indexed triangle fan is not drawn (its index list would need rewriting). */
	pipeline = exec->state->binds[BIND_GRAPHICS].pipeline;
	if (indexed && pipeline != NULL && pipeline->fan != 0U) {
		skip_command(exec, "an indexed triangle fan");
		return;
	}

	/* What the draw reads. */
	error = draw_prepare(exec, indexed);
	if (error != 0)
		return;

	/* The draw (a fan from its index list). */
	if (indexed) {
		put(exec, words, ghci_cmd_draw_indexed(words, arguments[0], arguments[1], arguments[2], (int32_t)arguments[3], arguments[4]));
	} else if (pipeline->fan != 0U) {
		draw_fan(exec, arguments[0], arguments[1], arguments[2], arguments[3]);
	} else {
		put(exec, words, ghci_cmd_draw(words, arguments[0], arguments[1], arguments[2], arguments[3]));
	}
}

/*
 * Draws a triangle fan as a triangle list from an index list in
 * temporary memory (design R30): triangle i is vertices (i + 1, i + 2, 0)
 * from the first, so that its provoking vertex is Vulkan's; the
 * client's index buffer is bound again before the next indexed draw.
 */
static void
draw_fan(
	struct exec *exec,
	uint32_t vertices,
	uint32_t instances,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	uint32_t words[GHCI_DW_DRAW_INDEXED];
	uint32_t triangles;
	uint32_t index;
	uint32_t value;
	uint64_t va;
	uint8_t *view;
	int error;

	/* Fewer than three vertices draw nothing. */
	if (vertices < 3U)
		return;
	triangles = vertices - 2U;
	if (triangles > (UINT32_MAX / 12U)) {
		skip_command(exec, "a triangle fan too long for its index list");
		return;
	}

	/* The index list. */
	error = temp_take(exec, triangles * 12U, 4U, &va, &view);
	if (error != 0)
		return;
	for (index = 0U; index < triangles; index++) {
		value = index + 1U;
		kern_memcpy(view + index * 12U, &value, 4U);
		value = index + 2U;
		kern_memcpy(view + index * 12U + 4U, &value, 4U);
		value = 0U;
		kern_memcpy(view + index * 12U + 8U, &value, 4U);
	}

	/* The list bound, the draw from it, and the client's index buffer again later. */
	put(exec, words, ghci_cmd_bind_index_buffer(words, va, 1U));
	put(exec, words, ghci_cmd_draw_indexed(words, triangles * 3U, instances, 0U, (int32_t)first_vertex, first_instance));
	exec->state->index_dirty = 1;
}

/*
 * vkCmdDrawIndirect and vkCmdDrawIndexedIndirect: [buffer][u64 offset]
 * [draw count][stride]; skipped without CAP_INDIRECT_DRAW, and for a
 * triangle fan.
 */
static void
draw_indirect(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_pipeline *pipeline;
	uint32_t words[GHCI_DW_DRAW_INDIRECT];
	uint64_t identity;
	uint64_t offset;
	uint64_t va;
	uint64_t remaining;
	uint32_t draws;
	uint32_t stride;
	int indexed;
	int has;
	int error;

	/* The arguments. */
	identity = read64(reader);
	offset = read64(reader);
	draws = read32(reader);
	stride = read32(reader);
	if (reader->error != 0)
		return;
	indexed = number == GPU_OP_CMD_DRAW_INDEXED_INDIRECT;

	/* A device that reads draws from memory, and no fan. */
	has = ghci_caps_has(&exec->session->device->caps, GHCI_HAS_INDIRECT_DRAW);
	if (!has) {
		skip_command(exec, "an indirect draw on a device without CAP_INDIRECT_DRAW");
		return;
	}
	pipeline = exec->state->binds[BIND_GRAPHICS].pipeline;
	if (pipeline != NULL && pipeline->fan != 0U) {
		skip_command(exec, "an indirect triangle fan");
		return;
	}
	if (buffer_va(exec, identity, 0U, &va, &remaining) != 0 || offset > UINT32_MAX) {
		refuse(exec, EINVAL, "an indirect draw from a buffer the session does not have bound");
		return;
	}

	/* What the draw reads, then the draw. */
	error = draw_prepare(exec, indexed);
	if (error != 0)
		return;
	put(exec, words, ghci_cmd_draw_indirect(words, indexed ? 1U : 0U, va, (uint32_t)offset, draws, stride));
}

/* Makes the ring hold what a dispatch reads: the compute pipeline and the tables. */
static int
dispatch_prepare(
	struct exec *exec)
{
	struct bind_state *bind;
	uint32_t words[GHCI_DW_BIND_PIPELINE];

	/* A compute pipeline bound, placed when it changed. */
	bind = &exec->state->binds[BIND_COMPUTE];
	if (bind->pipeline == NULL) {
		refuse(exec, EINVAL, "a dispatch without a compute pipeline bound");
		return EINVAL;
	}
	if (bind->pipeline_dirty) {
		put(exec, words, ghci_cmd_bind_pipeline(words, bind->pipeline->pso_va));
		bind->pipeline_dirty = 0;
	}

	/* Succeeded: the tables follow. */
	return tables_place(exec, BIND_COMPUTE);
}

/*
 * vkCmdDispatch: [x][y][z]; vkCmdDispatchIndirect: [buffer][u64 offset],
 * skipped without CAP_INDIRECT_DRAW and shader units.
 */
static void
dispatch(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	uint32_t words[GHCI_DW_DISPATCH_INDIRECT];
	uint32_t groups[3];
	uint64_t identity;
	uint64_t offset;
	uint64_t va;
	uint64_t remaining;
	int indirect;
	int units;
	int error;

	/* The arguments. */
	if (number == GPU_OP_CMD_DISPATCH) {
		groups[0] = read32(reader);
		groups[1] = read32(reader);
		groups[2] = read32(reader);
		if (reader->error != 0)
			return;
		error = dispatch_prepare(exec);
		if (error != 0)
			return;
		put(exec, words, ghci_cmd_dispatch(words, groups[0], groups[1], groups[2]));
		return;
	}

	/* An indirect dispatch needs both flags. */
	identity = read64(reader);
	offset = read64(reader);
	if (reader->error != 0)
		return;
	indirect = ghci_caps_has(&exec->session->device->caps, GHCI_HAS_INDIRECT_DRAW);
	units = ghci_caps_has(&exec->session->device->caps, GHCI_HAS_SHADER_UNITS);
	if (!indirect || !units) {
		skip_command(exec, "an indirect dispatch on a device without CAP_INDIRECT_DRAW");
		return;
	}
	if (buffer_va(exec, identity, 0U, &va, &remaining) != 0 || offset > UINT32_MAX) {
		refuse(exec, EINVAL, "an indirect dispatch from a buffer the session does not have bound");
		return;
	}
	error = dispatch_prepare(exec);
	if (error != 0)
		return;
	put(exec, words, ghci_cmd_dispatch_indirect(words, va, (uint32_t)offset));
}

/*
 * vkCmdPipelineBarrier: [source stages][destination stages][dependency
 * flags], then the barriers.  The stages become the hardware's, and the
 * accesses the caches to flush and invalidate.
 */
static void
barrier(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	uint32_t words[GHCI_DW_BARRIER];
	uint32_t source_stages;
	uint32_t destination_stages;
	uint32_t source_access;
	uint32_t destination_access;

	/* The stages and the barriers' accesses. */
	(void)number;
	source_stages = read32(reader);
	destination_stages = read32(reader);
	(void)read32(reader);
	barriers_read(reader, &source_access, &destination_access);
	if (reader->error != 0)
		return;

	/* The barrier. */
	put(exec, words, ghci_cmd_barrier(words, stages_of(source_stages), stages_of(destination_stages),
		barrier_flags(source_access, destination_access, source_stages, destination_stages)));
}

/*
 * Reads the memory, buffer and image barriers of a barrier or an event
 * wait and gathers their source and destination accesses (layouts mean
 * nothing to linear images).
 */
static void
barriers_read(
	struct i915_wire_reader *reader,
	uint32_t *source,
	uint32_t *destination)
{
	uint64_t count;
	uint64_t index;
	uint32_t kind;

	/* The three arrays: memory [sType][u64 0][src][dst], buffer and image with more after. */
	*source = 0U;
	*destination = 0U;
	for (kind = 0U; kind < 3U && reader->error == 0; kind++) {
		read_skip(reader, 4U);
		count = read_count(reader);
		for (index = 0U; index < count && reader->error == 0; index++) {
			read_skip(reader, 4U + 8U);
			*source |= read32(reader);
			*destination |= read32(reader);
			if (kind == 1U) {
				/* [queue families][buffer][u64 offset][u64 size]. */
				read_skip(reader, 8U + 8U + 16U);
			} else if (kind == 2U) {
				/* [old layout][new layout][queue families][image][the range of five words]. */
				read_skip(reader, 8U + 8U + 8U + 20U);
			}
		}
	}
}

/* Reports the hardware's stages (VF, VS, FS, CS, ROP, BLT) of Vulkan pipeline stages. */
static uint32_t
stages_of(
	uint32_t vulkan)
{
	uint32_t stages;

	/* Each Vulkan stage's unit. */
	stages = 0U;
	if ((vulkan & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)) != 0U)
		stages |= GHCI_STAGE_VF;
	if ((vulkan & (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
		| VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)) != 0U)
		stages |= GHCI_STAGE_VS;
	if ((vulkan & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0U)
		stages |= GHCI_STAGE_FS;
	if ((vulkan & (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
		| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)) != 0U)
		stages |= GHCI_STAGE_ROP;
	if ((vulkan & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0U)
		stages |= GHCI_STAGE_CS;
	if ((vulkan & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0U)
		stages |= GHCI_STAGE_BLT;
	if ((vulkan & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0U)
		stages |= GHCI_STAGE_VF | GHCI_STAGE_VS | GHCI_STAGE_FS | GHCI_STAGE_ROP;
	if ((vulkan & (VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) != 0U)
		stages |= GHCI_STAGE_VF | GHCI_STAGE_VS | GHCI_STAGE_FS | GHCI_STAGE_CS | GHCI_STAGE_ROP | GHCI_STAGE_BLT;

	/* Succeeded: the units. */
	return stages;
}

/*
 * Reports a barrier's cache operations: writes before it are flushed
 * from L1 (and from L2 for the host to read, after the units are idle);
 * reads after it see memory again (the texture cache for shader and
 * input attachment reads); what the host wrote invalidates the caches.
 */
static uint32_t
barrier_flags(
	uint32_t source_access,
	uint32_t destination_access,
	uint32_t source_stages,
	uint32_t destination_stages)
{
	uint32_t writes;
	uint32_t reads;
	uint32_t flags;

	/* The accesses that matter. */
	writes = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	reads = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
		| VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT
		| VK_ACCESS_MEMORY_READ_BIT;

	/* Writes before, reads after, and the host on either side. */
	flags = 0U;
	if ((source_access & writes) != 0U)
		flags |= GHCI_BARRIER_FLUSH_L1_DATA;
	if ((destination_access & reads) != 0U)
		flags |= GHCI_BARRIER_INVAL_L1_DATA;
	if ((destination_access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_MEMORY_READ_BIT)) != 0U)
		flags |= GHCI_BARRIER_INVAL_TEX;
	if ((source_access & VK_ACCESS_HOST_WRITE_BIT) != 0U || (source_stages & VK_PIPELINE_STAGE_HOST_BIT) != 0U)
		flags |= GHCI_BARRIER_INVAL_L1_DATA | GHCI_BARRIER_INVAL_TEX | GHCI_BARRIER_INVAL_DESCRIPTOR;
	if ((destination_access & VK_ACCESS_HOST_READ_BIT) != 0U || (destination_stages & VK_PIPELINE_STAGE_HOST_BIT) != 0U)
		flags |= GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_FLUSH_L2;

	/* Succeeded: the flags. */
	return flags;
}

/*
 * vkCmdBeginRenderPass: [u64 present][sType][u64 0][render pass]
 * [framebuffer][x][y][width][height][count][u64 count][the tagged clear
 * values][contents].  Its first subpass begins.
 */
static void
begin_pass(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *pass;
	struct ghci_vk_object *framebuffer;
	uint32_t value[4];
	uint64_t present;
	uint64_t count;
	uint64_t index;

	/* The begin info. */
	present = read64(reader);
	if (present != 1U) {
		refuse(exec, EINVAL, "a render pass begun without its begin info");
		return;
	}
	read_skip(reader, 4U + 8U);
	pass = ghci_vk_object_find(exec->session, GHCI_VK_RENDER_PASS, read64(reader));
	framebuffer = ghci_vk_object_find(exec->session, GHCI_VK_FRAMEBUFFER, read64(reader));
	for (index = 0U; index < 4U; index++)
		exec->pass.area[index] = read32(reader);
	read_skip(reader, 4U);
	count = read_count(reader);
	exec->pass.clear_count = 0U;
	for (index = 0U; index < count && reader->error == 0; index++) {
		read_clear_value(reader, value);
		if (index < CLEAR_VALUES_MAX) {
			kern_memcpy(exec->pass.clear[index], value, sizeof(value));
			exec->pass.clear_count++;
		}
	}
	(void)read32(reader);
	if (reader->error != 0)
		return;

	/* A pass and a framebuffer of the session with as many attachments, outside a pass. */
	if (exec->pass.active || pass == NULL || framebuffer == NULL || pass->as.render_pass == NULL) {
		refuse(exec, EINVAL, "a render pass begun with objects the session does not have");
		return;
	}
	if (framebuffer->as.framebuffer.attachment_count != pass->as.render_pass->attachment_count) {
		refuse(exec, EINVAL, "a framebuffer whose attachments are not its render pass's");
		return;
	}
	if (exec->pass.area[0] + exec->pass.area[2] > framebuffer->as.framebuffer.width
		|| exec->pass.area[1] + exec->pass.area[3] > framebuffer->as.framebuffer.height) {
		refuse(exec, EINVAL, "a render area past its framebuffer");
		return;
	}
	if (framebuffer->as.framebuffer.layers > 1U)
		skip_command(exec, "a framebuffer of more than one layer (layer 0 alone is drawn)");

	/* The first subpass. */
	exec->pass.active = 1;
	exec->pass.pass = pass->as.render_pass;
	exec->pass.framebuffer = &framebuffer->as.framebuffer;
	exec->pass.subpass = 0U;
	subpass_begin(exec, NULL);
}

/* Reads a tagged clear value: [u32 0][u32 2][u64 4][4 words] or [u32 1][float depth][u32 stencil]. */
static void
read_clear_value(
	struct i915_wire_reader *reader,
	uint32_t *value)
{
	uint32_t tag;
	uint32_t index;

	/* The tag says which. */
	kern_memset(value, 0, 4U * sizeof(*value));
	tag = read32(reader);
	if (tag == CLEAR_VALUE_COLOR) {
		if (read32(reader) != CLEAR_COLOR_TAG || read_count(reader) != CLEAR_COLOR_WORDS)
			reader->error = 1;
		for (index = 0U; index < CLEAR_COLOR_WORDS; index++)
			value[index] = read32(reader);
	} else if (tag == CLEAR_VALUE_DEPTH) {
		value[0] = read32(reader);
		value[1] = read32(reader);
	} else {
		reader->error = 1;
	}
}

/*
 * Begins the current subpass: its render pass description (the
 * attachments of the framebuffer's views, linear, the clear colours of
 * the attachments it clears in a table, colour 0's resolve) in temporary
 * memory, and BEGIN_RENDER_PASS over the render area (the stencil is
 * cleared only for a format that has one).  An attachment is
 * loaded or cleared by its operation in the first subpass that uses it
 * and loaded in the later ones; stored, unless the last subpass that uses
 * it does not store it.  clear (vkCmdClearAttachments) begins it again
 * loading everything but what it clears.
 */
static void
subpass_begin(
	struct exec *exec,
	const struct pass_clear *clear)
{
	const struct ghci_vk_pass_attachment *attachment;
	const struct ghci_vk_pass_references *references;
	const struct ghci_vk_image_view *view;
	const struct ghci_vk_subpass *subpass;
	struct ghci_render_pass_desc desc;
	uint32_t words[GHCI_DW_BEGIN_RENDER_PASS];
	uint32_t traits;
	uint32_t flags;
	uint32_t depth_value;
	uint32_t stencil_value;
	uint32_t load;
	uint32_t index;
	uint32_t at;
	uint64_t table_va;
	uint64_t desc_va;
	uint8_t *table;
	uint8_t *view_bytes;
	int stencil;
	int error;

	/* The subpass and what it names. */
	subpass = &exec->pass.pass->subpasses[exec->pass.subpass];
	references = &exec->pass.pass->references[exec->pass.subpass];
	kern_memset(&desc, 0, sizeof(desc));
	flags = 0U;
	depth_value = 0U;
	stencil_value = 0U;
	error = temp_take(exec, CLEAR_TABLE_BYTES, 16U, &table_va, &table);
	if (error != 0)
		return;

	/* Each colour attachment, its operations and its clear colour. */
	for (index = 0U; index < references->color_count; index++) {
		at = references->color[index];
		if (at == VK_ATTACHMENT_UNUSED)
			continue;
		error = view_of(exec, at, &view);
		if (error != 0)
			return;
		attachment = &exec->pass.pass->attachments[at];
		load = pass_load(attachment, exec->pass.subpass, attachment->load);
		if (clear != NULL)
			load = (clear->colors & (1U << index)) != 0U ? (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR : (uint32_t)VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.colors[index].va = view->va;
		desc.colors[index].row_pitch = view->row_pitch;
		desc.colors[index].format = attachment->format;
		desc.colors[index].ops = load | (pass_store(attachment, exec->pass.subpass, attachment->store) << 2);
		if (load != (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR)
			continue;
		flags |= 1U << index;
		if (clear != NULL)
			kern_memcpy(table + index * 16U, clear->color[index], 16U);
		else if (at < exec->pass.clear_count)
			kern_memcpy(table + index * 16U, exec->pass.clear[at], 16U);
	}

	/* The depth and stencil attachment. */
	at = references->depth;
	if (at != VK_ATTACHMENT_UNUSED) {
		error = view_of(exec, at, &view);
		if (error != 0)
			return;
		attachment = &exec->pass.pass->attachments[at];
		desc.depth_stencil.va = view->va;
		desc.depth_stencil.row_pitch = view->row_pitch;
		desc.depth_stencil.format = attachment->format;
		load = pass_load(attachment, exec->pass.subpass, attachment->load);
		if (clear != NULL)
			load = clear->depth ? (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR : (uint32_t)VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.depth_stencil.ops = load | (pass_store(attachment, exec->pass.subpass, attachment->store) << 2);
		if (load == (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR)
			flags |= GHCI_CLEAR_DEPTH;
		load = pass_load(attachment, exec->pass.subpass, attachment->stencil_load);
		if (clear != NULL)
			load = clear->stencil ? (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR : (uint32_t)VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.depth_stencil.ops |= (load << 4) | (pass_store(attachment, exec->pass.subpass, attachment->stencil_store) << 6);
		stencil = attachment->format >= (uint32_t)VK_FORMAT_S8_UINT && attachment->format <= (uint32_t)VK_FORMAT_D32_SFLOAT_S8_UINT;
		if (load == (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR && stencil)
			flags |= GHCI_CLEAR_STENCIL;
		if (clear != NULL) {
			depth_value = clear->depth_value;
			stencil_value = clear->stencil_value;
		} else if (at < exec->pass.clear_count) {
			depth_value = exec->pass.clear[at][0];
			stencil_value = exec->pass.clear[at][1];
		}
	}

	/* Colour 0's resolve: the description's (the others after the subpass). */
	at = references->resolve[0];
	if (references->color_count != 0U && at != VK_ATTACHMENT_UNUSED) {
		error = view_of(exec, at, &view);
		if (error != 0)
			return;
		(void)ghci_vk_format_traits(exec->pass.pass->attachments[at].format, &traits);
		desc.resolve.va = view->va;
		desc.resolve.row_pitch = view->row_pitch;
		desc.resolve.format = exec->pass.pass->attachments[at].format;
		desc.resolve.ops = (traits & GHCI_VK_FORMAT_INTEGER) != 0U ? 3U : 0U;
	}

	/* The size, one layer, the samples and the clear colours. */
	desc.width = exec->pass.framebuffer->width;
	desc.height = exec->pass.framebuffer->height;
	desc.layers = 1U;
	desc.samples = subpass->samples;
	desc.clear_table = table_va;

	/* The description in temporary memory, then the packet. */
	error = temp_take(exec, GHCI_RENDER_PASS_BYTES, DESC_ALIGN, &desc_va, &view_bytes);
	if (error != 0)
		return;
	error = ghci_render_pass_desc_encode(view_bytes, &desc);
	if (error != 0) {
		refuse(exec, EINVAL, "a subpass the render pass description cannot hold");
		return;
	}
	put(exec, words, ghci_cmd_begin_render_pass(words, desc_va, exec->pass.area, 0U, depth_value, stencil_value, flags));
}

/*
 * Ends the current subpass, and resolves its colour attachments 1 to 7
 * with RESOLVE_IMAGE over the render area (colour 0's is the
 * description's); a device without the blitter skips them.
 */
static void
subpass_end(
	struct exec *exec)
{
	const struct ghci_vk_pass_references *references;
	const struct ghci_vk_image_view *source;
	const struct ghci_vk_image_view *destination;
	struct ghci_transfer transfer;
	uint32_t words[GHCI_DW_RESOLVE_IMAGE];
	uint32_t traits;
	uint32_t index;
	int has;

	/* The subpass ends. */
	put(exec, words, ghci_cmd_end_render_pass(words));

	/* The resolves of colours 1 to 7. */
	references = &exec->pass.pass->references[exec->pass.subpass];
	has = ghci_caps_has(&exec->session->device->caps, GHCI_HAS_BLT);
	for (index = 1U; index < references->color_count; index++) {
		if (references->resolve[index] == VK_ATTACHMENT_UNUSED || references->color[index] == VK_ATTACHMENT_UNUSED)
			continue;
		if (!has) {
			skip_command(exec, "a resolve of colour attachment 1 to 7 on a device without the blitter");
			continue;
		}
		if (view_of(exec, references->color[index], &source) != 0 || view_of(exec, references->resolve[index], &destination) != 0)
			return;
		(void)ghci_vk_format_traits(exec->pass.pass->attachments[references->color[index]].format, &traits);
		kern_memset(&transfer, 0, sizeof(transfer));
		transfer.src.va = source->va;
		transfer.src.pitch = source->row_pitch;
		transfer.src.x = exec->pass.area[0];
		transfer.src.y = exec->pass.area[1];
		transfer.dst.va = destination->va;
		transfer.dst.pitch = destination->row_pitch;
		transfer.dst.x = exec->pass.area[0];
		transfer.dst.y = exec->pass.area[1];
		transfer.width = exec->pass.area[2];
		transfer.height = exec->pass.area[3];
		transfer.format = exec->pass.pass->attachments[references->color[index]].format;
		transfer.resolve_mode = (traits & GHCI_VK_FORMAT_INTEGER) != 0U ? 3U : 0U;
		transfer.samples = exec->pass.pass->attachments[references->color[index]].samples;
		put(exec, words, ghci_cmd_resolve_image(words, &transfer));
	}
}

/* Finds the framebuffer's view of an attachment.  Returns 0, or EINVAL (the submission is refused). */
static int
view_of(
	struct exec *exec,
	uint32_t attachment,
	const struct ghci_vk_image_view **view)
{
	struct ghci_vk_object *object;

	/* The view, still of the session. */
	object = ghci_vk_object_find(exec->session, GHCI_VK_IMAGE_VIEW, exec->pass.framebuffer->views[attachment]);
	if (object == NULL) {
		refuse(exec, EINVAL, "a framebuffer whose view the session no longer has");
		return EINVAL;
	}

	/* Succeeded: the view. */
	*view = &object->as.image_view;
	return 0;
}

/* Reports an attachment's load operation in a subpass: its own in the first that uses it, LOAD after. */
static uint32_t
pass_load(
	const struct ghci_vk_pass_attachment *attachment,
	uint32_t subpass,
	uint32_t load)
{
	/* The first use loads as the attachment says. */
	if (attachment->first == subpass)
		return load;

	/* Succeeded: what an earlier subpass stored. */
	return (uint32_t)VK_ATTACHMENT_LOAD_OP_LOAD;
}

/* Reports an attachment's store operation in a subpass: its own in the last that uses it, STORE before. */
static uint32_t
pass_store(
	const struct ghci_vk_pass_attachment *attachment,
	uint32_t subpass,
	uint32_t store)
{
	/* The last use stores as the attachment says. */
	if (attachment->last == subpass)
		return store;

	/* Succeeded: a later subpass loads it. */
	return (uint32_t)VK_ATTACHMENT_STORE_OP_STORE;
}

/*
 * vkCmdClearAttachments: [count][u64 count][each: aspect, attachment,
 * tagged value][count][u64 count][rects: x, y, width, height, layer,
 * layers].  A clear of the whole render area of layer 0 ends the subpass
 * and begins it again clearing those attachments; a partial clear is
 * skipped.
 */
static void
clear_attachments(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	struct pass_clear clear;
	uint32_t value[4];
	uint32_t aspect;
	uint32_t attachment;
	uint32_t rect[6];
	uint64_t count;
	uint64_t index;
	uint32_t at;
	int whole;

	/* The attachments and their values. */
	kern_memset(&clear, 0, sizeof(clear));
	read_skip(reader, 4U);
	count = read_count(reader);
	for (index = 0U; index < count && reader->error == 0; index++) {
		aspect = read32(reader);
		attachment = read32(reader);
		read_clear_value(reader, value);
		if ((aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0U && attachment < GHCI_VK_COLOR_ATTACHMENTS) {
			clear.colors |= 1U << attachment;
			kern_memcpy(clear.color[attachment], value, sizeof(value));
		}
		if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0U) {
			clear.depth = 1;
			clear.depth_value = value[0];
		}
		if ((aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U) {
			clear.stencil = 1;
			clear.stencil_value = value[1];
		}
	}

	/* The rectangles: each the whole render area of layer 0. */
	whole = 1;
	read_skip(reader, 4U);
	count = read_count(reader);
	for (index = 0U; index < count && reader->error == 0; index++) {
		for (at = 0U; at < 6U; at++)
			rect[at] = read32(reader);
		if (rect[0] != exec->pass.area[0] || rect[1] != exec->pass.area[1] || rect[2] != exec->pass.area[2]
			|| rect[3] != exec->pass.area[3] || rect[4] != 0U || rect[5] != 1U)
			whole = 0;
	}
	if (reader->error != 0)
		return;
	if (!exec->pass.active) {
		refuse(exec, EINVAL, "a clear of attachments outside a render pass");
		return;
	}
	if (!whole) {
		skip_command(exec, "a clear of part of the attachments");
		return;
	}

	/* The subpass again, clearing them. */
	subpass_end(exec);
	subpass_begin(exec, &clear);
}

/*
 * vkCmdClearColorImage: [image][layout][u64 present][the colour][count]
 * [u64 count][ranges]; vkCmdClearDepthStencilImage: [image][layout]
 * [u64 present][float depth][u32 stencil][count][u64 count][ranges].
 * Each level and layer (or slice of a 3D image) of each range is
 * cleared by a render pass of its own that only clears; an image whose
 * format cannot be an attachment is skipped.
 */
static void
clear_image(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	const struct ghci_vk_image *image;
	struct ghci_vk *vk;
	uint32_t value[4];
	uint32_t range[5];
	uint32_t features;
	uint32_t levels;
	uint32_t layers;
	uint32_t level;
	uint32_t layer;
	uint32_t slices;
	uint64_t identity;
	uint64_t base;
	uint64_t count;
	uint64_t index;
	uint32_t at;
	int depth;

	/* The image, its value and the ranges. */
	identity = read64(reader);
	read_skip(reader, 4U);
	kern_memset(value, 0, sizeof(value));
	depth = number == GPU_OP_CMD_CLEAR_DEPTH_STENCIL_IMAGE;
	if (read64(reader) != 0U) {
		if (depth) {
			value[0] = read32(reader);
			value[1] = read32(reader);
		} else {
			if (read32(reader) != CLEAR_COLOR_TAG || read_count(reader) != CLEAR_COLOR_WORDS)
				reader->error = 1;
			for (at = 0U; at < CLEAR_COLOR_WORDS; at++)
				value[at] = read32(reader);
		}
	}
	read_skip(reader, 4U);
	count = read_count(reader);
	if (reader->error != 0)
		return;

	/* An image of the session, bound, outside a render pass. */
	if (exec->pass.active || image_of(exec, identity, &image, &base) != 0) {
		refuse(exec, EINVAL, "a clear of an image the session does not have bound, or inside a render pass");
		return;
	}
	vk = exec->session->device->vk;
	features = 0U;
	if (image->format < GHCI_VK_FORMATS)
		features = vk->formats.linear[image->format];
	if ((!depth && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0U)
		|| (depth && (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0U)) {
		for (index = 0U; index < count && reader->error == 0; index++)
			read_skip(reader, 20U);
		skip_command(exec, "a clear of an image whose format cannot be an attachment");
		return;
	}

	/* Each range's levels and layers. */
	for (index = 0U; index < count && reader->error == 0 && exec->error == 0; index++) {
		for (at = 0U; at < 5U; at++)
			range[at] = read32(reader);
		levels = range[2] == VK_REMAINING_MIP_LEVELS ? image->layout.levels - range[1] : range[2];
		layers = range[4] == VK_REMAINING_ARRAY_LAYERS ? image->layout.layers - range[3] : range[4];
		if (range[1] >= image->layout.levels || levels > image->layout.levels - range[1] || range[3] >= image->layout.layers
			|| layers > image->layout.layers - range[3]) {
			refuse(exec, EINVAL, "a clear range past its image");
			return;
		}
		for (level = range[1]; level < range[1] + levels; level++) {
			slices = layers;
			if (image->type == (uint32_t)VK_IMAGE_TYPE_3D)
				slices = ghci_layout_extent(image->layout.depth, level);
			for (layer = 0U; layer < slices; layer++) {
				clear_slice(exec, image->format, image->type == (uint32_t)VK_IMAGE_TYPE_3D
					? image_slice(image, base, level, 0U, layer) : image_slice(image, base, level, range[3] + layer, 0U),
					ghci_layout_row(&image->layout, level), ghci_layout_extent(image->layout.width, level),
					ghci_layout_extent(image->layout.height, level), image->layout.samples, depth, value, range[0]);
			}
		}
	}
}

/*
 * Clears one slice of an image with a render pass that only clears: the
 * slice as colour attachment 0 (or the depth and stencil attachment,
 * clearing the aspects named), stored.
 */
static void
clear_slice(
	struct exec *exec,
	uint32_t format,
	uint64_t va,
	uint32_t pitch,
	uint32_t width,
	uint32_t height,
	uint32_t samples,
	int depth_attachment,
	const uint32_t *value,
	uint32_t aspects)
{
	struct ghci_render_pass_desc desc;
	uint32_t words[GHCI_DW_BEGIN_RENDER_PASS];
	uint32_t area[4];
	uint32_t flags;
	uint64_t desc_va;
	uint64_t table_va;
	uint8_t *view;
	uint8_t *table;
	int error;

	/* The description: one attachment, cleared where named and loaded elsewhere, stored. */
	kern_memset(&desc, 0, sizeof(desc));
	flags = 0U;
	table_va = 0U;
	if (depth_attachment) {
		desc.depth_stencil.va = va;
		desc.depth_stencil.row_pitch = pitch;
		desc.depth_stencil.format = format;
		if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0U) {
			desc.depth_stencil.ops |= (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR;
			flags |= GHCI_CLEAR_DEPTH;
		}
		if ((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U) {
			desc.depth_stencil.ops |= (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR << 4;
			flags |= GHCI_CLEAR_STENCIL;
		}
	} else {
		error = temp_take(exec, CLEAR_TABLE_BYTES, 16U, &table_va, &table);
		if (error != 0)
			return;
		kern_memcpy(table, value, 16U);
		desc.colors[0].va = va;
		desc.colors[0].row_pitch = pitch;
		desc.colors[0].format = format;
		desc.colors[0].ops = (uint32_t)VK_ATTACHMENT_LOAD_OP_CLEAR;
		flags |= 1U;
	}
	desc.width = width;
	desc.height = height;
	desc.layers = 1U;
	desc.samples = samples;
	desc.clear_table = table_va;

	/* The description, then the pass over the whole slice. */
	error = temp_take(exec, GHCI_RENDER_PASS_BYTES, DESC_ALIGN, &desc_va, &view);
	if (error != 0)
		return;
	if (ghci_render_pass_desc_encode(view, &desc) != 0) {
		refuse(exec, EINVAL, "a clear of a slice the render pass description cannot hold");
		return;
	}
	area[0] = 0U;
	area[1] = 0U;
	area[2] = width;
	area[3] = height;
	put(exec, words, ghci_cmd_begin_render_pass(words, desc_va, area, 0U, value[0], value[1], flags));
	put(exec, words, ghci_cmd_end_render_pass(words));
}

/* Finds an image of the session bound to memory, and its address.  Returns 0, or ENOENT. */
static int
image_of(
	struct exec *exec,
	uint64_t identity,
	const struct ghci_vk_image **image,
	uint64_t *va)
{
	struct ghci_vk_object *object;
	int error;

	/* An image of the session, bound. */
	object = ghci_vk_object_find(exec->session, GHCI_VK_IMAGE, identity);
	if (object == NULL || object->as.image.memory == 0U)
		return ENOENT;
	error = ghci_vk_bound_va(exec->session, object->as.image.memory, object->as.image.offset, va);
	if (error != 0)
		return error;

	/* Succeeded: the image and its address. */
	*image = &object->as.image;
	return 0;
}

/* Reports the address of one slice of an image: a level of a layer, and a depth slice of a 3D level. */
static uint64_t
image_slice(
	const struct ghci_vk_image *image,
	uint64_t base,
	uint32_t level,
	uint32_t layer,
	uint32_t z)
{
	/* The layer, the level in it, and the plane of the slice (section 8.5.2 of the specification). */
	return base + (uint64_t)layer * ghci_layout_layer_stride(&image->layout) + ghci_layout_level_offset(&image->layout, level)
		+ (uint64_t)z * ghci_layout_plane(&image->layout, level);
}

/*
 * Reports whether a transfer may be placed: the device has the blitter.
 * Without it the submission is refused (the shader's transfers come
 * later).
 */
static int
transfer_allowed(
	struct exec *exec)
{
	int has;

	/* The blitter. */
	has = ghci_caps_has(&exec->session->device->caps, GHCI_HAS_BLT);
	if (!has) {
		refuse(exec, ENOTSUP, "a transfer on a device without the blitter");
		return 0;
	}

	/* Succeeded: the blitter takes it. */
	return 1;
}

/* vkCmdCopyBuffer: [source][destination][count][u64 count][regions: source offset, destination offset, size]. */
static void
copy_buffer(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	uint64_t source_id;
	uint64_t destination_id;
	uint64_t source;
	uint64_t destination;
	uint64_t source_left;
	uint64_t destination_left;
	uint64_t region[3];
	uint64_t count;
	uint64_t index;
	uint32_t at;

	/* The buffers and the regions. */
	source_id = read64(reader);
	destination_id = read64(reader);
	read_skip(reader, 4U);
	count = read_count(reader);
	if (reader->error != 0 || !transfer_allowed(exec))
		return;

	/* Each region within both buffers. */
	for (index = 0U; index < count && reader->error == 0 && exec->error == 0; index++) {
		for (at = 0U; at < 3U; at++)
			region[at] = read64(reader);
		if (buffer_va(exec, source_id, region[0], &source, &source_left) != 0
			|| buffer_va(exec, destination_id, region[1], &destination, &destination_left) != 0
			|| region[2] > source_left || region[2] > destination_left) {
			refuse(exec, EINVAL, "a copy of buffers the session does not have bound, or past them");
			return;
		}
		copy_bytes(exec, source, destination, region[2]);
	}
}

/*
 * Copies bytes between GPU addresses with COPY_BUFFER (design R11):
 * COPY_BUFFER moves whole words, so a copy whose ends are not on 4 bytes
 * moves its aligned middle (when both addresses share their alignment)
 * and leaves the bytes at the ends to the shader's copy, which this
 * device does not have yet: those are skipped.
 */
static void
copy_bytes(
	struct exec *exec,
	uint64_t source,
	uint64_t destination,
	uint64_t bytes)
{
	uint32_t words[GHCI_DW_COPY_BUFFER];
	uint64_t head;
	uint64_t middle;
	uint64_t piece;

	/* Addresses that cannot share a word boundary have no middle. */
	if ((source & 3U) != (destination & 3U)) {
		skip_command(exec, "a copy of buffers whose offsets differ within a word");
		return;
	}

	/* The head to the first word boundary, the whole words, and the tail. */
	head = (4U - (source & 3U)) & 3U;
	if (head > bytes)
		head = bytes;
	middle = (bytes - head) & ~(uint64_t)3U;
	if (head != 0U || bytes - head - middle != 0U)
		skip_command(exec, "the bytes of a copy that are not whole words");

	/* The middle in pieces COPY_BUFFER takes. */
	source += head;
	destination += head;
	while (middle != 0U && exec->error == 0) {
		piece = middle > TRANSFER_BYTES_MAX ? TRANSFER_BYTES_MAX : middle;
		put(exec, words, ghci_cmd_copy_buffer(words, source, destination, (uint32_t)piece, 0U, 0U));
		source += piece;
		destination += piece;
		middle -= piece;
	}
}

/* vkCmdFillBuffer: [buffer][u64 offset][u64 size][data]; VK_WHOLE_SIZE fills to the last whole word. */
static void
fill_buffer(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	uint32_t words[GHCI_DW_FILL_BUFFER];
	uint64_t identity;
	uint64_t offset;
	uint64_t size;
	uint64_t va;
	uint64_t remaining;
	uint64_t piece;
	uint32_t data;

	/* The arguments. */
	identity = read64(reader);
	offset = read64(reader);
	size = read64(reader);
	data = read32(reader);
	if (reader->error != 0 || !transfer_allowed(exec))
		return;

	/* A buffer of the session, a range of whole words in it. */
	if (buffer_va(exec, identity, offset, &va, &remaining) != 0 || (offset & 3U) != 0U) {
		refuse(exec, EINVAL, "a fill of a buffer the session does not have bound");
		return;
	}
	if (size == VK_WHOLE_SIZE)
		size = remaining & ~(uint64_t)3U;
	if (size > remaining || (size & 3U) != 0U) {
		refuse(exec, EINVAL, "a fill past its buffer or of part of a word");
		return;
	}

	/* The fill in pieces FILL_BUFFER takes. */
	while (size != 0U && exec->error == 0) {
		piece = size > TRANSFER_BYTES_MAX ? TRANSFER_BYTES_MAX : size;
		put(exec, words, ghci_cmd_fill_buffer(words, va, (uint32_t)piece, data));
		va += piece;
		size -= piece;
	}
}

/*
 * vkCmdUpdateBuffer: [buffer][u64 offset][u64 size][u64 count][the bytes,
 * padded to words]: the bytes are written to temporary memory and copied
 * to the buffer.
 */
static void
update_buffer(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	uint32_t words[GHCI_DW_COPY_BUFFER];
	const uint8_t *bytes;
	uint64_t identity;
	uint64_t offset;
	uint64_t size;
	uint64_t count;
	uint64_t va;
	uint64_t remaining;
	uint64_t source;
	uint8_t *view;
	int error;

	/* The arguments and the bytes. */
	identity = read64(reader);
	offset = read64(reader);
	size = read64(reader);
	count = read_count(reader);
	bytes = drv_i915_wire_read_array(reader, (size_t)((count + 3U) & ~(uint64_t)3U), 1U);
	if (reader->error != 0 || !transfer_allowed(exec))
		return;

	/* Whole words within the buffer and the 64 KiB Vulkan allows. */
	if (bytes == NULL || count != size || size == 0U || size > UPDATE_BYTES_MAX || (size & 3U) != 0U || (offset & 3U) != 0U
		|| buffer_va(exec, identity, offset, &va, &remaining) != 0 || size > remaining) {
		refuse(exec, EINVAL, "an update of a buffer past its range or of part of a word");
		return;
	}

	/* The bytes in temporary memory, then the copy. */
	error = temp_take(exec, (uint32_t)size, 4U, &source, &view);
	if (error != 0)
		return;
	kern_memcpy(view, bytes, (size_t)size);
	put(exec, words, ghci_cmd_copy_buffer(words, source, va, (uint32_t)size, 0U, 0U));
}

/*
 * vkCmdCopyImage, vkCmdBlitImage and vkCmdResolveImage: [source][layout]
 * [destination][layout][count][u64 count][regions] (and a blit's
 * filter).  Each region becomes one packet per layer (a 3D image's
 * depth slices for a copy).
 */
static void
copy_image(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	const struct ghci_vk_image *source;
	const struct ghci_vk_image *destination;
	struct ghci_transfer transfer;
	VkImageSubresourceLayers layers[2];
	uint32_t words[GHCI_DW_BLIT];
	uint32_t traits;
	uint32_t extent[3];
	uint32_t filter;
	uint32_t slices;
	uint32_t slice;
	int32_t offsets[4][3];
	uint64_t source_id;
	uint64_t destination_id;
	uint64_t source_base;
	uint64_t destination_base;
	uint64_t count;
	uint64_t index;
	size_t regions;
	uint32_t at;

	/* The images and the regions (read after the filter that follows them is found). */
	source_id = read64(reader);
	read_skip(reader, 4U);
	destination_id = read64(reader);
	read_skip(reader, 4U);
	read_skip(reader, 4U);
	count = read_count(reader);
	regions = reader->offset;
	if (reader->error != 0)
		return;
	if (!transfer_allowed(exec) || image_of(exec, source_id, &source, &source_base) != 0
		|| image_of(exec, destination_id, &destination, &destination_base) != 0) {
		refuse(exec, exec->error != 0 ? exec->error : EINVAL, "a copy of images the session does not have bound");
		return;
	}

	/* Each region. */
	filter = 0U;
	for (index = 0U; index < count && reader->error == 0 && exec->error == 0; index++) {
		/* The region: [source layers][offset, or u64 2 and two][destination layers][the same][extent]. */
		read_layers(reader, &layers[0]);
		if (number == GPU_OP_CMD_BLIT_IMAGE && read_count(reader) != 2U)
			reader->error = 1;
		read_offset(reader, offsets[0]);
		if (number == GPU_OP_CMD_BLIT_IMAGE)
			read_offset(reader, offsets[1]);
		read_layers(reader, &layers[1]);
		if (number == GPU_OP_CMD_BLIT_IMAGE && read_count(reader) != 2U)
			reader->error = 1;
		read_offset(reader, offsets[2]);
		if (number == GPU_OP_CMD_BLIT_IMAGE) {
			read_offset(reader, offsets[3]);
		} else {
			for (at = 0U; at < 3U; at++)
				extent[at] = read32(reader);
		}
		if (reader->error != 0)
			break;

		/* The layers (a 3D copy's depth slices). */
		slices = layers[0].layerCount;
		if (number == GPU_OP_CMD_COPY_IMAGE && source->type == (uint32_t)VK_IMAGE_TYPE_3D)
			slices = extent[2];
		if (layers[0].mipLevel >= source->layout.levels || layers[1].mipLevel >= destination->layout.levels) {
			refuse(exec, EINVAL, "a copy of a level its image does not have");
			return;
		}
		for (slice = 0U; slice < slices && exec->error == 0; slice++) {
			kern_memset(&transfer, 0, sizeof(transfer));
			if (source->type == (uint32_t)VK_IMAGE_TYPE_3D)
				transfer.src.va = image_slice(source, source_base, layers[0].mipLevel, 0U, (uint32_t)offsets[0][2] + slice);
			else
				transfer.src.va = image_slice(source, source_base, layers[0].mipLevel, layers[0].baseArrayLayer + slice, 0U);
			if (destination->type == (uint32_t)VK_IMAGE_TYPE_3D)
				transfer.dst.va = image_slice(destination, destination_base, layers[1].mipLevel, 0U, (uint32_t)offsets[2][2] + slice);
			else
				transfer.dst.va = image_slice(destination, destination_base, layers[1].mipLevel, layers[1].baseArrayLayer + slice, 0U);
			transfer.src.pitch = ghci_layout_row(&source->layout, layers[0].mipLevel);
			transfer.dst.pitch = ghci_layout_row(&destination->layout, layers[1].mipLevel);
			transfer.src.x = (uint32_t)offsets[0][0];
			transfer.src.y = (uint32_t)offsets[0][1];
			transfer.format = source->format;
			if (number == GPU_OP_CMD_BLIT_IMAGE) {
				transfer.src_x1 = (uint32_t)offsets[1][0];
				transfer.src_y1 = (uint32_t)offsets[1][1];
				transfer.dst.x = (uint32_t)offsets[2][0];
				transfer.dst.y = (uint32_t)offsets[2][1];
				transfer.dst_x1 = (uint32_t)offsets[3][0];
				transfer.dst_y1 = (uint32_t)offsets[3][1];
				transfer.dst_format = destination->format;
				transfer.filter = filter;
				continue;
			}
			transfer.dst.x = (uint32_t)offsets[2][0];
			transfer.dst.y = (uint32_t)offsets[2][1];
			transfer.width = extent[0];
			transfer.height = extent[1];
			if (number == GPU_OP_CMD_RESOLVE_IMAGE) {
				(void)ghci_vk_format_traits(source->format, &traits);
				transfer.resolve_mode = (traits & GHCI_VK_FORMAT_INTEGER) != 0U ? 3U : 0U;
				transfer.samples = source->layout.samples;
				put(exec, words, ghci_cmd_resolve_image(words, &transfer));
			} else {
				put(exec, words, ghci_cmd_copy_image(words, &transfer));
			}
		}
	}

	/* A blit's filter follows its regions; the blits are placed with it. */
	if (number != GPU_OP_CMD_BLIT_IMAGE || reader->error != 0 || exec->error != 0)
		return;
	filter = read32(reader) == (uint32_t)VK_FILTER_LINEAR ? 1U : 0U;
	reader->offset = regions;
	for (index = 0U; index < count && reader->error == 0 && exec->error == 0; index++) {
		read_layers(reader, &layers[0]);
		(void)read_count(reader);
		read_offset(reader, offsets[0]);
		read_offset(reader, offsets[1]);
		read_layers(reader, &layers[1]);
		(void)read_count(reader);
		read_offset(reader, offsets[2]);
		read_offset(reader, offsets[3]);
		if (source->type == (uint32_t)VK_IMAGE_TYPE_3D || destination->type == (uint32_t)VK_IMAGE_TYPE_3D) {
			skip_command(exec, "a blit of a 3D image");
			continue;
		}
		for (slice = 0U; slice < layers[0].layerCount && exec->error == 0; slice++) {
			kern_memset(&transfer, 0, sizeof(transfer));
			transfer.src.va = image_slice(source, source_base, layers[0].mipLevel, layers[0].baseArrayLayer + slice, 0U);
			transfer.dst.va = image_slice(destination, destination_base, layers[1].mipLevel, layers[1].baseArrayLayer + slice, 0U);
			transfer.src.pitch = ghci_layout_row(&source->layout, layers[0].mipLevel);
			transfer.dst.pitch = ghci_layout_row(&destination->layout, layers[1].mipLevel);
			transfer.src.x = (uint32_t)offsets[0][0];
			transfer.src.y = (uint32_t)offsets[0][1];
			transfer.src_x1 = (uint32_t)offsets[1][0];
			transfer.src_y1 = (uint32_t)offsets[1][1];
			transfer.dst.x = (uint32_t)offsets[2][0];
			transfer.dst.y = (uint32_t)offsets[2][1];
			transfer.dst_x1 = (uint32_t)offsets[3][0];
			transfer.dst_y1 = (uint32_t)offsets[3][1];
			transfer.format = source->format;
			transfer.dst_format = destination->format;
			transfer.filter = filter;
			put(exec, words, ghci_cmd_blit(words, &transfer));
		}
	}
	(void)read32(reader);
}

/*
 * vkCmdCopyBufferToImage: [buffer][image][layout][count][u64 count]
 * [regions]; vkCmdCopyImageToBuffer: [image][layout][buffer][count]
 * [u64 count][regions].  A region is [u64 offset][row length][image
 * height][layers][offset][extent]; each layer (or depth slice) is one
 * packet whose buffer row pitch and slice come from the row length and
 * image height (in blocks for a compressed format).  One aspect of a
 * combined depth and stencil format is skipped (the packets cannot pick
 * it).
 */
static void
copy_buffer_image(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	const struct ghci_vk_image *image;
	struct ghci_transfer transfer;
	struct ghci_slice *image_side;
	struct ghci_slice *buffer_side;
	VkImageSubresourceLayers layers;
	uint32_t words[GHCI_DW_BUFFER_IMAGE];
	uint32_t texel_bytes;
	uint32_t block;
	uint32_t granule;
	uint32_t row_length;
	uint32_t image_height;
	uint32_t extent[3];
	uint32_t slices;
	uint32_t slice;
	uint32_t pitch;
	int32_t offset[3];
	uint64_t buffer_id;
	uint64_t image_id;
	uint64_t image_base;
	uint64_t buffer_offset;
	uint64_t slice_bytes;
	uint64_t va;
	uint64_t remaining;
	uint64_t count;
	uint64_t index;
	int to_image;

	/* The buffer, the image and the regions. */
	to_image = number == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE;
	if (to_image) {
		buffer_id = read64(reader);
		image_id = read64(reader);
		read_skip(reader, 4U);
	} else {
		image_id = read64(reader);
		read_skip(reader, 4U);
		buffer_id = read64(reader);
	}
	read_skip(reader, 4U);
	count = read_count(reader);
	if (reader->error != 0 || !transfer_allowed(exec))
		return;
	if (image_of(exec, image_id, &image, &image_base) != 0 || ghci_vk_format_size(image->format, &texel_bytes, &block) != 0) {
		refuse(exec, EINVAL, "a copy with an image the session does not have bound");
		return;
	}
	granule = block != 0U ? 4U : 1U;

	/* Each region. */
	for (index = 0U; index < count && reader->error == 0 && exec->error == 0; index++) {
		buffer_offset = read64(reader);
		row_length = read32(reader);
		image_height = read32(reader);
		read_layers(reader, &layers);
		read_offset(reader, offset);
		extent[0] = read32(reader);
		extent[1] = read32(reader);
		extent[2] = read32(reader);
		if (reader->error != 0)
			break;
		if (image->format == (uint32_t)VK_FORMAT_D24_UNORM_S8_UINT || image->format == (uint32_t)VK_FORMAT_D32_SFLOAT_S8_UINT) {
			skip_command(exec, "a copy of one aspect of a depth and stencil format");
			continue;
		}
		if (layers.mipLevel >= image->layout.levels) {
			refuse(exec, EINVAL, "a copy of a level its image does not have");
			return;
		}

		/* The buffer's rows and slices. */
		if (row_length == 0U)
			row_length = extent[0];
		if (image_height == 0U)
			image_height = extent[1];
		pitch = (row_length + granule - 1U) / granule * texel_bytes;
		slice_bytes = (uint64_t)pitch * ((image_height + granule - 1U) / granule);
		slices = image->type == (uint32_t)VK_IMAGE_TYPE_3D ? extent[2] : layers.layerCount;

		/* One packet per slice. */
		for (slice = 0U; slice < slices && exec->error == 0; slice++) {
			if (buffer_va(exec, buffer_id, buffer_offset + slice * slice_bytes, &va, &remaining) != 0) {
				refuse(exec, EINVAL, "a copy with a buffer the session does not have bound, or past it");
				return;
			}
			kern_memset(&transfer, 0, sizeof(transfer));
			image_side = to_image ? &transfer.dst : &transfer.src;
			buffer_side = to_image ? &transfer.src : &transfer.dst;
			buffer_side->va = va;
			buffer_side->pitch = pitch;
			if (image->type == (uint32_t)VK_IMAGE_TYPE_3D)
				image_side->va = image_slice(image, image_base, layers.mipLevel, 0U, (uint32_t)offset[2] + slice);
			else
				image_side->va = image_slice(image, image_base, layers.mipLevel, layers.baseArrayLayer + slice, 0U);
			image_side->pitch = ghci_layout_row(&image->layout, layers.mipLevel);
			image_side->x = (uint32_t)offset[0];
			image_side->y = (uint32_t)offset[1];
			transfer.width = extent[0];
			transfer.height = extent[1];
			transfer.format = image->format;
			if (to_image)
				put(exec, words, ghci_cmd_copy_buffer_to_image(words, &transfer));
			else
				put(exec, words, ghci_cmd_copy_image_to_buffer(words, &transfer));
		}
	}
}

/* Reads a VkImageSubresourceLayers: [aspect][level][first layer][layers]. */
static void
read_layers(
	struct i915_wire_reader *reader,
	VkImageSubresourceLayers *layers)
{
	/* The four words. */
	layers->aspectMask = read32(reader);
	layers->mipLevel = read32(reader);
	layers->baseArrayLayer = read32(reader);
	layers->layerCount = read32(reader);
}

/* Reads a VkOffset3D. */
static void
read_offset(
	struct i915_wire_reader *reader,
	int32_t *offset)
{
	/* The three words. */
	offset[0] = (int32_t)read32(reader);
	offset[1] = (int32_t)read32(reader);
	offset[2] = (int32_t)read32(reader);
}

/*
 * vkCmdSetEvent and vkCmdResetEvent: [event][stage mask]: a fence that
 * writes the event's word (1 or 0) once the work before it is done.
 */
static void
event_write(
	struct exec *exec,
	struct i915_wire_reader *reader,
	uint32_t value)
{
	struct ghci_vk_object *object;
	uint32_t words[GHCI_DW_FENCE];
	uint64_t identity;
	uint64_t bus;
	uint32_t index;

	/* The event, of the session. */
	identity = read64(reader);
	(void)read32(reader);
	if (reader->error != 0)
		return;
	object = ghci_vk_object_find(exec->session, GHCI_VK_EVENT, identity);
	if (object == NULL) {
		refuse(exec, EINVAL, "a set of an event the session does not have");
		return;
	}

	/* The submission's last write of the event. */
	for (index = 0U; index < exec->event_count; index++) {
		if (exec->events[index].identity == identity)
			break;
	}
	if (index == EVENTS_MAX) {
		refuse(exec, ENOMEM, "a submission that writes more events than it keeps");
		return;
	}
	if (index == exec->event_count) {
		exec->events[index].identity = identity;
		exec->event_count++;
	}
	exec->events[index].value = value;

	/* The fence on the word. */
	bus = object->as.event.block->allocation->bus + object->as.event.offset;
	put(exec, words, ghci_cmd_fence(words, bus, value, GHCI_FENCE_FLAG_MEM_WRITE));
}

/*
 * vkCmdWaitEvents: [count][u64 count][events][source stages][destination
 * stages], then the barriers (design R10).  An event this submission set
 * earlier is waited for with WAIT_FENCE on its word; one the host has set
 * and no placed work writes needs no wait.  Any other would stop the one
 * ring until something outside it sets the word, which the executor does
 * not hold for yet: the submission is refused.  The barrier follows.
 */
static void
wait_events(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint32_t words[GHCI_DW_WAIT_FENCE];
	uint64_t identities[EVENTS_MAX];
	uint32_t source_stages;
	uint32_t destination_stages;
	uint32_t source_access;
	uint32_t destination_access;
	uint64_t count;
	uint64_t index;
	uint64_t bus;
	uint32_t at;
	enum ghci_ticket_state state;

	/* The events, the stages and the barriers. */
	read_skip(reader, 4U);
	count = read_count(reader);
	if (count > EVENTS_MAX) {
		refuse(exec, EINVAL, "a wait for more events than a submission keeps");
		return;
	}
	for (index = 0U; index < count; index++)
		identities[index] = read64(reader);
	source_stages = read32(reader);
	destination_stages = read32(reader);
	barriers_read(reader, &source_access, &destination_access);
	if (reader->error != 0)
		return;

	/* Each event. */
	for (index = 0U; index < count && exec->error == 0; index++) {
		object = ghci_vk_object_find(exec->session, GHCI_VK_EVENT, identities[index]);
		if (object == NULL) {
			refuse(exec, EINVAL, "a wait for an event the session does not have");
			return;
		}

		/* Set earlier in this submission: the processor waits for its word. */
		for (at = 0U; at < exec->event_count; at++) {
			if (exec->events[at].identity == identities[index])
				break;
		}
		if (at < exec->event_count && exec->events[at].value == 1U) {
			bus = object->as.event.block->allocation->bus + object->as.event.offset;
			put(exec, words, ghci_cmd_wait_fence(words, bus, 1U));
			continue;
		}

		/* Set by the host, and no placed work writes it any more. */
		state = GHCI_TICKET_DONE;
		if (object->as.event.gpu_written)
			state = ghci_cp_ticket(exec->session->device, &object->as.event.written);
		if (at == exec->event_count && *object->as.event.word == 1U && state != GHCI_TICKET_PENDING)
			continue;
		refuse(exec, ENOTSUP, "a wait for an event nothing before it in the ring sets");
		return;
	}

	/* The barrier of the wait. */
	put(exec, words, ghci_cmd_barrier(words, stages_of(source_stages), stages_of(destination_stages),
		barrier_flags(source_access, destination_access, source_stages, destination_stages)));
}

/*
 * The query commands: vkCmdBeginQuery [pool][query][flags], vkCmdEndQuery
 * [pool][query] (16-byte slots with the availability word),
 * vkCmdWriteTimestamp [stage][pool][query] (the counter, then a fence of
 * 1 on the slot's availability word, design R12), vkCmdResetQueryPool
 * [pool][first][count] (a fill of zeros; a transfer), and
 * vkCmdCopyQueryPoolResults (the shader's copy, which this device does
 * not have yet: skipped).
 */
static void
query(
	struct exec *exec,
	uint32_t number,
	struct i915_wire_reader *reader)
{
	const struct ghci_vk_query_pool *pool;
	struct ghci_vk_object *object;
	uint32_t words[GHCI_DW_FENCE];
	uint32_t stage;
	uint32_t first;
	uint32_t count;
	uint32_t flags;
	uint32_t when;

	/* The arguments of each. */
	stage = 0U;
	count = 1U;
	flags = 0U;
	if (number == GPU_OP_CMD_WRITE_TIMESTAMP)
		stage = read32(reader);
	object = ghci_vk_object_find(exec->session, GHCI_VK_QUERY_POOL, read64(reader));
	first = read32(reader);
	if (number == GPU_OP_CMD_BEGIN_QUERY)
		flags = read32(reader);
	else if (number == GPU_OP_CMD_RESET_QUERY_POOL || number == GPU_OP_CMD_COPY_QUERY_POOL_RESULTS)
		count = read32(reader);
	if (number == GPU_OP_CMD_COPY_QUERY_POOL_RESULTS)
		read_skip(reader, 24U + 4U);
	if (reader->error != 0)
		return;

	/* A pool of the session and queries in it. */
	if (object == NULL || first > object->as.query_pool.count || count > object->as.query_pool.count - first) {
		refuse(exec, EINVAL, "a query of a pool the session does not have, or past it");
		return;
	}
	pool = &object->as.query_pool;

	/* Each command's packets. */
	switch (number) {
	case GPU_OP_CMD_BEGIN_QUERY:
	case GPU_OP_CMD_END_QUERY:
		flags = (flags & VK_QUERY_CONTROL_PRECISE_BIT) != 0U ? GHCI_QUERY_PRECISE : 0U;
		put(exec, words, ghci_cmd_query(words, number == GPU_OP_CMD_END_QUERY ? 1U : 0U, pool->va, first, flags | GHCI_QUERY_AVAILABILITY));
		break;
	case GPU_OP_CMD_WRITE_TIMESTAMP:
		/* The top of the pipe, after the rasterizer, or the bottom. */
		when = 2U;
		if (stage == (uint32_t)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
			when = 0U;
		else if ((stage & (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)) != 0U)
			when = 1U;
		put(exec, words, ghci_cmd_write_timestamp(words, 0U, pool->va + (uint64_t)first * QUERY_SLOT_BYTES, when));
		put(exec, words, ghci_cmd_fence(words, pool->bus + (uint64_t)first * QUERY_SLOT_BYTES + 8U, 1U, GHCI_FENCE_FLAG_MEM_WRITE));
		break;
	case GPU_OP_CMD_RESET_QUERY_POOL:
		if (count == 0U || !transfer_allowed(exec))
			break;
		put(exec, words, ghci_cmd_fill_buffer(words, pool->va + (uint64_t)first * QUERY_SLOT_BYTES, count * QUERY_SLOT_BYTES, 0U));
		break;
	default:
		skip_command(exec, "a copy of query results (the shader's copy)");
		break;
	}
}

/*
 * vkCmdExecuteCommands: [count][u64 count][command buffers]: each
 * secondary buffer's recording, within the render pass of this one.
 */
static void
execute_commands(
	struct exec *exec,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t identities[64];
	uint64_t count;
	uint64_t index;

	/* The buffers. */
	read_skip(reader, 4U);
	count = read_count(reader);
	if (count > 64U) {
		refuse(exec, EINVAL, "more secondary command buffers than one execute takes");
		return;
	}
	for (index = 0U; index < count; index++)
		identities[index] = read64(reader);
	if (reader->error != 0)
		return;

	/* Each, ended and secondary, converted in place. */
	for (index = 0U; index < count && exec->error == 0; index++) {
		object = ghci_vk_object_find(exec->session, GHCI_VK_COMMAND_BUFFER, identities[index]);
		if (object == NULL || object->device != exec->device || object->as.commands.state != GHCI_VK_RECORD_EXECUTABLE
			|| object->as.commands.level != (uint32_t)VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
			refuse(exec, EINVAL, "an execute of a buffer that is not an ended secondary one");
			return;
		}
		if (exec->state->secondary) {
			refuse(exec, EINVAL, "an execute within a secondary buffer");
			return;
		}
		(void)buffer_convert(exec, object, 1);
	}
}
