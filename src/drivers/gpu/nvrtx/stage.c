/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Stage marks of the nvrtx driver, and its boot parameters.
 *
 * The driver is brought up on the board one stage at a time.  Each stage
 * writes a line when it begins and when it ends, so the console on the
 * firmware's framebuffer, and the kernel log, show how far the driver came.
 * Lines are cut to the console's width.
 *
 * The parameters are read from the raw boot command line, because the driver
 * attaches while the platform discovers its devices, before the kernel parses
 * the line:
 *
 *   nvrtx.off=1           the driver does not look at the GPU at all
 *   nvrtx.stop=<stage>    the driver stops before that stage
 *   nvrtx.gsp=1           the driver may go on to the GSP boot path after
 *                         N1 (the stages that write); without it the GPU is
 *                         left to the firmware after N1
 *   nvrtx.gr=1            after the copy channel, the driver has the
 *                         resource manager make the graphics golden context
 *                         (and the TU10x scrubber channel), stage P5g
 *   nvrtx.disp=1          after the display's readout (P8a), the driver
 *                         takes the head the firmware lit over with its own
 *                         channels and surfaces, stage P8b
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <kern/boot.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/platform.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* The handoff object that holds the raw boot command line. */
#define STAGE_COMMAND_LINE_HANDOFF	"boot.command-line"

/* The whole token that keeps the driver away from the GPU. */
#define STAGE_OFF_TOKEN			"nvrtx.off=1"

/* The whole token that lets the driver go on to the GSP boot path. */
#define STAGE_GSP_TOKEN			"nvrtx.gsp=1"

/* The whole token that asks for the graphics golden context. */
#define STAGE_GR_TOKEN			"nvrtx.gr=1"

/* The whole token that asks for the display takeover. */
#define STAGE_DISP_TOKEN		"nvrtx.disp=1"

/* The longest stop token: "nvrtx.stop=" and a stage name. */
#define STAGE_TOKEN_CAPACITY		32U

static const char *stage_command_line(void);
static bool stage_token_present(const char *token);

/*
 * Reports whether the boot asked the driver to stay out entirely (nvrtx.off=1).
 */
bool
nvrtx_stage_driver_off(void)
{
	bool present;

	/* Looks for nvrtx.off=1 among the boot parameters. */
	present = stage_token_present(STAGE_OFF_TOKEN);
	if (present)
		return true;

	/* The driver looks at the GPU by default. */
	return false;
}

/*
 * Reports whether the boot asked for the GSP boot path (nvrtx.gsp=1).
 */
bool
nvrtx_stage_gsp_wanted(void)
{
	bool present;

	/* Looks for nvrtx.gsp=1 among the boot parameters. */
	present = stage_token_present(STAGE_GSP_TOKEN);
	if (present)
		return true;

	/* Without it the stages that write never run. */
	return false;
}

/*
 * Reports whether the boot asked for the graphics golden context (nvrtx.gr=1).
 */
bool
nvrtx_stage_gr_wanted(void)
{
	bool present;

	/* Looks for nvrtx.gr=1 among the boot parameters. */
	present = stage_token_present(STAGE_GR_TOKEN);
	if (present)
		return true;

	/* Without it the boot keeps to the copy channel. */
	return false;
}

/*
 * Reports whether the boot asked for the display takeover (nvrtx.disp=1).
 */
bool
nvrtx_stage_disp_wanted(void)
{
	bool present;

	/* Looks for nvrtx.disp=1 among the boot parameters. */
	present = stage_token_present(STAGE_DISP_TOKEN);
	if (present)
		return true;

	/* Without it the boot keeps the firmware's picture after the readout. */
	return false;
}

/*
 * Reports whether a stage may run; nvrtx.stop=<stage> stops before it.
 *
 * The driver then stops before that stage and every later one.
 */
bool
nvrtx_stage_allowed(
	const char *stage)
{
	char token[STAGE_TOKEN_CAPACITY];
	int length;
	bool present;

	/* Spells the stop token of this stage; a name too long to spell never matches. */
	length = kern_snprintf(token, sizeof(token), "nvrtx.stop=%s", stage);
	if (length < 0 || (size_t)length >= sizeof(token))
		return false;

	/* Looks for the token among the boot parameters. */
	present = stage_token_present(token);
	if (present)
		return false;

	/* Succeeded: nothing stops the stage. */
	return true;
}

/*
 * Writes one stage-mark line to the console and the kernel log.
 *
 * The line reads "nvrtx: <text>".  Whatever does not fit in
 * NVRTX_STAGE_LINE_COLUMNS columns is dropped, so one mark never wraps and
 * pushes the earlier marks off the console.
 */
void
nvrtx_stage_mark(
	const char *format,
	...)
{
	char line[NVRTX_STAGE_LINE_COLUMNS + 1U];
	va_list arguments;
	int prefix;

	/* Writes the driver's prefix. */
	prefix = kern_snprintf(line, sizeof(line), "nvrtx: ");
	if (prefix < 0)
		return;

	/* Writes the text after the prefix; the formatter cuts what does not fit. */
	va_start(arguments, format);
	(void)kern_vsnprintf(line + prefix, sizeof(line) - (size_t)prefix, format, arguments);
	va_end(arguments);

	/* Publishes the line to the console and the kernel log. */
	kern_logf("%s\n", line);

	/* Succeeded: the stage diagnostic has reached the kernel log. */
	return;
}

/* Finds the raw boot command line, or NULL when the boot gave none. */
static const char *
stage_command_line(void)
{
	const char *line;

	/* Asks the platform for the line the loader handed over. */
	line = kern_boot_handoff(STAGE_COMMAND_LINE_HANDOFF);

	/* Reports the line; the platform keeps owning it. */
	return line;
}

/* Reports whether one whole token is among the boot parameters. */
static bool
stage_token_present(
	const char *token)
{
	const char *line;
	int present;

	/* A boot without a command line holds no token. */
	line = stage_command_line();
	if (line == NULL)
		return false;

	/* Compares the token with each word of the line. */
	present = kern_boot_parameters_token_present(line, token);
	if (present != 0)
		return true;

	/* The token is not on the line. */
	return false;
}
