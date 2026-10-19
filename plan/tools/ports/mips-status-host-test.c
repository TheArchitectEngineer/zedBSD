/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host check of the MIPS HAL's return Status and FCSR sanitizing
 * (src/hal/mips/status.h): a user return never carries CU0, kernel mode,
 * ERL or BEV whatever the saved frame held, and a sanitized FCSR has no
 * Cause bit.
 *
 * cc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal/mips \
 *    plan/tools/ports/mips-status-host-test.c -o <out> && <out>
 */

#include <stdio.h>
#include "status.h"

/* The bits a user Status must never hold. */
#define FORBIDDEN	(MIPS_STATUS_COPROCESSOR_0 | MIPS_STATUS_ERROR_LEVEL | MIPS_STATUS_BOOT_VECTORS)

static int failures;

static void check(int condition, const char *what);

/*
 * Runs the checks and reports PASS or FAIL.
 */
int
main(
	void)
{
	uint32_t current;
	uint32_t status;
	uint32_t saved;

	/* A kernel Status with every privileged bit and a mask, as the handler sees it. */
	current = 0xffffffffU & ~(uint32_t)(MIPS_STATUS_MODE_MASK | MIPS_STATUS_EXCEPTION_LEVEL);

	/* A user return without the unit. */
	status = mips_user_status(current, false, false);
	check((status & FORBIDDEN) == 0, "user status forbidden bits");
	check((status & MIPS_STATUS_MODE_MASK) == MIPS_STATUS_MODE_USER, "user mode");
	check((status & MIPS_STATUS_EXCEPTION_LEVEL) != 0, "user EXL");
	check((status & MIPS_STATUS_INTERRUPTS) != 0, "user IE");
	check((status & MIPS_STATUS_INTERRUPT_MASK) == MIPS_STATUS_INTERRUPT_MASK, "user IM from current");
	check((status & (MIPS_STATUS_COPROCESSOR_1 | MIPS_STATUS_FLOAT_64)) == 0, "user no FPU");

	/* The owner of a usable unit gets CU1 and FR; a non-owner FR only; an unusable unit neither. */
	status = mips_user_status(current, true, true);
	check((status & MIPS_STATUS_COPROCESSOR_1) != 0, "owner CU1");
	check((status & MIPS_STATUS_FLOAT_64) != 0, "owner FR");
	check((status & FORBIDDEN) == 0, "owner forbidden bits");
	status = mips_user_status(current, false, true);
	check((status & MIPS_STATUS_COPROCESSOR_1) == 0, "non-owner CU1");
	check((status & MIPS_STATUS_FLOAT_64) != 0, "non-owner FR");
	status = mips_user_status(current, true, false);
	check((status & (MIPS_STATUS_COPROCESSOR_1 | MIPS_STATUS_FLOAT_64)) == 0, "unusable unit");

	/* The IM of a user return is today's, not the frame's. */
	status = mips_user_status(0x00008400U, false, false);
	check((status & MIPS_STATUS_INTERRUPT_MASK) == 0x00008400U, "user IM exact");

	/* A kernel return keeps the saved bits, takes today's IM and sets EXL. */
	saved = 0x0000ff01U;
	status = mips_kernel_status(saved, 0x00008000U);
	check(status == (0x00008000U | MIPS_STATUS_INTERRUPTS | MIPS_STATUS_EXCEPTION_LEVEL), "kernel status");

	/* The FCSR keeps everything but Cause. */
	check(mips_float_control_sanitize(0xffffffffU) == (0xffffffffU & ~(uint32_t)MIPS_FCSR_CAUSE_MASK), "fcsr all");
	check(mips_float_control_sanitize(0x00000f80U) == 0x00000f80U, "fcsr enables kept");
	check(mips_float_control_sanitize(0x0003f000U) == 0, "fcsr cause cleared");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("FAIL %d\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("PASS\n");
	return 0;
}

/* Counts and names a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A passing check leaves no trace. */
	if (condition)
		return;

	/* Names the failed check and counts it. */
	printf("failed: %s\n", what);
	failures++;
}
