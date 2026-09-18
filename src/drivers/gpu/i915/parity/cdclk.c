/*
 * WS031 Linux-parity — CDCLK init for ADL-P (see cdclk.h).
 *
 * Ports intel_cdclk.c's bxt_* CDCLK path for display version 13 (ADL-P):
 * HAS_CDCLK_CRAWL == 1, HAS_CDCLK_SQUASH == 0.  The control flow (readout ->
 * sanitize -> reprogram-if-needed, with the PCODE prepare/notify handshake
 * around the PLL + CDCLK_CTL writes) is preserved; only register access, the
 * PLL-lock waits and the PCODE mailbox are adapted onto the osdep backends.
 */
#include "../internal.h"
#include <kern/klog.h>
#include "parity.h"
#include "osdep/mmio.h"
#include "cdclk.h"
#include "wait.h"
#include "pcode.h"

/* --- registers (i915_reg.h) --- */
#define SKL_DSSM                          0x51004u
#define ICL_DSSM_CDCLK_PLL_REFCLK_MASK    (7u << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_24MHz   (0u << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_19_2MHz (1u << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_38_4MHz (2u << 29)

#define CDCLK_CTL                         0x46000u
#define BXT_CDCLK_CD2X_DIV_SEL_MASK       (3u << 22)
#define BXT_CDCLK_CD2X_DIV_SEL_1          (0u << 22)
#define BXT_CDCLK_CD2X_DIV_SEL_1_5        (1u << 22)
#define BXT_CDCLK_CD2X_DIV_SEL_2          (2u << 22)
#define BXT_CDCLK_CD2X_DIV_SEL_4          (3u << 22)
#define CDCLK_FREQ_DECIMAL_MASK           0x7ffu
#define TGL_CDCLK_CD2X_PIPE_NONE          (7u << 19)   /* == ICL_CDCLK_CD2X_PIPE_NONE */

#define BXT_DE_PLL_ENABLE                 0x46070u
#define BXT_DE_PLL_PLL_ENABLE             (1u << 31)
#define BXT_DE_PLL_LOCK                   (1u << 30)
#define BXT_DE_PLL_FREQ_REQ               (1u << 23)
#define BXT_DE_PLL_FREQ_REQ_ACK           (1u << 22)
#define ICL_CDCLK_PLL_RATIO_MASK          0xffu

/* PCODE (i915_reg.h) */
#define SKL_PCODE_CDCLK_CONTROL           0x7u
#define SKL_CDCLK_PREPARE_FOR_CHANGE      0x3u
#define SKL_CDCLK_READY_FOR_CHANGE        0x1u

#define PARITY_INVALID_PIPE               (-1)

/* adlp_cdclk_table[] (intel_cdclk.c). waveform is 0 (ADL-P has no squash). */
static const struct parity_cdclk_vals adlp_cdclk_table[] = {
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

const struct parity_cdclk_vals *
parity_adlp_cdclk_table(void) { return adlp_cdclk_table; }

/* DIV_ROUND_CLOSEST for non-negative operands. */
static uint32_t divrc(uint32_t a, uint32_t b) { return (a + b / 2u) / b; }

static int hweight16(uint16_t w)
{
	int n = 0;

	while (w) { n += (int)(w & 1u); w >>= 1; }
	return n;
}

/* adlp_revids[]: PCI revid -> display stepping (only the entries we need). */
int
parity_adlp_display_step(uint8_t revid)
{
	switch (revid) {
	case 0x0: return PARITY_STEP_A0;
	case 0x4: return PARITY_STEP_B0;
	case 0x8: return PARITY_STEP_C0;
	case 0xC: return PARITY_STEP_D0;
	default:  return PARITY_STEP_NONE;
	}
}

void
parity_intel_init_cdclk_hooks(struct parity_cdclk_dev *cd,
	int display_ver, int display_step, int is_alderlake_p)
{
	cd->display_ver = display_ver;

	/*
	 * intel_init_cdclk_hooks() ADL-P branch.  Wa_22011320316 selects the
	 * a-step table for [STEP_A0, STEP_B0); every later stepping (our D0)
	 * takes adlp_cdclk_table + tgl_cdclk_funcs.  RPL-U is a distinct SKU we
	 * do not match (8086:46a8).
	 */
	if (is_alderlake_p && display_ver >= 12) {
		if (display_step >= PARITY_STEP_A0 && display_step < PARITY_STEP_B0) {
			/* adlp_a_step_cdclk_table (not this device) */
			cd->table = adlp_cdclk_table;
			cd->funcs = PARITY_CDCLK_FUNCS_TGL;
		} else {
			cd->table = adlp_cdclk_table;
			cd->funcs = PARITY_CDCLK_FUNCS_TGL;
		}
	} else {
		cd->table = adlp_cdclk_table;
		cd->funcs = PARITY_CDCLK_FUNCS_TGL;
	}

	/* Platform capabilities (xe_lpd_display: crawl yes, squash no). */
	cd->has_cdclk_crawl = 1;
	cd->has_cdclk_squash = 0;
}

/* calc_voltage_level() + tgl_calc_voltage_level(). */
static uint8_t
calc_voltage_level(int cdclk, int n, const int *max_cdclk)
{
	int i;

	for (i = 0; i < n; i++)
		if (cdclk <= max_cdclk[i])
			return (uint8_t)i;
	/* MISSING_CASE: clamp to the top level. */
	return (uint8_t)(n - 1);
}

uint8_t
parity_tgl_calc_voltage_level(int cdclk)
{
	static const int tgl_max[] = { 312000, 326400, 556800, 652800 };

	return calc_voltage_level(cdclk, 4, tgl_max);
}

static uint8_t
cdclk_calc_voltage_level(struct parity_cdclk_dev *cd, int cdclk)
{
	(void)cd;   /* funcs == TGL for ADL-P */
	return parity_tgl_calc_voltage_level(cdclk);
}

/* skl_cdclk_decimal(). */
static uint32_t
skl_cdclk_decimal(int cdclk) { return divrc((uint32_t)(cdclk - 1000), 500u); }

/* bxt_calc_cdclk(): lowest table cdclk >= min_cdclk for the current refclk. */
int
parity_bxt_calc_cdclk(struct parity_cdclk_dev *cd, int min_cdclk)
{
	const struct parity_cdclk_vals *t = cd->table;
	int i;

	for (i = 0; t[i].refclk; i++)
		if (t[i].refclk == cd->hw.ref && (int)t[i].cdclk >= min_cdclk)
			return (int)t[i].cdclk;
	kern_logf("i915: parity cdclk: cannot satisfy min cdclk %d @ refclk %u\n",
		min_cdclk, cd->hw.ref);
	return 0;
}

/* bxt_calc_cdclk_pll_vco(). */
int
parity_bxt_calc_cdclk_pll_vco(struct parity_cdclk_dev *cd, int cdclk)
{
	const struct parity_cdclk_vals *t = cd->table;
	int i;

	if ((uint32_t)cdclk == cd->hw.bypass)
		return 0;
	for (i = 0; t[i].refclk; i++)
		if (t[i].refclk == cd->hw.ref && (int)t[i].cdclk == cdclk)
			return (int)(cd->hw.ref * t[i].ratio);
	kern_logf("i915: parity cdclk: cdclk %d not valid for refclk %u\n",
		cdclk, cd->hw.ref);
	return 0;
}

/* bxt_cdclk_cd2x_div_sel(). */
static uint32_t
bxt_cdclk_cd2x_div_sel(struct parity_cdclk_dev *cd, int cdclk, int vco)
{
	switch ((int)divrc((uint32_t)vco, (uint32_t)cdclk)) {
	default:
		/* Reference WARNs unless cdclk == bypass && vco == 0. */
		(void)cd;
		/* fallthrough */
	case 2:  return BXT_CDCLK_CD2X_DIV_SEL_1;
	case 3:  return BXT_CDCLK_CD2X_DIV_SEL_1_5;
	case 4:  return BXT_CDCLK_CD2X_DIV_SEL_2;
	case 8:  return BXT_CDCLK_CD2X_DIV_SEL_4;
	}
}

/* cdclk_squash_waveform(): ADL-P table rows carry waveform 0. */
static uint16_t
cdclk_squash_waveform(struct parity_cdclk_dev *cd, int cdclk)
{
	const struct parity_cdclk_vals *t = cd->table;
	int i;

	if ((uint32_t)cdclk == cd->hw.bypass)
		return 0;
	for (i = 0; t[i].refclk; i++)
		if (t[i].refclk == cd->hw.ref && (int)t[i].cdclk == cdclk)
			return t[i].waveform;
	return 0xffffu;
}

static int cdclk_pll_is_unknown(uint32_t vco) { return vco == ~0u; }

/* --- readout: icl_readout_refclk / bxt_de_pll_readout / bxt_get_cdclk --- */

static void
icl_readout_refclk(struct parity_cdclk_dev *cd, struct parity_cdclk_config *cfg)
{
	uint32_t dssm = osdep_mmio_raw_read32(cd->m, SKL_DSSM) &
		ICL_DSSM_CDCLK_PLL_REFCLK_MASK;

	switch (dssm) {
	default:
		/* MISSING_CASE -> fallthrough to 24MHz (reference). */
	case ICL_DSSM_CDCLK_PLL_REFCLK_24MHz:   cfg->ref = 24000; break;
	case ICL_DSSM_CDCLK_PLL_REFCLK_19_2MHz: cfg->ref = 19200; break;
	case ICL_DSSM_CDCLK_PLL_REFCLK_38_4MHz: cfg->ref = 38400; break;
	}
}

static void
bxt_de_pll_readout(struct parity_cdclk_dev *cd, struct parity_cdclk_config *cfg)
{
	uint32_t val, ratio;

	/* DISPLAY_VER >= 11 (ADL-P is 13, not DG2). */
	icl_readout_refclk(cd, cfg);

	val = osdep_mmio_raw_read32(cd->m, BXT_DE_PLL_ENABLE);
	if ((val & BXT_DE_PLL_PLL_ENABLE) == 0u ||
	    (val & BXT_DE_PLL_LOCK) == 0u) {
		/* PLL disabled: VCO/ratio do not matter; 0 signals that. */
		cfg->vco = 0;
		return;
	}

	/* DISPLAY_VER >= 11: ratio is in the PLL enable register. */
	ratio = val & ICL_CDCLK_PLL_RATIO_MASK;
	cfg->vco = ratio * cfg->ref;
}

void
parity_bxt_get_cdclk(struct parity_cdclk_dev *cd, struct parity_cdclk_config *cfg)
{
	uint32_t divider;
	int div;

	bxt_de_pll_readout(cd, cfg);

	/* DISPLAY_VER >= 12: bypass = ref / 2. */
	cfg->bypass = cfg->ref / 2u;

	if (cfg->vco == 0u) {
		cfg->cdclk = cfg->bypass;
		goto out;
	}

	divider = osdep_mmio_raw_read32(cd->m, CDCLK_CTL) & BXT_CDCLK_CD2X_DIV_SEL_MASK;
	switch (divider) {
	case BXT_CDCLK_CD2X_DIV_SEL_1:   div = 2; break;
	case BXT_CDCLK_CD2X_DIV_SEL_1_5: div = 3; break;
	case BXT_CDCLK_CD2X_DIV_SEL_2:   div = 4; break;
	case BXT_CDCLK_CD2X_DIV_SEL_4:   div = 8; break;
	default:
		/* MISSING_CASE: leave cdclk unset (reference returns early). */
		return;
	}

	/* HAS_CDCLK_SQUASH == 0 on ADL-P: no squash_ctl read. */
	cfg->cdclk = divrc(cfg->vco, (uint32_t)div);

out:
	/* Voltage level is not read out; assume at least what cdclk requires. */
	cfg->voltage_level = cdclk_calc_voltage_level(cd, (int)cfg->cdclk);
}

void
parity_intel_update_cdclk(struct parity_cdclk_dev *cd)
{
	/* intel_cdclk_get_cdclk() == bxt_get_cdclk for ADL-P. */
	parity_bxt_get_cdclk(cd, &cd->hw);
	/* GMBUSFREQ_VLV update is VLV/CHV only: not applicable. */
}

/* --- PLL programming (icl_cdclk_pll_*, adlp_cdclk_pll_crawl) --- */

static void
icl_cdclk_pll_disable(struct parity_cdclk_dev *cd)
{
	uint32_t v = osdep_mmio_raw_read32(cd->m, BXT_DE_PLL_ENABLE);

	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, v & ~BXT_DE_PLL_PLL_ENABLE);
	if (parity_wait_reg(cd->m, BXT_DE_PLL_ENABLE, BXT_DE_PLL_LOCK, 0u, 10u, 1u, 0))
		kern_logf("i915: parity cdclk: timeout waiting for CDCLK PLL unlock\n");
	cd->hw.vco = 0;
}

static void
icl_cdclk_pll_enable(struct parity_cdclk_dev *cd, int vco)
{
	uint32_t ratio = divrc((uint32_t)vco, cd->hw.ref);
	uint32_t val = ratio & ICL_CDCLK_PLL_RATIO_MASK;

	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, val);
	val |= BXT_DE_PLL_PLL_ENABLE;
	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, val);
	if (parity_wait_reg(cd->m, BXT_DE_PLL_ENABLE, BXT_DE_PLL_LOCK, BXT_DE_PLL_LOCK,
			    10u, 1u, 0))
		kern_logf("i915: parity cdclk: timeout waiting for CDCLK PLL lock\n");
	cd->hw.vco = (uint32_t)vco;
}

static void
icl_cdclk_pll_update(struct parity_cdclk_dev *cd, int vco)
{
	if (cd->hw.vco != 0u && cd->hw.vco != (uint32_t)vco)
		icl_cdclk_pll_disable(cd);
	if (cd->hw.vco != (uint32_t)vco)
		icl_cdclk_pll_enable(cd, vco);
}

static void
adlp_cdclk_pll_crawl(struct parity_cdclk_dev *cd, int vco)
{
	uint32_t ratio = divrc((uint32_t)vco, cd->hw.ref);
	uint32_t val;

	/* Write PLL ratio without disabling. */
	val = (ratio & ICL_CDCLK_PLL_RATIO_MASK) | BXT_DE_PLL_PLL_ENABLE;
	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, val);

	/* Submit freq change request. */
	val |= BXT_DE_PLL_FREQ_REQ;
	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, val);

	if (parity_wait_reg(cd->m, BXT_DE_PLL_ENABLE,
			    BXT_DE_PLL_LOCK | BXT_DE_PLL_FREQ_REQ_ACK,
			    BXT_DE_PLL_LOCK | BXT_DE_PLL_FREQ_REQ_ACK, 10u, 1u, 0))
		kern_logf("i915: parity cdclk: timeout waiting for FREQ change ack\n");

	val &= ~BXT_DE_PLL_FREQ_REQ;
	osdep_mmio_raw_write32(cd->m, BXT_DE_PLL_ENABLE, val);
	cd->hw.vco = (uint32_t)vco;
}

/* --- _bxt_set_cdclk / bxt_set_cdclk (ADL-P: crawl yes, squash no) --- */

static void
_bxt_set_cdclk(struct parity_cdclk_dev *cd, const struct parity_cdclk_config *cfg,
	int pipe)
{
	int cdclk = (int)cfg->cdclk;
	int vco = (int)cfg->vco;
	int unsquashed_cdclk;
	uint16_t waveform;
	uint32_t val;

	cd->diag_hw_sequence_reached = 1;

	if (cd->has_cdclk_crawl && cd->hw.vco > 0u && vco > 0 &&
	    !cdclk_pll_is_unknown(cd->hw.vco)) {
		if (cd->hw.vco != (uint32_t)vco)
			adlp_cdclk_pll_crawl(cd, vco);
	} else {
		/* DISPLAY_VER >= 11. */
		icl_cdclk_pll_update(cd, vco);
	}

	waveform = cdclk_squash_waveform(cd, cdclk);
	unsquashed_cdclk = (int)divrc((uint32_t)cdclk * 16u,
		(uint32_t)hweight16(waveform ? waveform : 0xffffu));

	/* HAS_CDCLK_SQUASH == 0 on ADL-P: no dg2_cdclk_squash_program(). */

	val = bxt_cdclk_cd2x_div_sel(cd, unsquashed_cdclk, vco);
	/* pipe == INVALID_PIPE at init -> CD2X pipe NONE (no vblank sync). */
	val |= (pipe == PARITY_INVALID_PIPE) ? TGL_CDCLK_CD2X_PIPE_NONE
					     : ((uint32_t)pipe << 20);
	/* SSA precharge is GLK/BXT only; DISPLAY_VER < 20 -> decimal freq. */
	val |= skl_cdclk_decimal(cdclk) & CDCLK_FREQ_DECIMAL_MASK;

	osdep_mmio_raw_write32(cd->m, CDCLK_CTL, val);
	/* pipe == INVALID_PIPE: no intel_crtc_wait_for_next_vblank(). */
}

void
parity_bxt_set_cdclk(struct parity_cdclk_dev *cd, const struct parity_cdclk_config *cfg)
{
	int cdclk = (int)cfg->cdclk;
	int ret;

	cd->diag_requested = *cfg;

	/*
	 * Inform PCU of the upcoming change (DISPLAY_VER >= 11, not DG2):
	 * skl_pcode_request(CDCLK_CONTROL, PREPARE, READY, READY, 3).
	 */
	ret = parity_skl_pcode_request(cd->sb_lock, cd->m, SKL_PCODE_CDCLK_CONTROL,
		SKL_CDCLK_PREPARE_FOR_CHANGE, SKL_CDCLK_READY_FOR_CHANGE,
		SKL_CDCLK_READY_FOR_CHANGE, 3);
	cd->diag_prepare_status = ret;
	if (ret) {
		kern_logf("i915: parity cdclk: PCU prepare failed (err %d, freq %d)\n",
			ret, cdclk);
		return;   /* no PLL / CDCLK_CTL writes on prepare failure */
	}

	/*
	 * cdclk_compute_crawl_and_squash_midpoint() needs BOTH crawl and squash;
	 * ADL-P has squash == 0, so a single _bxt_set_cdclk() (no midpoint).
	 */
	_bxt_set_cdclk(cd, cfg, PARITY_INVALID_PIPE);

	/* Notify the new voltage level (DISPLAY_VER >= 11, not DG2). */
	ret = parity_snb_pcode_write(cd->sb_lock, cd->m, SKL_PCODE_CDCLK_CONTROL,
		cfg->voltage_level);
	cd->diag_notify_status = ret;
	if (ret) {
		kern_logf("i915: parity cdclk: PCODE freq set failed (err %d, freq %d)\n",
			ret, cdclk);
		return;   /* HW already changed; do NOT fake the state update */
	}

	parity_intel_update_cdclk(cd);

	/* Voltage level can't be read back; assume as requested. */
	cd->hw.voltage_level = cfg->voltage_level;
}

/* --- sanitize + init_hw --- */

void
parity_bxt_sanitize_cdclk(struct parity_cdclk_dev *cd)
{
	uint32_t cdctl, expected;
	int cdclk, clock, vco;

	parity_intel_update_cdclk(cd);
	cd->diag_observed_before = cd->hw;

	if (cd->hw.vco == 0u || cd->hw.cdclk == cd->hw.bypass)
		goto sanitize;

	/* DPLL okay; verify CDCLK_CTL (BIOS may leave a bad decimal / MBZ bits). */
	cdctl = osdep_mmio_raw_read32(cd->m, CDCLK_CTL);
	/* Ignore the pipe field (BIOS may have synced to a pipe or PIPE_NONE). */
	cdctl &= ~TGL_CDCLK_CD2X_PIPE_NONE;

	cdclk = parity_bxt_calc_cdclk(cd, (int)cd->hw.cdclk);
	if (cdclk != (int)cd->hw.cdclk)
		goto sanitize;

	vco = parity_bxt_calc_cdclk_pll_vco(cd, cdclk);
	if (vco != (int)cd->hw.vco)
		goto sanitize;

	expected = skl_cdclk_decimal(cdclk);

	/* HAS_CDCLK_SQUASH == 0 -> clock is the cdclk. */
	clock = (int)cd->hw.cdclk;
	expected |= bxt_cdclk_cd2x_div_sel(cd, clock, (int)cd->hw.vco);
	/* SSA precharge is GLK/BXT only: not applicable to ADL-P. */

	if (cdctl == expected) {
		cd->diag_sanitized = cd->hw;   /* nothing to sanitize */
		return;
	}

sanitize:
	kern_logf("i915: parity cdclk: sanitizing cdclk programmed by pre-os\n");
	cd->hw.cdclk = 0;        /* force cdclk programming */
	cd->hw.vco = ~0u;        /* force full PLL disable + enable */
	cd->diag_sanitized = cd->hw;
}

static void
bxt_cdclk_init_hw(struct parity_cdclk_dev *cd)
{
	struct parity_cdclk_config cfg;

	parity_bxt_sanitize_cdclk(cd);

	if (cd->hw.cdclk != 0u && cd->hw.vco != 0u) {
		cd->diag_no_change = 1;   /* sanitize accepted HW: nothing to do */
		return;
	}

	cfg = cd->hw;
	/* FIXME(reference): initial CDCLK should come from VBT; use min for now. */
	cfg.cdclk = (uint32_t)parity_bxt_calc_cdclk(cd, 0);
	cfg.vco = (uint32_t)parity_bxt_calc_cdclk_pll_vco(cd, (int)cfg.cdclk);
	cfg.voltage_level = cdclk_calc_voltage_level(cd, (int)cfg.cdclk);

	parity_bxt_set_cdclk(cd, &cfg);
}

void
parity_intel_cdclk_init_hw(struct parity_cdclk_dev *cd)
{
	/* DISPLAY_VER >= 10 || BXT -> bxt path. */
	bxt_cdclk_init_hw(cd);
}
