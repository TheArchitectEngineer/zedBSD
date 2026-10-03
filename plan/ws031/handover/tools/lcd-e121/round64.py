#!/usr/bin/env python3
"""WS031 E-121 round 64: the DPLL readout (encoder get_config + pipe credit) as parity_intel_dpll_readout(), the DPLL
sanitize exported, and a GPU-free test of the native case (pipe A on DDI A fed by DPLL1).  usage: round64.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = open(P + "display_nogem.c").read()
a = c.index("	/*\n	 * encoder->get_config() for the linked encoders:")
b = c.index("		if (pll->on)\n			d->readout_dplls_on++;", a)
block = c[a:b]
# the block uses i and m; move it into its own function
func = ("/* encoder->get_config() for the linked encoders, then intel_dpll_readout_hw_state() (E-121) */" + NL +
        "void" + NL + "parity_intel_dpll_readout(struct parity_display_nogem *d, struct osdep_mmio *m)" + NL + "{" + NL +
        "	unsigned i;" + NL + NL + block.replace("		pll->active_mask = pll->pipe_mask;", "		pll->active_mask = pll->pipe_mask;" + NL + "	}") + "}" + NL + NL)
# in the readout: call it, then keep the per-PLL logging loop
c = c[:a] + """	parity_intel_dpll_readout(d, m);
	for (i = 0u; i < d->num_dplls; i++) {
		struct parity_dpll *pll = &d->dplls[i];

""" + c[b:]
# place the function before the readout function
k = c.index("/* ---------------- P5-d: the sanitize half ---------------- */")
# find the start of the readout function: put the helper just before the file's readout entry point
r0 = c.index("parity_intel_modeset_readout_hw_state(")
r0 = c.rindex(NL + NL, 0, r0) + 2
c = c[:r0] + func + c[r0:]
c = rep(c, "static void\nintel_dpll_sanitize_state(struct parity_display_nogem *d, struct osdep_mmio *m,",
        "void\nparity_intel_dpll_sanitize_state(struct parity_display_nogem *d, struct osdep_mmio *m,")
assert c.count("intel_dpll_sanitize_state(d, m") >= 1
c = c.replace("	intel_dpll_sanitize_state(d, m", "	parity_intel_dpll_sanitize_state(d, m")
open(P + "display_nogem.c", "w").write(c)

h = open(P + "display_nogem.h").read()
i = h.rindex("#endif")
h = h[:i] + ("/* E-121: encoder get_config (combo PHY clock select) + the PLL <- active pipe credit; the DPLL sanitize */" + NL +
             "void parity_intel_dpll_readout(struct parity_display_nogem *d, struct osdep_mmio *m);" + NL +
             "void parity_intel_dpll_sanitize_state(struct parity_display_nogem *d, struct osdep_mmio *m, int display_ver, int display_step);" + NL + NL) + h[i:]
open(P + "display_nogem.h", "w").write(h)

t = open(P + "ktest.c").read()
anchor = "		/* ---- P5A-CRTC: 6 planes per pipe (1 primary + 4 sprites + cursor) ---- */"
assert t.count(anchor) == 1
t = t.replace(anchor, r"""		/* ---- P5C-DPLL (E-121): the native start -- pipe A on DDI A fed by DPLL1 keeps DPLL1 ---- */
		{
			static struct parity_display_nogem t;
			unsigned kept;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_shared_dpll_init(&t, 13, 1);
			fake_mmio_open(&m, &f);
			osdep_mmio_raw_write32(&m, 0x164280u, 0x1u);          /* PHY A clock select = DPLL1 */
			osdep_mmio_raw_write32(&m, 0x46014u, 0xc0000000u);    /* DPLL1 enabled + locked (the GOP) */
			t.crtcs[0].state.active = 1;
			t.num_encoders = 1u;
			t.encoders[0].port = PARITY_PORT_A;
			t.encoders[0].phy = PARITY_PHY_A;
			t.encoders[0].clk_funcs = PARITY_DDI_CLK_ICL_COMBO;
			t.encoders[0].crtc_linked = 1;
			t.encoders[0].pipe_mask = 1u;
			parity_intel_dpll_readout(&t, &m);
			f.wt_n = 0u;
			parity_intel_dpll_sanitize_state(&t, &m, 13, 0);
			kept = (osdep_mmio_raw_read32(&m, 0x46014u) & 0x80000000u) != 0u;
			KCHECK(t.encoders[0].shared_dpll_id == 1 && t.dplls[1].on && t.dplls[1].pipe_mask == 1u &&
				t.dplls[1].active_mask == 1u && t.dplls[0].pipe_mask == 0u && kept && t.dplls_disabled == 0u,
				"p5c: P5C-DPLL native start: DDI A's clock select names DPLL1 -> pipe A credited -> sanitize keeps DPLL1");

			/* the reference's own case: a PLL on with no active pipe on it is disabled */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_shared_dpll_init(&t, 13, 1);
			fake_mmio_open(&m, &f);
			osdep_mmio_raw_write32(&m, 0x164280u, 0x0u);          /* PHY A -> DPLL0 */
			osdep_mmio_raw_write32(&m, 0x46010u, 0xc0000000u);
			osdep_mmio_raw_write32(&m, 0x46014u, 0xc0000000u);    /* DPLL1 on, nobody uses it */
			t.crtcs[0].state.active = 1;
			t.num_encoders = 1u;
			t.encoders[0].port = PARITY_PORT_A; t.encoders[0].phy = PARITY_PHY_A;
			t.encoders[0].clk_funcs = PARITY_DDI_CLK_ICL_COMBO; t.encoders[0].crtc_linked = 1; t.encoders[0].pipe_mask = 1u;
			parity_intel_dpll_readout(&t, &m);
			parity_intel_dpll_sanitize_state(&t, &m, 13, 0);
			KCHECK(t.dplls[0].active_mask == 1u && (osdep_mmio_raw_read32(&m, 0x46010u) & 0x80000000u) != 0u &&
				(osdep_mmio_raw_read32(&m, 0x46014u) & 0x80000000u) == 0u && t.dplls_disabled == 1u,
				"p5c: P5C-DPLL-UNUSED DPLL0 feeds pipe A and stays; DPLL1 on but unused is disabled (reference behaviour)");

			/* an active TC link whose PLL cannot be read: nothing is disabled */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_shared_dpll_init(&t, 13, 1);
			fake_mmio_open(&m, &f);
			osdep_mmio_raw_write32(&m, 0x46014u, 0xc0000000u);
			t.crtcs[1].state.active = 1;
			t.num_encoders = 1u;
			t.encoders[0].port = PARITY_PORT_TC1; t.encoders[0].phy = PARITY_PHY_F;
			t.encoders[0].clk_funcs = PARITY_DDI_CLK_ICL_TC; t.encoders[0].crtc_linked = 1; t.encoders[0].pipe_mask = 2u;
			parity_intel_dpll_readout(&t, &m);
			parity_intel_dpll_sanitize_state(&t, &m, 13, 0);
			KCHECK(t.dplls[1].readout_incomplete && (osdep_mmio_raw_read32(&m, 0x46014u) & 0x80000000u) != 0u &&
				t.dplls_disabled == 0u,
				"p5c: P5C-DPLL-TC an active TC link's PLL is unknown -> readout incomplete -> no PLL is disabled");
		}

""" + anchor)
open(P + "ktest.c", "w").write(t)
print("done")
