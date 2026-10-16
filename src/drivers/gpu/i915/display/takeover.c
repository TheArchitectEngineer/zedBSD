/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_modeset_setup.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 *
 * Read out the current hardware modeset state, and sanitize it to the current
 * state.
 */

/*
 * The front of the display start that the firmware's display still needs
 * from the older code: the VGA arbiter client and the legacy VGA plane the
 * firmware may have left on, and the output setup (the front of the Linux
 * intel_display_driver_probe_nogem(): the watermark latencies, the crtc,
 * plane, DPLL and encoder records).  What the firmware left lit is read,
 * sanitized and stopped by xd-takeover.c and xd-display.c.
 *
 * Errors are positive errno values.
 */

#include "internal.h"
#include "modeset-internal.h"
#include "watermark-internal.h"
#include "takeover-internal.h"
#include "takeover.h"
#include <kern/kcrt.h>

#include "clock.h"
#include "color.h"
#include "ddi.h"
#include "dmc.h"
#include "dp.h"
#include "dp-ext-kern.h"
#include "pipe.h"
#include "power.h"
#include "scanout.h"
#include "tc-kern.h"
#include "vblank.h"
#include "vbt-parse.h"
#include "watermark.h"
#include "xd-clock.h"
#include "xd.h"

#include "../ggtt.h"
#include "../mmio.h"
#include "../power.h"
#include "../sync.h"
#include "../trace.h"

#include "bootloader/include/amd64-handoff.h"
#include "drivers/platform/pcat/graphics/backend.h"

#include <drivers/pci/pci.h>
#include <hal/hal.h>
#include <kern/device-io.h>

#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/platform.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The trace stage of the display noirq bring-up.
 *
 * The VGA registration was recorded under this stage number before the
 * stages lost their enum; the value is kept so the trace reads the same.
 */
#define I915_TAKEOVER_TRACE_STAGE		4U

/* The PCODE mailbox commands of the watermark latencies and the SAGV block time. */
#define I915_GEN9_PCODE_READ_MEM_LATENCY	0x6U
#define I915_GEN9_MEM_LATENCY_LEVEL_0_4_MASK	0x000000ffU
#define I915_GEN9_MEM_LATENCY_LEVEL_1_5_MASK	0x0000ff00U
#define I915_GEN9_MEM_LATENCY_LEVEL_2_6_MASK	0x00ff0000U
#define I915_GEN9_MEM_LATENCY_LEVEL_3_7_MASK	0xff000000U
#define I915_GEN12_PCODE_READ_SAGV_BLOCK_TIME_US 0x23U

/* The panel power sequencer and the South Display Engine GMBUS bases. */
#define I915_PPS_BASE				0x61200U
#define I915_PCH_DISPLAY_BASE			0xc0000U
#define I915_GMBUS0_OFF				0x5100U
#define I915_GMBUS4_OFF				0x5110U
#define I915_GMBUS_RATE_100KHZ			0U

/* The registers of every arm of intel_display_wa_apply(), not only Alder Lake-P's. */
#define I915_ILK_DPFC_CHICKEN_FBC_A		0x43224U	/* ILK_DPFC_CHICKEN(INTEL_FBC_A) */
#define I915_DPFC_CHICKEN_COMP_DUMMY_PIXEL	(1U << 14)	/* glk+ */
#define I915_CLKREQ_POLICY			0x101038U
#define I915_CLKREQ_POLICY_MEM_UP_OVRD		(1U << 1)
#define I915_ICL_DELAY_PMRSP			(1U << 22)
#define I915_GEN9_CLKGATE_DIS_5			0x46540U
#define I915_DPCE_GATING_DIS			(1U << 17)
#define I915_GEN8_CHICKEN_DCPR_1		0x46430U
#define I915_DDI_CLOCK_REG_ACCESS		(1U << 7)

/* The VGA plane control (display 5 and later) and the standard VGA ports (video/vga.h). */
#define I915_VGA_SEQ_I				0x3C4U	/* sequencer index */
#define I915_VGA_SEQ_D				0x3C5U	/* sequencer data */
#define I915_VGA_SR01_SCREEN_OFF		0x20U	/* SR01 bit 5: screen off */
#define I915_VGA_MIS_R				0x3CCU	/* Misc Output read */
#define I915_VGA_MIS_W				0x3C2U	/* Misc Output write */

/* The GMCH control word of the host bridge and its VGA-disable bit. */
#define I915_SNB_GMCH_CTRL			0x50U	/* display 6 and later */
#define I915_INTEL_GMCH_CTRL			0x52U	/* older */
#define I915_INTEL_GMCH_VGA_DISABLE		(1U << 1)

/* The PCI VGA display class (base 0x03, subclass 0x00). */
#define I915_PCI_CLASS_VGA_MASK			0xffff00U
#define I915_PCI_CLASS_VGA			0x030000U

/* The ICL+ shared DPLL enable registers (read by the firmware's log). */

/* enum intel_dpll_id, ICL naming (values preserved). */

/* The GMBUS pin indices (intel_gmbus.h). */
#define I915_GMBUS_PIN_1_BXT			1U
#define I915_GMBUS_PIN_2_BXT			2U
#define I915_GMBUS_PIN_3_BXT			3U
#define I915_GMBUS_PIN_9_TC1_ICP		9U
#define I915_GMBUS_PIN_10_TC2_ICP		10U
#define I915_GMBUS_PIN_11_TC3_ICP		11U
#define I915_GMBUS_PIN_12_TC4_ICP		12U
#define I915_GMBUS_PIN_13_TC5_TGP		13U
#define I915_GMBUS_PIN_14_TC6_TGP		14U

/* The GPIO ordinals of the ICP pin table. */
#define I915_GPIOB				1U
#define I915_GPIOC				2U
#define I915_GPIOD				3U
#define I915_GPIOJ				9U
#define I915_GPIOK				10U
#define I915_GPIOL				11U
#define I915_GPIOM				12U
#define I915_GPION				13U
#define I915_GPIOO				14U

/*
 * The VBT DVO_PORT_* codes the port mapping uses.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_vbt_defs.h (DVO_PORT_*).
 */
#define I915_DVO_PORT_HDMIA			0U
#define I915_DVO_PORT_HDMIB			1U
#define I915_DVO_PORT_HDMIC			2U
#define I915_DVO_PORT_HDMID			3U
#define I915_DVO_PORT_HDMIF			14U
#define I915_DVO_PORT_HDMIG			16U
#define I915_DVO_PORT_HDMIH			18U
#define I915_DVO_PORT_HDMII			20U
#define I915_DVO_PORT_DPA			10U
#define I915_DVO_PORT_DPB			7U
#define I915_DVO_PORT_DPC			8U
#define I915_DVO_PORT_DPD			9U
#define I915_DVO_PORT_DPF			13U
#define I915_DVO_PORT_DPG			15U
#define I915_DVO_PORT_DPH			17U
#define I915_DVO_PORT_DPI			19U

/* The VBT child device types the output setup decides on. */
#define I915_DEVICE_TYPE_TMDS_DVI_SIGNALING	(1U << 4)
#define I915_DEVICE_TYPE_NOT_HDMI_OUTPUT	(1U << 11)
#define I915_DEVICE_TYPE_DISPLAYPORT_OUTPUT	(1U << 2)
#define I915_DEVICE_TYPE_MIPI_OUTPUT		(1U << 10)

/*
 * One pin of the ICP GMBUS pin table (gmbus_pins_icp[]).
 *
 * The pin number is the index of the pin record it fills.
 */
struct i915_gmbus_pin_desc {
	unsigned pin;
	const char *name;
	unsigned gpio;
};

/*
 * One port of the VBT DVO port mapping: the DDI port, and the HDMI and DP
 * DVO codes that name it.
 */
struct i915_dvo_port_map {
	int port;
	uint8_t hdmi;
	uint8_t dp;
};

static int i915_vga_screen_off(void *context);
static unsigned i915_popcount(unsigned value);
static void i915_nogem_write(struct i915_display_nogem *d, struct i915_mmio *m, uint32_t reg, uint32_t value);
static uint32_t i915_nogem_rmw(struct i915_display_nogem *d, struct i915_mmio *m, uint32_t reg, uint32_t clear, uint32_t set);
static uint32_t i915_sagv_block_time(int display_ver, struct mutex *sb_lock, struct i915_mmio *m);
static void i915_sagv_init(struct i915_display_nogem *d, int display_ver, struct mutex *sb_lock, struct i915_mmio *m, struct i915_bw_state *bw);
static int i915_gmbus_setup(struct i915_display_nogem *d, struct i915_mmio *m);
static int i915_ddi_lanes_domain(int display_ver, int port);
static int i915_port_in_use(const struct i915_display_nogem *d, int port);
static void i915_ddi_skip(struct i915_display_nogem *d, int port, int reason);
static void i915_ddi_init(struct i915_display_nogem *d, int display_ver, unsigned port_mask, const struct i915_vbt_child *child);
static int i915_vga_client_register(struct i915_vga_client *c);
static int i915_vga_get_legacy_io(struct i915_display *display);
static unsigned char i915_vga_in8(struct i915_display *display, unsigned short port);
static void i915_vga_out8(struct i915_display *display, unsigned short port, unsigned char value);
static void i915_vga_put_legacy_io(struct i915_display *display);

/*
 * Allocates the takeover world of a display: the context the older
 * modeset text passes along (it names the display).
 */
int
drv_i915_takeover_world_create(
	struct i915_display *display)
{
	struct i915_takeover_world *takeover;

	/* Allocates the world zeroed. */
	takeover = kern_calloc(1, sizeof(*takeover));
	if (takeover == NULL)
		return ENOMEM;

	/* Links the world to its display, whose register-trace switch the walks read. */
	takeover->display = display;

	/* Publishes the world to the display. */
	display->takeover_world = takeover;

	/* Succeeded: the display owns an empty takeover world. */
	return 0;
}

/*
 * Frees the takeover world of a display.
 *
 * A display without a world is left as it is.
 */
void
drv_i915_takeover_world_destroy(
	struct i915_display *display)
{
	/* A display whose world was never created has nothing to free. */
	if (display->takeover_world == NULL)
		return;

	/* Frees the world and forgets it. */
	kern_free(display->takeover_world);
	display->takeover_world = NULL;
}

/*
 * Runs the front of the display probe without GEM, up to the output setup.
 *
 * It covers intel_wm_init() through intel_vga_disable() of
 * intel_display_driver_probe_nogem().  The records in d are cleared first;
 * d is display->nogem and vga is display->vga_client in the driver.  Returns
 * 0, or the errno of the first step the reference propagates one from, with
 * d->fail_where naming that step.
 */
int
drv_i915_display_nogem_front(
	struct i915_display *display,
	struct i915_display_nogem *d,
	int display_ver,
	unsigned pipe_mask,
	struct i915_mmio *m,
	struct mutex *sb_lock,
	struct i915_cdclk_dev *cd,
	struct i915_bw_state *bw,
	struct i915_vga_client *vga)
{
	unsigned pipe;
	unsigned pipe_count;
	int is_adlp;
	int error;
	int vga_result;

	/* Alder Lake-P is the display version 13 this path serves. */
	is_adlp = 0;
	if (display_ver == 13)
		is_adlp = 1;

	/* Starts from empty records. */
	kern_memset(d, 0, sizeof(*d));

	/* intel_wm_init() -> skl_wm_init() for display 9 and later. */
	i915_sagv_init(d, display_ver, sb_lock, m, bw);
	drv_i915_skl_setup_wm_latency(d, display_ver, sb_lock, m, 0 /* dram_info.wm_lv_0_adjust_needed; the start decoded it */);

	/*
	 * intel_panel_sanitize_ssc(): LVDS SSC handling.  ADL-P has no LVDS and
	 * no PCH SSC override, so there is nothing to sanitize here.
	 */

	/* intel_pps_setup(): not PCH_SPLIT/GLK/BXT and not VLV/CHV. */
	d->pps_mmio_base = I915_PPS_BASE;

	/* Sets the GMBUS pins up (intel_gmbus_setup()). */
	error = i915_gmbus_setup(d, m);
	if (error != 0) {
		d->fail_where = "intel_gmbus_setup";
		return error;
	}

	/* Reports how many pipes the platform has. */
	pipe_count = i915_popcount(pipe_mask);
	kern_logf("i915: %u display pipe%s available.\n", pipe_count, pipe_count > 1U ? "s" : "");

	/* Creates the crtc of every pipe the platform has (for_each_pipe: intel_crtc_init()). */
	for (pipe = 0U; pipe < (unsigned)I915_NOGEM_MAX_PIPES; pipe++) {
		/* Skips a pipe the platform does not have. */
		if ((pipe_mask & (1U << pipe)) == 0U)
			continue;

		/* Creates the crtc and its planes. */
		error = drv_i915_crtc_init(d, display_ver, pipe);
		if (error != 0) {
			d->fail_where = "intel_crtc_init";
			return error;
		}

		/* Counts the created crtc. */
		d->num_crtcs++;
	}

	/*
	 * intel_plane_possible_crtcs_init(): each plane's possible_crtcs is the
	 * mask of its own CRTC.  The planes here are already per-CRTC records, so
	 * the relation is structural.
	 */

	/* Creates the shared DPLL manager. */
	d->xd = NULL;
	if (display->xd.chosen)
		d->xd = &display->xd;
	drv_i915_shared_dpll_init(d, d->xd != NULL ? d->xd->choice.platform : NULL);

	/* intel_fdi_pll_freq_update(): IRONLAKE/SNB/IVB only -> returns. */
	/* intel_update_czclk(): VLV/CHV only -> returns. */

	/* intel_display_driver_init_hw(): reads the CDCLK the hardware runs at. */
	drv_i915_update_cdclk(cd);
	kern_logf("i915: Current CDCLK: cdclk=%u vco=%u ref=%u voltage=%u\n", cd->hw.cdclk, cd->hw.vco, cd->hw.ref, (unsigned)cd->hw.voltage_level);

	/* The logical and actual CDCLK states now equal the hardware state. */
	d->cdclk_logical_set = 1;

	/* Applies the display workarounds of the platform. */
	drv_i915_display_wa_apply(d, m, display_ver, is_adlp);

	/* intel_dpll_update_ref_clks() -> icl_update_dpll_ref_clks(): no SSC reference. */
	if (d->dpll_mgr_present != 0)
		d->dpll_ref_nssc = cd->hw.ref;

	/*
	 * intel_hdcp_component_init(): needs the component framework and the GSC
	 * firmware path.  Neither exists here; recorded, not faked.
	 */
	d->hdcp_component_unimplemented = 1;

	/* Computes the maximum CDCLK when nothing set it (intel_update_max_cdclk()). */
	if (d->max_cdclk_freq == 0U)
		drv_i915_update_max_cdclk(d, display_ver, cd->hw.ref);

	/*
	 * intel_hti_init(): only when DISPLAY_INFO()->has_hti.  xe_lpd does NOT
	 * set has_hti (RKL and ADL-S do), so HDPORT_STATE is deliberately not read.
	 */
	d->hti_state_read = 0;

	/* Disables the VGA plane once at startup. */
	vga_result = drv_i915_vga_disable(display, vga, m);

	/* Records whether the plane was already off. */
	d->vga_already_disabled = 0;
	if (vga_result == 1)
		d->vga_already_disabled = 1;

	/* Records whether the plane was turned off now. */
	d->vga_disable_done = 0;
	if (vga_result == 0)
		d->vga_disable_done = 1;

	/* The front is complete. */
	d->inited = 1;

	/* Succeeded: the records are ready for the output setup. */
	return 0;
}

/*
 * Releases the crtc and DPLL records of the output setup.
 */
void
drv_i915_display_nogem_fini(
	struct i915_display_nogem *d)
{
	unsigned pipe;

	/* Marks every crtc record unused. */
	for (pipe = 0U; pipe < (unsigned)I915_NOGEM_MAX_PIPES; pipe++)
		d->crtcs[pipe].in_use = 0;

	/* Forgets the crtcs and the DPLL manager. */
	d->num_crtcs = 0U;
	d->num_dplls = 0U;
	d->dpll_mgr_present = 0;
	d->inited = 0;
}

/*
 * Adjusts the watermark latencies the PCODE reported.
 *
 * A level whose latency is 0 us disables itself and every level above it;
 * WaWmMemoryReadLatency adds the read latency to every valid level when
 * level 0 came back as 0 us; the 16 GB DIMM workaround adds 1 us to level 0.
 */
void
drv_i915_adjust_wm_latency(
	uint16_t wm[],
	int num_levels,
	int read_latency,
	int wm_lv_0_adjust_needed)
{
	int level;
	int above;

	/* Disables every level from the first one (above 0) with no latency. */
	for (level = 1; level < num_levels; level++) {
		/* A level with a latency stays. */
		if (wm[level] != 0U)
			continue;

		/* Clears every level above it and stops counting there. */
		for (above = level + 1; above < num_levels; above++)
			wm[above] = 0U;
		num_levels = level;
		break;
	}

	/*
	 * WaWmMemoryReadLatency: the punit does not account for the read latency,
	 * so add it to every valid level when level 0 came back as 0us.
	 */
	if (wm[0] == 0U) {
		for (level = 0; level < num_levels; level++)
			wm[level] = (uint16_t)(wm[level] + (uint16_t)read_latency);
	}

	/* WA Level-0 adjustment for 16GB DIMMs (SKL+). */
	if (wm_lv_0_adjust_needed != 0)
		wm[0] = (uint16_t)(wm[0] + 1U);
}

/*
 * Reads the watermark latencies from the PCODE (skl_setup_wm_latency()).
 *
 * A mailbox failure is logged and leaves the latencies unset.
 */
void
drv_i915_skl_setup_wm_latency(
	struct i915_display_nogem *d,
	int display_ver,
	struct mutex *sb_lock,
	struct i915_mmio *m,
	int wm_lv_0_adjust_needed)
{
	uint32_t val;
	uint32_t val1;
	uint32_t mult;
	int read_latency;
	int error;

	/* Display 12 and later add a read latency of 3 us, older ones 2 us. */
	read_latency = (display_ver >= 12) ? 3 : 2;

	/* IS_DG2 only doubles this. */
	mult = 1U;

	/* HAS_HW_SAGV_WM(ver >= 13 && !DGFX) -> 6 levels, otherwise 8. */
	d->wm_num_levels = (display_ver >= 13) ? 6U : 8U;

	/*
	 * Reads the first set of memory latencies [0:3] (data0 = 0).  The log
	 * shows the negative code the reference prints.
	 */
	val = 0U;
	val1 = 0U;
	error = drv_i915_pcode_read(sb_lock, m, I915_GEN9_PCODE_READ_MEM_LATENCY, &val, &val1);
	if (error != 0) {
		kern_logf("i915: SKL Mailbox read error = %d\n", -error);
		return;
	}

	/* Decodes levels 0..3, one byte each. */
	d->wm_skl_latency[0] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_0_4_MASK) >> 0) * mult);
	d->wm_skl_latency[1] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_1_5_MASK) >> 8) * mult);
	d->wm_skl_latency[2] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_2_6_MASK) >> 16) * mult);
	d->wm_skl_latency[3] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_3_7_MASK) >> 24) * mult);

	/* Reads the second set [4:7] (data0 = 1). */
	val = 1U;
	val1 = 0U;
	error = drv_i915_pcode_read(sb_lock, m, I915_GEN9_PCODE_READ_MEM_LATENCY, &val, &val1);
	if (error != 0) {
		kern_logf("i915: SKL Mailbox read error = %d\n", -error);
		return;
	}

	/* Decodes levels 4..7, one byte each. */
	d->wm_skl_latency[4] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_0_4_MASK) >> 0) * mult);
	d->wm_skl_latency[5] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_1_5_MASK) >> 8) * mult);
	d->wm_skl_latency[6] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_2_6_MASK) >> 16) * mult);
	d->wm_skl_latency[7] = (uint16_t)(((val & I915_GEN9_MEM_LATENCY_LEVEL_3_7_MASK) >> 24) * mult);

	/* Applies the level cut-off and the workarounds. */
	drv_i915_adjust_wm_latency(d->wm_skl_latency, (int)d->wm_num_levels, read_latency, wm_lv_0_adjust_needed);

	/* The latencies are now usable by the watermark code. */
	d->wm_latency_valid = 1;
}

/*
 * Creates the shared DPLL records from the platform's PLLs (the xd
 * description): the combo DPLLs, the Thunderbolt PLL and the Type-C
 * PLLs, with their names, ids (the platform's select values) and enable
 * words.  A display without a description has no records.
 */
void
drv_i915_shared_dpll_init(
	struct i915_display_nogem *d,
	const struct i915_xd_platform *platform)
{
	static const char *const combo_names[] = { "DPLL 0", "DPLL 1", "DPLL 2", "DPLL 3" };
	static const char *const tc_names[] = { "TC PLL 1", "TC PLL 2", "TC PLL 3", "TC PLL 4", "TC PLL 5", "TC PLL 6" };
	const struct i915_xd_pll_desc *desc;
	struct i915_dpll *pll;
	unsigned index;

	/* Initializes the lock of the DPLL records. */
	(void)mutex_init(&d->dpll_lock, LOCK_RANK_DEVICE, "i915-dpll");

	/* A display without a description has no records. */
	d->dpll_mgr_present = 0;
	d->num_dplls = 0U;
	if (platform == NULL)
		return;

	/* One record per PLL of the platform. */
	for (index = 0U; index < platform->nplls && index < (unsigned)I915_NOGEM_MAX_DPLLS; index++) {
		desc = &platform->plls[index];
		pll = &d->dplls[index];
		pll->id = (int)desc->select;
		pll->enable_reg = desc->enable;
		pll->xd_pll = desc;
		pll->index = index;
		if (desc->kind == I915_XD_PLL_COMBO) {
			pll->name = combo_names[desc->select % 4U];
			pll->funcs = I915_DPLL_FUNCS_COMBO;
		} else if (desc->kind == I915_XD_PLL_TBT) {
			pll->name = "TBT PLL";
			pll->funcs = I915_DPLL_FUNCS_TBT;
		} else {
			pll->name = tc_names[(desc->tc - 1U) % 6U];
			pll->funcs = I915_DPLL_FUNCS_DKL;
		}
	}

	/* Publishes the records and the manager. */
	d->num_dplls = index;
	d->dpll_mgr_present = 1;
}

/*
 * Creates the crtc record of one pipe with its planes (intel_crtc_init()).
 *
 * The pipe gets a primary plane, the sprites of its display version and a
 * cursor.  Returns 0, or EINVAL for a pipe beyond the records.
 */
int
drv_i915_crtc_init(
	struct i915_display_nogem *d,
	int display_ver,
	unsigned pipe)
{
	struct i915_crtc *crtc;
	unsigned count;
	unsigned sprite;
	unsigned num_sprites;

	/* Refuses a pipe beyond the records. */
	if (pipe >= (unsigned)I915_NOGEM_MAX_PIPES)
		return EINVAL;

	/* Starts the crtc from an empty record. */
	crtc = &d->crtcs[pipe];
	kern_memset(crtc, 0, sizeof(*crtc));
	crtc->pipe = pipe;

	/* num_scalers[pipe] = 2 for DISPLAY_VER >= 11. */
	crtc->num_scalers = (display_ver >= 11) ? 2U : 0U;

	/* Creates the primary plane: skl_universal_plane_create(PLANE_PRIMARY) for ver >= 9. */
	count = 0U;
	crtc->planes[count].id = 0;
	crtc->planes[count].type = I915_PLANE_PRIMARY;
	crtc->planes[count].pipe = pipe;
	crtc->planes[count].in_use = 1;
	crtc->plane_ids_mask |= 1U << (unsigned)crtc->planes[count].id;
	count++;

	/* intel_init_fifo_underrun_reporting(crtc, false) */
	crtc->fifo_underrun_reporting = 0;

	/* num_sprites[pipe] = 4 for DISPLAY_VER >= 13, 6 for 11 and 12. */
	if (display_ver >= 13)
		num_sprites = 4U;
	else if (display_ver >= 11)
		num_sprites = 6U;
	else
		num_sprites = 0U;

	/* Creates the sprite planes. */
	for (sprite = 0U; sprite < num_sprites; sprite++) {
		/* Keeps the last slot for the cursor. */
		if (count >= (unsigned)I915_NOGEM_MAX_PLANES - 1U)
			break;

		/* PLANE_SPRITE0 + sprite */
		crtc->planes[count].id = (int)(1U + sprite);
		crtc->planes[count].type = I915_PLANE_SPRITE;
		crtc->planes[count].pipe = pipe;
		crtc->planes[count].in_use = 1;
		crtc->plane_ids_mask |= 1U << (unsigned)crtc->planes[count].id;
		count++;
	}

	/* Creates the cursor plane (intel_cursor_plane_create(), PLANE_CURSOR). */
	crtc->planes[count].id = 7;
	crtc->planes[count].type = I915_PLANE_CURSOR;
	crtc->planes[count].pipe = pipe;
	crtc->planes[count].in_use = 1;
	crtc->plane_ids_mask |= 1U << (unsigned)crtc->planes[count].id;
	count++;
	crtc->num_planes = count;

	/* intel_crtc_state_reset(): INVALID_TRANSCODER etc. */
	crtc->state.cpu_transcoder = -1;

	/* The record now stands for a pipe. */
	crtc->in_use = 1;

	/* Succeeded: the crtc and its planes exist. */
	return 0;
}

/*
 * Applies the display workarounds of the platform (intel_display_wa_apply()).
 *
 * Alder Lake-P, the display 12 (xe_d) platforms and the display 11 arm are
 * written out; the display 11 arm has never run on hardware.
 */
void
drv_i915_display_wa_apply(
	struct i915_display_nogem *d,
	struct i915_mmio *m,
	int display_ver,
	int is_alderlake_p)
{
	/* The Alder Lake-P arm. */
	if (is_alderlake_p != 0) {
		/* Wa_22011091694:adlp */
		(void)i915_nogem_rmw(d, m, I915_GEN9_CLKGATE_DIS_5, 0U, I915_DPCE_GATING_DIS);

		/* Bspec/49189 Initialize Sequence */
		(void)i915_nogem_rmw(d, m, I915_GEN8_CHICKEN_DCPR_1, I915_DDI_CLOCK_REG_ACCESS, 0U);

		d->adlp_wa_applied = 1;
		return;
	}

	/* The xe_d arm: Tiger Lake, Rocket Lake, DG1, ADL-S. */
	if (display_ver == 12) {
		/* Wa_1409120013 */
		i915_nogem_write(d, m, I915_ILK_DPFC_CHICKEN_FBC_A, I915_DPFC_CHICKEN_COMP_DUMMY_PIXEL);

		/* Wa_14013723622 */
		(void)i915_nogem_rmw(d, m, I915_CLKREQ_POLICY, I915_CLKREQ_POLICY_MEM_UP_OVRD, 0U);

		d->xe_d_wa_applied = 1;
		kern_logf("i915: xe_d_display_wa_apply: Wa_1409120013 (DPFC chicken dummy pixel) + Wa_14013723622 (CLKREQ_POLICY memory-up override cleared)\n");
		return;
	}

	/*
	 * XXX: unimplemented path -- this driver attaches to Gen12 only, so the display 11
	 * arm of the reference is written out but has never run on hardware.
	 */
	if (display_ver == 11) {
		kern_logf("i915: gen11_display_wa_apply: XXX unimplemented path entered (display 11 is not a device this driver claims)\n");

		/* Wa_1409120013 */
		i915_nogem_write(d, m, I915_ILK_DPFC_CHICKEN_FBC_A, I915_DPFC_CHICKEN_COMP_DUMMY_PIXEL);

		/* Wa_14010594013 */
		(void)i915_nogem_rmw(d, m, I915_GEN8_CHICKEN_DCPR_1, 0U, I915_ICL_DELAY_PMRSP);
		return;
	}

	/* No other display version has an arm. */
	kern_logf("i915: intel_display_wa_apply: XXX no arm for display version %d\n", display_ver);
}

/*
 * Computes the maximum CDCLK of the platform (intel_update_max_cdclk()).
 *
 * Only the display 11 and later arm is ported; older ones leave it 0.
 */
void
drv_i915_update_max_cdclk(
	struct i915_display_nogem *d,
	int display_ver,
	uint32_t cdclk_ref)
{
	/* Display 11 and later: 648 MHz on a 24 MHz reference, 652.8 MHz otherwise. */
	if (display_ver >= 11) {
		d->max_cdclk_freq = (cdclk_ref == 24000U) ? 648000U : 652800U;
	} else {
		/* Older arms are out of scope. */
		d->max_cdclk_freq = 0U;
	}
}

/*
 * Creates the encoder records of the VBT's child devices (intel_setup_outputs()).
 *
 * HAS_DDI -> intel_ddi_crt_present() (false for display 9 and later) ->
 * intel_bios_for_each_encoder(intel_ddi_init).  This stops at the decision
 * and the encoder record, plus the eDP connector the dp_connector_init hook
 * builds; the DRM encoder registration, HDMI, AUX and HPD are not done, so
 * the connector-driven sanitize arms cannot fire (recorded, not hidden).
 */
void
drv_i915_setup_outputs(
	struct i915_display_nogem *d,
	int display_ver,
	unsigned port_mask,
	const struct i915_vbt_state *vbt,
	struct i915_mmio *m)
{
	unsigned index;

	UNUSED_PARAMETER(m);

	/* intel_pps_unlock_regs_wa(): HAS_DDI -> returns immediately. */

	/* Starts with no live eDP connector. */
	d->edp_port = -1;
	d->edp_init_rc = 0;

	/* HAS_DDI(ADL-P) is true; crt_present would call intel_crt_init(), false on ver >= 9. */
	d->crt_present = drv_i915_ddi_crt_present(display_ver);

	/* intel_bios_for_each_encoder(dev_priv, intel_ddi_init) */
	for (index = 0U; index < vbt->num_display_devices; index++)
		i915_ddi_init(d, display_ver, port_mask, &vbt->display_devices[index]);

	/* GLK/BXT would run vlv_dsi_init() here. */

	/* The output setup is complete. */
	d->outputs_done = 1;
}

/*
 * Reports whether the platform has a DDI CRT output (intel_ddi_crt_present()).
 */
int
drv_i915_ddi_crt_present(
	int display_ver)
{
	/* DISPLAY_VER >= 9 -> no DDI CRT. */
	if (display_ver >= 9)
		return 0;

	/* Succeeded: an older DDI platform may have a CRT. */
	return 1;
}

/*
 * Maps a VBT DVO port code to a DDI port (dvo_port_to_port()).
 *
 * Display 13 and later use the xelpd mapping, in which the Type-C ports take
 * the HDMIF..HDMII / DPF..DPI entries -- not the HDMIC/HDMID ones a
 * pre-xelpd platform would use.  Returns the port, or I915_PORT_NONE.
 */
int
drv_i915_dvo_port_to_port(
	int display_ver,
	uint8_t dvo_port)
{
	static const struct i915_dvo_port_map xelpd[] = {
		{ I915_PORT_A,   I915_DVO_PORT_HDMIA, I915_DVO_PORT_DPA },
		{ I915_PORT_B,   I915_DVO_PORT_HDMIB, I915_DVO_PORT_DPB },
		{ I915_PORT_C,   I915_DVO_PORT_HDMIC, I915_DVO_PORT_DPC },
		{ I915_PORT_TC1, I915_DVO_PORT_HDMIF, I915_DVO_PORT_DPF },
		{ I915_PORT_TC2, I915_DVO_PORT_HDMIG, I915_DVO_PORT_DPG },
		{ I915_PORT_TC3, I915_DVO_PORT_HDMIH, I915_DVO_PORT_DPH },
		{ I915_PORT_TC4, I915_DVO_PORT_HDMII, I915_DVO_PORT_DPI }
	};
	static const struct i915_dvo_port_map legacy[] = {
		{ I915_PORT_A, I915_DVO_PORT_HDMIA, I915_DVO_PORT_DPA },
		{ I915_PORT_B, I915_DVO_PORT_HDMIB, I915_DVO_PORT_DPB },
		{ I915_PORT_C, I915_DVO_PORT_HDMIC, I915_DVO_PORT_DPC },
		{ I915_PORT_D, I915_DVO_PORT_HDMID, I915_DVO_PORT_DPD }
	};
	const struct i915_dvo_port_map *map;
	unsigned count;
	unsigned index;

	/* Selects the xelpd mapping for display 13 and later, the legacy one otherwise. */
	if (display_ver >= 13) {
		map = xelpd;
		count = sizeof(xelpd) / sizeof(xelpd[0]);
	} else {
		map = legacy;
		count = sizeof(legacy) / sizeof(legacy[0]);
	}

	/* Finds the port whose HDMI or DP code the child carries. */
	for (index = 0U; index < count; index++) {
		/* The HDMI code names the port. */
		if (dvo_port == map[index].hdmi)
			return map[index].port;

		/* So does the DP code. */
		if (dvo_port == map[index].dp)
			return map[index].port;
	}

	/* No port uses this code. */
	return I915_PORT_NONE;
}

/*
 * Maps a DDI port to its PHY (intel_port_to_phy()).
 *
 * On display 13 the Type-C ports start at PHY F.
 */
int
drv_i915_port_to_phy(
	int display_ver,
	int port)
{
	/* The Type-C ports of display 13 start at PHY F. */
	if (display_ver >= 13 && port >= I915_PORT_TC1)
		return I915_PHY_F + (port - I915_PORT_TC1);

	/* Succeeded: every other port has the PHY of its letter. */
	return I915_PHY_A + (port - I915_PORT_A);
}

/*
 * Reports whether a PHY is a Type-C PHY (intel_phy_is_tc()).
 */
int
drv_i915_phy_is_tc(
	int display_ver,
	int phy)
{
	/* Only display 13 is described: earlier versions report no Type-C PHY. */
	if (display_ver < 13)
		return 0;

	/* PHY F through PHY I are the Type-C PHYs. */
	if (phy < I915_PHY_F)
		return 0;
	if (phy > I915_PHY_I)
		return 0;

	/* Succeeded: the PHY is a Type-C PHY. */
	return 1;
}

/*
 * Reports whether a DDI port is a Type-C port (intel_ddi_is_tc()).
 */
int
drv_i915_ddi_is_tc(
	int display_ver,
	int port)
{
	/* Display 12 and later: TC1 and above. */
	if (display_ver >= 12) {
		/* TC1 and above are the Type-C ports. */
		if (port >= I915_PORT_TC1)
			return 1;

		/* The ports below TC1 are combo ports. */
		return 0;
	}

	/* Display 11: port C and above. */
	if (display_ver >= 11) {
		/* C and above are the Type-C ports. */
		if (port >= I915_PORT_C)
			return 1;

		/* A and B are combo ports. */
		return 0;
	}

	/* Older platforms have no Type-C ports. */
	return 0;
}

/*
 * Sets whether the host bridge decodes the legacy VGA ranges
 * (intel_gmch_vga_set_state()).
 *
 * Read-modify-writes the VGA-disable bit in GMCH_CTRL of the host bridge
 * (00:00.0) as a 16-bit config word -- not the GPU function.  Returns 0,
 * ENODEV when there is no host bridge, or EIO when the word could not be
 * read or written.
 */
int
drv_i915_gmch_vga_set_state(
	struct i915_vga_client *c,
	int enable_decode)
{
	uint16_t gmch_ctrl;
	unsigned reg;
	int decode_disabled;
	int disable_wanted;
	int error;

	/* Display 6 and later keep the control word at SNB_GMCH_CTRL. */
	reg = (c->display_ver >= 6U) ? I915_SNB_GMCH_CTRL : I915_INTEL_GMCH_CTRL;

	/* No host bridge: GMCH_CTRL is not reachable. */
	if (c->gmch == NULL)
		return ENODEV;

	/* Reads the control word from the host bridge. */
	gmch_ctrl = 0;
	error = drv_pci_device_config_read16(c->gmch, reg, &gmch_ctrl);
	if (error != 0) {
		kern_logf("i915: vga: failed to read GMCH control word\n");
		return EIO;
	}

	/* VGA_DISABLE set means decode is disabled. */
	decode_disabled = 0;
	if ((gmch_ctrl & I915_INTEL_GMCH_VGA_DISABLE) != 0U)
		decode_disabled = 1;

	/* The caller asks for decode off when enable_decode is 0. */
	disable_wanted = 0;
	if (enable_decode == 0)
		disable_wanted = 1;

	/* Already in the requested state. */
	if (decode_disabled == disable_wanted)
		return 0;

	/* Clears the bit to decode, sets it to stop decoding. */
	if (enable_decode != 0)
		gmch_ctrl &= (uint16_t)~I915_INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= I915_INTEL_GMCH_VGA_DISABLE;

	/* Writes the control word back. */
	error = drv_pci_device_config_write16(c->gmch, reg, gmch_ctrl);
	if (error != 0) {
		kern_logf("i915: vga: failed to write GMCH control word\n");
		return EIO;
	}

	/* Succeeded: the host bridge decodes as asked. */
	return 0;
}

/*
 * Sets the GMCH VGA decode and reports the resources the device decodes
 * (intel_gmch_vga_set_decode(), the arbiter's decode callback).
 */
unsigned
drv_i915_gmch_vga_set_decode(
	struct i915_vga_client *c,
	int enable_decode)
{
	/* Sets the decode; a failure is not the arbiter's concern, as in the reference. */
	(void)drv_i915_gmch_vga_set_state(c, enable_decode);

	/* A decoding device claims the legacy resources as well. */
	if (enable_decode != 0) {
		return I915_VGA_RSRC_LEGACY_IO |
		       I915_VGA_RSRC_LEGACY_MEM |
		       I915_VGA_RSRC_NORMAL_IO |
		       I915_VGA_RSRC_NORMAL_MEM;
	}

	/* Succeeded: a device that does not decode claims only the normal resources. */
	return I915_VGA_RSRC_NORMAL_IO | I915_VGA_RSRC_NORMAL_MEM;
}

/*
 * Registers the display device with the VGA arbiter (intel_vga_register()).
 *
 * The decode callback is intel_gmch_vga_set_decode; the arbiter's ENODEV
 * for a device that is not the PCI VGA display class (a secondary
 * controller) is tolerated.  Returns 0, or the arbiter's error.
 */
int
drv_i915_vga_register(
	struct i915_vga_client *c,
	struct drv_pci_device *gpu,
	unsigned display_ver,
	struct i915_trace *trace)
{
	struct drv_pci_address bridge;
	const char *what;
	const char *client_state;
	const char *bridge_state;
	uint16_t op;
	int error;
	int reported_error;

	/* Binds the client to the display device. */
	c->gpu = gpu;
	c->display_ver = display_ver;
	c->registered = 0;

	/*
	 * i915->gmch.pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0)).
	 * The decode callback needs it to reach GMCH_CTRL on the host bridge.
	 */
	kern_memset(&bridge, 0, sizeof(bridge));
	c->gmch = drv_pci_find_device(&bridge);
	if (c->gmch == NULL)
		drv_i915_trace_record(trace, I915_TAKEOVER_TRACE_STAGE, I915_TRACE_NOTE, "intel_vga_register:no_gmch_bridge", 0U, 0U);

	/* Registers the decode callback; a real arbiter error ends the registration. */
	error = i915_vga_client_register(c);
	if (error != 0 && error != ENODEV)
		return error;

	/* The trace and the log keep the negative code the reference reports. */
	reported_error = -error;

	/* Records the registration, or the tolerated secondary controller. */
	op = I915_TRACE_NOTE;
	what = "intel_vga_register:enodev_secondary";
	if (error == 0) {
		op = I915_TRACE_ACQUIRE;
		what = "intel_vga_register";
	}
	drv_i915_trace_record(trace, I915_TAKEOVER_TRACE_STAGE, op, what, (uint64_t)(unsigned)reported_error, 0U);

	/* Reports the client and the host bridge. */
	client_state = (c->registered != 0) ? "registered" : "not-vga(-ENODEV)";
	bridge_state = (c->gmch != NULL) ? "found" : "absent";
	kern_logf("i915: intel_vga_register: client=%s gmch_bridge=%s ret=%d\n", client_state, bridge_state, reported_error);

	/* Succeeded: the device takes part in arbitration, or is a tolerated secondary. */
	return 0;
}

/*
 * Unregisters the display device from the VGA arbiter
 * (vga_client_unregister(): drops the decode callback).
 */
void
drv_i915_vga_unregister(
	struct i915_vga_client *c)
{
	/* Forgets the callback, the device and the host bridge. */
	c->registered = 0;
	c->gpu = NULL;
	c->gmch = NULL;
}

/*
 * Replaces the legacy VGA I/O accessor.
 *
 * NULL restores the production path: the driver owns the legacy VGA I/O of
 * its device and uses port I/O.  A GPU-free test installs a recorder here.
 */
void
drv_i915_vga_io_test_set(
	struct i915_display *display,
	const struct i915_vga_io_ops *ops)
{
	/* Installs the accessor every legacy VGA access goes through. */
	display->g_vga_io = ops;
}

/*
 * Resets the legacy VGA I/O state (intel_vga_reset_io_mem()).
 *
 * Owns the LEGACY_IO arbiter resource, touches the VGA MSR (read MIS_R,
 * write it back to MIS_W) so vgacon stays sane, then releases the resource.
 * I/O ownership is independent of the decode-client registration; if the
 * resource cannot be acquired there is no I/O and no put.  c must be the
 * vga_client of a struct i915_display (the power-well code holds only the
 * client); the legacy I/O accessor is that display's.
 */
void
drv_i915_vga_reset_io_mem(
	struct i915_vga_client *c)
{
	struct i915_display *display;
	unsigned char misc;
	int owned;

	/* The client is embedded in the display whose accessor is used. */
	display = container_of(c, struct i915_display, vga_client);

	/* Does nothing when the legacy I/O resource cannot be owned. */
	owned = i915_vga_get_legacy_io(display);
	if (owned == 0)
		return;

	/* Writes the Misc Output register back to itself. */
	misc = i915_vga_in8(display, I915_VGA_MIS_R);
	i915_vga_out8(display, I915_VGA_MIS_W, misc);

	/* Gives the resource back. */
	i915_vga_put_legacy_io(display);
}

/*
 * Disables the VGA plane the driver never uses, through the xd parts
 * (drv_i915_xd_vga_off): the legacy sequencer's screen off is this
 * file's, since the legacy VGA I/O belongs to the VGA client.  Returns 0
 * after disabling the plane, 1 when it was already disabled (not an
 * errno), or EIO when the legacy I/O could not be owned.
 */
int
drv_i915_vga_disable(
	struct i915_display *display,
	struct i915_vga_client *c,
	struct i915_mmio *m)
{
	int result;

	UNUSED_PARAMETER(c);
	UNUSED_PARAMETER(m);

	/* The screen off through the sequencer, then the plane. */
	result = drv_i915_xd_vga_off(&display->xd.io, i915_vga_screen_off, display);
	if (result == EIO)
		return EIO;

	/* Succeeded: the plane is off (1 when it already was). */
	return result;
}

/* Counts the set bits of a pipe mask. */
static unsigned
i915_popcount(
	unsigned value)
{
	unsigned count;

	/* Adds one for every set bit. */
	count = 0U;
	while (value != 0U) {
		count += value & 1U;
		value >>= 1;
	}

	/* Succeeded: reports the number of set bits. */
	return count;
}

/* Writes a display register and counts the write in the records. */
static void
i915_nogem_write(
	struct i915_display_nogem *d,
	struct i915_mmio *m,
	uint32_t reg,
	uint32_t value)
{
	/* Writes the register and counts the write. */
	drv_i915_write32(m, reg, value);
	d->mmio_writes++;
}

/* Read-modify-writes a display register, writing only a changed value. */
static uint32_t
i915_nogem_rmw(
	struct i915_display_nogem *d,
	struct i915_mmio *m,
	uint32_t reg,
	uint32_t clear,
	uint32_t set)
{
	uint32_t old;
	uint32_t value;

	/* Reads the register and computes the new value. */
	old = drv_i915_read32(m, reg);
	value = (old & ~clear) | set;

	/* Writes only a value that changed. */
	if (value != old)
		i915_nogem_write(d, m, reg, value);

	/* Succeeded: reports the value before the change. */
	return old;
}

/* Reads the SAGV block time (intel_sagv_block_time()). */
static uint32_t
i915_sagv_block_time(
	int display_ver,
	struct mutex *sb_lock,
	struct i915_mmio *m)
{
	uint32_t val;
	uint32_t val1;
	int error;

	/* DISPLAY_VER >= 12 reads it from the PCODE. */
	if (display_ver >= 12) {
		val = 0U;
		val1 = 0U;
		error = drv_i915_pcode_read(sb_lock, m, I915_GEN12_PCODE_READ_SAGV_BLOCK_TIME_US, &val, &val1);
		if (error != 0) {
			kern_logf("i915: Couldn't read SAGV block time!\n");
			return 0U;
		}

		/* The PCODE reported the block time. */
		return val;
	}

	/* Display 11 has a fixed block time. */
	if (display_ver == 11)
		return 10U;

	/* Succeeded: HAS_SAGV pre-icl. */
	return 30U;
}

/* Records the SAGV status and block time (intel_sagv_init()); on icl+ the bandwidth init settled the status. */
static void
i915_sagv_init(
	struct i915_display_nogem *d,
	int display_ver,
	struct mutex *sb_lock,
	struct i915_mmio *m,
	struct i915_bw_state *bw)
{
	const char *supported;

	/* HAS_SAGV: ver >= 9 && !IS_LP; a platform without SAGV does not control it. */
	if (display_ver < 9)
		bw->sagv_status = (int)I915_SAGV_NOT_CONTROLLED;

	/* DISPLAY_VER < 11 would probe with skl_sagv_disable() here. */

	/* The bandwidth init should have settled the status. */
	if (bw->sagv_status == (int)I915_SAGV_UNKNOWN)
		kern_logf("i915: WARN SAGV status still unknown at sagv_init\n");

	/* Records the status and the block time. */
	d->sagv_status = bw->sagv_status;
	d->sagv_block_time_us = i915_sagv_block_time(display_ver, sb_lock, m);

	/* Avoid overflow when adding with wm0 latency etc. */
	if (d->sagv_block_time_us > 0xffffU) {
		kern_logf("i915: Excessive SAGV block time %u, ignoring\n", d->sagv_block_time_us);
		d->sagv_block_time_us = 0U;
	}

	/* An SAGV the driver does not control blocks nothing. */
	if (bw->sagv_status == (int)I915_SAGV_NOT_CONTROLLED)
		d->sagv_block_time_us = 0U;

	/* Reports the SAGV. */
	supported = (bw->sagv_status != (int)I915_SAGV_NOT_CONTROLLED) ? "yes" : "no";
	kern_logf("i915: SAGV supported: %s, original SAGV block time: %u us\n", supported, d->sagv_block_time_us);
}

/* Sets the ICP GMBUS pins up and resets the controller (intel_gmbus_setup()). */
static int
i915_gmbus_setup(
	struct i915_display_nogem *d,
	struct i915_mmio *m)
{
	static const struct i915_gmbus_pin_desc icp[] = {
		{ I915_GMBUS_PIN_1_BXT,      "dpa", I915_GPIOB },
		{ I915_GMBUS_PIN_2_BXT,      "dpb", I915_GPIOC },
		{ I915_GMBUS_PIN_3_BXT,      "dpc", I915_GPIOD },
		{ I915_GMBUS_PIN_9_TC1_ICP,  "tc1", I915_GPIOJ },
		{ I915_GMBUS_PIN_10_TC2_ICP, "tc2", I915_GPIOK },
		{ I915_GMBUS_PIN_11_TC3_ICP, "tc3", I915_GPIOL },
		{ I915_GMBUS_PIN_12_TC4_ICP, "tc4", I915_GPIOM },
		{ I915_GMBUS_PIN_13_TC5_TGP, "tc5", I915_GPION },
		{ I915_GMBUS_PIN_14_TC6_TGP, "tc6", I915_GPIOO }
	};
	unsigned count;
	unsigned index;
	unsigned pin;

	/* !HAS_GMCH -> the South Display Engine offsets. */
	d->gmbus_mmio_base = I915_PCH_DISPLAY_BASE;

	/* Initializes the GMBUS lock and wait queue. */
	(void)mutex_init(&d->gmbus_lock, LOCK_RANK_DEVICE, "i915-gmbus");
	waitq_init(&d->gmbus_waitq, "i915-gmbus");

	/* gmbus_pins_icp[]: indices are the pin numbers, gaps stay absent. */
	count = sizeof(icp) / sizeof(icp[0]);
	for (index = 0U; index < count; index++) {
		/* Skips a pin beyond the pin records. */
		pin = icp[index].pin;
		if (pin >= (unsigned)I915_NOGEM_MAX_GMBUS)
			continue;

		/* Fills the pin record. */
		d->gmbus_pins[pin].name = icp[index].name;
		d->gmbus_pins[pin].gpio = icp[index].gpio;
		d->gmbus_pins[pin].reg0 = pin | I915_GMBUS_RATE_100KHZ;
		d->gmbus_pins[pin].present = 1;
		d->gmbus_pins_present++;
	}

	/*
	 * The reference now creates one i2c_adapter per pin.  There is no i2c core
	 * in this port, so the adapters are NOT created and that is recorded rather
	 * than reported as success.  Nothing in the readout/sanitize needs them.
	 */
	d->gmbus_adapters_unimplemented = 1;

	/* intel_gmbus_reset() */
	i915_nogem_write(d, m, d->gmbus_mmio_base + I915_GMBUS0_OFF, 0U);
	i915_nogem_write(d, m, d->gmbus_mmio_base + I915_GMBUS4_OFF, 0U);

	/* Succeeded: the pins are known and the controller is reset. */
	return 0;
}

/* Reports a port's DDI lanes power domain (intel_display_power_ddi_lanes_domain(), d13_port_domains). */
static int
i915_ddi_lanes_domain(
	int display_ver,
	int port)
{
	/* Display 13 has the combo ports A..C and the Type-C ports TC1..TC4. */
	if (display_ver >= 13) {
		/* The combo ports take DDI_LANES_A..C. */
		if (port >= I915_PORT_A && port <= I915_PORT_C)
			return (int)I915_PW_DOMAIN_PORT_DDI_LANES_A + (port - I915_PORT_A);

		/* The Type-C ports take DDI_LANES_TC1..TC4. */
		if (port >= I915_PORT_TC1 && port <= I915_PORT_TC4)
			return (int)I915_PW_DOMAIN_PORT_DDI_LANES_TC1 + (port - I915_PORT_TC1);
	}

	/* drm_WARN_ON path: fall back to LANES_A rather than an invalid domain. */
	return (int)I915_PW_DOMAIN_PORT_DDI_LANES_A;
}

/* Reports whether an earlier child already claimed a port. */
static int
i915_port_in_use(
	const struct i915_display_nogem *d,
	int port)
{
	unsigned index;

	/* Looks for an encoder on the port. */
	for (index = 0U; index < d->num_encoders; index++) {
		if (d->encoders[index].port == port)
			return 1;
	}

	/* No encoder uses the port. */
	return 0;
}

/* Records why intel_ddi_init() declined a child. */
static void
i915_ddi_skip(
	struct i915_display_nogem *d,
	int port,
	int reason)
{
	/* Keeps the reason while there is room for it. */
	if (d->num_ddi_skips < (unsigned)I915_NOGEM_MAX_ENCODERS) {
		d->ddi_skip_reason[d->num_ddi_skips] = reason;
		d->ddi_skip_port[d->num_ddi_skips] = port;
		d->num_ddi_skips++;
	}

	/* Counts every declined child. */
	d->ddi_skipped++;
}

/* Creates the encoder record of one VBT child (intel_ddi_init(), decision half), recording every early return. */
static void
i915_ddi_init(
	struct i915_display_nogem *d,
	int display_ver,
	unsigned port_mask,
	const struct i915_vbt_child *child)
{
	struct i915_encoder *e;
	int port;
	int phy;
	int in_use;
	int supports_dvi;
	int supports_hdmi;
	int init_hdmi;
	int init_dp;
	int connector_result;

	/* Counts the child. */
	d->ddi_init_calls++;

	/* Maps the child's DVO port to a DDI port. */
	port = drv_i915_dvo_port_to_port(display_ver, child->dvo_port);
	if (port == I915_PORT_NONE) {
		i915_ddi_skip(d, port, I915_DDI_SKIP_PORT_NONE);
		return;
	}

	/* port_strap_detected(): straps are not used on skl+. */
	if (display_ver < 9) {
		i915_ddi_skip(d, port, I915_DDI_SKIP_STRAP);
		return;
	}

	/* assert_port_valid(): the platform must advertise the port. */
	if ((port_mask & (1U << (unsigned)port)) == 0U) {
		kern_logf("i915: WARN Platform does not support port %c\n", (char)('A' + port));
		i915_ddi_skip(d, port, I915_DDI_SKIP_PORT_INVALID);
		return;
	}

	/* A port an earlier child claimed is not claimed again. */
	in_use = i915_port_in_use(d, port);
	if (in_use != 0) {
		kern_logf("i915: Port %c already claimed\n", (char)('A' + port));
		i915_ddi_skip(d, port, I915_DDI_SKIP_PORT_IN_USE);
		return;
	}

	/* intel_bios_encoder_supports_dsi() -> icl_dsi_init(), a separate path. */
	if ((child->device_type & I915_DEVICE_TYPE_MIPI_OUTPUT) != 0U) {
		i915_ddi_skip(d, port, I915_DDI_SKIP_DSI);
		return;
	}

	/* Maps the port to its PHY. */
	phy = drv_i915_port_to_phy(display_ver, port);

	/*
	 * intel_hti_uses_phy(): HTI can reserve PHYs.  xe_lpd has no has_hti, so
	 * hti.state stays 0 and no PHY is ever reserved here.
	 */
	if (d->hti_state != 0U) {
		i915_ddi_skip(d, port, I915_DDI_SKIP_HTI);
		return;
	}

	/* intel_bios_encoder_supports_dvi(): TMDS DVI signalling. */
	supports_dvi = 0;
	if ((child->device_type & I915_DEVICE_TYPE_TMDS_DVI_SIGNALING) != 0U)
		supports_dvi = 1;

	/* intel_bios_encoder_supports_hdmi(): DVI signalling that is not marked not-HDMI. */
	supports_hdmi = 0;
	if (supports_dvi != 0 && (child->device_type & I915_DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0U)
		supports_hdmi = 1;

	/* HDMI is initialized for a DVI or an HDMI child. */
	init_hdmi = 0;
	if (supports_dvi != 0 || supports_hdmi != 0)
		init_hdmi = 1;

	/* intel_bios_encoder_supports_dp() */
	init_dp = 0;
	if ((child->device_type & I915_DEVICE_TYPE_DISPLAYPORT_OUTPUT) != 0U)
		init_dp = 1;

	/* intel_bios_encoder_is_lspcon(): HAS_LSPCON is DISPLAY_VER 9..10 only. */

	/* A child that is neither DVI, HDMI nor DP is respected as such. */
	if (init_dp == 0 && init_hdmi == 0) {
		kern_logf("i915: VBT says port %c is not DVI/HDMI/DP compatible, "
			"respect it\n", (char)('A' + port));
		i915_ddi_skip(d, port, I915_DDI_SKIP_NOT_DVI_HDMI_DP);
		return;
	}

	/* No record left for the encoder. */
	if (d->num_encoders >= (unsigned)I915_NOGEM_MAX_ENCODERS)
		return;

	/* Fills the encoder record. */
	e = &d->encoders[d->num_encoders];
	d->num_encoders++;
	e->port = port;
	e->phy = phy;
	e->is_tc = drv_i915_ddi_is_tc(display_ver, port);
	e->power_domain = i915_ddi_lanes_domain(display_ver, port);
	e->init_hdmi = init_hdmi;
	e->init_dp = init_dp;
	e->dvo_port = child->dvo_port;
	e->device_type = child->device_type;

	/*
	 * DISPLAY_VER >= 11 and not ADL-S / RKL / DG1 / JSL / EHL: the icl combo or
	 * TC clock ops, selected by intel_ddi_is_tc().
	 */
	e->clk_funcs = (e->is_tc != 0) ? I915_DDI_CLK_ICL_TC : I915_DDI_CLK_ICL_COMBO;
	e->in_use = 1;

	/*
	 * if (init_dp) intel_ddi_init_dp_connector(): for the eDP port this is where the
	 * reference powers the panel logic, reads DPCD / EDID and settles the PPS delays.
	 * A failure takes the reference's `goto err`: the encoder does not survive.
	 */
	if (init_dp != 0 && d->dp_connector_init != NULL) {
		connector_result = d->dp_connector_init(d->dp_connector_ctx, port);
		if (connector_result == 0) {
			d->edp_port = port;
		} else if (connector_result < 0) {
			d->edp_init_rc = connector_result;
			e->in_use = 0;
			d->num_encoders--;
			i915_ddi_skip(d, port, I915_DDI_SKIP_EDP_INIT_FAILED);
		}
	}
}

/* Registers the decode callback with the arbiter (vga_client_register()); only a PCI VGA-class device is a client. */
static int
i915_vga_client_register(
	struct i915_vga_client *c)
{
	uint32_t class_code;

	/* !PCI_DISPLAY_CLASS_VGA: not an arbiter client. */
	class_code = drv_pci_device_class(c->gpu);
	if ((class_code & I915_PCI_CLASS_VGA_MASK) != I915_PCI_CLASS_VGA)
		return ENODEV;

	/* The callback is installed. */
	c->registered = 1;

	/* Succeeded: the device is an arbiter client. */
	return 0;
}

/* Takes the legacy VGA I/O resource: 1 when owned, 0 when refused. */
static int
i915_vga_get_legacy_io(
	struct i915_display *display)
{
	const struct i915_vga_io_ops *ops;
	int owned;

	/* Production: the driver owns the legacy VGA I/O on this device, so the get succeeds. */
	ops = display->g_vga_io;
	if (ops == NULL)
		return 1;

	/* Asks the injected accessor. */
	owned = ops->get(ops->ctx, (int)I915_VGA_RSRC_LEGACY_IO);

	/* Succeeded: reports whether the resource was granted. */
	return owned;
}

/* Reads a legacy VGA port. */
static unsigned char
i915_vga_in8(
	struct i915_display *display,
	unsigned short port)
{
	const struct i915_vga_io_ops *ops;
	unsigned char value;

	/* Reads through the injected accessor, or the port itself. */
	ops = display->g_vga_io;
	if (ops != NULL)
		value = ops->in8(ops->ctx, port);
	else
		value = kern_io_in8(port);

	/* Succeeded: reports the port value. */
	return value;
}
/* Turns the screen off through sequencer register SR01 of the legacy VGA I/O; 0, or EIO when the I/O could not be owned. */
static int
i915_vga_screen_off(
	void *context)
{
	struct i915_display *display;
	unsigned char sr1;
	int owned;

	/* Owns the legacy I/O first. */
	display = context;
	owned = i915_vga_get_legacy_io(display);
	if (owned == 0)
		return EIO;

	/* SR01's screen-off bit. */
	i915_vga_out8(display, I915_VGA_SEQ_I, 0x01U);
	sr1 = i915_vga_in8(display, I915_VGA_SEQ_D);
	i915_vga_out8(display, I915_VGA_SEQ_D, (unsigned char)(sr1 | I915_VGA_SR01_SCREEN_OFF));
	i915_vga_put_legacy_io(display);

	/* Succeeded: the screen is off. */
	return 0;
}

/* Writes a legacy VGA port. */
static void
i915_vga_out8(
	struct i915_display *display,
	unsigned short port,
	unsigned char value)
{
	const struct i915_vga_io_ops *ops;

	/* Writes through the injected accessor, or the port itself. */
	ops = display->g_vga_io;
	if (ops != NULL)
		ops->out8(ops->ctx, port, value);
	else
		kern_io_out8(port, value);
}

/* Gives the legacy VGA I/O resource back; production holds nothing to give back. */
static void
i915_vga_put_legacy_io(
	struct i915_display *display)
{
	const struct i915_vga_io_ops *ops;

	/* Only an injected accessor has a resource to release. */
	ops = display->g_vga_io;
	if (ops != NULL)
		ops->put(ops->ctx, (int)I915_VGA_RSRC_LEGACY_IO);
}
