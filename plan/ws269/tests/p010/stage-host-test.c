/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of ws269-p010a: the stage lines' wording, the once-a-boot ok,
 * the limit of three failures and the one "further failures not shown",
 * and the text of hw.gpu.powervr.stages.
 */

#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/stage.h"

static int failures;
static int passes;

/* One expectation. */
static void
expect(
	int good,
	const char *what)
{
	if (good) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

/* A note's printing and line. */
static void
expect_line(
	int printed,
	const char *line,
	const char *wanted,
	const char *what)
{
	if (wanted == NULL) {
		expect(!printed, what);
		return;
	}
	expect(printed && strcmp(line, wanted) == 0, what);
	if (printed && strcmp(line, wanted) != 0)
		printf("  got \"%s\", wanted \"%s\"\n", line, wanted);
}

int
main(void)
{
	struct powervr_stage_record record;
	char line[POWERVR_STAGE_LINE_MAX];
	char text[256];
	char small[8];
	size_t length;
	int printed;
	int i;

	memset(&record, 0, sizeof(record));

	/* Nothing reached yet. */
	length = powervr_stage_text(&record, text, sizeof(text));
	expect(strcmp(text, "firmware=- render-node=- compute=- ta=- 3d=- transfer=- present=-") == 0, "empty text");
	expect(length == strlen(text), "empty length");

	/* ok once a boot. */
	printed = powervr_stage_note_ok(&record, POWERVR_STAGE_FIRMWARE, line, sizeof(line));
	expect_line(printed, line, "powervr: firmware: ok", "firmware ok");
	printed = powervr_stage_note_ok(&record, POWERVR_STAGE_FIRMWARE, line, sizeof(line));
	expect_line(printed, line, NULL, "firmware ok twice");
	printed = powervr_stage_note_ok(&record, POWERVR_STAGE_RENDER_NODE, line, sizeof(line));
	expect_line(printed, line, "powervr: render-node: ok", "render-node ok");

	/* Three failures shown, the fourth says the rest are not, then silence. */
	for (i = 0; i < 3; i++) {
		printed = powervr_stage_note_fail(&record, POWERVR_STAGE_TA, POWERVR_REASON_TIMEOUT, line, sizeof(line));
		expect_line(printed, line, "powervr: ta: fail(timeout)", "ta fail shown");
	}
	printed = powervr_stage_note_fail(&record, POWERVR_STAGE_TA, POWERVR_REASON_FAULT, line, sizeof(line));
	expect_line(printed, line, "powervr: ta: further failures not shown", "ta fourth");
	for (i = 0; i < 5; i++) {
		printed = powervr_stage_note_fail(&record, POWERVR_STAGE_TA, POWERVR_REASON_FAULT, line, sizeof(line));
		expect_line(printed, line, NULL, "ta later");
	}

	/* A reason with a space, and an unknown reason is "error". */
	printed = powervr_stage_note_fail(&record, POWERVR_STAGE_COMPUTE, POWERVR_REASON_SPIRV_SUBSET, line, sizeof(line));
	expect_line(printed, line, "powervr: compute: fail(spirv subset)", "compute spirv");
	printed = powervr_stage_note_fail(&record, POWERVR_STAGE_PRESENT, (enum powervr_stage_reason)99, line, sizeof(line));
	expect_line(printed, line, "powervr: present: fail(error)", "unknown reason");
	printed = powervr_stage_note_ok(&record, (enum powervr_stage)99, line, sizeof(line));
	expect_line(printed, line, NULL, "unknown stage");

	/* The text names the latest state; words have no spaces. */
	powervr_stage_text(&record, text, sizeof(text));
	expect(strcmp(text, "firmware=ok render-node=ok compute=fail:spirv-subset ta=fail:fault 3d=- transfer=- present=fail:error") == 0, "text");
	if (failures != 0)
		printf("  text \"%s\"\n", text);

	/* A failure then an ok: the text says ok, the ok line is printed once. */
	printed = powervr_stage_note_ok(&record, POWERVR_STAGE_COMPUTE, line, sizeof(line));
	expect_line(printed, line, "powervr: compute: ok", "compute ok after fail");
	powervr_stage_text(&record, text, sizeof(text));
	expect(strstr(text, "compute=ok ") != NULL, "text after ok");

	/* A short buffer stays terminated and the length is the whole. */
	length = powervr_stage_text(&record, small, sizeof(small));
	expect(strlen(small) == sizeof(small) - 1U && length == strlen(text), "short buffer");

	/* Errors that are their own reason. */
	expect(powervr_stage_reason_of(ETIMEDOUT) == POWERVR_REASON_TIMEOUT, "etimedout");
	expect(powervr_stage_reason_of(ENOENT) == POWERVR_REASON_ENOENT, "enoent");
	expect(powervr_stage_reason_of(EINVAL) == POWERVR_REASON_ERROR, "einval");

	printf("stage-host-test: %d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
