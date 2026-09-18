/*
 * WS031 Linux-parity — display-core HW bring-up for ADL-P (see display_core.h).
 *
 * Faithful port of icl_display_core_init(false) + intel_power_domains_init_hw()
 * for display version 13.  gen12_dbuf_slices_config() and icl_mbus_init() are
 * no-ops on ADL-P (verified against the reference), so they are omitted.  The
 * combo PHY and CDCLK bodies are the SAME ones used by their unit tests; no
 * simplified duplicates.  Only the MMIO / PCODE / time backends are faked.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <errno.h>
#include "parity.h"
#include "osdep/mmio.h"
#include "osdep/trace.h"
#include "power_domains.h"
#include "cdclk.h"
#include "combo_phy.h"
#include "wait.h"
#include "dram_bw.h"
#include "display_core.h"

/* --- registers (i915_reg.h) --- */
#define DC_STATE_EN                 0x45504u
#define DC_STATE_EN_UPTO_DC5_DC6    0x3u
#define DC_STATE_EN_DC3CO           0x40000000u
#define HSW_NDE_RSTWRN_OPT          0x46408u
#define RESET_PCH_HANDSHAKE_ENABLE  (1u << 4)
#define GEN11_CHICKEN_DCPR_2        0x46434u
#define DCPR_CLEAR_MEMSTAT_DIS      (1u << 24)
#define DCPR_SEND_RESP_IMM          (1u << 25)
#define DCPR_MASK_LPMODE            (1u << 26)
#define DCPR_MASK_MAXLATENCY_MEMUP_CLR (1u << 27)
#define XELPD_DISPLAY_ERR_FATAL_MASK 0x4421cu

/* DBUF: ADL-P (xe_lpd) has 4 slices S1..S4. */
#define DBUF_POWER_REQUEST          (1u << 31)
#define DBUF_POWER_STATE            (1u << 30)
static const uint32_t dbuf_ctl_s[4] = { 0x44FE8u, 0x44300u, 0x44304u, 0x44308u };
#define DBUF_SLICE_MASK             0xFu   /* BIT(S1)|BIT(S2)|BIT(S3)|BIT(S4) */

/* BW_BUDDY: abox_mask = GENMASK(1,0) on ADL-P. */
#define BW_BUDDY_CTL0               0x45130u
#define BW_BUDDY_CTL1               0x45140u
#define BW_BUDDY_PAGE_MASK0         0x45134u
#define BW_BUDDY_PAGE_MASK1         0x45144u
#define BW_BUDDY_DISABLE            (1u << 31)

/* tgl_buddy_page_masks[] (parity dram-type enum values). */
struct buddy_mask { unsigned nch; int type; uint32_t page_mask; };
static const struct buddy_mask tgl_buddy[] = {
	{ 1, PARITY_DRAM_DDR4,   0x0Fu },
	{ 1, PARITY_DRAM_DDR5,   0x0Fu },
	{ 2, PARITY_DRAM_LPDDR4, 0x1Cu },
	{ 2, PARITY_DRAM_LPDDR5, 0x1Cu },
	{ 2, PARITY_DRAM_DDR4,   0x1Fu },
	{ 2, PARITY_DRAM_DDR5,   0x1Eu },
	{ 4, PARITY_DRAM_LPDDR4, 0x38u },
	{ 4, PARITY_DRAM_LPDDR5, 0x38u },
	{ 0, 0, 0u }
};

static void
rmw(struct osdep_mmio *m, uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t v = osdep_mmio_raw_read32(m, reg);

	osdep_mmio_raw_write32(m, reg, (v & ~clear) | set);
}

/* gen9_set_dc_state(DC_STATE_DISABLE): clear the DC5/6/DC3CO bits. */
static void
gen9_set_dc_state_disable(struct parity_display_core *dc)
{
	uint32_t mask = DC_STATE_EN_UPTO_DC5_DC6;
	uint32_t v = osdep_mmio_raw_read32(dc->m, DC_STATE_EN);

	if (dc->pd->allowed_dc_mask & DC_STATE_EN_DC3CO)
		mask |= DC_STATE_EN_DC3CO;
	v &= ~mask;   /* state = DC_STATE_DISABLE (0) */
	osdep_mmio_raw_write32(dc->m, DC_STATE_EN, v);
}

/* intel_pch_reset_handshake(enable=true): DISPLAY_VER < 14 -> no PICA bit. */
static void
pch_reset_handshake(struct parity_display_core *dc)
{
	rmw(dc->m, HSW_NDE_RSTWRN_OPT, RESET_PCH_HANDSHAKE_ENABLE, RESET_PCH_HANDSHAKE_ENABLE);
}

/* --- DBUF (gen9_dbuf_*) --- */

uint8_t
parity_enabled_dbuf_slices_mask(struct parity_display_core *dc)
{
	uint8_t mask = 0u;
	unsigned s;

	for (s = 0u; s < 4u; s++)
		if (osdep_mmio_raw_read32(dc->m, dbuf_ctl_s[s]) & DBUF_POWER_STATE)
			mask |= (uint8_t)(1u << s);
	return mask;
}

/* gen9_dbuf_slice_set(): RMW REQUEST -> posting read -> udelay(10) -> read STATE. */
static void
gen9_dbuf_slice_set(struct parity_display_core *dc, unsigned slice, int enable)
{
	uint32_t reg = dbuf_ctl_s[slice];
	int state;

	rmw(dc->m, reg, DBUF_POWER_REQUEST, enable ? DBUF_POWER_REQUEST : 0u);
	(void)osdep_mmio_raw_read32(dc->m, reg);   /* posting read */
	(void)parity_udelay(10u);
	state = (osdep_mmio_raw_read32(dc->m, reg) & DBUF_POWER_STATE) ? 1 : 0;
	if ((enable ? 1 : 0) != state)
		kern_logf("i915: parity DBUF slice %u power %s timeout\n",
			slice, enable ? "enable" : "disable");
}

static void
gen9_dbuf_slices_update(struct parity_display_core *dc, uint8_t req_slices)
{
	unsigned s;

	mutex_lock(&dc->pd->lock);
	for (s = 0u; s < 4u; s++)
		gen9_dbuf_slice_set(dc, s, (req_slices & (1u << s)) ? 1 : 0);
	dc->dbuf_enabled_slices = req_slices;
	mutex_unlock(&dc->pd->lock);
}

static void
gen9_dbuf_enable(struct parity_display_core *dc)
{
	/* Read what BIOS left, keep it, and add at least slice S1. */
	uint8_t enabled = parity_enabled_dbuf_slices_mask(dc);
	uint8_t slices_mask = (uint8_t)(1u | enabled);   /* BIT(DBUF_S1) | enabled */

	gen9_dbuf_slices_update(dc, slices_mask & DBUF_SLICE_MASK);
}

/* tgl_bw_buddy_init(): program the arbiter BW_BUDDY from the DRAM config. */
static void
tgl_bw_buddy_init(struct parity_display_core *dc)
{
	int i;
	const struct buddy_mask *row = 0;

	for (i = 0; tgl_buddy[i].nch != 0u; i++)
		if (tgl_buddy[i].nch == dc->dram_channels &&
		    tgl_buddy[i].type == dc->dram_type) {
			row = &tgl_buddy[i];
			break;
		}

	if (row == 0) {
		/* Unknown memory configuration: disable address buddy logic. */
		osdep_mmio_raw_write32(dc->m, BW_BUDDY_CTL0, BW_BUDDY_DISABLE);
		osdep_mmio_raw_write32(dc->m, BW_BUDDY_CTL1, BW_BUDDY_DISABLE);
		kern_logf("i915: parity BW_BUDDY: unknown DRAM config (ch=%u type=%d); disabled\n",
			dc->dram_channels, dc->dram_type);
		return;
	}
	/* DISPLAY_VER 13: no Wa_22010178259 TLB timer (that is ver 12 only). */
	osdep_mmio_raw_write32(dc->m, BW_BUDDY_PAGE_MASK0, row->page_mask);
	osdep_mmio_raw_write32(dc->m, BW_BUDDY_PAGE_MASK1, row->page_mask);
}

/*
 * Trace ring for the combo-PHY init calls made from here.  struct osdep_trace
 * is 128 KiB (4096 records) -- eight times a 16 KiB kernel thread stack -- so
 * it must NEVER be an automatic variable: as a local it silently overflowed
 * the runner's stack by ~115 KiB and osdep_trace_init() zero-filled the heap
 * below it (other threads' stacks and thread structs).  Display-core init and
 * the DC_off well enable are serialised (one attach at a time, pd->lock), so
 * one static ring is enough.
 */
static struct osdep_trace dc_trace;

/* --- the display-core sequence --- */

static void
fault(struct parity_display_core *dc, const char *where)
{
	if (!dc->fault_stop) {   /* keep the FIRST fault + position */
		dc->fault_stop = 1;
		dc->fault_where = where;
	}
	kern_logf("i915: parity display_core: stop at %s (adaptation-layer fault)\n", where);
}

static int
icl_display_core_init(struct parity_display_core *dc, int resume)
{
	unsigned combo_n = 0u;
	int idx, rc;

	(void)resume;   /* ADL-P: DMC reload (resume path) is done later, not here */
	osdep_trace_init(&dc_trace);

	/* gen9_set_dc_state(DC_STATE_DISABLE). */
	dc->last_child = 1;
	gen9_set_dc_state_disable(dc);

	/* 1. Enable PCH reset handshake. */
	dc->last_child = 2;
	pch_reset_handshake(dc);

	/* HAS_DISPLAY: true. */

	/* 2. Initialize all combo PHYs (void contract: check the fault state after). */
	dc->last_child = 3;
	(void)parity_intel_combo_phy_init(dc->m, &dc_trace, &combo_n);
	if (parity_wait_time_base_faulted()) {
		fault(dc, "intel_combo_phy_init");
		return -EIO;
	}

	/* 3. Enable Power Well 1 (PG1); AUX wells enable on demand. */
	dc->last_child = 4;
	idx = parity_power_well_by_id(dc->pd, PARITY_SKL_DISP_PW_1);
	if (idx < 0) {
		fault(dc, "lookup_power_well(PW_1)");
		return -EIO;
	}
	mutex_lock(&dc->pd->lock);
	rc = parity_power_well_enable(&dc->pd->power_wells[idx], dc->pwc);
	mutex_unlock(&dc->pd->lock);
	if (rc == -EIO) {
		fault(dc, "power_well_1_enable");
		return -EIO;
	}

	/* 4. Enable CDCLK (shared body; check the fault state after). */
	dc->last_child = 5;
	parity_intel_cdclk_init_hw(dc->cd);
	if (parity_wait_time_base_faulted()) {
		fault(dc, "intel_cdclk_init_hw");
		return -EIO;
	}

	/* gen12_dbuf_slices_config(): no-op on ADL-P. */

	/* 5. Enable DBUF. */
	dc->last_child = 6;
	gen9_dbuf_enable(dc);

	/* 6. icl_mbus_init(): no-op on ADL-P. */

	/* 7. Program arbiter BW_BUDDY registers. */
	dc->last_child = 7;
	tgl_bw_buddy_init(dc);

	/* Wa_14011508470:...,adl-p (IP 12.0..13.0). */
	rmw(dc->m, GEN11_CHICKEN_DCPR_2, 0u,
		DCPR_CLEAR_MEMSTAT_DIS | DCPR_SEND_RESP_IMM |
		DCPR_MASK_LPMODE | DCPR_MASK_MAXLATENCY_MEMUP_CLR);

	/* Wa_14011503030:xelpd. */
	osdep_mmio_raw_write32(dc->m, XELPD_DISPLAY_ERR_FATAL_MASK, ~0u);
	return 0;
}

/*
 * gen9_dc_off_power_well_enable() -> gen9_disable_dc_states(): disable DC states,
 * then READ the current CDCLK/DBUF and COMPARE against the saved state (never a
 * re-init), and restore the combo PHYs.  The shared cdclk / dbuf-mask objects are
 * used, so no simplified per-DC_off duplicate.
 */
void
parity_dc_off_enable(struct parity_pw_ctx *c)
{
	unsigned n = 0u, s;

	c->dc_off_enable_calls++;

	/* target == DC3CO would take tgl_disable_dc3co(); ADL-P target is UPTO_DC6. */
	if (c->target_dc_state == DC_STATE_EN_DC3CO)
		return;

	/*
	 * gen9_set_dc_state(DC_STATE_DISABLE): the reference's own function, so the
	 * write is verified AND display.dmc.dc_state follows it.  (An earlier version
	 * wrote the register directly; the software copy then stayed at the old value
	 * and the next DC enable reported a false "DC state mismatch" -- seen the first
	 * time DC_off was taken after P7, by the eDP AUX acquisition.)
	 */
	parity_gen9_set_dc_state(c, 0u);

	/* HAS_DISPLAY: read CDCLK into a temp and COMPARE (no re-init). */
	if (c->cd != 0) {
		struct parity_cdclk_config tmp;
		for (s = 0u; s < sizeof(tmp); s++) ((char *)&tmp)[s] = 0;
		parity_bxt_get_cdclk(c->cd, &tmp);
		c->dc_off_cdclk_readouts++;
		if (tmp.cdclk != c->cd->hw.cdclk || tmp.vco != c->cd->hw.vco)
			kern_logf("i915: parity DC_off: cdclk needs modeset (hw %u/%u vs saved %u/%u)\n",
				tmp.cdclk, tmp.vco, c->cd->hw.cdclk, c->cd->hw.vco);
	}

	/* gen9_assert_dbuf_enabled(): COMPARE the HW slice mask to the saved one. */
	if (c->dbuf_slices != 0) {
		uint8_t cur = 0u;
		for (s = 0u; s < 4u; s++)
			if (osdep_mmio_raw_read32(c->mmio, dbuf_ctl_s[s]) & DBUF_POWER_STATE)
				cur |= (uint8_t)(1u << s);
		c->dc_off_dbuf_asserts++;
		if (cur != *c->dbuf_slices)
			kern_logf("i915: parity DC_off: DBUF mismatch (hw 0x%x vs saved 0x%x)\n",
				cur, *c->dbuf_slices);
	}

	/* DISPLAY_VER >= 11: DMC loses combo PHY B context across DC, so restore it. */
	osdep_trace_init(&dc_trace);
	(void)parity_intel_combo_phy_init(c->mmio, &dc_trace, &n);
	c->dc_off_combo_inits++;
}

void
parity_intel_power_domains_init_hw(struct parity_display_core *dc, int resume)
{
	struct parity_power_domains *pd = dc->pd;
	unsigned i;
	int rc;

	dc->initializing = 1;

	/* Share the DC_off enable dependencies through the well context (same objects). */
	dc->pwc->cd = dc->cd;
	dc->pwc->dbuf_slices = &dc->dbuf_enabled_slices;
	dc->pwc->target_dc_state = pd->target_dc_state;
	dc->pwc->allowed_dc_mask = pd->allowed_dc_mask;

	/* DISPLAY_VER >= 11: icl_display_core_init. */
	rc = icl_display_core_init(dc, resume);
	if (rc != 0) {
		/*
		 * Adaptation-layer fault mid-child: stop.  Do NOT take the INIT
		 * reference and do NOT run the sync (the fault position is kept).
		 */
		dc->initializing = 0;
		return;
	}

	/*
	 * Keep all power wells enabled for dependent HW access during init and to
	 * keep BIOS-enabled display HW powered until readout.  This reference is
	 * dropped in intel_power_domains_enable() (not here).
	 */
	(void)parity_display_power_get(pd, PARITY_PW_DOMAIN_INIT, dc->pwc);
	dc->init_wakeref_held = 1;
	dc->pm_wakeref = 1;   /* the runtime-PM side of the init wakeref */
	dc->reached_init_ref = 1;

	/*
	 * disable_power_well is set on ADL-P, so the reference does NOT take the
	 * extra disable_wakeref ( !disable_power_well is false ).
	 */

	/* intel_power_domains_sync_hw: sync every well under the domains lock. */
	mutex_lock(&pd->lock);
	for (i = 0u; i < pd->num_power_wells; i++)
		parity_power_well_sync_hw(&pd->power_wells[i], dc->pwc);
	mutex_unlock(&pd->lock);
	dc->reached_sync_hw = 1;

	dc->initializing = 0;
}

void
parity_intel_power_domains_driver_remove(struct parity_display_core *dc)
{
	/*
	 * intel_power_domains_driver_remove():
	 *   wakeref = fetch_and_zero(init_wakeref);
	 *   if (!disable_power_well) put(POWER_DOMAIN_INIT, disable_wakeref);  // skipped on ADL-P
	 *   flush_work_sync(); verify_state();
	 *   intel_runtime_pm_put(wakeref);   // cancel ONLY the rpm wakeref; wells stay enabled
	 *
	 * The main INIT reference is released as a RUNTIME-PM put, NOT a domain
	 * put -- the power wells keep their refcount and stay enabled (for reload).
	 */
	int had = dc->init_wakeref_held;

	dc->init_wakeref_held = 0;

	/* disable_power_well set on ADL-P: no disable_wakeref domain put. */

	/* flush async put work + verify state: no-op in this GPU-free model. */

	if (had)
		dc->pm_wakeref = 0;   /* runtime_pm_put(wakeref): rpm side only */
	/* NOTE: intentionally NO parity_display_power_put(POWER_DOMAIN_INIT). */
}
