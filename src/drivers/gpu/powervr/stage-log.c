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

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/sysctl.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/powervr-private.h"
#include "drivers/gpu/powervr/stage.h"
#include "drivers/gpu/powervr/render-job.h"

static size_t stages_read(char *text, size_t capacity);
static int stages_write(const char *text, size_t length);
static int parse_number(const char *text, size_t length, size_t *at, unsigned base, uint64_t *value);

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
	kern_gpu_stages_write_set(stages_write);
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

/* Notes that stage failed for reason with the value it carries (a fault's address, a reset's reason). */
void
powervr_stage_fail_value(
	enum powervr_stage stage,
	enum powervr_stage_reason reason,
	uint64_t value)
{
	char line[POWERVR_STAGE_LINE_MAX];
	bool print;

	/* Notes it under the lock; prints after. */
	spin_lock(&stage_lock);

	print = powervr_stage_note_fail_value(&stage_record, stage, reason, value, line, sizeof(line));

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

/*
 * Takes a root write of hw.gpu.powervr.stages: the test-only draw filter
 * of render jobs, for bisecting a job that locks the core up.  Either an
 * 8-byte number (sysctl hw.gpu.powervr.stages=N: keep the job's first N
 * draws; with skip bits of draws 0 to 31 in the high word, 0 for no
 * limit), or text "draws=N skip=M" (N decimal or "all", M hexadecimal
 * draw-index bits, either part optional).  0 or "draws=all skip=0"
 * turns it off.  EINVAL for anything else.
 */
static int
stages_write(
	const char *text,
	size_t length)
{
	uint64_t limit;
	uint64_t skip;
	uint64_t value;
	size_t at;
	int error;

	/*
	 * An 8-byte number (the sysctl command's form): the draw limit in the
	 * low 32 bits (0xffffffff for none, 0 also none), the skip bits of
	 * draws 0 to 31 in the high ones.
	 */
	if (length == 8U && (text[0] < 'a' || text[0] > 'z')) {
		kern_memcpy(&value, text, 8U);
		limit = value & 0xffffffffU;
		if (limit == 0)
			limit = 0xffffffffU;
		powervr_render_debug_set((uint32_t)limit, value >> 32);
		return 0;
	}

	/* Off unless named. */
	limit = 0xffffffffU;
	skip = 0;
	at = 0;

	/* Each word in turn. */
	while (at < length) {
		/* Spaces and a final line end between words. */
		if (text[at] == ' ' || text[at] == '\n' || text[at] == '\0') {
			at++;
			continue;
		}

		/* draws=all or draws=N. */
		if (length - at >= 9U && kern_memcmp(text + at, "draws=all", 9U) == 0) {
			limit = 0xffffffffU;
			at += 9U;
		} else if (length - at >= 6U && kern_memcmp(text + at, "draws=", 6U) == 0) {
			at += 6U;
			error = parse_number(text, length, &at, 10U, &value);
			if (error != 0 || value > 0xffffffffU)
				return EINVAL;
			limit = value;
		} else if (length - at >= 5U && kern_memcmp(text + at, "skip=", 5U) == 0) {
			at += 5U;
			error = parse_number(text, length, &at, 16U, &skip);
			if (error != 0)
				return EINVAL;
		} else {
			return EINVAL;
		}
	}

	/* Succeeded: the filter is in force for later jobs. */
	powervr_render_debug_set((uint32_t)limit, skip);
	return 0;
}

/* Reads a number in a base (10 or 16, digits only) at *at, moving past it; EINVAL without a digit. */
static int
parse_number(
	const char *text,
	size_t length,
	size_t *at,
	unsigned base,
	uint64_t *value)
{
	uint64_t number;
	unsigned digit;
	unsigned digits;
	char c;

	/* The digits up to a space or the end. */
	number = 0;
	digits = 0;
	while (*at < length) {
		c = text[*at];
		if (c >= '0' && c <= '9') {
			digit = (unsigned)(c - '0');
		} else if (base == 16U && c >= 'a' && c <= 'f') {
			digit = (unsigned)(c - 'a') + 10U;
		} else if (base == 16U && c >= 'A' && c <= 'F') {
			digit = (unsigned)(c - 'A') + 10U;
		} else {
			break;
		}
		if (digit >= base || digits >= 16U)
			return EINVAL;
		number = number * base + digit;
		digits++;
		(*at)++;
	}

	/* A number has a digit. */
	if (digits == 0)
		return EINVAL;

	/* Succeeded: the number. */
	*value = number;
	return 0;
}
