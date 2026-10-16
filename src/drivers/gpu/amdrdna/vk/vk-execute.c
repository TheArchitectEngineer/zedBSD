/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The running of a command buffer's records on the GPU.
 *
 * Each session has a run of the executor's GPU memory for its
 * submissions: an indirect buffer of command packets, the resource tables
 * of its draws, the word its barriers write and wait for, a zero page and
 * the corners of a quad.  The records are turned into packets in order
 * (vk-state.c) and the indirect buffer is run on the graphics queue when
 * it is full, when a copy must be done by the CPU, and at the end; each
 * run is waited for, so the run's memory is free again after it and
 * nothing a submission names is in use by the GPU once it returns.
 *
 * A draw writes everything it uses: for each of its two programs a
 * resource table of its own (the push constants, the buffer descriptors
 * of its storage and uniform buffers, the image and sampler descriptors
 * of its sampled images, and the vertex program's vertex buffer
 * descriptors, where the compiler placed each), the target, the viewport,
 * scissor, blend and culling, and the programs.  A copy, a blit, a copy
 * between a buffer and an image, a clear and a pass's clear are drawn
 * too: a quad of the executor's own programs samples the source (in
 * pixel coordinates) into the destination, or writes one colour.  A copy
 * whose buffer is not laid out as a linear image can be (its start or its
 * row pitch off 256 bytes) is done by the CPU after the work before it.
 * Between a pass or a transfer and what follows, and at the end of every
 * indirect buffer, a barrier drains the programs, writes the colour
 * caches back and empties the shader caches.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

/* The session's run: its size and where each of its parts starts. */
#define EXECUTE_RUN_BYTES		(2U * 1024U * 1024U)
#define EXECUTE_BUFFER_AT		0U
#define EXECUTE_BUFFER_BYTES		(1024U * 1024U)
#define EXECUTE_TABLES_AT		(1024U * 1024U)
#define EXECUTE_TABLES_BYTES		(768U * 1024U)
#define EXECUTE_SYNC_AT			(1792U * 1024U)
#define EXECUTE_ZERO_AT			(1796U * 1024U)
#define EXECUTE_ZERO_BYTES		4096U
#define EXECUTE_CORNERS_AT		(1800U * 1024U)

/* The words of the indirect buffer, and what one record writes at most into it (a transfer of AMDRDNA_VK_REGIONS_MAX quads). */
#define EXECUTE_BUFFER_WORDS		(EXECUTE_BUFFER_BYTES / 4U)
#define EXECUTE_RECORD_WORDS		16384U

/*
 * A resource table starts on 64 bytes; the most one record's tables take
 * (two tables for each of AMDRDNA_VK_REGIONS_MAX quads, each at most this
 * many bytes).
 */
#define EXECUTE_TABLE_ALIGN		64U
#define EXECUTE_TABLE_MAX		2048U
#define EXECUTE_RECORD_TABLES		(2U * AMDRDNA_VK_REGIONS_MAX * EXECUTE_TABLE_MAX)

/*
 * An indirect buffer of the graphics queue is a whole number of eight
 * words.
 *
 * Value referenced: Mesa 25.0.7 src/amd/common/ac_gpu_info.c
 * (ib_pad_dw_mask of the graphics IP: 0x7, line 650).
 */
#define EXECUTE_BUFFER_PAD		8U

/* The quad's corners: two triangles of six corners, two floats each. */
#define EXECUTE_CORNERS			6U
#define EXECUTE_CORNER_STRIDE		8U

/*
 * The float bits of -1 and 1.
 *
 * Values referenced: IEEE 754-2008 binary32 (sign 31, biased exponent
 * 30:23 with a bias of 127, fraction 22:0).
 */
#define EXECUTE_FLOAT_MINUS_ONE		0xbf800000U
#define EXECUTE_FLOAT_ONE		0x3f800000U

/*
 * The executor's three quad programs: the vertex program, the textured
 * fragment program and the one-colour fragment program.
 */
#define EXECUTE_QUAD_VERTEX		0U
#define EXECUTE_QUAD_TEXTURED		1U
#define EXECUTE_QUAD_COLOUR		2U
#define EXECUTE_QUAD_PROGRAMS		3U

/*
 * The executor's own programs and the session's run.
 *
 * token names the run to the node, cpu and va its two views; pm4 writes
 * its indirect buffer, tables is how much of its tables' part the buffer
 * uses, draws and quads how many application draws and own quads it
 * holds (for the node's counts), sync the last value a barrier wrote.
 * The quad programs are compiled once, into a run of their own.
 */
struct amdrdna_vk_execute {
	void *token;
	uint8_t *cpu;
	uint64_t va;
	struct amdrdna_pm4 pm4;
	uint32_t tables;
	uint32_t draws;
	uint32_t quads;
	uint32_t sync;
	void *program_token;
	uint8_t *program_cpu;
	uint64_t program_va;
	struct amdrdna_vk_program quad[EXECUTE_QUAD_PROGRAMS];
	bool programs_ready;
};

/*
 * What a command buffer's records have bound so far, and the pass they
 * are in.
 */
struct execute_state {
	const struct amdrdna_vk_pipeline *pipeline;
	uint32_t viewport[6];
	bool viewport_set;
	VkRect2D scissor;
	bool scissor_set;
	const struct amdrdna_vk_descriptor_set *sets[AMDRDNA_VK_SETS_MAX];
	const struct amdrdna_vk_buffer *buffers[AMDRDNA_VK_VERTEX_BINDINGS_MAX];
	uint64_t offsets[AMDRDNA_VK_VERTEX_BINDINGS_MAX];
	uint8_t push[AMDRDNA_VK_PUSH_BYTES];
	bool in_pass;
	struct amdrdna_vk_target target;
	bool target_written;
	VkRect2D area;
};

/*
 * One quad an internal draw makes: the destination and its region; the
 * source texture (a linear image at source, its size and format), its
 * region in pixels and its filter, or the one colour (four float words)
 * when textured is clear.
 */
struct execute_quad {
	struct amdrdna_vk_target target;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	bool textured;
	uint64_t source;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t source_format;
	int32_t u0;
	int32_t v0;
	int32_t u1;
	int32_t v1;
	uint32_t filter;
	uint32_t colour[4];
};

static int execute_open(struct amdrdna_vk_session *session, struct amdrdna_vk_execute **execute);
static int execute_programs(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute);
static int execute_compile(struct amdrdna_quad_ir *program, struct amdrdna_vk_program *compiled);
static void execute_start(struct amdrdna_vk_execute *execute);
static int execute_flush(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute);
static int execute_room(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state);
static void execute_barrier(struct amdrdna_vk_execute *execute);
static int execute_record(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static void execute_bind(struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_begin_pass(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_draw(struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_table(struct amdrdna_vk_execute *execute, const struct amdrdna_gfx10_program *program, uint8_t **cpu, uint64_t *va);
static int execute_resources(const struct amdrdna_gfx10_program *program, const struct execute_state *state, uint8_t *table);
static int execute_buffer_slot(const struct amdrdna_vk_descriptor_slot *slot, uint32_t size, uint32_t *words);
static int execute_texture_slot(const struct amdrdna_vk_descriptor_slot *slot, uint32_t *image_words, uint32_t *sampler_words);
static void execute_vertex_input(const struct amdrdna_vk_execute *execute, const struct amdrdna_vk_pipeline *pipeline, const struct execute_state *state, uint8_t *table, uint32_t *instance_mask);
static int execute_transfer(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_copy_images(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_buffer_image(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_cpu_copy(const struct amdrdna_vk_buffer *buffer, const struct amdrdna_vk_image *image, const VkBufferImageCopy *region, bool to_image);
static int execute_clear_image(struct amdrdna_vk_session *session, struct amdrdna_vk_execute *execute, struct execute_state *state, const struct amdrdna_vk_record *record);
static int execute_quad(struct amdrdna_vk_execute *execute, const struct execute_quad *quad);
static int execute_image_target(const struct amdrdna_vk_image *image, uint32_t format, struct amdrdna_vk_target *target);
static bool execute_region_inside(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t image_width, uint32_t image_height);
static void execute_full_viewport(uint32_t *viewport, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static void execute_put32(uint8_t *cpu, uint32_t at, uint32_t value);
static void execute_put_words(uint8_t *cpu, uint32_t at, const uint32_t *words, uint32_t count);

/*
 * Runs an executable command buffer's records on the GPU and waits for
 * them.  Returns 0, EINVAL for a command buffer that is not executable, or
 * the error of the run that failed (the GPU then did not finish).
 */
int
amdrdna_vk_execute(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_command_buffer *command)
{
	struct amdrdna_vk_execute *execute;
	struct execute_state *state;
	uint32_t index;
	int error;

	/* Only an executable command buffer runs. */
	if (command->state != AMDRDNA_VK_COMMAND_EXECUTABLE)
		return EINVAL;
	if (command->record_count == 0U)
		return 0;

	/* The session's run and programs. */
	error = execute_open(session, &execute);
	if (error != 0)
		return error;

	/* The state starts empty. */
	state = kern_calloc(1U, sizeof(*state));
	if (state == NULL)
		return ENOMEM;

	/* Each record in order, the indirect buffer started over when it fills. */
	execute_start(execute);
	error = 0;
	for (index = 0U; index < command->record_count; index++) {
		error = execute_room(session, execute, state);
		if (error != 0)
			break;
		error = execute_record(session, execute, state, &command->records[index]);
		if (error != 0)
			break;
	}

	/* The rest of the indirect buffer. */
	if (error == 0)
		error = execute_flush(session, execute);
	kern_free(state);

	/* Reports a run that failed. */
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done the command buffer. */
	return 0;
}

/* Gives back a closing session's run and programs. */
void
amdrdna_vk_execute_close(
	struct amdrdna_vk_session *session)
{
	struct amdrdna_vk_execute *execute;
	uint32_t index;

	execute = session->execute;
	if (execute == NULL)
		return;

	/* The runs, the compiled quad programs, then the record. */
	if (execute->token != NULL)
		amdrdna_vk_gpu_free(session->owner, execute->token);
	if (execute->program_token != NULL)
		amdrdna_vk_gpu_free(session->owner, execute->program_token);
	for (index = 0U; index < EXECUTE_QUAD_PROGRAMS; index++) {
		if (execute->quad[index].compiled != NULL)
			amdrdna_gfx10_program_free(execute->quad[index].compiled);
	}
	kern_free(execute);
	session->execute = NULL;
}

/*
 * Gives the session's run, made at its first submission: the zero page,
 * the quad's corners and the barrier's word written once, and the
 * executor's programs.
 */
static int
execute_open(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute **execute)
{
	struct amdrdna_vk_execute *opened;
	static const uint32_t corners[EXECUTE_CORNERS * 2U] = {
		0U, 0U,
		EXECUTE_FLOAT_ONE, 0U,
		0U, EXECUTE_FLOAT_ONE,
		EXECUTE_FLOAT_ONE, 0U,
		EXECUTE_FLOAT_ONE, EXECUTE_FLOAT_ONE,
		0U, EXECUTE_FLOAT_ONE
	};
	int error;

	/* Made once; programs that did not compile are tried again. */
	if (session->execute != NULL) {
		opened = session->execute;
		if (!opened->programs_ready) {
			error = execute_programs(session, opened);
			if (error != 0)
				return error;
		}
		*execute = opened;
		return 0;
	}

	/* The record and the run. */
	opened = kern_calloc(1U, sizeof(*opened));
	if (opened == NULL)
		return ENOMEM;
	error = amdrdna_vk_gpu_alloc(session->owner, EXECUTE_RUN_BYTES, &opened->token, &opened->cpu, &opened->va);
	if (error != 0) {
		kern_free(opened);
		return error;
	}

	/* The zero page, the barrier's word, and the corners: (0,0) (1,0) (0,1), (1,0) (1,1) (0,1). */
	kern_memset(opened->cpu + EXECUTE_ZERO_AT, 0, EXECUTE_ZERO_BYTES);
	execute_put32(opened->cpu, EXECUTE_SYNC_AT, 0U);
	execute_put_words(opened->cpu, EXECUTE_CORNERS_AT, corners, EXECUTE_CORNERS * 2U);

	/* The executor's programs. */
	session->execute = opened;
	error = execute_programs(session, opened);
	if (error != 0)
		return error;

	/* Succeeded: the run is the session's. */
	*execute = opened;
	return 0;
}

/*
 * Compiles the executor's quad programs (the corners placed by the push
 * constants' rectangle, the texture read at the push constants' texture
 * rectangle, or the push constants' colour) into a run of their own, each
 * on a 256-byte boundary.
 */
static int
execute_programs(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute)
{
	struct amdrdna_quad_ir *program;
	uint32_t index;
	uint32_t at;
	uint32_t bytes;
	int error;

	/* The IR's storage. */
	program = kern_malloc(sizeof(*program));
	if (program == NULL)
		return ENOMEM;

	/* The three programs (kept from a try that did not finish). */
	error = 0;
	if (execute->quad[EXECUTE_QUAD_VERTEX].compiled == NULL) {
		amdrdna_quad_vertex_ir(program, 1);
		error = execute_compile(program, &execute->quad[EXECUTE_QUAD_VERTEX]);
	}
	if (error == 0 && execute->quad[EXECUTE_QUAD_TEXTURED].compiled == NULL) {
		amdrdna_quad_fragment_ir(program, 1);
		error = execute_compile(program, &execute->quad[EXECUTE_QUAD_TEXTURED]);
	}
	if (error == 0 && execute->quad[EXECUTE_QUAD_COLOUR].compiled == NULL) {
		amdrdna_quad_fragment_ir(program, 0);
		error = execute_compile(program, &execute->quad[EXECUTE_QUAD_COLOUR]);
	}
	kern_free(program);
	if (error != 0)
		return error;

	/* The run: each program's code rounded up to 256 bytes. */
	bytes = 0U;
	for (index = 0U; index < EXECUTE_QUAD_PROGRAMS; index++)
		bytes += (execute->quad[index].compiled->code_words * 4U + AMDRDNA_VK_RESOURCE_ALIGN - 1U) & ~(AMDRDNA_VK_RESOURCE_ALIGN - 1U);
	if (execute->program_token == NULL) {
		error = amdrdna_vk_gpu_alloc(session->owner, bytes, &execute->program_token, &execute->program_cpu, &execute->program_va);
		if (error != 0)
			return error;
	}

	/* Each program's code in its place. */
	at = 0U;
	for (index = 0U; index < EXECUTE_QUAD_PROGRAMS; index++) {
		kern_memcpy(execute->program_cpu + at, execute->quad[index].compiled->code, (size_t)execute->quad[index].compiled->code_words * 4U);
		execute->quad[index].va = execute->program_va + at;
		at += (execute->quad[index].compiled->code_words * 4U + AMDRDNA_VK_RESOURCE_ALIGN - 1U) & ~(AMDRDNA_VK_RESOURCE_ALIGN - 1U);
	}

	/* Succeeded: the programs are in place. */
	execute->programs_ready = true;
	return 0;
}

/* Compiles one quad program and its register values. */
static int
execute_compile(
	struct amdrdna_quad_ir *program,
	struct amdrdna_vk_program *compiled)
{
	struct amdrdna_gfx10_diagnostic diagnostic;
	int error;

	/* Compiled for GFX10. */
	kern_memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile_ir(&program->ir, NULL, &compiled->compiled, &diagnostic);
	if (error != 0) {
		kern_logf("amdrdna: vk: a quad program did not compile (%d: %s)\n", error,
			  (diagnostic.reason != NULL) ? diagnostic.reason : "?");
		compiled->compiled = NULL;
		return error;
	}

	/* The values a draw writes for it. */
	amdrdna_gfx10_program_registers(compiled->compiled, &compiled->registers);

	/* Succeeded: the program is compiled. */
	return 0;
}

/*
 * Starts an indirect buffer: the state every draw shares and the caches
 * emptied (vk-state.c); nothing of the tables is used yet.
 */
static void
execute_start(
	struct amdrdna_vk_execute *execute)
{
	/* The buffer over its part of the run. */
	amdrdna_pm4_start(&execute->pm4, (uint32_t *)(void *)(execute->cpu + EXECUTE_BUFFER_AT), EXECUTE_BUFFER_WORDS);
	amdrdna_vk_state_start(&execute->pm4);

	/* Nothing of the tables is used. */
	execute->tables = 0U;
	execute->draws = 0U;
	execute->quads = 0U;
}

/*
 * Runs the indirect buffer written so far, after a barrier that leaves
 * everything it wrote in memory, and waits for it; then starts the next
 * one.  Returns the run's error.
 */
static int
execute_flush(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute)
{
	int error;

	/* The barrier at the end, the buffer padded. */
	execute_barrier(execute);
	amdrdna_pm4_pad(&execute->pm4, EXECUTE_BUFFER_PAD);
	if (execute->pm4.full)
		return ENOSPC;

	/* Counts its draws, then runs it. */
	amdrdna_vk_gpu_drawn(session->owner, execute->draws, execute->quads);
	error = amdrdna_vk_gpu_run(session->owner, execute->va + EXECUTE_BUFFER_AT, execute->pm4.count * 4U);
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done it; the next buffer starts. */
	execute_start(execute);
	return 0;
}

/*
 * Makes room for one record: runs the indirect buffer when the record
 * could pass its end or the tables' part could not take its tables.  The
 * target is written again in the next buffer.
 */
static int
execute_room(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state)
{
	bool full;
	int error;

	/* Full when the buffer or the tables could not take a record. */
	full = false;
	if (execute->pm4.count + EXECUTE_RECORD_WORDS > EXECUTE_BUFFER_WORDS)
		full = true;
	if (execute->tables + EXECUTE_RECORD_TABLES > EXECUTE_TABLES_BYTES)
		full = true;
	if (!full)
		return 0;

	/* Runs what there is. */
	error = execute_flush(session, execute);
	if (error != 0)
		return error;

	/* Succeeded: the next buffer has room; its target is written anew. */
	state->target_written = false;
	return 0;
}

/* Writes a barrier with the next value of the session's barrier word. */
static void
execute_barrier(
	struct amdrdna_vk_execute *execute)
{
	/* A value the word has not held since the last run began. */
	execute->sync++;
	amdrdna_vk_state_barrier(&execute->pm4, execute->va + EXECUTE_SYNC_AT, execute->sync);
}

/* Runs one record. */
static int
execute_record(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	int error;

	/* The record's command. */
	error = 0;
	switch (record->opcode) {
	case GPU_OP_CMD_BIND_PIPELINE:
	case GPU_OP_CMD_SET_VIEWPORT:
	case GPU_OP_CMD_SET_SCISSOR:
	case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
	case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
	case GPU_OP_CMD_PUSH_CONSTANTS:
		execute_bind(state, record);
		break;
	case GPU_OP_CMD_BEGIN_RENDER_PASS:
		error = execute_begin_pass(session, execute, state, record);
		break;
	case GPU_OP_CMD_END_RENDER_PASS:
		state->in_pass = false;
		execute_barrier(execute);
		break;
	case GPU_OP_CMD_DRAW:
		error = execute_draw(execute, state, record);
		break;
	case GPU_OP_CMD_COPY_IMAGE:
	case GPU_OP_CMD_BLIT_IMAGE:
	case GPU_OP_CMD_COPY_BUFFER_TO_IMAGE:
	case GPU_OP_CMD_COPY_IMAGE_TO_BUFFER:
	case GPU_OP_CMD_CLEAR_COLOR_IMAGE:
		error = execute_transfer(session, execute, state, record);
		break;
	default:
		break;
	}

	/* Reports a record that could not run, or a buffer that overflowed. */
	if (error != 0)
		return error;
	if (execute->pm4.full)
		return ENOSPC;

	/* Succeeded: the record ran (or is bound for the draws after it). */
	return 0;
}

/* Keeps what a binding record binds for the draws after it. */
static void
execute_bind(
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	uint32_t index;

	/* The state each command binds. */
	switch (record->opcode) {
	case GPU_OP_CMD_BIND_PIPELINE:
		state->pipeline = record->objects[0]->payload;
		break;
	case GPU_OP_CMD_SET_VIEWPORT:
		kern_memcpy(state->viewport, record->values, sizeof(state->viewport));
		state->viewport_set = true;
		break;
	case GPU_OP_CMD_SET_SCISSOR:
		state->scissor = record->rect;
		state->scissor_set = true;
		break;
	case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
		for (index = 0U; index < record->object_count; index++) {
			if (record->objects[index] != NULL)
				state->sets[record->first + index] = record->objects[index]->payload;
		}
		break;
	case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
		for (index = 0U; index < record->object_count; index++) {
			state->buffers[record->first + index] = record->objects[index]->payload;
			state->offsets[record->first + index] = record->offsets[index];
		}
		break;
	case GPU_OP_CMD_PUSH_CONSTANTS:
		kern_memcpy(state->push + record->first, record->push, record->count);
		break;
	default:
		break;
	}
}

/*
 * Begins a pass: its framebuffer's view's image is the target from now on,
 * and a pass whose attachment loads by clearing clears its render area
 * with a quad of the clear colour.
 */
static int
execute_begin_pass(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	const struct amdrdna_vk_render_pass *pass;
	const struct amdrdna_vk_framebuffer *framebuffer;
	const struct amdrdna_vk_view *view;
	struct execute_quad quad;
	int32_t x1;
	int32_t y1;
	int error;

	UNUSED_PARAMETER(session);

	pass = record->objects[0]->payload;
	framebuffer = record->objects[1]->payload;

	/* A pass that writes no attachment draws nothing that stays. */
	state->in_pass = true;
	state->target_written = false;
	state->area = record->rect;
	if (pass->colour == 0U || framebuffer->view == NULL) {
		state->target.address = 0U;
		return 0;
	}

	/* The target: the view's image, in the view's format. */
	view = framebuffer->view->payload;
	error = execute_image_target(view->image->payload, view->format, &state->target);
	if (error != 0)
		return error;

	/* A pass loading by clearing clears its area, within the framebuffer and the image. */
	if (pass->load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
		return 0;
	x1 = state->area.offset.x + (int32_t)state->area.extent.width;
	y1 = state->area.offset.y + (int32_t)state->area.extent.height;
	if (x1 > (int32_t)framebuffer->width)
		x1 = (int32_t)framebuffer->width;
	if (y1 > (int32_t)framebuffer->height)
		y1 = (int32_t)framebuffer->height;
	if (x1 > (int32_t)state->target.width)
		x1 = (int32_t)state->target.width;
	if (y1 > (int32_t)state->target.height)
		y1 = (int32_t)state->target.height;
	if (state->area.offset.x < 0 || state->area.offset.y < 0 || x1 <= state->area.offset.x || y1 <= state->area.offset.y)
		return 0;

	/* The clear colour's quad over the area. */
	kern_memset(&quad, 0, sizeof(quad));
	quad.target = state->target;
	quad.x0 = state->area.offset.x;
	quad.y0 = state->area.offset.y;
	quad.x1 = x1;
	quad.y1 = y1;
	kern_memcpy(quad.colour, record->values, sizeof(quad.colour));
	error = execute_quad(execute, &quad);
	if (error != 0)
		return error;

	/* Succeeded: the pass draws into its target; the draws write it again. */
	state->target_written = false;
	return 0;
}

/*
 * Runs a draw in a pass: the resource tables of its two programs, the
 * target when not written yet, the viewport and scissor, the blend and
 * culling, the programs and the draw.
 */
static int
execute_draw(
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	const struct amdrdna_vk_pipeline *pipeline;
	struct amdrdna_vk_blend blend;
	const uint32_t *viewport;
	const VkRect2D *scissor;
	uint8_t *vertex_table;
	uint8_t *fragment_table;
	uint64_t vertex_va;
	uint64_t fragment_va;
	uint32_t instance_mask;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	int error;

	/* A draw in a pass with a target, with a pipeline. */
	pipeline = state->pipeline;
	if (!state->in_pass || pipeline == NULL)
		return EINVAL;
	if (state->target.address == 0U || record->values[0] == 0U || record->values[1] == 0U)
		return 0;

	/* The two programs' resource tables. */
	error = execute_table(execute, pipeline->vertex.compiled, &vertex_table, &vertex_va);
	if (error == 0)
		error = execute_table(execute, pipeline->fragment.compiled, &fragment_table, &fragment_va);
	if (error == 0)
		error = execute_resources(pipeline->vertex.compiled, state, vertex_table);
	if (error == 0)
		error = execute_resources(pipeline->fragment.compiled, state, fragment_table);
	if (error != 0)
		return error;
	execute_vertex_input(execute, pipeline, state, vertex_table, &instance_mask);

	/* The scissor: dynamic or the pipeline's, within the target; nothing to draw outside it. */
	scissor = &pipeline->scissor;
	if (pipeline->dynamic_scissor != 0U && state->scissor_set)
		scissor = &state->scissor;
	x0 = scissor->offset.x;
	y0 = scissor->offset.y;
	x1 = x0 + (int32_t)scissor->extent.width;
	y1 = y0 + (int32_t)scissor->extent.height;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > (int32_t)state->target.width)
		x1 = (int32_t)state->target.width;
	if (y1 > (int32_t)state->target.height)
		y1 = (int32_t)state->target.height;
	if (x1 <= x0 || y1 <= y0)
		return 0;

	/* The target, once in a buffer. */
	if (!state->target_written) {
		error = amdrdna_vk_state_target(&execute->pm4, &state->target);
		if (error != 0)
			return error;
		state->target_written = true;
	}

	/* The viewport (dynamic or the pipeline's) and the scissor. */
	viewport = pipeline->viewport;
	if (pipeline->dynamic_viewport != 0U && state->viewport_set)
		viewport = state->viewport;
	amdrdna_vk_state_viewport(&execute->pm4, viewport);
	amdrdna_vk_state_scissor(&execute->pm4, (uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1);

	/* The blend and the culling. */
	blend.enable = pipeline->blend_enable;
	blend.colour_op = pipeline->blend_colour_op;
	blend.colour_source = pipeline->blend_src_colour;
	blend.colour_destination = pipeline->blend_dst_colour;
	blend.alpha_op = pipeline->blend_alpha_op;
	blend.alpha_source = pipeline->blend_src_alpha;
	blend.alpha_destination = pipeline->blend_dst_alpha;
	blend.write_mask = pipeline->write_mask;
	error = amdrdna_vk_state_blend(&execute->pm4, &blend);
	if (error != 0)
		return error;
	amdrdna_vk_state_raster(&execute->pm4, pipeline->cull_mode, pipeline->front_face);

	/* The programs with their tables, the first vertex and instance, and the draw. */
	amdrdna_vk_state_programs(&execute->pm4, &pipeline->vertex, &pipeline->fragment, vertex_va, fragment_va, record->values[2],
				  record->values[3], instance_mask);
	error = amdrdna_vk_state_draw(&execute->pm4, pipeline->topology, record->values[0], record->values[1]);
	if (error != 0)
		return error;
	execute->draws++;

	/* Succeeded: the draw is written. */
	return 0;
}

/* Gives a program's resource table: the next part of the run's tables, cleared. */
static int
execute_table(
	struct amdrdna_vk_execute *execute,
	const struct amdrdna_gfx10_program *program,
	uint8_t **cpu,
	uint64_t *va)
{
	uint32_t bytes;

	/* The table's bytes on the boundary (at least one, so that every table has an address). */
	bytes = (program->table_bytes + EXECUTE_TABLE_ALIGN) & ~(EXECUTE_TABLE_ALIGN - 1U);
	if (bytes > EXECUTE_TABLE_MAX)
		return ENOSPC;
	if (execute->tables + bytes > EXECUTE_TABLES_BYTES)
		return ENOSPC;

	/* The part. */
	*cpu = execute->cpu + EXECUTE_TABLES_AT + execute->tables;
	*va = execute->va + EXECUTE_TABLES_AT + execute->tables;
	kern_memset(*cpu, 0, bytes);
	execute->tables += bytes;

	/* Succeeded: the part is the program's. */
	return 0;
}

/*
 * Writes a program's resource table: the push constants, then each
 * storage and uniform buffer's descriptor and each sampled image's image
 * and sampler descriptors from the bound sets' bindings.  A binding with
 * nothing bound refuses the draw.
 */
static int
execute_resources(
	const struct amdrdna_gfx10_program *program,
	const struct execute_state *state,
	uint8_t *table)
{
	const struct amdrdna_vk_descriptor_set *set;
	const struct amdrdna_gfx10_binding *binding;
	uint32_t image_words[AMDRDNA_IMAGE_WORDS];
	uint32_t sampler_words[AMDRDNA_SAMPLER_WORDS];
	uint32_t words[4];
	uint32_t index;
	int error;

	/* The push constants the program reads. */
	kern_memcpy(table, state->push, program->push_bytes);

	/* The storage buffers: their whole range. */
	for (index = 0U; index < program->storage_used; index++) {
		binding = &program->storage[index];
		set = state->sets[binding->set];
		if (set == NULL)
			return EINVAL;
		error = execute_buffer_slot(&set->slots[binding->binding], 0U, words);
		if (error != 0)
			return error;
		execute_put_words(table, binding->table_offset, words, 4U);
	}

	/* The uniform buffers: the bytes the program reads, within the range. */
	for (index = 0U; index < program->uniform_used; index++) {
		binding = &program->uniforms[index];
		set = state->sets[binding->set];
		if (set == NULL)
			return EINVAL;
		error = execute_buffer_slot(&set->slots[binding->binding], binding->size, words);
		if (error != 0)
			return error;
		execute_put_words(table, binding->table_offset, words, 4U);
	}

	/* The sampled images: the image descriptor, then the sampler's. */
	for (index = 0U; index < program->texture_used; index++) {
		binding = &program->textures[index];
		set = state->sets[binding->set];
		if (set == NULL)
			return EINVAL;
		error = execute_texture_slot(&set->slots[binding->binding], image_words, sampler_words);
		if (error != 0)
			return error;
		execute_put_words(table, binding->table_offset, image_words, AMDRDNA_IMAGE_WORDS);
		execute_put_words(table, binding->table_offset + AMDRDNA_IMAGE_WORDS * 4U, sampler_words, AMDRDNA_SAMPLER_WORDS);
	}

	/* Succeeded: the table holds every resource. */
	return 0;
}

/*
 * Writes the raw buffer descriptor of a buffer binding: its range, or the
 * first size bytes of it when size is given.  EINVAL for a binding with no
 * buffer, EIO for a buffer without storage.
 */
static int
execute_buffer_slot(
	const struct amdrdna_vk_descriptor_slot *slot,
	uint32_t size,
	uint32_t *words)
{
	const struct amdrdna_vk_buffer *buffer;
	uint64_t range;
	uint64_t va;
	uint8_t *cpu;
	int error;

	/* The binding's buffer, with storage. */
	if (slot->buffer == NULL)
		return EINVAL;
	buffer = slot->buffer->payload;
	error = amdrdna_vk_buffer_address(buffer, &va, &cpu);
	if (error != 0)
		return error;
	if (slot->offset > buffer->size)
		return EINVAL;

	/* The range within the buffer (the whole size names the rest of it), then the bytes asked for. */
	range = buffer->size - slot->offset;
	if (slot->range < range)
		range = slot->range;
	if (size != 0U && size < range)
		range = size;
	if (range > 0xffffffffU)
		range = 0xffffffffU;

	/* Succeeded: the descriptor reads the range. */
	amdrdna_gfx10_raw_buffer_descriptor(va + slot->offset, (uint32_t)range, words);
	return 0;
}

/*
 * Writes the image and the sampler descriptors of a combined image
 * sampler binding.  EINVAL for a binding without both.
 */
static int
execute_texture_slot(
	const struct amdrdna_vk_descriptor_slot *slot,
	uint32_t *image_words,
	uint32_t *sampler_words)
{
	const struct amdrdna_vk_view *view;
	const struct amdrdna_vk_image *image;
	uint64_t va;
	uint8_t *cpu;
	int error;

	/* The view's image, with storage, and the sampler. */
	if (slot->view == NULL || slot->sampler == NULL)
		return EINVAL;
	view = slot->view->payload;
	image = view->image->payload;
	error = amdrdna_vk_image_address(image, &va, &cpu);
	if (error != 0)
		return error;

	/* The two descriptors. */
	error = amdrdna_vk_image_descriptor(va, image->width, image->height, view->format, view->swizzle, image_words);
	if (error != 0)
		return error;
	amdrdna_vk_sampler_descriptor(slot->sampler->payload, sampler_words);

	/* Succeeded: the binding's descriptors are written. */
	return 0;
}

/*
 * Writes the vertex program's vertex buffer descriptors: each location it
 * fetches reads the pipeline's attribute there from the buffer its binding
 * names (the records after the attribute's start, the binding's stride
 * apart); a location without an attribute, or whose binding has no
 * storage, reads no records (zeros).  instance_mask gets the locations
 * read by instance.
 */
static void
execute_vertex_input(
	const struct amdrdna_vk_execute *execute,
	const struct amdrdna_vk_pipeline *pipeline,
	const struct execute_state *state,
	uint8_t *table,
	uint32_t *instance_mask)
{
	const struct amdrdna_gfx10_program *program;
	const struct amdrdna_vk_vertex_attribute *attribute;
	const struct amdrdna_vk_vertex_binding *binding;
	const struct amdrdna_vk_buffer *buffer;
	uint32_t words[4];
	uint32_t location;
	uint32_t index;
	uint32_t code;
	uint32_t components;
	uint32_t bytes;
	uint32_t records;
	uint64_t start;
	uint64_t room;
	uint64_t va;
	uint8_t *cpu;
	int error;

	program = pipeline->vertex.compiled;
	*instance_mask = 0U;
	for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
		if ((program->fetched_locations & (1U << location)) == 0U)
			continue;

		/* No records until an attribute with storage is found. */
		amdrdna_gfx10_vertex_descriptor(execute->va + EXECUTE_ZERO_AT, 0U, 0U, AMDRDNA_GFX10_VERTEX_FLOAT4, 4U, words);

		/* The pipeline's attribute at the location, and its binding. */
		attribute = NULL;
		for (index = 0U; index < pipeline->attribute_count; index++) {
			if (pipeline->attributes[index].location == location)
				attribute = &pipeline->attributes[index];
		}
		binding = NULL;
		for (index = 0U; attribute != NULL && index < pipeline->binding_count; index++) {
			if (pipeline->bindings[index].binding == attribute->binding)
				binding = &pipeline->bindings[index];
		}

		/* The records of the bound buffer after the attribute's start. */
		buffer = NULL;
		if (binding != NULL)
			buffer = state->buffers[binding->binding];
		error = EINVAL;
		if (buffer != NULL)
			error = amdrdna_vk_buffer_address(buffer, &va, &cpu);
		if (error == 0 && amdrdna_vk_vertex_format(attribute->format, &code, &components, &bytes)) {
			start = state->offsets[binding->binding] + attribute->offset;
			if (start + bytes <= buffer->size) {
				/* Every record that holds the attribute whole (a stride of 0 reads the first record for every vertex). */
				room = buffer->size - start - bytes;
				records = 0xffffffffU;
				if (binding->stride != 0U && room / binding->stride < 0xffffffffU)
					records = (uint32_t)(room / binding->stride) + 1U;
				amdrdna_gfx10_vertex_descriptor(va + start, binding->stride, records, code, components, words);
				if (binding->instance != 0U)
					*instance_mask |= 1U << location;
			}
		}

		/* The location's descriptor. */
		execute_put_words(table, program->vertex_table_offset + location * 16U, words, 4U);
	}
}

/*
 * Runs a transfer outside a pass: the barrier before it (the work before
 * may have written its source), the transfer, the barrier after it (the
 * target written anew in the next pass).
 */
static int
execute_transfer(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	int error;

	/* What wrote the source has finished. */
	execute_barrier(execute);

	/* The transfer. */
	if (record->opcode == GPU_OP_CMD_COPY_IMAGE || record->opcode == GPU_OP_CMD_BLIT_IMAGE) {
		error = execute_copy_images(session, execute, state, record);
	} else if (record->opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
		error = execute_clear_image(session, execute, state, record);
	} else {
		error = execute_buffer_image(session, execute, state, record);
	}
	if (error != 0)
		return error;

	/* What follows reads what it wrote; the pass's target is written again. */
	execute_barrier(execute);
	state->target_written = false;

	/* Succeeded: the transfer is written. */
	return 0;
}

/*
 * Copies or blits regions between two images by drawing quads: the
 * destination region's quad samples the source region (a copy one to one
 * and nearest, a blit scaled and filtered).  A blit's region given
 * backwards mirrors it.
 */
static int
execute_copy_images(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	const struct amdrdna_vk_image *source;
	const struct amdrdna_vk_image *destination;
	const VkImageCopy *copies;
	const VkImageBlit *blits;
	struct execute_quad quad;
	uint8_t *cpu;
	uint32_t index;
	int32_t dx0;
	int32_t dy0;
	int32_t dx1;
	int32_t dy1;
	int32_t sx0;
	int32_t sy0;
	int32_t sx1;
	int32_t sy1;
	int error;

	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(state);

	source = record->objects[0]->payload;
	destination = record->objects[1]->payload;

	/* The two images, with storage. */
	kern_memset(&quad, 0, sizeof(quad));
	error = execute_image_target(destination, destination->format, &quad.target);
	if (error == 0)
		error = amdrdna_vk_image_address(source, &quad.source, &cpu);
	if (error != 0)
		return error;
	quad.textured = true;
	quad.source_width = source->width;
	quad.source_height = source->height;
	quad.source_format = source->format;
	quad.filter = VK_FILTER_NEAREST;
	if (record->opcode == GPU_OP_CMD_BLIT_IMAGE)
		quad.filter = record->values[0];

	/* Each region. */
	copies = record->regions;
	blits = record->regions;
	for (index = 0U; index < record->region_count; index++) {
		/* The destination's and the source's corners. */
		if (record->opcode == GPU_OP_CMD_COPY_IMAGE) {
			dx0 = copies[index].dstOffset.x;
			dy0 = copies[index].dstOffset.y;
			dx1 = dx0 + (int32_t)copies[index].extent.width;
			dy1 = dy0 + (int32_t)copies[index].extent.height;
			sx0 = copies[index].srcOffset.x;
			sy0 = copies[index].srcOffset.y;
			sx1 = sx0 + (int32_t)copies[index].extent.width;
			sy1 = sy0 + (int32_t)copies[index].extent.height;
		} else {
			dx0 = blits[index].dstOffsets[0].x;
			dy0 = blits[index].dstOffsets[0].y;
			dx1 = blits[index].dstOffsets[1].x;
			dy1 = blits[index].dstOffsets[1].y;
			sx0 = blits[index].srcOffsets[0].x;
			sy0 = blits[index].srcOffsets[0].y;
			sx1 = blits[index].srcOffsets[1].x;
			sy1 = blits[index].srcOffsets[1].y;
		}

		/* The quad covers the destination's region in order; a reversed edge swaps the source's. */
		quad.x0 = dx0;
		quad.x1 = dx1;
		quad.u0 = sx0;
		quad.u1 = sx1;
		if (dx1 < dx0) {
			quad.x0 = dx1;
			quad.x1 = dx0;
			quad.u0 = sx1;
			quad.u1 = sx0;
		}
		quad.y0 = dy0;
		quad.y1 = dy1;
		quad.v0 = sy0;
		quad.v1 = sy1;
		if (dy1 < dy0) {
			quad.y0 = dy1;
			quad.y1 = dy0;
			quad.v0 = sy1;
			quad.v1 = sy0;
		}

		error = execute_quad(execute, &quad);
		if (error != 0)
			return error;
	}

	/* Succeeded: the regions are copied. */
	return 0;
}

/*
 * Copies regions between a buffer and an image.  A buffer region laid out
 * as a linear image (its start and its row pitch on 256 bytes) is drawn,
 * as a texture to an image or as a target from one; another is copied by
 * the CPU after the work before it ran.
 */
static int
execute_buffer_image(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	const struct amdrdna_vk_buffer *buffer;
	const struct amdrdna_vk_image *image;
	const VkBufferImageCopy *regions;
	const VkBufferImageCopy *region;
	struct execute_quad quad;
	bool to_image;
	uint64_t buffer_va;
	uint64_t image_va;
	uint64_t start;
	uint8_t *buffer_cpu;
	uint8_t *image_cpu;
	uint32_t index;
	uint32_t row;
	uint32_t pitch;
	bool inside;
	int error;

	/* The buffer and the image, with storage. */
	to_image = (record->opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE);
	if (to_image) {
		buffer = record->objects[0]->payload;
		image = record->objects[1]->payload;
	} else {
		image = record->objects[0]->payload;
		buffer = record->objects[1]->payload;
	}
	error = amdrdna_vk_buffer_address(buffer, &buffer_va, &buffer_cpu);
	if (error == 0)
		error = amdrdna_vk_image_address(image, &image_va, &image_cpu);
	if (error != 0)
		return error;

	/* Each region. */
	regions = record->regions;
	for (index = 0U; index < record->region_count; index++) {
		region = &regions[index];

		/* Inside the image; the buffer's rows (a row length of zero is the region's width). */
		inside = execute_region_inside(region->imageOffset.x, region->imageOffset.y, region->imageExtent.width,
					       region->imageExtent.height, image->width, image->height);
		if (!inside || region->imageExtent.width == 0U || region->imageExtent.height == 0U)
			return EINVAL;
		row = region->bufferRowLength;
		if (row == 0U)
			row = region->imageExtent.width;
		pitch = row * AMDRDNA_VK_PIXEL_BYTES;
		if (region->bufferOffset > buffer->size ||
		    (uint64_t)pitch * (region->imageExtent.height - 1U) + (uint64_t)region->imageExtent.width * AMDRDNA_VK_PIXEL_BYTES >
		    buffer->size - region->bufferOffset)
			return EINVAL;
		start = buffer_va + region->bufferOffset;

		/* A region not laid out as a linear image is copied by the CPU, after the work before it. */
		if ((start % AMDRDNA_VK_RESOURCE_ALIGN) != 0U || (pitch % AMDRDNA_VK_PITCH_ALIGN) != 0U ||
		    pitch / AMDRDNA_VK_PIXEL_BYTES > AMDRDNA_VK_IMAGE_SIDE_MAX) {
			error = execute_flush(session, execute);
			if (error == 0)
				error = execute_cpu_copy(buffer, image, region, to_image);
			if (error != 0)
				return error;

			/* Counted for the node; the next draw writes its target anew. */
			amdrdna_vk_gpu_copied(session->owner, (uint64_t)region->imageExtent.width * AMDRDNA_VK_PIXEL_BYTES * region->imageExtent.height);
			state->target_written = false;
			continue;
		}

		/* Drawn: the destination's region samples the source's, one to one; the buffer is an image of its row length. */
		kern_memset(&quad, 0, sizeof(quad));
		quad.textured = true;
		quad.filter = VK_FILTER_NEAREST;
		if (to_image) {
			error = execute_image_target(image, image->format, &quad.target);
			quad.x0 = region->imageOffset.x;
			quad.y0 = region->imageOffset.y;
			quad.source = start;
			quad.source_width = row;
			quad.source_height = region->imageExtent.height;
			quad.source_format = image->format;
			quad.u0 = 0;
			quad.v0 = 0;
		} else {
			quad.target.address = start;
			quad.target.width = row;
			quad.target.height = region->imageExtent.height;
			quad.target.format = image->format;
			quad.x0 = 0;
			quad.y0 = 0;
			quad.source = image_va;
			quad.source_width = image->width;
			quad.source_height = image->height;
			quad.source_format = image->format;
			quad.u0 = region->imageOffset.x;
			quad.v0 = region->imageOffset.y;
		}
		if (error != 0)
			return error;
		quad.x1 = quad.x0 + (int32_t)region->imageExtent.width;
		quad.y1 = quad.y0 + (int32_t)region->imageExtent.height;
		quad.u1 = quad.u0 + (int32_t)region->imageExtent.width;
		quad.v1 = quad.v0 + (int32_t)region->imageExtent.height;
		error = execute_quad(execute, &quad);
		if (error != 0)
			return error;
	}

	/* Succeeded: the regions are copied. */
	return 0;
}

/*
 * Copies one region between a buffer and an image by the CPU, row by row
 * (the GPU is idle: the work before has run).
 */
static int
execute_cpu_copy(
	const struct amdrdna_vk_buffer *buffer,
	const struct amdrdna_vk_image *image,
	const VkBufferImageCopy *region,
	bool to_image)
{
	uint64_t va;
	uint8_t *buffer_cpu;
	uint8_t *image_cpu;
	uint8_t *buffer_row;
	uint8_t *image_row;
	uint32_t row_pixels;
	uint32_t bytes;
	uint32_t y;
	int error;

	/* The two views. */
	error = amdrdna_vk_buffer_address(buffer, &va, &buffer_cpu);
	if (error == 0)
		error = amdrdna_vk_image_address(image, &va, &image_cpu);
	if (error != 0)
		return error;

	/* The rows: the buffer's at its row length, the image's at its pitch. */
	row_pixels = region->bufferRowLength;
	if (row_pixels == 0U)
		row_pixels = region->imageExtent.width;
	bytes = region->imageExtent.width * AMDRDNA_VK_PIXEL_BYTES;
	for (y = 0U; y < region->imageExtent.height; y++) {
		buffer_row = buffer_cpu + region->bufferOffset + (uint64_t)y * row_pixels * AMDRDNA_VK_PIXEL_BYTES;
		image_row = image_cpu + (uint64_t)(region->imageOffset.y + (int32_t)y) * image->pitch +
		    (uint64_t)region->imageOffset.x * AMDRDNA_VK_PIXEL_BYTES;
		if (to_image) {
			kern_memcpy(image_row, buffer_row, bytes);
		} else {
			kern_memcpy(buffer_row, image_row, bytes);
		}
	}

	/* Succeeded: the region is copied. */
	return 0;
}

/* Clears a whole image to a colour (it has one level and one layer) with a quad of the colour. */
static int
execute_clear_image(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_execute *execute,
	struct execute_state *state,
	const struct amdrdna_vk_record *record)
{
	const struct amdrdna_vk_image *image;
	struct execute_quad quad;
	int error;

	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(state);

	image = record->objects[0]->payload;

	/* The image as a target, its whole rectangle, the colour. */
	kern_memset(&quad, 0, sizeof(quad));
	error = execute_image_target(image, image->format, &quad.target);
	if (error != 0)
		return error;
	quad.x1 = (int32_t)quad.target.width;
	quad.y1 = (int32_t)quad.target.height;
	kern_memcpy(quad.colour, record->values, sizeof(quad.colour));
	error = execute_quad(execute, &quad);
	if (error != 0)
		return error;

	/* Succeeded: the clear is written. */
	return 0;
}

/*
 * Draws one internal quad: the destination as the target, the viewport
 * and scissor its region, no blending, the corners, and the executor's
 * programs with their tables (the quad over the whole viewport, and the
 * source's region in pixels with its descriptors, or the colour).
 */
static int
execute_quad(
	struct amdrdna_vk_execute *execute,
	const struct execute_quad *quad)
{
	static const uint32_t identity[4] = {
		VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
	};
	const struct amdrdna_vk_program *vertex;
	const struct amdrdna_vk_program *fragment;
	struct amdrdna_vk_sampler sampler;
	struct amdrdna_vk_blend blend;
	uint32_t image_words[AMDRDNA_IMAGE_WORDS];
	uint32_t sampler_words[AMDRDNA_SAMPLER_WORDS];
	uint32_t corners[4];
	uint32_t viewport[6];
	uint8_t *vertex_table;
	uint8_t *fragment_table;
	uint64_t vertex_va;
	uint64_t fragment_va;
	uint32_t component;
	bool inside;
	int error;

	/* A region inside the target; nothing to draw for an empty one. */
	if (quad->x1 <= quad->x0 || quad->y1 <= quad->y0)
		return 0;
	inside = execute_region_inside(quad->x0, quad->y0, (uint32_t)(quad->x1 - quad->x0), (uint32_t)(quad->y1 - quad->y0),
				       quad->target.width, quad->target.height);
	if (!inside)
		return EINVAL;

	/* The programs and their tables. */
	vertex = &execute->quad[EXECUTE_QUAD_VERTEX];
	fragment = &execute->quad[EXECUTE_QUAD_COLOUR];
	if (quad->textured)
		fragment = &execute->quad[EXECUTE_QUAD_TEXTURED];
	error = execute_table(execute, vertex->compiled, &vertex_table, &vertex_va);
	if (error == 0)
		error = execute_table(execute, fragment->compiled, &fragment_table, &fragment_va);
	if (error != 0)
		return error;

	/* The vertex program's push constants: the quad from -1 to 1, the source's region in pixels; the corners at location 0. */
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_RECT, EXECUTE_FLOAT_MINUS_ONE);
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_RECT + 4U, EXECUTE_FLOAT_MINUS_ONE);
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_RECT + 8U, EXECUTE_FLOAT_ONE);
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_RECT + 12U, EXECUTE_FLOAT_ONE);
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_TEXTURE, amdrdna_vk_float(quad->u0));
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_TEXTURE + 4U, amdrdna_vk_float(quad->v0));
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_TEXTURE + 8U, amdrdna_vk_float(quad->u1));
	execute_put32(vertex_table, AMDRDNA_QUAD_PUSH_TEXTURE + 12U, amdrdna_vk_float(quad->v1));
	amdrdna_gfx10_vertex_descriptor(execute->va + EXECUTE_CORNERS_AT, EXECUTE_CORNER_STRIDE, EXECUTE_CORNERS, AMDRDNA_GFX10_VERTEX_FLOAT2,
					2U, corners);
	execute_put_words(vertex_table, vertex->compiled->vertex_table_offset, corners, 4U);

	/* The fragment program's: the source's descriptors (a sampler in pixels, clamped), or the colour. */
	if (quad->textured) {
		error = amdrdna_vk_image_descriptor(quad->source, quad->source_width, quad->source_height, quad->source_format, identity,
						    image_words);
		if (error != 0)
			return error;
		kern_memset(&sampler, 0, sizeof(sampler));
		sampler.mag_filter = quad->filter;
		sampler.min_filter = quad->filter;
		sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.unnormalized = 1U;
		amdrdna_vk_sampler_descriptor(&sampler, sampler_words);
		execute_put_words(fragment_table, fragment->compiled->textures[0].table_offset, image_words, AMDRDNA_IMAGE_WORDS);
		execute_put_words(fragment_table, fragment->compiled->textures[0].table_offset + AMDRDNA_IMAGE_WORDS * 4U, sampler_words,
				  AMDRDNA_SAMPLER_WORDS);
	} else {
		for (component = 0U; component < 4U; component++)
			execute_put32(fragment_table, AMDRDNA_QUAD_PUSH_COLOUR + component * 4U, quad->colour[component]);
	}

	/* The target, the viewport and the scissor its region. */
	error = amdrdna_vk_state_target(&execute->pm4, &quad->target);
	if (error != 0)
		return error;
	execute_full_viewport(viewport, quad->x0, quad->y0, quad->x1, quad->y1);
	amdrdna_vk_state_viewport(&execute->pm4, viewport);
	amdrdna_vk_state_scissor(&execute->pm4, (uint32_t)quad->x0, (uint32_t)quad->y0, (uint32_t)quad->x1, (uint32_t)quad->y1);

	/* No blending, every channel, no culling. */
	kern_memset(&blend, 0, sizeof(blend));
	blend.write_mask = 0xfU;
	error = amdrdna_vk_state_blend(&execute->pm4, &blend);
	if (error != 0)
		return error;
	amdrdna_vk_state_raster(&execute->pm4, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	/* The programs, two triangles. */
	amdrdna_vk_state_programs(&execute->pm4, vertex, fragment, vertex_va, fragment_va, 0U, 0U, 0U);
	error = amdrdna_vk_state_draw(&execute->pm4, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, EXECUTE_CORNERS, 1U);
	if (error != 0)
		return error;
	execute->quads++;

	/* Succeeded: the quad is written. */
	return 0;
}

/*
 * Describes an image as a colour target in a format; EIO without storage,
 * EINVAL for a format the executor does not draw into.
 */
static int
execute_image_target(
	const struct amdrdna_vk_image *image,
	uint32_t format,
	struct amdrdna_vk_target *target)
{
	uint8_t *cpu;
	uint32_t layout;
	int error;

	/* The image's storage. */
	error = amdrdna_vk_image_address(image, &target->address, &cpu);
	if (error != 0)
		return error;

	/* A format drawn into. */
	if (!amdrdna_vk_target_format(format, &layout))
		return EINVAL;

	/* Succeeded: the target is the image. */
	target->width = image->width;
	target->height = image->height;
	target->format = format;
	return 0;
}

/* Reports whether a region lies inside an image of a size. */
static bool
execute_region_inside(
	int32_t x,
	int32_t y,
	uint32_t width,
	uint32_t height,
	uint32_t image_width,
	uint32_t image_height)
{
	/* On the image, not past its edges. */
	if (x < 0 || y < 0)
		return false;
	if ((uint32_t)x > image_width || width > image_width - (uint32_t)x)
		return false;
	if ((uint32_t)y > image_height || height > image_height - (uint32_t)y)
		return false;

	/* Succeeded: the region is inside. */
	return true;
}

/* Fills a VkViewport's floats (as bits) for a whole-pixel rectangle, depths 0 to 1. */
static void
execute_full_viewport(
	uint32_t *viewport,
	int32_t x0,
	int32_t y0,
	int32_t x1,
	int32_t y1)
{
	/* x, y, width, height, minimum depth, maximum depth. */
	viewport[0] = amdrdna_vk_float(x0);
	viewport[1] = amdrdna_vk_float(y0);
	viewport[2] = amdrdna_vk_float(x1 - x0);
	viewport[3] = amdrdna_vk_float(y1 - y0);
	viewport[4] = 0U;
	viewport[5] = EXECUTE_FLOAT_ONE;
}

/* Writes a 32-bit word at a byte offset (the GPU reads it little-endian). */
static void
execute_put32(
	uint8_t *cpu,
	uint32_t at,
	uint32_t value)
{
	/* The four bytes, low first. */
	cpu[at] = (uint8_t)value;
	cpu[at + 1U] = (uint8_t)(value >> 8);
	cpu[at + 2U] = (uint8_t)(value >> 16);
	cpu[at + 3U] = (uint8_t)(value >> 24);
}

/* Writes count 32-bit words from a byte offset. */
static void
execute_put_words(
	uint8_t *cpu,
	uint32_t at,
	const uint32_t *words,
	uint32_t count)
{
	uint32_t index;

	/* Each word in turn. */
	for (index = 0U; index < count; index++)
		execute_put32(cpu, at + index * 4U, words[index]);
}
