/*
 * Copyright © 2006-2017 Intel Corporation
 * Copyright © 2006-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD display rebuild: the CDCLK and combo PLL tables of display/clock.c.
 *
 * Moved without change to any value from the pre-rebuild tree (commit
 * 7e7ff337): the Linux-typed tables from parity/lcd/intel_cdclk_port.c and
 * parity/lcd/intel_dpll_port.c, which were extracted from Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_cdclk.c (sha256
 * e954ff508ced6525c83ced776ba26788cc18a2f8677aa4441697d5f9ccc38c48) and
 * intel_dpll_mgr.c (sha256
 * bc90f5855afccb4ef1c18f07b284c90fb67e27df96cf9d22545768d14abc7b00); and the
 * same CDCLK rows in the display core's own layout (struct i915_cdclk_vals)
 * from parity/cdclk.c.  The notices above are the source files' own.
 *
 * Included once, at file scope, by display/clock.c after the definitions of
 * struct intel_cdclk_vals and struct icl_combo_pll_params.  Every table is
 * constant and shared by every device.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_CLOCK_TABLES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_CLOCK_TABLES_INC

/*
 * icl_cdclk_table[] (intel_cdclk.c): the CDCLK frequencies Tiger Lake takes
 * with tgl_cdclk_funcs, per reference clock, as the modeset text reads them.
 * An empty row ends it.
 */
static const struct intel_cdclk_vals icl_cdclk_table[] = {
	{ .refclk = 19200, .cdclk = 172800, .divider = 2, .ratio = 18 },
	{ .refclk = 19200, .cdclk = 192000, .divider = 2, .ratio = 20 },
	{ .refclk = 19200, .cdclk = 307200, .divider = 2, .ratio = 32 },
	{ .refclk = 19200, .cdclk = 326400, .divider = 4, .ratio = 68 },
	{ .refclk = 19200, .cdclk = 556800, .divider = 2, .ratio = 58 },
	{ .refclk = 19200, .cdclk = 652800, .divider = 2, .ratio = 68 },

	{ .refclk = 24000, .cdclk = 180000, .divider = 2, .ratio = 15 },
	{ .refclk = 24000, .cdclk = 192000, .divider = 2, .ratio = 16 },
	{ .refclk = 24000, .cdclk = 312000, .divider = 2, .ratio = 26 },
	{ .refclk = 24000, .cdclk = 324000, .divider = 4, .ratio = 54 },
	{ .refclk = 24000, .cdclk = 552000, .divider = 2, .ratio = 46 },
	{ .refclk = 24000, .cdclk = 648000, .divider = 2, .ratio = 54 },

	{ .refclk = 38400, .cdclk = 172800, .divider = 2, .ratio =  9 },
	{ .refclk = 38400, .cdclk = 192000, .divider = 2, .ratio = 10 },
	{ .refclk = 38400, .cdclk = 307200, .divider = 2, .ratio = 16 },
	{ .refclk = 38400, .cdclk = 326400, .divider = 4, .ratio = 34 },
	{ .refclk = 38400, .cdclk = 556800, .divider = 2, .ratio = 29 },
	{ .refclk = 38400, .cdclk = 652800, .divider = 2, .ratio = 34 },
	{ .refclk = 0 }
};

/*
 * adlp_cdclk_table[] (intel_cdclk.c): the CDCLK frequencies of Alder Lake-P
 * past display stepping B0, as the modeset text reads them.  An empty row
 * ends it.
 */
static const struct intel_cdclk_vals adlp_cdclk_table[] = {
	{ .refclk = 19200, .cdclk = 172800, .divider = 3, .ratio = 27 },
	{ .refclk = 19200, .cdclk = 192000, .divider = 2, .ratio = 20 },
	{ .refclk = 19200, .cdclk = 307200, .divider = 2, .ratio = 32 },
	{ .refclk = 19200, .cdclk = 556800, .divider = 2, .ratio = 58 },
	{ .refclk = 19200, .cdclk = 652800, .divider = 2, .ratio = 68 },

	{ .refclk = 24000, .cdclk = 176000, .divider = 3, .ratio = 22 },
	{ .refclk = 24000, .cdclk = 192000, .divider = 2, .ratio = 16 },
	{ .refclk = 24000, .cdclk = 312000, .divider = 2, .ratio = 26 },
	{ .refclk = 24000, .cdclk = 552000, .divider = 2, .ratio = 46 },
	{ .refclk = 24000, .cdclk = 648000, .divider = 2, .ratio = 54 },

	{ .refclk = 38400, .cdclk = 179200, .divider = 3, .ratio = 14 },
	{ .refclk = 38400, .cdclk = 192000, .divider = 2, .ratio = 10 },
	{ .refclk = 38400, .cdclk = 307200, .divider = 2, .ratio = 16 },
	{ .refclk = 38400, .cdclk = 556800, .divider = 2, .ratio = 29 },
	{ .refclk = 38400, .cdclk = 652800, .divider = 2, .ratio = 34 },
	{ .refclk = 0 }
};

/*
 * icl_cdclk_table[] in the display core's layout: the table the Tiger Lake
 * CDCLK bring-up programs from.  A row of zero reference clock ends it.
 */
static const struct i915_cdclk_vals i915_icl_cdclk_table[] = {
	{ 19200, 172800, 2, 18, 0 },
	{ 19200, 192000, 2, 20, 0 },
	{ 19200, 307200, 2, 32, 0 },
	{ 19200, 326400, 4, 68, 0 },
	{ 19200, 556800, 2, 58, 0 },
	{ 19200, 652800, 2, 68, 0 },
	{ 24000, 180000, 2, 15, 0 },
	{ 24000, 192000, 2, 16, 0 },
	{ 24000, 312000, 2, 26, 0 },
	{ 24000, 324000, 4, 54, 0 },
	{ 24000, 552000, 2, 46, 0 },
	{ 24000, 648000, 2, 54, 0 },
	{ 38400, 172800, 2,  9, 0 },
	{ 38400, 192000, 2, 10, 0 },
	{ 38400, 307200, 2, 16, 0 },
	{ 38400, 326400, 4, 34, 0 },
	{ 38400, 556800, 2, 29, 0 },
	{ 38400, 652800, 2, 34, 0 },
	{ 0, 0, 0, 0, 0 }
};

/*
 * adlp_cdclk_table[] in the display core's layout: the table the Alder
 * Lake-P CDCLK bring-up programs from (waveform 0: no squash).  A row of
 * zero reference clock ends it.
 */
static const struct i915_cdclk_vals i915_adlp_cdclk_table[] = {
	{ 19200, 172800, 3, 27, 0 },
	{ 19200, 192000, 2, 20, 0 },
	{ 19200, 307200, 2, 32, 0 },
	{ 19200, 556800, 2, 58, 0 },
	{ 19200, 652800, 2, 68, 0 },
	{ 24000, 176000, 3, 22, 0 },
	{ 24000, 192000, 2, 16, 0 },
	{ 24000, 312000, 2, 26, 0 },
	{ 24000, 552000, 2, 46, 0 },
	{ 24000, 648000, 2, 54, 0 },
	{ 38400, 179200, 3, 14, 0 },
	{ 38400, 192000, 2, 10, 0 },
	{ 38400, 307200, 2, 16, 0 },
	{ 38400, 556800, 2, 29, 0 },
	{ 38400, 652800, 2, 34, 0 },
	{ 0, 0, 0, 0, 0 }
};

/*
 * icl_dp_combo_pll_24MHz_values[] (intel_dpll_mgr.c): the combo PLL words
 * of each DP link rate on a 24 MHz reference.  The values are already the
 * bits written to the registers, not the logical dividers.
 */
static const struct icl_combo_pll_params icl_dp_combo_pll_24MHz_values[] = {
	{ 540000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [0]: 5.4 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 270000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [1]: 2.7 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 162000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [2]: 1.62 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 324000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [3]: 3.24 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 216000,
	  { .dco_integer = 0x168, .dco_fraction = 0x0000,		/* [4]: 2.16 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2, }, },
	{ 432000,
	  { .dco_integer = 0x168, .dco_fraction = 0x0000,		/* [5]: 4.32 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 648000,
	  { .dco_integer = 0x195, .dco_fraction = 0x0000,		/* [6]: 6.48 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 810000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [7]: 8.1 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
};

/*
 * icl_dp_combo_pll_19_2MHz_values[] (intel_dpll_mgr.c): the same for a
 * 19.2 MHz reference, also used for 38.4 MHz.
 */
static const struct icl_combo_pll_params icl_dp_combo_pll_19_2MHz_values[] = {
	{ 540000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [0]: 5.4 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 270000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [1]: 2.7 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 162000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [2]: 1.62 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 324000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [3]: 3.24 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 216000,
	  { .dco_integer = 0x1C2, .dco_fraction = 0x0000,		/* [4]: 2.16 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2, }, },
	{ 432000,
	  { .dco_integer = 0x1C2, .dco_fraction = 0x0000,		/* [5]: 4.32 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 648000,
	  { .dco_integer = 0x1FA, .dco_fraction = 0x2000,		/* [6]: 6.48 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 810000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [7]: 8.1 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
};

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_CLOCK_TABLES_INC */
