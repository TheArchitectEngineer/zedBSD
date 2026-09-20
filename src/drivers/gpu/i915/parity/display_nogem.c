/*
 * WS031 Linux-parity — P5-a: the front of intel_display_driver_probe_nogem().
 * See display_nogem.h.
 */
#include "display_nogem.h"
#include "cdclk.h"
#include "dram_bw.h"
#include "pcode.h"
#include "vga.h"
#include "bios.h"
#include "power_domains.h"
#include "osdep/mmio.h"
#include <kern/klog.h>
#include <errno.h>

/* ---------------- registers ---------------- */

#define GEN9_PCODE_READ_MEM_LATENCY         0x6u
#define GEN9_MEM_LATENCY_LEVEL_0_4_MASK     0x000000ffu
#define GEN9_MEM_LATENCY_LEVEL_1_5_MASK     0x0000ff00u
#define GEN9_MEM_LATENCY_LEVEL_2_6_MASK     0x00ff0000u
#define GEN9_MEM_LATENCY_LEVEL_3_7_MASK     0xff000000u
#define GEN12_PCODE_READ_SAGV_BLOCK_TIME_US 0x23u

#define PPS_BASE                            0x61200u
#define PCH_DISPLAY_BASE                    0xc0000u
#define GMBUS0_OFF                          0x5100u
#define GMBUS4_OFF                          0x5110u
#define GMBUS_RATE_100KHZ                   0u

/* adlp_display_wa_apply() */
#define GEN9_CLKGATE_DIS_5                  0x46540u
#define DPCE_GATING_DIS                     (1u << 17)
#define GEN8_CHICKEN_DCPR_1                 0x46430u
#define DDI_CLOCK_REG_ACCESS                (1u << 7)

/* intel_vga_disable() */
#define CPU_VGACNTRL                        0x41000u
#define VGA_DISP_DISABLE                    (1u << 31)
/* Standard VGA sequencer ports (video/vga.h), same family as the MIS ports
 * already used by vga.c. */
#define VGA_SEQ_I                           0x3C4u
#define VGA_SEQ_D                           0x3C5u
#define VGA_SR01_SCREEN_OFF                 0x20u

/* ICL+ shared DPLL enable registers. */
#define _DPLL0_ENABLE                       0x46010u
#define _DPLL1_ENABLE                       0x46014u
#define TBT_PLL_ENABLE                      0x46020u
#define _MG_PLL1_ENABLE                     0x46030u

/* enum intel_dpll_id (ICL naming, values preserved). */
#define DPLL_ID_ICL_DPLL0                   0
#define DPLL_ID_ICL_DPLL1                   1
#define DPLL_ID_ICL_TBTPLL                  2
#define DPLL_ID_ICL_MGPLL1                  3
#define DPLL_ID_ICL_MGPLL2                  4
#define DPLL_ID_ICL_MGPLL3                  5
#define DPLL_ID_ICL_MGPLL4                  6

/* GMBUS pin indices (intel_gmbus.h). */
#define GMBUS_PIN_1_BXT                     1u
#define GMBUS_PIN_2_BXT                     2u
#define GMBUS_PIN_3_BXT                     3u
#define GMBUS_PIN_9_TC1_ICP                 9u
#define GMBUS_PIN_10_TC2_ICP                10u
#define GMBUS_PIN_11_TC3_ICP                11u
#define GMBUS_PIN_12_TC4_ICP                12u
#define GMBUS_PIN_13_TC5_TGP                13u
#define GMBUS_PIN_14_TC6_TGP                14u

/* GPIO ordinals used by the ICP pin table. */
#define GPIOB 1u
#define GPIOC 2u
#define GPIOD 3u
#define GPIOJ 9u
#define GPIOK 10u
#define GPIOL 11u
#define GPIOM 12u
#define GPION 13u
#define GPIOO 14u

/* ---------------- helpers ---------------- */

static void
zero_mem(void *p, unsigned n)
{
	unsigned i;
	char *c = (char *)p;

	for (i = 0u; i < n; i++)
		c[i] = 0;
}

static unsigned
popcount4(unsigned v)
{
	unsigned n = 0u;

	while (v != 0u) { n += (v & 1u); v >>= 1; }
	return n;
}

static void
wr(struct parity_display_nogem *d, struct osdep_mmio *m, uint32_t reg, uint32_t val)
{
	osdep_mmio_write32(m, reg, val);
	d->mmio_writes++;
}

static uint32_t
rmw(struct parity_display_nogem *d, struct osdep_mmio *m, uint32_t reg,
	uint32_t clear, uint32_t set)
{
	uint32_t old = osdep_mmio_read32(m, reg);
	uint32_t val = (old & ~clear) | set;

	if (val != old)
		wr(d, m, reg, val);

	return old;
}

/* ---------------- skl_watermark.c: wm latency + SAGV ---------------- */

void
parity_adjust_wm_latency(uint16_t wm[], int num_levels, int read_latency,
	int wm_lv_0_adjust_needed)
{
	int i, level;

	/*
	 * If a level n (n > 1) has a 0us latency, all levels m (m >= n)
	 * need to be disabled.
	 */
	for (level = 1; level < num_levels; level++) {
		if (wm[level] == 0u) {
			for (i = level + 1; i < num_levels; i++)
				wm[i] = 0u;
			num_levels = level;
			break;
		}
	}

	/*
	 * WaWmMemoryReadLatency: the punit does not account for the read latency,
	 * so add it to every valid level when level 0 came back as 0us.
	 */
	if (wm[0] == 0u) {
		for (level = 0; level < num_levels; level++)
			wm[level] = (uint16_t)(wm[level] + (uint16_t)read_latency);
	}

	/* WA Level-0 adjustment for 16GB DIMMs (SKL+). */
	if (wm_lv_0_adjust_needed)
		wm[0] = (uint16_t)(wm[0] + 1u);
}

void
parity_skl_setup_wm_latency(struct parity_display_nogem *d, int display_ver,
	struct mutex *sb_lock, struct osdep_mmio *m, int wm_lv_0_adjust_needed)
{
	uint32_t val;
	uint32_t val1 = 0u;
	int ret;
	int read_latency = (display_ver >= 12) ? 3 : 2;
	int mult = 1;   /* IS_DG2 only doubles this */

	/* HAS_HW_SAGV_WM(ver >= 13 && !DGFX) -> 6 levels, otherwise 8. */
	d->wm_num_levels = (display_ver >= 13) ? 6u : 8u;

	/* First set of memory latencies [0:3]: data0 = 0. */
	val = 0u;
	ret = parity_pcode_read(sb_lock, m, GEN9_PCODE_READ_MEM_LATENCY, &val, &val1);
	if (ret != 0) {
		kern_logf("i915: parity SKL Mailbox read error = %d\n", ret);
		return;
	}
	d->wm_skl_latency[0] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_0_4_MASK) >> 0) * (uint32_t)mult);
	d->wm_skl_latency[1] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_1_5_MASK) >> 8) * (uint32_t)mult);
	d->wm_skl_latency[2] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_2_6_MASK) >> 16) * (uint32_t)mult);
	d->wm_skl_latency[3] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_3_7_MASK) >> 24) * (uint32_t)mult);

	/* Second set [4:7]: data0 = 1. */
	val = 1u;
	val1 = 0u;
	ret = parity_pcode_read(sb_lock, m, GEN9_PCODE_READ_MEM_LATENCY, &val, &val1);
	if (ret != 0) {
		kern_logf("i915: parity SKL Mailbox read error = %d\n", ret);
		return;
	}
	d->wm_skl_latency[4] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_0_4_MASK) >> 0) * (uint32_t)mult);
	d->wm_skl_latency[5] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_1_5_MASK) >> 8) * (uint32_t)mult);
	d->wm_skl_latency[6] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_2_6_MASK) >> 16) * (uint32_t)mult);
	d->wm_skl_latency[7] = (uint16_t)(((val & GEN9_MEM_LATENCY_LEVEL_3_7_MASK) >> 24) * (uint32_t)mult);

	parity_adjust_wm_latency(d->wm_skl_latency, (int)d->wm_num_levels,
		read_latency, wm_lv_0_adjust_needed);
	d->wm_latency_valid = 1;
}

/* intel_sagv_block_time(): DISPLAY_VER >= 12 reads it from the PCODE. */
static uint32_t
intel_sagv_block_time(int display_ver, struct mutex *sb_lock, struct osdep_mmio *m)
{
	if (display_ver >= 12) {
		uint32_t val = 0u, val1 = 0u;

		if (parity_pcode_read(sb_lock, m,
			GEN12_PCODE_READ_SAGV_BLOCK_TIME_US, &val, &val1) != 0) {
			kern_logf("i915: parity Couldn't read SAGV block time!\n");
			return 0u;
		}
		return val;
	}
	if (display_ver == 11)
		return 10u;
	return 30u;   /* HAS_SAGV pre-icl */
}

/*
 * intel_sagv_init().  On icl+ the SAGV control state was already settled by
 * intel_bw_init_hw()/intel_bw_init(), so this only records the block time; the
 * status is carried in from the P2 bandwidth state.
 */
static void
intel_sagv_init(struct parity_display_nogem *d, int display_ver,
	struct mutex *sb_lock, struct osdep_mmio *m, struct parity_bw_state *bw)
{
	int has_sagv = (display_ver >= 9);   /* HAS_SAGV: ver >= 9 && !IS_LP */

	if (!has_sagv)
		bw->sagv_status = (int)PARITY_SAGV_NOT_CONTROLLED;

	/* DISPLAY_VER < 11 would probe with skl_sagv_disable() here. */

	if (bw->sagv_status == (int)PARITY_SAGV_UNKNOWN)
		kern_logf("i915: parity WARN SAGV status still unknown at sagv_init\n");

	d->sagv_status = bw->sagv_status;
	d->sagv_block_time_us = intel_sagv_block_time(display_ver, sb_lock, m);

	/* Avoid overflow when adding with wm0 latency etc. */
	if (d->sagv_block_time_us > 0xffffu) {
		kern_logf("i915: parity Excessive SAGV block time %u, ignoring\n",
			d->sagv_block_time_us);
		d->sagv_block_time_us = 0u;
	}

	if (bw->sagv_status == (int)PARITY_SAGV_NOT_CONTROLLED)
		d->sagv_block_time_us = 0u;

	kern_logf("i915: parity SAGV supported: %s, original SAGV block time: %u us\n",
		(bw->sagv_status != (int)PARITY_SAGV_NOT_CONTROLLED) ? "yes" : "no",
		d->sagv_block_time_us);
}

/* ---------------- intel_dpll_mgr.c: intel_shared_dpll_init() ---------------- */

void
parity_intel_shared_dpll_init(struct parity_display_nogem *d, int display_ver,
	int is_alderlake_p)
{
	/* adlp_plls[]: DPLL0, DPLL1, TBT PLL, TC PLL 1..4. */
	static const struct {
		const char *name;
		int id;
		int funcs;
		uint32_t reg;
	} adlp_plls[] = {
		{ "DPLL 0",   DPLL_ID_ICL_DPLL0,  PARITY_DPLL_FUNCS_COMBO, _DPLL0_ENABLE },
		{ "DPLL 1",   DPLL_ID_ICL_DPLL1,  PARITY_DPLL_FUNCS_COMBO, _DPLL1_ENABLE },
		{ "TBT PLL",  DPLL_ID_ICL_TBTPLL, PARITY_DPLL_FUNCS_TBT,   TBT_PLL_ENABLE },
		{ "TC PLL 1", DPLL_ID_ICL_MGPLL1, PARITY_DPLL_FUNCS_DKL,   _MG_PLL1_ENABLE + 0u },
		{ "TC PLL 2", DPLL_ID_ICL_MGPLL2, PARITY_DPLL_FUNCS_DKL,   _MG_PLL1_ENABLE + 4u },
		{ "TC PLL 3", DPLL_ID_ICL_MGPLL3, PARITY_DPLL_FUNCS_DKL,   _MG_PLL1_ENABLE + 8u },
		{ "TC PLL 4", DPLL_ID_ICL_MGPLL4, PARITY_DPLL_FUNCS_DKL,   _MG_PLL1_ENABLE + 12u }
	};
	unsigned i;

	(void)mutex_init(&d->dpll_lock, LOCK_RANK_DEVICE, "parity-dpll");

	/*
	 * DISPLAY_VER >= 14 / DG2 have no shared DPLLs (port PLLs live in the PHY);
	 * ADL-P selects adlp_pll_mgr.  Anything else is out of scope for this port
	 * and leaves the manager absent rather than guessing a table.
	 */
	if (display_ver >= 14 || !is_alderlake_p) {
		d->dpll_mgr_present = 0;
		d->num_dplls = 0u;
		return;
	}

	for (i = 0u; i < (unsigned)(sizeof(adlp_plls) / sizeof(adlp_plls[0])); i++) {
		if (i >= (unsigned)PARITY_NOGEM_MAX_DPLLS)
			break;
		d->dplls[i].name = adlp_plls[i].name;
		d->dplls[i].id = adlp_plls[i].id;
		d->dplls[i].funcs = adlp_plls[i].funcs;
		d->dplls[i].enable_reg = adlp_plls[i].reg;
		d->dplls[i].index = i;
	}
	d->num_dplls = i;
	d->dpll_mgr_present = 1;
}

/* ---------------- intel_crtc.c: intel_crtc_init() ---------------- */

int
parity_intel_crtc_init(struct parity_display_nogem *d, int display_ver,
	unsigned pipe)
{
	struct parity_crtc *crtc;
	unsigned n = 0u;
	unsigned sprite;
	unsigned num_sprites;

	if (pipe >= (unsigned)PARITY_NOGEM_MAX_PIPES)
		return -EINVAL;

	crtc = &d->crtcs[pipe];
	zero_mem(crtc, (unsigned)sizeof(*crtc));

	crtc->pipe = pipe;
	/* num_scalers[pipe] = 2 for DISPLAY_VER >= 11. */
	crtc->num_scalers = (display_ver >= 11) ? 2u : 0u;

	/* primary: skl_universal_plane_create(PLANE_PRIMARY) for ver >= 9. */
	crtc->planes[n].id = 0;
	crtc->planes[n].type = PARITY_PLANE_PRIMARY;
	crtc->planes[n].pipe = pipe;
	crtc->planes[n].in_use = 1;
	crtc->plane_ids_mask |= (1u << (unsigned)crtc->planes[n].id);
	n++;

	/* intel_init_fifo_underrun_reporting(crtc, false) */
	crtc->fifo_underrun_reporting = 0;

	/* num_sprites[pipe] = 4 for DISPLAY_VER >= 13. */
	num_sprites = (display_ver >= 13) ? 4u : ((display_ver >= 11) ? 6u : 0u);
	for (sprite = 0u; sprite < num_sprites; sprite++) {
		if (n >= (unsigned)PARITY_NOGEM_MAX_PLANES - 1u)
			break;   /* keep a slot for the cursor */
		crtc->planes[n].id = (int)(1u + sprite);   /* PLANE_SPRITE0 + sprite */
		crtc->planes[n].type = PARITY_PLANE_SPRITE;
		crtc->planes[n].pipe = pipe;
		crtc->planes[n].in_use = 1;
		crtc->plane_ids_mask |= (1u << (unsigned)crtc->planes[n].id);
		n++;
	}

	/* intel_cursor_plane_create() */
	crtc->planes[n].id = 7;   /* PLANE_CURSOR */
	crtc->planes[n].type = PARITY_PLANE_CURSOR;
	crtc->planes[n].pipe = pipe;
	crtc->planes[n].in_use = 1;
	crtc->plane_ids_mask |= (1u << (unsigned)crtc->planes[n].id);
	n++;

	crtc->num_planes = n;

	/* intel_crtc_state_reset(): INVALID_TRANSCODER etc. */
	crtc->state.cpu_transcoder = -1;

	crtc->in_use = 1;
	return 0;
}

/* ---------------- intel_display_wa.c ---------------- */

void
parity_adlp_display_wa_apply(struct parity_display_nogem *d, struct osdep_mmio *m,
	int display_ver, int is_alderlake_p)
{
	if (is_alderlake_p) {
		/* Wa_22011091694:adlp */
		(void)rmw(d, m, GEN9_CLKGATE_DIS_5, 0u, DPCE_GATING_DIS);
		/* Bspec/49189 Initialize Sequence */
		(void)rmw(d, m, GEN8_CHICKEN_DCPR_1, DDI_CLOCK_REG_ACCESS, 0u);
		d->adlp_wa_applied = 1;
		return;
	}
	/*
	 * The DISPLAY_VER == 12 (xe_d) and == 11 arms write ILK_DPFC_CHICKEN and
	 * their own chicken bits; this port targets ADL-P and does not guess them.
	 */
	(void)display_ver;
}

/* ---------------- intel_cdclk.c: intel_update_max_cdclk() ---------------- */

void
parity_intel_update_max_cdclk(struct parity_display_nogem *d, int display_ver,
	uint32_t cdclk_ref)
{
	if (display_ver >= 11)
		d->max_cdclk_freq = (cdclk_ref == 24000u) ? 648000u : 652800u;
	else
		d->max_cdclk_freq = 0u;   /* older arms are out of scope */
}

/* ---------------- intel_gmbus.c: intel_gmbus_setup() ---------------- */

static int
gmbus_setup(struct parity_display_nogem *d, struct osdep_mmio *m)
{
	/* gmbus_pins_icp[]: indices are the pin numbers, gaps stay absent. */
	static const struct { unsigned pin; const char *name; unsigned gpio; } icp[] = {
		{ GMBUS_PIN_1_BXT,      "dpa", GPIOB },
		{ GMBUS_PIN_2_BXT,      "dpb", GPIOC },
		{ GMBUS_PIN_3_BXT,      "dpc", GPIOD },
		{ GMBUS_PIN_9_TC1_ICP,  "tc1", GPIOJ },
		{ GMBUS_PIN_10_TC2_ICP, "tc2", GPIOK },
		{ GMBUS_PIN_11_TC3_ICP, "tc3", GPIOL },
		{ GMBUS_PIN_12_TC4_ICP, "tc4", GPIOM },
		{ GMBUS_PIN_13_TC5_TGP, "tc5", GPION },
		{ GMBUS_PIN_14_TC6_TGP, "tc6", GPIOO }
	};
	unsigned i;

	/* !HAS_GMCH -> the South Display Engine offsets. */
	d->gmbus_mmio_base = PCH_DISPLAY_BASE;

	(void)mutex_init(&d->gmbus_lock, LOCK_RANK_DEVICE, "parity-gmbus");
	waitq_init(&d->gmbus_waitq, "parity-gmbus");

	for (i = 0u; i < (unsigned)(sizeof(icp) / sizeof(icp[0])); i++) {
		unsigned pin = icp[i].pin;

		if (pin >= (unsigned)PARITY_NOGEM_MAX_GMBUS)
			continue;
		d->gmbus_pins[pin].name = icp[i].name;
		d->gmbus_pins[pin].gpio = icp[i].gpio;
		d->gmbus_pins[pin].reg0 = pin | GMBUS_RATE_100KHZ;
		d->gmbus_pins[pin].present = 1;
		d->gmbus_pins_present++;
	}

	/*
	 * The reference now creates one i2c_adapter per pin.  There is no i2c core
	 * in this port, so the adapters are NOT created and that is recorded rather
	 * than reported as success.  Nothing in P5's readout/sanitize needs them.
	 */
	d->gmbus_adapters_unimplemented = 1;

	/* intel_gmbus_reset() */
	wr(d, m, d->gmbus_mmio_base + GMBUS0_OFF, 0u);
	wr(d, m, d->gmbus_mmio_base + GMBUS4_OFF, 0u);

	return 0;
}

/* ---------------- the P5-a entry point ---------------- */

int
parity_intel_display_nogem_front(struct parity_display_nogem *d, int display_ver,
	unsigned pipe_mask, struct osdep_mmio *m, struct mutex *sb_lock,
	struct parity_cdclk_dev *cd, struct parity_bw_state *bw,
	struct parity_vga_client *vga)
{
	unsigned pipe;
	int rc;
	int is_adlp = (display_ver == 13);

	zero_mem(d, (unsigned)sizeof(*d));

	/* intel_wm_init() -> skl_wm_init() for DISPLAY_VER >= 9. */
	intel_sagv_init(d, display_ver, sb_lock, m, bw);
	parity_skl_setup_wm_latency(d, display_ver, sb_lock, m,
		0 /* dram_info.wm_lv_0_adjust_needed; P2 decoded it */);

	/*
	 * intel_panel_sanitize_ssc(): LVDS SSC handling.  ADL-P has no LVDS and no
	 * PCH SSC override, so there is nothing to sanitize here.
	 */

	/* intel_pps_setup(): not PCH_SPLIT/GLK/BXT and not VLV/CHV. */
	d->pps_mmio_base = PPS_BASE;

	/* intel_gmbus_setup() */
	rc = gmbus_setup(d, m);
	if (rc != 0) {
		d->fail_where = "intel_gmbus_setup";
		return rc;
	}

	kern_logf("i915: parity P5 %u display pipe%s available.\n",
		popcount4(pipe_mask),
		popcount4(pipe_mask) > 1u ? "s" : "");

	/* for_each_pipe: intel_crtc_init() */
	for (pipe = 0u; pipe < (unsigned)PARITY_NOGEM_MAX_PIPES; pipe++) {
		if ((pipe_mask & (1u << pipe)) == 0u)
			continue;
		rc = parity_intel_crtc_init(d, display_ver, pipe);
		if (rc != 0) {
			d->fail_where = "intel_crtc_init";
			return rc;
		}
		d->num_crtcs++;
	}

	/*
	 * intel_plane_possible_crtcs_init(): each plane's possible_crtcs is the
	 * mask of its own CRTC.  The planes here are already per-CRTC records, so
	 * the relation is structural.
	 */

	parity_intel_shared_dpll_init(d, display_ver, is_adlp);

	/* intel_fdi_pll_freq_update(): IRONLAKE/SNB/IVB only -> returns. */
	/* intel_update_czclk(): VLV/CHV only -> returns. */

	/* intel_display_driver_init_hw() */
	parity_intel_update_cdclk(cd);
	kern_logf("i915: parity P5 Current CDCLK: cdclk=%u vco=%u ref=%u voltage=%u\n",
		cd->hw.cdclk, cd->hw.vco, cd->hw.ref, (unsigned)cd->hw.voltage_level);
	d->cdclk_logical_set = 1;   /* cdclk_state->logical = actual = hw */
	parity_adlp_display_wa_apply(d, m, display_ver, is_adlp);

	/* intel_dpll_update_ref_clks() -> icl_update_dpll_ref_clks(): no SSC ref. */
	if (d->dpll_mgr_present)
		d->dpll_ref_nssc = cd->hw.ref;

	/*
	 * intel_hdcp_component_init(): needs the component framework and the GSC
	 * firmware path.  Neither exists here; recorded, not faked.
	 */
	d->hdcp_component_unimplemented = 1;

	/* if (max_cdclk_freq == 0) intel_update_max_cdclk() */
	if (d->max_cdclk_freq == 0u)
		parity_intel_update_max_cdclk(d, display_ver, cd->hw.ref);

	/*
	 * intel_hti_init(): only when DISPLAY_INFO()->has_hti.  xe_lpd does NOT
	 * set has_hti (RKL and ADL-S do), so HDPORT_STATE is deliberately not read.
	 */
	d->hti_state_read = 0;

	/* Just disable it once at startup. */
	{
		int vrc = parity_intel_vga_disable(vga, m);

		d->vga_already_disabled = (vrc == 1) ? 1 : 0;
		d->vga_disable_done = (vrc == 0) ? 1 : 0;
	}

	d->inited = 1;
	return 0;
}

/* ---------------- P5-b: intel_setup_outputs() ---------------- */

/* intel_vbt_defs.h DVO_PORT_* (the ones the xelpd mapping uses). */
#define DVO_PORT_HDMIA 0u
#define DVO_PORT_HDMIB 1u
#define DVO_PORT_HDMIC 2u
#define DVO_PORT_HDMID 3u
#define DVO_PORT_HDMIE 12u
#define DVO_PORT_HDMIF 14u
#define DVO_PORT_HDMIG 15u
#define DVO_PORT_HDMIH 16u
#define DVO_PORT_HDMII 17u
#define DVO_PORT_DPA 10u
#define DVO_PORT_DPB 7u
#define DVO_PORT_DPC 8u
#define DVO_PORT_DPD 9u
#define DVO_PORT_DPE 11u
#define DVO_PORT_DPF 13u
#define DVO_PORT_DPG 16u
#define DVO_PORT_DPH 20u
#define DVO_PORT_DPI 21u

#define DEVICE_TYPE_TMDS_DVI_SIGNALING  (1u << 4)
#define DEVICE_TYPE_NOT_HDMI_OUTPUT     (1u << 11)
#define DEVICE_TYPE_DISPLAYPORT_OUTPUT  (1u << 2)
#define DEVICE_TYPE_MIPI_OUTPUT         (1u << 10)

/* POWER_DOMAIN_PORT_DDI_LANES_A .. (enum parity_power_domain ordinals). */
#define PARITY_PD_PORT_DDI_LANES_A   17
#define PARITY_PD_PORT_DDI_LANES_TC1 23

/* icl_ddi_combo / icl_ddi_tc clock registers. */
#define ICL_DPCLKA_CFGCR0            0x164280u
#define DDI_CLK_SEL_MASK             0xf0000000u
#define DDI_CLK_SEL_NONE             0x00000000u
#define PORT_CLK_SEL_A               0x46100u

/* ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy) = 1 << _PICK(phy, 10, 11, 24, 4, 5) */
static uint32_t
icl_dpclka_ddi_clk_off(int phy)
{
	static const unsigned bits[5] = { 10u, 11u, 24u, 4u, 5u };

	if (phy < 0 || phy > 4)
		return 0u;
	return 1u << bits[phy];
}

/*
 * ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port): the bit positions are NOT contiguous --
 * TC_PORT_1..3 are bits 12..14 but TC_PORT_4+ restart at bit 21.
 */
#define PARITY_TC_PORT_4 3
static uint32_t
icl_dpclka_tc_clk_off(int tc_port)
{
	if (tc_port < PARITY_TC_PORT_4)
		return 1u << (unsigned)(tc_port + 12);
	return 1u << (unsigned)(tc_port - PARITY_TC_PORT_4 + 21);
}

int
parity_intel_ddi_crt_present(int display_ver)
{
	/* DISPLAY_VER >= 9 -> no DDI CRT. */
	if (display_ver >= 9)
		return 0;
	return 1;
}

/*
 * dvo_port_to_port().  DISPLAY_VER >= 13 uses the xelpd mapping, in which the
 * TypeC ports take the HDMIF..HDMII / DPF..DPI entries -- NOT the HDMIC/HDMID
 * ones a pre-xelpd platform would use.
 */
int
parity_dvo_port_to_port(int display_ver, uint8_t dvo_port)
{
	static const struct { int port; uint8_t a, b; } xelpd[] = {
		{ PARITY_PORT_A,   DVO_PORT_HDMIA, DVO_PORT_DPA },
		{ PARITY_PORT_B,   DVO_PORT_HDMIB, DVO_PORT_DPB },
		{ PARITY_PORT_C,   DVO_PORT_HDMIC, DVO_PORT_DPC },
		{ PARITY_PORT_TC1, DVO_PORT_HDMIF, DVO_PORT_DPF },
		{ PARITY_PORT_TC2, DVO_PORT_HDMIG, DVO_PORT_DPG },
		{ PARITY_PORT_TC3, DVO_PORT_HDMIH, DVO_PORT_DPH },
		{ PARITY_PORT_TC4, DVO_PORT_HDMII, DVO_PORT_DPI }
	};
	static const struct { int port; uint8_t a, b; } legacy[] = {
		{ PARITY_PORT_A, DVO_PORT_HDMIA, DVO_PORT_DPA },
		{ PARITY_PORT_B, DVO_PORT_HDMIB, DVO_PORT_DPB },
		{ PARITY_PORT_C, DVO_PORT_HDMIC, DVO_PORT_DPC },
		{ PARITY_PORT_D, DVO_PORT_HDMID, DVO_PORT_DPD }
	};
	unsigned i;

	if (display_ver >= 13) {
		for (i = 0u; i < (unsigned)(sizeof(xelpd) / sizeof(xelpd[0])); i++)
			if (dvo_port == xelpd[i].a || dvo_port == xelpd[i].b)
				return xelpd[i].port;
		return PARITY_PORT_NONE;
	}
	for (i = 0u; i < (unsigned)(sizeof(legacy) / sizeof(legacy[0])); i++)
		if (dvo_port == legacy[i].a || dvo_port == legacy[i].b)
			return legacy[i].port;
	return PARITY_PORT_NONE;
}

int
parity_intel_port_to_phy(int display_ver, int port)
{
	if (display_ver >= 13 && port >= PARITY_PORT_TC1)
		return PARITY_PHY_F + (port - PARITY_PORT_TC1);
	return PARITY_PHY_A + (port - PARITY_PORT_A);
}

int
parity_intel_phy_is_tc(int display_ver, int phy)
{
	if (display_ver >= 13)
		return (phy >= PARITY_PHY_F && phy <= PARITY_PHY_I) ? 1 : 0;
	return 0;
}

int
parity_intel_ddi_is_tc(int display_ver, int port)
{
	if (display_ver >= 12)
		return (port >= PARITY_PORT_TC1) ? 1 : 0;
	if (display_ver >= 11)
		return (port >= PARITY_PORT_C) ? 1 : 0;
	return 0;
}

/* intel_display_power_ddi_lanes_domain(): d13_port_domains for ver >= 13. */
static int
ddi_lanes_domain(int display_ver, int port)
{
	if (display_ver >= 13) {
		if (port >= PARITY_PORT_A && port <= PARITY_PORT_C)
			return PARITY_PD_PORT_DDI_LANES_A + (port - PARITY_PORT_A);
		if (port >= PARITY_PORT_TC1 && port <= PARITY_PORT_TC4)
			return PARITY_PD_PORT_DDI_LANES_TC1 + (port - PARITY_PORT_TC1);
	}
	/* drm_WARN_ON path: fall back to LANES_A rather than an invalid domain. */
	return PARITY_PD_PORT_DDI_LANES_A;
}

static int
port_in_use(const struct parity_display_nogem *d, int port)
{
	unsigned i;

	for (i = 0u; i < d->num_encoders; i++)
		if (d->encoders[i].port == port)
			return 1;
	return 0;
}

static void
ddi_skip(struct parity_display_nogem *d, int port, int reason)
{
	if (d->num_ddi_skips < (unsigned)PARITY_NOGEM_MAX_ENCODERS) {
		d->ddi_skip_reason[d->num_ddi_skips] = reason;
		d->ddi_skip_port[d->num_ddi_skips] = port;
		d->num_ddi_skips++;
	}
	d->ddi_skipped++;
}

/*
 * intel_ddi_init(), decision half.  Every early return the reference takes is
 * reproduced and recorded; what is NOT done is the DRM encoder registration and
 * the DP/HDMI/AUX/HPD/connector construction that follows it.
 */
static void
intel_ddi_init(struct parity_display_nogem *d, int display_ver, unsigned port_mask,
	const struct parity_vbt_child *child)
{
	struct parity_encoder *e;
	int port, phy;
	int init_hdmi, init_dp;

	d->ddi_init_calls++;

	port = parity_dvo_port_to_port(display_ver, child->dvo_port);
	if (port == PARITY_PORT_NONE) {
		ddi_skip(d, port, PARITY_DDI_SKIP_PORT_NONE);
		return;
	}

	/* port_strap_detected(): straps are not used on skl+. */
	if (display_ver < 9) {
		ddi_skip(d, port, PARITY_DDI_SKIP_STRAP);
		return;
	}

	/* assert_port_valid(): the platform must advertise the port. */
	if ((port_mask & (1u << (unsigned)port)) == 0u) {
		kern_logf("i915: parity WARN Platform does not support port %c\n",
			(char)('A' + port));
		ddi_skip(d, port, PARITY_DDI_SKIP_PORT_INVALID);
		return;
	}

	if (port_in_use(d, port)) {
		kern_logf("i915: parity Port %c already claimed\n", (char)('A' + port));
		ddi_skip(d, port, PARITY_DDI_SKIP_PORT_IN_USE);
		return;
	}

	/* intel_bios_encoder_supports_dsi() -> icl_dsi_init(), a separate path. */
	if (child->device_type & DEVICE_TYPE_MIPI_OUTPUT) {
		ddi_skip(d, port, PARITY_DDI_SKIP_DSI);
		return;
	}

	phy = parity_intel_port_to_phy(display_ver, port);

	/*
	 * intel_hti_uses_phy(): HTI can reserve PHYs.  xe_lpd has no has_hti, so
	 * hti.state stays 0 and no PHY is ever reserved here.
	 */
	if (d->hti_state != 0u) {
		ddi_skip(d, port, PARITY_DDI_SKIP_HTI);
		return;
	}

	{
		/* intel_bios_encoder_supports_dvi / _hdmi / _dp */
		int sup_dvi = (child->device_type & DEVICE_TYPE_TMDS_DVI_SIGNALING) != 0u;
		int sup_hdmi = sup_dvi &&
			((child->device_type & DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0u);

		init_hdmi = sup_dvi || sup_hdmi;
		init_dp = (child->device_type & DEVICE_TYPE_DISPLAYPORT_OUTPUT) != 0u;
	}

	/* intel_bios_encoder_is_lspcon(): HAS_LSPCON is DISPLAY_VER 9..10 only. */

	if (!init_dp && !init_hdmi) {
		kern_logf("i915: parity VBT says port %c is not DVI/HDMI/DP compatible, "
			"respect it\n", (char)('A' + port));
		ddi_skip(d, port, PARITY_DDI_SKIP_NOT_DVI_HDMI_DP);
		return;
	}

	if (d->num_encoders >= (unsigned)PARITY_NOGEM_MAX_ENCODERS)
		return;

	e = &d->encoders[d->num_encoders++];
	e->port = port;
	e->phy = phy;
	e->is_tc = parity_intel_ddi_is_tc(display_ver, port);
	e->power_domain = ddi_lanes_domain(display_ver, port);
	e->init_hdmi = init_hdmi;
	e->init_dp = init_dp;
	e->dvo_port = child->dvo_port;
	e->device_type = child->device_type;
	/*
	 * DISPLAY_VER >= 11 and not ADL-S / RKL / DG1 / JSL / EHL: the icl combo or
	 * TC clock ops, selected by intel_ddi_is_tc().
	 */
	e->clk_funcs = e->is_tc ? PARITY_DDI_CLK_ICL_TC : PARITY_DDI_CLK_ICL_COMBO;
	e->in_use = 1;

	/*
	 * if (init_dp) intel_ddi_init_dp_connector(): for the eDP port this is where the
	 * reference powers the panel logic, reads DPCD / EDID and settles the PPS delays.
	 * A failure takes the reference's `goto err`: the encoder does not survive.
	 */
	if (init_dp && d->dp_connector_init != 0) {
		int crc = d->dp_connector_init(d->dp_connector_ctx, port);

		if (crc == 0) {
			d->edp_port = port;
		} else if (crc < 0) {
			d->edp_init_rc = crc;
			e->in_use = 0;
			d->num_encoders--;
			ddi_skip(d, port, PARITY_DDI_SKIP_EDP_INIT_FAILED);
		}
	}
}

void
parity_intel_setup_outputs(struct parity_display_nogem *d, int display_ver,
	unsigned port_mask, const struct parity_vbt_state *vbt,
	struct osdep_mmio *m)
{
	unsigned i;

	(void)m;

	/* intel_pps_unlock_regs_wa(): HAS_DDI -> returns immediately. */

	d->edp_port = -1;
	d->edp_init_rc = 0;

	/* HAS_DDI(ADL-P) is true. */
	d->crt_present = parity_intel_ddi_crt_present(display_ver);
	/* crt_present would call intel_crt_init(); false on ver >= 9. */

	/* intel_bios_for_each_encoder(dev_priv, intel_ddi_init) */
	for (i = 0u; i < vbt->num_display_devices; i++)
		intel_ddi_init(d, display_ver, port_mask, &vbt->display_devices[i]);

	/* GLK/BXT would run vlv_dsi_init() here. */

	d->outputs_done = 1;
}

/* ---------------- DDI hw state / clock, for P5-c and P5-d ---------------- */

/*
 * intel_ddi_get_encoder_pipes() + intel_ddi_get_hw_state().  The caller passes
 * the set of pipes whose transcoder power is on; the reference gates each
 * TRANS_DDI_FUNC_CTL read on that same power state.
 */
int
parity_intel_ddi_get_hw_state(struct parity_display_nogem *d,
	struct parity_encoder *e, struct osdep_mmio *m, unsigned pipe_mask_avail,
	unsigned *pipe_out)
{
	uint32_t tmp;
	unsigned pipe_mask = 0u;
	unsigned mst_pipe_mask = 0u;
	unsigned p;

	(void)d;
	e->pipe_mask = 0u;
	e->is_mst = 0;

	tmp = osdep_mmio_read32(m, 0x64000u + (unsigned)e->port * 0x100u);
	if (!(tmp & (1u << 31)))          /* DDI_BUF_CTL_ENABLE */
		return 0;

	/*
	 * HAS_TRANSCODER(TRANSCODER_EDP) is false on DISPLAY_VER >= 12, so the
	 * PORT_A / TRANSCODER_EDP special case does not apply here.
	 */
	for (p = 0u; p < 4u; p++) {
		unsigned port_sel;

		if ((pipe_mask_avail & (1u << p)) == 0u)
			continue;

		tmp = osdep_mmio_read32(m, 0x60400u + p * 0x1000u);

		/* DISPLAY_VER >= 12: TGL_TRANS_DDI_PORT_MASK / SELECT_PORT. */
		port_sel = (tmp >> 27) & 0xfu;            /* TGL port select field */
		if (port_sel != ((unsigned)e->port + 1u))
			continue;

		/* TRANS_DDI_MODE_SELECT_DP_MST == 3 in bits 26:24. */
		if (((tmp >> 24) & 0x7u) == 3u)
			mst_pipe_mask |= (1u << p);

		pipe_mask |= (1u << p);
	}

	if (pipe_mask == 0u)
		return 0;

	if (mst_pipe_mask == 0u && popcount4(pipe_mask) > 1u) {
		kern_logf("i915: parity Multiple pipes for port %c (pipe_mask %02x)\n",
			(char)('A' + e->port), pipe_mask);
		/* keep the lowest set pipe, as the reference does */
		pipe_mask &= (unsigned)(-(int)pipe_mask);
	}
	if (mst_pipe_mask != 0u && mst_pipe_mask != pipe_mask)
		kern_logf("i915: parity Conflicting MST and non-MST state for port %c\n",
			(char)('A' + e->port));
	else
		e->is_mst = (mst_pipe_mask != 0u);

	e->pipe_mask = pipe_mask;

	if (e->is_mst)
		return 0;   /* intel_ddi_get_hw_state() reports false for MST */

	if (pipe_out != 0) {
		unsigned q;

		for (q = 0u; q < 4u; q++)
			if (pipe_mask & (1u << q)) { *pipe_out = q; break; }
	}
	return 1;
}

int
parity_intel_ddi_is_clock_enabled(struct parity_encoder *e, struct osdep_mmio *m)
{
	if (e->clk_funcs == PARITY_DDI_CLK_ICL_COMBO) {
		uint32_t off = icl_dpclka_ddi_clk_off(e->phy);

		return (osdep_mmio_read32(m, ICL_DPCLKA_CFGCR0) & off) ? 0 : 1;
	}
	if (e->clk_funcs == PARITY_DDI_CLK_ICL_TC) {
		int tc = e->port - PARITY_PORT_TC1;
		uint32_t tmp = osdep_mmio_read32(m,
			PORT_CLK_SEL_A + (unsigned)e->port * 4u);

		if ((tmp & DDI_CLK_SEL_MASK) == DDI_CLK_SEL_NONE)
			return 0;
		tmp = osdep_mmio_read32(m, ICL_DPCLKA_CFGCR0);
		return (tmp & icl_dpclka_tc_clk_off(tc)) ? 0 : 1;
	}
	return 0;
}

void
parity_intel_ddi_disable_clock(struct parity_display_nogem *d,
	struct parity_encoder *e, struct osdep_mmio *m)
{
	if (e->clk_funcs == PARITY_DDI_CLK_ICL_COMBO) {
		(void)rmw(d, m, ICL_DPCLKA_CFGCR0, 0u,
			icl_dpclka_ddi_clk_off(e->phy));
		return;
	}
	if (e->clk_funcs == PARITY_DDI_CLK_ICL_TC) {
		int tc = e->port - PARITY_PORT_TC1;

		(void)rmw(d, m, ICL_DPCLKA_CFGCR0, 0u, icl_dpclka_tc_clk_off(tc));
		wr(d, m, PORT_CLK_SEL_A + (unsigned)e->port * 4u, DDI_CLK_SEL_NONE);
	}
}

/* ---------------- P5-c: intel_modeset_readout_hw_state() ---------------- */

#define TRANSCONF_A                 0x70008u   /* _MMIO_PIPE2 stride 0x1000 */
#define TRANSCONF_ENABLE            (1u << 31)
#define TRANS_DDI_FUNC_CTL_A        0x60400u   /* _MMIO_TRANS2 stride 0x1000 */
#define TRANS_DDI_FUNC_ENABLE       (1u << 31)
#define TRANS_DDI_EDP_INPUT_MASK    (7u << 12)
#define TRANS_DDI_EDP_INPUT_A_ON    (0u << 12)
#define TRANS_DDI_EDP_INPUT_A_ONOFF (4u << 12)
#define TRANS_DDI_EDP_INPUT_B_ONOFF (5u << 12)
#define TRANS_DDI_EDP_INPUT_C_ONOFF (6u << 12)
#define TRANS_DDI_EDP_INPUT_D_ONOFF (7u << 12)
#define TRANS_HTOTAL_A              0x60000u
#define TRANS_VTOTAL_A              0x6000cu
#define PIPESRC_A                   0x6001cu
#define PLANE_CTL_1_A               0x70180u
#define PLANE_CTL_ENABLE            (1u << 31)
#define PLL_ENABLE_BIT              (1u << 31)

/* enum transcoder: A..D map 1:1 to the pipes, then EDP, DSI_0, DSI_1. */
#define PARITY_TRANSCODER_EDP   4
#define PARITY_TRANSCODER_DSI_0 5
#define PARITY_TRANSCODER_DSI_1 6

/* The DSI transcoders are not at a flat 0x1000 stride from TRANSCODER_A. */
static uint32_t
trans_reg(unsigned trans, uint32_t reg_a)
{
	static const uint32_t off[7] = {
		0x00000u, 0x01000u, 0x02000u, 0x03000u,   /* A..D */
		0x00000u,                                  /* EDP: not on xe_lpd */
		0x0b000u, 0x0b800u                         /* DSI_0, DSI_1 */
	};

	if (trans > 6u)
		return reg_a;
	return reg_a + off[trans];
}

static int
trans_power_on(struct parity_power_domains *pd, struct parity_pw_ctx *pwc,
	unsigned trans)
{
	if (trans > 3u)
		return 1;   /* DSI transcoders have no dedicated pipe well here */
	return parity_display_power_is_enabled(pd,
		(enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + trans), pwc);
}

/*
 * hsw_enabled_transcoders().  On xe_lpd the runtime cpu_transcoder_mask has no
 * TRANSCODER_EDP, so the panel-transcoder loop only covers DSI_0/DSI_1.
 * Bigjoiner is not modelled: no bigjoiner-slave arm (recorded, not faked).
 */
unsigned
parity_hsw_enabled_transcoders(struct parity_display_nogem *d, int display_ver,
	unsigned pipe, struct osdep_mmio *m, struct parity_power_domains *pd,
	struct parity_pw_ctx *pwc)
{
	unsigned enabled = 0u;
	unsigned t;

	(void)d;

	/* hsw_panel_transcoders(): EDP + (ver >= 11) DSI_0/DSI_1, intersected with
	 * the runtime mask -- xe_lpd advertises DSI_0/DSI_1 but not EDP. */
	if (display_ver >= 11) {
		for (t = PARITY_TRANSCODER_DSI_0; t <= PARITY_TRANSCODER_DSI_1; t++) {
			uint32_t tmp = 0u;
			unsigned trans_pipe;

			if (trans_power_on(pd, pwc, t))
				tmp = osdep_mmio_read32(m,
					trans_reg(t, TRANS_DDI_FUNC_CTL_A));

			if (!(tmp & TRANS_DDI_FUNC_ENABLE))
				continue;

			switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
			case TRANS_DDI_EDP_INPUT_B_ONOFF: trans_pipe = 1u; break;
			case TRANS_DDI_EDP_INPUT_C_ONOFF: trans_pipe = 2u; break;
			case TRANS_DDI_EDP_INPUT_D_ONOFF: trans_pipe = 3u; break;
			case TRANS_DDI_EDP_INPUT_A_ON:
			case TRANS_DDI_EDP_INPUT_A_ONOFF:
			default:
				trans_pipe = 0u;
				break;
			}

			if (trans_pipe == pipe)
				enabled |= (1u << t);
		}
	}

	/* single pipe or bigjoiner master: the transcoder that matches the pipe. */
	{
		uint32_t tmp = 0u;

		if (trans_power_on(pd, pwc, pipe))
			tmp = osdep_mmio_read32(m, trans_reg(pipe, TRANS_DDI_FUNC_CTL_A));
		if (tmp & TRANS_DDI_FUNC_ENABLE)
			enabled |= (1u << pipe);
	}

	return enabled;
}

/* intel_get_transcoder_timings(), the parts the sanitize phase can use. */
static void
get_transcoder_timings(struct parity_crtc_state *cs, unsigned trans,
	struct osdep_mmio *m)
{
	uint32_t tmp;

	tmp = osdep_mmio_read32(m, trans_reg(trans, TRANS_HTOTAL_A));
	cs->hdisplay = (tmp & 0xffffu) + 1u;          /* HACTIVE_MASK */
	cs->htotal = ((tmp >> 16) & 0xffffu) + 1u;    /* HTOTAL_MASK */

	tmp = osdep_mmio_read32(m, trans_reg(trans, TRANS_VTOTAL_A));
	cs->vdisplay = (tmp & 0xffffu) + 1u;          /* VACTIVE_MASK */
	cs->vtotal = ((tmp >> 16) & 0xffffu) + 1u;    /* VTOTAL_MASK */
}

/* hsw_get_pipe_config(): gated on the pipe power, then the transcoder state. */
static int
hsw_get_pipe_config(struct parity_display_nogem *d, int display_ver,
	struct parity_crtc *crtc, struct osdep_mmio *m,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc)
{
	struct parity_crtc_state *cs = &crtc->state;
	unsigned enabled;
	unsigned trans;
	uint32_t tmp;

	if (!parity_display_power_is_enabled(pd,
		(enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + crtc->pipe), pwc)) {
		cs->power_gated = 1;
		return 0;
	}

	enabled = parity_hsw_enabled_transcoders(d, display_ver, crtc->pipe, m, pd, pwc);
	cs->enabled_transcoders = enabled;
	if (enabled == 0u)
		return 0;

	/* With the exception of DSI there is only ever one; pick the first. */
	for (trans = 0u; trans < 7u; trans++)
		if (enabled & (1u << trans))
			break;
	cs->cpu_transcoder = (int)trans;

	if (!trans_power_on(pd, pwc, trans))
		return 0;

	/* TRANSCONF is addressed by the PIPE offsets (0x70008 + pipe*0x1000), not
	 * the transcoder ones; a DSI transcoder drives its own pipe TRANSCONF. */
	tmp = osdep_mmio_read32(m, TRANSCONF_A + crtc->pipe * 0x1000u);
	cs->transconf = tmp;
	if (!(tmp & TRANSCONF_ENABLE))
		return 0;

	get_transcoder_timings(cs, trans, m);

	tmp = osdep_mmio_read32(m, trans_reg(trans, PIPESRC_A));
	cs->pipe_src_w = ((tmp >> 16) & 0xffffu) + 1u;
	cs->pipe_src_h = (tmp & 0xffffu) + 1u;

	return 1;
}

/* readout_plane_state(): skl_plane_get_hw_state() per plane. */
static void
readout_plane_state(struct parity_display_nogem *d, struct osdep_mmio *m,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc)
{
	unsigned pi, pl;

	for (pi = 0u; pi < (unsigned)PARITY_NOGEM_MAX_PIPES; pi++) {
		struct parity_crtc *crtc = &d->crtcs[pi];
		int powered;

		if (!crtc->in_use)
			continue;

		powered = parity_display_power_is_enabled(pd,
			(enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pi), pwc);
		crtc->state.active_planes = 0u;

		for (pl = 0u; pl < crtc->num_planes; pl++) {
			struct parity_plane *p = &crtc->planes[pl];
			uint32_t ctl = 0u;

			p->visible = 0;
			if (!powered)
				continue;
			if (p->type == PARITY_PLANE_CURSOR)
				continue;   /* CUR_CTL is a different register block */

			ctl = osdep_mmio_read32(m, PLANE_CTL_1_A + pi * 0x1000u +
				(unsigned)p->id * 0x100u);
			if (ctl & PLANE_CTL_ENABLE) {
				p->visible = 1;
				crtc->state.active_planes |= (1u << (unsigned)p->id);
				d->readout_planes_visible++;
			}
		}
	}
}

/* encoder->get_config() for the linked encoders, then intel_dpll_readout_hw_state() (E-121) */
void
parity_intel_dpll_readout(struct parity_display_nogem *d, struct osdep_mmio *m)
{
	unsigned i;

	/*
	 * encoder->get_config() for the linked encoders: icl_ddi_combo_get_config() ->
	 * intel_ddi_get_clock(icl_ddi_combo_get_pll()) = _icl_ddi_get_pll(ICL_DPCLKA_CFGCR0,
	 * ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy), _SHIFT(phy) = 2 * phy): the id of the PLL feeding the PHY.
	 * icl_ddi_tc_get_pll() is not ported: a linked TC encoder leaves its PLL unknown (readout incomplete).
	 */
	{
		int tc_unknown = 0;

		for (i = 0u; i < d->num_encoders; i++) {
			struct parity_encoder *e = &d->encoders[i];

			e->shared_dpll_id = -1;
			if (!e->crtc_linked)
				continue;
			if (e->clk_funcs == PARITY_DDI_CLK_ICL_COMBO) {
				e->dpclka_cfgcr0 = osdep_mmio_read32(m, 0x164280u);
				e->shared_dpll_id = (int)((e->dpclka_cfgcr0 >> (2u * (unsigned)e->phy)) & 0x3u);
				kern_logf("i915: parity P5c [ENCODER port %c] get_config: ICL_DPCLKA_CFGCR0=0x%08x -> "
					"shared_dpll id %d\n", (char)('A' + e->port), e->dpclka_cfgcr0, e->shared_dpll_id);
			} else {
				tc_unknown = 1;
				kern_logf("i915: parity P5c [ENCODER port %c] get_config: icl_ddi_tc_get_pll not ported -- the PLL "
					"of this active link is unknown\n", (char)('A' + e->port));
			}
		}
		for (i = 0u; i < d->num_dplls; i++)
			d->dplls[i].readout_incomplete = tc_unknown;
	}

	/* intel_dpll_readout_hw_state() -> readout_dpll_hw_state() */
	for (i = 0u; i < d->num_dplls; i++) {
		struct parity_dpll *pll = &d->dplls[i];
		unsigned c, k;

		pll->on = (osdep_mmio_read32(m, pll->enable_reg) & PLL_ENABLE_BIT) ? 1 : 0;
		pll->pipe_mask = 0u;
		/* for_each_intel_crtc: crtc_state->hw.active && crtc_state->shared_dpll == pll -> pipe_mask |= BIT(pipe) */
		for (c = 0u; c < (unsigned)PARITY_NOGEM_MAX_PIPES; c++) {
			if (!d->crtcs[c].state.active)
				continue;
			for (k = 0u; k < d->num_encoders; k++) {
				const struct parity_encoder *e = &d->encoders[k];

				if (e->crtc_linked && (e->pipe_mask & (1u << c)) != 0u && e->shared_dpll_id == pll->id)
					pll->pipe_mask |= 1u << c;
			}
		}
		pll->active_mask = pll->pipe_mask;
	}
}

void
parity_intel_modeset_readout_hw_state(struct parity_display_nogem *d,
	int display_ver, struct osdep_mmio *m, struct parity_power_domains *pd,
	struct parity_pw_ctx *pwc)
{
	unsigned i;

	d->active_pipes = 0u;
	d->readout_crtcs = 0u;
	d->readout_planes_visible = 0u;
	d->readout_encoders_linked = 0u;
	d->readout_dplls_on = 0u;

	/* for_each_intel_crtc: intel_crtc_get_pipe_config() */
	for (i = 0u; i < (unsigned)PARITY_NOGEM_MAX_PIPES; i++) {
		struct parity_crtc *crtc = &d->crtcs[i];
		struct parity_crtc_state *cs = &crtc->state;

		if (!crtc->in_use)
			continue;

		/* intel_crtc_state_reset() before each readout. */
		cs->active = 0;
		cs->enable = 0;
		cs->cpu_transcoder = -1;
		cs->enabled_transcoders = 0u;
		cs->transconf = 0u;
		cs->power_gated = 0;
		cs->hdisplay = cs->htotal = cs->vdisplay = cs->vtotal = 0u;
		cs->pipe_src_w = cs->pipe_src_h = 0u;

		cs->active = hsw_get_pipe_config(d, display_ver, crtc, m, pd, pwc);
		cs->enable = cs->active;
		crtc->enabled = cs->enable;
		crtc->active = cs->active;

		if (cs->active)
			d->active_pipes |= (1u << i);

		d->readout_crtcs++;
		kern_logf("i915: parity P5c [CRTC:%c] hw state readout: %s%s\n",
			(char)('A' + i), cs->active ? "enabled" : "disabled",
			cs->power_gated ? " (power gated)" : "");
	}

	readout_plane_state(d, m, pd, pwc);

	/* for_each_intel_encoder: encoder->get_hw_state() */
	for (i = 0u; i < d->num_encoders; i++) {
		struct parity_encoder *e = &d->encoders[i];
		unsigned pipe = 0u;
		unsigned avail = 0u;
		unsigned p;

		for (p = 0u; p < 4u; p++)
			if (trans_power_on(pd, pwc, p))
				avail |= (1u << p);

		if (parity_intel_ddi_get_hw_state(d, e, m, avail, &pipe)) {
			e->crtc_linked = 1;
			d->readout_encoders_linked++;
		} else {
			e->crtc_linked = 0;
		}
		kern_logf("i915: parity P5c [ENCODER port %c] hw state readout: %s "
			"pipe_mask=0x%x mst=%d\n", (char)('A' + e->port),
			e->crtc_linked ? "enabled" : "disabled", e->pipe_mask, e->is_mst);
	}

	parity_intel_dpll_readout(d, m);
	for (i = 0u; i < d->num_dplls; i++) {
		struct parity_dpll *pll = &d->dplls[i];

		if (pll->on)
			d->readout_dplls_on++;
		kern_logf("i915: parity P5c %s hw state readout: pipe_mask 0x%x, on %d\n",
			pll->name, pll->pipe_mask, pll->on);
	}

	/*
	 * The connector loop is empty: P5's approved scope stops before connector
	 * creation, so connector_mask / encoder_mask are not updated and the
	 * connector-driven sanitize arms cannot fire.  Recorded, not hidden.
	 */

	d->readout_detail_unimplemented = 1;
	d->readout_done = 1;
}

/* ---------------- P5-d: the sanitize half ---------------- */

#define PIPEDMC_CONTROL_A   0x45250u   /* _MMIO_PIPE stride 4 */
#define PIPEDMC_ENABLE      (1u << 0)
#define ILK_DPFC_CONTROL_A  0x43208u   /* _MMIO_PIPE(fbc_id, 0x43208, 0x43248) */
#define ILK_DPFC_CONTROL_B  0x43248u
#define DPFC_CTL_EN         (1u << 31)
#define TRANS_CMTG_CHICKEN  0x6fa90u
#define DISABLE_DPT_CLK_GATING (1u << 22)

/*
 * intel_early_display_was(): Display WA #1185 / Wa_14010480278 is
 * IS_DISPLAY_VER(10, 12) -- ADL-P is 13, so it is NOT applied.  The HSW and
 * KBL/CFL/CML arms are older still.  The gate is kept so the boundary is real.
 */
static void
intel_early_display_was(struct parity_display_nogem *d, struct osdep_mmio *m,
	int display_ver)
{
	(void)m;
	if (display_ver >= 10 && display_ver <= 12) {
		/* GEN9_CLKGATE_DIS_0 |= DARBF_GATING_DIS */
		d->early_display_was_applied = 1;
	}
}

/* intel_fbc_sanitize(): an FBC left active by the pre-OS is deactivated. */
static void
intel_fbc_sanitize(struct parity_display_nogem *d, struct osdep_mmio *m,
	unsigned fbc_mask)
{
	unsigned id;

	/* for_each_intel_fbc(): the runtime fbc_mask owned by the P3 display state
	 * (ADL-P has BIT(INTEL_FBC_A) only). */
	for (id = 0u; id < 2u; id++) {
		uint32_t reg;
		uint32_t ctl;

		if ((fbc_mask & (1u << id)) == 0u)
			continue;

		reg = (id == 0u) ? ILK_DPFC_CONTROL_A : ILK_DPFC_CONTROL_B;
		ctl = osdep_mmio_read32(m, reg);
		if (!(ctl & DPFC_CTL_EN))          /* ilk_fbc_is_active() */
			continue;

		/* ilk_fbc_deactivate(): clear DPFC_CTL_EN. */
		wr(d, m, reg, ctl & ~DPFC_CTL_EN);
		d->fbc_deactivated++;
	}
}

/*
 * intel_ddi_sanitize_encoder_pll_mapping().  A disabled encoder must not leave
 * its DDI clock ungated.
 */
static void
sanitize_encoder_pll_mapping(struct parity_display_nogem *d,
	struct parity_encoder *e, struct osdep_mmio *m)
{
	int ddi_clk_needed = e->crtc_linked;

	if (ddi_clk_needed)
		return;
	if (!parity_intel_ddi_is_clock_enabled(e, m))
		return;

	kern_logf("i915: parity [ENCODER port %c] is disabled with an ungated DDI "
		"clock, gate it\n", (char)('A' + e->port));
	parity_intel_ddi_disable_clock(d, e, m);
	d->encoder_clocks_gated++;
}

/*
 * intel_sanitize_crtc().  An inactive pipe returns immediately; an active pipe
 * with encoders also returns.  The remaining case (active, no encoders) needs
 * the full modeset disable, which is out of the approved P5 scope.
 */
static int
intel_sanitize_crtc(struct parity_display_nogem *d, struct parity_crtc *crtc)
{
	unsigned i;
	int has_encoders = 0;

	if (!crtc->state.active)
		return 0;

	/*
	 * An active pipe would first have every non-primary plane disabled and
	 * the BIOS background colour committed; both need the plane/colour commit
	 * paths that belong to a later stage.
	 */
	for (i = 0u; i < d->num_encoders; i++)
		if (d->encoders[i].crtc_linked &&
		    (d->encoders[i].pipe_mask & (1u << crtc->pipe)) != 0u)
			has_encoders = 1;

	if (has_encoders)
		return 0;

	kern_logf("i915: parity WARN [CRTC:%c] is active with no encoders: "
		"intel_crtc_disable_noatomic() is NOT implemented, pipe left as-is\n",
		(char)('A' + crtc->pipe));
	d->crtc_disable_noatomic_unimplemented = 1;
	return 0;
}

/* adlp_cmtg_clock_gating_wa(): ADL-P STEP_A0..B0 and DPLL0 only. */
static void
adlp_cmtg_clock_gating_wa(struct parity_display_nogem *d, struct osdep_mmio *m,
	int display_ver, int display_step, const struct parity_dpll *pll)
{
	uint32_t val;

	/* PARITY_STEP_A0 == 1 .. PARITY_STEP_B0 == 5 (exclusive upper bound). */
	if (!(display_ver == 13 && display_step >= 1 && display_step < 5) ||
	    pll->id != 0 /* DPLL_ID_ICL_DPLL0 */)
		return;

	/* Wa_16011069516:adl-p[a0] -- a double read, then set the gating bit. */
	val = osdep_mmio_read32(m, TRANS_CMTG_CHICKEN);
	val = rmw(d, m, TRANS_CMTG_CHICKEN, 0xffffffffu, DISABLE_DPT_CLK_GATING);
	if (val & ~DISABLE_DPT_CLK_GATING)
		kern_logf("i915: parity Unexpected flags in TRANS_CMTG_CHICKEN: %08x\n",
			val);
	d->cmtg_wa_applied = 1;
}

/* intel_dpll_sanitize_state() -> sanitize_dpll_state(). */
void
parity_intel_dpll_sanitize_state(struct parity_display_nogem *d, struct osdep_mmio *m,
	int display_ver, int display_step)
{
	unsigned i;

	for (i = 0u; i < d->num_dplls; i++) {
		struct parity_dpll *pll = &d->dplls[i];

		if (!pll->on)
			continue;

		adlp_cmtg_clock_gating_wa(d, m, display_ver, display_step, pll);

		if (pll->active_mask != 0u)
			continue;
		if (pll->readout_incomplete) {
			kern_logf("i915: parity %s enabled, active_mask 0 but the readout is incomplete (an active link's PLL is "
				"unknown): NOT disabled\n", pll->name);
			continue;
		}

		kern_logf("i915: parity %s enabled but not in use, disabling\n",
			pll->name);
		/* _intel_disable_shared_dpll(): clear PLL_ENABLE. */
		(void)rmw(d, m, pll->enable_reg, PLL_ENABLE_BIT, 0u);
		pll->on = 0;
		d->dplls_disabled++;
	}
}

/*
 * intel_power_domains_sanitize_state(): walk the wells in REVERSE and turn off
 * any the BIOS left on that nothing references.  Note this uses a FRESH
 * is_enabled() read, unlike __intel_display_power_is_enabled()'s cached check.
 */
static void
intel_power_domains_sanitize_state(struct parity_display_nogem *d,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc)
{
	unsigned i = pd->num_power_wells;

	while (i-- > 0u) {
		struct parity_power_well *w = &pd->power_wells[i];

		if (w->always_on || w->refcount != 0u ||
		    !parity_power_well_is_enabled(w, pwc))
			continue;

		kern_logf("i915: parity BIOS left unused %s power well enabled, "
			"disabling it\n", w->name);
		(void)parity_power_well_disable(w, pwc);
		d->wells_disabled++;
	}
}

void
parity_intel_modeset_sanitize_hw_state(struct parity_display_nogem *d,
	int display_ver, int display_step, unsigned fbc_mask, struct osdep_mmio *m,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc)
{
	/*
	 * E-124 (N1): the takeover of the display the FIRMWARE lit is ported and runs later, with the
	 * reference own readout and its power-domain accounting.  While that display is still running this
	 * stage must not turn anything off: its readout does not take the references the reference does, so
	 * the well feeding the live port looks unused.  On bare metal that is exactly what happened --
	 * "BIOS left unused DDI_IO A power well enabled, disabling it" killed the picture (the backlight
	 * stayed on, the content went black) long before N1 ran.
	 */
	const int keep_firmware_display = PARITY_N1_TEST && d->active_pipes != 0u;

	unsigned i;

	/*
	 * The reference brackets the whole of intel_modeset_setup_hw_state() in a
	 * POWER_DOMAIN_INIT reference; P3's init reference is still held here.
	 */
	intel_early_display_was(d, m, display_ver);

	/* intel_pch_sanitize(): HAS_PCH_IBX only. */
	d->pch_sanitize_applied = 0;

	/*
	 * Per CRTC: start with underrun reporting disabled on active pipes (and,
	 * on non-GMCH, on inactive ones too), reset vblank, and for an ACTIVE pipe
	 * enable its pipe DMC and turn vblank on.
	 */
	for (i = 0u; i < (unsigned)PARITY_NOGEM_MAX_PIPES; i++) {
		struct parity_crtc *crtc = &d->crtcs[i];

		if (!crtc->in_use)
			continue;

		/* intel_sanitize_fifo_underrun_reporting(): !active && !HAS_GMCH */
		crtc->fifo_underrun_reporting = (!crtc->state.active) ? 1 : 0;

		/* drm_crtc_vblank_reset() */
		d->vblank_resets++;

		if (crtc->state.active) {
			/* intel_dmc_enable_pipe(): PIPEDMC_CONTROL(pipe) |= ENABLE */
			(void)rmw(d, m, PIPEDMC_CONTROL_A + i * 4u, 0u, PIPEDMC_ENABLE);
			d->dmc_pipes_enabled++;
			/* intel_crtc_vblank_on() */
			d->vblank_on_count++;
		}
	}

	intel_fbc_sanitize(d, m, fbc_mask);

	/* intel_sanitize_plane_mapping(): DISPLAY_VER >= 4 returns immediately. */
	d->plane_mapping_sanitized = (display_ver < 4) ? 1 : 0;

	if (!keep_firmware_display)
		for (i = 0u; i < d->num_encoders; i++)
			sanitize_encoder_pll_mapping(d, &d->encoders[i], m);

	/*
	 * intel_modeset_update_connector_atomic_state(): no connectors exist in
	 * the approved P5 scope, so there is nothing to update.
	 */

	/* intel_sanitize_all_crtcs() */
	if (!keep_firmware_display) {
		for (i = 0u; i < (unsigned)PARITY_NOGEM_MAX_PIPES; i++)
			if (d->crtcs[i].in_use)
				(void)intel_sanitize_crtc(d, &d->crtcs[i]);

		parity_intel_dpll_sanitize_state(d, m, display_ver, display_step);
	}

	/* intel_wm_get_hw_state(): skl_wm_get_hw_state + skl_wm_sanitize. */
	d->wm_hw_state_read = 1;

	if (!keep_firmware_display)
		intel_power_domains_sanitize_state(d, pd, pwc);
	else
		kern_logf("i915: parity P5c: the firmware display on pipes 0x%x is KEPT (N1 takes it over "
			"later): the encoder clock gating, the crtc / DPLL sanitize and the unused-well disable are "
			"not run here\n", d->active_pipes);

	d->sanitize_done = 1;
}

void
parity_intel_display_nogem_fini(struct parity_display_nogem *d)
{
	unsigned i;

	for (i = 0u; i < (unsigned)PARITY_NOGEM_MAX_PIPES; i++)
		d->crtcs[i].in_use = 0;
	d->num_crtcs = 0u;
	d->num_dplls = 0u;
	d->dpll_mgr_present = 0;
	d->inited = 0;
}
