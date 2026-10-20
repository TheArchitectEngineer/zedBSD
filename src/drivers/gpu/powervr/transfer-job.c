/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs one transfer on a session's transfer context.
 *
 * A clear writes its colour through the clear registers; a copy runs a
 * background object whose program samples the source.  Either stores the
 * rectangle through the end-of-tile program.  The programs and the
 * shared words are written into memory of the transfer's own, mapped in
 * the session's windows, and returned once it is done.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/transfer-job.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/render-command.h"
#include "drivers/gpu/powervr/render-program.h"

/* How long a transfer may take. */
#define TRANSFER_WAIT_MS		2000U

/*
 * The transfer's memory: the copy's shared words in the general window;
 * in the data sequencer's the end-of-tile data, the copy's task program
 * (data, then code after eight words) and its shared register program's
 * code and data; in the shading cluster's the end-of-tile and copy
 * programs.
 */
#define GENERAL_SHARED			0x0U
#define GENERAL_BYTES			0x1000U
#define PDS_EVENT			0x000U
#define PDS_KICK			0x040U
#define PDS_SHARED_CODE			0x080U
#define PDS_SHARED_DATA			0x200U
#define PDS_BYTES			0x1000U
#define PDS_SHARED_CODE_ROOM		(PDS_SHARED_DATA - PDS_SHARED_CODE)
#define PDS_SHARED_DATA_ROOM		(PDS_BYTES - PDS_SHARED_DATA)
#define USC_EOT				0x000U
#define USC_COPY			0x100U
#define USC_BYTES			0x1000U

/* The transfer's memory and programs while it runs. */
struct transfer_memory {
	struct powervr_resource *general;
	struct powervr_buffer *pds;
	struct powervr_buffer *usc;
	uint64_t pds_place;
	uint64_t usc_place;
	struct powervr_render_usc program;
	struct powervr_render_kick kick;
	struct powervr_data_shared_program shared;
	struct powervr_transfer_job job;
	struct powervr_transfer_regs regs;
	uint8_t command[POWERVR_TRANSFER_COMMAND_BYTES];
};

static int transfer_one(struct powervr_render *render, struct powervr_session *session, struct transfer_memory *memory, const struct powervr_transfer_request *request, const struct powervr_render_area *area, uint32_t source_y);
static int transfer_map(struct powervr_render *render, struct powervr_session *session, struct transfer_memory *memory);
static void transfer_release(struct powervr_render *render, struct powervr_session *session, struct transfer_memory *memory);
static void put_words(struct powervr_buffer *buffer, uint32_t offset, const uint32_t *words, unsigned count);
static void put_bytes(struct powervr_buffer *buffer, uint32_t offset, const uint8_t *bytes, size_t count);

/*
 * Runs a transfer, one row at a time when the destination's rows need
 * it.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_3d_copy_blit_core: the background object for a copy, the clear
 * registers for a clear, the store through the pixel event).
 */
int
powervr_transfer_run(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_transfer_request *request)
{
	struct powervr_transfer_context *context;
	struct transfer_memory *memory;
	struct powervr_render_area row;
	uint32_t index;
	int error;

	/* A copy names its source. */
	if (request->kind != POWERVR_TRANSFER_CLEAR && request->source == NULL)
		return EINVAL;

	/* The session's transfer context, and the transfer's memory. */
	error = powervr_session_transfer_context(render, session, &context);
	if (error != 0)
		return error;

	memory = kern_calloc(1, sizeof(*memory));
	if (memory == NULL)
		return ENOMEM;

	error = transfer_map(render, session, memory);
	if (error != 0) {
		transfer_release(render, session, memory);
		return error;
	}

	/* The whole rectangle, or each of its rows. */
	if (!powervr_transfer_needs_rows(&request->destination, &request->area)) {
		error = transfer_one(render, session, memory, request, &request->area, request->source_y);
	} else {
		row = request->area;
		row.height = 1;
		for (index = 0; error == 0 && index < request->area.height; index++) {
			row.y = request->area.y + index;
			error = transfer_one(render, session, memory, request, &row, request->source_y + index);
		}
	}

	/* A transfer the core may still run keeps its memory. */
	if (error == ETIMEDOUT) {
		kern_logf("powervr: transfer did not finish; its memory stays with the core\n");
		session->abandoned = 1;
		return error;
	}

	/* Done or refused: the memory goes. */
	transfer_release(render, session, memory);
	if (error != 0)
		return error;

	/* Succeeded: the transfer is done. */
	return 0;
}

/* Builds, kicks and waits for the transfer of one rectangle. */
static int
transfer_one(
	struct powervr_render *render,
	struct powervr_session *session,
	struct transfer_memory *memory,
	const struct powervr_transfer_request *request,
	const struct powervr_render_area *area,
	uint32_t source_y)
{
	struct powervr_transfer_job *job;
	uint32_t words[POWERVR_TRANSFER_SHARED_WORDS];
	uint32_t event[POWERVR_RENDER_EVENT_DATA_WORDS];
	uint32_t state[2];
	uint32_t pds_base;
	uint32_t usc_base;
	uint32_t value;
	int error;

	/* The job: the kind, the destination, the rectangle, the programs' places. */
	pds_base = (uint32_t)(memory->pds_place - POWERVR_PDS_HEAP_BASE);
	usc_base = (uint32_t)(memory->usc_place - POWERVR_USC_HEAP_BASE);
	job = &memory->job;
	kern_memset(job, 0, sizeof(*job));
	job->kind = request->kind;
	job->destination = request->destination;
	job->area = *area;
	kern_memcpy(job->colour, request->colour, sizeof(job->colour));
	job->source_x = request->source_x;
	job->source_y = source_y;
	job->copy_kick_offset = pds_base + PDS_KICK;
	job->shared_code_offset = pds_base + PDS_SHARED_CODE;
	job->shared_data_offset = pds_base + PDS_SHARED_DATA;
	job->event_data_offset = pds_base + PDS_EVENT;

	/* A copy: its program, its task program, its shared words and the program that loads them. */
	if (request->kind == POWERVR_TRANSFER_COPY) {
		error = powervr_transfer_copy_usc(&memory->program);
		if (error != 0)
			return error;

		put_bytes(memory->usc, USC_COPY, memory->program.code, memory->program.length);
		error = powervr_render_kick_build(usc_base + USC_COPY, memory->program.temps, &memory->kick);
		if (error != 0)
			return error;

		put_words(memory->pds, PDS_KICK, memory->kick.data, POWERVR_RENDER_KICK_DATA_WORDS);
		put_words(memory->pds, PDS_KICK + POWERVR_RENDER_KICK_DATA_WORDS * 4U, memory->kick.code, POWERVR_RENDER_KICK_CODE_WORDS);
		error = powervr_transfer_shared_words(request->source, request->source_x, source_y, area, words);
		if (error != 0)
			return error;

		put_words(memory->general->buffer, GENERAL_SHARED, words, POWERVR_TRANSFER_SHARED_WORDS);
		error = powervr_transfer_shared_program(memory->general->address + GENERAL_SHARED, &memory->shared);
		if (error != 0)
			return error;

		if (memory->shared.code_words * 4U > PDS_SHARED_CODE_ROOM || memory->shared.data_words * 4U > PDS_SHARED_DATA_ROOM)
			return E2BIG;

		put_words(memory->pds, PDS_SHARED_CODE, memory->shared.code, memory->shared.code_words);
		put_words(memory->pds, PDS_SHARED_DATA, memory->shared.data, memory->shared.data_words);
		job->shared_data_words = memory->shared.data_words;
	}

	/* The registers, and the end-of-tile program that emits the back end's state. */
	error = powervr_transfer_regs_build(job, &memory->regs, state);
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

	/* The command, its kick, its wait; the wait says the transfer stage. */
	kern_memset(memory->command, 0, sizeof(memory->command));
	powervr_transfer_command(&memory->regs, memory->command);
	mutex_lock(&render->core->lock);

	error = powervr_session_kick_transfer(render, session, memory->command, &value);

	mutex_unlock(&render->core->lock);

	if (error != 0)
		return error;

	error = powervr_transfer_finish(session, value, TRANSFER_WAIT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the rectangle is written. */
	return 0;
}

/* Makes and maps the transfer's memory. */
static int
transfer_map(
	struct powervr_render *render,
	struct powervr_session *session,
	struct transfer_memory *memory)
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

/* Unmaps and returns the transfer's memory and its bookkeeping. */
static void
transfer_release(
	struct powervr_render *render,
	struct powervr_session *session,
	struct transfer_memory *memory)
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
