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
#define GENERAL_LOAD_WORDS		0x100U
#define GENERAL_BYTES			0x1000U
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
 * The load and store operations the job reads.
 *
 * Values referenced:
 * Khronos Vulkan 1.x vulkan_core.h (VK_ATTACHMENT_LOAD_OP_LOAD 0,
 * VK_ATTACHMENT_LOAD_OP_CLEAR 1, VK_ATTACHMENT_LOAD_OP_DONT_CARE 2,
 * VK_ATTACHMENT_STORE_OP_DONT_CARE 1).
 */
#define LOAD_OP_LOAD			0U
#define LOAD_OP_CLEAR			1U
#define LOAD_OP_DONT_CARE		2U
#define STORE_OP_DONT_CARE		1U

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
};

static int job_check(const struct powervr_render_job *job);
static int job_map(struct powervr_render *render, struct powervr_session *session, struct job_memory *memory);
static int job_programs(const struct powervr_render_job *job, struct job_memory *memory, uint32_t *shared_registers);
static int job_background(const struct powervr_render_target *color, struct job_memory *memory, uint32_t pds_base, uint32_t usc_base, uint32_t *shared_registers);
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
	uint32_t shared_registers;
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

	/* The commands: an empty stream, the fragment of the target's tiles. */
	terminate = POWERVR_VDM_TERMINATE;
	put_words(memory->general->buffer, GENERAL_STREAM, &terminate, 1);
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

	/* Draws come with the graphics pipeline; one colour attachment, no depth load or store. */
	if (job->draw_count != 0 || job->color_count != 1)
		return ENOTSUP;
	if (job->depth.present && (job->depth.load_op != LOAD_OP_DONT_CARE || job->depth.store_op != STORE_OP_DONT_CARE))
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
