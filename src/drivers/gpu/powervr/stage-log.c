/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one stage record of the boot: the driver's paths note
 * a stage's ok or failure here, the line goes to the console through
 * kern_logf, and hw.gpu.powervr.stages reads the record.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/sysctl.h>

#include "drivers/gpu/powervr/powervr-private.h"
#include "drivers/gpu/powervr/stage.h"

static size_t stages_read(char *text, size_t capacity);

/* The record of the boot and the lock around it (taken from any thread, never in an interrupt). */
static struct powervr_stage_record stage_record;
static struct spinlock stage_lock;

/*
 * Readies the record and installs hw.gpu.powervr.stages; called once on
 * the boot thread while the driver attaches.
 */
void
powervr_stage_init(void)
{
	/* The lock, then the sysctl that takes it. */
	spin_init(&stage_lock, LOCK_RANK_DEVICE, "powervr stages");
	kern_gpu_stages_set(stages_read);
}

/* Notes that stage succeeded, printing its line the first time. */
void
powervr_stage_ok(
	enum powervr_stage stage)
{
	char line[POWERVR_STAGE_LINE_MAX];
	bool print;

	/* Notes it under the lock; prints after. */
	spin_lock(&stage_lock);

	print = powervr_stage_note_ok(&stage_record, stage, line, sizeof(line));

	spin_unlock(&stage_lock);

	/* The line, outside the lock. */
	if (print)
		kern_logf("%s\n", line);
}

/* Notes that stage failed for reason, printing its line while under the limit. */
void
powervr_stage_fail(
	enum powervr_stage stage,
	enum powervr_stage_reason reason)
{
	char line[POWERVR_STAGE_LINE_MAX];
	bool print;

	/* Notes it under the lock; prints after. */
	spin_lock(&stage_lock);

	print = powervr_stage_note_fail(&stage_record, stage, reason, line, sizeof(line));

	spin_unlock(&stage_lock);

	/* The line, outside the lock. */
	if (print)
		kern_logf("%s\n", line);
}

/* The text of hw.gpu.powervr.stages. */
static size_t
stages_read(
	char *text,
	size_t capacity)
{
	size_t length;

	/* Writes it under the lock. */
	spin_lock(&stage_lock);

	length = powervr_stage_text(&stage_record, text, capacity);

	spin_unlock(&stage_lock);
	return length;
}
