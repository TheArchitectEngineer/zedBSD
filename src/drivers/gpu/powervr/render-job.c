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
#include "drivers/gpu/powervr/transfer-program.h"
#include "drivers/gpu/powervr/render-program.h"
#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-draw.h"
#include "drivers/gpu/powervr/render-depth.h"
#include "drivers/gpu/powervr/vk-draw.h"
#include "drivers/gpu/powervr/vk.h"
#include "drivers/gpu/powervr/descriptor-set.h"
#include "drivers/gpu/powervr/shader.h"
#include "drivers/gpu/powervr/load-draw-shader.h"

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
#define GENERAL_LOAD_WORDS		0x1800U
#define GENERAL_DBIAS			0x1900U
#define GENERAL_SCISSORS		0x1a00U
#define GENERAL_TERMINATE_PPP		0x1d00U
#define GENERAL_BYTES			0x2000U

/* The most pixels of a job the timing counts as small (256 x 256). */
#define JOB_SMALL_PIXELS		65536U

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
#define DRAW_DEFAULTS			0x300U
#define ATTRIBUTE_NO_DEFAULTS		0xffffffffU
#define DRAW_PDS_VERTEX_DATA		0x000U
#define DRAW_PDS_VERTEX_CODE		0x400U
#define DRAW_PDS_VSHARED_CODE		0x500U
#define DRAW_PDS_VSHARED_DATA		0x600U
#define DRAW_PDS_KICK			0x800U
#define DRAW_PDS_FSHARED_CODE		0x880U
#define DRAW_PDS_FSHARED_DATA		0x980U
#define DRAW_PDS_COEFFICIENT		0xb80U
#define DRAW_STREAM_ROOM		(GENERAL_LOAD_WORDS - GENERAL_STREAM)

_Static_assert(DRAW_STREAM_ROOM >= ((POWERVR_RENDER_DRAWS + 1U) * POWERVR_DRAW_VDM_WORDS + POWERVR_RENDER_TERMINATE_STREAM_WORDS) * 4U, "the stream's room, a load draw included");
_Static_assert(POWERVR_DRAW_PPP_WORDS * 4U <= DRAW_PUSH, "the primitive state block's room");
_Static_assert(POWERVR_DRAW_PUSH_BYTES <= DRAW_BLEND - DRAW_PUSH, "the push constants' room");
_Static_assert(POWERVR_DATA_VERTEX_DATA_MAX * 4U <= DRAW_PDS_VERTEX_CODE, "the vertex program's data room");
_Static_assert(POWERVR_DATA_VERTEX_CODE_MAX * 4U <= DRAW_PDS_VSHARED_CODE - DRAW_PDS_VERTEX_CODE, "the vertex program's code room");
_Static_assert(POWERVR_DATA_SHARED_CODE_MAX * 4U <= DRAW_PDS_VSHARED_DATA - DRAW_PDS_VSHARED_CODE, "shared code room");
_Static_assert(POWERVR_DATA_SHARED_DATA_MAX * 4U <= DRAW_PDS_KICK - DRAW_PDS_VSHARED_DATA, "shared data room");
_Static_assert(POWERVR_DATA_SHARED_CODE_MAX * 4U <= DRAW_PDS_FSHARED_DATA - DRAW_PDS_FSHARED_CODE, "fragment shared code room");
_Static_assert(POWERVR_DATA_SHARED_DATA_MAX * 4U <= DRAW_PDS_COEFFICIENT - DRAW_PDS_FSHARED_DATA, "fragment shared data room");
_Static_assert((POWERVR_DRAW_COEFFICIENT_DATA + 3U) / 4U * 16U + POWERVR_DRAW_COEFFICIENT_CODE * 4U <= DRAW_SLOT - DRAW_PDS_COEFFICIENT, "coefficient room");
_Static_assert((POWERVR_RENDER_DRAWS + 1U) * POWERVR_DRAW_SCISSOR_WORDS * 4U <= GENERAL_TERMINATE_PPP - GENERAL_SCISSORS, "scissor table room, a load draw included");
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
#define LOAD_OP_DONT_CARE		2U

/*
 * A colour attachment that is LOADed is drawn over by a full-screen
 * draw that samples what the job before stored (LOAD_DRAW 1, the
 * default), its background left as contents that do not matter; on the
 * board a background object that samples from its shared registers read
 * 0 whatever its program (2026-10-21), while a draw's sampling reads the
 * image.  POWERVR_RENDER_LOAD_PROBE other than 0 keeps the background
 * forms the board was probed with.
 *
 * Values referenced:
 * Khronos Vulkan 1.x vulkan_core.h (VK_ATTACHMENT_LOAD_OP_DONT_CARE 2,
 * VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 3, VK_FORMAT_R32G32_SFLOAT 103).
 */
#define LOAD_DRAW_TOPOLOGY		3U
#define LOAD_DRAW_POSITION_FORMAT	103U
#define LOAD_DRAW_VERTICES		6U
#define LOAD_DRAW_STRIDE		16U
#define LOAD_DRAW_FORMATS		4U
#define LOAD_DRAW_CODE_BYTES		0x2000U

/* The load draw's compiled shaders, one pipeline per attachment format, kept by the render node. */
struct powervr_load_draws {
	uint32_t formats[LOAD_DRAW_FORMATS];
	struct powervr_vk_graphics *graphics[LOAD_DRAW_FORMATS];
};

/* One job's load draw: the draw, its set and the set's bytes. */
struct load_draw {
	struct powervr_render_draw draw;
	struct powervr_vk_descriptor_set set;
};

/*
 * A render job whose 3D may still run: its memory, the copy of the job
 * the load draw took and the load draw, when it was kicked (for the
 * timing) and whether it was small; one of the session's list, oldest
 * first (powervr_render_jobs_reap).
 */
struct powervr_job_done {
	struct powervr_job_done *next;
	struct job_memory *memory;
	struct powervr_render_job *work;
	struct load_draw *load;
	uint64_t kicked;
	int small;
};

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
static void load_draw_free(struct powervr_render_job *work, struct load_draw *load);
static uint32_t float_bits(uint32_t value);
static int load_graphics(struct powervr_render *render, uint32_t vk_format, struct powervr_vk_graphics **result);
static void debug_filter(struct powervr_render_job *job);
static int load_graphics_compile(struct powervr_vk_graphics *graphics);
static int load_graphics_shaders(struct powervr_vk_graphics *graphics, struct powervr_shader_workspace *workspace);
static int load_draw_build(struct powervr_render *render, const struct powervr_render_job *job, struct job_memory *memory, struct load_draw *load);
static int job_map(struct powervr_render *render, struct powervr_session *session, struct job_memory *memory);
static int job_programs(const struct powervr_render_job *job, struct job_memory *memory, uint32_t *shared_registers);
/*
 * The LOAD background's probe (for the board): 0 the usual load program,
 * 1 the sample at (0, 0) (POWERVR_LOAD_PROBE_ORIGIN), 2 the program with a
 * wait group after each group (POWERVR_LOAD_PROBE_SPACED), 3 no sample at
 * all: the background a constant magenta as a clear's, 4 the usual
 * program with its eight shared words written by the data sequencer as
 * constants (DOUTW) instead of copied from memory (DOUTD), 5 the
 * transfer's blit program and shared words (one to one over the
 * attachment) as the background.
 */
#ifndef POWERVR_RENDER_LOAD_PROBE
#define POWERVR_RENDER_LOAD_PROBE	0
#endif
#define LOAD_PROBE_CONSTANT		3
#define LOAD_PROBE_WRITTEN		4
#define LOAD_PROBE_BLIT			5

/* The load program's form: the probe's own up to SPACED, the usual one for the later probes. */
#if POWERVR_RENDER_LOAD_PROBE <= POWERVR_LOAD_PROBE_SPACED
#define LOAD_USC_PROBE_FORM		POWERVR_RENDER_LOAD_PROBE
#else
#define LOAD_USC_PROBE_FORM		POWERVR_LOAD_PROBE_USUAL
#endif

/* No draw limit. */
#define DEBUG_NO_LIMIT			0xffffffffU

/*
 * The test-only draw filter, for bisecting a job that locks the core up:
 * a render job keeps its own draws below debug_draw_limit whose bits in
 * debug_draw_skip are clear (the load draw is never dropped).  Set by
 * powervr_render_debug_set from a root write of hw.gpu.powervr.stages;
 * off (no limit, no skip) from boot.  Read without a lock: a job takes
 * either the old or the new setting.
 */
static volatile uint32_t debug_draw_limit = DEBUG_NO_LIMIT;
static volatile uint64_t debug_draw_skip;

/* Reads a little-endian 32-bit word. */
static uint32_t
get_le32(
	const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int job_background(const struct powervr_render_target *color, struct job_memory *memory, uint32_t pds_base, uint32_t usc_base, uint32_t *shared_registers);
static int job_draws(struct powervr_render *render, struct powervr_session *session, const struct powervr_render_job *job, const struct powervr_render_draw *prefix, struct job_memory *memory, uint32_t *stream_words, uint32_t *vertex_shareds);
static int draw_place(const struct powervr_render_job *job, const struct powervr_render_draw *draw, uint32_t index, struct job_memory *memory, uint64_t sets_place, uint64_t usc_place, uint32_t stream_at, uint32_t *words);
static const struct powervr_render_draw *job_draw(const struct powervr_render_job *job, const struct powervr_render_draw *prefix, uint32_t index);
static int draw_shared(const struct powervr_shader_program *program, const struct powervr_data_state *state, struct job_memory *memory, uint32_t code_at, uint32_t data_at, uint32_t *data_words);
static void job_release(struct powervr_render *render, struct powervr_session *session, struct job_memory *memory);
static int attribute_words(uint32_t format, uint32_t components, uint32_t *words, uint32_t *defaults);
static void put_words(struct powervr_buffer *buffer, uint32_t offset, const uint32_t *words, unsigned count);
static void put_bytes(struct powervr_buffer *buffer, uint32_t offset, const uint8_t *bytes, size_t count);

/* The components a vertex format lacks: (0, 0, 0, 1.0) for a float format, then (0, 0, 0, 1) for an integer one, little-endian. */
static const uint8_t attribute_defaults[32] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x80, 0x3f,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x00, 0x00,
};

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
	uint32_t terminate[POWERVR_RENDER_TERMINATE_STREAM_WORDS];
	uint32_t terminate_state[POWERVR_RENDER_TERMINATE_STATE_WORDS];
	const struct powervr_render_job *given;
	struct powervr_render_job *work;
	const struct powervr_render_draw *prefix;
	struct load_draw *load;
	struct powervr_job_done *done;
	unsigned long enabled;
	uint64_t started;
	uint32_t at;
	int error;

	/* When the job started, for the submission's timing. */
	started = powervr_timing_now();

	/* Refuses what the job runner does not build yet. */
	error = job_check(job);
	if (error != 0)
		return error;

	/*
	 * A loaded colour attachment: the job runs with its background's
	 * contents not mattering and a load draw first (LOAD_DRAW), on a
	 * copy of the job.
	 */
	given = job;
	work = NULL;
	load = NULL;
	if (POWERVR_RENDER_LOAD_PROBE == 0 && job->colors[0].load_op == LOAD_OP_LOAD) {
		work = kern_malloc(sizeof(*work));
		load = kern_calloc(1, sizeof(*load));
		if (work == NULL || load == NULL) {
			kern_free(work);
			kern_free(load);
			return ENOMEM;
		}

		*work = *job;
		work->colors[0].load_op = LOAD_OP_DONT_CARE;
		job = work;
	}

	/* The test-only draw filter (hw.gpu.powervr.stages written "draws=N skip=M"), on a copy of the job. */
	if (debug_draw_limit != DEBUG_NO_LIMIT || debug_draw_skip != 0U) {
		if (work == NULL) {
			work = kern_malloc(sizeof(*work));
			if (work == NULL)
				return ENOMEM;
			*work = *job;
			job = work;
		}
		debug_filter(work);
	}

	/* The job's shape and its first colour target's load and store, with the diagnostics. */
	if (POWERVR_DIAGNOSTICS && job->color_count != 0) {
		kern_logf("powervr: render job: %ux%u, %u draws, colour load %u store %u at 0x%llx (%s, pitch %u, padded %u)\n",
			  (unsigned)job->width, (unsigned)job->height, (unsigned)job->draw_count,
			  (unsigned)job->colors[0].load_op, (unsigned)job->colors[0].store_op,
			  (unsigned long long)job->colors[0].surface.address, job->colors[0].surface.linear ? "linear" : "twiddled",
			  (unsigned)job->colors[0].surface.row_pitch, (unsigned)job->colors[0].surface.padded_width);
	}

	/* The session's render context and a target of the frame. */
	error = powervr_session_render_context(render, session, &context);
	if (error == 0) {
		shape.width = job->width;
		shape.height = job->height;
		shape.samples = 1;
		shape.layers = 1;
		error = powervr_session_target(render, session, &shape, &target);
	}

	/* The job's memory and its programs. */
	memory = NULL;
	if (error == 0) {
		memory = kern_calloc(1, sizeof(*memory));
		if (memory == NULL)
			error = ENOMEM;
	}

	if (error != 0) {
		load_draw_free(work, load);
		return error;
	}

	/* The job's memory, timed on its own. */
	powervr_timing_add(POWERVR_TIMING_JOB_SETUP, started);
	started = powervr_timing_now();
	error = job_map(render, session, memory);
	powervr_timing_add(POWERVR_TIMING_JOB_MAP, started);

	/* The background's programs and the load draw. */
	started = powervr_timing_now();
	if (error == 0)
		error = job_programs(job, memory, &shared_registers);
	if (error == 0 && load != NULL)
		error = load_draw_build(render, given, memory, load);
	if (error != 0) {
		job_release(render, session, memory);
		load_draw_free(work, load);
		return error;
	}

	/* The draws' programs and streams (the load draw first), then the stream's end. */
	stream_words = 0;
	vertex_shareds = 0;
	if (job->draw_count != 0 || load != NULL) {
		/* The load draw, when there is one, goes before the job's own. */
		prefix = NULL;
		if (load != NULL)
			prefix = &load->draw;
		error = job_draws(render, session, job, prefix, memory, &stream_words, &vertex_shareds);
		if (error != 0) {
			job_release(render, session, memory);
			load_draw_free(work, load);
			return error;
		}

		/* q1523 diagnostics: the session keeps a note of its newest job for a reset's report. */
		powervr_session_note_job(session, job, prefix);
	}

	/* The shared registers the job takes: the background's or the draws', whichever is more. */
	if (vertex_shareds > shared_registers)
		shared_registers = vertex_shareds;

	/* The stream's end as Mesa ends a render: the terminate state's block, then the end. */
	powervr_render_terminate_state(job->width, job->height, terminate_state);
	put_words(memory->general->buffer, GENERAL_TERMINATE_PPP, terminate_state, POWERVR_RENDER_TERMINATE_STATE_WORDS);
	error = powervr_render_terminate_stream(memory->general->address + GENERAL_TERMINATE_PPP, terminate);
	if (error != 0) {
		job_release(render, session, memory);
		load_draw_free(work, load);
		return error;
	}

	put_words(memory->general->buffer, GENERAL_STREAM + stream_words * 4U, terminate, POWERVR_RENDER_TERMINATE_STREAM_WORDS);

	/* The commands: the stream, the fragment of the target's tiles with the draws' tables. */
	memory->fragment_setup.scissor_table = memory->general->address + GENERAL_SCISSORS;
	memory->fragment_setup.dbias_table = memory->general->address + GENERAL_DBIAS;

	/* A depth target's loads, stores and background depth. */
	if (job->depth.present) {
		error = powervr_render_zls_words(&job->depth, &zls);
		if (error != 0) {
			job_release(render, session, memory);
			load_draw_free(work, load);
			return error;
		}
		memory->fragment_setup.has_depth = 1;
		memory->fragment_setup.zls_control = zls.control;
		memory->fragment_setup.zls_base = zls.base;
		memory->fragment_setup.background_depth = zls.background_depth;
		memory->fragment_setup.zls_stride = zls.stride;
	}

	/* The target's tiles and the border colour table, then the fragment command. */
	layout = powervr_target_layout_of(target);
	memory->fragment_setup.tiles_x = layout->tiles_x;
	memory->fragment_setup.tiles_y = layout->tiles_y;
	memory->fragment_setup.border_table = powervr_session_border_table(session);
	memory->geometry_setup.border_table = memory->fragment_setup.border_table;
	powervr_fragment_command(&memory->fragment_setup, memory->fragment);

	/* The diagnostics: the fragment command's words, eight a line, to set a loading job beside a clearing one. */
	if (POWERVR_DIAGNOSTICS) {
		for (at = 0; at < POWERVR_FRAGMENT_BYTES; at += 32U) {
			kern_logf("powervr: render job: fragment +%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", (unsigned)at,
				  (unsigned)get_le32(memory->fragment + at), (unsigned)get_le32(memory->fragment + at + 4U),
				  (unsigned)get_le32(memory->fragment + at + 8U), (unsigned)get_le32(memory->fragment + at + 12U),
				  (unsigned)get_le32(memory->fragment + at + 16U), (unsigned)get_le32(memory->fragment + at + 20U),
				  (unsigned)get_le32(memory->fragment + at + 24U), (unsigned)get_le32(memory->fragment + at + 28U));
		}
	}
	memory->geometry_setup.stream = memory->general->address + GENERAL_STREAM;
	memory->geometry_setup.region_header_bytes = layout->region_header_bytes;
	memory->geometry_setup.shared_registers = shared_registers;
	powervr_geometry_command(&memory->geometry_setup, memory->geometry);

	/* The record that keeps the job's memory until its 3D finished. */
	powervr_timing_add(POWERVR_TIMING_JOB_BUILD, started);
	done = kern_calloc(1, sizeof(*done));
	if (done == NULL) {
		job_release(render, session, memory);
		load_draw_free(work, load);
		return ENOMEM;
	}

	/* The kicks; the partial render runs the fragment's own command. */
	started = powervr_timing_now();
	mutex_lock(&render->core->lock);

	session->noted_target = target;
	error = powervr_session_kick_render(render, session, target, 0, memory->geometry, memory->fragment, memory->fragment, NULL, 0, &memory->marks);

	mutex_unlock(&render->core->lock);

	if (error != 0) {
		kern_free(done);
		job_release(render, session, memory);
		load_draw_free(work, load);
		return error;
	}

	/*
	 * The job runs on: its memory waits in the session's list until its
	 * 3D point is reached (powervr_render_jobs_reap), and the session's
	 * later callbacks and fences complete at that point.
	 */
	powervr_timing_add(POWERVR_TIMING_JOB_KICK, started);
	done->memory = memory;
	done->work = work;
	done->load = load;
	done->kicked = powervr_timing_now();
	done->small = job->width * job->height <= JOB_SMALL_PIXELS;
	enabled = spin_lock_irqsave(&render->queue_lock);

	if (session->jobs_done_tail != NULL) {
		session->jobs_done_tail->next = done;
	} else {
		session->jobs_done = done;
	}
	session->jobs_done_tail = done;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	powervr_session_point_set(session, POWERVR_POINT_FRAGMENT, memory->marks.fragment_value);

	/* Succeeded: the render was kicked. */
	return 0;
}

/*
 * Releases the memory of a session's render jobs whose 3D points were
 * reached, oldest first.  With wait 1 each job is waited for, bounded by
 * POWERVR_PROGRESS_MS; with wait 2 every job is released (the session's
 * work is over).  Nothing is released of a lost or isolated session, or
 * on a faulted device, before then: the core may still reach it.
 */
void
powervr_render_jobs_reap(
	struct powervr_render *render,
	struct powervr_session *session,
	int wait)
{
	struct powervr_job_done *done;
	unsigned long enabled;
	uint64_t started;
	int reached;
	int halted;
	int error;

	/* Oldest first, while there is one to release. */
	for (;;) {
		/* The oldest job, and whether its point was reached. */
		reached = 0;
		halted = 0;
		enabled = spin_lock_irqsave(&render->queue_lock);

		done = session->jobs_done;
		if (done != NULL) {
			reached = powervr_session_point_reached(session, POWERVR_POINT_FRAGMENT, done->memory->marks.fragment_value);
			if (render->faulted || session->lost || session->isolated)
				halted = 1;
		}

		spin_unlock_irqrestore(&render->queue_lock, enabled);

		/* Nothing more. */
		if (done == NULL)
			return;

		/* The core may still reach it, until the session's work is over. */
		if (halted && wait != 2)
			return;

		/* Not reached: left for later, or waited for. */
		if (!reached && wait == 0)
			return;
		if (!reached && wait == 1) {
			error = powervr_session_point_wait(session, POWERVR_POINT_FRAGMENT, done->memory->marks.fragment_value, POWERVR_PROGRESS_MS);
			if (error != 0)
				return;
		}

		/* Takes it off the list. */
		enabled = spin_lock_irqsave(&render->queue_lock);

		session->jobs_done = done->next;
		if (session->jobs_done == NULL)
			session->jobs_done_tail = NULL;

		spin_unlock_irqrestore(&render->queue_lock, enabled);

		/* Its stages and timing (seen at this point, after the 3D finished). */
		if (!halted) {
			powervr_render_retired(render, session, &done->memory->marks);
			powervr_timing_add(POWERVR_TIMING_JOB_WAIT, done->kicked);
			if (done->small)
				powervr_timing_add(POWERVR_TIMING_WAIT_SMALL, done->kicked);
		}

		/* Its memory. */
		started = powervr_timing_now();
		job_release(render, session, done->memory);
		load_draw_free(done->work, done->load);
		powervr_timing_add(POWERVR_TIMING_JOB_RELEASE, started);
		kern_free(done);
	}
}

/*
 * Finds or compiles the load draw's pipeline for an attachment format:
 * the vertex shader passing a position and a texture coordinate (one
 * binding of two R32G32 attributes), the fragment shader sampling set 0,
 * binding 0 there into the one output of the format, a triangle list
 * without culling, depth or blending, the viewport and scissor given by
 * the draw.  Kept by the render node for every later job.
 */
static int
load_graphics(
	struct powervr_render *render,
	uint32_t vk_format,
	struct powervr_vk_graphics **result)
{
	struct powervr_vk_graphics *graphics;
	struct powervr_draw_fixed *fixed;
	uint32_t slot;
	int error;

	/* The node's table, made once. */
	if (render->load_draws == NULL) {
		render->load_draws = kern_calloc(1, sizeof(*render->load_draws));
		if (render->load_draws == NULL)
			return ENOMEM;
	}

	/* A pipeline already compiled for the format. */
	for (slot = 0; slot < LOAD_DRAW_FORMATS; slot++) {
		if (render->load_draws->graphics[slot] != NULL && render->load_draws->formats[slot] == vk_format) {
			*result = render->load_draws->graphics[slot];
			return 0;
		}
	}

	/* A free slot for a new one. */
	for (slot = 0; slot < LOAD_DRAW_FORMATS; slot++) {
		if (render->load_draws->graphics[slot] == NULL)
			break;
	}

	if (slot == LOAD_DRAW_FORMATS)
		return ENOSPC;

	/* The pipeline's record. */
	graphics = kern_calloc(1, sizeof(*graphics));
	if (graphics == NULL)
		return ENOMEM;

	/* The fixed state and the keys. */
	fixed = &graphics->fixed;
	fixed->topology = LOAD_DRAW_TOPOLOGY;
	fixed->dynamic_viewport = true;
	fixed->dynamic_scissor = true;
	fixed->binding_count = 1;
	fixed->bindings[0].stride = LOAD_DRAW_STRIDE;
	fixed->attribute_count = 2;
	fixed->attributes[0].location = 0;
	fixed->attributes[0].format = LOAD_DRAW_POSITION_FORMAT;
	fixed->attributes[1].location = 1;
	fixed->attributes[1].format = LOAD_DRAW_POSITION_FORMAT;
	fixed->attributes[1].offset = 8;
	fixed->vertex_key.attribute_formats[0] = LOAD_DRAW_POSITION_FORMAT;
	fixed->vertex_key.attribute_formats[1] = LOAD_DRAW_POSITION_FORMAT;
	fixed->vertex_key.samples = 1;
	fixed->fragment_key.output_formats[0] = vk_format;
	fixed->fragment_key.blend[0].write_mask = 0xf;
	fixed->fragment_key.samples = 1;

	/* The two shaders and the varyings between them; what was allocated goes on a failure. */
	error = load_graphics_compile(graphics);
	if (error != 0) {
		kern_free(graphics->vertex_code);
		kern_free(graphics->fragment_code);
		kern_free(graphics);
		return error;
	}

	/* Succeeded: the pipeline, kept. */
	render->load_draws->formats[slot] = vk_format;
	render->load_draws->graphics[slot] = graphics;
	*result = graphics;
	return 0;
}

/*
 * Compiles the load draw's two shaders into code rooms of their own and
 * links their varyings; the caller frees the code rooms on a failure.
 */
static int
load_graphics_compile(
	struct powervr_vk_graphics *graphics)
{
	struct powervr_shader_workspace *workspace;
	int error;

	/* The vertex shader's code room. */
	graphics->vertex_code = kern_malloc(LOAD_DRAW_CODE_BYTES);
	if (graphics->vertex_code == NULL)
		return ENOMEM;

	/* The fragment shader's code room. */
	graphics->fragment_code = kern_malloc(LOAD_DRAW_CODE_BYTES);
	if (graphics->fragment_code == NULL)
		return ENOMEM;

	/* The compiler's working memory. */
	workspace = kern_malloc(powervr_shader_workspace_size());
	if (workspace == NULL)
		return ENOMEM;

	/* The shaders and their varyings. */
	error = load_graphics_shaders(graphics, workspace);

	/* The working memory goes, whatever the outcome. */
	kern_free(workspace);

	/* Reports a shader that did not compile. */
	if (error != 0)
		return error;

	/* Succeeded: both shaders are compiled and linked. */
	return 0;
}

/* Compiles the load draw's vertex and fragment shaders and builds their varyings. */
static int
load_graphics_shaders(
	struct powervr_vk_graphics *graphics,
	struct powervr_shader_workspace *workspace)
{
	struct powervr_draw_fixed *fixed;
	int error;

	/* The vertex shader. */
	fixed = &graphics->fixed;
	error = powervr_shader_compile(powervr_load_draw_vert, sizeof(powervr_load_draw_vert) / 4U, POWERVR_SHADER_VERTEX, &fixed->vertex_key, workspace, &graphics->vertex, graphics->vertex_code, LOAD_DRAW_CODE_BYTES);
	if (error != 0)
		return error;

	/* The fragment shader. */
	error = powervr_shader_compile(powervr_load_draw_frag, sizeof(powervr_load_draw_frag) / 4U, POWERVR_SHADER_FRAGMENT, &fixed->fragment_key, workspace, &graphics->fragment, graphics->fragment_code, LOAD_DRAW_CODE_BYTES);
	if (error != 0)
		return error;

	/* The varyings between them. */
	error = powervr_draw_varyings_build(&graphics->vertex, &graphics->fragment, &graphics->varyings);
	if (error != 0)
		return error;

	/* Succeeded: the shaders and their varyings. */
	return 0;
}

/*
 * Builds a job's load draw: two triangles over the framebuffer with
 * texture coordinates 0 to 1, in the job's general memory; a set of one
 * combined image sampler, the attachment's view and a nearest sampler
 * clamped to the edge; the framebuffer's viewport and the render area's
 * scissor.  Sampled at each pixel's centre, a coordinate names that
 * pixel's own texel.
 *
 * Procedure referenced:
 * zedBSD src/drivers/gpu/powervr/vk-layout.c (set_image: the image's
 * descriptor and the sampler's in a combined binding).
 */
static int
load_draw_build(
	struct powervr_render *render,
	const struct powervr_render_job *job,
	struct job_memory *memory,
	struct load_draw *load)
{
	/* Two triangles: x, y in device coordinates and u, v, as float bits (-1.0, 0, 1.0). */
	static const uint32_t vertices[LOAD_DRAW_VERTICES * 4U] = {
		0xbf800000U, 0xbf800000U, 0x00000000U, 0x00000000U,
		0x3f800000U, 0xbf800000U, 0x3f800000U, 0x00000000U,
		0xbf800000U, 0x3f800000U, 0x00000000U, 0x3f800000U,
		0x3f800000U, 0xbf800000U, 0x3f800000U, 0x00000000U,
		0x3f800000U, 0x3f800000U, 0x3f800000U, 0x3f800000U,
		0xbf800000U, 0x3f800000U, 0x00000000U, 0x3f800000U,
	};
	const struct powervr_render_target *color;
	struct powervr_set_binding_info binding;
	struct powervr_sampler_shape sampler;
	struct powervr_vk_graphics *graphics;
	uint64_t view_words[2];
	uint64_t sampler_words[2];
	uint32_t offset;
	uint32_t bits;
	int error;

	/* The pipeline for the attachment's format. */
	color = &job->colors[0];
	mutex_lock(&render->core->lock);

	error = load_graphics(render, color->surface.format->vk_format, &graphics);

	mutex_unlock(&render->core->lock);

	/* Reports a pipeline that could not be made. */
	if (error != 0)
		return error;

	/* The vertices, in the job's general memory. */
	_Static_assert(sizeof(vertices) <= GENERAL_DBIAS - GENERAL_LOAD_WORDS, "the load draw's vertices' room");
	put_words(memory->general->buffer, GENERAL_LOAD_WORDS, vertices, LOAD_DRAW_VERTICES * 4U);

	/* The set: one combined image sampler. */
	binding.binding = 0;
	binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.count = 1;
	error = powervr_set_layout_build(&binding, 1, &load->set.layout);
	if (error != 0)
		return error;
	load->set.bytes = kern_calloc(1, load->set.layout.bytes);
	if (load->set.bytes == NULL)
		return ENOMEM;
	error = powervr_view_words(&color->view, view_words);
	if (error != 0)
		return error;
	error = powervr_set_descriptor_offset(&load->set.layout, 0, 0, POWERVR_SHADER_PART_IMAGE_STATE, &offset);
	if (error != 0)
		return error;
	powervr_descriptor_image(&load->set.bytes[offset], view_words, (uint32_t)color->view.layout->layer_bytes, powervr_pack_info(color->surface.format->vk_format));
	kern_memset(&sampler, 0, sizeof(sampler));
	sampler.mag_filter = VK_FILTER_NEAREST;
	sampler.min_filter = VK_FILTER_NEAREST;
	sampler.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	error = powervr_sampler_words(&sampler, sampler_words);
	if (error != 0)
		return error;
	error = powervr_set_descriptor_offset(&load->set.layout, 0, 0, POWERVR_SHADER_PART_SAMPLER_STATE, &offset);
	if (error != 0)
		return error;
	powervr_descriptor_sampler(&load->set.bytes[offset], sampler_words, VK_COMPARE_OP_NEVER);

	/* The draw: the six vertices once, the framebuffer's viewport, the render area's scissor. */
	load->draw.pipeline = graphics;
	load->draw.sets[0] = &load->set;
	load->draw.vertex_addresses[0] = memory->general->address + GENERAL_LOAD_WORDS;
	load->draw.count = LOAD_DRAW_VERTICES;
	load->draw.instances = 1;
	bits = float_bits(job->width);
	kern_memcpy(&load->draw.viewport.width, &bits, sizeof(bits));
	bits = float_bits(job->height);
	kern_memcpy(&load->draw.viewport.height, &bits, sizeof(bits));
	bits = float_bits(1);
	kern_memcpy(&load->draw.viewport.maxDepth, &bits, sizeof(bits));
	load->draw.scissor.offset.x = (int32_t)job->area.x;
	load->draw.scissor.offset.y = (int32_t)job->area.y;
	load->draw.scissor.extent.width = job->area.width;
	load->draw.scissor.extent.height = job->area.height;

	/* Succeeded: the draw. */
	return 0;
}

/* The bits of a whole number below 2^24 as a 32-bit float, without the processor's floating point. */
static uint32_t
float_bits(
	uint32_t value)
{
	uint32_t exponent;

	/* Zero is all zero bits. */
	if (value == 0)
		return 0;

	/* The highest set bit gives the exponent; the bits below it, shifted up, the mantissa. */
	exponent = 31U - (uint32_t)__builtin_clz(value);
	return ((127U + exponent) << 23) | ((value << (23U - exponent)) & 0x7fffffU);
}

/* Frees a job's working copy and its load draw's set. */
static void
load_draw_free(
	struct powervr_render_job *work,
	struct load_draw *load)
{
	if (load != NULL)
		kern_free(load->set.bytes);
	kern_free(load);
	kern_free(work);
}

/* The draw at an index of a job: the prefix draw first when there is one, then the job's own. */
static const struct powervr_render_draw *
job_draw(
	const struct powervr_render_job *job,
	const struct powervr_render_draw *prefix,
	uint32_t index)
{
	/* Without a prefix the index is the job's own. */
	if (prefix == NULL)
		return &job->draws[index];

	/* The prefix comes first. */
	if (index == 0)
		return prefix;

	/* Succeeded: the job's draw after the prefix. */
	return &job->draws[index - 1U];
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

	/* A colour of one 32-bit word a pixel, or of four half floats in two (the compiler packs them). */
	color = &job->colors[0];
	if (color->surface.format == NULL)
		return ENOTSUP;

	/* Refuses any other pixel size. */
	if (color->surface.format->texel_bytes != 4U && color->surface.format->vk_format != VK_FORMAT_R16G16B16A16_SFLOAT)
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
	struct powervr_data_load loads[POWERVR_RENDER_LOAD_SHARED_WORDS / 2U];
	struct powervr_render_area whole;
	uint32_t blit[POWERVR_TRANSFER_SHARED_WORDS];
	uint32_t words[POWERVR_RENDER_LOAD_SHARED_WORDS];
	uint32_t value[4];
	uint32_t clear_words;
	uint32_t pair;
	int error;

	/* The background's program and the shared register program that feeds it. */
	if (color->load_op == LOAD_OP_LOAD && POWERVR_RENDER_LOAD_PROBE == LOAD_PROBE_BLIT) {
		/* The transfer's blit (coordinates from the position by a scale and an offset, filtered sample, packed), one to one over the attachment. */
		error = powervr_transfer_blit_usc(color->surface.format, &memory->program);
		if (error != 0)
			return error;

		/* Its words over the whole attachment, one to one. */
		whole.x = 0;
		whole.y = 0;
		whole.width = color->view.width;
		whole.height = color->view.height;
		error = powervr_transfer_blit_words(&color->view, &whole, &whole, false, false, false, blit);
		if (error != 0)
			return error;

		/* The words in the job's memory and the program that loads them. */
		put_words(memory->general->buffer, GENERAL_LOAD_WORDS, blit, POWERVR_TRANSFER_SHARED_WORDS);
		error = powervr_transfer_shared_program(memory->general->address + GENERAL_LOAD_WORDS, &memory->shared);
		if (error != 0)
			return error;

		/* The blit's shared words. */
		*shared_registers = POWERVR_TRANSFER_SHARED_WORDS;
	} else if (color->load_op == LOAD_OP_LOAD && POWERVR_RENDER_LOAD_PROBE != LOAD_PROBE_CONSTANT) {
		/* The attachment's image and sampler words, and the program that samples it. */
		error = powervr_render_load_usc_probe(color->surface.format, LOAD_USC_PROBE_FORM, &memory->program);
		if (error != 0)
			return error;

		error = powervr_render_load_words(&color->view, words);
		if (error != 0)
			return error;

		/* The diagnostics: where the load reads and its words. */
		if (POWERVR_DIAGNOSTICS) {
			kern_logf("powervr: render job: load from 0x%llx (%s, %ux%u, view mapping %u %u %u %u), image words %08x %08x %08x %08x, sampler %08x %08x\n",
				  (unsigned long long)color->view.address, color->view.linear ? "linear" : "twiddled",
				  (unsigned)color->view.width, (unsigned)color->view.height,
				  (unsigned)color->view.mapping[0], (unsigned)color->view.mapping[1], (unsigned)color->view.mapping[2], (unsigned)color->view.mapping[3],
				  (unsigned)words[0], (unsigned)words[1], (unsigned)words[2], (unsigned)words[3], (unsigned)words[4], (unsigned)words[5]);
		}

		/* The words in the job's memory, and the program that loads them (or writes them as constants for the WRITTEN probe). */
		put_words(memory->general->buffer, GENERAL_LOAD_WORDS, words, POWERVR_RENDER_LOAD_SHARED_WORDS);
		if (POWERVR_RENDER_LOAD_PROBE == LOAD_PROBE_WRITTEN) {
			/* The words as four pairs of constants. */
			for (pair = 0; pair < POWERVR_RENDER_LOAD_SHARED_WORDS / 2U; pair++) {
				loads[pair].kind = POWERVR_DATA_LOAD_WRITE;
				loads[pair].shared = (uint16_t)(pair * 2U);
				loads[pair].words = 2;
				loads[pair].source = (uint64_t)words[pair * 2U] | (uint64_t)words[pair * 2U + 1U] << 32;
			}

			error = powervr_data_shared_build(loads, POWERVR_RENDER_LOAD_SHARED_WORDS / 2U, &memory->shared);
		} else {
			error = powervr_render_load_shared(memory->general->address + GENERAL_LOAD_WORDS, &memory->shared);
		}

		/* Reports a program that could not be built. */
		if (error != 0)
			return error;

		*shared_registers = POWERVR_RENDER_LOAD_SHARED_WORDS;
	} else {
		/*
		 * The clear value in the format's words (two for sRGB, whose
		 * back end takes F16, within the two output registers a pixel
		 * the fragment command gives), or zero for contents that do not
		 * matter.
		 */
		clear_words = powervr_format_clear_word_count(color->surface.format);
		if (clear_words == 0 || clear_words > 2U)
			return ENOTSUP;

		/* The value: the clear colour, zero, or the constant probe's magenta. */
		kern_memset(value, 0, sizeof(value));
		if (color->load_op == LOAD_OP_CLEAR)
			kern_memcpy(value, color->clear, sizeof(value));
		if (color->load_op == LOAD_OP_LOAD) {
			/* The constant probe: magenta, opaque, in the format's packed word as a clear's value is. */
			value[0] = 0xffff00ffU;
		}

		/* The program that writes the value. */
		error = powervr_render_clear_usc_words(clear_words, &memory->program);
		if (error != 0)
			return error;

		error = powervr_render_clear_shared_words(value, clear_words, &memory->shared);
		if (error != 0)
			return error;

		*shared_registers = clear_words;
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
	const struct powervr_render_draw *prefix,
	struct job_memory *memory,
	uint32_t *stream_words,
	uint32_t *vertex_shareds)
{
	const struct powervr_vk_graphics *graphics;
	const struct powervr_vk_descriptor_set *set;
	const struct powervr_render_draw *draw;
	struct powervr_buffer *buffer;
	uint64_t sets_bytes;
	uint64_t usc_bytes;
	uint64_t sets_place;
	uint64_t usc_place;
	uint32_t draw_count;
	uint32_t index;
	uint32_t slot;
	uint32_t words;
	int error;

	/* The job's draws after the prefix draw, when there is one. */
	draw_count = job->draw_count;
	if (prefix != NULL)
		draw_count++;

	/* What the draws take: their shaders' code and their sets' copies, each on 16 bytes. */
	sets_bytes = 0;
	usc_bytes = 0;
	for (index = 0; index < draw_count; index++) {
		draw = job_draw(job, prefix, index);
		graphics = draw->pipeline;
		if (graphics == NULL)
			return EINVAL;
		usc_bytes += (graphics->vertex.length + 15U) & ~(uint64_t)15U;
		usc_bytes += (graphics->fragment.length + 15U) & ~(uint64_t)15U;
		for (slot = 0; slot < POWERVR_DRAW_SETS; slot++) {
			set = draw->sets[slot];
			if (set != NULL)
				sets_bytes += (set->layout.bytes + 15U) & ~15U;
		}
	}

	/* The general memory: a slot per draw, then the sets. */
	error = powervr_buffer_create(render, (uint64_t)draw_count * DRAW_SLOT + sets_bytes + 16U, &buffer);
	if (error != 0)
		return error;

	error = powervr_resource_make(session, buffer, 1, 0, &memory->draw_general);
	powervr_buffer_put(render, buffer);
	if (error != 0) {
		memory->draw_general = NULL;
		return error;
	}

	/* The program buffers, in their windows. */
	error = powervr_buffer_create(render, (uint64_t)draw_count * DRAW_SLOT, &memory->draw_pds);
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
	sets_place = (uint64_t)draw_count * DRAW_SLOT;
	usc_place = 0;
	for (index = 0; index < draw_count; index++) {
		draw = job_draw(job, prefix, index);
		graphics = draw->pipeline;
		error = draw_place(job, draw, index, memory, sets_place, usc_place, *stream_words, &words);
		if (error != 0)
			return error;

		*stream_words += words;
		if (graphics->vertex.shareds > *vertex_shareds)
			*vertex_shareds = graphics->vertex.shareds;
		usc_place += (graphics->vertex.length + 15U) & ~(uint64_t)15U;
		usc_place += (graphics->fragment.length + 15U) & ~(uint64_t)15U;
		for (slot = 0; slot < POWERVR_DRAW_SETS; slot++) {
			set = draw->sets[slot];
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
	const struct powervr_render_draw *draw,
	uint32_t index,
	struct job_memory *memory,
	uint64_t sets_place,
	uint64_t usc_place,
	uint32_t stream_at,
	uint32_t *words)
{
	const struct powervr_vk_graphics *graphics;
	const struct powervr_vk_descriptor_set *set;
	const struct powervr_draw_attribute *attribute;
	const struct powervr_shader_input *input;
	const VkViewport *draw_viewport;
	uint32_t dma_words;
	uint32_t defaults;
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

	/* The vertex program: a DMA per attribute into its input registers, the defaults (0, 0, 0, 1.0) beside them. */
	put_bytes(memory->draw_general->buffer, index * DRAW_SLOT + DRAW_DEFAULTS, attribute_defaults, sizeof(attribute_defaults));
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
		error = attribute_words(attribute->format, input->components, &dma_words, &defaults);
		if (error != 0)
			return error;

		memory->dmas[dma_count].words = dma_words;
		memory->dmas[dma_count].reg = input->reg;
		dma_count++;

		/*
		 * Procedure referenced: Mesa 25.0.7 src/imagination/vulkan/pvr_pipeline.c
		 * (the vertex DMA's size_in_dwords is the format's block bits / 32)
		 * and the Vulkan 1.0 specification, "Vertex Input Extraction" (a
		 * format with fewer components than the shader's input gives 0
		 * for G and B and 1 for A) -- the components past the format's
		 * are a stride-0 load from the slot's defaults.
		 */
		if (dma_words < input->components && defaults != ATTRIBUTE_NO_DEFAULTS) {
			if (dma_count == POWERVR_DATA_VERTEX_DMAS_MAX)
				return ENOTSUP;
			memory->dmas[dma_count].address = general + DRAW_DEFAULTS + defaults + dma_words * 4U;
			memory->dmas[dma_count].stride = 0;
			memory->dmas[dma_count].words = input->components - dma_words;
			memory->dmas[dma_count].reg = input->reg + dma_words;
			dma_count++;
		}
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

/*
 * The words a vertex attribute's DMA loads, the format's bytes but no
 * more than the shader's input takes, and where its defaults are: a
 * format of 32-bit channels lacks the components past its own (the
 * defaults fill them); a packed one's words are the shader's to unpack,
 * so only 32-bit-channel formats take defaults.
 */
static int
attribute_words(
	uint32_t format,
	uint32_t components,
	uint32_t *words,
	uint32_t *defaults)
{
	const struct powervr_format *found;
	uint32_t format_words;

	/* A format the table does not know: the shader's input, as the pipeline let it through. */
	found = powervr_format_find(format);
	*defaults = ATTRIBUTE_NO_DEFAULTS;
	if (found == NULL || found->texel_bytes == 0) {
		*words = components;
		return 0;
	}

	/* The format's bytes in whole words; the defaults of an integer 32-bit-channel format after the float ones. */
	format_words = ((uint32_t)found->texel_bytes + 3U) / 4U;
	if (format >= VK_FORMAT_R32_UINT && format <= VK_FORMAT_R32G32B32A32_SFLOAT) {
		*defaults = 0;
		if (format != VK_FORMAT_R32_SFLOAT && format != VK_FORMAT_R32G32_SFLOAT && format != VK_FORMAT_R32G32B32_SFLOAT && format != VK_FORMAT_R32G32B32A32_SFLOAT)
			*defaults = 16;
	}
	*words = format_words < components ? format_words : components;

	/* Succeeded: the words and the defaults' offset. */
	return 0;
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

/*
 * Sets the test-only draw filter: a render job keeps its own draws below
 * limit (DEBUG_NO_LIMIT, 0xffffffff, for all) whose bits in skip are
 * clear; limit 0xffffffff and skip 0 turn it off.
 */
void
powervr_render_debug_set(
	uint32_t limit,
	uint64_t skip)
{
	/* The setting, for every later job. */
	debug_draw_limit = limit;
	debug_draw_skip = skip;
	kern_logf("powervr: debug: render jobs keep draws below %u, skipping mask 0x%llx\n", (unsigned)limit, (unsigned long long)skip);
}

/* Drops a job's draws the test-only filter names, keeping the rest in order. */
static void
debug_filter(
	struct powervr_render_job *job)
{
	uint32_t limit;
	uint64_t skip;
	uint32_t index;
	uint32_t kept;

	/* The setting, read once. */
	limit = debug_draw_limit;
	skip = debug_draw_skip;

	/* Each draw below the limit whose bit is clear, moved down. */
	kept = 0;
	for (index = 0; index < job->draw_count; index++) {
		if (index >= limit)
			break;
		if (index < 64U && (skip & (1ULL << index)) != 0U)
			continue;
		if (kept != index)
			job->draws[kept] = job->draws[index];
		kept++;
	}
	job->draw_count = kept;
}
