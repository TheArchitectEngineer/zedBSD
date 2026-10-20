/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs one render job of a render pass instance.
 *
 * A job without draws runs an empty geometry stream; its fragment clears
 * or loads every tile of the render area through the background object
 * and stores it through the end-of-tile program.  The programs, the
 * shared words and the stream are written into memory of the job's own,
 * mapped in the session's windows, and returned once the render is done.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-job.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/pds-encode.h"
#include "drivers/gpu/powervr/render-command.h"
#include "drivers/gpu/powervr/render-kick-layout.h"
#include "drivers/gpu/powervr/render-load-program.h"
#include "drivers/gpu/powervr/render-program.h"
#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/render-draw.h"
#include "drivers/gpu/powervr/render-depth.h"
#include "drivers/gpu/powervr/vk-draw.h"
#include "drivers/gpu/powervr/vk.h"

/* How long a render may take. */
#define JOB_WAIT_MS			2000U

/*
 * The job's memory: in the general window the stream (the word that
 * ends it) and the load's shared words; in the data sequencer's window
 * the end-of-tile data, the background's task program (data, then code
 * after eight words) and its shared register program's code and data;
 * in the shading cluster's the end-of-tile and background programs.
 */
#define GENERAL_STREAM			0x0U
#define GENERAL_LOAD_WORDS		0x800U
#define GENERAL_DBIAS			0x900U
#define GENERAL_SCISSORS		0xa00U
#define GENERAL_BYTES			0x1000U

/*
 * Each draw's memory: in its slot of the draws' general buffer the
 * primitive state block, the push constants, the blend constants, then
 * its descriptor sets' copies after every slot; in its slot of the
 * data sequencer buffer the vertex program's data and code, the vertex
 * and fragment shared register programs' code and data, the fragment's
 * task program and the coefficient program (data, then code); its
 * shaders' code one after another in the shading cluster buffer.
 */
#define DRAW_SLOT			0x1000U
#define DRAW_PPP			0x000U
#define DRAW_PUSH			0x100U
#define DRAW_BLEND			0x200U
#define DRAW_PDS_VERTEX_DATA		0x000U
#define DRAW_PDS_VERTEX_CODE		0x400U
#define DRAW_PDS_VSHARED_CODE		0x500U
#define DRAW_PDS_VSHARED_DATA		0x600U
#define DRAW_PDS_KICK			0x800U
#define DRAW_PDS_FSHARED_CODE		0x880U
#define DRAW_PDS_FSHARED_DATA		0x980U
#define DRAW_PDS_COEFFICIENT		0xb80U
#define DRAW_STREAM_ROOM		(GENERAL_LOAD_WORDS - GENERAL_STREAM)

_Static_assert(DRAW_STREAM_ROOM >= (POWERVR_RENDER_DRAWS * POWERVR_DRAW_VDM_WORDS + 1U) * 4U, "the stream's room");
_Static_assert(POWERVR_DRAW_PPP_WORDS * 4U <= DRAW_PUSH, "the primitive state block's room");
_Static_assert(POWERVR_DRAW_PUSH_BYTES <= DRAW_BLEND - DRAW_PUSH, "the push constants' room");
_Static_assert(POWERVR_DATA_VERTEX_DATA_MAX * 4U <= DRAW_PDS_VERTEX_CODE, "the vertex program's data room");
_Static_assert(POWERVR_DATA_VERTEX_CODE_MAX * 4U <= DRAW_PDS_VSHARED_CODE - DRAW_PDS_VERTEX_CODE, "the vertex program's code room");
_Static_assert(POWERVR_DATA_SHARED_CODE_MAX * 4U <= DRAW_PDS_VSHARED_DATA - DRAW_PDS_VSHARED_CODE, "shared code room");
_Static_assert(POWERVR_DATA_SHARED_DATA_MAX * 4U <= DRAW_PDS_KICK - DRAW_PDS_VSHARED_DATA, "shared data room");
_Static_assert(POWERVR_DATA_SHARED_CODE_MAX * 4U <= DRAW_PDS_FSHARED_DATA - DRAW_PDS_FSHARED_CODE, "fragment shared code room");
_Static_assert(POWERVR_DATA_SHARED_DATA_MAX * 4U <= DRAW_PDS_COEFFICIENT - DRAW_PDS_FSHARED_DATA, "fragment shared data room");
_Static_assert((POWERVR_DRAW_COEFFICIENT_DATA + 3U) / 4U * 16U + POWERVR_DRAW_COEFFICIENT_CODE * 4U <= DRAW_SLOT - DRAW_PDS_COEFFICIENT, "coefficient room");
_Static_assert(POWERVR_RENDER_DRAWS * POWERVR_DRAW_SCISSOR_WORDS * 4U <= GENERAL_BYTES - GENERAL_SCISSORS, "scissor table room");
#define PDS_EVENT			0x000U
#define PDS_KICK			0x040U
#define PDS_SHARED_CODE			0x080U
#define PDS_SHARED_DATA			0x200U
#define PDS_BYTES			0x1000U
#define PDS_SHARED_CODE_ROOM		(PDS_SHARED_DATA - PDS_SHARED_CODE)
#define PDS_SHARED_DATA_ROOM		(PDS_BYTES - PDS_SHARED_DATA)
#define USC_EOT				0x000U
#define USC_BACKGROUND			0x100U
#define USC_BYTES			0x1000U

/*
 * The load operations of a colour attachment the job reads.
 *
 * Values referenced:
 * Khronos Vulkan 1.x vulkan_core.h (VK_ATTACHMENT_LOAD_OP_LOAD 0,
 * VK_ATTACHMENT_LOAD_OP_CLEAR 1).
 */
#define LOAD_OP_LOAD			0U
#define LOAD_OP_CLEAR			1U

/* The job's memory and programs while it runs. */
struct job_memory {
	struct powervr_resource *general;
	struct powervr_buffer *pds;
	struct powervr_buffer *usc;
	uint64_t pds_place;
	uint64_t usc_place;
	struct powervr_render_usc program;
	struct powervr_render_kick kick;
	struct powervr_data_shared_program shared;
	struct powervr_fragment_setup fragment_setup;
	struct powervr_geometry_setup geometry_setup;
	uint8_t geometry[POWERVR_GEOMETRY_BYTES];
	uint8_t fragment[POWERVR_FRAGMENT_BYTES];
	struct powervr_render_marks marks;
	struct powervr_resource *draw_general;
	struct powervr_buffer *draw_pds;
	struct powervr_buffer *draw_usc;
	uint64_t draw_pds_place;
	uint64_t draw_usc_place;
	struct powervr_data_load loads[POWERVR_DATA_LOADS_MAX];
	struct powervr_data_vertex_dma dmas[POWERVR_DATA_VERTEX_DMAS_MAX];
	struct powervr_data_vertex_program vertex;
	uint32_t ppp[POWERVR_DRAW_PPP_WORDS];
	uint32_t vdm[POWERVR_DRAW_VDM_WORDS];
};

static int job_check(const struct powervr_render_job *job);
static int job_map(struct powervr_render *render, struct powervr_session *session, struct job_memory *memory);
static int job_programs(const struct powervr_render_job *job, struct job_memory *memory, uint32_t *shared_registers);
static int job_background(const struct powervr_render_target *color, struct job_memory *memory, uint32_t pds_base, uint32_t usc_base, uint32_t *shared_registers);
static int job_draws(struct powervr_render *render, struct powervr_session *session, const struct powervr_render_job *job, struct job_memory *memory, uint32_t *stream_words, uint32_t *vertex_shareds);
static int draw_place(const struct powervr_render_job *job, uint32_t index, struct job_memory *memory, uint64_t sets_place, uint64_t usc_place, uint32_t stream_at, uint32_t *words);
static int draw_shared(const struct powervr_shader_program *program, const struct powervr_data_state *state, struct job_memory *memory, uint32_t code_at, uint32_t data_at, uint32_t *data_words);
static void job_release(struct powervr_render *render, struct powervr_session *session, struct job_memory *memory);
static void put_words(struct powervr_buffer *buffer, uint32_t offset, const uint32_t *words, unsigned count);
static void put_bytes(struct powervr_buffer *buffer, uint32_t offset, const uint8_t *bytes, size_t count);

/*
 * Runs a render job without draws.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_sub_cmd_gfx_job_init: an empty geometry stream for a render pass
 * instance without draws, the load operations as the background, the
 * store through the pixel event).
 */
int
powervr_render_job_run(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_render_job *job)
{
	struct powervr_render_context *context;
	struct powervr_target_shape shape;
	struct powervr_target *target;
	const struct powervr_target_layout *layout;
	struct job_memory *memory;
	struct powervr_render_zls zls;
	uint32_t shared_registers;
	uint32_t vertex_shareds;
	uint32_t stream_words;
	uint32_t terminate;
	int error;

	/* Refuses what the job runner does not build yet. */
	error = job_check(job);
	if (error != 0)
		return error;

	/* The session's render context and a target of the frame. */
	error = powervr_session_render_context(render, session, &context);
	if (error != 0)
		return error;

	shape.width = job->width;
	shape.height = job->height;
	shape.samples = 1;
	shape.layers = 1;
	error = powervr_session_target(render, session, &shape, &target);
	if (error != 0)
		return error;

	/* The job's memory and its programs. */
	memory = kern_calloc(1, sizeof(*memory));
	if (memory == NULL)
		return ENOMEM;

	error = job_map(render, session, memory);
	if (error == 0)
		error = job_programs(job, memory, &shared_registers);
	if (error != 0) {
		job_release(render, session, memory);
		return error;
	}

	/* The draws' programs and streams, then the stream's end. */
	stream_words = 0;
	vertex_shareds = 0;
	if (job->draw_count != 0) {
		error = job_draws(render, session, job, memory, &stream_words, &vertex_shareds);
		if (error != 0) {
			job_release(render, session, memory);
			return error;
		}
	}
	if (vertex_shareds > shared_registers)
		shared_registers = vertex_shareds;
	terminate = POWERVR_VDM_TERMINATE;
	put_words(memory->general->buffer, GENERAL_STREAM + stream_words * 4U, &terminate, 1);

	/* The commands: the stream, the fragment of the target's tiles with the draws' tables. */
	memory->fragment_setup.scissor_table = memory->general->address + GENERAL_SCISSORS;
	memory->fragment_setup.dbias_table = memory->general->address + GENERAL_DBIAS;

	/* A depth target's loads, stores and background depth. */
	if (job->depth.present) {
		error = powervr_render_zls_words(&job->depth, &zls);
		if (error != 0) {
			job_release(render, session, memory);
			return error;
		}
		memory->fragment_setup.has_depth = 1;
		memory->fragment_setup.zls_control = zls.control;
		memory->fragment_setup.zls_base = zls.base;
		memory->fragment_setup.background_depth = zls.background_depth;
		memory->fragment_setup.zls_stride = zls.stride;
	}
	layout = powervr_target_layout_of(target);
	memory->fragment_setup.tiles_x = layout->tiles_x;
	memory->fragment_setup.tiles_y = layout->tiles_y;
	powervr_fragment_command(&memory->fragment_setup, memory->fragment);
	memory->geometry_setup.stream = memory->general->address + GENERAL_STREAM;
	memory->geometry_setup.region_header_bytes = layout->region_header_bytes;
	memory->geometry_setup.shared_registers = shared_registers;
	powervr_geometry_command(&memory->geometry_setup, memory->geometry);

	/* The kicks; the partial render runs the fragment's own command. */
	mutex_lock(&render->core->lock);

	error = powervr_session_kick_render(render, session, target, 0, memory->geometry, memory->fragment, memory->fragment, NULL, 0, &memory->marks);

	mutex_unlock(&render->core->lock);

	if (error != 0) {
		job_release(render, session, memory);
		return error;
	}

	/* The wait; the render's end says the TA and 3D stages. */
	error = powervr_render_finish(render, session, &memory->marks, JOB_WAIT_MS);
	if (error != 0) {
		kern_logf("powervr: render job did not finish (%d); its memory stays with the core\n", error);
		session->abandoned = 1;
		return error;
	}

	/* Succeeded: the render is done and its memory goes. */
	job_release(render, session, memory);
	return 0;
}

/* Refuses a job the runner does not build yet. */
static int
job_check(
	const struct powervr_render_job *job)
{
	const struct powervr_render_target *color;

	/* One colour attachment, draws each with a pipeline. */
	if (job->color_count != 1 || job->draw_count > POWERVR_RENDER_DRAWS)
		return ENOTSUP;

	/* A colour of one 32-bit word a pixel. */
	color = &job->colors[0];
	if (color->surface.format == NULL || color->surface.format->texel_bytes != 4U)
		return ENOTSUP;

	/* Succeeded: the job can run. */
	return 0;
}

/* Makes and maps the job's memory. */
static int
job_map(
	struct powervr_render *render,
	struct powervr_session *session,
	struct job_memory *memory)
{
	struct powervr_buffer *buffer;
	int error;

	/* The general memory. */
	error = powervr_buffer_create(render, GENERAL_BYTES, &buffer);
	if (error != 0)
		return error;

	error = powervr_resource_make(session, buffer, 1, 0, &memory->general);
	powervr_buffer_put(render, buffer);
	if (error != 0) {
		memory->general = NULL;
		return error;
	}

	/* The program buffers, in their windows. */
	error = powervr_buffer_create(render, PDS_BYTES, &memory->pds);
	if (error != 0) {
		memory->pds = NULL;
		return error;
	}

	error = powervr_session_program_map(session, POWERVR_PROGRAM_PDS, memory->pds, &memory->pds_place);
	if (error != 0) {
		memory->pds_place = 0;
		return error;
	}

	error = powervr_buffer_create(render, USC_BYTES, &memory->usc);
	if (error != 0) {
		memory->usc = NULL;
		return error;
	}

	error = powervr_session_program_map(session, POWERVR_PROGRAM_USC, memory->usc, &memory->usc_place);
	if (error != 0) {
		memory->usc_place = 0;
		return error;
	}

	/* Succeeded: the memory is mapped. */
	return 0;
}

/*
 * Writes the job's programs: the end-of-tile program storing the colour
 * attachment over the render area, and the background.
 */
static int
job_programs(
	const struct powervr_render_job *job,
	struct job_memory *memory,
	uint32_t *shared_registers)
{
	const struct powervr_render_target *color;
	uint32_t state[2];
	uint32_t event[POWERVR_RENDER_EVENT_DATA_WORDS];
	uint32_t pds_base;
	uint32_t usc_base;
	uint32_t code_word;
	int error;

	/* The places as offsets from the windows' bases. */
	color = &job->colors[0];
	pds_base = (uint32_t)(memory->pds_place - POWERVR_PDS_HEAP_BASE);
	usc_base = (uint32_t)(memory->usc_place - POWERVR_USC_HEAP_BASE);

	/* The back end words over the render area, and the end-of-tile program that emits them. */
	error = powervr_render_pbe_words(&color->surface, &job->area, state, memory->fragment_setup.pbe);
	if (error != 0)
		return error;

	error = powervr_render_eot_usc(state, &memory->program);
	if (error != 0)
		return error;

	put_bytes(memory->usc, USC_EOT, memory->program.code, memory->program.length);
	error = powervr_render_event_data(usc_base + USC_EOT, memory->program.temps, event);
	if (error != 0)
		return error;

	put_words(memory->pds, PDS_EVENT, event, POWERVR_RENDER_EVENT_DATA_WORDS);
	error = powervr_render_event_words(pds_base + PDS_EVENT, &code_word, &memory->fragment_setup.event_data, &memory->fragment_setup.event_info);
	if (error != 0)
		return error;

	/* The background, which a partial render runs again. */
	error = job_background(color, memory, pds_base, usc_base, shared_registers);
	if (error != 0)
		return error;

	kern_memcpy(memory->fragment_setup.partial_background, memory->fragment_setup.background, sizeof(memory->fragment_setup.background));

	/* Succeeded: the programs are written. */
	return 0;
}

/*
 * Writes the background: the attachment loaded for LOAD, cleared to the
 * clear value for CLEAR, and to zero when its contents do not matter.
 */
static int
job_background(
	const struct powervr_render_target *color,
	struct job_memory *memory,
	uint32_t pds_base,
	uint32_t usc_base,
	uint32_t *shared_registers)
{
	struct powervr_render_background background;
	uint32_t words[POWERVR_RENDER_LOAD_SHARED_WORDS];
	uint32_t value;
	int error;

	/* The background's program and the shared register program that feeds it. */
	if (color->load_op == LOAD_OP_LOAD) {
		/* The attachment's image and sampler words, and the program that samples it. */
		error = powervr_render_load_usc(color->surface.format, &memory->program);
		if (error != 0)
			return error;

		error = powervr_render_load_words(&color->view, words);
		if (error != 0)
			return error;

		put_words(memory->general->buffer, GENERAL_LOAD_WORDS, words, POWERVR_RENDER_LOAD_SHARED_WORDS);
		error = powervr_render_load_shared(memory->general->address + GENERAL_LOAD_WORDS, &memory->shared);
		if (error != 0)
			return error;

		*shared_registers = POWERVR_RENDER_LOAD_SHARED_WORDS;
	} else {
		/* The clear value, or zero for contents that do not matter. */
		value = 0;
		if (color->load_op == LOAD_OP_CLEAR)
			value = color->clear[0];
		error = powervr_render_clear_usc(&memory->program);
		if (error != 0)
			return error;

		error = powervr_render_clear_shared(value, &memory->shared);
		if (error != 0)
			return error;

		*shared_registers = 1;
	}

	/* The program, its task program and its shared register program. */
	if (memory->shared.code_words * 4U > PDS_SHARED_CODE_ROOM || memory->shared.data_words * 4U > PDS_SHARED_DATA_ROOM)
		return E2BIG;

	put_bytes(memory->usc, USC_BACKGROUND, memory->program.code, memory->program.length);
	error = powervr_render_kick_build(usc_base + USC_BACKGROUND, memory->program.temps, &memory->kick);
	if (error != 0)
		return error;

	put_words(memory->pds, PDS_KICK, memory->kick.data, POWERVR_RENDER_KICK_DATA_WORDS);
	put_words(memory->pds, PDS_KICK + POWERVR_RENDER_KICK_DATA_WORDS * 4U, memory->kick.code, POWERVR_RENDER_KICK_CODE_WORDS);
	put_words(memory->pds, PDS_SHARED_CODE, memory->shared.code, memory->shared.code_words);
	put_words(memory->pds, PDS_SHARED_DATA, memory->shared.data, memory->shared.data_words);

	/* The background's words. */
	background.kick_offset = pds_base + PDS_KICK;
	background.shared_code_offset = pds_base + PDS_SHARED_CODE;
	background.shared_data_offset = pds_base + PDS_SHARED_DATA;
	background.shared_data_words = memory->shared.data_words;
	background.shared_registers = *shared_registers;
	error = powervr_render_background_words(&background, memory->fragment_setup.background);
	if (error != 0)
		return error;

	/* Succeeded: the background is written. */
	return 0;
}

/*
 * Makes the draws' memory and places every draw's programs, tables and
 * stream: the shading cluster code of each draw's two shaders, then for
 * each draw its slot of data sequencer programs and general memory, its
 * scissor and its part of the stream.  Reports the stream's words and
 * the most shared registers a vertex shader takes.
 */
static int
job_draws(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_render_job *job,
	struct job_memory *memory,
	uint32_t *stream_words,
	uint32_t *vertex_shareds)
{
	const struct powervr_vk_graphics *graphics;
	const struct powervr_vk_descriptor_set *set;
	struct powervr_buffer *buffer;
	uint64_t sets_bytes;
	uint64_t usc_bytes;
	uint64_t sets_place;
	uint64_t usc_place;
	uint32_t index;
	uint32_t slot;
	uint32_t words;
	int error;

	/* What the draws take: their shaders' code and their sets' copies, each on 16 bytes. */
	sets_bytes = 0;
	usc_bytes = 0;
	for (index = 0; index < job->draw_count; index++) {
		graphics = job->draws[index].pipeline;
		if (graphics == NULL)
			return EINVAL;
		usc_bytes += (graphics->vertex.length + 15U) & ~(uint64_t)15U;
		usc_bytes += (graphics->fragment.length + 15U) & ~(uint64_t)15U;
		for (slot = 0; slot < POWERVR_DRAW_SETS; slot++) {
			set = job->draws[index].sets[slot];
			if (set != NULL)
				sets_bytes += (set->layout.bytes + 15U) & ~15U;
		}
	}

	/* The general memory: a slot per draw, then the sets. */
	error = powervr_buffer_create(render, (uint64_t)job->draw_count * DRAW_SLOT + sets_bytes + 16U, &buffer);
	if (error != 0)
		return error;

	error = powervr_resource_make(session, buffer, 1, 0, &memory->draw_general);
	powervr_buffer_put(render, buffer);
	if (error != 0) {
		memory->draw_general = NULL;
		return error;
	}

	/* The program buffers, in their windows. */
	error = powervr_buffer_create(render, (uint64_t)job->draw_count * DRAW_SLOT, &memory->draw_pds);
	if (error != 0) {
		memory->draw_pds = NULL;
		return error;
	}

	error = powervr_session_program_map(session, POWERVR_PROGRAM_PDS, memory->draw_pds, &memory->draw_pds_place);
	if (error != 0) {
		memory->draw_pds_place = 0;
		return error;
	}

	error = powervr_buffer_create(render, usc_bytes + 16U, &memory->draw_usc);
	if (error != 0) {
		memory->draw_usc = NULL;
		return error;
	}

	error = powervr_session_program_map(session, POWERVR_PROGRAM_USC, memory->draw_usc, &memory->draw_usc_place);
	if (error != 0) {
		memory->draw_usc_place = 0;
		return error;
	}

	/* Each draw in order, its stream after the one before. */
	*stream_words = 0;
	*vertex_shareds = 0;
	sets_place = (uint64_t)job->draw_count * DRAW_SLOT;
	usc_place = 0;
	for (index = 0; index < job->draw_count; index++) {
		graphics = job->draws[index].pipeline;
		error = draw_place(job, index, memory, sets_place, usc_place, *stream_words, &words);
		if (error != 0)
			return error;

		*stream_words += words;
		if (graphics->vertex.shareds > *vertex_shareds)
			*vertex_shareds = graphics->vertex.shareds;
		usc_place += (graphics->vertex.length + 15U) & ~(uint64_t)15U;
		usc_place += (graphics->fragment.length + 15U) & ~(uint64_t)15U;
		for (slot = 0; slot < POWERVR_DRAW_SETS; slot++) {
			set = job->draws[index].sets[slot];
			if (set != NULL)
				sets_place += (set->layout.bytes + 15U) & ~15U;
		}
	}

	/* Succeeded: every draw is placed. */
	return 0;
}

/*
 * Places one draw: its shaders' code, its sets' and push constants'
 * copies, its vertex, shared register, task and coefficient programs,
 * its scissor, its primitive state block and its part of the stream.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_emit_dirty_pds_state, pvr_emit_dirty_ppp_state,
 * pvr_emit_vdm_index_list: the descriptor programs of both stages, the
 * fragment's task and coefficient programs in the primitive state, the
 * vertex program and the index list in the stream); zedBSD
 * src/drivers/gpu/powervr/render-check.c (the same placing for the
 * kernel's own triangle).
 */
static int
draw_place(
	const struct powervr_render_job *job,
	uint32_t index,
	struct job_memory *memory,
	uint64_t sets_place,
	uint64_t usc_place,
	uint32_t stream_at,
	uint32_t *words)
{
	const struct powervr_render_draw *draw;
	const struct powervr_vk_graphics *graphics;
	const struct powervr_vk_descriptor_set *set;
	const struct powervr_draw_attribute *attribute;
	const struct powervr_shader_input *input;
	const VkViewport *draw_viewport;
	const VkRect2D *draw_scissor;
	struct powervr_data_state state;
	struct powervr_draw_ppp ppp;
	struct powervr_draw_vdm vdm;
	uint32_t viewport[POWERVR_DRAW_VIEWPORT_WORDS];
	uint32_t scissor[POWERVR_DRAW_SCISSOR_WORDS];
	uint64_t general;
	uint64_t task;
	uint32_t pds_base;
	uint32_t usc_base;
	uint32_t pds_slot;
	uint32_t vertex_usc;
	uint32_t fragment_usc;
	uint32_t vertex_shared_words;
	uint32_t fragment_shared_words;
	uint32_t ppp_words;
	uint32_t dma_count;
	uint32_t slot;
	uint32_t at;
	int error;

	/* Where the draw's parts go. */
	draw = &job->draws[index];
	graphics = draw->pipeline;
	general = memory->draw_general->address + (uint64_t)index * DRAW_SLOT;
	pds_slot = index * DRAW_SLOT;
	pds_base = (uint32_t)(memory->draw_pds_place - POWERVR_PDS_HEAP_BASE) + pds_slot;
	usc_base = (uint32_t)(memory->draw_usc_place - POWERVR_USC_HEAP_BASE);
	vertex_usc = usc_base + (uint32_t)usc_place;
	fragment_usc = vertex_usc + (uint32_t)((graphics->vertex.length + 15U) & ~(size_t)15U);

	/* The two shaders' code. */
	put_bytes(memory->draw_usc, (uint32_t)usc_place, graphics->vertex_code, graphics->vertex.length);
	put_bytes(memory->draw_usc, (uint32_t)(usc_place + ((graphics->vertex.length + 15U) & ~(size_t)15U)), graphics->fragment_code, graphics->fragment.length);

	/* The bound sets' copies, the push constants and zero blend constants. */
	kern_memset(&state, 0, sizeof(state));
	at = (uint32_t)sets_place;
	for (slot = 0; slot < POWERVR_DRAW_SETS; slot++) {
		set = draw->sets[slot];
		if (set == NULL)
			continue;
		put_bytes(memory->draw_general->buffer, at, set->bytes, set->layout.bytes);
		state.layouts[slot] = &set->layout;
		state.set_addresses[slot] = memory->draw_general->address + at;
		at += (set->layout.bytes + 15U) & ~15U;
	}
	put_bytes(memory->draw_general->buffer, index * DRAW_SLOT + DRAW_PUSH, draw->push, draw->push_bytes);
	state.push_address = general + DRAW_PUSH;
	state.blend_address = general + DRAW_BLEND;

	/* The two shared register programs. */
	error = draw_shared(&graphics->vertex, &state, memory, pds_slot + DRAW_PDS_VSHARED_CODE, pds_slot + DRAW_PDS_VSHARED_DATA, &vertex_shared_words);
	if (error != 0)
		return error;

	error = draw_shared(&graphics->fragment, &state, memory, pds_slot + DRAW_PDS_FSHARED_CODE, pds_slot + DRAW_PDS_FSHARED_DATA, &fragment_shared_words);
	if (error != 0)
		return error;

	/* The vertex program: a DMA per attribute into its input registers. */
	dma_count = 0;
	for (slot = 0; slot < graphics->fixed.attribute_count; slot++) {
		attribute = &graphics->fixed.attributes[slot];
		input = NULL;
		for (at = 0; at < graphics->vertex.input_count; at++) {
			if (graphics->vertex.inputs[at].location == attribute->location)
				input = &graphics->vertex.inputs[at];
		}
		if (input == NULL)
			continue;
		if (attribute->binding >= POWERVR_DRAW_VERTEX_BUFFERS)
			return ENOTSUP;
		memory->dmas[dma_count].address = draw->vertex_addresses[attribute->binding] + attribute->offset;
		memory->dmas[dma_count].stride = 0;
		for (at = 0; at < graphics->fixed.binding_count; at++) {
			if (graphics->fixed.bindings[at].binding == attribute->binding)
				memory->dmas[dma_count].stride = graphics->fixed.bindings[at].stride;
		}
		memory->dmas[dma_count].words = input->components;
		memory->dmas[dma_count].reg = input->reg;
		dma_count++;
	}
	error = powervr_pds_task_word(vertex_usc, graphics->vertex.temps, 0, 0, &task);
	if (error != 0)
		return error;

	error = powervr_data_vertex_build(memory->dmas, dma_count, graphics->vertex.vertex_index_reg, graphics->vertex.instance_index_reg, task, &memory->vertex);
	if (error != 0)
		return error;

	put_words(memory->draw_pds, pds_slot + DRAW_PDS_VERTEX_DATA, memory->vertex.data, memory->vertex.data_words);
	put_words(memory->draw_pds, pds_slot + DRAW_PDS_VERTEX_CODE, memory->vertex.code, memory->vertex.code_words);

	/* The fragment's task program, and the coefficient program: data, then code after it. */
	error = powervr_render_kick_build(fragment_usc, graphics->fragment.temps, &memory->kick);
	if (error != 0)
		return error;

	put_words(memory->draw_pds, pds_slot + DRAW_PDS_KICK, memory->kick.data, POWERVR_RENDER_KICK_DATA_WORDS);
	put_words(memory->draw_pds, pds_slot + DRAW_PDS_KICK + POWERVR_RENDER_KICK_DATA_WORDS * 4U, memory->kick.code, POWERVR_RENDER_KICK_CODE_WORDS);
	put_words(memory->draw_pds, pds_slot + DRAW_PDS_COEFFICIENT, graphics->varyings.data, graphics->varyings.data_words);
	put_words(memory->draw_pds, pds_slot + DRAW_PDS_COEFFICIENT + (graphics->varyings.data_words + 3U) / 4U * 16U, graphics->varyings.code, graphics->varyings.code_words);

	/*
	 * The viewport and the scissor, the scissor in its entry of the
	 * table: the pipeline's own unless it made them dynamic, then the
	 * ones the command buffer set.
	 */
	draw_viewport = &draw->viewport;
	if (!graphics->fixed.dynamic_viewport)
		draw_viewport = &graphics->fixed.viewport;
	draw_scissor = &draw->scissor;
	if (!graphics->fixed.dynamic_scissor)
		draw_scissor = &graphics->fixed.scissor;
	error = powervr_draw_viewport_words(draw_viewport, viewport);
	if (error != 0)
		return error;

	kern_memset(&ppp, 0, sizeof(ppp));
	error = powervr_draw_scissor_words(draw_viewport, draw_scissor, scissor, ppp.region_clip);
	if (error != 0)
		return error;

	put_words(memory->general->buffer, GENERAL_SCISSORS + index * POWERVR_DRAW_SCISSOR_WORDS * 4U, scissor, POWERVR_DRAW_SCISSOR_WORDS);

	/* The primitive state block. */
	ppp.fixed = &graphics->fixed;
	ppp.varyings = &graphics->varyings;
	ppp.has_depth = job->depth.present;
	ppp.fragment_kick_offset = pds_base + DRAW_PDS_KICK;
	ppp.shared_code_offset = pds_base + DRAW_PDS_FSHARED_CODE;
	ppp.shared_data_offset = pds_base + DRAW_PDS_FSHARED_DATA;
	ppp.shared_data_words = fragment_shared_words;
	ppp.fragment_shareds = graphics->fragment.shareds;
	ppp.coefficient_offset = pds_base + DRAW_PDS_COEFFICIENT;
	kern_memcpy(ppp.viewport, viewport, sizeof(viewport));
	ppp.scissor_index = index;
	error = powervr_draw_ppp_build(&ppp, memory->ppp, &ppp_words);
	if (error != 0)
		return error;

	put_words(memory->draw_general->buffer, index * DRAW_SLOT + DRAW_PPP, memory->ppp, ppp_words);

	/* The stream: the block, the vertex program and the index list. */
	kern_memset(&vdm, 0, sizeof(vdm));
	vdm.ppp_address = general + DRAW_PPP;
	vdm.ppp_words = ppp_words;
	vdm.vertex_data_offset = pds_base + DRAW_PDS_VERTEX_DATA;
	vdm.vertex_code_offset = pds_base + DRAW_PDS_VERTEX_CODE;
	vdm.vertex_data_words = memory->vertex.data_words;
	vdm.vertex_temps = memory->vertex.temps;
	vdm.shared_code_offset = pds_base + DRAW_PDS_VSHARED_CODE;
	vdm.shared_data_offset = pds_base + DRAW_PDS_VSHARED_DATA;
	vdm.shared_data_words = vertex_shared_words;
	vdm.vertex_shareds = graphics->vertex.shareds;
	vdm.vertex_inputs = graphics->vertex.vertex_inputs;
	vdm.vertex_outputs = graphics->vertex.vertex_outputs;
	vdm.topology = graphics->fixed.topology;
	vdm.indexed = draw->indexed;
	vdm.index_address = draw->index_address;
	vdm.index_bytes = draw->index_bytes;
	vdm.count = draw->count;
	vdm.instances = draw->instances;
	vdm.vertex_offset = draw->vertex_offset;
	error = powervr_draw_vdm_build(&vdm, memory->vdm, words);
	if (error != 0)
		return error;

	put_words(memory->general->buffer, GENERAL_STREAM + stream_at * 4U, memory->vdm, *words);

	/* Succeeded: the draw is placed. */
	return 0;
}

/* Builds and places a shader's shared register program from the bound state; one halt when it takes no shared registers. */
static int
draw_shared(
	const struct powervr_shader_program *program,
	const struct powervr_data_state *state,
	struct job_memory *memory,
	uint32_t code_at,
	uint32_t data_at,
	uint32_t *data_words)
{
	uint32_t halt;
	uint32_t count;
	int error;

	/* No request: a program that only ends. */
	*data_words = 0;
	if (program->request_count == 0) {
		error = powervr_pds_halt(0, &halt);
		if (error != 0)
			return error;

		put_words(memory->draw_pds, code_at, &halt, 1);
		return 0;
	}

	/* The loads the requests ask for. */
	error = powervr_data_resolve(program, state, memory->loads, POWERVR_DATA_LOADS_MAX, &count);
	if (error != 0)
		return error;

	/* The program, its code and data at their places. */
	error = powervr_data_shared_build(memory->loads, count, &memory->shared);
	if (error != 0)
		return error;

	put_words(memory->draw_pds, code_at, memory->shared.code, memory->shared.code_words);
	put_words(memory->draw_pds, data_at, memory->shared.data, memory->shared.data_words);
	*data_words = memory->shared.data_words;

	/* Succeeded: the program is placed. */
	return 0;
}

/* Unmaps and returns the job's memory and its bookkeeping. */
static void
job_release(
	struct powervr_render *render,
	struct powervr_session *session,
	struct job_memory *memory)
{
	/* The program buffers. */
	if (memory->usc != NULL) {
		if (memory->usc_place != 0U)
			powervr_session_program_unmap(session, POWERVR_PROGRAM_USC, memory->usc, memory->usc_place);
		powervr_buffer_put(render, memory->usc);
	}

	if (memory->pds != NULL) {
		if (memory->pds_place != 0U)
			powervr_session_program_unmap(session, POWERVR_PROGRAM_PDS, memory->pds, memory->pds_place);
		powervr_buffer_put(render, memory->pds);
	}

	/* The draws' program buffers and memory. */
	if (memory->draw_usc != NULL) {
		if (memory->draw_usc_place != 0U)
			powervr_session_program_unmap(session, POWERVR_PROGRAM_USC, memory->draw_usc, memory->draw_usc_place);
		powervr_buffer_put(render, memory->draw_usc);
	}

	if (memory->draw_pds != NULL) {
		if (memory->draw_pds_place != 0U)
			powervr_session_program_unmap(session, POWERVR_PROGRAM_PDS, memory->draw_pds, memory->draw_pds_place);
		powervr_buffer_put(render, memory->draw_pds);
	}

	if (memory->draw_general != NULL)
		powervr_resource_unmake(session, memory->draw_general);

	/* The general memory and the bookkeeping. */
	if (memory->general != NULL)
		powervr_resource_unmake(session, memory->general);
	kern_free(memory);
}

/* Writes words through a buffer's uncached view, low byte first. */
static void
put_words(
	struct powervr_buffer *buffer,
	uint32_t offset,
	const uint32_t *words,
	unsigned count)
{
	unsigned index;

	/* One word at a time. */
	for (index = 0; index < count; index++) {
		buffer->view[offset + index * 4U + 0U] = (uint8_t)words[index];
		buffer->view[offset + index * 4U + 1U] = (uint8_t)(words[index] >> 8);
		buffer->view[offset + index * 4U + 2U] = (uint8_t)(words[index] >> 16);
		buffer->view[offset + index * 4U + 3U] = (uint8_t)(words[index] >> 24);
	}
}

/* Writes bytes through a buffer's uncached view. */
static void
put_bytes(
	struct powervr_buffer *buffer,
	uint32_t offset,
	const uint8_t *bytes,
	size_t count)
{
	size_t index;

	/* One byte at a time. */
	for (index = 0; index < count; index++)
		buffer->view[offset + index] = bytes[index];
}
