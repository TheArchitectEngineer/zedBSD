#!/usr/bin/env python3
"""WS031 E-121 round 63 (N1, bucket B, the DPLL hazard): the PLL <- pipe credit of the readout.
 reference: intel_modeset_readout_hw_state -> encoder->get_config (icl_ddi_combo_get_config ->
 intel_ddi_get_clock(icl_ddi_combo_get_pll()) = _icl_ddi_get_pll(ICL_DPCLKA_CFGCR0, DDI_CLK_SEL(phy))) fills
 crtc_state->shared_dpll; intel_dpll_readout_hw_state -> readout_dpll_hw_state: pipe_mask |= BIT(pipe) for every
 active crtc whose shared_dpll is this PLL; active_mask = pipe_mask.  intel_dpll_sanitize_state then leaves a PLL with
 an active pipe alone.  A linked TC encoder (icl_ddi_tc_get_pll not ported) makes the readout incomplete: sanitize
 then disables no PLL (recorded).  N0 also records ICL_DPCLKA_CFGCR0.  usage: round63.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
BS = chr(92)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

h = open(P + "display_nogem.h").read()
h = rep(h, """	unsigned pipe_mask;
	int is_mst;
	int in_use;
};""", """	unsigned pipe_mask;
	int is_mst;
	int in_use;
	int shared_dpll_id;     /* encoder->get_config: the PLL id feeding it (-1 = none / not read) */
	uint32_t dpclka_cfgcr0; /* the value that id was read from */
};""")
h = rep(h, """	unsigned pipe_mask;
	unsigned active_mask;
};""", """	unsigned pipe_mask;
	unsigned active_mask;
	int readout_incomplete; /* an active pipe's PLL could not be read (e.g. TC get_pll not ported): never disable */
};""")
open(P + "display_nogem.h", "w").write(h)

c = open(P + "display_nogem.c").read()
c = rep(c, """	/* intel_dpll_readout_hw_state() */
	for (i = 0u; i < d->num_dplls; i++) {
		struct parity_dpll *pll = &d->dplls[i];
		unsigned c;

		pll->on = (osdep_mmio_read32(m, pll->enable_reg) & PLL_ENABLE_BIT) ? 1 : 0;
		pll->pipe_mask = 0u;
		/*
		 * The reference credits a pipe to a PLL through
		 * crtc_state->shared_dpll, which is filled by the per-encoder config
		 * readout this port does not do.  With no active pipe there is nothing
		 * to credit; when one appears this must be revisited.
		 */
		for (c = 0u; c < (unsigned)PARITY_NOGEM_MAX_PIPES; c++)
			(void)c;
		pll->active_mask = pll->pipe_mask;""", """	/*
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
					"shared_dpll id %d\\n", (char)('A' + e->port), e->dpclka_cfgcr0, e->shared_dpll_id);
			} else {
				tc_unknown = 1;
				kern_logf("i915: parity P5c [ENCODER port %c] get_config: icl_ddi_tc_get_pll not ported -- the PLL "
					"of this active link is unknown\\n", (char)('A' + e->port));
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
		pll->active_mask = pll->pipe_mask;""")
c = rep(c, """		if (pll->active_mask != 0u)
			continue;
""", """		if (pll->active_mask != 0u)
			continue;
		if (pll->readout_incomplete) {
			kern_logf("i915: parity %s enabled, active_mask 0 but the readout is incomplete (an active link's PLL is "
				"unknown): NOT disabled\\n", pll->name);
			continue;
		}
""")
open(P + "display_nogem.c", "w").write(c)

n = open(P + "native_precheck.h").read()
n = rep(n, "	uint32_t pll_enable[2], ddi_buf_ctl_a, pp_status, pp_control, blc_ctl, blc_duty;",
        "	uint32_t pll_enable[2], ddi_buf_ctl_a, pp_status, pp_control, blc_ctl, blc_duty;" + NL +
        "	uint32_t dpclka_cfgcr0;         /* ICL_DPCLKA_CFGCR0: the PHY -> PLL clock select (E-121) */")
open(P + "native_precheck.h", "w").write(n)
n = open(P + "native_precheck.c").read()
n = rep(n, "		r->blc_duty = osdep_mmio_read32(m, 0xc8258u);", "		r->blc_duty = osdep_mmio_read32(m, 0xc8258u);" + NL +
        "		r->dpclka_cfgcr0 = osdep_mmio_read32(m, 0x164280u);")
a = n.index('kern_logf("i915: parity N0 pipe-A domain: DPLL0_ENABLE')
b = n.index("r->blc_duty);", a) + len("r->blc_duty);")
n = n[:a] + 'kern_logf("i915: parity N0 pipe-A domain: DPLL0_ENABLE=0x%08x DPLL1_ENABLE=0x%08x DPCLKA_CFGCR0=0x%08x (PHY A -> DPLL %u) "' + NL + \
    '\t\t\t"DDI_BUF_CTL_A=0x%08x PP_STATUS=0x%08x PP_CONTROL=0x%08x BLC_PWM_CTL=0x%08x BLC_PWM_DUTY=0x%08x' + BS + 'n", r->pll_enable[0],' + NL + \
    '\t\t\tr->pll_enable[1], r->dpclka_cfgcr0, r->dpclka_cfgcr0 & 3u, r->ddi_buf_ctl_a, r->pp_status, r->pp_control, r->blc_ctl,' + NL + \
    '\t\t\tr->blc_duty);' + n[b:]
open(P + "native_precheck.c", "w").write(n)
print("done")
