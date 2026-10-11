/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Logical callbacks, whole pending graph ownership and checked native DMA retirement are separate boundaries. */
#include <kern/clock.h>
#include <kern/kmem.h>
#include <kern/klog.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/gpu/bcm2711/vulkan-native-job.h"
#include "drivers/gpu/bcm2711/vulkan-barrier.h"
#include "drivers/gpu/bcm2711/vulkan-native-image.h"
#include "drivers/gpu/bcm2711/vulkan-buffer-copy.h"

/* All concurrently resident native pass/draw uploads share this submission-wide padded budget. */
#define NATIVE_JOB_BYTES (256ULL * 1024U * 1024U)

static int release_job(struct bcm2711_vulkan_native_job *job);
static int retire_closed_sessions(struct bcm2711_render_device *controller);
static void report_perf(struct bcm2711_v3d_perf *perf);
static uint64_t average_us(uint64_t ticks, uint64_t count);

/*
 * Prepares an independently pending primary graph before any native allocation or worker acceptance.
 *
 * The renderer owns its exact Vulkan namespace, so uncertain work can keep
 * that session alive after common close.  Submit-time preparation copies
 * descriptor identity/state only; coherent UBO, vertex and texture bytes are
 * read later at FIFO execution after preceding native writes retire.
 */
int
bcm2711_vulkan_native_job_create(
	struct bcm2711_vulkan_object *command,
	struct bcm2711_vulkan_native_job **job)
{
	struct bcm2711_vulkan_native_job *created;
	struct bcm2711_render_session *session;
	int error;

	/* No partial pending graph escapes an ordinary allocation refusal. */
	if (job == NULL)
		return EINVAL;
	*job = NULL;

	/* A typed primary requires an exact renderer-owned non-closing Vulkan session. */
	if (command == NULL ||
	    command->session == NULL ||
	    command->kind != I915_VK_OBJ_COMMAND_BUFFER)
		return EINVAL;
	session = command->session->render;
	if (session == NULL ||
	    session->device == NULL ||
	    session->vulkan != command->session ||
	    command->session->closing)
		return EINVAL;

	/* The CPU payload root exists before the primary graph acquires its pending counts and cloned descriptor holds. */
	created = kern_calloc(1, sizeof(*created));
	if (created == NULL)
		return ENOMEM;
	created->session = session;
	created->retired = true;
	error = bcm2711_vulkan_prepared_create(command, &created->prepared);
	if (error != 0) {
		kern_free(created);
		return error;
	}

	/* Succeeded: worker acceptance or no-launch disposal owns this complete independently pending primary payload. */
	*job = created;
	return 0;
}

/*
 * Executes one pending primary through complete sequential native passes under the controller mutex.
 *
 * Each following pass stages its coherent source bytes only after the
 * previous pass's actual native completion/output visibility.  A failed
 * launched pass keeps all native inputs and the whole pending primary in
 * this root.  Later callback disposal transfers it to controller quarantine
 * rather than dropping a last CPU pointer to still referenced DMA storage.
 */
int
bcm2711_vulkan_native_job_execute(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_session *session,
	void *payload,
	bool *retired)
{
	struct bcm2711_vulkan_native_job *job;
	const struct bcm2711_vulkan_prepared_event *event;
	const struct bcm2711_vulkan_prepared_event *next;
	struct bcm2711_v3d_job_result completion;
	uint64_t remaining;
	uint64_t bytes;
	uint64_t started;
	uint64_t step;
	uint64_t prepare_ticks;
	uint32_t tiles;
	uint64_t execute_ticks;
	uint64_t release_ticks;
	uint64_t copy_ticks;
	uint64_t elapsed;
	struct bcm2711_v3d_perf *perf;
	uint32_t region;
	uint32_t index;
	int tiled;
	int error;
	int synchronized;

	/* Initial refusals launch no work and therefore retain no device borrow. */
	if (retired == NULL)
		return EINVAL;
	*retired = true;
	job = payload;

	/* The accepted payload must belong to the same exact renderer namespace and native controller. */
	if (controller == NULL ||
	    session == NULL ||
	    job == NULL ||
	    job->prepared == NULL ||
	    job->session != session ||
	    session->device != controller ||
	    controller->space.native == NULL)
		return EINVAL;

	/* A completed or uncertain payload never replays coherent source reads, clear or native work. */
	if (job->executed || job->quarantined) {
		*retired = job->retired;
		return EBUSY;
	}

	/* A stopped controller refuses both native launches and CPU layout publication before the primary becomes single-use work. */
	if (!controller->space.native->hardware.initialized ||
	    !controller->space.native->hardware.ready ||
	    !controller->space.native->power.ready)
		return EIO;

	/* One finite resident upload budget covers every pass and its complete independent draw input prefix. */
	job->executed = true;
	started = clock_ticks();
	perf = &controller->space.native->hardware.perf;
	prepare_ticks = 0;
	execute_ticks = 0;
	release_ticks = 0;
	copy_ticks = 0;
	remaining = NATIVE_JOB_BYTES;
	region = 0;
	event = job->prepared->first;
	while (event != NULL) {
		/* A whole-image upload or readback becomes a tile copy pass; ENOTSUP leaves it to the CPU. */
		tiled = ENOTSUP;
		if (event->opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE || event->opcode == GPU_OP_CMD_COPY_IMAGE_TO_BUFFER) {
			step = clock_ticks();
			error = bcm2711_v3d_memory_begin(&controller->space);
			if (error != 0)
				return error;
			tiled = bcm2711_vulkan_native_buffer_image_create(&controller->space, event->record, &remaining, &job->pass);
			synchronized = bcm2711_v3d_memory_end(&controller->space);
			prepare_ticks += clock_ticks() - step;
			if (tiled == 0 && synchronized != 0)
				tiled = synchronized;
			if (tiled != 0 && tiled != ENOTSUP)
				return tiled;
		}

		/* Exact coherent buffer transfers execute only after every preceding native pass has retired and published its output. */
		if (tiled == ENOTSUP &&
		    (event->opcode == GPU_OP_CMD_COPY_BUFFER ||
		     event->opcode == GPU_OP_CMD_COPY_BUFFER_TO_IMAGE ||
		     event->opcode == GPU_OP_CMD_COPY_IMAGE_TO_BUFFER)) {
			step = clock_ticks();
			error = bcm2711_vulkan_buffer_copy_run(event->record);
			copy_ticks += clock_ticks() - step;
			if (error != 0)
				return error;
			event = event->next;
			continue;
		}

		/* Every earlier native pass completed before this explicit dependency can publish FIFO-visible layout state. */
		if (event->opcode == GPU_OP_CMD_PIPELINE_BARRIER) {
			error = bcm2711_vulkan_barrier_run(event->record);
			if (error != 0)
				return error;
			event = event->next;
			continue;
		}

		/* Measures preparation while deferring input translations until one checked publication. */
		step = clock_ticks();
		error = bcm2711_v3d_memory_begin(&controller->space);
		if (error != 0)
			return error;

		/* Graphics, clears and copied image regions acquire complete independent native owners. */
		if (tiled == 0) {
			error = 0;
			next = event->next;
		} else if (event->opcode == GPU_OP_CMD_BEGIN_RENDER_PASS) {
			error = bcm2711_vulkan_native_pass_create(&controller->space, event, &remaining, &job->pass, &next);
		} else if (event->opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
			error = bcm2711_vulkan_native_clear_create(&controller->space, event, &remaining, &job->pass);
			next = event->next;
		} else if (event->opcode == GPU_OP_CMD_COPY_IMAGE || event->opcode == GPU_OP_CMD_BLIT_IMAGE) {
			error = bcm2711_vulkan_native_image_create(&controller->space, (const struct bcm2711_vulkan_transfer *)event->record, region, &remaining, &job->pass);
			region++;
			next = event;
			if (region == event->record->count) {
				next = event->next;
				region = 0;
			}
		} else {
			error = ENOTSUP;
		}

		/* Even a rolled-back preparation must flush its edits before any retired VA or RAM is reused. */
		synchronized = bcm2711_v3d_memory_end(&controller->space);
		if (synchronized != 0)
			error = synchronized;

		/* Accumulate preparation separately from device execution and retirement. */
		prepare_ticks += clock_ticks() - step;

		/* A copy whose destination already holds its source's bytes prepared no pass. */
		if (error == 0 && job->pass == NULL) {
			perf->skipped_copies++;
			event = next;
			continue;
		}

		/* The cost report separates graphics passes, image clears and image transfers. */
		if (error == 0) {
			bcm2711_vulkan_native_pass_costs(job->pass, perf);
			tiles = (job->pass->state.last_x - job->pass->state.first_x + 1U) *
			    (job->pass->state.last_y - job->pass->state.first_y + 1U);
			if (event->opcode == GPU_OP_CMD_BEGIN_RENDER_PASS) {
				perf->render_passes++;
				perf->render_tiles += tiles;
			} else if (event->opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
				perf->clear_passes++;
			} else {
				perf->image_passes++;
				perf->image_tiles += tiles;
			}
		}

		/* A refused whole-pass preparation owns no launched DMA and leaves the primary pending until disposal. */
		if (error != 0) {
			bcm2711_stage_mark(
				BCM2711_FAMILY_V3D,
				"submit prepare opcode=%u error=%d",
				event->opcode,
				error);
			return error;
		}

		/* Exact clear, supervised bin/render and GPU cache visibility execute while every independent owner is held. */
		step = clock_ticks();
		error = bcm2711_vulkan_native_pass_run(job->pass, &completion);
		execute_ticks += clock_ticks() - step;
		perf->tfu_ticks += completion.tfu_ticks;
		perf->bin_ticks += completion.bin_ticks;
		perf->render_ticks += completion.render_ticks;
		perf->output_clean_ticks += completion.clean_ticks;
		if (completion.counters[0] != 0U) {
			for (index = 0; index < BCM2711_V3D_COUNTERS; index++)
				perf->counters[index] += completion.counters[index];
			perf->counter_runs++;
		}
		job->retired = completion.retired;
		*retired = completion.retired;
		if (error != 0) {
			bcm2711_stage_mark(
				BCM2711_FAMILY_V3D,
				"submit execute opcode=%u draws=%u size=%ux%u error=%d retired=%u",
				event->opcode,
				job->pass->draws,
				job->pass->state.width,
				job->pass->state.height,
				error,
				(unsigned)completion.retired);
			return error;
		}

		/* Confirmed DMA retirement permits this resident pass budget and its mappings to be reused by the next FIFO pass. */
		bytes = job->pass->bytes;
		step = clock_ticks();
		error = bcm2711_v3d_memory_begin(&controller->space);
		if (error != 0)
			return error;

		/* Consumes all stopped pass roots while their physical allocations remain batch-owned. */
		error = bcm2711_vulkan_native_pass_release(&job->pass, true);

		/* Physical backing stays owned until the complete batch of PTE withdrawals is flushed. */
		synchronized = bcm2711_v3d_memory_end(&controller->space);
		if (synchronized != 0)
			error = synchronized;
		release_ticks += clock_ticks() - step;
		if (error != 0)
			return error;
		remaining += bytes;
		event = next;
	}

	/* Adds this completed submission to the periodic cost report. */
	elapsed = clock_ticks() - started;
	perf->submissions++;
	perf->total_ticks += elapsed;
	if (elapsed > perf->max_ticks)
		perf->max_ticks = elapsed;
	perf->prepare_ticks += prepare_ticks;
	perf->execute_ticks += execute_ticks;
	perf->release_ticks += release_ticks;
	perf->transfer_ticks += copy_ticks;
	report_perf(perf);

	/* Succeeded: the pending primary remains owned until worker disposal, after actual native completion of every pass. */
	return 0;
}

/*
 * Disposes a completed payload or transfers a whole uncertain payload to persistent controller ownership.
 *
 * Worker callback retirement always follows this operation.  False retirement
 * therefore keeps the pending primary, all descriptor charges, current pass
 * mappings, output and session alive in the controller list with no allocation.
 */
int
bcm2711_vulkan_native_job_dispose(
	struct bcm2711_render_device *controller,
	void *payload,
	bool retired)
{
	struct bcm2711_vulkan_native_job *job;
	int error;

	/* Only one exact renderer/controller can consume or persist this prepared root. */
	job = payload;
	if (controller == NULL ||
	    job == NULL ||
	    job->session == NULL ||
	    job->session->device != controller)
		return EINVAL;

	/* Uncertain native DMA transfers the existing whole root without a fallible allocation or loss of its pending owner graph. */
	if (!retired) {
		/* A repeated false disposal must not duplicate the same root in the persistent list. */
		if (job->quarantined)
			return 0;
		job->quarantined = true;
		job->retired = false;
		job->next = controller->quarantine;
		controller->quarantine = job;
		return 0;
	}

	/* A controller-owned quarantine entry may retire only through the explicit global-reset recovery traversal. */
	if (job->quarantined)
		return EBUSY;

	/* Whole native owners retire before the pending primary releases descriptor charges and typed dependencies. */
	error = release_job(job);
	if (error != 0)
		return error;

	/* Succeeded: no worker callback can lose an uncertain whole payload or its session lifetime. */
	return 0;
}

/*
 * Retires all persistent native payloads and closed sessions after checked global reset stops DMA.
 *
 * External sessions must already have closed.  Logical owner retirement is
 * completed even after a mapping teardown refusal; the address space keeps
 * failed final translations/physical allocations for the next checked reset.
 */
int
bcm2711_vulkan_native_jobs_recover(
	struct bcm2711_render_device *controller)
{
	struct bcm2711_vulkan_native_job *job;
	int first;
	int error;

	/* Missing native recovery ownership supplies no valid DMA-stop boundary. */
	if (controller == NULL || controller->space.native == NULL)
		return EINVAL;

	/* Surviving external sessions or failed native reset leave every persistent whole root untouched. */
	if (controller->sessions != 0)
		return EBUSY;
	if (!controller->space.native->hardware.ready)
		return EIO;

	/* Checked reset permits every CPU root to retire while failed translations remain independently space-owned. */
	first = 0;
	while (controller->quarantine != NULL) {
		job = controller->quarantine;
		controller->quarantine = job->next;
		job->next = NULL;
		job->quarantined = false;
		error = release_job(job);
		if (first == 0 && error != 0)
			first = error;
	}

	/* Closed Vulkan namespaces retain their renderer descriptors until all former prepared destructors stop borrowing them. */
	error = retire_closed_sessions(controller);
	if (first == 0 && error != 0)
		first = error;
	if (first != 0)
		return first;

	/* Succeeded: no logical native payload or closed session remains hidden behind callback completion. */
	return 0;
}

/* Consumes the complete native pass before dropping pending primary/descriptor references and finally the CPU payload root. */
static int
release_job(
	struct bcm2711_vulkan_native_job *job)
{
	int first;
	int error;

	/* Checked DMA retirement consumes all mapped inputs, including incomplete allocation prefixes. */
	first = bcm2711_vulkan_native_pass_release(&job->pass, true);

	/* Pending counts and cloned descriptors remain valid through the last native pass destructor. */
	error = bcm2711_vulkan_prepared_release(job->prepared, true);
	if (first == 0 && error != 0)
		first = error;
	kern_free(job);
	if (first != 0)
		return first;

	/* Succeeded: every independently pending primary and native pass input has retired once. */
	return 0;
}

/* Releases closed renderer descriptors only after their final Vulkan dependency graph and protocol arena can retire. */
static int
retire_closed_sessions(
	struct bcm2711_render_device *controller)
{
	struct bcm2711_render_session **position;
	struct bcm2711_render_session *session;
	int first;
	int error;

	/* A failed logical close stays linked for the next explicit recovery, with its renderer pointer still valid. */
	first = 0;
	position = &controller->closed;
	while (*position != NULL) {
		session = *position;
		error = bcm2711_vulkan_session_close(&session->vulkan);
		if (first == 0 && error != 0)
			first = error;

		/* Remaining typed owners need this same renderer descriptor for their eventual destructors. */
		if (session->vulkan != NULL) {
			position = &session->closed_next;
			continue;
		}

		/* The entire protocol graph has retired, so the closed renderer has no remaining borrower. */
		*position = session->closed_next;
		kern_free(session);
	}

	/* A retirement refusal stays visible even when complete logical prefixes were safely consumed. */
	if (first != 0)
		return first;

	/* Succeeded: every closed namespace and its internally retained renderer descriptor has retired. */
	return 0;
}

/*
 * Prints one line of submission cost totals every two seconds and starts a new window.
 *
 * Averages are per submission in microseconds, so one line summarizes many
 * frames without adding console output to each submission.
 */
static void
report_perf(
	struct bcm2711_v3d_perf *perf)
{
	uint64_t now;
	uint64_t count;
	uint64_t busy;

	/* The first submission opens the window that later reports measure. */
	now = clock_ticks();
	if (perf->window_start == 0) {
		perf->window_start = now;
		return;
	}

	/* A window shorter than two seconds keeps accumulating. */
	if (kern_ticks_to_ms(now - perf->window_start) < 2000U)
		return;

	/* The share of the window the submissions held the worker shows how much CPU and GPU time frames took. */
	count = perf->submissions;
	busy = perf->total_ticks * 100U / (now - perf->window_start);

	/* Prints the window's per-submission averages and its worst submission. */
	kern_logf(
		"v3d: perf ms=%llu busy=%llu%% subs=%llu passes=%u/%u/%u skipped=%u draws=%u kib=%llu avg_us total=%llu prep=%llu tex=%llu copy=%llu prog=%llu rec=%llu alloc=%llu map=%llu clean=%llu exec=%llu tfu=%llu bin=%llu render=%llu oclean=%llu rel=%llu xfer=%llu max_us=%llu snap=%u/%u/%u/%u mapped_kib=%llu largest=%ux%u%c tiles=%u/%u\n",
		(unsigned long long)kern_ticks_to_ms(now - perf->window_start),
		(unsigned long long)busy,
		(unsigned long long)count,
		perf->render_passes,
		perf->clear_passes,
		perf->image_passes,
		perf->skipped_copies,
		perf->draws,
		(unsigned long long)(perf->staged_bytes / 1024U),
		(unsigned long long)average_us(perf->total_ticks, count),
		(unsigned long long)average_us(perf->prepare_ticks, count),
		(unsigned long long)average_us(perf->texture_ticks, count),
		(unsigned long long)average_us(perf->texel_copy_ticks, count),
		(unsigned long long)average_us(perf->program_ticks, count),
		(unsigned long long)average_us(perf->record_ticks, count),
		(unsigned long long)average_us(perf->allocation_ticks, count),
		(unsigned long long)average_us(perf->mapping_ticks, count),
		(unsigned long long)average_us(perf->clean_ticks, count),
		(unsigned long long)average_us(perf->execute_ticks, count),
		(unsigned long long)average_us(perf->tfu_ticks, count),
		(unsigned long long)average_us(perf->bin_ticks, count),
		(unsigned long long)average_us(perf->render_ticks, count),
		(unsigned long long)average_us(perf->output_clean_ticks, count),
		(unsigned long long)average_us(perf->release_ticks, count),
		(unsigned long long)average_us(perf->transfer_ticks, count),
		(unsigned long long)average_us(perf->max_ticks, 1U),
		perf->snapshot_hits,
		perf->snapshot_fills,
		perf->mapped_textures,
		perf->private_textures,
		(unsigned long long)(perf->mapped_bytes / 1024U),
		perf->largest_size >> 16,
		perf->largest_size & 0xffffU,
		perf->largest_kind != 0 ? perf->largest_kind : '-',
		perf->render_tiles,
		perf->image_tiles);

	/* The render runs' performance counters, in thousands per run. */
	if (perf->counter_runs != 0U) {
		kern_logf(
			"v3d: gpu runs=%u k/run cycles=%llu qidle=%llu qfrag=%llu qvalid=%llu qtmu=%llu qsb=%llu qvary=%llu qstall=%llu tmu=%llu tmumiss=%llu tmustall=%llu cle=%llu quads=%llu written=%llu tlbrd=%llu tlbwr=%llu\n",
			perf->counter_runs,
			(unsigned long long)(perf->counters[0] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[1] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[2] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[3] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[4] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[5] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[6] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[7] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[8] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[9] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[10] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[11] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[12] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[13] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[14] / perf->counter_runs / 1000U),
			(unsigned long long)(perf->counters[15] / perf->counter_runs / 1000U));
	}

	/* The next window starts empty at this report. */
	kern_memset(perf, 0, sizeof(*perf));
	perf->window_start = now;
}

/* Converts a tick total into microseconds per item, rounding down. */
static uint64_t
average_us(
	uint64_t ticks,
	uint64_t count)
{
	uint64_t microseconds;

	/* An empty window reports zero rather than dividing by zero. */
	if (count == 0)
		return 0;

	/*
	 * Scheduler ticks are coarse, but a total of many independently
	 * phased intervals still estimates the mean without bias.
	 */
	microseconds = kern_ticks_to_rate(ticks, 1000000U);

	/* Reports the average microseconds of one item. */
	return microseconds / count;
}
