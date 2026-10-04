/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The firmware's clocks of the BCM2711 graphics hardware.
 *
 * The VideoCore firmware owns the clocks of the display path and of V3D.  The
 * ARM side asks about them through the mailbox's property channel.  This file
 * only asks (the get tags); nothing here changes a clock.  The tag values and
 * the clock numbers are those of the Raspberry Pi firmware wiki's mailbox
 * property interface.
 */

#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/platform/rpi4/rpi4-firmware.h"

/* Asks whether a clock is on and whether it exists. */
#define CLOCK_TAG_ASK_ON_OFF		0x00030001U

/* Asks a clock's present rate, in hertz. */
#define CLOCK_TAG_ASK_HZ		0x00030002U

/* Asks the highest rate a clock may be set to, in hertz. */
#define CLOCK_TAG_ASK_TOP_HZ		0x00030004U

/* The on/off answer: bit 0 is set while the clock runs. */
#define CLOCK_ON_OFF_RUNNING		0x00000001U

/* The on/off answer: bit 1 is set when the firmware has no such clock. */
#define CLOCK_ON_OFF_ABSENT		0x00000002U

/* The words of a request and of its answer: the clock number and one value. */
#define CLOCK_VALUE_WORDS		2U

static int ask_clock(uint32_t tag, uint32_t clock_id, uint32_t *answer);

/*
 * Writes one stage-mark line with what the firmware says about a clock.
 *
 * The line names the clock, whether it runs, its present rate and its
 * highest rate.  A question the firmware does not answer is shown with the
 * error it gave, so the mark is written whatever happens.
 */
void
bcm2711_clock_report(
	const char *family,
	const char *name,
	uint32_t clock_id)
{
	uint32_t on_off;
	uint32_t hz;
	uint32_t top_hz;
	int error;

	/* Asks whether the clock exists and runs. */
	error = ask_clock(CLOCK_TAG_ASK_ON_OFF, clock_id, &on_off);
	if (error != 0) {
		bcm2711_stage_mark(family, "clk %s (%u) no answer (%d)", name, (unsigned)clock_id, error);
		return;
	}

	/* Reports a clock the firmware does not have. */
	if ((on_off & CLOCK_ON_OFF_ABSENT) != 0) {
		bcm2711_stage_mark(family, "clk %s (%u) absent", name, (unsigned)clock_id);
		return;
	}

	/* Asks the present rate; zero stands for an unanswered question. */
	error = ask_clock(CLOCK_TAG_ASK_HZ, clock_id, &hz);
	if (error != 0)
		hz = 0;

	/* Asks the highest rate; zero stands for an unanswered question. */
	error = ask_clock(CLOCK_TAG_ASK_TOP_HZ, clock_id, &top_hz);
	if (error != 0)
		top_hz = 0;

	/* Writes the clock's line. */
	bcm2711_stage_mark(family, "clk %s (%u) %s now %u Hz top %u Hz",
			   name,
			   (unsigned)clock_id,
			   (on_off & CLOCK_ON_OFF_RUNNING) != 0 ? "on" : "off",
			   (unsigned)hz,
			   (unsigned)top_hz);
}

/* Sends one get tag about a clock and returns the value word of its answer. */
static int
ask_clock(
	uint32_t tag,
	uint32_t clock_id,
	uint32_t *answer)
{
	uint32_t values[CLOCK_VALUE_WORDS];
	uint32_t answered;
	int error;

	/* Fills the request: the clock number, and room for the answer. */
	values[0] = clock_id;
	values[1] = 0;

	/* Sends the tag and waits for the firmware. */
	error = drv_rpi4_firmware_property(tag, values, 1U, CLOCK_VALUE_WORDS, &answered);
	if (error != 0)
		return error;

	/* Refuses an answer too short to hold the value, or about another clock. */
	if (answered < CLOCK_VALUE_WORDS * 4U)
		return EIO;
	if (values[0] != clock_id)
		return EIO;

	/* Succeeded: the value word answers the question. */
	*answer = values[1];
	return 0;
}
