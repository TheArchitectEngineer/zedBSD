/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The table of the display 9 arithmetic: every number the Gen9 mode set,
 * the backlight and the firmware's readout compute, over a grid of
 * inputs, written as one line each.
 *
 * Nothing is checked here.  The table taken from one tree is the record
 * another tree's table is compared with, line for line, so a rewrite of
 * the arithmetic shows every input whose answer it changed.  The inputs
 * are fixed (a grid, and a fixed pseudo-random sequence), so two runs of
 * one tree write the same table.
 */

#include "host-test.h"

#include "../../display/gen9-backlight.h"
#include "../../display/gen9-flip.h"
#include "../../display/gen9-mode.h"
#include "../../display/gen9-outputs.h"
#include "../../device-info.h"
#include "../../intel/display-regs-gen9.h"
#include "../../intel/display-regs-gen9-modeset.h"

#include <stdio.h>
#include <string.h>

/* How many pseudo-random inputs the sampled parts of the table take. */
#define I915_TABLE_SAMPLES	2000U

static void i915_table_cdclk(FILE *out);
static void i915_table_pick_mode(FILE *out);
static void i915_table_dp_link(FILE *out);
static void i915_table_dpcd_rates(FILE *out);
static void i915_table_train_delay(FILE *out);
static void i915_table_dpll(FILE *out);
static void i915_table_buf_trans(FILE *out);
static void i915_table_training(FILE *out);
static void i915_table_watermarks(FILE *out);
static void i915_table_wm_words(FILE *out);
static void i915_table_latency(FILE *out);
static void i915_table_pwm(FILE *out);
static void i915_table_readout(FILE *out);
static void i915_table_flip_numbers(FILE *out);
static void i915_table_print_wm(FILE *out, unsigned row, int error, const struct i915_skl_pipe_wm pipes[3]);
static void i915_table_print_readout(FILE *out, unsigned row, int error, const struct i915_skl_readout *readout);
static void i915_table_sv7(struct i915_skl_registers *registers);
static uint32_t i915_table_random(void);

/*
 * The state of the pseudo-random sequence.
 *
 * Starts at a fixed seed and moves on every draw, so the sampled inputs
 * are the same on every run.
 */
static uint32_t i915_table_seed = 0x2545f491U;

/* The memory latencies of the watermark rows, in microseconds: a typical part, and one with level 0 at 0. */
static const uint16_t i915_table_latencies[2][8] = {
	{ 2U, 4U, 10U, 18U, 28U, 40U, 60U, 90U },
	{ 0U, 7U, 15U, 25U, 40U, 60U, 0U, 0U }
};

/*
 * Writes the table to the file the command line names, or to the standard output.
 */
int
main(
	int argc,
	char **argv)
{
	FILE *out;

	/* The file the table goes to. */
	out = stdout;
	if (argc > 1) {
		out = fopen(argv[1], "w");
		if (out == NULL) {
			fprintf(stderr, "%s: cannot write\n", argv[1]);
			return 2;
		}
	}

	/* Each part of the arithmetic in turn. */
	i915_table_cdclk(out);
	i915_table_pick_mode(out);
	i915_table_dp_link(out);
	i915_table_dpcd_rates(out);
	i915_table_train_delay(out);
	i915_table_dpll(out);
	i915_table_buf_trans(out);
	i915_table_training(out);
	i915_table_watermarks(out);
	i915_table_wm_words(out);
	i915_table_latency(out);
	i915_table_pwm(out);
	i915_table_readout(out);
	i915_table_flip_numbers(out);

	/* The file is complete. */
	if (out != stdout)
		fclose(out);

	/* Succeeded: the whole table is written. */
	return 0;
}

/* Writes the CD clock of every LCPLL1 state, DPLL0 VCO code and CDCLK_CTL select. */
static void
i915_table_cdclk(
	FILE *out)
{
	static const uint32_t lcpll1[3] = { 0U, I915_SKL_DPLL_ENABLE, I915_SKL_DPLL_ENABLE | (1U << 30) };
	uint32_t ctrl1;
	uint32_t cdclk_ctl;
	uint32_t khz;
	unsigned state;
	unsigned rate;
	unsigned select;
	unsigned divider;

	/* Each LCPLL1 state, DPLL0 link rate code, CD clock select and decimal divider. */
	for (state = 0U; state < 3U; state++) {
		for (rate = 0U; rate < 8U; rate++) {
			for (select = 0U; select < 4U; select++) {
				for (divider = 0U; divider < 0x800U; divider += 0x7fU) {
					ctrl1 = (uint32_t)rate << 1;
					cdclk_ctl = ((uint32_t)select << 26) | divider;
					khz = drv_i915_skl_cdclk_khz(lcpll1[state], ctrl1, cdclk_ctl);
					fprintf(out, "cdclk %08x %08x %08x %u\n", lcpll1[state], ctrl1, cdclk_ctl, khz);
				}
			}
		}
	}
}

/* Writes the mode picked from pseudo-random EDIDs at each pixel clock limit. */
static void
i915_table_pick_mode(
	FILE *out)
{
	static const uint32_t limits[3] = { 0U, I915_SKL_HDMI_MAX_KHZ, 675000U };
	struct i915_display_mode mode;
	uint8_t edid[256];
	unsigned sample;
	unsigned index;
	unsigned limit;
	int error;

	/* Each sample's EDID: random bytes, a CEA extension's tag and DTD offset on half of them. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		for (index = 0U; index < sizeof(edid); index++)
			edid[index] = (uint8_t)i915_table_random();
		if ((sample & 1U) != 0U) {
			edid[128] = 0x02U;
			edid[130] = (uint8_t)(4U + (i915_table_random() % 100U));
		}

		/* The base block alone and both blocks, at each limit. */
		for (limit = 0U; limit < 3U; limit++) {
			memset(&mode, 0, sizeof(mode));
			error = drv_i915_skl_pick_mode(edid, 256U, limits[limit], &mode);
			fprintf(out, "mode %u %u %d %d %u %u %u %u %u %u %u %u %d %d %u %u %u %d\n",
			    sample,
			    limits[limit],
			    error,
			    mode.clock_khz,
			    mode.hdisplay,
			    mode.hsync_start,
			    mode.hsync_end,
			    mode.htotal,
			    mode.vdisplay,
			    mode.vsync_start,
			    mode.vsync_end,
			    mode.vtotal,
			    mode.hsync_positive,
			    mode.vsync_positive,
			    mode.width_mm,
			    mode.height_mm,
			    mode.descriptor_index,
			    mode.edid_bpc);
		}
	}

	/* No EDID at all: the safe mode. */
	memset(&mode, 0, sizeof(mode));
	error = drv_i915_skl_pick_mode(NULL, 0U, I915_SKL_HDMI_MAX_KHZ, &mode);
	fprintf(out, "mode none %d %d %u %u\n", error, mode.clock_khz, mode.hdisplay, mode.vdisplay);
}

/* Writes the DP link and its M and N for every sink limit and pixel clock. */
static void
i915_table_dp_link(
	FILE *out)
{
	static const uint32_t sinks[4] = { I915_SKL_DP_RBR_KHZ, I915_SKL_DP_HBR_KHZ, I915_SKL_DP_HBR2_KHZ, 810000U };
	struct i915_skl_link link;
	uint32_t pixel_khz;
	unsigned sink;
	unsigned lanes;
	int error;

	/* Each sink rate, lane count and pixel clock from 20 MHz to 700 MHz. */
	for (sink = 0U; sink < 4U; sink++) {
		for (lanes = 1U; lanes <= 4U; lanes++) {
			for (pixel_khz = 20000U; pixel_khz <= 700000U; pixel_khz += 2750U) {
				error = drv_i915_skl_dp_link(sinks[sink], lanes, pixel_khz, &link);
				fprintf(out, "link %u %u %u %d %u %u %08x %08x %08x %08x %u\n",
				    sinks[sink],
				    lanes,
				    pixel_khz,
				    error,
				    link.port_khz,
				    link.lanes,
				    link.data_m,
				    link.data_n,
				    link.link_m,
				    link.link_n,
				    link.tu);
			}
		}
	}

	/* The M and N alone at odd clocks on every link. */
	for (sink = 0U; sink < 3U; sink++) {
		for (lanes = 1U; lanes <= 4U; lanes *= 2U) {
			for (pixel_khz = 1U; pixel_khz <= 900000U; pixel_khz = pixel_khz * 3U + 7U) {
				memset(&link, 0, sizeof(link));
				link.port_khz = sinks[sink];
				link.lanes = lanes;
				drv_i915_skl_dp_m_n(&link, pixel_khz);
				fprintf(out, "m_n %u %u %u %08x %08x %08x %08x %u\n",
				    link.port_khz,
				    link.lanes,
				    pixel_khz,
				    link.data_m,
				    link.data_n,
				    link.link_m,
				    link.link_n,
				    link.tu);
			}
		}
	}
}

/* Writes the port clock of every DPCD rate code and the code of every port clock. */
static void
i915_table_dpcd_rates(
	FILE *out)
{
	static const uint32_t clocks[8] = { 0U, 81000U, 162000U, 216000U, 270000U, 324000U, 540000U, 810000U };
	unsigned code;
	unsigned clock;

	/* Each code byte. */
	for (code = 0U; code < 256U; code++)
		fprintf(out, "rate_khz %02x %u\n", code, drv_i915_skl_dpcd_rate_khz((uint8_t)code));

	/* Each port clock. */
	for (clock = 0U; clock < 8U; clock++)
		fprintf(out, "rate_code %u %02x\n", clocks[clock], drv_i915_skl_dpcd_rate_code(clocks[clock]));
}

/* Writes the training intervals of every DPCD revision and read interval. */
static void
i915_table_train_delay(
	FILE *out)
{
	static const uint8_t revisions[4] = { 0x10U, 0x11U, 0x12U, 0x14U };
	unsigned revision;
	unsigned interval;
	int eq;

	/* Each revision, interval byte and phase. */
	for (revision = 0U; revision < 4U; revision++) {
		for (interval = 0U; interval < 256U; interval++) {
			for (eq = 0; eq <= 1; eq++) {
				fprintf(out, "train_delay %02x %02x %d %u\n",
				    revisions[revision],
				    interval,
				    eq,
				    drv_i915_skl_dp_train_delay_us(revisions[revision], (uint8_t)interval, eq));
			}
		}
	}
}

/* Writes the DPLL words of every DP port clock and of HDMI clocks from 20 MHz to 650 MHz. */
static void
i915_table_dpll(
	FILE *out)
{
	static const uint32_t clocks[9] = { 0U, 81000U, 162000U, 216000U, 243000U, 270000U, 324000U, 432000U, 540000U };
	static const uint32_t references[2] = { 24000U, 19200U };
	struct i915_skl_dpll_words words;
	uint32_t pixel_khz;
	unsigned clock;
	unsigned reference;
	int error;

	/* Each DP port clock. */
	for (clock = 0U; clock < 9U; clock++) {
		memset(&words, 0, sizeof(words));
		error = drv_i915_skl_dpll_dp(clocks[clock], &words);
		fprintf(out, "dpll_dp %u %d %08x %08x %08x %u\n", clocks[clock], error, words.ctrl1, words.cfgcr1, words.cfgcr2, words.port_khz);
	}

	/* Each HDMI pixel clock on both references. */
	for (reference = 0U; reference < 2U; reference++) {
		for (pixel_khz = 20000U; pixel_khz <= 650000U; pixel_khz += 250U) {
			memset(&words, 0, sizeof(words));
			error = drv_i915_skl_dpll_hdmi(pixel_khz, references[reference], &words);
			fprintf(out, "dpll_hdmi %u %u %d %08x %08x %08x %u\n",
			    references[reference],
			    pixel_khz,
			    error,
			    words.ctrl1,
			    words.cfgcr1,
			    words.cfgcr2,
			    words.port_khz);
		}
	}
}

/* Writes the DDI buffer tables of every part, output and port, and the levels around them. */
static void
i915_table_buf_trans(
	FILE *out)
{
	static const int platforms[4] = { I915_PLATFORM_SKYLAKE, I915_PLATFORM_KABYLAKE, I915_PLATFORM_COFFEELAKE, I915_PLATFORM_COMETLAKE };
	static const uint16_t devices[16] = {
		0x1902U, 0x1906U, 0x190eU, 0x1912U, 0x1916U, 0x191eU, 0x5906U, 0x590eU,
		0x5916U, 0x591eU, 0x5917U, 0x3e92U, 0x3ea0U, 0x9bc5U, 0x9b41U, 0x87c0U
	};
	const struct i915_xd9_buf_trans *table;
	unsigned platform;
	unsigned device;
	unsigned output;
	unsigned port;
	unsigned count;
	unsigned entry;
	unsigned set;
	int sku;

	/* The power class of each device id on each platform. */
	for (platform = 0U; platform < 4U; platform++) {
		for (device = 0U; device < 16U; device++)
			fprintf(out, "sku %d %04x %d\n", platforms[platform], devices[device], drv_i915_skl_sku(devices[device], platforms[platform]));
	}

	/* Each class, platform, output kind and port: the entries, and the HDMI default. */
	for (sku = I915_SKL_SKU_H; sku <= I915_SKL_SKU_ULX; sku++) {
		for (platform = 0U; platform < 4U; platform++) {
			fprintf(out, "hdmi_level %d %d %u\n", sku, platforms[platform], drv_i915_skl_hdmi_level(sku, platforms[platform]));
			for (output = I915_SKL_OUTPUT_DP; output <= I915_SKL_OUTPUT_EDP; output++) {
				for (port = 0U; port < 5U; port++) {
					count = 0U;
					table = drv_i915_skl_buf_trans(sku, platforms[platform], (int)output, port, &count);
					fprintf(out, "buf_trans %d %d %u %u %u\n", sku, platforms[platform], output, port, count);
					for (entry = 0U; table != NULL && entry < count; entry++)
						fprintf(out, "  %u %08x %08x %u\n", entry, table[entry].trans1, table[entry].trans2, table[entry].iboost);
				}
			}
		}
	}

	/* The table entry of every training set, and the highest swing of every table size. */
	for (set = 0U; set < 256U; set++)
		fprintf(out, "signal_level %02x %u\n", set, drv_i915_skl_signal_level((uint8_t)set));
	for (count = 0U; count <= I915_SKL_BUF_TRANS_MAX + 1U; count++)
		fprintf(out, "voltage_max %u %u\n", count, drv_i915_skl_voltage_max(count));
}

/* Writes the training decisions on pseudo-random link status bytes. */
static void
i915_table_training(
	FILE *out)
{
	uint8_t status[6];
	unsigned sample;
	unsigned index;
	unsigned lanes;
	unsigned voltage_max;

	/* Each sample's six bytes, at each lane count and swing limit. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES; sample++) {
		for (index = 0U; index < 6U; index++)
			status[index] = (uint8_t)i915_table_random();
		for (lanes = 1U; lanes <= 4U; lanes *= 2U) {
			for (voltage_max = 2U; voltage_max <= 3U; voltage_max++) {
				fprintf(out, "train %02x%02x%02x%02x%02x%02x %u %u %02x %d %d\n",
				    status[0],
				    status[1],
				    status[2],
				    status[3],
				    status[4],
				    status[5],
				    lanes,
				    voltage_max,
				    drv_i915_skl_adjust_train(status, lanes, (uint8_t)voltage_max),
				    drv_i915_skl_dp_cr_ok(status, lanes),
				    drv_i915_skl_dp_eq_ok(status, lanes));
			}
		}
	}
}

/* Writes the DDB split and watermarks of pseudo-random sets of lit pipes. */
static void
i915_table_watermarks(
	FILE *out)
{
	static const uint32_t widths[6] = { 640U, 1280U, 1920U, 2560U, 3840U, 4096U };
	struct i915_skl_pipe_wm pipes[3];
	uint32_t width;
	unsigned sample;
	unsigned pipe;
	unsigned latency;
	int error;

	/* Each sample: every pipe lit or dark, its mode's width, total and clock, the plane as wide as the mode. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES; sample++) {
		memset(pipes, 0, sizeof(pipes));
		for (pipe = 0U; pipe < 3U; pipe++) {
			if ((i915_table_random() % 3U) == 0U)
				continue;
			width = widths[i915_table_random() % 6U];
			pipes[pipe].active = 1;
			pipes[pipe].hdisplay = width;
			pipes[pipe].htotal = width + 32U + i915_table_random() % 600U;
			pipes[pipe].pixel_khz = 25000U + i915_table_random() % 575000U;
			pipes[pipe].plane_width = width;
		}

		/* The computation on one of the latency sets. */
		latency = sample & 1U;
		error = drv_i915_skl_wm_compute(pipes, i915_table_latencies[latency]);
		i915_table_print_wm(out, sample, error, pipes);
	}
}

/* Writes the register words of watermark levels and DDB entries. */
static void
i915_table_wm_words(
	FILE *out)
{
	struct i915_skl_wm_level level;
	uint32_t start;
	uint32_t end;
	unsigned sample;

	/* Each sample's level. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		memset(&level, 0, sizeof(level));
		level.enable = (int)(i915_table_random() & 1U);
		level.blocks = i915_table_random() % 1100U;
		level.lines = i915_table_random() % 40U;
		level.min_ddb = i915_table_random() % 1100U;
		fprintf(out, "wm_word %d %u %u %u %08x\n", level.enable, level.blocks, level.lines, level.min_ddb, drv_i915_skl_wm_word(&level));
	}

	/* Each sample's DDB entry, empty ones included. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		start = i915_table_random() % 900U;
		end = start + i915_table_random() % 64U;
		fprintf(out, "ddb_word %u %u %08x\n", start, end, drv_i915_skl_ddb_word(start, end));
	}
}

/* Writes the latencies decoded from pseudo-random PCODE words, with and without IPC. */
static void
i915_table_latency(
	FILE *out)
{
	uint16_t latency[8];
	uint32_t low;
	uint32_t high;
	unsigned sample;
	int ipc;

	/* Each sample's two words, both ways. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		low = i915_table_random();
		high = i915_table_random();
		if ((sample % 5U) == 0U)
			low &= 0xffU;
		for (ipc = 0; ipc <= 1; ipc++) {
			memset(latency, 0, sizeof(latency));
			drv_i915_skl_wm_latency(low, high, ipc, latency);
			fprintf(out, "latency %08x %08x %d %u %u %u %u %u %u %u %u\n",
			    low,
			    high,
			    ipc,
			    latency[0],
			    latency[1],
			    latency[2],
			    latency[3],
			    latency[4],
			    latency[5],
			    latency[6],
			    latency[7]);
		}
	}
}

/* Writes the backlight PWM's decode and its brightness mapping both ways. */
static void
i915_table_pwm(
	FILE *out)
{
	struct i915_skl_pwm_registers registers;
	struct i915_skl_pwm pwm;
	uint32_t percent;
	uint32_t duty;
	unsigned sample;
	int error;

	/* Each sample: either PCH layout, a control, a period and a duty. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		memset(&registers, 0, sizeof(registers));
		memset(&pwm, 0, sizeof(pwm));
		registers.cnp = (int)(sample & 1U);
		registers.control = i915_table_random() & 0xa0000000U;
		registers.period = i915_table_random();
		if ((sample % 7U) == 0U)
			registers.period &= 0xffffU;
		registers.duty = i915_table_random() & 0xfffffU;
		error = drv_i915_skl_pwm_decode(&registers, &pwm);
		fprintf(out, "pwm %d %08x %08x %08x %d %d %d %u %u %u\n",
		    registers.cnp,
		    registers.control,
		    registers.period,
		    registers.duty,
		    error,
		    pwm.enabled,
		    pwm.active_low,
		    pwm.max,
		    pwm.min,
		    pwm.duty);

		/* The mapping of a usable PWM: each brightness's duty, and the brightness of a few duties. */
		if (error != 0 || pwm.max == 0U)
			continue;
		for (percent = 0U; percent <= 101U; percent += 3U)
			fprintf(out, "  duty %u %u\n", percent, drv_i915_skl_pwm_duty(&pwm, percent));
		for (duty = 0U; duty <= pwm.max + pwm.max / 8U; duty += pwm.max / 16U + 1U)
			fprintf(out, "  percent %u %u\n", duty, drv_i915_skl_pwm_percent(&pwm, duty));
	}
}

/* Writes the readout of the SV7's firmware display with pseudo-random changes to its registers. */
static void
i915_table_readout(
	FILE *out)
{
	struct i915_skl_registers registers;
	struct i915_skl_readout readout;
	uint32_t *words;
	unsigned sample;
	unsigned changes;
	unsigned change;
	unsigned word;
	unsigned count;
	int error;

	/* The SV7 itself. */
	i915_table_sv7(&registers);
	memset(&readout, 0, sizeof(readout));
	error = drv_i915_skl_readout_decode(&registers, &readout);
	i915_table_print_readout(out, 0U, error, &readout);

	/* Each sample: the SV7 with one to three words flipped in one bit, or replaced. */
	words = (uint32_t *)&registers;
	count = (unsigned)(sizeof(registers) / sizeof(uint32_t));
	for (sample = 1U; sample <= I915_TABLE_SAMPLES; sample++) {
		i915_table_sv7(&registers);
		changes = 1U + i915_table_random() % 3U;
		for (change = 0U; change < changes; change++) {
			word = i915_table_random() % count;
			if ((i915_table_random() & 1U) != 0U) {
				words[word] ^= 1U << (i915_table_random() % 32U);
			} else {
				words[word] = i915_table_random();
			}
		}

		/* The decode of the changed registers. */
		memset(&readout, 0, sizeof(readout));
		error = drv_i915_skl_readout_decode(&registers, &readout);
		i915_table_print_readout(out, sample, error, &readout);
	}
}

/* Writes the flip display's window sizes, pixel clocks and port clocks. */
static void
i915_table_flip_numbers(
	FILE *out)
{
	uint32_t bytes;
	uint32_t link_m;
	uint32_t link_n;
	uint32_t port_khz;
	unsigned rate;
	unsigned sample;

	/* The display window of buffers from one page to 64 MiB. */
	for (bytes = 1U; bytes <= 64U * 1024U * 1024U; bytes = bytes * 2U + 4095U)
		fprintf(out, "window %u %u\n", bytes, drv_i915_skl_flip_window_pages(bytes));

	/* The port clock of every DPLL_CTRL1 link rate code. */
	for (rate = 0U; rate < 8U; rate++)
		fprintf(out, "port_khz %u %u\n", rate, drv_i915_skl_port_khz(rate));

	/* The pixel clock of pseudo-random link M and N on the DP port clocks. */
	for (sample = 0U; sample < I915_TABLE_SAMPLES / 4U; sample++) {
		link_m = i915_table_random() & 0xffffffU;
		link_n = i915_table_random() & 0xffffffU;
		if ((sample % 9U) == 0U)
			link_n = 0U;
		port_khz = drv_i915_skl_port_khz(i915_table_random() % 6U);
		fprintf(out, "pixel_khz %u %u %u %u\n", link_m, link_n, port_khz, drv_i915_skl_dp_pixel_khz(link_m, link_n, port_khz));
	}
}

/* Writes one watermark row: the error and each pipe's DDB, levels and line time. */
static void
i915_table_print_wm(
	FILE *out,
	unsigned row,
	int error,
	const struct i915_skl_pipe_wm pipes[3])
{
	const struct i915_skl_pipe_wm *wm;
	unsigned pipe;
	unsigned level;

	/* The row's outcome. */
	fprintf(out, "wm %u %d\n", row, error);

	/* Each pipe that is lit. */
	for (pipe = 0U; pipe < 3U; pipe++) {
		wm = &pipes[pipe];
		if (!wm->active)
			continue;
		fprintf(out, "  pipe %u %u %u %u %u ddb %u %u plane %u %u cursor %u %u linetime %u\n",
		    pipe,
		    wm->hdisplay,
		    wm->htotal,
		    wm->pixel_khz,
		    wm->plane_width,
		    wm->ddb_start,
		    wm->ddb_end,
		    wm->plane_start,
		    wm->plane_end,
		    wm->cursor_start,
		    wm->cursor_end,
		    wm->linetime);
		for (level = 0U; level < 8U; level++) {
			fprintf(out, "    level %u %d %u %u %u\n",
			    level,
			    wm->level[level].enable,
			    wm->level[level].blocks,
			    wm->level[level].lines,
			    wm->level[level].min_ddb);
		}
	}
}

/* Writes one readout row. */
static void
i915_table_print_readout(
	FILE *out,
	unsigned row,
	int error,
	const struct i915_skl_readout *readout)
{
	const char *reason;

	/* A readout the flips accept has no reason. */
	reason = readout->reason;
	if (reason == NULL)
		reason = "-";

	/* Succeeded: every field. */
	fprintf(out, "readout %u %d %s %u %u %u %u %u %u %d %08x %u %d %u %u %u %u %d\n",
	    row,
	    error,
	    reason,
	    readout->lit_pipes,
	    readout->pipe,
	    readout->transcoder,
	    readout->width,
	    readout->height,
	    readout->stride_bytes,
	    readout->rgbx,
	    readout->surf,
	    readout->refresh_millihz,
	    readout->refresh_measured,
	    readout->pixel_khz,
	    readout->htotal,
	    readout->port,
	    readout->dpll,
	    readout->scaled);
}

/*
 * Fills the registers of a Let's Note SV7 (Kaby Lake R) as its firmware
 * leaves them: the 1920x1200 eDP panel on transcoder EDP and pipe A, DDI A
 * clocked by DPLL0 at HBR, the first plane on a linear XRGB8888 surface.
 */
static void
i915_table_sv7(
	struct i915_skl_registers *registers)
{
	memset(registers, 0, sizeof(*registers));

	/* Power wells 1 and 2 up, no DC state, DPLL0 at link rate 1350 for DDI A. */
	registers->power = 0xf0000000U;
	registers->cdclk = 0x000003ffU;
	registers->dpll_ctrl1 = (I915_SKL_LINK_RATE_1350 << 1) | 1U;
	registers->dpll_ctrl2 = 1U;

	/* The eDP transcoder: DP SST on DDI A from pipe A, running, with its totals and link M and N. */
	registers->transcoder_read[I915_SKL_TRANSCODER_EDP] = 1;
	registers->ddi_func[I915_SKL_TRANSCODER_EDP] = 0x80000000U | (2U << 24) | (4U << 12);
	registers->transconf[I915_SKL_TRANSCODER_EDP] = 0xc0000000U;
	registers->htotal[I915_SKL_TRANSCODER_EDP] = ((2080U - 1U) << 16) | (1920U - 1U);
	registers->vtotal[I915_SKL_TRANSCODER_EDP] = ((1235U - 1U) << 16) | (1200U - 1U);
	registers->link_m[I915_SKL_TRANSCODER_EDP] = 299038U;
	registers->link_n[I915_SKL_TRANSCODER_EDP] = 0x80000U;

	/* Transcoders A to C are read and off. */
	registers->transcoder_read[0] = 1;
	registers->transcoder_read[1] = 1;
	registers->transcoder_read[2] = 1;

	/* Pipe A's first plane: on, XRGB8888, linear, 1920x1200, stride 120 units, surface 0. */
	registers->pipe_read[0] = 1;
	registers->pipe_src[0] = ((1920U - 1U) << 16) | (1200U - 1U);
	registers->plane_ctl[0] = 0x80000000U | (4U << 24) | (1U << 13);
	registers->plane_stride[0] = 120U;
	registers->plane_size[0] = ((1200U - 1U) << 16) | (1920U - 1U);

	/* Pipes B and C are read and off. */
	registers->pipe_read[1] = 1;
	registers->pipe_read[2] = 1;
}

/* Reports the next number of the fixed pseudo-random sequence. */
static uint32_t
i915_table_random(void)
{
	/* One step of a 32-bit xorshift. */
	i915_table_seed ^= i915_table_seed << 13;
	i915_table_seed ^= i915_table_seed >> 17;
	i915_table_seed ^= i915_table_seed << 5;

	/* Succeeded: the new state is the number. */
	return i915_table_seed;
}
