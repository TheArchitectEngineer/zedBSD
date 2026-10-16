/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws039-p015's display routine parameters
 * (display-command.c): each packer's bytes against the same values put
 * in the Linux v6.19 structures (atomfirmware.h, built with the Linux
 * tree's include directory on the path), for the board's re-light of its
 * DVI output on transmitter E.  The Linux names stay in this test.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __counted_by(member)
#define __packed __attribute__((packed))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#include "atomfirmware.h"

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

static unsigned checks;
static unsigned failures;

static void
check(int ok, const char *format, ...)
{
	va_list arguments;

	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

int
main(void)
{
	struct set_pixel_clock_parameter_v1_7 clock;
	struct dig_encoder_stream_setup_parameters_v1_5 encoder;
	struct dig_transmitter_control_ps_allocation_v1_6 transmitter;
	struct enable_crtc_parameters crtc;
	struct enable_disp_power_gating_parameters_v2_1 gating;
	struct amdrdna_pixel_clock_command clock_command;
	struct amdrdna_encoder_command encoder_command;
	struct amdrdna_transmitter_command transmitter_command;
	uint32_t words[AMDRDNA_COMMAND_WORDS];

	/* The pixel clock: 148.5 MHz on the PHY PLL 4 for the encoder of transmitter E, DVI, generator 0. */
	memset(&clock, 0, sizeof(clock));
	clock.pixclk_100hz = 1485000U;
	clock.pll_id = ATOM_COMBOPHY_PLL4;
	clock.encoderobjid = 0x21U;
	clock.encoder_mode = ATOM_ENCODER_MODE_DVI;
	clock.miscinfo = PIXEL_CLOCK_V7_MISC_FORCE_PROG_PPLL;
	clock.crtc_id = ATOM_CRTC1;
	clock.deep_color_ratio = 0U;
	clock_command = (struct amdrdna_pixel_clock_command){ 1485000U, ATOM_COMBOPHY_PLL4, 0x21U, ATOM_ENCODER_MODE_DVI,
		PIXEL_CLOCK_V7_MISC_FORCE_PROG_PPLL, ATOM_CRTC1, 0U };
	amdrdna_command_pixel_clock(words, &clock_command);
	check(sizeof(clock) <= sizeof(words) && memcmp(words, &clock, sizeof(clock)) == 0, "the pixel clock's bytes (%zu)", sizeof(clock));

	/* The encoder's stream setup: DIG 4, DVI, four lanes, 148.5 MHz, 8 bits. */
	memset(&encoder, 0, sizeof(encoder));
	encoder.digid = 4U;
	encoder.action = ATOM_ENCODER_CMD_STREAM_SETUP;
	encoder.digmode = ATOM_ENCODER_MODE_DVI;
	encoder.lanenum = 4U;
	encoder.pclk_10khz = 14850U;
	encoder.bitpercolor = PANEL_8BIT_PER_COLOR;
	encoder.dplinkrate_270mhz = 0U;
	encoder_command = (struct amdrdna_encoder_command){ 4U, ATOM_ENCODER_CMD_STREAM_SETUP, ATOM_ENCODER_MODE_DVI, 4U, 14850U,
		PANEL_8BIT_PER_COLOR, 0U };
	amdrdna_command_encoder(words, &encoder_command);
	check(sizeof(encoder) <= sizeof(words) && memcmp(words, &encoder, sizeof(encoder)) == 0, "the encoder's bytes (%zu)", sizeof(encoder));

	/* The transmitter: enable PHY E, DVI, four lanes, 148.5 MHz, hot-plug 6, no front end, the DisplayPort connector. */
	memset(&transmitter, 0, sizeof(transmitter));
	transmitter.param.phyid = 4U;
	transmitter.param.action = ATOM_TRANSMITTER_ACTION_ENABLE;
	transmitter.param.mode_laneset.digmode = 2U;
	transmitter.param.lanenum = 4U;
	transmitter.param.symclk_10khz = 14850U;
	transmitter.param.hpdsel = 6U;
	transmitter.param.digfe_sel = 0U;
	transmitter.param.connobj_id = 0x13U;
	transmitter_command = (struct amdrdna_transmitter_command){ 4U, ATOM_TRANSMITTER_ACTION_ENABLE, 2U, 4U, 14850U, 6U, 0U, 0x13U };
	amdrdna_command_transmitter(words, &transmitter_command);
	check(sizeof(transmitter) <= sizeof(words) && memcmp(words, &transmitter, sizeof(transmitter)) == 0, "the transmitter's bytes (%zu)",
	    sizeof(transmitter));

	/* A generator's enable and a pipe's power. */
	memset(&crtc, 0, sizeof(crtc));
	crtc.crtc_id = ATOM_CRTC1;
	crtc.enable = ATOM_ENABLE;
	amdrdna_command_pipe(words, ATOM_CRTC1, ATOM_ENABLE);
	check(memcmp(words, &crtc, sizeof(crtc)) == 0, "the generator's enable bytes");
	memset(&gating, 0, sizeof(gating));
	gating.disp_pipe_id = 2U;
	gating.enable = ATOM_DISABLE;
	amdrdna_command_pipe(words, 2U, ATOM_DISABLE);
	check(memcmp(words, &gating, sizeof(gating)) == 0, "the pipe's power bytes");

	printf("%s amdrdna command: %u checks, %u failures\n", failures == 0U ? "PASS" : "FAIL", checks, failures);
	return failures == 0U ? 0 : 1;
}
