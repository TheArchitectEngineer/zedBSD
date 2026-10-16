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
 * The modeset half is the Linux text of skl_watermark.c, intel_bw.c and
 * intel_wm.c for one pipe with one visible plane: the watermark levels, the
 * DBUF slices, the DDB allocation, the watermark and DDB register writes,
 * the MBUS and DBUF updates around a plane update, and the watermark
 * readout of the takeover.  The pieces of skl_compute_wm() and
 * skl_compute_ddb() that iterate over the crtcs of an atomic state are
 * reduced to the one crtc of the modeset (drv_i915_lcd_ms_wm_compute()).
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
static unsigned int i915_intel_bw_crtc_data_rate(const struct intel_crtc_state *crtc_state);
static int i915_intel_bw_crtc_min_cdclk(const struct intel_crtc_state *crtc_state);
static u8 i915_intel_dbuf_enabled_slices(const struct intel_dbuf_state *dbuf_state);
static bool i915_adlp_check_mbus_joined(u8 active_pipes);
static u8 i915_skl_compute_dbuf_slices(struct intel_crtc *crtc, u8 active_pipes, bool join_mbus);
static u16 i915_skl_ddb_entry_init(struct skl_ddb_entry *entry, u16 start, u16 end);
static unsigned int i915_mbus_ddb_offset(struct drm_i915_private *i915, u8 slice_mask);
static bool i915_skl_watermark_ipc_enabled(struct drm_i915_private *i915);
static unsigned int i915_skl_cursor_allocation(const struct intel_crtc_state *crtc_state, int num_active);
static void i915_intel_crtc_dbuf_weights(const struct intel_dbuf_state *dbuf_state, enum pipe for_pipe, unsigned int *weight_start, unsigned int *weight_end, unsigned int *weight_total);
static void i915_wm_xd_pipe(struct drm_i915_private *i915, const struct intel_crtc_state *crtc_state, struct i915_xd_wm_pipe *pipe);
static void i915_wm_level_to_xd(const struct skl_wm_level *from, struct i915_xd_wm_level *to);
static void i915_wm_level_from_xd(const struct i915_xd_wm_level *from, struct skl_wm_level *to);
static void i915_wm_plane_to_xd(const struct skl_plane_wm *from, struct i915_xd_plane_wm *to);
static void i915_wm_plane_from_xd(const struct i915_xd_plane_wm *from, struct skl_plane_wm *to);
static int i915_skl_build_pipe_wm(struct i915_lcd_wm_ctx *wm, struct intel_atomic_state *state, struct intel_crtc *crtc);
static int i915_skl_crtc_allocate_plane_ddb(struct i915_lcd_wm_ctx *wm, struct intel_atomic_state *state, struct intel_crtc *crtc);
static unsigned int i915_intel_crtc_ddb_weight(const struct intel_crtc_state *crtc_state);
static int i915_skl_crtc_allocate_ddb(struct i915_lcd_wm_ctx *wm, struct intel_crtc *crtc);

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
 * Reports the minimum CDCLK the modeset's crtc needs for its pipe read
 * bandwidth (the one crtc's share of intel_bw_calc_min_cdclk()).
 */
int
drv_i915_lcd_ms_bw_min_cdclk(
	struct i915_lcd_modeset *ms)
{
	int min_cdclk;

	/* The "Maximum Pipe Read Bandwidth" of the crtc. */
	min_cdclk = i915_intel_bw_crtc_min_cdclk(&ms->crtc_state);

	/* Succeeded: reports the CDCLK in kHz. */
	return min_cdclk;
}

/*
 * Reports the data rate of the modeset's crtc in MB/s (intel_bw_data_rate()
 * of one crtc, rounded as intel_bw_check_qgv_points() rounds it).
 */
unsigned int
drv_i915_lcd_ms_bw_data_rate(
	struct i915_lcd_modeset *ms)
{
	unsigned int data_rate;

	/* The crtc's data rate in kB/s. */
	data_rate = i915_intel_bw_crtc_data_rate(&ms->crtc_state);

	/* Succeeded: reports it in MB/s, rounded up. */
	return DIV_ROUND_UP(data_rate, 1000);
}

/*
 * Tells whether a plane counts for the watermarks (intel_wm_plane_visible()).
 *
 * A cursor with a framebuffer always counts, since cursor updates can be
 * faster than the refresh rate and the watermark code does not handle that;
 * intel_legacy_cursor_update() throttles the updates that change the fb or
 * the cursor size.
 */
bool
drv_i915_wm_plane_visible(
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane;

	/* The plane of the state. */
	plane = to_intel_plane(plane_state->uapi.plane);

	/* FIXME check the 'enable' instead */
	if (!crtc_state->hw.active)
		return false;

	/* A cursor counts when it has a framebuffer. */
	if (plane->id == PLANE_CURSOR) {
		if (plane_state->hw.fb != NULL)
			return true;

		/* A cursor without a framebuffer does not count. */
		return false;
	}

	/* Succeeded: any other plane counts when it is visible. */
	return plane_state->uapi.visible;
}

/*
 * Computes the watermarks and the DDB of the modeset's crtc (the one crtc's
 * part of skl_compute_wm() and skl_compute_ddb()).
 *
 * The new DBUF state is the old one with this pipe and the configuration's
 * other pipes active; the MBUS joining, the pipe's slices, the enabled
 * slices and the pipe's weight follow, and a change of any of them marks
 * the global state changed (intel_atomic_serialize_global_state()).  The
 * SAGV and bandwidth side (intel_compute_sagv_mask()) is not covered here.
 * The modeset's watermark context becomes the one the watermark world
 * names as current.  Returns 0, or EINVAL when the watermarks or the DDB
 * cannot be met.
 */
int
drv_i915_lcd_ms_wm_compute(
	struct i915_wm_world *wm_world,
	struct i915_lcd_modeset *ms)
{
	struct intel_crtc_state *cs;
	struct intel_dbuf_state *new_dbuf;
	struct i915_lcd_wm_ctx *wm;
	enum pipe pipe;
	int display_ver;
	int error;

	/* The crtc state, the watermark context and the pipe of the modeset. */
	cs = &ms->crtc_state;
	wm = &ms->wm;
	new_dbuf = &ms->wm.new_dbuf;
	pipe = ms->crtc.pipe;

	/* The watermark text works on this modeset's context from now on. */
	wm_world->i915_lcd_wm = wm;

	/* The watermark context works on this crtc state, in the modeset's atomic state. */
	ms->wm.crtc_state = cs;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = cs;

	/* What intel_plane_atomic_check() leaves for a visible plane: its data rates. */
	cs->only_plane = &ms->plane;
	cs->only_plane_state = &ms->plane_state;
	drv_i915_lcd_ms_plane_data_rates(ms);

	/* Builds the pipe's watermark levels. */
	error = i915_skl_build_pipe_wm(wm, &ms->state, &ms->crtc);
	if (error != 0)
		return error;

	/*
	 * skl_compute_ddb(): the new DBUF state starts from the old one, which
	 * knows its atomic state (and through it the device).
	 */
	ms->wm.old_dbuf.base.state = &ms->state;
	*new_dbuf = ms->wm.old_dbuf;

	/*
	 * The whole configuration's pipes: this one and the others it lights
	 * (the Linux atomic state would hold every crtc).
	 */
	new_dbuf->active_pipes = (u8)(ms->wm.old_dbuf.active_pipes | BIT(pipe) | ms->also_active_pipes);

	/* HAS_MBUS_JOINING(): Alder Lake-P and display 14+; a Tiger Lake MBUS is never joined. */
	display_ver = drv_i915_lcd_display_ver();
	if (display_ver >= 13) {
		new_dbuf->joined_mbus = i915_adlp_check_mbus_joined(new_dbuf->active_pipes);
	} else {
		new_dbuf->joined_mbus = false;
	}

	/* The pipe's slices, the enabled slices and the pipe's weight. */
	new_dbuf->slices[pipe] = i915_skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);
	new_dbuf->enabled_slices = i915_intel_dbuf_enabled_slices(new_dbuf);
	new_dbuf->weight[pipe] = i915_intel_crtc_ddb_weight(cs);

	/* A change of the slices, the MBUS joining or the active pipes serializes the global state. */
	new_dbuf->base.changed = false;
	if (new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus) {
		new_dbuf->base.changed = true;
	} else if (new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes) {
		new_dbuf->base.changed = true;
	}

	/* Gives the pipe its share of the DDB. */
	error = i915_skl_crtc_allocate_ddb(wm, &ms->crtc);
	if (error != 0)
		return error;

	/* Hands the pipe's DDB out to its planes. */
	error = i915_skl_crtc_allocate_plane_ddb(wm, &ms->state, &ms->crtc);
	if (error != 0)
		return error;

	/* Succeeded: the watermarks and the DDB are computed. */
	return 0;
}

/*
 * Computes the DBUF state of the commit that turns the modeset's crtc off
 * (the same part of skl_compute_ddb(), with the pipe no longer active).
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
	struct intel_dbuf_state *new_dbuf;
	enum pipe pipe;
	int display_ver;
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

	/* HAS_MBUS_JOINING(): Alder Lake-P and display 14+. */
	display_ver = drv_i915_lcd_display_ver();
	if (display_ver >= 13) {
		new_dbuf->joined_mbus = i915_adlp_check_mbus_joined(new_dbuf->active_pipes);
	} else {
		new_dbuf->joined_mbus = false;
	}

	/* The pipe keeps no slices, weight or DDB. */
	new_dbuf->slices[pipe] = i915_skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);
	new_dbuf->enabled_slices = i915_intel_dbuf_enabled_slices(new_dbuf);
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

/*
 * Writes the watermark levels and the DDB allocation of one plane
 * through the xd watermarks (skl_write_plane_wm()).
 */
void
skl_write_plane_wm(
	struct intel_plane *plane,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	const struct skl_pipe_wm *pipe_wm;
	const struct skl_ddb_entry *ddb;
	struct i915_xd_plane_wm xd_wm;
	struct i915_xd_ddb range;

	/* The plane's watermarks and DDB entry the state computed. */
	i915 = i915_lcd_to_i915(plane->base.dev);
	pipe_wm = &crtc_state->wm.skl.optimal;
	ddb = &crtc_state->wm.skl.plane_ddb[plane->id];
	i915_wm_plane_to_xd(&pipe_wm->planes[plane->id], &xd_wm);
	range.start = ddb->start;
	range.end = ddb->end;

	/* Writes the levels, the transition, the SAGV pair and the entry. */
	drv_i915_xd_wm_write_plane(drv_i915_lcd_xd_io(i915), drv_i915_lcd_xd_platform(), (unsigned)plane->pipe,
		(unsigned)i915->display.wm.num_levels, &xd_wm, pipe_wm->use_sagv_wm ? 1 : 0, &range);
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

/* The data rate of a crtc's planes, the cursor aside (intel_bw_crtc_data_rate()). */
static unsigned int
i915_intel_bw_crtc_data_rate(
	const struct intel_crtc_state *crtc_state)
{
	unsigned int data_rate;
	enum plane_id plane_id;
	int display_ver;

	/* The display version of the crtc's device (DISPLAY_VER()). */
	display_ver = drv_i915_lcd_display_ver();
	data_rate = 0;

	/* Sums the planes of the crtc (for_each_plane_id_on_crtc(): the primary only). */
	for (plane_id = PLANE_PRIMARY; plane_id <= PLANE_PRIMARY; plane_id++) {
		/*
		 * We assume cursors are small enough
		 * to not not cause bandwidth problems.
		 */
		if (plane_id == PLANE_CURSOR)
			continue;

		data_rate += crtc_state->data_rate[plane_id];

		/* Before display version 11 the NV12 Y plane counts apart. */
		if (display_ver < 11)
			data_rate += crtc_state->data_rate_y[plane_id];
	}

	/* Succeeded: reports the data rate. */
	return data_rate;
}

/* The CDCLK the crtc's "Maximum Pipe Read Bandwidth" needs (intel_bw_crtc_min_cdclk()). */
static int
i915_intel_bw_crtc_min_cdclk(
	const struct intel_crtc_state *crtc_state)
{
	unsigned int data_rate;
	u64 scaled;
	int display_ver;

	/* The limit exists from display version 12 (DISPLAY_VER() of the crtc's device). */
	display_ver = drv_i915_lcd_display_ver();
	if (display_ver < 12)
		return 0;

	/* data rate * 10 / 512, rounded up. */
	data_rate = i915_intel_bw_crtc_data_rate(crtc_state);
	scaled = mul_u32_u32(data_rate, 10);

	/* Succeeded: reports the CDCLK. */
	return DIV_ROUND_UP_ULL(scaled, 512);
}

/* The DBUF slices a DBUF state enables: S1 always, and every pipe's slices (intel_dbuf_enabled_slices()). */
static u8
i915_intel_dbuf_enabled_slices(
	const struct intel_dbuf_state *dbuf_state)
{
	struct drm_i915_private *i915;
	u8 enabled_slices;
	enum pipe pipe;

	/* The device of the state's commit. */
	i915 = i915_lcd_to_i915(dbuf_state->base.state->base.dev);

	/*
	 * FIXME: For now we always enable slice S1 as per
	 * the Bspec display initialization sequence.
	 */
	enabled_slices = BIT(DBUF_S1);

	/* for_each_pipe(): adds the slices of every pipe of the device. */
	for (pipe = PIPE_A; pipe < I915_MAX_PIPES; pipe++) {
		if ((I915_LCD_DISPLAY_RUNTIME_INFO(i915)->pipe_mask & BIT(pipe)) == 0)
			continue;

		enabled_slices |= dbuf_state->slices[pipe];
	}

	/* Succeeded: reports the slices. */
	return enabled_slices;
}

/* Tells whether a set of active pipes joins the MBUS, through the xd DBUF. */
static bool
i915_adlp_check_mbus_joined(
	u8 active_pipes)
{
	u8 joined;

	/* The platform's table. */
	joined = drv_i915_xd_dbuf_joined(drv_i915_lcd_xd_platform(), active_pipes);
	if (!joined)
		return false;

	/* Succeeded: the bus is joined. */
	return true;
}

/* The DBUF slices of a crtc's pipe by platform (skl_compute_dbuf_slices()). */
static u8
i915_skl_compute_dbuf_slices(
	struct intel_crtc *crtc,
	u8 active_pipes,
	bool join_mbus)
{
	enum pipe pipe;
	int display_ver;
	u8 slices;

	/* The pipe, and the display version of the crtc's device (DISPLAY_VER()). */
	pipe = crtc->pipe;
	display_ver = drv_i915_lcd_display_ver();

	/*
	 * The table of the platform.  The DG2 table is not reached: IS_DG2() is
	 * 0 on this path, so dg2_compute_dbuf_slices() is not called.
	 */
	if (display_ver >= 12) {
		slices = drv_i915_xd_dbuf_slices(drv_i915_lcd_xd_platform(), (unsigned)pipe, active_pipes, join_mbus ? 1 : 0);
	} else if (display_ver == 11) {
		slices = icl_compute_dbuf_slices(pipe, active_pipes, join_mbus);
	} else if (active_pipes & BIT(pipe)) {
		/*
		 * For anything else just return one slice yet.
		 * Should be extended for other platforms.
		 */
		slices = BIT(DBUF_S1);
	} else {
		slices = 0;
	}

	/* Succeeded: reports the slices. */
	return slices;
}

/* Fills a DDB entry and reports its end (skl_ddb_entry_init()). */
static u16
i915_skl_ddb_entry_init(
	struct skl_ddb_entry *entry,
	u16 start,
	u16 end)
{
	/* The entry's block range. */
	entry->start = start;
	entry->end = end;

	/* Succeeded: reports the end, where the next entry starts. */
	return end;
}

/* Where the allocations of a set of slices count from, through the xd DBUF. */
static unsigned int
i915_mbus_ddb_offset(
	struct drm_i915_private *i915,
	u8 slice_mask)
{
	u16 offset;

	UNUSED_PARAMETER(i915);

	/* The start of the slices' half. */
	offset = drv_i915_xd_dbuf_mbus_offset(drv_i915_lcd_xd_platform(), slice_mask);

	/* Succeeded: the offset. */
	return offset;
}

/* Tells whether IPC is enabled (skl_watermark_ipc_enabled()). */
static bool
i915_skl_watermark_ipc_enabled(
	struct drm_i915_private *i915)
{
	/* The device's IPC setting. */
	return i915->display.wm.ipc_enabled;
}

/* The cursor's share of a pipe, through the xd watermarks. */
static unsigned int
i915_skl_cursor_allocation(
	const struct intel_crtc_state *crtc_state,
	int num_active)
{
	struct drm_i915_private *i915;
	struct i915_xd_wm_pipe pipe;
	u16 blocks;

	/* The pipe. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);
	i915_wm_xd_pipe(i915, crtc_state, &pipe);

	/* The cursor's blocks. */
	blocks = drv_i915_xd_wm_cursor_blocks(drv_i915_lcd_xd_platform(), &pipe, (unsigned)num_active);

	/* Succeeded: the share. */
	return blocks;
}

/*
 * The weights before, up to and around a pipe among the pipes on the same
 * slices (intel_crtc_dbuf_weights()).
 */
static void
i915_intel_crtc_dbuf_weights(
	const struct intel_dbuf_state *dbuf_state,
	enum pipe for_pipe,
	unsigned int *weight_start,
	unsigned int *weight_end,
	unsigned int *weight_total)
{
	struct drm_i915_private *i915;
	enum pipe pipe;
	int weight;

	/* The device of the state's commit, and empty sums. */
	i915 = i915_lcd_to_i915(dbuf_state->base.state->base.dev);
	*weight_start = 0;
	*weight_end = 0;
	*weight_total = 0;

	/* for_each_pipe(): sums the weights of the pipes on the same slices. */
	for (pipe = PIPE_A; pipe < I915_MAX_PIPES; pipe++) {
		if ((I915_LCD_DISPLAY_RUNTIME_INFO(i915)->pipe_mask & BIT(pipe)) == 0)
			continue;

		/* The pipe's weight. */
		weight = dbuf_state->weight[pipe];

		/*
		 * Do not account pipes using other slice sets
		 * luckily as of current BSpec slice sets do not partially
		 * intersect(pipes share either same one slice or same slice set
		 * i.e no partial intersection), so it is enough to check for
		 * equality for now.
		 */
		if (dbuf_state->slices[pipe] != dbuf_state->slices[for_pipe])
			continue;

		*weight_total += weight;
		if (pipe < for_pipe) {
			*weight_start += weight;
			*weight_end += weight;
		} else if (pipe == for_pipe) {
			*weight_end += weight;
		}
	}
}

/* Describes a crtc state's pipe to the xd watermarks: the latencies, IPC, the SAGV block time, the rates. */
static void
i915_wm_xd_pipe(
	struct drm_i915_private *i915,
	const struct intel_crtc_state *crtc_state,
	struct i915_xd_wm_pipe *pipe)
{
	unsigned level;

	/* The levels and their latencies. */
	kern_memset(pipe, 0, sizeof(*pipe));
	pipe->levels = (unsigned)i915->display.wm.num_levels;
	for (level = 0; level < pipe->levels && level < I915_XD_WM_LEVELS; level++)
		pipe->latency_us[level] = i915->display.wm.skl_latency[level];

	/* IPC, the SAGV block time, the pipe's pixel rate and line total. */
	pipe->ipc = i915_skl_watermark_ipc_enabled(i915) ? 1 : 0;
	pipe->sagv_block_us = (unsigned)i915->display.sagv.block_time_us;
	if (crtc_state->hw.active)
		pipe->pixel_rate_khz = crtc_state->pixel_rate;
	pipe->htotal = (uint32_t)crtc_state->hw.pipe_mode.crtc_htotal;
}

/* Copies one Linux watermark level into an xd one. */
static void
i915_wm_level_to_xd(
	const struct skl_wm_level *from,
	struct i915_xd_wm_level *to)
{
	/* The fields. */
	to->enable = from->enable ? 1U : 0U;
	to->ignore_lines = from->ignore_lines ? 1U : 0U;
	to->blocks = from->blocks;
	to->lines = from->lines;
	to->min_ddb = from->min_ddb_alloc;
}

/* Copies one xd watermark level into a Linux one. */
static void
i915_wm_level_from_xd(
	const struct i915_xd_wm_level *from,
	struct skl_wm_level *to)
{
	/* The fields. */
	to->enable = from->enable != 0U;
	to->ignore_lines = from->ignore_lines != 0U;
	to->blocks = from->blocks;
	to->lines = (u8)from->lines;
	to->min_ddb_alloc = from->min_ddb;
}

/* Copies a Linux plane watermark into an xd one. */
static void
i915_wm_plane_to_xd(
	const struct skl_plane_wm *from,
	struct i915_xd_plane_wm *to)
{
	unsigned level;

	/* The levels, the transition and the SAGV pair. */
	for (level = 0; level < I915_XD_WM_LEVELS; level++)
		i915_wm_level_to_xd(&from->wm[level], &to->level[level]);
	i915_wm_level_to_xd(&from->trans_wm, &to->trans);
	i915_wm_level_to_xd(&from->sagv.wm0, &to->sagv);
	i915_wm_level_to_xd(&from->sagv.trans_wm, &to->sagv_trans);
}

/* Copies an xd plane watermark into a Linux one. */
static void
i915_wm_plane_from_xd(
	const struct i915_xd_plane_wm *from,
	struct skl_plane_wm *to)
{
	unsigned level;

	/* The levels, the transition and the SAGV pair. */
	for (level = 0; level < I915_XD_WM_LEVELS; level++)
		i915_wm_level_from_xd(&from->level[level], &to->wm[level]);
	i915_wm_level_from_xd(&from->trans, &to->trans_wm);
	i915_wm_level_from_xd(&from->sagv, &to->sagv.wm0);
	i915_wm_level_from_xd(&from->sagv_trans, &to->sagv.trans_wm);
}

/*
 * Builds a pipe's watermarks through the xd watermarks: the plane's
 * levels, transition and SAGV watermarks, then the levels the vertical
 * blank allows (EINVAL when none).
 */
static int
i915_skl_build_pipe_wm(
	struct i915_lcd_wm_ctx *wm,
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state;
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	struct drm_i915_private *i915;
	const struct drm_display_mode *adjusted_mode;
	struct i915_xd_wm_pipe pipe;
	struct i915_xd_wm_surface surface;
	struct i915_xd_plane_wm xd_wm;
	struct skl_plane_wm *plane_wm;
	unsigned deepest;
	int sagv_too_short;
	bool visible;
	int error;

	/* The crtc state, the pipe and its one plane. */
	crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);
	plane = wm->crtc_state->only_plane;
	plane_state = (struct intel_plane_state *)wm->crtc_state->only_plane_state;
	i915_wm_xd_pipe(i915, crtc_state, &pipe);

	/* The plane's watermarks, when it is on this pipe and visible. */
	if (plane != NULL && plane->pipe == crtc->pipe) {
		plane_wm = &crtc_state->wm.skl.raw.planes[plane->id];
		kern_memset(plane_wm, 0, sizeof(*plane_wm));
		visible = drv_i915_wm_plane_visible(crtc_state, plane_state);
		if (visible) {
			surface.width = (uint32_t)(i915_drm_rect_width(&plane_state->uapi.src) >> 16);
			surface.cpp = plane_state->hw.fb->format->cpp[0];
			surface.layout = I915_XD_WM_LINEAR;
			if (plane_state->hw.fb->modifier == I915_FORMAT_MOD_X_TILED) {
				surface.layout = I915_XD_WM_X_TILED;
			} else if (plane_state->hw.fb->modifier != FORMAT_MOD_LINEAR) {
				surface.layout = I915_XD_WM_Y_TILED;
			}
			surface.pixel_rate_khz = drv_i915_plane_pixel_rate(crtc_state, plane_state);
			surface.minimal = 0;
			if (drv_i915_lcd_display_ver() >= 13 && crtc_state->uapi.async_flip && plane->async_flip)
				surface.minimal = 1;
			drv_i915_xd_wm_plane(drv_i915_lcd_xd_platform(), &pipe, &surface, &xd_wm);
			i915_wm_plane_from_xd(&xd_wm, plane_wm);
		}
	}

	/* The optimal watermarks start as the raw ones. */
	crtc_state->wm.skl.optimal = crtc_state->wm.skl.raw;
	if (!crtc_state->hw.active)
		return 0;

	/* The deepest level the vertical blank allows. */
	adjusted_mode = &crtc_state->hw.adjusted_mode;
	pipe.htotal = (uint32_t)adjusted_mode->crtc_htotal;
	error = drv_i915_xd_wm_vblank_level(&pipe, (uint32_t)adjusted_mode->crtc_clock,
		(uint32_t)(adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vblank_start),
		(unsigned)crtc_state->framestart_delay,
		crtc_state->wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].lines,
		&deepest, &sagv_too_short);
	if (error != 0)
		return error;

	/* The deeper levels and a SAGV that does not fit go off. */
	crtc_state->wm_level_disabled = false;
	if ((int)deepest < i915->display.wm.num_levels - 1)
		crtc_state->wm_level_disabled = true;
	if (drv_i915_lcd_display_ver() < 12)
		sagv_too_short = 0;
	i915_wm_plane_to_xd(&crtc_state->wm.skl.optimal.planes[PLANE_PRIMARY], &xd_wm);
	drv_i915_xd_wm_cut(&xd_wm, pipe.levels, deepest, sagv_too_short);
	i915_wm_plane_from_xd(&xd_wm, &crtc_state->wm.skl.optimal.planes[PLANE_PRIMARY]);

	/* Succeeded: the pipe's watermarks are built. */
	return 0;
}

/*
 * Hands a pipe's share of the DBUF out to its cursor and its plane through
 * the xd watermarks, turning off the watermarks the allocation cannot
 * hold.  Returns 0, or EINVAL when not even level 0 fits.
 */
static int
i915_skl_crtc_allocate_plane_ddb(
	struct i915_lcd_wm_ctx *wm_ctx,
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct drm_i915_private *i915;
	struct intel_crtc_state *crtc_state;
	const struct intel_dbuf_state *dbuf_state;
	const struct skl_ddb_entry *alloc;
	struct skl_plane_wm *plane_wm;
	struct i915_xd_wm_pipe pipe;
	struct i915_xd_plane_wm xd_wm;
	struct i915_xd_ddb range;
	struct i915_xd_ddb plane_ddb;
	struct i915_xd_ddb cursor_ddb;
	u16 cursor_blocks;
	int num_active;
	int error;

	/* The device, the new state, the pipe's share and the active pipes. */
	i915 = i915_lcd_to_i915(crtc->base.dev);
	crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	dbuf_state = i915_wm_intel_atomic_get_new_dbuf_state(wm_ctx);
	alloc = &dbuf_state->ddb[crtc->pipe];
	num_active = hweight8(dbuf_state->active_pipes);

	/* Nothing is handed out on an inactive pipe or an empty share. */
	kern_memset(crtc_state->wm.skl.plane_ddb, 0, sizeof(crtc_state->wm.skl.plane_ddb));
	kern_memset(crtc_state->wm.skl.plane_ddb_y, 0, sizeof(crtc_state->wm.skl.plane_ddb_y));
	if (!crtc_state->hw.active)
		return 0;
	if (skl_ddb_entry_size(alloc) == 0)
		return 0;

	/* The cursor's part and the plane's watermarks. */
	cursor_blocks = (u16)i915_skl_cursor_allocation(crtc_state, num_active);
	i915_wm_xd_pipe(i915, crtc_state, &pipe);
	plane_wm = &crtc_state->wm.skl.optimal.planes[PLANE_PRIMARY];
	i915_wm_plane_to_xd(plane_wm, &xd_wm);

	/* Hands the share out. */
	range.start = alloc->start;
	range.end = alloc->end;
	error = drv_i915_xd_wm_allocate(&pipe, &range, cursor_blocks, &xd_wm, crtc_state->rel_data_rate[PLANE_PRIMARY], &plane_ddb, &cursor_ddb);
	if (error != 0) {
		I915_LCD_DRM_DBG_KMS(&i915->drm, "Requested display configuration exceeds system DDB limitations");
		return error;
	}

	/* The plane's and the cursor's entries and the plane's watermarks. */
	i915_wm_plane_from_xd(&xd_wm, plane_wm);
	i915_skl_ddb_entry_init(&crtc_state->wm.skl.plane_ddb[PLANE_PRIMARY], plane_ddb.start, plane_ddb.end);
	i915_skl_ddb_entry_init(&crtc_state->wm.skl.plane_ddb[PLANE_CURSOR], cursor_ddb.start, cursor_ddb.end);

	/* Succeeded: the share is handed out. */
	return 0;
}

/* The DDB weight of a crtc: its pipe mode's width (intel_crtc_ddb_weight()). */
static unsigned int
i915_intel_crtc_ddb_weight(
	const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *pipe_mode;
	int hdisplay;
	int vdisplay;

	/* An inactive crtc weighs nothing. */
	pipe_mode = &crtc_state->hw.pipe_mode;
	if (!crtc_state->hw.active)
		return 0;

	/*
	 * Watermark/ddb requirement highly depends upon width of the
	 * framebuffer, So instead of allocating DDB equally among pipes
	 * distribute DDB based on resolution/width of the display.
	 */
	drv_i915_drm_mode_get_hv_timing(pipe_mode, &hdisplay, &vdisplay);

	/* Succeeded: reports the width. */
	return hdisplay;
}

/*
 * Gives a crtc's pipe its share of its slices' DDB (skl_crtc_allocate_ddb());
 * 0, or the error of the global state lock.  The crtc's state is the
 * watermark context's.
 */
static int
i915_skl_crtc_allocate_ddb(
	struct i915_lcd_wm_ctx *wm,
	struct intel_crtc *crtc)
{
	struct drm_i915_private *i915;
	unsigned int weight_total;
	unsigned int weight_start;
	unsigned int weight_end;
	const struct intel_dbuf_state *old_dbuf_state;
	struct intel_dbuf_state *new_dbuf_state;
	struct intel_crtc_state *crtc_state;
	struct i915_xd_ddb range;
	enum pipe pipe;
	unsigned int mbus_offset;
	u32 dbuf_slice_mask;
	bool equal;
	int error;

	/* The device, the pipe, and the DBUF states of the commit. */
	i915 = i915_lcd_to_i915(crtc->base.dev);
	old_dbuf_state = i915_wm_intel_atomic_get_old_dbuf_state(wm);
	new_dbuf_state = i915_wm_intel_atomic_get_new_dbuf_state(wm);
	pipe = crtc->pipe;
	mbus_offset = 0;

	/* A pipe without weight gets no DDB; otherwise its weighted share of its slices' range. */
	/* The pipe's share of its slices by weight (none without weight). */
	if (new_dbuf_state->weight[pipe] == 0) {
		i915_skl_ddb_entry_init(&new_dbuf_state->ddb[pipe], 0, 0);
	} else {
		dbuf_slice_mask = new_dbuf_state->slices[pipe];
		mbus_offset = i915_mbus_ddb_offset(i915, dbuf_slice_mask);
		i915_intel_crtc_dbuf_weights(new_dbuf_state, pipe,
			&weight_start, &weight_end, &weight_total);
		drv_i915_xd_dbuf_pipe_range(drv_i915_lcd_xd_platform(), (u8)dbuf_slice_mask, weight_start, weight_end, weight_total, &range);
		i915_skl_ddb_entry_init(&new_dbuf_state->ddb[pipe], range.start, range.end);
	}

	if (old_dbuf_state->slices[pipe] == new_dbuf_state->slices[pipe]) {
		equal = skl_ddb_entry_equal(&old_dbuf_state->ddb[pipe],
			&new_dbuf_state->ddb[pipe]);
		if (equal)
			return 0;
	}

	/* Locks the global DBUF state (nothing to lock: one commit at a time). */
	error = intel_atomic_lock_global_state(&new_dbuf_state->base);
	if (error != 0)
		return error;

	/* The crtc's state in the commit: the watermark context's. */
	crtc_state = wm->crtc_state;

	/*
	 * Used for checking overlaps, so we need absolute
	 * offsets instead of MBUS relative offsets.
	 */
	crtc_state->wm.skl.ddb.start = mbus_offset + new_dbuf_state->ddb[pipe].start;
	crtc_state->wm.skl.ddb.end = mbus_offset + new_dbuf_state->ddb[pipe].end;

	/* The debug line of the change. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
		"[CRTC:%d:%s] dbuf slices 0x%x -> 0x%x, ddb (%d - %d) -> (%d - %d), active pipes 0x%x -> 0x%x\n",
		crtc->base.base.id, crtc->base.name,
		old_dbuf_state->slices[pipe], new_dbuf_state->slices[pipe],
		old_dbuf_state->ddb[pipe].start, old_dbuf_state->ddb[pipe].end,
		new_dbuf_state->ddb[pipe].start, new_dbuf_state->ddb[pipe].end,
		old_dbuf_state->active_pipes, new_dbuf_state->active_pipes);

	/* Succeeded: the pipe has its DDB. */
	return 0;
}
