/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The render context record, the kick commands of a render and the
 * reading of a context reset notice.  Nothing here locks or allocates, so
 * the host test builds it as it is.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-kick-layout.h"

/* The command's data follows its type and flags. */
#define COMMAND_DATA	8U

/*
 * The record and the command fields fit together as the firmware's own
 * structures do.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_shared_check.h
 * (struct rogue_fwif_geom_registers_caswitch 184 bytes,
 * geom_state[1].geom_reg_vdm_context_store_task0 104,
 * geom_state[0].geom_reg_vdm_context_resume_task4 96; struct
 * rogue_fwif_static_rendercontext_state 368 bytes);
 * pvr_rogue_fwif_check.h (struct rogue_fwif_kccb_cmd, 88 bytes).
 */
_Static_assert(POWERVR_SWITCH_BUFFER + 2U * POWERVR_SWITCH_BUFFER_BYTES == POWERVR_SWITCH_CORE_BYTES, "switch registers");
_Static_assert(POWERVR_SWITCH_BUFFER + POWERVR_SWITCH_BUFFER_BYTES == 104U, "second buffer");
_Static_assert(POWERVR_SWITCH_BUFFER + POWERVR_SWITCH_RESUME_4 == 96U, "resume task 4");
_Static_assert(POWERVR_SWITCH_CORES * POWERVR_SWITCH_CORE_BYTES == 368U, "static render state");
_Static_assert(POWERVR_RENDER_CONTEXT_STATIC + 368U + 8U == POWERVR_RENDER_CONTEXT_BYTES, "render context");
_Static_assert(COMMAND_DATA + 2U * POWERVR_KICK_DATA_BYTES <= POWERVR_KICK_COMMAND_WORDS * 4U, "combined kick");
_Static_assert(POWERVR_KICK_CLEANUP_ADDRESSES + 4U * POWERVR_KICK_CLEANUPS_MAX == POWERVR_KICK_WORK_ESTIMATE, "cleanup counts");
_Static_assert(POWERVR_FRAGMENT_PARTIAL_BACKGROUND + 24U <= POWERVR_FRAGMENT_BYTES, "partial render background");

static void put32(uint8_t *bytes, uint32_t offset, uint32_t value);
static void put64(uint8_t *bytes, uint32_t offset, uint64_t value);
static uint32_t get32(const uint8_t *bytes, uint32_t offset);
static int kick_data(const struct powervr_kick *kick, uint32_t *data);

/* Writes a little-endian 32-bit word. */
static void
put32(
	uint8_t *bytes,
	uint32_t offset,
	uint32_t value)
{
	/* Low byte first, as the firmware reads it. */
	bytes[offset + 0U] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
	bytes[offset + 2U] = (uint8_t)(value >> 16);
	bytes[offset + 3U] = (uint8_t)(value >> 24);
}

/* Writes a little-endian 64-bit word. */
static void
put64(
	uint8_t *bytes,
	uint32_t offset,
	uint64_t value)
{
	/* The low half, then the high. */
	put32(bytes, offset, (uint32_t)value);
	put32(bytes, offset + 4U, (uint32_t)(value >> 32));
}

/* Reads a little-endian 32-bit word. */
static uint32_t
get32(
	const uint8_t *bytes,
	uint32_t offset)
{
	/* Low byte first. */
	return (uint32_t)bytes[offset] | (uint32_t)bytes[offset + 1U] << 8 |
	    (uint32_t)bytes[offset + 2U] << 16 | (uint32_t)bytes[offset + 3U] << 24;
}

/*
 * Fills the eight words of one kick's data.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_cccb.c
 * (pvr_cccb_send_kccb_kick: the context, the write offset, the wrap
 * mask, then the render target data set's cleanup count when the job has
 * one, and no work estimate).
 */
static int
kick_data(
	const struct powervr_kick *kick,
	uint32_t *data)
{
	unsigned index;

	/* Refuses more cleanup counts than the command holds. */
	if (kick->cleanup_count > POWERVR_KICK_CLEANUPS_MAX)
		return EINVAL;

	/* The context and its buffer's offsets, then the cleanup counts; the rest stays zero. */
	data[0] = kick->context;
	data[1] = kick->write_offset;
	data[2] = kick->wrap_mask;
	data[3] = kick->cleanup_count;
	for (index = 0; index < POWERVR_KICK_CLEANUPS_MAX; index++)
		data[4U + index] = index < kick->cleanup_count ? kick->cleanups[index] : 0U;
	data[7] = 0;

	/* Succeeded: the data. */
	return 0;
}

/* Builds the kick command of one context. */
int
powervr_kick_command(
	const struct powervr_kick *kick,
	uint32_t command[POWERVR_KICK_COMMAND_WORDS])
{
	unsigned index;

	/* A zeroed command of the kick's type. */
	for (index = 0; index < POWERVR_KICK_COMMAND_WORDS; index++)
		command[index] = 0;
	command[0] = 0x2abc0000U | 101U;

	/* The kick's data. */
	return kick_data(kick, &command[COMMAND_DATA / 4U]);
}

/*
 * Builds the combined kick of a geometry context and its partial render.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_cccb.c
 * (pvr_cccb_send_kccb_combined_kick: the geometry's kick data, then the
 * fragment's; the caller gives the partial render no cleanup count, as
 * the firmware attaches none to it).
 */
int
powervr_kick_combined_command(
	const struct powervr_kick *geometry,
	const struct powervr_kick *fragment,
	uint32_t command[POWERVR_KICK_COMMAND_WORDS])
{
	unsigned index;
	int error;

	/* A zeroed command of the combined type. */
	for (index = 0; index < POWERVR_KICK_COMMAND_WORDS; index++)
		command[index] = 0;
	command[0] = POWERVR_COMMAND_COMBINED_KICK;

	/* The geometry's data, then the fragment's. */
	error = kick_data(geometry, &command[COMMAND_DATA / 4U]);
	if (error != 0)
		return error;
	return kick_data(fragment, &command[(COMMAND_DATA + POWERVR_KICK_DATA_BYTES) / 4U]);
}

/*
 * Puts the render target and the partial render test of a geometry
 * command.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_queue.c
 * (pvr_queue_submit_job_to_cccb: a paired geometry's
 * partial_render_geom_frag_fence is the geometry queue's timeline with
 * the job's sequence number minus one); pvr_job.c (the render target
 * data set's address in the shared part).
 */
void
powervr_geometry_partial_fence(
	uint8_t *command,
	uint32_t target_data,
	uint32_t timeline,
	uint32_t value)
{
	/* The data set, then the fence. */
	put32(command, POWERVR_SHARED_TARGET_DATA, target_data);
	put32(command, POWERVR_GEOMETRY_PARTIAL_FENCE, timeline);
	put32(command, POWERVR_GEOMETRY_PARTIAL_FENCE + 4U, value - 1U);
}

/* Puts the render target data set of a fragment or partial render command. */
void
powervr_fragment_target(
	uint8_t *command,
	uint32_t target_data)
{
	/* The shared part's data set. */
	put32(command, POWERVR_SHARED_TARGET_DATA, target_data);
}

/*
 * Fills the static state of a render context.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_context.c
 * (pvr_render_ctx_ws_static_state_init: the VDM and TA state buffers'
 * addresses, then for each buffer the store tasks of the shared register
 * store program and the persistent temporaries' store program, and the
 * resume tasks of the load and resume programs); Linux v7.3
 * drivers/gpu/drm/imagination/pvr_stream_defs.c
 * (pvr_static_render_context_state_stream: the stream fills the first
 * core's registers only).  The second core's registers are filled the
 * same, as the BXE-4-32 has one geometry core and the copy costs nothing.
 */
void
powervr_render_switch_state(
	uint8_t *record,
	uint64_t vdm_state,
	uint64_t ta_state,
	const uint64_t store[5],
	const uint64_t resume[5])
{
	static const uint32_t store_at[5] = {
		POWERVR_SWITCH_STORE_0, POWERVR_SWITCH_STORE_1, POWERVR_SWITCH_STORE_2,
		POWERVR_SWITCH_STORE_3, POWERVR_SWITCH_STORE_4,
	};
	static const uint32_t resume_at[5] = {
		POWERVR_SWITCH_RESUME_0, POWERVR_SWITCH_RESUME_1, POWERVR_SWITCH_RESUME_2,
		POWERVR_SWITCH_RESUME_3, POWERVR_SWITCH_RESUME_4,
	};
	uint32_t core;
	uint32_t buffer;
	uint32_t base;
	unsigned task;

	/* Each core: the state buffers, then both buffers' task words. */
	for (core = 0; core < POWERVR_SWITCH_CORES; core++) {
		base = POWERVR_RENDER_CONTEXT_STATIC + core * POWERVR_SWITCH_CORE_BYTES;
		put64(record, base + POWERVR_SWITCH_VDM_STATE, vdm_state);
		put64(record, base + POWERVR_SWITCH_VDM_RESUME, 0);
		put64(record, base + POWERVR_SWITCH_TA_STATE, ta_state);
		for (buffer = 0; buffer < 2U; buffer++) {
			for (task = 0; task < 5U; task++) {
				put64(record, base + POWERVR_SWITCH_BUFFER + buffer * POWERVR_SWITCH_BUFFER_BYTES + store_at[task], store[task]);
				put64(record, base + POWERVR_SWITCH_BUFFER + buffer * POWERVR_SWITCH_BUFFER_BYTES + resume_at[task], resume[task]);
			}
		}
	}
}

/* Reads a context reset notice's data. */
void
powervr_reset_notice_read(
	const uint8_t *data,
	struct powervr_reset_notice *notice)
{
	/* The fields the stage line and the session's lookup use. */
	notice->context = get32(data, POWERVR_RESET_CONTEXT);
	notice->reason = get32(data, POWERVR_RESET_REASON);
	notice->data_master = get32(data, POWERVR_RESET_DATA_MASTER);
	notice->flags = get32(data, POWERVR_RESET_FLAGS);
	notice->fault_address = (uint64_t)get32(data, POWERVR_RESET_FAULT_ADDRESS) |
	    (uint64_t)get32(data, POWERVR_RESET_FAULT_ADDRESS + 4U) << 32;
}

/* The stage a reset names and what its line says. */
void
powervr_reset_stage_of(
	const struct powervr_reset_notice *notice,
	enum powervr_context_kind kind,
	struct powervr_reset_stage *result)
{
	/* The stage: the context's kind, a render context's master. */
	if (kind == POWERVR_CONTEXT_TRANSFER)
		result->stage = POWERVR_STAGE_TRANSFER;
	else if (kind == POWERVR_CONTEXT_COMPUTE)
		result->stage = POWERVR_STAGE_COMPUTE;
	else if (notice->data_master == POWERVR_DATA_MASTER_GEOMETRY)
		result->stage = POWERVR_STAGE_TA;
	else
		result->stage = POWERVR_STAGE_3D;

	/* A page fault names its address; any other reset its reason. */
	if ((notice->flags & POWERVR_RESET_FLAG_FAULT) != 0U) {
		result->reason = POWERVR_REASON_FAULT;
		result->value = notice->fault_address;
	} else {
		result->reason = POWERVR_REASON_RESET;
		result->value = notice->reason;
	}
}
