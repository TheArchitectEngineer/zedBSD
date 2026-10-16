/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_bw.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/skl_watermark.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_wm.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2023 Intel Corporation
 */

/*
 * The DRAM and display bandwidth, SAGV, and the watermark and DDB text
 * (see watermark.h).
 *
 * The driver probe half:
 *
 *   intel_dram_detect()      gen12_get_dram_info() through the PCODE
 *                            memory-subsystem query (ADL-P, graphics 12);
 *   intel_bw_init_hw()       tgl_get_bw_info() with the ADL-P system agent
 *                            parameters and icl_get_qgv_points(), into the
 *                            device's bandwidth table and SAGV status;
 *   intel_bw_init()          the bandwidth global object and, on display
 *                            11..13 with SAGV, the forced SAGV disable
 *                            through the PCODE QGV restriction;
 *   intel_pmdemand_init()    the PM demand global object.
 *
 * The modeset half takes the screen's plan (xd-screen.c) as the state of
 * one pipe with one visible plane: the plane's watermarks and its part of
 * the display buffer, the DBUF state of an enable and of a disable, and
 * the watermark and buffer writes through the xd watermarks.
 *
 * The Linux text found the watermark context -- the old and new DBUF states
 * and the crtc state being worked on -- through a file-scope pointer, and
 * the readout's crtcs through the takeover registry's file-scope accessors.
 * Here both are arguments: the context is the modeset object's wm member,
 * the registry is the takeover world.
 */

#include "internal.h"
#include "modeset-internal.h"
#include "watermark-internal.h"
#include "takeover-internal.h"
#include "watermark.h"
#include "state.h"
#include "edid.h"
#include "pipe.h"
#include "plane.h"
#include "xd-dbuf.h"
#include "xd-io.h"
#include "xd-platform.h"
#include "xd-screen.h"

#include "../intel/display-regs-xd.h"
#include "../mmio.h"
#include "../power.h"
#include <kern/kcrt.h>

#include <kern/klog.h>
#include <kern/kmem.h>

#include <uapi/errno.h>
#include <limits.h>
#include <stddef.h>

/*
 * The SAGV / QGV PCODE mailbox (i915_reg.h): the request and the fields of
 * its request and reply words.
 */
#define ICL_PCODE_SAGV_DE_MEM_SS_CONFIG   0xeu

/* REG_GENMASK(1, 0) / REG_FIELD_PREP(mask, 0) */
#define ICL_PCODE_REP_QGV_MASK            0x3u
#define ICL_PCODE_REP_QGV_SAFE            0x0u

/* REG_GENMASK(3, 2) / REG_FIELD_PREP(mask, 0) */
#define ADLS_PCODE_REP_PSF_MASK           0xcu
#define ADLS_PCODE_REP_PSF_SAFE           0x0u

/* REG_GENMASK(7, 0) / REG_GENMASK(10, 8) with REG_FIELD_PREP */
#define ICL_PCODE_REQ_QGV_PT_MASK         0xffu
#define ICL_PCODE_REQ_QGV_PT(x)           ((((unsigned)(x)) << 0) & ICL_PCODE_REQ_QGV_PT_MASK)
#define ADLS_PCODE_REQ_PSF_PT_MASK        0x700u
#define ADLS_PCODE_REQ_PSF_PT(x)          ((((unsigned)(x)) << 8) & ADLS_PCODE_REQ_PSF_PT_MASK)

/* Wa_14016740474 (display version 14 A0..C0 only; never taken on ADL-P). */
#define XELPD_CHICKEN_DCPR_3              0x46434u
#define DMD_RSP_TIMEOUT_DISABLE           (1u << 19)

/* The display stepping index of STEP_C0 (STEP_A0 is 0). */
#define I915_WM_STEP_C0 8

/*
 * The PCODE memory-subsystem requests (ICL_PCODE_MEM_SUBSYSYSTEM_INFO with
 * its subcommand): the global information, one QGV point, the PSF GV points.
 */
#define I915_DRAM_REQ_GLOBAL_INFO     0x0000000du
#define I915_DRAM_REQ_QGV_POINT(pt)   (((uint32_t)(pt) << 16) | 0x0000010du)
#define I915_DRAM_REQ_PSF_GV_INFO     0x0000020du

/* The ADL-P system agent parameters (adlp_sa_info). */
#define I915_ADLP_SA_DEBURST          16
#define I915_ADLP_SA_DEPROGBWLIMIT    38
#define I915_ADLP_SA_DISPLAYRTIDS     256
#define I915_ADLP_SA_DERATING         20

/* The IPQ depth of the PCH (ipqdepthpch). */
#define I915_BW_IPQDEPTHPCH           16

/*
 * One QGV point as the PCODE reports it: the DRAM clock and timings.
 */
struct i915_qgv_point {
	unsigned dclk;
	unsigned t_rp;
	unsigned t_rdpre;
	unsigned t_rc;
	unsigned t_ras;
	unsigned t_rcd;
};

/*
 * One PSF GV point: its clock.
 */
struct i915_psf_gv_point {
	unsigned clk;
};

/*
 * The memory subsystem as the bandwidth calculation sees it: the QGV and
 * PSF points and the DRAM geometry (the Linux struct intel_qgv_info).
 */
struct i915_qgv_info {
	struct i915_qgv_point points[I915_NUM_QGV_POINTS];
	struct i915_psf_gv_point psf_points[I915_NUM_PSF_POINTS];
	unsigned num_points;
	unsigned num_psf_points;
	unsigned t_bl;
	unsigned max_numchannels;
	unsigned channel_width;
	unsigned deinterleave;
};

/*
 * The parameters one plane's watermark levels are computed from (the Linux
 * struct skl_wm_params).
 */
struct skl_wm_params {
	bool x_tiled, y_tiled;
	bool rc_surface;
	bool is_planar;
	u32 width;
	u8 cpp;
	u32 plane_pixel_rate;
	u32 y_min_scanlines;
	u32 plane_bytes_per_line;
	uint_fixed_16_16_t plane_blocks_per_line;
	uint_fixed_16_16_t y_tile_minimum;
	u32 linetime_us;
	u32 dbuf_block_size;
};

/*
 * The walk that hands out a pipe's DDB to its planes (the Linux struct
 * skl_plane_ddb_iter): what is left of the data rate and of the blocks.
 */
struct skl_plane_ddb_iter {
	u64 data_rate;
	u16 start, size;
};

/*
 * The duplicate and destroy hooks of the bandwidth and PM demand global
 * objects: empty, as the atomic commit is not ported (see state.c).
 */
static const struct i915_global_state_funcs i915_bw_funcs = { 0, 0 };
static const struct i915_global_state_funcs i915_pmdemand_funcs = { 0, 0 };

static unsigned i915_genmask_low(unsigned num);
static int i915_is_power_of_2(unsigned n);
static uint32_t i915_wm_rmw(struct i915_mmio *mmio, uint32_t reg, uint32_t clear, uint32_t set);
static int i915_icl_get_qgv_points(struct mutex *sb_lock, struct i915_mmio *mmio, const struct i915_dram_info *di, struct i915_qgv_info *qi, int is_y_tile);
static unsigned i915_sagv_max_dclk(const struct i915_qgv_info *qi);
static unsigned i915_icl_max_bw_index(const struct i915_bw_state *bw, int num_planes, int qgv_point);
static unsigned i915_tgl_max_bw_index(const struct i915_bw_state *bw, int num_planes, int qgv_point);
static unsigned i915_icl_qgv_bw(const struct i915_bw_state *bw, int display_ver, int num_active_planes, int qgv_point);
static unsigned i915_adl_psf_bw(const struct i915_bw_state *bw, int psf_gv_point);
static uint16_t i915_icl_prepare_qgv_points_mask(const struct i915_bw_state *bw, unsigned qgv_points, unsigned psf_points);
static int i915_is_sagv_enabled(const struct i915_bw_state *bw, uint16_t points_mask);
static void i915_icl_force_disable_sagv(struct i915_display_state *d, struct mutex *sb_lock, struct i915_mmio *mmio, int display_ver, struct i915_bw_state *bw, struct i915_bw_obj_state *bw_obj);

/*
 * Creates the watermark world of a display.
 *
 * The world starts zeroed: no DBUF state is published and no watermark
 * context is bound.  Returns 0, or ENOMEM.
 */
int
drv_i915_wm_world_create(
	struct i915_display *display)
{
	struct i915_wm_world *world;

	/* Allocates the world cleared. */
	world = kern_calloc(1, sizeof(*world));
	if (world == NULL)
		return ENOMEM;

	/* The display owns the world until the destroy; the world knows its display. */
	world->display = display;
	display->wm_world = world;

	/* Succeeded: the display has its watermark world. */
	return 0;
}

/*
 * Destroys the watermark world of a display.
 */
void
drv_i915_wm_world_destroy(
	struct i915_display *display)
{
	/* A display without a world has nothing to free. */
	if (display->wm_world == NULL)
		return;

	/* Frees the world; the display no longer has one. */
	kern_free(display->wm_world);
	display->wm_world = NULL;
}

/*
 * Decodes the PCODE global memory information (the gen12 decode of
 * icl_pcode_read_mem_global_info()).
 *
 * Returns 0, or EINVAL for a DRAM type the decode does not know; the caller
 * tolerates the failure.
 */
int
drv_i915_dram_decode(
	uint32_t val,
	struct i915_dram_info *di)
{
	/* The DRAM type is in bits 3:0. */
	switch (val & 0xfu) {
	case 0:
		di->type = I915_DRAM_DDR4;
		break;
	case 1:
		di->type = I915_DRAM_DDR5;
		break;
	case 2:
		di->type = I915_DRAM_LPDDR5;
		break;
	case 3:
		di->type = I915_DRAM_LPDDR4;
		break;
	case 4:
		di->type = I915_DRAM_DDR3;
		break;
	case 5:
		di->type = I915_DRAM_LPDDR3;
		break;
	default:
		di->type = I915_DRAM_UNKNOWN;
		kern_logf("i915: dram: unknown type field 0x%x (tolerated)\n", val & 0xfu);
		return EINVAL;
	}

	/* The channels, the QGV points and the PSF GV points. */
	di->num_channels = (val & 0xf0u) >> 4;
	di->num_qgv_points = (val & 0xf00u) >> 8;
	di->num_psf_gv_points = (val & 0x3000u) >> 12;
	di->valid = 1;

	/* Logs what was decoded. */
	kern_logf("i915: dram: raw=0x%08x type=%d channels=%u qgv_points=%u psf_gv_points=%u wm_lv0_adjust=%d\n",
		val, di->type, di->num_channels, di->num_qgv_points,
		di->num_psf_gv_points, di->wm_lv_0_adjust_needed);

	/* Succeeded: the DRAM information is valid. */
	return 0;
}

/*
 * Reads the DRAM information from the PCODE (gen12_get_dram_info() through
 * icl_pcode_read_mem_global_info()).
 *
 * Returns 0, the PCODE error, or the decode error; the caller tolerates a
 * failure.
 */
int
drv_i915_dram_detect(
	struct mutex *sb_lock,
	struct i915_mmio *mmio,
	struct i915_dram_info *di)
{
	uint32_t val;
	int error;

	/*
	 * detect_mem_freq() has no ADL-P branch.  intel_dram_detect() (graphics
	 * 12, not DG2, a display present) sets wm_lv_0_adjust_needed
	 * (!IS_GEN9_LP), and gen12_get_dram_info() clears it again.
	 */
	di->wm_lv_0_adjust_needed = 1;
	di->wm_lv_0_adjust_needed = 0;

	/* Reads the global memory information. */
	val = 0;
	error = drv_i915_pcode_read(sb_lock, mmio, I915_DRAM_REQ_GLOBAL_INFO, &val, NULL);
	if (error != 0) {
		kern_logf("i915: dram: global-info pcode failed err=%d (tolerated)\n", error);
		return error;
	}

	/* Decodes it. */
	error = drv_i915_dram_decode(val, di);
	if (error != 0)
		return error;

	/* Succeeded: the DRAM information is decoded. */
	return 0;
}

/*
 * Computes the display bandwidth table and the SAGV status (intel_bw_init_hw()
 * with tgl_get_bw_info(&adlp_sa_info) on ADL-P).
 *
 * Returns 0, or the PCODE error of the QGV query, in which case the
 * bandwidth limits are ignored.  A QGV point with a non-positive row cycle
 * time leaves its entries zero and the table not valid.
 */
int
drv_i915_bw_init_hw(
	struct mutex *sb_lock,
	struct i915_mmio *mmio,
	const struct i915_dram_info *di,
	struct i915_bw_state *bw)
{
	const int is_y_tile = 1;
	struct i915_qgv_info qi;
	struct i915_bw_group *bi;
	struct i915_bw_group *bi_next;
	const struct i915_qgv_point *sp;
	int num_channels;
	int ipqdepth;
	int dclk_max;
	int maxdebw;
	int peakbw;
	int clperchgroup;
	int clpchgroup;
	int ct;
	int bw_j;
	int divisor;
	int incomputable;
	int i;
	int j;
	int error;

	/* Starts from an empty memory description and at least one channel. */
	incomputable = 0;
	kern_memset(&qi, 0, sizeof(qi));
	if (di->num_channels < 1u) {
		num_channels = 1;
	} else {
		num_channels = (int)di->num_channels;
	}

	/* Reads the QGV and PSF points. */
	error = i915_icl_get_qgv_points(sb_lock, mmio, di, &qi, is_y_tile);
	if (error != 0) {
		kern_logf("i915: bw: failed to get memory subsystem info, ignoring bandwidth limits (err=%d)\n", error);
		return error;
	}

	/* DISPLAY_VER < 14 and LPDDR4/5: the channel count doubles. */
	if (di->type == I915_DRAM_LPDDR4 || di->type == I915_DRAM_LPDDR5)
		num_channels *= 2;

	/* A deinterleave the DRAM type left open follows from the channels. */
	if (qi.deinterleave == 0u) {
		divisor = is_y_tile ? 4 : 2;
		qi.deinterleave = (unsigned)DIV_ROUND_UP(num_channels, divisor);
	}

	/* Display version 12+: fewer channels than the maximum halve the deinterleave. */
	if (num_channels < (int)qi.max_numchannels)
		qi.deinterleave = (unsigned)max(DIV_ROUND_UP((int)qi.deinterleave, 2), 1);

	/* Display version 12+: more channels than the maximum are reported. */
	if (num_channels > (int)qi.max_numchannels)
		kern_logf("i915: bw: number of channels exceeds max number of channels\n");

	/* The channels count up to the maximum. */
	if (qi.max_numchannels != 0u)
		num_channels = min(num_channels, (int)qi.max_numchannels);

	/* The peak and the deprogrammed bandwidth, the IPQ depth and the cachelines per channel group. */
	dclk_max = (int)i915_sagv_max_dclk(&qi);
	peakbw = num_channels * DIV_ROUND_UP((int)qi.channel_width, 8) * dclk_max;
	maxdebw = min(I915_ADLP_SA_DEPROGBWLIMIT * 1000, peakbw * 6 / 10);   /* 60% */
	ipqdepth = min(I915_BW_IPQDEPTHPCH, I915_ADLP_SA_DISPLAYRTIDS / num_channels);
	clperchgroup = 4 * DIV_ROUND_UP(8, num_channels) * (int)qi.deinterleave;

	/* Fills every bandwidth group. */
	for (i = 0; i < I915_BW_GROUPS; i++) {
		bi = &bw->max[i];
		clpchgroup = (I915_ADLP_SA_DEBURST * (int)qi.deinterleave / num_channels) << i;

		/* num_planes belongs to the NEXT group (max[i + 1]), as in Linux. */
		if (i < I915_BW_GROUPS - 1) {
			bi_next = &bw->max[i + 1];

			/* A group of fewer cachelines than a channel group holds more planes. */
			if (clpchgroup < clperchgroup) {
				bi_next->num_planes = (ipqdepth - clpchgroup) / clpchgroup + 1;
			} else {
				bi_next->num_planes = 0;
			}
		}

		/* The group sees every QGV and PSF point. */
		bi->num_qgv_points = qi.num_points;
		bi->num_psf_gv_points = qi.num_psf_points;

		/* The derated and the peak bandwidth of each QGV point. */
		for (j = 0; j < (int)qi.num_points; j++) {
			sp = &qi.points[j];
			ct = max((int)sp->t_rc,
				(int)sp->t_rp + (int)sp->t_rcd +
				(clpchgroup - 1) * (int)qi.t_bl + (int)sp->t_rdpre);

			/*
			 * Input check (an explicit deviation from Linux, which
			 * trusts non-zero hardware timings): a non-positive row
			 * cycle time cannot yield a valid entry, so the entry is
			 * left zero rather than fabricated.
			 */
			if (ct <= 0) {
				bi->deratedbw[j] = 0u;
				bi->peakbw[j] = 0u;
				incomputable = 1;
				kern_logf("i915: bw: BW%d/QGV%d invalid timing ct=%d (input anomaly)\n",
					i, j, ct);
				continue;
			}

			/* The raw bandwidth, derated and capped, and the peak. */
			bw_j = DIV_ROUND_UP((int)sp->dclk * clpchgroup * 32 * num_channels, ct);
			bi->deratedbw[j] = (unsigned)min(maxdebw, bw_j * (100 - I915_ADLP_SA_DERATING) / 100);
			bi->peakbw[j] = (unsigned)DIV_ROUND_CLOSEST((int)sp->dclk * num_channels * (int)qi.channel_width, 8);
			kern_logf("i915: bw: BW%d/QGV%d num_planes=%u deratedbw=%u peakbw=%u\n",
				i, j, bi->num_planes, bi->deratedbw[j], bi->peakbw[j]);
		}

		/* The bandwidth of each PSF GV point. */
		for (j = 0; j < (int)qi.num_psf_points; j++) {
			bi->psf_bw[j] = (unsigned)DIV_ROUND_CLOSEST(64 * (int)qi.psf_points[j].clk * 100, 6);
			kern_logf("i915: bw: BW%d/PSF%d num_planes=%u bw=%u\n",
				i, j, bi->num_planes, bi->psf_bw[j]);
		}
	}

	/* One QGV point means SAGV is disabled in the BIOS: it cannot be controlled. */
	if (qi.num_points == 1u) {
		bw->sagv_status = (int)I915_SAGV_NOT_CONTROLLED;
	} else {
		bw->sagv_status = (int)I915_SAGV_ENABLED;
	}

	/* An incomputable QGV timing leaves the table not fully valid. */
	if (incomputable != 0) {
		bw->valid = 0;
		kern_logf("i915: bw: table has incomputable entries; bw.valid=0\n");
	} else {
		bw->valid = 1;
	}

	/* Logs the SAGV status and the points. */
	kern_logf("i915: bw: sagv_status=%d num_qgv_points=%u num_psf_points=%u\n",
		bw->sagv_status, qi.num_points, qi.num_psf_points);

	/* Succeeded: the bandwidth table is computed. */
	return 0;
}

/*
 * Tells whether the display controls SAGV (the Linux intel_has_sagv()).
 *
 * HAS_SAGV() is display version 9+ outside the LP parts, and the PCODE must
 * not have reported SAGV as out of the driver's control.
 */
int
drv_i915_has_sagv(
	int display_ver,
	int legacy_platform,
	int sagv_status)
{
	/* Before display version 9 there is no SAGV. */
	if (display_ver < 9)
		return 0;

	/* IS_LP(): Broxton has none. */
	if (legacy_platform == I915_PLAT_BROXTON)
		return 0;

	/* A SAGV the BIOS disabled is not controlled. */
	if (sagv_status == I915_SAGV_NOT_CONTROLLED)
		return 0;

	/* Succeeded: the display controls SAGV. */
	return 1;
}

/*
 * Reports the mask of every QGV and PSF point the PCODE advertised
 * (icl_qgv_points_mask()).
 *
 * The whole ADLS_QGV_PT_MASK cannot be used: the PCode rejects a mask that
 * names an unadvertised point.
 */
uint16_t
drv_i915_icl_qgv_points_mask(
	const struct i915_bw_state *bw)
{
	unsigned num_psf_gv_points;
	unsigned num_qgv_points;
	uint16_t qgv_points;
	uint16_t psf_points;

	/* The points the PCODE reported. */
	num_psf_gv_points = bw->max[0].num_psf_gv_points;
	num_qgv_points = bw->max[0].num_qgv_points;
	qgv_points = 0u;
	psf_points = 0u;

	/* One bit per advertised QGV point. */
	if (num_qgv_points > 0u)
		qgv_points = (uint16_t)i915_genmask_low(num_qgv_points);

	/* One bit per advertised PSF point. */
	if (num_psf_gv_points > 0u)
		psf_points = (uint16_t)i915_genmask_low(num_psf_gv_points);

	/* Succeeded: reports both fields in the PCODE request layout. */
	return (uint16_t)(ICL_PCODE_REQ_QGV_PT(qgv_points) | ADLS_PCODE_REQ_PSF_PT(psf_points));
}

/*
 * Reports the derated bandwidth of a QGV point for a number of active
 * planes (icl_qgv_bw(), for the modeset's bandwidth check).
 */
unsigned
drv_i915_icl_qgv_bw(
	const struct i915_bw_state *bw,
	int display_ver,
	int num_active_planes,
	int qgv_point)
{
	unsigned bandwidth;

	/* Looks the bandwidth up in the table. */
	bandwidth = i915_icl_qgv_bw(bw, display_ver, num_active_planes, qgv_point);

	/* Succeeded: reports the bandwidth. */
	return bandwidth;
}

/*
 * Reports the QGV point with the most bandwidth (icl_max_bw_qgv_point_mask()).
 *
 * The point is needed to disable SAGV when the watermarks exceed the SAGV
 * block time.
 */
unsigned
drv_i915_icl_max_bw_qgv_point_mask(
	const struct i915_bw_state *bw,
	int display_ver,
	int num_active_planes)
{
	unsigned num_qgv_points;
	unsigned max_bw_point;
	unsigned max_bw;
	unsigned max_data_rate;
	unsigned i;

	/* No point is chosen yet. */
	num_qgv_points = bw->max[0].num_qgv_points;
	max_bw_point = 0u;
	max_bw = 0u;

	/* Keeps the first point with the largest bandwidth. */
	for (i = 0u; i < num_qgv_points; i++) {
		max_data_rate = i915_icl_qgv_bw(bw, display_ver, num_active_planes, (int)i);
		if (max_data_rate > max_bw) {
			max_bw_point = (1u << i);
			max_bw = max_data_rate;
		}
	}

	/* Succeeded: reports the point's bit. */
	return max_bw_point;
}

/*
 * Reports the PSF GV points with the most bandwidth
 * (icl_max_bw_psf_gv_point_mask()); a tie keeps every tied point.
 */
unsigned
drv_i915_icl_max_bw_psf_gv_point_mask(
	const struct i915_bw_state *bw)
{
	unsigned num_psf_gv_points;
	unsigned max_bw_point_mask;
	unsigned max_bw;
	unsigned max_data_rate;
	unsigned i;

	/* No point is chosen yet. */
	num_psf_gv_points = bw->max[0].num_psf_gv_points;
	max_bw_point_mask = 0u;
	max_bw = 0u;

	/* Keeps the points of the largest bandwidth. */
	for (i = 0u; i < num_psf_gv_points; i++) {
		max_data_rate = i915_adl_psf_bw(bw, (int)i);
		if (max_data_rate > max_bw) {
			max_bw_point_mask = (1u << i);
			max_bw = max_data_rate;
		} else if (max_data_rate == max_bw) {
			max_bw_point_mask |= (1u << i);
		}
	}

	/* Succeeded: reports the points' bits. */
	return max_bw_point_mask;
}

/*
 * Restricts the QGV and PSF points the memory may use
 * (icl_pcode_restrict_qgv_points()).
 *
 * Display version 14 and later handle SAGV through the PM demand, so this
 * is a no-op there.  Bspec asks for retries during at least 1 ms.  On
 * success the SAGV status follows the mask.  Returns 0 or the PCODE error.
 */
int
drv_i915_icl_pcode_restrict_qgv_points(
	struct mutex *sb_lock,
	struct i915_mmio *mmio,
	int display_ver,
	struct i915_bw_state *bw,
	uint32_t points_mask)
{
	int error;
	int enabled;

	/* Display version 14+ restricts the points through the PM demand. */
	if (display_ver >= 14)
		return 0;

	/* Asks the PCODE for the restriction until it reports both fields safe. */
	error = drv_i915_skl_pcode_request(sb_lock, mmio, ICL_PCODE_SAGV_DE_MEM_SS_CONFIG,
		points_mask,
		ICL_PCODE_REP_QGV_MASK | ADLS_PCODE_REP_PSF_MASK,
		ICL_PCODE_REP_QGV_SAFE | ADLS_PCODE_REP_PSF_SAFE,
		1);
	if (error != 0) {
		kern_logf("i915: Failed to disable qgv points (%d) points: 0x%x\n",
			error, points_mask);
		return error;
	}

	/* SAGV stays enabled unless the mask leaves exactly one QGV point. */
	enabled = i915_is_sagv_enabled(bw, (uint16_t)points_mask);
	if (enabled != 0) {
		bw->sagv_status = I915_SAGV_ENABLED;
	} else {
		bw->sagv_status = I915_SAGV_DISABLED;
	}

	/* Succeeded: the points are restricted. */
	return 0;
}

/*
 * Creates the bandwidth global object (intel_bw_init()).
 *
 * The limit applies only with SAGV; from display version 14 SAGV is handled
 * through PM demand requests, so the forced SAGV disable runs on display
 * versions 11..13 only.  Returns 0, or the error of the state allocation.
 */
int
drv_i915_bw_init(
	struct i915_display_state *d,
	int display_ver,
	struct i915_bw_state *bw,
	struct mutex *sb_lock,
	struct i915_mmio *mmio)
{
	int error;
	int has_sagv;

	/* Allocates the bandwidth state. */
	error = drv_i915_alloc_global_state(d, &d->bw_obj_state, (unsigned)sizeof(d->bw_obj_state));
	if (error != 0) {
		d->fail_where = "intel_bw_init";
		return error;
	}

	/* Registers the object on the global list and names it. */
	drv_i915_atomic_global_obj_init(d, &d->bw_obj, &d->bw_obj_state.base, &i915_bw_funcs);
	d->bw_obj.name = "bw";

	/* Forces SAGV off on display versions 11..13 when the display controls it. */
	has_sagv = drv_i915_has_sagv(display_ver, I915_PLAT_NONE, bw->sagv_status);
	if (has_sagv != 0) {
		if (display_ver >= 11 && display_ver <= 13)
			i915_icl_force_disable_sagv(d, sb_lock, mmio, display_ver, bw, &d->bw_obj_state);
	}

	/* Succeeded: the bandwidth object is registered. */
	return 0;
}

/*
 * Creates the PM demand global object (intel_pmdemand_init()).
 *
 * Wa_14016740474 (IS_DISPLAY_IP_STEP(i915, IP_VER(14, 0), STEP_A0,
 * STEP_C0)) applies to display version 14 only and so never on ADL-P.
 * Returns 0, or the error of the state allocation.
 */
int
drv_i915_pmdemand_init(
	struct i915_display_state *d,
	int display_ver,
	struct i915_mmio *mmio,
	int ip_step)
{
	int error;

	/* Allocates the PM demand state. */
	error = drv_i915_alloc_global_state(d, &d->pmdemand_state, (unsigned)sizeof(d->pmdemand_state));
	if (error != 0) {
		d->fail_where = "intel_pmdemand_init";
		return error;
	}

	/* Registers the object on the global list and names it. */
	drv_i915_atomic_global_obj_init(d, &d->pmdemand_obj, &d->pmdemand_state.base, &i915_pmdemand_funcs);
	d->pmdemand_obj.name = "pmdemand";

	/* Wa_14016740474: display version 14, steppings A0 up to (not including) C0. */
	if (display_ver == 14 && ip_step >= 0 && ip_step < I915_WM_STEP_C0) {
		(void)i915_wm_rmw(mmio, XELPD_CHICKEN_DCPR_3, 0u, DMD_RSP_TIMEOUT_DISABLE);
		d->pmdemand_wa_14016740474 = 1;
	}

	/* Succeeded: the PM demand object is registered. */
	return 0;
}

/*
 * Takes the screen's plan as the modeset's new DBUF state: the old one
 * with this pipe and the configuration's other pipes running, the
 * joining, the pipe's slices, the slices powered, the pipe's weight and
 * range.  A change of the
 * slices, the joining or the running pipes marks the state changed, so
 * the commit updates the buffer.  The modeset's watermark context
 * becomes the one the watermark world names as current.
 */
void
drv_i915_lcd_ms_wm_compute(
	struct i915_wm_world *wm_world,
	struct i915_lcd_modeset *ms,
	const struct i915_xd_plan *plan)
{
	struct intel_crtc_state *cs;
	struct intel_dbuf_state *new_dbuf;
	enum pipe pipe;

	/* The crtc state, the new DBUF state and the pipe of the modeset. */
	cs = &ms->crtc_state;
	new_dbuf = &ms->wm.new_dbuf;
	pipe = ms->crtc.pipe;

	/* The watermark context of the commits is this modeset's. */
	wm_world->i915_lcd_wm = &ms->wm;
	ms->wm.crtc_state = cs;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = cs;

	/* The new DBUF state: the old one with the configuration's pipes and this pipe's part. */
	*new_dbuf = ms->wm.old_dbuf;
	new_dbuf->active_pipes = (u8)(ms->wm.old_dbuf.active_pipes | BIT(pipe) | ms->also_active_pipes);
	new_dbuf->joined_mbus = plan->joined != 0U;
	new_dbuf->slices[pipe] = plan->slices;
	new_dbuf->enabled_slices = plan->enabled_slices;
	new_dbuf->weight[pipe] = plan->weight;
	new_dbuf->ddb[pipe].start = plan->pipe_ddb.start;
	new_dbuf->ddb[pipe].end = plan->pipe_ddb.end;

	/* A change of the slices, the joining or the running pipes is to be written. */
	new_dbuf->base.changed = false;
	if (new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes) {
		new_dbuf->base.changed = true;
	}
}

/*
 * Computes the DBUF state of the commit that turns the modeset's crtc off:
 * the joining, the slices and the slices powered with the pipe no longer
 * running.
 *
 * The old state is the device's current one, which another screen's
 * commit may have changed since this screen's enable.  The
 * pipes that stay in the configuration are those the other running
 * screens light or leave room for (keep_pipes): this pipe stays when the
 * screen that still runs left room for it, so that its stop changes
 * neither the slices nor the MBUS joining under that screen's running
 * pipe, and a pipe only room was left for goes once nothing that runs
 * leaves room for it.
 */
void
drv_i915_lcd_ms_wm_compute_off(
	struct i915_wm_world *wm_world,
	struct i915_lcd_modeset *ms,
	unsigned keep_pipes)
{
	const struct i915_xd_platform *platform;
	struct intel_dbuf_state *new_dbuf;
	enum pipe pipe;
	int current_error;

	/* The watermark text works on this modeset's context from now on. */
	wm_world->i915_lcd_wm = &ms->wm;

	/*
	 * The device's current DBUF state is the old one; before any commit
	 * published one (ENOENT, nothing is copied), the screen's own stands.
	 */
	current_error = drv_i915_lcd_dbuf_current(wm_world, &ms->wm.old_dbuf);
	if (current_error != 0)
		I915_LCD_DECIDED(&ms->i915, "DBUF off: no published device state; the screen's own old state is the old one");

	/* The new DBUF state is the old one with only the pipes that stay. */
	new_dbuf = &ms->wm.new_dbuf;
	pipe = ms->crtc.pipe;
	*new_dbuf = ms->wm.old_dbuf;
	new_dbuf->active_pipes = ms->wm.old_dbuf.active_pipes & (u8)keep_pipes;

	/* The joining and the pipe's slices for the pipes that stay. */
	platform = drv_i915_lcd_xd_platform();
	new_dbuf->joined_mbus = drv_i915_xd_dbuf_joined(platform, new_dbuf->active_pipes) != 0U;
	new_dbuf->slices[pipe] = drv_i915_xd_dbuf_slices(platform, (unsigned)pipe, new_dbuf->active_pipes, new_dbuf->joined_mbus ? 1 : 0);

	/* The slices powered for all the pipes; the pipe keeps no weight or range. */
	new_dbuf->enabled_slices = drv_i915_xd_dbuf_enabled(new_dbuf->slices, I915_MAX_PIPES);
	new_dbuf->weight[pipe] = 0;
	kern_memset(&new_dbuf->ddb[pipe], 0, sizeof(new_dbuf->ddb[pipe]));

	/* A change of the slices, the MBUS joining or the active pipes serializes the global state. */
	new_dbuf->base.changed = false;
	if (new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes) {
		new_dbuf->base.changed = true;
	}
}

/*
 * Reports the device's current DBUF state: what the last commit published.
 *
 * The screens commit one after the other, so the state the last commit
 * left is what the next one starts from.  Returns 0, or ENOENT when no
 * commit has published one yet.
 */
int
drv_i915_lcd_dbuf_current(
	struct i915_wm_world *wm_world,
	struct intel_dbuf_state *out)
{
	/*
	 * Nothing is published yet.
	 *
	 * XXX: the old function returned -1 here; ENOENT is the positive error.
	 */
	if (!wm_world->i915_lcd_dbuf_dev_valid)
		return ENOENT;

	/* Copies the published state. */
	*out = wm_world->i915_lcd_dbuf_dev;

	/* Succeeded: the current state is reported. */
	return 0;
}

/*
 * Publishes the DBUF state a commit left as the device's current one.
 */
void
drv_i915_lcd_dbuf_publish(
	struct i915_wm_world *wm_world,
	const struct intel_dbuf_state *now)
{
	/* The next commit starts from this state. */
	wm_world->i915_lcd_dbuf_dev = *now;
	wm_world->i915_lcd_dbuf_dev_valid = 1;
}

/*
 * Forgets the device's DBUF state, as on a fresh device: the next prepare
 * takes the state its caller read from the hardware.
 */
void
drv_i915_lcd_dbuf_forget(
	struct i915_wm_world *wm_world)
{
	/* No state is published any more. */
	kern_memset(&wm_world->i915_lcd_dbuf_dev, 0, sizeof(wm_world->i915_lcd_dbuf_dev));
	wm_world->i915_lcd_dbuf_dev_valid = 0;
}

/* GENMASK(num - 1, 0): the low num bits. */
static unsigned
i915_genmask_low(
	unsigned num)
{
	/* No bits, and every bit. */
	if (num == 0u)
		return 0u;
	if (num >= 32u)
		return 0xffffffffu;

	/* Succeeded: reports the low bits. */
	return (1u << num) - 1u;
}

/* Tells whether n is a power of two (linux/log2.h is_power_of_2()). */
static int
i915_is_power_of_2(
	unsigned n)
{
	/* Zero is no power of two. */
	if (n == 0u)
		return 0;

	/* A power of two has one bit set. */
	if ((n & (n - 1u)) != 0u)
		return 0;

	/* Succeeded: n is a power of two. */
	return 1;
}

/* Clears and sets bits of a register, writing only when the value changes (intel_de_rmw()); reports the old value. */
static uint32_t
i915_wm_rmw(
	struct i915_mmio *mmio,
	uint32_t reg,
	uint32_t clear,
	uint32_t set)
{
	uint32_t old_value;
	uint32_t new_value;

	/* The register's value and its changed value. */
	old_value = drv_i915_read32(mmio, reg);
	new_value = (old_value & ~clear) | set;

	/* Writes only a changed value. */
	if (new_value != old_value)
		drv_i915_write32(mmio, reg, new_value);

	/* Succeeded: reports the value before the change. */
	return old_value;
}

/* Reads the QGV and PSF points from the PCODE (icl_get_qgv_points(), display 12+, Y tiling assumed). */
static int
i915_icl_get_qgv_points(
	struct mutex *sb_lock,
	struct i915_mmio *mmio,
	const struct i915_dram_info *di,
	struct i915_qgv_info *qi,
	int is_y_tile)
{
	uint32_t val;
	uint32_t val2;
	unsigned dclk;
	unsigned i;
	int error;

	/* The number of points the DRAM information reported. */
	qi->num_points = di->num_qgv_points;
	qi->num_psf_points = di->num_psf_gv_points;

	/* The burst length, channels, channel width and deinterleave of the DRAM type. */
	switch (di->type) {
	case I915_DRAM_DDR4:
		qi->t_bl = is_y_tile ? 8u : 4u;
		qi->max_numchannels = 2u;
		qi->channel_width = 64u;
		qi->deinterleave = is_y_tile ? 1u : 2u;
		break;
	case I915_DRAM_DDR5:
		qi->t_bl = is_y_tile ? 16u : 8u;
		qi->max_numchannels = 4u;
		qi->channel_width = 32u;
		qi->deinterleave = is_y_tile ? 1u : 2u;
		break;
	case I915_DRAM_LPDDR4:
		/* ADL-P is not Rocket Lake: the LPDDR5 parameters apply. */
	case I915_DRAM_LPDDR5:
		qi->t_bl = 16u;
		qi->max_numchannels = 8u;
		qi->channel_width = 16u;
		qi->deinterleave = is_y_tile ? 2u : 4u;
		break;
	default:
		qi->t_bl = 16u;
		qi->max_numchannels = 1u;
		break;
	}

	/* No more points than the table holds. */
	if (qi->num_points > I915_NUM_QGV_POINTS)
		qi->num_points = I915_NUM_QGV_POINTS;

	/* Reads the clock and timings of each QGV point. */
	for (i = 0u; i < qi->num_points; i++) {
		val = 0;
		val2 = 0;
		error = drv_i915_pcode_read(sb_lock, mmio, I915_DRAM_REQ_QGV_POINT(i), &val, &val2);
		if (error != 0) {
			kern_logf("i915: bw: could not read QGV %u info err=%d\n", i, error);
			return error;
		}

		/* The clock (16.667 MHz units) and the timings of the point. */
		dclk = val & 0xffffu;
		qi->points[i].dclk = DIV_ROUND_UP(16667u * dclk + 500u, 1000u);
		qi->points[i].t_rp = (val & 0xff0000u) >> 16;
		qi->points[i].t_rcd = (val & 0xff000000u) >> 24;
		qi->points[i].t_rdpre = val2 & 0xffu;
		qi->points[i].t_ras = (val2 & 0xff00u) >> 8;
		qi->points[i].t_rc = qi->points[i].t_rp + qi->points[i].t_ras;
		kern_logf("i915: bw: QGV %u DCLK=%u tRP=%u tRDPRE=%u tRAS=%u tRCD=%u tRC=%u\n",
			i, qi->points[i].dclk, qi->points[i].t_rp, qi->points[i].t_rdpre,
			qi->points[i].t_ras, qi->points[i].t_rcd, qi->points[i].t_rc);
	}

	/* Without PSF points there is nothing more to read. */
	if (qi->num_psf_points == 0u)
		return 0;

	/* Reads the PSF points; the Linux fallback drops them from the calculation when the read fails. */
	val = 0;
	error = drv_i915_pcode_read(sb_lock, mmio, I915_DRAM_REQ_PSF_GV_INFO, &val, NULL);
	if (error != 0) {
		kern_logf("i915: bw: failed to read PSF point data; PSF points dropped\n");
		qi->num_psf_points = 0u;
		return 0;
	}

	/* One clock byte per PSF point. */
	for (i = 0u; i < I915_NUM_PSF_POINTS; i++) {
		qi->psf_points[i].clk = val & 0xffu;
		val >>= 8;
	}

	/* Logs the advertised PSF points. */
	for (i = 0u; i < qi->num_psf_points; i++)
		kern_logf("i915: bw: PSF GV %u CLK=%u\n", i, qi->psf_points[i].clk);

	/* Succeeded: the points are read. */
	return 0;
}

/* The highest DRAM clock of the QGV points (icl_sagv_max_dclk()). */
static unsigned
i915_sagv_max_dclk(
	const struct i915_qgv_info *qi)
{
	unsigned dclk;
	unsigned i;

	/* Keeps the largest clock. */
	dclk = 0;
	for (i = 0u; i < qi->num_points; i++)
		dclk = max(dclk, qi->points[i].dclk);

	/* Succeeded: reports the clock. */
	return dclk;
}

/*
 * The bandwidth group for a plane count on display 11 (icl_max_bw_index()).
 * Not a variant of the version 12 walk: it scans upward, compares with the
 * opposite sense and falls back differently.
 */
static unsigned
i915_icl_max_bw_index(
	const struct i915_bw_state *bw,
	int num_planes,
	int qgv_point)
{
	const struct i915_bw_group *bi;
	int i;

	/* Let's return max bw for 0 planes */
	if (num_planes < 1)
		num_planes = 1;

	/* The first group whose plane count the planes reach. */
	for (i = 0; i < (int)I915_BW_GROUPS; i++) {
		bi = &bw->max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= (int)bi->num_qgv_points)
			return UINT_MAX;

		/* The planes reach the group's plane count. */
		if (num_planes >= (int)bi->num_planes)
			return (unsigned)i;
	}

	/* Succeeded: no group fits. */
	return UINT_MAX;
}

/* The bandwidth group for a plane count on display 12+ (tgl_max_bw_index()); see i915_icl_max_bw_index(). */
static unsigned
i915_tgl_max_bw_index(
	const struct i915_bw_state *bw,
	int num_planes,
	int qgv_point)
{
	const struct i915_bw_group *bi;
	int i;

	/* Let's return max bw for 0 planes */
	if (num_planes < 1)
		num_planes = 1;

	/* The last group whose plane count covers the planes. */
	for (i = (int)I915_BW_GROUPS - 1; i >= 0; i--) {
		bi = &bw->max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= (int)bi->num_qgv_points)
			return UINT_MAX;

		/* The group's plane count covers the planes. */
		if (num_planes <= (int)bi->num_planes)
			return (unsigned)i;
	}

	/* Succeeded: the first group serves the rest. */
	return 0u;
}

/* The derated bandwidth of a QGV point for a plane count (icl_qgv_bw()); 0 when no group fits. */
static unsigned
i915_icl_qgv_bw(
	const struct i915_bw_state *bw,
	int display_ver,
	int num_active_planes,
	int qgv_point)
{
	unsigned idx;

	/* The group of the plane count, by the version's own walk. */
	if (display_ver >= 12) {
		idx = i915_tgl_max_bw_index(bw, num_active_planes, qgv_point);
	} else {
		idx = i915_icl_max_bw_index(bw, num_active_planes, qgv_point);
	}

	/* No group fits. */
	if (idx >= (unsigned)I915_BW_GROUPS)
		return 0u;

	/* Succeeded: reports the group's bandwidth at the point. */
	return bw->max[idx].deratedbw[qgv_point];
}

/* The bandwidth of a PSF GV point (adl_psf_bw()). */
static unsigned
i915_adl_psf_bw(
	const struct i915_bw_state *bw,
	int psf_gv_point)
{
	const struct i915_bw_group *bi;

	/* The PSF bandwidth is the same in every group. */
	bi = &bw->max[0];

	/* Succeeded: reports the point's bandwidth. */
	return bi->psf_bw[psf_gv_point];
}

/* The PCODE mask that restricts the memory to the given points (icl_prepare_qgv_points_mask()). */
static uint16_t
i915_icl_prepare_qgv_points_mask(
	const struct i915_bw_state *bw,
	unsigned qgv_points,
	unsigned psf_points)
{
	uint16_t advertised;

	/* The advertised points. */
	advertised = drv_i915_icl_qgv_points_mask(bw);

	/* Succeeded: masks every advertised point but the given ones. */
	return (uint16_t)(~(ICL_PCODE_REQ_QGV_PT(qgv_points) | ADLS_PCODE_REQ_PSF_PT(psf_points)) & advertised);
}

/* Tells whether SAGV stays enabled under a restriction mask: it does unless exactly one QGV point is left. */
static int
i915_is_sagv_enabled(
	const struct i915_bw_state *bw,
	uint16_t points_mask)
{
	uint16_t advertised;
	int single;

	/* The QGV points the mask leaves. */
	advertised = drv_i915_icl_qgv_points_mask(bw);
	single = i915_is_power_of_2((unsigned)(~points_mask) & advertised & ICL_PCODE_REQ_QGV_PT_MASK);

	/* One point left: SAGV cannot switch. */
	if (single != 0)
		return 0;

	/* Succeeded: SAGV stays enabled. */
	return 1;
}

/* Restricts the memory to its highest-bandwidth points (icl_force_disable_sagv()). */
static void
i915_icl_force_disable_sagv(
	struct i915_display_state *d,
	struct mutex *sb_lock,
	struct i915_mmio *mmio,
	int display_ver,
	struct i915_bw_state *bw,
	struct i915_bw_obj_state *bw_obj)
{
	unsigned qgv_points;
	unsigned psf_points;

	/* The points of the highest bandwidth, and the mask that leaves only them. */
	qgv_points = drv_i915_icl_max_bw_qgv_point_mask(bw, display_ver, 0);
	psf_points = drv_i915_icl_max_bw_psf_gv_point_mask(bw);
	bw_obj->qgv_points_mask = i915_icl_prepare_qgv_points_mask(bw, qgv_points, psf_points);

	/* Records the attempt. */
	d->sagv_qgv_points = qgv_points;
	d->sagv_psf_points = psf_points;
	d->sagv_force_disable_attempted = 1;

	/* Logs the mask. */
	kern_logf("i915: Forcing SAGV disable: mask 0x%x\n",
		(unsigned)bw_obj->qgv_points_mask);

	/* Asks the PCODE for the restriction and records its answer. */
	d->sagv_pcode_ret = drv_i915_icl_pcode_restrict_qgv_points(sb_lock, mmio,
		display_ver, bw, (uint32_t)bw_obj->qgv_points_mask);
}
