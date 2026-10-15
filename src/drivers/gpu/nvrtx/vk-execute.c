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
 * submissions: the push buffer, a constant buffer 0 for each draw, the
 * texture header and sampler pools, a zero page and the corners of a quad.
 * The records are turned into methods in order (exec-push.c and
 * draw-push.c) and the push buffer is run on the work channel when it is
 * full, when a copy must be done by the CPU, and at the end; each run is
 * waited for, so the run's memory is free again after it and nothing a
 * submission names is in use by the GPU once it returns.
 *
 * A draw writes everything it uses: its constants (the push constants and
 * its texture handles) in a constant buffer of its own, a header and a
 * sampler for each sampled image, the viewport, scissor, blend, vertex
 * input and programs.  A copy, a blit, a copy between a buffer and an
 * image, and a clear are drawn too: a quad of the executor's own programs
 * samples the source (in pixel coordinates) into the destination.  A copy
 * whose buffer is not laid out as a texture or a target can be (its start
 * or row pitch off their boundaries) is done by the CPU through the BAR1
 * window.  Between a pass or a transfer and what follows, the GPU waits
 * for its idle and its texture and constant caches are invalidated.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/nvrtx/nvrtx-vk-objects.h"
#include "drivers/gpu/nvrtx/nvrtx-exec.h"
#include "drivers/gpu/nvrtx/nvrtx-draw.h"
#include "drivers/gpu/nvrtx/nvrtx-gsp.h"
#include "drivers/gpu/nvrtx/compiler/sm75.h"

/* The session's run: its size and where each of its parts starts. */
#define EXECUTE_RUN_BYTES		(2U * 1024U * 1024U)
#define EXECUTE_PUSH_AT			0U
#define EXECUTE_PUSH_BYTES		(1024U * 1024U)
#define EXECUTE_CONSTANTS_AT		(1024U * 1024U)
#define EXECUTE_CONSTANTS_BYTES		(512U * 1024U)
#define EXECUTE_HEADERS_AT		(1536U * 1024U)
#define EXECUTE_SAMPLERS_AT		(1600U * 1024U)
#define EXECUTE_ZERO_AT			(1664U * 1024U)
#define EXECUTE_CORNERS_AT		(1668U * 1024U)

/* The headers and the samplers of one push buffer, each eight words. */
#define EXECUTE_POOL_ENTRIES		2048U
#define EXECUTE_POOL_ENTRY_BYTES	32U

/* One draw's constant buffer 0 (a whole number of 256-byte units): the push constants, then the texture handles; its slot in the run. */
#define EXECUTE_CONSTANTS_SIZE		0x500U
#define EXECUTE_CONSTANTS_SLOT		0x500U

/* What one record writes at most into the push buffer (a transfer of NVRTX_VK_REGIONS_MAX quads), before the run is started to make room. */
#define EXECUTE_RECORD_ROOM		32768U

/* A texture handle: the header's index, and the sampler's from bit 20. */
#define EXECUTE_HANDLE_SAMPLER_SHIFT	20U

/* The quad's corners: two triangles of six corners, two floats each. */
#define EXECUTE_CORNERS			6U
#define EXECUTE_CORNER_STRIDE		8U

/* The float bits of -1 and 1, and the bytes of an internal draw's push constants. */
#define EXECUTE_FLOAT_MINUS_ONE		0xbf800000U
#define EXECUTE_FLOAT_ONE		0x3f800000U
#define EXECUTE_INTERNAL_PUSH_BYTES	0x20U

/* A buffer written as a target: its start and its row pitch on these boundaries. */
#define EXECUTE_BUFFER_START_ALIGN	256U
#define EXECUTE_BUFFER_PITCH_ALIGN	128U

/*
 * A buffer read as a texture: the texture header holds the address from
 * bit 5 and the pitch from bit 5, so the texture starts on the 32-byte
 * boundary at or before the region and the region begins that many pixels
 * into its rows.
 */
#define EXECUTE_TEXTURE_START_ALIGN	32U
#define EXECUTE_TEXTURE_PITCH_ALIGN	32U

/*
 * The executor's own programs (a textured quad placed by push constants,
 * the P9g draw's) and the session's run.
 *
 * token names the run to the node, cpu and va its two views; at is where
 * the push buffer goes on, constants, headers and samplers how much of
 * their parts the push buffer uses, draws and quads how many application
 * draws and own quads it holds (for the node's counts).  The programs are compiled once, into
 * a run of their own.
 */
struct nvrtx_vk_execute {
	void *token;
	uint8_t *cpu;
	uint64_t va;
	size_t at;
	uint32_t constants;
	uint32_t headers;
	uint32_t samplers;
	uint32_t draws;
	uint32_t quads;
	bool started;
	void *program_token;
	uint8_t *program_cpu;
	uint64_t program_va;
	uint64_t quad_vertex;
	uint64_t quad_fragment;
	uint32_t quad_vertex_gprs;
	uint32_t quad_fragment_gprs;
	bool programs_ready;
};

/*
 * What a command buffer's records have bound so far, and the pass they
 * are in.
 */
struct execute_state {
	const struct nvrtx_vk_pipeline *pipeline;
	uint32_t viewport[6];
	bool viewport_set;
	VkRect2D scissor;
	bool scissor_set;
	const struct nvrtx_vk_descriptor_set *sets[NVRTX_VK_SETS_MAX];
	const struct nvrtx_vk_buffer *buffers[NVRTX_VK_VERTEX_BINDINGS_MAX];
	uint64_t offsets[NVRTX_VK_VERTEX_BINDINGS_MAX];
	uint8_t push[NVRTX_VK_PUSH_BYTES];
	bool in_pass;
	struct nvrtx_exec_target target;
	bool target_written;
	VkRect2D area;
};

/* One quad an internal draw makes: the destination, the source texture and its region in pixels. */
struct execute_quad {
	struct nvrtx_exec_target target;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	uint64_t source;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t source_pitch;
	uint32_t source_format;
	int32_t u0;
	int32_t v0;
	int32_t u1;
	int32_t v1;
	uint32_t filter;
};

static int execute_open(struct nvrtx_vk_session *session, struct nvrtx_vk_execute **execute);
static int execute_programs(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute);
static int execute_compile(struct nvrtx_draw_ir *program, uint8_t *cpu, uint32_t *gprs, uint32_t *bytes);
static void execute_start(struct nvrtx_vk_execute *execute);
static int execute_flush(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute);
static int execute_room(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state);
static int execute_record(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static void execute_bind(struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_begin_pass(struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_draw(struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_textures(struct nvrtx_vk_execute *execute, const struct execute_state *state, uint8_t *constants);
static int execute_texture(struct nvrtx_vk_execute *execute, const struct nvrtx_vk_view *view, const struct nvrtx_vk_sampler *sampler, uint32_t *handle);
static int execute_pool_entry(struct nvrtx_vk_execute *execute, const uint32_t *header, const uint32_t *sampler, uint32_t *handle);
static int execute_constants(struct nvrtx_vk_execute *execute, uint8_t **cpu, uint64_t *va);
static void execute_vertex_input(const struct nvrtx_vk_pipeline *pipeline, const struct execute_state *state, struct nvrtx_exec_vertex *vertex);
static int execute_transfer(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_copy_images(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_buffer_image(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_cpu_copy(const struct nvrtx_vk_buffer *buffer, const struct nvrtx_vk_image *image, const VkBufferImageCopy *region, bool to_image);
static int execute_clear_image(struct nvrtx_vk_execute *execute, struct execute_state *state, const struct nvrtx_vk_record *record);
static int execute_quad(struct nvrtx_vk_session *session, struct nvrtx_vk_execute *execute, struct execute_state *state, const struct execute_quad *quad);
static int execute_image_target(const struct nvrtx_vk_image *image, uint32_t format, struct nvrtx_exec_target *target);
static bool execute_region_inside(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t image_width, uint32_t image_height);
static void execute_full_viewport(uint32_t *viewport, int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/*
 * Runs an executable command buffer's records on the GPU and waits for
 * them.  Returns 0, EINVAL for a command buffer that is not executable, or
 * the error of the run that failed (the GPU then did not finish).
 */
int
nvrtx_vk_execute(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_command_buffer *command)
{
	struct nvrtx_vk_execute *execute;
	struct execute_state *state;
	uint32_t index;
	int error;

	/* Only an executable command buffer runs. */
	if (command->state != NVRTX_VK_COMMAND_EXECUTABLE)
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

	/* Each record in order, the push buffer started over when it fills. */
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

	/* The rest of the push buffer. */
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
nvrtx_vk_execute_close(
	struct nvrtx_vk_session *session)
{
	struct nvrtx_vk_execute *execute;

	execute = session->execute;
	if (execute == NULL)
		return;

	/* The runs, then the record. */
	if (execute->token != NULL)
		nvrtx_vk_gpu_free(session->owner, execute->token);
	if (execute->program_token != NULL)
		nvrtx_vk_gpu_free(session->owner, execute->program_token);
	kern_free(execute);
	session->execute = NULL;
}

/*
 * Gives the session's run, made at its first submission: the zero page
 * and the quad's corners written once, and the executor's programs.
 */
static int
execute_open(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute **execute)
{
	struct nvrtx_vk_execute *opened;
	static const uint32_t corners[EXECUTE_CORNERS * 2U] = {
		0U, 0U,
		EXECUTE_FLOAT_ONE, 0U,
		0U, EXECUTE_FLOAT_ONE,
		EXECUTE_FLOAT_ONE, 0U,
		EXECUTE_FLOAT_ONE, EXECUTE_FLOAT_ONE,
		0U, EXECUTE_FLOAT_ONE
	};
	uint32_t index;
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
	error = nvrtx_vk_gpu_alloc(session->owner, EXECUTE_RUN_BYTES, &opened->token, &opened->cpu, &opened->va);
	if (error != 0) {
		kern_free(opened);
		return error;
	}

	/* The zero page, and the corners: (0,0) (1,0) (0,1), (1,0) (1,1) (0,1). */
	kern_memset(opened->cpu + EXECUTE_ZERO_AT, 0, NVRTX_GSP_PAGE_SIZE);
	for (index = 0U; index < EXECUTE_CORNERS * 2U; index++)
		nvrtx_put32(opened->cpu, EXECUTE_CORNERS_AT + index * 4U, corners[index]);

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
 * Compiles the executor's quad programs (P9g's textured quad: the corners
 * placed by the push constants' rectangle, the texture read at the push
 * constants' texture rectangle) into a run of their own.
 */
static int
execute_programs(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute)
{
	struct nvrtx_draw_ir *program;
	uint8_t *cpu;
	uint64_t va;
	uint32_t bytes;
	int error;

	/* The run: a page for each program (kept from a try that did not compile). */
	if (execute->program_token == NULL) {
		error = nvrtx_vk_gpu_alloc(session->owner, 2U * NVRTX_GSP_PAGE_SIZE, &execute->program_token, &execute->program_cpu,
					   &execute->program_va);
		if (error != 0)
			return error;
	}
	cpu = execute->program_cpu;
	va = execute->program_va;

	/* The IR's storage. */
	program = kern_malloc(sizeof(*program));
	if (program == NULL)
		return ENOMEM;

	/* The vertex program in the first page, the fragment program in the second. */
	nvrtx_draw_vertex_ir(program, 1);
	error = execute_compile(program, cpu, &execute->quad_vertex_gprs, &bytes);
	if (error == 0) {
		nvrtx_draw_fragment_ir(program, 1);
		error = execute_compile(program, cpu + NVRTX_GSP_PAGE_SIZE, &execute->quad_fragment_gprs, &bytes);
	}
	kern_free(program);
	if (error != 0)
		return error;

	/* Succeeded: the programs are in place. */
	execute->quad_vertex = va;
	execute->quad_fragment = va + NVRTX_GSP_PAGE_SIZE;
	execute->programs_ready = true;
	return 0;
}

/* Compiles one quad program into a page: its header, then its code. */
static int
execute_compile(
	struct nvrtx_draw_ir *program,
	uint8_t *cpu,
	uint32_t *gprs,
	uint32_t *bytes)
{
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic diagnostic;
	uint32_t code;
	int error;

	/* Compiled for SM75. */
	binary = NULL;
	kern_memset(&diagnostic, 0, sizeof(diagnostic));
	error = nvrtx_sm75_compile_ir(&program->ir, &binary, &diagnostic);
	if (error != 0) {
		kern_logf("nvrtx: vk: a quad program did not compile (%d)\n", error);
		return error;
	}

	/* The header and the code must fit the page. */
	code = binary->code_words * 4U;
	if (binary->graphics == 0U || NVRTX_SM75_SPH_BYTES + code > NVRTX_GSP_PAGE_SIZE) {
		nvrtx_sm75_binary_free(binary);
		return EINVAL;
	}

	/* Copies them. */
	kern_memcpy(cpu, binary->sph, NVRTX_SM75_SPH_BYTES);
	kern_memcpy(cpu + NVRTX_SM75_SPH_BYTES, binary->code, code);
	*gprs = binary->gpr_count;
	*bytes = NVRTX_SM75_SPH_BYTES + code;
	nvrtx_sm75_binary_free(binary);

	/* Succeeded: the program is in place. */
	return 0;
}

/*
 * Starts a push buffer: the 3D class's state a queue starts with, the
 * header and sampler pools, and the shader caches emptied; nothing of the
 * run's parts is used yet.
 *
 * Values referenced: Mesa 25.3.6 src/nouveau/vulkan/nvk_queue.c
 * (nvk_queue_init_context_state) and nvk_cmd_draw.c
 * (nvk_push_draw_state_init).
 */
static void
execute_start(
	struct nvrtx_vk_execute *execute)
{
	uint8_t *push;

	/* The state and the pools. */
	push = execute->cpu + EXECUTE_PUSH_AT;
	execute->at = nvrtx_push_draw_init(push, 0U, execute->va + EXECUTE_ZERO_AT);
	execute->at = nvrtx_push_draw_textures(push, execute->at, execute->va + EXECUTE_HEADERS_AT, EXECUTE_POOL_ENTRIES,
					       execute->va + EXECUTE_SAMPLERS_AT, EXECUTE_POOL_ENTRIES);

	/* The programs and constants the CPU wrote since the last run are read anew, once for every draw of this push buffer. */
	execute->at = nvrtx_push_draw_invalidate(push, execute->at);

	/* Nothing of the parts is used. */
	execute->constants = 0U;
	execute->headers = 0U;
	execute->samplers = 0U;
	execute->draws = 0U;
	execute->quads = 0U;
	execute->started = true;
}

/*
 * Runs the push buffer written so far and waits for it, then starts the
 * next one.  Returns the run's error.
 */
static int
execute_flush(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute)
{
	int error;

	/* Counts its draws, then runs it. */
	nvrtx_vk_gpu_drawn(session->owner, execute->draws, execute->quads);
	error = nvrtx_vk_gpu_run(session->owner, execute->va + EXECUTE_PUSH_AT, (uint32_t)execute->at);
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done it; the next push buffer starts. */
	execute_start(execute);
	return 0;
}

/*
 * Makes room for one record: runs the push buffer when the record could
 * pass its end or the run's other parts are nearly used.  The target is
 * written again in the next push buffer.
 */
static int
execute_room(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state)
{
	bool full;
	int error;

	/* Full when the push buffer, the constants or the pools could not take a record. */
	full = false;
	if (execute->at + EXECUTE_RECORD_ROOM > EXECUTE_PUSH_BYTES)
		full = true;
	if (execute->constants + NVRTX_VK_REGIONS_MAX * EXECUTE_CONSTANTS_SLOT > EXECUTE_CONSTANTS_BYTES)
		full = true;
	if (execute->headers + NVRTX_VK_REGIONS_MAX + NVRTX_VK_TEXTURES_MAX > EXECUTE_POOL_ENTRIES)
		full = true;
	if (!full)
		return 0;

	/* Runs what there is. */
	error = execute_flush(session, execute);
	if (error != 0)
		return error;

	/* Succeeded: the next push buffer has room; its target is written anew. */
	state->target_written = false;
	return 0;
}

/* Runs one record. */
static int
execute_record(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
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
		error = execute_begin_pass(execute, state, record);
		break;
	case GPU_OP_CMD_END_RENDER_PASS:
		state->in_pass = false;
		execute->at = nvrtx_exec_barrier(execute->cpu + EXECUTE_PUSH_AT, execute->at);
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

	/* Reports a record that could not run. */
	if (error != 0)
		return error;

	/* Succeeded: the record ran (or is bound for the draws after it). */
	return 0;
}

/* Keeps what a binding record binds for the draws after it. */
static void
execute_bind(
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
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
 * and a pass whose attachment loads by clearing clears its render area.
 */
static int
execute_begin_pass(
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	const struct nvrtx_vk_render_pass *pass;
	const struct nvrtx_vk_framebuffer *framebuffer;
	const struct nvrtx_vk_view *view;
	int32_t x1;
	int32_t y1;
	int error;

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
	execute->at = nvrtx_exec_target(execute->cpu + EXECUTE_PUSH_AT, execute->at, &state->target);
	state->target_written = true;
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
	execute->at = nvrtx_exec_clear(execute->cpu + EXECUTE_PUSH_AT, execute->at, record->values,
				       (uint32_t)state->area.offset.x, (uint32_t)state->area.offset.y, (uint32_t)x1, (uint32_t)y1);

	/* Succeeded: the pass draws into its target. */
	return 0;
}

/*
 * Runs a draw in a pass: its constants (the push constants and the
 * texture handles of the fragment program's samples), the target when not
 * written yet, the viewport and scissor, the blend, the vertex input, the
 * programs and the draw.
 */
static int
execute_draw(
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	const struct nvrtx_vk_pipeline *pipeline;
	struct nvrtx_exec_vertex vertex;
	struct nvrtx_exec_blend blend;
	const uint32_t *viewport;
	const VkRect2D *scissor;
	uint8_t *push;
	uint8_t *constants;
	uint64_t constants_va;
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

	/* The draw's constants: the push constants, then the handles of its textures. */
	error = execute_constants(execute, &constants, &constants_va);
	if (error != 0)
		return error;
	kern_memcpy(constants, state->push, NVRTX_VK_PUSH_BYTES);
	error = execute_textures(execute, state, constants);
	if (error != 0)
		return error;

	/* The target, once in a push buffer. */
	push = execute->cpu + EXECUTE_PUSH_AT;
	if (!state->target_written) {
		execute->at = nvrtx_exec_target(push, execute->at, &state->target);
		state->target_written = true;
	}

	/* The viewport and the scissor: dynamic or the pipeline's, the scissor within the target. */
	viewport = pipeline->viewport;
	if (pipeline->dynamic_viewport != 0U && state->viewport_set)
		viewport = state->viewport;
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
	execute->at = nvrtx_exec_viewport(push, execute->at, viewport);
	execute->at = nvrtx_exec_scissor(push, execute->at, (uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1);

	/* The blend. */
	blend.enable = pipeline->blend_enable;
	blend.colour_op = pipeline->blend_colour_op;
	blend.colour_source = pipeline->blend_src_colour;
	blend.colour_destination = pipeline->blend_dst_colour;
	blend.alpha_op = pipeline->blend_alpha_op;
	blend.alpha_source = pipeline->blend_src_alpha;
	blend.alpha_destination = pipeline->blend_dst_alpha;
	blend.write_mask = pipeline->write_mask;
	execute->at = nvrtx_exec_blend(push, execute->at, &blend);

	/* The vertex input, the constants and the programs. */
	execute_vertex_input(pipeline, state, &vertex);
	execute->at = nvrtx_exec_vertex(push, execute->at, &vertex);
	execute->at = nvrtx_push_draw_constants(push, execute->at, constants_va, EXECUTE_CONSTANTS_SIZE);
	execute->at = nvrtx_push_draw_pipeline(push, execute->at, pipeline->vertex.va, pipeline->vertex.gprs,
					       pipeline->fragment.va, pipeline->fragment.gprs);

	/* The draw, one more in the push buffer. */
	execute->at = nvrtx_exec_draw(push, execute->at, pipeline->topology, record->values[2], record->values[0],
				      record->values[1], record->values[3]);
	execute->draws++;

	/* Succeeded: the draw is written. */
	return 0;
}

/*
 * Writes the handles of the fragment program's sampled images into a
 * draw's constants: each one's header and sampler from the bound set's
 * binding.  A binding with nothing bound refuses the draw.
 */
static int
execute_textures(
	struct nvrtx_vk_execute *execute,
	const struct execute_state *state,
	uint8_t *constants)
{
	const struct nvrtx_vk_program *program;
	const struct nvrtx_vk_descriptor_set *set;
	const struct nvrtx_vk_descriptor_slot *slot;
	uint32_t handle;
	uint32_t index;
	int error;

	/* Each sampled image of the fragment program. */
	program = &state->pipeline->fragment;
	for (index = 0U; index < program->texture_count; index++) {
		set = state->sets[program->textures[index].set];
		if (set == NULL)
			return EINVAL;
		slot = &set->slots[program->textures[index].binding];
		if (slot->view == NULL || slot->sampler == NULL)
			return EINVAL;

		/* Its handle at its slot. */
		error = execute_texture(execute, slot->view->payload, slot->sampler->payload, &handle);
		if (error != 0)
			return error;
		nvrtx_put32(constants, NVRTX_SM75_CB_TEXTURES + index * 4U, handle);
	}

	/* Succeeded: every handle is in place. */
	return 0;
}

/* Writes a view's header and a sampler into the pools and gives their handle. */
static int
execute_texture(
	struct nvrtx_vk_execute *execute,
	const struct nvrtx_vk_view *view,
	const struct nvrtx_vk_sampler *sampler,
	uint32_t *handle)
{
	const struct nvrtx_vk_image *image;
	struct nvrtx_exec_sampler words;
	uint32_t header[8];
	uint32_t sampled[8];
	uint64_t va;
	uint8_t *cpu;
	int error;

	/* The view's image, with storage. */
	image = view->image->payload;
	error = nvrtx_vk_image_address(image, &va, &cpu);
	if (error != 0)
		return error;

	/* The header and the sampler. */
	nvrtx_exec_texture_header(header, va, image->width, image->height, image->pitch, view->format, view->swizzle);
	words.mag_filter = sampler->mag_filter;
	words.min_filter = sampler->min_filter;
	words.address_u = sampler->address_u;
	words.address_v = sampler->address_v;
	words.address_w = sampler->address_w;
	words.unnormalized = sampler->unnormalized;
	nvrtx_exec_sampler_words(sampled, &words);

	/* Into the pools. */
	error = execute_pool_entry(execute, header, sampled, handle);
	if (error != 0)
		return error;

	/* Succeeded: the handle names the header and the sampler. */
	return 0;
}

/* Puts a header and a sampler into the next entries of the pools and gives their handle. */
static int
execute_pool_entry(
	struct nvrtx_vk_execute *execute,
	const uint32_t *header,
	const uint32_t *sampler,
	uint32_t *handle)
{
	uint32_t index;

	/* Room in the pools (execute_room keeps some for each record). */
	if (execute->headers >= EXECUTE_POOL_ENTRIES || execute->samplers >= EXECUTE_POOL_ENTRIES)
		return ENOSPC;

	/* The two entries' words. */
	for (index = 0U; index < 8U; index++) {
		nvrtx_put32(execute->cpu, EXECUTE_HEADERS_AT + execute->headers * EXECUTE_POOL_ENTRY_BYTES + index * 4U, header[index]);
		nvrtx_put32(execute->cpu, EXECUTE_SAMPLERS_AT + execute->samplers * EXECUTE_POOL_ENTRY_BYTES + index * 4U, sampler[index]);
	}

	/* The handle: the header's index, the sampler's above it. */
	*handle = execute->headers | (execute->samplers << EXECUTE_HANDLE_SAMPLER_SHIFT);
	execute->headers++;
	execute->samplers++;

	/* Succeeded: the entries are the draw's. */
	return 0;
}

/* Gives a draw's constant buffer: the next slot of the run's constants, cleared. */
static int
execute_constants(
	struct nvrtx_vk_execute *execute,
	uint8_t **cpu,
	uint64_t *va)
{
	/* Room for one more (execute_room keeps some for each record). */
	if (execute->constants + EXECUTE_CONSTANTS_SLOT > EXECUTE_CONSTANTS_BYTES)
		return ENOSPC;

	/* The slot. */
	*cpu = execute->cpu + EXECUTE_CONSTANTS_AT + execute->constants;
	*va = execute->va + EXECUTE_CONSTANTS_AT + execute->constants;
	kern_memset(*cpu, 0, EXECUTE_CONSTANTS_SIZE);
	execute->constants += EXECUTE_CONSTANTS_SLOT;

	/* Succeeded: the slot is the draw's. */
	return 0;
}

/*
 * Fills a draw's vertex input: the pipeline's attributes, each from the
 * stream of its binding, and each binding's stream at its bound buffer
 * (a binding without storage stays off: its attributes read the zero
 * page).
 */
static void
execute_vertex_input(
	const struct nvrtx_vk_pipeline *pipeline,
	const struct execute_state *state,
	struct nvrtx_exec_vertex *vertex)
{
	const struct nvrtx_vk_vertex_binding *binding;
	const struct nvrtx_vk_buffer *buffer;
	struct nvrtx_exec_stream *stream;
	uint64_t va;
	uint8_t *cpu;
	uint32_t index;
	int error;

	/* The attributes: the stream is the binding's number. */
	kern_memset(vertex, 0, sizeof(*vertex));
	vertex->attribute_count = pipeline->attribute_count;
	for (index = 0U; index < pipeline->attribute_count; index++) {
		vertex->attributes[index].location = pipeline->attributes[index].location;
		vertex->attributes[index].stream = pipeline->attributes[index].binding;
		vertex->attributes[index].offset = pipeline->attributes[index].offset;
		vertex->attributes[index].format = pipeline->attributes[index].format;
	}

	/* The streams of the pipeline's bindings. */
	for (index = 0U; index < pipeline->binding_count; index++) {
		binding = &pipeline->bindings[index];
		buffer = state->buffers[binding->binding];
		if (buffer == NULL)
			continue;
		error = nvrtx_vk_buffer_address(buffer, &va, &cpu);
		if (error != 0 || state->offsets[binding->binding] >= buffer->size)
			continue;

		stream = &vertex->streams[binding->binding];
		stream->enabled = 1U;
		stream->address = va + state->offsets[binding->binding];
		stream->bytes = buffer->size - state->offsets[binding->binding];
		stream->stride = binding->stride;
		stream->instance = binding->instance;
	}
}

/*
 * Runs a transfer outside a pass: the barrier before it (the work before
 * may have written its source), the transfer, the barrier after it (the
 * target written anew in the next pass).
 */
static int
execute_transfer(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	int error;

	/* What wrote the source has finished. */
	execute->at = nvrtx_exec_barrier(execute->cpu + EXECUTE_PUSH_AT, execute->at);

	/* The transfer. */
	if (record->opcode == GPU_OP_CMD_COPY_IMAGE || record->opcode == GPU_OP_CMD_BLIT_IMAGE) {
		error = execute_copy_images(session, execute, state, record);
	} else if (record->opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
		error = execute_clear_image(execute, state, record);
	} else {
		error = execute_buffer_image(session, execute, state, record);
	}
	if (error != 0)
		return error;

	/* What follows reads what it wrote; the pass's target is written again. */
	execute->at = nvrtx_exec_barrier(execute->cpu + EXECUTE_PUSH_AT, execute->at);
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
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	const struct nvrtx_vk_image *source;
	const struct nvrtx_vk_image *destination;
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

	source = record->objects[0]->payload;
	destination = record->objects[1]->payload;

	/* The two images, with storage. */
	kern_memset(&quad, 0, sizeof(quad));
	error = execute_image_target(destination, destination->format, &quad.target);
	if (error == 0)
		error = nvrtx_vk_image_address(source, &quad.source, &cpu);
	if (error != 0)
		return error;
	quad.source_width = source->width;
	quad.source_height = source->height;
	quad.source_pitch = source->pitch;
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

		error = execute_quad(session, execute, state, &quad);
		if (error != 0)
			return error;
	}

	/* Succeeded: the regions are copied. */
	return 0;
}

/*
 * Copies regions between a buffer and an image.  A buffer region laid out
 * as a texture can be (to an image) or as a target can be (from an image)
 * is drawn; another is copied by the CPU after the work before it ran.
 */
static int
execute_buffer_image(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	const struct nvrtx_vk_buffer *buffer;
	const struct nvrtx_vk_image *image;
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
	uint32_t skew;
	bool inside;
	bool drawn;
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
	error = nvrtx_vk_buffer_address(buffer, &buffer_va, &buffer_cpu);
	if (error == 0)
		error = nvrtx_vk_image_address(image, &image_va, &image_cpu);
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
		pitch = row * NVRTX_VK_PIXEL_BYTES;
		if (region->bufferOffset > buffer->size ||
		    (uint64_t)pitch * (region->imageExtent.height - 1U) + (uint64_t)region->imageExtent.width * NVRTX_VK_PIXEL_BYTES >
		    buffer->size - region->bufferOffset)
			return EINVAL;
		start = buffer_va + region->bufferOffset;

		/*
		 * The GPU draws the region when the buffer can be its texture (whole
		 * pixels, rows on 32 bytes) or its target (start on 256 bytes, rows
		 * on 128).
		 */
		drawn = false;
		if (to_image) {
			if ((start % NVRTX_VK_PIXEL_BYTES) == 0U && (pitch % EXECUTE_TEXTURE_PITCH_ALIGN) == 0U)
				drawn = true;
		} else if ((start % EXECUTE_BUFFER_START_ALIGN) == 0U && (pitch % EXECUTE_BUFFER_PITCH_ALIGN) == 0U) {
			drawn = true;
		}

		/* Any other region is copied by the CPU, after the work before it. */
		if (!drawn) {
			error = execute_flush(session, execute);
			if (error == 0)
				error = execute_cpu_copy(buffer, image, region, to_image);
			if (error != 0)
				return error;

			/* Counted for nvrtx.vk.perf=1; the next draw writes its target anew. */
			nvrtx_vk_gpu_copied(session->owner, (uint64_t)region->imageExtent.width * NVRTX_VK_PIXEL_BYTES * region->imageExtent.height);
			state->target_written = false;
			continue;
		}

		/* Drawn: the destination's region samples the source's, one to one. */
		kern_memset(&quad, 0, sizeof(quad));
		quad.filter = VK_FILTER_NEAREST;
		if (to_image) {
			/* The texture from the 32-byte boundary at or before the region; the region skew pixels into its rows. */
			skew = (uint32_t)(start % EXECUTE_TEXTURE_START_ALIGN) / NVRTX_VK_PIXEL_BYTES;
			error = execute_image_target(image, image->format, &quad.target);
			quad.x0 = region->imageOffset.x;
			quad.y0 = region->imageOffset.y;
			quad.source = start - (uint64_t)skew * NVRTX_VK_PIXEL_BYTES;
			quad.source_width = skew + region->imageExtent.width;
			quad.source_height = region->imageExtent.height;
			quad.source_pitch = pitch;
			quad.source_format = image->format;
			quad.u0 = (int32_t)skew;
			quad.v0 = 0;
		} else {
			quad.target.address = start;
			quad.target.width = region->imageExtent.width;
			quad.target.height = region->imageExtent.height;
			quad.target.pitch = pitch;
			quad.target.format = image->format;
			quad.x0 = 0;
			quad.y0 = 0;
			quad.source = image_va;
			quad.source_width = image->width;
			quad.source_height = image->height;
			quad.source_pitch = image->pitch;
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
		error = execute_quad(session, execute, state, &quad);
		if (error != 0)
			return error;
	}

	/* Succeeded: the regions are copied. */
	return 0;
}

/*
 * Copies one region between a buffer and an image by the CPU, row by row
 * through the BAR1 window (the GPU is idle: the work before has run).
 */
static int
execute_cpu_copy(
	const struct nvrtx_vk_buffer *buffer,
	const struct nvrtx_vk_image *image,
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
	error = nvrtx_vk_buffer_address(buffer, &va, &buffer_cpu);
	if (error == 0)
		error = nvrtx_vk_image_address(image, &va, &image_cpu);
	if (error != 0)
		return error;

	/* The rows: the buffer's at its row length, the image's at its pitch. */
	row_pixels = region->bufferRowLength;
	if (row_pixels == 0U)
		row_pixels = region->imageExtent.width;
	bytes = region->imageExtent.width * NVRTX_VK_PIXEL_BYTES;
	for (y = 0U; y < region->imageExtent.height; y++) {
		buffer_row = buffer_cpu + region->bufferOffset + (uint64_t)y * row_pixels * NVRTX_VK_PIXEL_BYTES;
		image_row = image_cpu + (uint64_t)(region->imageOffset.y + (int32_t)y) * image->pitch +
		    (uint64_t)region->imageOffset.x * NVRTX_VK_PIXEL_BYTES;
		if (to_image) {
			kern_memcpy(image_row, buffer_row, bytes);
		} else {
			kern_memcpy(buffer_row, image_row, bytes);
		}
	}

	/* Succeeded: the region is copied. */
	return 0;
}

/* Clears a whole image to a colour (it has one level and one layer). */
static int
execute_clear_image(
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct nvrtx_vk_record *record)
{
	const struct nvrtx_vk_image *image;
	struct nvrtx_exec_target target;
	uint8_t *push;
	int error;

	image = record->objects[0]->payload;

	/* The image as a target. */
	error = execute_image_target(image, image->format, &target);
	if (error != 0)
		return error;

	/* The target, then its whole rectangle cleared. */
	push = execute->cpu + EXECUTE_PUSH_AT;
	execute->at = nvrtx_exec_target(push, execute->at, &target);
	execute->at = nvrtx_exec_clear(push, execute->at, record->values, 0U, 0U, target.width, target.height);
	state->target_written = false;

	/* Succeeded: the clear is written. */
	return 0;
}

/*
 * Draws one internal quad: the destination as the target, the viewport
 * and scissor its region, no blending, the corners, the constants (the
 * quad over the whole viewport and the source's region in pixels) with
 * the source's handle, and the executor's programs.
 */
static int
execute_quad(
	struct nvrtx_vk_session *session,
	struct nvrtx_vk_execute *execute,
	struct execute_state *state,
	const struct execute_quad *quad)
{
	struct nvrtx_exec_sampler sampler;
	struct nvrtx_exec_vertex vertex;
	struct nvrtx_exec_blend blend;
	static const uint32_t identity[4] = {
		VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
	};
	uint32_t viewport[6];
	uint32_t header[8];
	uint32_t sampled[8];
	uint32_t handle;
	uint8_t *push;
	uint8_t *constants;
	uint64_t constants_va;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	bool inside;
	int error;

	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(state);

	/* A region inside the target; nothing to draw for an empty one. */
	inside = execute_region_inside(quad->x0, quad->y0, (uint32_t)(quad->x1 - quad->x0), (uint32_t)(quad->y1 - quad->y0),
				       quad->target.width, quad->target.height);
	if (quad->x1 <= quad->x0 || quad->y1 <= quad->y0)
		return 0;
	if (!inside)
		return EINVAL;

	/* The source's header and a sampler in pixels, clamped. */
	nvrtx_exec_texture_header(header, quad->source, quad->source_width, quad->source_height, quad->source_pitch,
				  quad->source_format, identity);
	kern_memset(&sampler, 0, sizeof(sampler));
	sampler.mag_filter = quad->filter;
	sampler.min_filter = quad->filter;
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.unnormalized = 1U;
	nvrtx_exec_sampler_words(sampled, &sampler);
	error = execute_pool_entry(execute, header, sampled, &handle);
	if (error != 0)
		return error;

	/* The constants: the quad from -1 to 1, the source's region, the handle. */
	error = execute_constants(execute, &constants, &constants_va);
	if (error != 0)
		return error;
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_RECT, EXECUTE_FLOAT_MINUS_ONE);
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_RECT + 4U, EXECUTE_FLOAT_MINUS_ONE);
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_RECT + 8U, EXECUTE_FLOAT_ONE);
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_RECT + 12U, EXECUTE_FLOAT_ONE);
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_TEXTURE, nvrtx_exec_float(quad->u0));
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_TEXTURE + 4U, nvrtx_exec_float(quad->v0));
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_TEXTURE + 8U, nvrtx_exec_float(quad->u1));
	nvrtx_put32(constants, NVRTX_DRAW_PUSH_TEXTURE + 12U, nvrtx_exec_float(quad->v1));
	nvrtx_put32(constants, NVRTX_SM75_CB_TEXTURES, handle);

	/* The target, the viewport and the scissor. */
	push = execute->cpu + EXECUTE_PUSH_AT;
	x0 = quad->x0;
	y0 = quad->y0;
	x1 = quad->x1;
	y1 = quad->y1;
	execute->at = nvrtx_exec_target(push, execute->at, &quad->target);
	execute_full_viewport(viewport, x0, y0, x1, y1);
	execute->at = nvrtx_exec_viewport(push, execute->at, viewport);
	execute->at = nvrtx_exec_scissor(push, execute->at, (uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1);

	/* No blending, every channel. */
	kern_memset(&blend, 0, sizeof(blend));
	blend.write_mask = 0xfU;
	execute->at = nvrtx_exec_blend(push, execute->at, &blend);

	/* The corners at location 0, the constants, the programs, two triangles. */
	kern_memset(&vertex, 0, sizeof(vertex));
	vertex.attribute_count = 1U;
	vertex.attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	vertex.streams[0].enabled = 1U;
	vertex.streams[0].address = execute->va + EXECUTE_CORNERS_AT;
	vertex.streams[0].bytes = EXECUTE_CORNERS * EXECUTE_CORNER_STRIDE;
	vertex.streams[0].stride = EXECUTE_CORNER_STRIDE;
	execute->at = nvrtx_exec_vertex(push, execute->at, &vertex);
	execute->at = nvrtx_push_draw_constants(push, execute->at, constants_va, EXECUTE_CONSTANTS_SIZE);
	execute->at = nvrtx_push_draw_pipeline(push, execute->at, execute->quad_vertex, execute->quad_vertex_gprs,
					       execute->quad_fragment, execute->quad_fragment_gprs);
	execute->at = nvrtx_exec_draw(push, execute->at, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0U, EXECUTE_CORNERS, 1U, 0U);
	execute->quads++;

	/* Succeeded: the quad is written. */
	return 0;
}

/*
 * Describes an image as a colour target in a format; EIO without storage,
 * EINVAL for a format the executor does not render to.
 */
static int
execute_image_target(
	const struct nvrtx_vk_image *image,
	uint32_t format,
	struct nvrtx_exec_target *target)
{
	uint8_t *cpu;
	uint32_t code;
	bool renderable;
	int error;

	/* The image's storage. */
	error = nvrtx_vk_image_address(image, &target->address, &cpu);
	if (error != 0)
		return error;

	/* A format rendered to. */
	renderable = nvrtx_exec_target_format(format, &code);
	if (!renderable)
		return EINVAL;

	/* Succeeded: the target is the image. */
	target->width = image->width;
	target->height = image->height;
	target->pitch = image->pitch;
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
	/* x, y, width, height, min depth, max depth. */
	viewport[0] = nvrtx_exec_float(x0);
	viewport[1] = nvrtx_exec_float(y0);
	viewport[2] = nvrtx_exec_float(x1 - x0);
	viewport[3] = nvrtx_exec_float(y1 - y0);
	viewport[4] = 0U;
	viewport[5] = EXECUTE_FLOAT_ONE;
}
