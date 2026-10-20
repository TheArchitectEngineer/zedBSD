/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR render node's queue and recovery.
 *
 * Each session owns a compute context of the firmware with a command
 * buffer of its own and a timeline word the firmware advances.  A native
 * command becomes one job in that buffer, followed by an update of the
 * timeline; a kick through the kernel's buffer tells the firmware the
 * buffer grew.  A service thread, woken by the firmware's interrupt and
 * once a tick while work is pending, retires the commands whose timeline
 * value was reached and reads the firmware's notices.
 *
 * Recovery follows the GPU core's contract: a session is stopped by
 * having the firmware clean its context up, isolated when that cannot be
 * confirmed while the firmware still answers, and the device is put in
 * quarantine when the firmware stops answering.  A reset restarts the
 * firmware from the image kept in memory and frees what was kept.
 *
 * Lock order: the core's lock, then the queue lock; the buffer guard is
 * never held with either.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/fixed-area.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/render-kick-layout.h"
#include "drivers/gpu/powervr/render-command.h"

/*
 * How long the service sleeps with nothing pending, how many tries a
 * busy cleanup gets and how far apart, how long a command may wait in
 * the kernel's full buffer, and how far past the firmware's own deadline
 * a pending command makes the service ask whether the firmware is alive.
 */
#define SERVICE_IDLE_MS		100U
#define CLEANUP_TRIES		100U
#define CLEANUP_PAUSE_MS	10U
#define KICK_WAIT_MS		1000U
#define WATCHDOG_MS		(POWERVR_DEADLINE_MS + 5000U)

/* A kick of a render context is one kernel command. */
_Static_assert(POWERVR_KICK_COMMAND_WORDS == POWERVR_COMMAND_WORDS, "a kick is one kernel command");

/* The most 32-bit words of one command a render puts in a context's buffer. */
#define RENDER_COMMAND_WORDS	(POWERVR_FRAGMENT_BYTES / 4U)

/* The most callbacks one pass of the service delivers. */
#define DELIVER_MAX		64U

/* Callbacks taken under the queue lock, delivered after it. */
struct delivery {
	struct drv_gpu_completion *completion[DELIVER_MAX];
	int error[DELIVER_MAX];
	struct powervr_session *session[DELIVER_MAX];
	unsigned count;
	unsigned markers_freed;
};

/* What the notices the firmware queued asked of the host. */
struct notice_outcome {
	struct powervr_render *render;
	int device_lost;
};

static int take_table(void *context, struct powervr_gpu_table *table);
static void give_table(void *context, struct powervr_gpu_table *table);
static void *take_memory(void *context, size_t size);
static void give_memory(void *context, void *memory);
static int map_fixed_area(struct powervr_session *session);
static void keep_fixed_area(struct powervr_session *session);
static int make_records(struct powervr_render *render, struct powervr_session *session);
static void fill_records(struct powervr_session *session);
static void destroy_records(struct powervr_render *render, struct powervr_session *session);
static int client_space(const struct powervr_client *client, uint32_t bytes);
static void client_put(struct powervr_client *client, uint32_t type, uint32_t size, const uint32_t *data, uint32_t words);
static int send_kick(struct powervr_render *render, struct powervr_session *session);
static int send_command(struct powervr_render *render, const uint32_t command[POWERVR_COMMAND_WORDS]);
static uint32_t commands_bytes(const struct powervr_client_command *commands, unsigned count);
static void commands_put(struct powervr_client *client, const struct powervr_client_command *commands, unsigned count);
static void list_marks(const struct powervr_render *render, struct powervr_list_marks *marks);
static int kick_refused(struct powervr_render *render, struct powervr_session *session);
static int send_waiting(struct powervr_core *core, const uint32_t command[POWERVR_COMMAND_WORDS], uint32_t *slot);
static void cleanup_command(uint32_t context, uint32_t command[POWERVR_COMMAND_WORDS]);
static void service_thread(void *argument);
static int service_pass(struct powervr_render *render, uint64_t now);
static void take_entry(struct powervr_session *session, int error, struct delivery *delivery);
static void take_reached(struct powervr_session *session, struct delivery *delivery);
static void take_all(struct powervr_session *session, int error, struct delivery *delivery);
static void deliver(struct powervr_render *render, struct delivery *delivery);
static void handle_notice(void *context, uint32_t type, const struct powervr_object *buffer, uint32_t offset);
static uint64_t ms_ticks(unsigned milliseconds);
static unsigned is_faulted(struct powervr_render *render);

/* Where a session's page tables and the host's bookkeeping of them come from. */
static const struct powervr_gpu_memory table_memory_template = {
	.take_table = take_table,
	.give_table = give_table,
	.take_memory = take_memory,
	.give_memory = give_memory,
	.context = NULL,
};

/*
 * Makes the pages of a session's fixed area with its programs written;
 * the core's lock is not held.
 *
 * Each heap's page is zeroed when made, so the general heap's fence and
 * colour coefficients stay zero; on failure the pages made so far stay
 * in the session for powervr_session_fixed_drop().
 */
int
powervr_session_fixed_make(
	struct powervr_render *render,
	struct powervr_session *session)
{
	int error;

	/* The general heap's page. */
	error = powervr_buffer_create(render, POWERVR_FIXED_GENERAL_BYTES, &session->fixed[POWERVR_FIXED_HEAP_GENERAL]);
	if (error != 0)
		return error;

	/* The data sequencer heap's page. */
	error = powervr_buffer_create(render, POWERVR_FIXED_PDS_BYTES, &session->fixed[POWERVR_FIXED_HEAP_PDS]);
	if (error != 0)
		return error;

	/* The shading cluster heap's page. */
	error = powervr_buffer_create(render, POWERVR_FIXED_USC_BYTES, &session->fixed[POWERVR_FIXED_HEAP_USC]);
	if (error != 0)
		return error;

	/* The programs, through the processor's uncached views. */
	error = powervr_fixed_area_write(session->fixed[POWERVR_FIXED_HEAP_PDS]->view, session->fixed[POWERVR_FIXED_HEAP_USC]->view);
	if (error != 0)
		return error;

	/* Succeeded: the pages are ready to be mapped. */
	return 0;
}

/*
 * Returns the pages of a session's fixed area that nothing maps; the
 * core's lock is not held.
 */
void
powervr_session_fixed_drop(
	struct powervr_render *render,
	struct powervr_session *session)
{
	unsigned heap;

	/* Each page that was made goes back; a fault keeps it until the reset. */
	for (heap = 0; heap < POWERVR_FIXED_HEAPS; heap++) {
		if (session->fixed[heap] != NULL) {
			powervr_buffer_put(render, session->fixed[heap]);
			session->fixed[heap] = NULL;
		}
	}
}

/*
 * Makes a session's memory context, with the fixed area's pages mapped,
 * and its firmware records; the core's lock is held.
 */
int
powervr_session_firmware_create(
	struct powervr_render *render,
	struct powervr_session *session)
{
	int error;

	/* The program windows, with the executor's places held. */
	error = powervr_session_heaps_init(session);
	if (error != 0)
		return error;

	/* The tables come from runs below the core's limit; the callbacks find the limit through the node. */
	session->mmu.memory = NULL;
	error = powervr_gpu_mmu_init(&session->mmu, &render->table_memory);
	if (error != 0)
		return error;

	/* The fixed area at the start of its three heaps, before anything else is mapped there. */
	error = map_fixed_area(session);
	if (error != 0) {
		powervr_gpu_mmu_fini(&session->mmu);
		return error;
	}

	/* The firmware's records of the session; nothing has been kicked, so they and the tables just go on failure. */
	error = make_records(render, session);
	if (error != 0) {
		destroy_records(render, session);
		powervr_gpu_mmu_fini(&session->mmu);
		return error;
	}

	/* Fills them: the memory context, the command buffer's control and the compute context. */
	fill_records(session);

	/* Succeeded: the firmware can run jobs of the session once kicked. */
	return 0;
}

/*
 * Returns a session's firmware records and tables; the firmware no
 * longer uses them.
 */
void
powervr_session_firmware_destroy(
	struct powervr_render *render,
	struct powervr_session *session)
{
	/* The render and transfer contexts first, with the memory the render's switch uses. */
	powervr_render_context_destroy(render, session);
	powervr_transfer_context_destroy(render, session);
	powervr_compute_switch_destroy(render, session, 0);

	/* Returns the records, unmapping each from the firmware's processor. */
	mutex_lock(&render->core->lock);

	destroy_records(render, session);

	/* Has the core drop anything it cached of the tables before they go. */
	if (session->mmu.catalogue.view != NULL && !render->lost)
		(void)powervr_command_mmu_flush(render->core);

	mutex_unlock(&render->core->lock);

	/* Returns the tables, then the fixed area's pages they named. */
	powervr_gpu_mmu_fini(&session->mmu);
	powervr_session_fixed_drop(render, session);
}

/*
 * Returns a kept session's records and tables once the firmware that knew
 * them is gone; the records' old places in the firmware's heap are not
 * touched.
 */
void
powervr_session_firmware_discard(
	struct powervr_session *session)
{
	/* The render context, the render targets and free lists, then each record's run. */
	powervr_render_context_discard(session);
	powervr_transfer_context_discard(session);
	powervr_compute_switch_destroy(session->render, session, 1);
	powervr_session_targets_discard(session);
	powervr_object_drop(&session->context);
	powervr_object_drop(&session->compute.timeline);
	powervr_object_drop(&session->compute.buffer);
	powervr_object_drop(&session->compute.control);
	powervr_object_drop(&session->context_state);
	powervr_object_drop(&session->memory_context);

	/* Returns the tables, then hands the fixed area's pages they named to the kept buffers. */
	powervr_gpu_mmu_fini(&session->mmu);
	keep_fixed_area(session);
}

/*
 * Has the firmware clean up each of a session's contexts that ran (the
 * compute context, then the render context's geometry and fragment
 * contexts) and waits for each, bounded; EBUSY when one stays busy.
 *
 * Procedure referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/imagination/pvr_fw.c (pvr_fw_structure_cleanup: the
 * cleanup command naming the common context, a bounded wait on its
 * return slot, busy when the slot says so); Linux v7.3
 * drivers/gpu/drm/imagination/pvr_context.c (pvr_context_destroy_queues:
 * each queue of a render context cleaned up through its own common
 * context).
 */
int
powervr_session_cleanup(
	struct powervr_render *render,
	struct powervr_session *session)
{
	uint32_t command[POWERVR_COMMAND_WORDS];
	uint32_t context;
	uint32_t answer;
	unsigned which;
	unsigned tries;
	int error;

	/* Each context still to clean up, in turn, until none is left. */
	for (;;) {
		context = powervr_session_next_cleanup(session, &which);
		if (context == 0U)
			break;

		/* Sends the cleanup until the firmware says the context is idle, a bounded number of times. */
		cleanup_command(context, command);
		error = EBUSY;
		for (tries = 0; tries < CLEANUP_TRIES; tries++) {
			mutex_lock(&render->core->lock);

			error = powervr_command_run(render->core, command, POWERVR_COMMAND_WAIT_MS, &answer);

			mutex_unlock(&render->core->lock);

			/* The firmware did not answer. */
			if (error != 0)
				return error;

			/* The context is idle and forgotten. */
			if ((answer & POWERVR_RETURN_CLEANUP_BUSY) == 0U)
				break;

			/* Still busy: waits a little before asking again. */
			error = EBUSY;
			sched_sleep(sched_ticks() + ms_ticks(CLEANUP_PAUSE_MS));
		}

		/* The context stayed busy. */
		if (error != 0)
			return error;

		/* The firmware no longer uses this one. */
		powervr_session_cleaned(session, which);
	}

	/* Succeeded: the firmware no longer uses any of the session's contexts. */
	return 0;
}

/*
 * Kicks one empty job on a session's context that advances its timeline
 * to value once done, after an optional wait for a sync word; the core's
 * lock is held.
 *
 * Procedure referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/imagination/pvr_cccb.c
 * (pvr_cccb_write_command_with_header: each command behind its header,
 * its size aligned to 8, a padding command when the next would not fit
 * before the end; pvr_cccb_send_kccb_kick: the context, the new write
 * offset and the wrap mask in a kick command after a barrier) and
 * pvr_queue.c (the fences before the job and the timeline update after
 * it).
 */
int
powervr_session_kick(
	struct powervr_render *render,
	struct powervr_session *session,
	uint32_t value,
	uint32_t fence_address,
	uint32_t fence_value)
{
	uint32_t sync[2];
	uint32_t bytes;
	int error;

	/* Room for the fence, the empty job and the update, each behind a header. */
	bytes = 2U * POWERVR_HEADER_BYTES + POWERVR_SYNC_BYTES;
	if (fence_address != 0U)
		bytes += POWERVR_HEADER_BYTES + POWERVR_SYNC_BYTES;
	error = client_space(&session->compute, bytes);
	if (error != 0)
		return error;

	/* The fence: the job waits until the sync word holds the value. */
	if (fence_address != 0U) {
		sync[0] = fence_address;
		sync[1] = fence_value;
		client_put(&session->compute, POWERVR_CLIENT_FENCE, POWERVR_SYNC_BYTES, sync, 2);
	}

	/* The empty job, then the update of the timeline once it is done. */
	client_put(&session->compute, POWERVR_CLIENT_NULL, 0, NULL, 0);
	sync[0] = powervr_object_address(&session->compute.timeline, 0);
	sync[1] = value;
	client_put(&session->compute, POWERVR_CLIENT_UPDATE, POWERVR_SYNC_BYTES, sync, 2);

	/* Tells the firmware the buffer grew. */
	error = send_kick(render, session);
	if (error != 0)
		return error;

	/* Succeeded: the job is the firmware's. */
	session->kicked = 1;
	return 0;
}

/*
 * Kicks one compute command on a session's context that advances its
 * timeline to value once done; the core's lock is held.
 *
 * Procedure referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/imagination/pvr_queue.c
 * (pvr_queue_submit_job_to_cccb: the job's command, then the update of
 * the queue's timeline).
 */
int
powervr_session_kick_compute(
	struct powervr_render *render,
	struct powervr_session *session,
	uint32_t value,
	const uint32_t *command,
	uint32_t words)
{
	uint32_t sync[2];
	uint32_t bytes;
	int error;

	/* No kick on a device in quarantine or a session that is stopping, lost or abandoned. */
	error = kick_refused(render, session);
	if (error != 0)
		return error;

	/* Room for the command and the update, each behind a header. */
	bytes = 2U * POWERVR_HEADER_BYTES + ((words * 4U + 7U) & ~7U) + POWERVR_SYNC_BYTES;
	error = client_space(&session->compute, bytes);
	if (error != 0)
		return error;

	/* The compute command, then the update of the timeline once it is done. */
	client_put(&session->compute, POWERVR_CLIENT_COMPUTE, words * 4U, command, words);
	sync[0] = powervr_object_address(&session->compute.timeline, 0);
	sync[1] = value;
	client_put(&session->compute, POWERVR_CLIENT_UPDATE, POWERVR_SYNC_BYTES, sync, 2);

	/* Tells the firmware the buffer grew. */
	error = send_kick(render, session);
	if (error != 0)
		return error;

	/* Succeeded: the command is the firmware's. */
	session->kicked = 1;
	return 0;
}

/*
 * Kicks one render on a session's render context; the core's lock is
 * held.
 *
 * The geometry and its partial render go in one combined kick, the
 * geometry's naming the render target half's cleanup count and the
 * partial render's none; the fragment follows in a kick of its own with
 * the cleanup count.  The geometry's timeline takes the next value, the
 * fragment's the one after next (the partial render writes the one
 * between).  A fragment command that names no background after a
 * partial render takes the context's stand-in.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_queue.c (pvr_queue_run_job:
 * the geometry and its paired fragment written, then the combined kick;
 * any other job's own kick) and pvr_cccb.c
 * (pvr_cccb_send_kccb_combined_kick, pvr_cccb_send_kccb_kick:
 * fill_cmd_kick_data with the data set's cleanup state, none for a
 * partial render, after a barrier).
 */
int
powervr_session_kick_render(
	struct powervr_render *render,
	struct powervr_session *session,
	struct powervr_target *target,
	unsigned half,
	const uint8_t *geometry,
	const uint8_t *partial,
	const uint8_t *fragment,
	const struct powervr_sync *waits,
	unsigned wait_count,
	struct powervr_render_marks *marks)
{
	struct powervr_render_context *context;
	struct powervr_render_sequence *sequence;
	struct powervr_render_request request;
	struct powervr_kick geometry_kick;
	struct powervr_kick partial_kick;
	struct powervr_kick fragment_kick;
	uint32_t command[POWERVR_COMMAND_WORDS];
	uint32_t cleanup;
	uint32_t bytes;
	int error;

	/* Refuses a session without a render context and a half that does not exist. */
	context = session->render_context;
	if (context == NULL || half > 1U)
		return EINVAL;

	/* No kick on a device in quarantine or a session that is stopping, lost or abandoned. */
	error = kick_refused(render, session);
	if (error != 0)
		return error;

	/* What the render's commands name: the timelines and their next values, the data set, the waits. */
	request.geometry_timeline = powervr_object_address(&context->geometry.timeline, 0);
	request.geometry_value = context->geometry.submitted + 1U;
	request.fragment_timeline = powervr_object_address(&context->fragment.timeline, 0);
	request.fragment_value = context->fragment.submitted + 2U;
	request.target_data = powervr_target_data_address(target, half);
	request.waits = waits;
	request.wait_count = wait_count;
	request.geometry = geometry;
	request.partial = partial;
	request.fragment = fragment;
	request.partial_background = context->background;
	sequence = &context->sequence;
	error = powervr_render_sequence_make(&request, sequence);
	if (error != 0)
		return error;

	/* Room in both buffers for all of it. */
	bytes = commands_bytes(sequence->geometry_commands, sequence->geometry_count);
	error = client_space(&context->geometry, bytes);
	if (error != 0)
		return error;

	bytes = commands_bytes(sequence->partial_commands, sequence->partial_count);
	bytes += commands_bytes(sequence->fragment_commands, sequence->fragment_count);
	error = client_space(&context->fragment, bytes);
	if (error != 0)
		return error;

	/* What the firmware reported of the free lists before, for the render's end to compare. */
	list_marks(render, &marks->lists);

	/* The geometry and the partial render, then their combined kick. */
	cleanup = powervr_target_cleanup_address(target, half);
	commands_put(&context->geometry, sequence->geometry_commands, sequence->geometry_count);
	commands_put(&context->fragment, sequence->partial_commands, sequence->partial_count);
	geometry_kick.context = powervr_object_address(&context->record, POWERVR_RENDER_CONTEXT_GEOMETRY);
	geometry_kick.write_offset = context->geometry.write;
	geometry_kick.wrap_mask = POWERVR_CLIENT_CCB_BYTES - 1U;
	geometry_kick.cleanups[0] = cleanup;
	geometry_kick.cleanup_count = 1;
	partial_kick.context = powervr_object_address(&context->record, POWERVR_RENDER_CONTEXT_FRAGMENT);
	partial_kick.write_offset = context->fragment.write;
	partial_kick.wrap_mask = POWERVR_CLIENT_CCB_BYTES - 1U;
	partial_kick.cleanup_count = 0;
	error = powervr_kick_combined_command(&geometry_kick, &partial_kick, command);
	if (error != 0)
		return error;

	error = send_command(render, command);
	if (error != 0)
		return error;

	/* From here the firmware has the context's jobs. */
	context->kicked = 1;
	context->geometry.submitted = request.geometry_value;
	context->fragment.submitted = request.fragment_value - 1U;

	/* The fragment, then its own kick. */
	commands_put(&context->fragment, sequence->fragment_commands, sequence->fragment_count);
	fragment_kick = partial_kick;
	fragment_kick.write_offset = context->fragment.write;
	fragment_kick.cleanups[0] = cleanup;
	fragment_kick.cleanup_count = 1;
	error = powervr_kick_command(&fragment_kick, command);
	if (error != 0)
		return error;

	error = send_command(render, command);
	if (error != 0)
		return error;

	/* Succeeded: the render is the firmware's; its end is the fragment's value. */
	context->fragment.submitted = request.fragment_value;
	marks->geometry_value = request.geometry_value;
	marks->fragment_value = request.fragment_value;
	return 0;
}

/*
 * Kicks one transfer command on a session's transfer context; the
 * core's lock is held.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_queue.c
 * (pvr_queue_submit_job_to_cccb: the job's command, then the update of
 * the queue's timeline; pvr_queue_run_job: its own kick, no data set).
 */
int
powervr_session_kick_transfer(
	struct powervr_render *render,
	struct powervr_session *session,
	const uint8_t *command,
	uint32_t *value)
{
	struct powervr_transfer_context *context;
	struct powervr_client_command commands[2];
	struct powervr_sync update;
	struct powervr_kick kick;
	uint32_t words[POWERVR_COMMAND_WORDS];
	uint32_t bytes;
	int error;

	/* Refuses a session without a transfer context. */
	context = session->transfer_context;
	if (context == NULL)
		return EINVAL;

	/* No kick on a device in quarantine or a session that is stopping, lost or abandoned. */
	error = kick_refused(render, session);
	if (error != 0)
		return error;

	/* The command and the update of the timeline to the next value. */
	update.address = powervr_object_address(&context->client.timeline, 0);
	update.value = context->client.submitted + 1U;
	commands[0].type = POWERVR_CLIENT_TRANSFER;
	commands[0].size = POWERVR_TRANSFER_COMMAND_BYTES;
	commands[0].data = command;
	commands[1].type = POWERVR_CLIENT_UPDATE;
	commands[1].size = POWERVR_SYNC_BYTES;
	commands[1].data = (const uint8_t *)&update;
	bytes = commands_bytes(commands, 2);
	error = client_space(&context->client, bytes);
	if (error != 0)
		return error;

	/* Writes them, then the kick naming the context. */
	commands_put(&context->client, commands, 2);
	kick.context = powervr_object_address(&context->record, 0);
	kick.write_offset = context->client.write;
	kick.wrap_mask = POWERVR_CLIENT_CCB_BYTES - 1U;
	kick.cleanup_count = 0;
	error = powervr_kick_command(&kick, words);
	if (error != 0)
		return error;

	error = send_command(render, words);
	if (error != 0)
		return error;

	/* Succeeded: the transfer is the firmware's. */
	context->kicked = 1;
	context->client.submitted = update.value;
	*value = update.value;
	return 0;
}

/* Waits, bounded, for a transfer to finish and reports its stage. */
int
powervr_transfer_finish(
	struct powervr_session *session,
	uint32_t value,
	unsigned milliseconds)
{
	struct powervr_transfer_context *context;
	uint64_t deadline;
	uint64_t now;
	uint32_t current;

	/* Refuses a session without a transfer context. */
	context = session->transfer_context;
	if (context == NULL)
		return EINVAL;

	/* Reads the timeline once a tick until it reaches the value or the bound is spent. */
	deadline = sched_ticks() + ms_ticks(milliseconds);
	for (;;) {
		current = powervr_object_get32(&context->client.timeline, POWERVR_SYNC_ADDRESS);
		if ((int32_t)(current - value) >= 0)
			break;

		/* Out of time: the transfer stage fails with a timeout. */
		now = sched_ticks();
		if (now >= deadline) {
			powervr_stage_fail(POWERVR_STAGE_TRANSFER, POWERVR_REASON_TIMEOUT);
			return ETIMEDOUT;
		}

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* Succeeded: the transfer finished. */
	powervr_stage_ok(POWERVR_STAGE_TRANSFER);
	return 0;
}

/*
 * Waits, bounded, for a render's fragment to finish and reports its
 * stages.
 *
 * A render that runs out of time names the stage of the first kick whose
 * timeline did not reach its value; one that finishes says whether its
 * free lists ran out on the way.
 */
int
powervr_render_finish(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_render_marks *marks,
	unsigned milliseconds)
{
	struct powervr_render_context *context;
	struct powervr_list_marks lists;
	enum powervr_stage stage;
	uint64_t deadline;
	uint64_t now;
	uint32_t geometry_now;
	uint32_t fragment_now;
	int stalled;
	int ran_out;

	/* Refuses a session without a render context. */
	context = session->render_context;
	if (context == NULL)
		return EINVAL;

	/* Reads the fragment's timeline once a tick until it reaches the value or the bound is spent. */
	deadline = sched_ticks() + ms_ticks(milliseconds);
	for (;;) {
		geometry_now = powervr_object_get32(&context->geometry.timeline, POWERVR_SYNC_ADDRESS);
		fragment_now = powervr_object_get32(&context->fragment.timeline, POWERVR_SYNC_ADDRESS);
		stalled = powervr_render_stalled(geometry_now, marks->geometry_value, fragment_now, marks->fragment_value, &stage);
		if (!stalled)
			break;

		/* Out of time: the stage it stopped in fails with a timeout. */
		now = sched_ticks();
		if (now >= deadline) {
			powervr_stage_fail(stage, POWERVR_REASON_TIMEOUT);
			return ETIMEDOUT;
		}

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* What the firmware reported of the free lists since the kicks. */
	mutex_lock(&render->core->lock);

	list_marks(render, &lists);

	mutex_unlock(&render->core->lock);

	/* The TA ran out of free list pages, or did not; the 3D finished. */
	ran_out = powervr_free_list_ran_out(&marks->lists, &lists);
	if (ran_out) {
		powervr_stage_fail(POWERVR_STAGE_TA, POWERVR_REASON_FREE_LIST);
	} else {
		powervr_stage_ok(POWERVR_STAGE_TA);
	}
	powervr_stage_ok(POWERVR_STAGE_3D);

	/* Succeeded: the render finished. */
	return 0;
}

/*
 * Kicks a session's context again without new commands; the core's lock
 * is held.
 */
int
powervr_session_rekick(
	struct powervr_render *render,
	struct powervr_session *session)
{
	int error;

	/* The same write offset again. */
	error = send_kick(render, session);
	if (error != 0)
		return error;

	/* Succeeded: the firmware looks at the context again. */
	return 0;
}

/*
 * Reads a session's timeline word as the firmware left it.
 */
uint32_t
powervr_session_timeline(
	const struct powervr_session *session)
{
	uint32_t value;

	/* One load through the record's uncached view. */
	value = powervr_object_get32(&session->compute.timeline, POWERVR_SYNC_ADDRESS);

	/* Succeeded: the value. */
	return value;
}

/*
 * Starts the thread that retires native commands and reads the firmware's
 * notices.
 */
int
powervr_service_start(
	struct powervr_render *render)
{
	struct thread *thread;
	int error;

	/* The table callbacks find the node through their context. */
	render->table_memory = table_memory_template;
	render->table_memory.context = render;

	/* Starts the thread; nobody joins it, and its end releases it. */
	render->service_done = 0;
	error = kthread_create(service_thread, render, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/* Succeeded: the service runs. */
	return 0;
}

/*
 * Ends the service thread and waits until it has.
 */
void
powervr_service_stop(
	struct powervr_render *render)
{
	unsigned long enabled;
	uint64_t observed;

	/* Tells the thread to end, wakes it, and sleeps until it says it has. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	render->running = 0;
	waitq_wake_all(&render->service);
	while (!render->service_done) {
		observed = waitq_sequence(&render->retired);
		(void)waitq_sleep(&render->retired, &render->queue_lock, observed, 0, 0);
	}

	spin_unlock_irqrestore(&render->queue_lock, enabled);
}

/*
 * Ends every pending callback of a session with an error.
 */
void
powervr_session_fail_pending(
	struct powervr_render *render,
	struct powervr_session *session,
	int error)
{
	struct delivery delivery;
	unsigned long enabled;

	/* Takes every pending command of the session. */
	delivery.count = 0;
	delivery.markers_freed = 0;
	enabled = spin_lock_irqsave(&render->queue_lock);

	take_all(session, error, &delivery);

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* Delivers them outside the lock. */
	deliver(render, &delivery);
}

/*
 * Arms the device-wide quarantine.  Idempotent.
 */
void
powervr_render_quarantine(
	struct powervr_render *render,
	int error)
{
	struct powervr_session *session;
	struct delivery delivery;
	unsigned long enabled;
	unsigned already;

	/* Sets the flag once; from here no kick goes out and nothing the core may reach is freed. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	already = render->faulted;
	render->faulted = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* A second call has nothing more to do. */
	if (already)
		return;

	/* Ends every session's pending callbacks with the error; the jobs may still run. */
	mutex_lock(&render->core->lock);

	for (session = render->sessions; session != NULL; session = session->next) {
		delivery.count = 0;
		delivery.markers_freed = 0;
		enabled = spin_lock_irqsave(&render->queue_lock);

		take_all(session, error, &delivery);

		spin_unlock_irqrestore(&render->queue_lock, enabled);
		deliver(render, &delivery);
	}

	mutex_unlock(&render->core->lock);
}

/*
 * Takes one native command: an empty job retired through the session's
 * timeline.
 */
int
powervr_render_submit(
	void *device,
	void *opaque,
	const void *command,
	uint32_t bytes,
	uint32_t flags,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct powervr_render *render;
	struct powervr_session *session;
	const uint8_t *words;
	unsigned long enabled;
	uint32_t opcode;
	uint32_t reserved;
	int refused;
	int error;

	/* A frame that is not the empty job is a command frame of the Vulkan transport. */
	render = device;
	session = opaque;
	opcode = 0;
	reserved = 0;
	if (bytes == POWERVR_NATIVE_BYTES && command != NULL) {
		words = command;
		kern_memcpy(&opcode, words, sizeof(opcode));
		kern_memcpy(&reserved, words + 4, sizeof(reserved));
	}
	if (opcode != POWERVR_NATIVE_EMPTY_JOB || reserved != 0U) {
		error = powervr_render_frame(render, session, command, bytes, flags, timeline, completion);
		if (error != 0)
			return error;

		return 0;
	}

	/* The empty job, on the session's one timeline. */
	if (timeline != 0U)
		return EINVAL;
	if ((flags & ~GPU_COMMAND_CONTEXT_FENCE) != 0U)
		return EINVAL;

	/* Refuses work on a device or session that is stopping or lost, and a full ring. */
	mutex_lock(&render->core->lock);

	enabled = spin_lock_irqsave(&render->queue_lock);

	refused = 0;
	if (render->faulted || session->stopping || session->lost || session->isolated) {
		refused = EIO;
	} else if (session->pending_count + session->reserved >= POWERVR_RENDER_PENDING) {
		refused = EBUSY;
	}

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	if (refused != 0) {
		mutex_unlock(&render->core->lock);
		return refused;
	}

	/* Kicks the job with the session's next timeline value, and records it pending for the service. */
	error = powervr_session_kick(render, session, session->submitted + 1U, 0, 0);
	if (error == 0) {
		session->submitted++;
		powervr_session_pend(render, session, session->submitted, completion, 0, POWERVR_NO_MARKER);
	}

	mutex_unlock(&render->core->lock);

	/* The firmware did not take the job; no callback is held. */
	if (error != 0)
		return error;

	/* Succeeded: the callback is held until the job retires. */
	return 0;
}

/*
 * Puts a callback in a session's ring: retired in order once the
 * session's timeline reaches value, delivered with the error, freeing the
 * job marker when it names one.  The caller checked the ring has room.
 */
void
powervr_session_pend(
	struct powervr_render *render,
	struct powervr_session *session,
	uint32_t value,
	struct drv_gpu_completion *completion,
	int error,
	unsigned marker)
{
	unsigned long enabled;
	unsigned slot;

	/* The entry after the last one, and the service woken to look at it. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	slot = (session->pending_head + session->pending_count) % POWERVR_RENDER_PENDING;
	session->pending[slot].value = value;
	session->pending[slot].submitted = sched_ticks();
	session->pending[slot].completion = completion;
	session->pending[slot].error = error;
	session->pending[slot].marker = marker;
	session->pending_count++;
	session->outstanding++;
	waitq_wake_all(&render->service);

	spin_unlock_irqrestore(&render->queue_lock, enabled);
}

/*
 * Waits until every callback the session's commands hold has been
 * delivered.
 */
void
powervr_render_drain(
	void *device,
	void *opaque)
{
	struct powervr_render *render;
	struct powervr_session *session;
	unsigned long enabled;
	uint64_t observed;

	/* Sleeps on the deliveries until the session has none left. */
	render = device;
	session = opaque;
	enabled = spin_lock_irqsave(&render->queue_lock);

	while (session->outstanding != 0U) {
		observed = waitq_sequence(&render->retired);
		(void)waitq_sleep(&render->retired, &render->queue_lock, observed, 0, 0);
	}

	spin_unlock_irqrestore(&render->queue_lock, enabled);
}

/*
 * Stops a session's new work and asks the firmware to clean up the first
 * of its contexts that ran, without waiting; the poll asks for the rest.
 */
int
powervr_render_stop_begin(
	void *device,
	void *opaque,
	int error)
{
	struct powervr_render *render;
	struct powervr_session *session;
	uint32_t command[POWERVR_COMMAND_WORDS];
	unsigned long enabled;
	uint32_t context;
	unsigned faulted;
	unsigned which;
	int sent;

	/* No new kick from here. */
	(void)error;
	render = device;
	session = opaque;
	enabled = spin_lock_irqsave(&render->queue_lock);

	session->stopping = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* No context that ran, or a device that is gone, has nothing to ask. */
	context = powervr_session_next_cleanup(session, &which);
	faulted = is_faulted(render);
	if (context == 0U || faulted)
		return 0;

	/* Asks for the cleanup; a full buffer is retried by the poll. */
	cleanup_command(context, command);
	mutex_lock(&render->core->lock);

	sent = powervr_command_send(render->core, command, &session->stop_slot);

	mutex_unlock(&render->core->lock);
	session->stop_context = which;
	session->stop_since = sched_ticks();
	session->stop_tries = 1;
	session->stop_sent = 0;
	if (sent == 0)
		session->stop_sent = 1;

	/* Succeeded: the stop is under way. */
	return 0;
}

/*
 * Reports whether a session's stop is done: EAGAIN while it is not, zero
 * once the firmware cleaned every context that ran up and every callback
 * was delivered, another error when the firmware does not confirm it.
 */
int
powervr_render_stop_poll(
	void *device,
	void *opaque)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct delivery delivery;
	uint32_t command[POWERVR_COMMAND_WORDS];
	unsigned long enabled;
	uint32_t context;
	uint32_t answer;
	uint64_t waited;
	unsigned outstanding;
	unsigned faulted;
	unsigned which;
	int sent;

	/* A device in quarantine confirms nothing. */
	render = device;
	session = opaque;
	faulted = is_faulted(render);
	if (faulted)
		return EIO;

	/* A context still to be cleaned up: reads the firmware's answer without waiting. */
	context = powervr_session_next_cleanup(session, &which);
	while (context != 0U) {
		cleanup_command(context, command);
		mutex_lock(&render->core->lock);

		/* A cleanup the full buffer refused, or the first of a new context, is sent again. */
		if (!session->stop_sent || session->stop_context != which) {
			session->stop_context = which;
			sent = powervr_command_send(render->core, command, &session->stop_slot);
			session->stop_sent = 0;
			if (sent == 0)
				session->stop_sent = 1;
			answer = POWERVR_RETURN_NONE;
		} else {
			answer = powervr_object_get32(&render->core->firmware.kccb_returns, session->stop_slot * 4U);
		}

		/* Busy: the context still runs, asks again a bounded number of times. */
		if ((answer & POWERVR_RETURN_EXECUTED) != 0U && (answer & POWERVR_RETURN_CLEANUP_BUSY) != 0U) {
			session->stop_sent = 0;
			session->stop_tries++;
			sent = powervr_command_send(render->core, command, &session->stop_slot);
			if (sent == 0)
				session->stop_sent = 1;
			answer = POWERVR_RETURN_NONE;
		}

		mutex_unlock(&render->core->lock);

		/* Not done yet: out of tries, out of time, or asked again later. */
		if ((answer & POWERVR_RETURN_EXECUTED) == 0U) {
			if (session->stop_tries > CLEANUP_TRIES)
				return EBUSY;
			waited = sched_ticks() - session->stop_since;
			if (waited > ms_ticks(POWERVR_DEADLINE_MS))
				return ETIMEDOUT;
			return EAGAIN;
		}

		/* Done: the firmware forgot this context; the next one, if any, is asked for. */
		powervr_session_cleaned(session, which);
		session->stop_sent = 0;
		context = powervr_session_next_cleanup(session, &which);
	}

	/* Retires what reached the timeline, then ends the rest with an error: their jobs no longer run. */
	delivery.count = 0;
	delivery.markers_freed = 0;
	enabled = spin_lock_irqsave(&render->queue_lock);

	take_reached(session, &delivery);
	take_all(session, ECANCELED, &delivery);

	spin_unlock_irqrestore(&render->queue_lock, enabled);
	deliver(render, &delivery);

	/* Waits for callbacks another thread is still delivering. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	outstanding = session->outstanding;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	if (outstanding != 0U)
		return EAGAIN;

	/* Succeeded: nothing of the session runs and every callback is delivered. */
	return 0;
}

/*
 * Puts the device in quarantine.  Idempotent; it says nothing about the
 * hardware having stopped.
 */
void
powervr_render_fault(
	void *device,
	int error)
{
	/* The node's quarantine, with the core's error for the callbacks. */
	powervr_render_quarantine(device, error);
}

/*
 * Restarts the firmware from the image kept in memory, once every old
 * owner has retired, and frees what was kept for it.
 */
int
powervr_render_reset(
	void *device)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_buffer *buffer;
	unsigned long enabled;
	int error;

	/* Restarts the firmware: it stops the processor, powers the core off and on, and boots it. */
	render = device;
	mutex_lock(&render->core->lock);

	error = powervr_firmware_restart(render->core);
	if (error != 0)
		render->lost = 1;

	/* The kept sessions' records belonged to the stopped firmware: their runs go, their old places with it. */
	while (render->kept_sessions != NULL) {
		session = render->kept_sessions;
		render->kept_sessions = session->next;
		powervr_session_firmware_discard(session);
		kern_free(session);
	}

	mutex_unlock(&render->core->lock);

	/* The kept buffers: nothing reaches them any more. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	buffer = render->kept_buffers;
	render->kept_buffers = NULL;
	if (error == 0)
		render->faulted = 0;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	powervr_buffers_free(buffer);

	/* The firmware did not come back; the device stays lost. */
	if (error != 0) {
		kern_logf("powervr: firmware restart failed (%d); the render node stays lost\n", error);
		return error;
	}

	/* Succeeded: the firmware runs again and nothing old is kept. */
	kern_logf("powervr: firmware restarted after a reset\n");
	return 0;
}

/*
 * Isolates a session whose stop could not be confirmed: keeps everything
 * the firmware may reach of it until a reset, ends its callbacks with an
 * error, and checks the firmware still serves the others.
 */
int
powervr_render_isolate(
	void *device,
	void *opaque)
{
	struct powervr_render *render;
	struct powervr_session *session;
	uint32_t command[POWERVR_COMMAND_WORDS];
	unsigned long enabled;
	uint32_t answer;
	uint32_t index;
	unsigned faulted;
	int error;

	/* From here every destroy and the close keep what the session owned. */
	render = device;
	session = opaque;
	enabled = spin_lock_irqsave(&render->queue_lock);

	session->isolated = 1;
	session->stopping = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* A device in quarantine cannot isolate one session. */
	faulted = is_faulted(render);
	if (faulted)
		return EIO;

	/* Checks the firmware still answers, so the other sessions go on. */
	for (index = 0; index < POWERVR_COMMAND_WORDS; index++)
		command[index] = 0;
	command[POWERVR_COMMAND_TYPE / 4U] = POWERVR_COMMAND_HEALTH_CHECK;
	mutex_lock(&render->core->lock);

	error = powervr_command_run(render->core, command, POWERVR_COMMAND_WAIT_MS, &answer);

	mutex_unlock(&render->core->lock);

	/* The firmware does not answer: the core puts the device in quarantine. */
	if (error != 0)
		return error;

	/* Ends the session's callbacks with an error. */
	powervr_session_fail_pending(render, session, EIO);

	/* Succeeded: the session is isolated and the firmware serves the rest. */
	return 0;
}

/*
 * Wakes the render node's service from the interrupt handler, when there
 * is one.
 */
void
powervr_render_interrupt(
	struct powervr_core *core)
{
	struct powervr_render *render;
	unsigned long enabled;

	/* No node yet: the later stage counts the interrupt only. */
	render = core->render;
	if (render == NULL)
		return;

	/* Wakes the service. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	waitq_wake_all(&render->service);

	spin_unlock_irqrestore(&render->queue_lock, enabled);
}

/* Takes one zeroed table page below the core's limit, with its uncached view. */
static int
take_table(
	void *context,
	struct powervr_gpu_table *table)
{
	struct powervr_render *render;
	struct kern_pmem run;
	volatile uint64_t *words;
	unsigned index;
	void *view;
	int error;

	/* One page the core can name. */
	render = context;
	error = kern_pmem_alloc_limited(POWERVR_GPU_PAGE_BYTES, POWERVR_GPU_PAGE_BYTES, render->core->description.dma_limit, 0, &run);
	if (error != 0)
		return error;

	/* Its uncached view, through which every entry is written. */
	error = kern_pmem_map_uncached(&run, &view);
	if (error != 0) {
		(void)kern_pmem_free(&run);
		return error;
	}

	/* Cleared: no entry is valid. */
	words = view;
	for (index = 0; index < POWERVR_GPU_PAGE_BYTES / 8U; index++)
		words[index] = 0;

	/* Succeeded: the page. */
	table->physical = run.paddr;
	table->view = view;
	table->opaque = NULL;
	return 0;
}

/* Returns a table page. */
static void
give_table(
	void *context,
	struct powervr_gpu_table *table)
{
	struct kern_pmem run;

	/* Ends the view and returns the page. */
	(void)context;
	run.paddr = table->physical;
	run.size = POWERVR_GPU_PAGE_BYTES;
	(void)kern_pmem_unmap_uncached((void *)table->view, POWERVR_GPU_PAGE_BYTES);
	(void)kern_pmem_free(&run);
	table->view = NULL;
}

/* Takes zeroed bookkeeping memory. */
static void *
take_memory(
	void *context,
	size_t size)
{
	void *memory;

	/* Kernel memory, cleared. */
	(void)context;
	memory = kern_calloc(1, size);

	/* Succeeded: the memory, or NULL when there is none. */
	return memory;
}

/* Returns bookkeeping memory. */
static void
give_memory(
	void *context,
	void *memory)
{
	/* Back to the kernel. */
	(void)context;
	kern_free(memory);
}

/*
 * Maps the fixed area's pages at the start of the general, data sequencer
 * and shading cluster heaps; on failure nothing of them stays mapped.
 *
 * The pages are written by the processor through uncached views, so the
 * core reaches them around its system level cache.  Only the area's own
 * bytes are mapped, so the next page of each heap stays free for the
 * session's allocations.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/winsys/pvr_winsys_helper.c
 * (pvr_winsys_helper_allocate_static_memory: one buffer for each of the
 * three heaps, mapped at the heap's carve-out address for the
 * carve-out's size).
 */
static int
map_fixed_area(
	struct powervr_session *session)
{
	struct powervr_buffer *general;
	struct powervr_buffer *pds;
	struct powervr_buffer *usc;
	unsigned flags;
	int error;

	/* The pages powervr_session_fixed_make() made, and the attributes of pages the processor writes. */
	general = session->fixed[POWERVR_FIXED_HEAP_GENERAL];
	pds = session->fixed[POWERVR_FIXED_HEAP_PDS];
	usc = session->fixed[POWERVR_FIXED_HEAP_USC];
	flags = powervr_gpu_buffer_flags(1, 0);

	/* The general heap's page: the fence and the colour coefficients. */
	error = powervr_gpu_mmu_map(&session->mmu, POWERVR_GENERAL_HEAP_BASE, general->run.paddr, POWERVR_FIXED_GENERAL_BYTES, flags);
	if (error != 0)
		return error;

	/* The data sequencer heap's page: the synchronisation program and the end-of-tile code. */
	error = powervr_gpu_mmu_map(&session->mmu, POWERVR_PDS_HEAP_BASE, pds->run.paddr, POWERVR_FIXED_PDS_BYTES, flags);
	if (error != 0) {
		powervr_gpu_mmu_unmap(&session->mmu, POWERVR_GENERAL_HEAP_BASE, POWERVR_FIXED_GENERAL_BYTES);
		return error;
	}

	/* The shading cluster heap's page: the synchronisation task's program. */
	error = powervr_gpu_mmu_map(&session->mmu, POWERVR_USC_HEAP_BASE, usc->run.paddr, POWERVR_FIXED_USC_BYTES, flags);
	if (error != 0) {
		powervr_gpu_mmu_unmap(&session->mmu, POWERVR_PDS_HEAP_BASE, POWERVR_FIXED_PDS_BYTES);
		powervr_gpu_mmu_unmap(&session->mmu, POWERVR_GENERAL_HEAP_BASE, POWERVR_FIXED_GENERAL_BYTES);
		return error;
	}

	/* Succeeded: the core finds the area at each heap's start. */
	return 0;
}

/*
 * Hands the fixed area's pages of a kept session to the node's kept
 * buffers, which the reset frees once nothing reaches them; the core's
 * lock may be held, so the buffer guard is not taken here.
 */
static void
keep_fixed_area(
	struct powervr_session *session)
{
	struct powervr_render *render;
	unsigned long enabled;
	unsigned heap;

	/* Links each page that was made, its only reference the session's. */
	render = session->render;
	enabled = spin_lock_irqsave(&render->queue_lock);

	for (heap = 0; heap < POWERVR_FIXED_HEAPS; heap++) {
		if (session->fixed[heap] != NULL) {
			session->fixed[heap]->next = render->kept_buffers;
			render->kept_buffers = session->fixed[heap];
			session->fixed[heap] = NULL;
		}
	}

	spin_unlock_irqrestore(&render->queue_lock, enabled);
}

/* Makes the session's firmware records, each uncached on the firmware's side. */
static int
make_records(
	struct powervr_render *render,
	struct powervr_session *session)
{
	struct powervr_heap *heap;
	int error;

	/* The memory context, naming the session's catalogue. */
	heap = &render->core->firmware.heap;
	error = powervr_object_create(heap, POWERVR_MEMORY_CONTEXT_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->memory_context);
	if (error != 0)
		return error;

	/* The compute context's state. */
	error = powervr_object_create(heap, POWERVR_COMPUTE_STATE_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->context_state);
	if (error != 0)
		return error;

	/* The command buffer's control and the buffer. */
	error = powervr_object_create(heap, POWERVR_CLIENT_CONTROL_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->compute.control);
	if (error != 0)
		return error;
	error = powervr_object_create(heap, POWERVR_CLIENT_CCB_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->compute.buffer);
	if (error != 0)
		return error;

	/* The timeline word the firmware advances. */
	error = powervr_object_create(heap, POWERVR_SYNC_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->compute.timeline);
	if (error != 0)
		return error;

	/* The compute context itself. */
	error = powervr_object_create(heap, POWERVR_COMPUTE_CONTEXT_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &session->context);
	if (error != 0)
		return error;

	/* Succeeded: every record exists, zeroed. */
	return 0;
}

/* Returns the session's firmware records; the core's lock is held, and a record never made is skipped. */
static void
destroy_records(
	struct powervr_render *render,
	struct powervr_session *session)
{
	struct powervr_heap *heap;

	/* Each record, unmapped from the firmware's processor, in the reverse of their making. */
	heap = &render->core->firmware.heap;
	powervr_object_destroy(heap, &session->context);
	powervr_object_destroy(heap, &session->compute.timeline);
	powervr_object_destroy(heap, &session->compute.buffer);
	powervr_object_destroy(heap, &session->compute.control);
	powervr_object_destroy(heap, &session->context_state);
	powervr_object_destroy(heap, &session->memory_context);
}

/*
 * Fills the session's firmware records.
 *
 * Procedure referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/imagination/pvr_vm.c (fw_mem_context_init: the
 * catalogue's address and no register set); pvr_cccb.c (cccb_ctrl_init:
 * both offsets and the dependency offset zero, the wrap mask);
 * pvr_queue.c (init_fw_context: the buffer and its control, the data
 * master, the priority, the deadline, the host's identity, the memory
 * context and the state).
 */
static void
fill_records(
	struct powervr_session *session)
{
	struct powervr_object *context;

	/* The memory context: the catalogue, and no fixed register set. */
	powervr_object_put64(&session->memory_context, POWERVR_MEMORY_CONTEXT_ROOT, powervr_gpu_mmu_root(&session->mmu));
	powervr_object_put32(&session->memory_context, POWERVR_MEMORY_CONTEXT_SET, POWERVR_NO_REGISTER_SET);

	/* The command buffer's control: empty, with its wrap mask. */
	powervr_object_put32(&session->compute.control, POWERVR_CLIENT_WRITE, 0);
	powervr_object_put32(&session->compute.control, POWERVR_CLIENT_READ, 0);
	powervr_object_put32(&session->compute.control, POWERVR_CLIENT_DEPENDENCY, 0);
	powervr_object_put32(&session->compute.control, POWERVR_CLIENT_WRAP_MASK, POWERVR_CLIENT_CCB_BYTES - 1U);
	session->compute.write = 0;

	/* The compute context's common part; the rest stays zero. */
	context = &session->context;
	powervr_object_put32(context, POWERVR_CONTEXT_CCB_CONTROL, powervr_object_address(&session->compute.control, 0));
	powervr_object_put32(context, POWERVR_CONTEXT_CCB, powervr_object_address(&session->compute.buffer, 0));
	powervr_object_put32(context, POWERVR_CONTEXT_STATE, powervr_object_address(&session->context_state, 0));
	powervr_object_put32(context, POWERVR_CONTEXT_PRIORITY, 0);
	powervr_object_put32(context, POWERVR_CONTEXT_DATA_MASTER, POWERVR_DATA_MASTER_COMPUTE);
	powervr_object_put32(context, POWERVR_CONTEXT_DEADLINE, POWERVR_DEADLINE_MS);
	powervr_object_put32(context, POWERVR_CONTEXT_MEMORY, powervr_object_address(&session->memory_context, 0));
	powervr_object_put32(context, POWERVR_CONTEXT_IDENTITY, session->identity);
}

/*
 * Checks the session's command buffer has room for bytes more, padding to
 * its start when they would not fit before its end; EBUSY when the
 * firmware has not read far enough yet.
 */
static int
client_space(
	const struct powervr_client *client,
	uint32_t bytes)
{
	uint32_t read;
	uint32_t write;
	uint32_t remaining;
	uint32_t required;
	uint32_t free_bytes;

	/* What is left before the end; a sequence that does not fit takes the rest for padding. */
	write = client->write;
	read = powervr_object_get32(&client->control, POWERVR_CLIENT_READ);
	remaining = POWERVR_CLIENT_CCB_BYTES - write;
	required = bytes;
	if (remaining < bytes + POWERVR_HEADER_BYTES)
		required = bytes + remaining;

	/* The free bytes between the host's write offset and the firmware's read offset. */
	free_bytes = (read - write + POWERVR_CLIENT_CCB_BYTES - 1U) & (POWERVR_CLIENT_CCB_BYTES - 1U);
	if (free_bytes < required)
		return EBUSY;

	/* Succeeded: the sequence fits. */
	return 0;
}

/*
 * Writes one command behind its header at the host's write offset,
 * padding to the start first when it would not fit before the end.  The
 * caller checked the room.
 */
static void
client_put(
	struct powervr_client *client,
	uint32_t type,
	uint32_t size,
	const uint32_t *data,
	uint32_t words)
{
	uint32_t remaining;
	uint32_t offset;
	uint32_t index;

	/* Pads the rest of the buffer when the command and a padding header would not fit. */
	remaining = POWERVR_CLIENT_CCB_BYTES - client->write;
	if (remaining < size + POWERVR_HEADER_BYTES + POWERVR_HEADER_BYTES) {
		for (index = 0; index < POWERVR_HEADER_BYTES; index += 4U)
			powervr_object_put32(&client->buffer, client->write + index, 0);
		powervr_object_put32(&client->buffer, client->write + POWERVR_HEADER_TYPE, POWERVR_CLIENT_PADDING);
		powervr_object_put32(&client->buffer, client->write + POWERVR_HEADER_SIZE, remaining - POWERVR_HEADER_BYTES);
		client->write = 0;
	}

	/* The header: the type and the size, the rest zero. */
	offset = client->write;
	for (index = 0; index < POWERVR_HEADER_BYTES; index += 4U)
		powervr_object_put32(&client->buffer, offset + index, 0);
	powervr_object_put32(&client->buffer, offset + POWERVR_HEADER_TYPE, type);
	powervr_object_put32(&client->buffer, offset + POWERVR_HEADER_SIZE, (size + 7U) & ~7U);

	/* The command's words behind it. */
	for (index = 0; index < words; index++)
		powervr_object_put32(&client->buffer, offset + POWERVR_HEADER_BYTES + index * 4U, data[index]);

	/* Moves the host's write offset past it. */
	client->write = (offset + POWERVR_HEADER_BYTES + ((size + 7U) & ~7U)) & (POWERVR_CLIENT_CCB_BYTES - 1U);
}

/* Sends a kick naming the session's compute context and the host's write offset. */
static int
send_kick(
	struct powervr_render *render,
	struct powervr_session *session)
{
	uint32_t command[POWERVR_COMMAND_WORDS];
	uint32_t index;
	int error;

	/* The kick: the context, the new write offset, the wrap mask, no cleanup. */
	for (index = 0; index < POWERVR_COMMAND_WORDS; index++)
		command[index] = 0;
	command[POWERVR_COMMAND_TYPE / 4U] = POWERVR_COMMAND_KICK;
	command[(POWERVR_COMMAND_DATA + POWERVR_KICK_CONTEXT) / 4U] = powervr_object_address(&session->context, 0);
	command[(POWERVR_COMMAND_DATA + POWERVR_KICK_WRITE_OFFSET) / 4U] = session->compute.write;
	command[(POWERVR_COMMAND_DATA + POWERVR_KICK_WRAP_MASK) / 4U] = POWERVR_CLIENT_CCB_BYTES - 1U;
	command[(POWERVR_COMMAND_DATA + POWERVR_KICK_CLEANUPS) / 4U] = 0;

	/* Sends it after the commands it names. */
	error = send_command(render, command);
	if (error != 0)
		return error;

	/* Succeeded: the firmware will look at the context. */
	return 0;
}

/* Sends a kick after a barrier that orders the commands it names before it, waiting while the kernel's buffer is full. */
static int
send_command(
	struct powervr_render *render,
	const uint32_t command[POWERVR_COMMAND_WORDS])
{
	uint32_t slot;
	int error;

	/* Orders the commands in the context's buffer before the kick that names them. */
	kern_io_barrier();

	/* Sends it, waiting while the kernel's buffer is full. */
	error = send_waiting(render->core, command, &slot);
	if (error != 0)
		return error;

	/* Succeeded: the command is sent. */
	return 0;
}

/* Sends a command, trying again once a tick while the kernel's buffer is full, bounded. */
static int
send_waiting(
	struct powervr_core *core,
	const uint32_t command[POWERVR_COMMAND_WORDS],
	uint32_t *slot)
{
	uint64_t deadline;
	uint64_t now;
	int error;

	/* Tries until the buffer takes it or the bound is spent. */
	deadline = sched_ticks() + ms_ticks(KICK_WAIT_MS);
	for (;;) {
		error = powervr_command_send(core, command, slot);
		if (error != EBUSY)
			break;

		/* The bound is spent with the buffer still full. */
		now = sched_ticks();
		if (now >= deadline)
			return ETIMEDOUT;

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* The firmware's buffer refused it for another reason. */
	if (error != 0)
		return error;

	/* Succeeded: the command is sent. */
	return 0;
}

/* Builds the cleanup command for a common context named by its firmware address. */
static void
cleanup_command(
	uint32_t context,
	uint32_t command[POWERVR_COMMAND_WORDS])
{
	uint32_t index;

	/* The command: a common context, named by its firmware address. */
	for (index = 0; index < POWERVR_COMMAND_WORDS; index++)
		command[index] = 0;
	command[POWERVR_COMMAND_TYPE / 4U] = POWERVR_COMMAND_CLEANUP;
	command[(POWERVR_COMMAND_DATA + POWERVR_CLEANUP_TYPE) / 4U] = POWERVR_CLEANUP_COMMON_CONTEXT;
	command[(POWERVR_COMMAND_DATA + POWERVR_CLEANUP_CONTEXT) / 4U] = context;
}

/*
 * The service: sleeps until the firmware's interrupt, a submission or its
 * timeout wakes it, then retires what the firmware finished and reads
 * the firmware's notices, until it is told to end.
 */
static void
service_thread(
	void *argument)
{
	struct powervr_render *render;
	unsigned long enabled;
	uint64_t observed;
	uint64_t deadline;
	int busy;

	/* The node the service runs for. */
	render = argument;
	busy = 0;

	/* Sleeps, then makes one pass, until told to end. */
	for (;;) {
		enabled = spin_lock_irqsave(&render->queue_lock);

		/* Told to end: says so and leaves. */
		if (!render->running) {
			render->service_done = 1;
			waitq_wake_all(&render->retired);
			spin_unlock_irqrestore(&render->queue_lock, enabled);
			return;
		}

		/* Sleeps a tick while work is pending, longer otherwise. */
		observed = waitq_sequence(&render->service);
		deadline = sched_ticks() + 1U;
		if (!busy)
			deadline = sched_ticks() + ms_ticks(SERVICE_IDLE_MS);
		(void)waitq_sleep(&render->service, &render->queue_lock, observed, deadline, 0);

		spin_unlock_irqrestore(&render->queue_lock, enabled);

		/* One pass over the firmware's notices and the sessions' timelines. */
		busy = service_pass(render, sched_ticks());
	}
}

/*
 * One pass: reads the firmware's notices, retires every command whose
 * timeline value was reached, and asks the firmware whether it is alive
 * when a command waited past its deadline.  Reports whether work is
 * still pending.
 */
static int
service_pass(
	struct powervr_render *render,
	uint64_t now)
{
	struct powervr_session *session;
	struct notice_outcome outcome;
	struct delivery delivery;
	uint32_t command[POWERVR_COMMAND_WORDS];
	unsigned long enabled;
	uint32_t answer;
	uint32_t index;
	unsigned faulted;
	int overdue;
	int pending;
	int error;

	/* A lost firmware is not read any more. */
	outcome.render = render;
	outcome.device_lost = 0;
	overdue = 0;
	pending = 0;
	mutex_lock(&render->core->lock);

	if (render->lost) {
		mutex_unlock(&render->core->lock);
		return 0;
	}

	/* The notices, which may mark sessions or the device lost. */
	(void)powervr_notices_take(render->core, handle_notice, &outcome);

	/* Each session's retired commands, a bounded number per pass; and whether one is overdue. */
	delivery.count = 0;
	delivery.markers_freed = 0;
	for (session = render->sessions; session != NULL; session = session->next) {
		enabled = spin_lock_irqsave(&render->queue_lock);

		take_reached(session, &delivery);
		if (session->pending_count != 0U) {
			pending = 1;
			if (now - session->pending[session->pending_head].submitted > ms_ticks(WATCHDOG_MS))
				overdue = 1;
		}

		spin_unlock_irqrestore(&render->queue_lock, enabled);
	}

	/* A command overdue: asks the firmware whether it is alive. */
	error = 0;
	faulted = is_faulted(render);
	if (overdue && !faulted) {
		for (index = 0; index < POWERVR_COMMAND_WORDS; index++)
			command[index] = 0;
		command[POWERVR_COMMAND_TYPE / 4U] = POWERVR_COMMAND_HEALTH_CHECK;
		error = powervr_command_run(render->core, command, POWERVR_COMMAND_WAIT_MS, &answer);
	}

	mutex_unlock(&render->core->lock);

	/* Delivers what retired. */
	deliver(render, &delivery);

	/*
	 * The firmware stopped answering, or asked for a restart or reported a
	 * fault of its own: the device goes into quarantine before the core
	 * hears of the loss.
	 */
	if (error != 0 || outcome.device_lost) {
		if (error == 0)
			error = EIO;
		powervr_render_quarantine(render, error);
		drv_gpu_report_error(render->device, error);
	}

	/* Succeeded: whether work is still pending. */
	return pending;
}

/* Takes, under the queue lock, the session's commands whose timeline value was reached. */
static void
take_reached(
	struct powervr_session *session,
	struct delivery *delivery)
{
	struct powervr_pending *entry;
	uint32_t current;

	/* Reads the timeline once, then retires in order up to it, each with its own error. */
	current = powervr_session_timeline(session);
	while (session->pending_count != 0U && delivery->count < DELIVER_MAX) {
		entry = &session->pending[session->pending_head];
		if ((int32_t)(current - entry->value) < 0)
			break;
		take_entry(session, entry->error, delivery);
	}
}

/* Takes, under the queue lock, every pending command of the session, to be ended with an error. */
static void
take_all(
	struct powervr_session *session,
	int error,
	struct delivery *delivery)
{
	/* Every one, in order, as far as one delivery holds. */
	while (session->pending_count != 0U && delivery->count < DELIVER_MAX)
		take_entry(session, error, delivery);
}

/*
 * Takes, under the queue lock, the session's first pending command into
 * the delivery with an error, and frees the job marker it names.
 */
static void
take_entry(
	struct powervr_session *session,
	int error,
	struct delivery *delivery)
{
	struct powervr_pending *entry;

	/* The callback and its error. */
	entry = &session->pending[session->pending_head];
	delivery->completion[delivery->count] = entry->completion;
	delivery->error[delivery->count] = error;
	delivery->session[delivery->count] = session;
	delivery->count++;

	/* A committed marker is free again; its capacity is announced after the delivery. */
	if (entry->marker != POWERVR_NO_MARKER) {
		session->markers[entry->marker].state = POWERVR_MARKER_FREE;
		session->markers[entry->marker].completion = NULL;
		delivery->markers_freed++;
	}

	/* The entry leaves the ring. */
	session->pending_head = (session->pending_head + 1U) % POWERVR_RENDER_PENDING;
	session->pending_count--;
}

/*
 * Delivers the callbacks taken, then counts each off its session and wakes
 * a drain.  A session lives until its last callback is counted off.
 */
static void
deliver(
	struct powervr_render *render,
	struct delivery *delivery)
{
	unsigned long enabled;
	unsigned index;

	/* Nothing taken. */
	if (delivery->count == 0U) {
		delivery->markers_freed = 0;
		return;
	}

	/* Each callback, outside every lock. */
	for (index = 0; index < delivery->count; index++)
		drv_gpu_complete(delivery->completion[index], delivery->error[index]);

	/* Counts them off and wakes a drain. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	for (index = 0; index < delivery->count; index++)
		delivery->session[index]->outstanding--;
	waitq_wake_all(&render->retired);

	spin_unlock_irqrestore(&render->queue_lock, enabled);
	delivery->count = 0;

	/* Freed markers are capacity a waiting producer may take. */
	if (delivery->markers_freed != 0U && render->device != NULL)
		drv_gpu_capacity_changed(render->device);
	delivery->markers_freed = 0;
}

/*
 * Acts on one notice of the firmware; the core's lock is held.
 *
 * A context reset prints the stage its context's master names and loses
 * that session, or every session when the firmware says it reset them
 * all; a page fault of the firmware's own or a request to restart loses
 * the device; a free list's request is answered.
 */
static void
handle_notice(
	void *context,
	uint32_t type,
	const struct powervr_object *buffer,
	uint32_t offset)
{
	struct notice_outcome *outcome;
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_reset_notice notice;
	struct powervr_reset_stage stage;
	enum powervr_context_kind kind;
	uint8_t data[POWERVR_RESET_BYTES];
	unsigned long enabled;
	uint32_t index;
	uint32_t word;
	int named;
	int all;

	/* The node the pass runs for. */
	outcome = context;
	render = outcome->render;

	/* The firmware lost the device. */
	if (type == POWERVR_NOTICE_FIRMWARE_FAULT || type == POWERVR_NOTICE_RESTART) {
		outcome->device_lost = 1;
		return;
	}

	/* Other notices go to the render targets, which answer the free lists' requests and keep their statistics. */
	if (type != POWERVR_NOTICE_CONTEXT_RESET) {
		powervr_targets_notice(render, type, buffer, offset);
		return;
	}

	/* A context the firmware reset: its stage line, then its session (or every session) lost. */
	for (index = 0; index < POWERVR_RESET_BYTES; index += 4U) {
		word = powervr_object_get32(buffer, offset + POWERVR_COMMAND_DATA + index);
		data[index + 0U] = (uint8_t)word;
		data[index + 1U] = (uint8_t)(word >> 8);
		data[index + 2U] = (uint8_t)(word >> 16);
		data[index + 3U] = (uint8_t)(word >> 24);
	}
	powervr_reset_notice_read(data, &notice);
	all = (notice.flags & POWERVR_RESET_FLAG_ALL) != 0U;
	for (session = render->sessions; session != NULL; session = session->next) {
		/* Which of the session's contexts the notice names, if any. */
		named = 0;
		kind = POWERVR_CONTEXT_COMPUTE;
		if (session->identity == notice.context) {
			named = 1;
		} else if (session->render_context != NULL && session->render_context->identity == notice.context) {
			named = 1;
			kind = POWERVR_CONTEXT_RENDER;
		} else if (session->transfer_context != NULL && session->transfer_context->identity == notice.context) {
			named = 1;
			kind = POWERVR_CONTEXT_TRANSFER;
		}
		if (!all && !named)
			continue;

		/* The stage the context's kind and master name, with the fault's address or the reset's reason. */
		if (named) {
			powervr_reset_stage_of(&notice, kind, &stage);
			powervr_stage_fail_value(stage.stage, stage.reason, stage.value);
		}

		/* No new kick for it; the core hears of the loss. */
		enabled = spin_lock_irqsave(&render->queue_lock);

		session->lost = 1;

		spin_unlock_irqrestore(&render->queue_lock, enabled);
		drv_gpu_report_session_error(render->device, session, EIO);
		if (!all)
			break;
	}
}

/*
 * Refuses a kick (EIO) on a device in quarantine, or for a session that
 * is stopping, lost to a reset, isolated or abandoned after a timeout:
 * the firmware may still run its earlier work, and a new kick would
 * queue behind it or reach a firmware that no longer answers.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_queue.c
 * (pvr_queue_run_job and pvr_context's faulty entity: no job of a
 * faulty context reaches the firmware).
 */
static int
kick_refused(
	struct powervr_render *render,
	struct powervr_session *session)
{
	unsigned long enabled;
	int refused;

	/* The flags, read under the queue lock. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	refused = 0;
	if (render->faulted || session->stopping || session->lost || session->isolated)
		refused = EIO;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* An abandoned session's earlier work may still run. */
	if (session->abandoned)
		refused = EIO;

	/* Refused, or not. */
	if (refused != 0)
		return refused;

	/* Succeeded: the kick may go out. */
	return 0;
}

/* Counts the bytes a run of commands takes in a context's buffer, each behind its header. */
static uint32_t
commands_bytes(
	const struct powervr_client_command *commands,
	unsigned count)
{
	uint32_t bytes;
	unsigned index;

	/* Each command's header and its size aligned to 8. */
	bytes = 0;
	for (index = 0; index < count; index++)
		bytes += POWERVR_HEADER_BYTES + ((commands[index].size + 7U) & ~7U);

	/* Succeeded: the bytes. */
	return bytes;
}

/* Writes a run of commands into a context's buffer; the caller checked the room. */
static void
commands_put(
	struct powervr_client *client,
	const struct powervr_client_command *commands,
	unsigned count)
{
	uint32_t words[RENDER_COMMAND_WORDS];
	unsigned index;
	uint32_t length;

	/* Each command's bytes as words, behind its header. */
	for (index = 0; index < count; index++) {
		length = commands[index].size / 4U;
		kern_memcpy(words, commands[index].data, (size_t)length * 4U);
		client_put(client, commands[index].type, commands[index].size, words, length);
	}
}

/* Copies what the firmware reported of the free lists into a render's marks; the core's lock is held. */
static void
list_marks(
	const struct powervr_render *render,
	struct powervr_list_marks *marks)
{
	struct powervr_target_events events;

	/* The three counts a render compares. */
	powervr_render_target_events(render, &events);
	marks->grow_requests = events.grow_requests;
	marks->partial_renders = events.partial_renders;
	marks->out_of_memory = events.out_of_memory;
}

/* Converts milliseconds to scheduler ticks, rounding up. */
static uint64_t
ms_ticks(
	unsigned milliseconds)
{
	/* Rounds up, so a short span is still a tick. */
	return ((uint64_t)milliseconds * KERN_CLOCK_HZ + 999U) / 1000U;
}

/* Reads whether the device is in quarantine. */
static unsigned
is_faulted(
	struct powervr_render *render)
{
	unsigned long enabled;
	unsigned faulted;

	/* One read under the queue lock. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	faulted = render->faulted;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* Succeeded: the flag. */
	return faulted;
}
