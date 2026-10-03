#!/usr/bin/env python3
"""WS031 E-123 round 83: HDMI-B (-DPARITY_HDMI_B_TEST=1) -- one picture on the external HDMI display through the
reference's modeset (port B / pipe B / transcoder B / DPLL 0), a finite window for the camera, then the reference's
stop path.  The machinery is LCD-B's; the run parameters carry the output, the pipe and the state to use.
Idempotent.  usage: round83.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)

def edit(path, pairs, marker):
    s = open(path).read()
    if marker in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90])
        s = s.replace(old, new)
    open(path, "w").write(s)

edit(L + "parity_lcd_kernel.c", [
    # run parameters: which output / pipe / port, and the state when it is not the panel's
    ("struct lcd_run_params {" + NL + "	unsigned pattern_id;",
     "struct lcd_run_params {" + NL +
     "	/* HDMI-B: the output is an HDMI sink on another port / pipe, and the state is the caller's (no EDID, no DPCD) */" + NL +
     "	int output_hdmi, port, pipe, cpu_transcoder, dpll_id;" + NL +
     "	const struct parity_lcd_state *state;" + NL +
     "	const char *tag;" + NL +
     "	unsigned pattern_id;"),
    # preflight: for HDMI the panel is not this test's subject; the pipe / port it uses must be idle
    ("	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {",
     "	if (k->p != 0 && k->p->output_hdmi) {" + NL +
     "		uint32_t tcf = osdep_mmio_read32(d->mmio, 0x71008u + 0u);   /* TRANSCONF(B) */" + NL +
     "		uint32_t bctl = osdep_mmio_read32(d->mmio, 0x64100u);       /* DDI_BUF_CTL(B) */" + NL + NL +
     "		if (!d->gm->inited) {" + NL +
     '			kern_logf("i915: parity HDMI-B preflight: no GT memory (GGTT) for the scanout buffer' + chr(92) + 'n");' + NL +
     "			return -1;" + NL +
     "		}" + NL +
     '		kern_logf("i915: parity HDMI-B preflight: TRANSCONF(B)=0x%08x DDI_BUF_CTL(B)=0x%08x SDEISR=0x%08x ' +
     'DPLL0=0x%08x DPLL1=0x%08x' + chr(92) + 'n",' + NL +
     "			tcf, bctl, osdep_mmio_read32(d->mmio, 0xc4000u), osdep_mmio_read32(d->mmio, 0x46010u)," + NL +
     "			osdep_mmio_read32(d->mmio, 0x46014u));" + NL +
     "		if ((tcf & 0x80000000u) != 0u || (bctl & 0x80000000u) != 0u) {" + NL +
     '			kern_logf("i915: parity HDMI-B preflight: pipe B / DDI B are not idle' + chr(92) + 'n");' + NL +
     "			return -1;" + NL +
     "		}" + NL +
     "		return 0;" + NL +
     "	}" + NL +
     "	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {"),
    # the run: the parameters decide the output
    ("	bind_ops(k);" + NL + NL + "	memset(&env, 0, sizeof(env));" + NL +
     "	if (preflight(k) != 0 || fill_cfg(k, &env.cfg) != 0) {" + NL +
     '		kern_logf("i915: parity LCD-B verdict: FAIL (preflight: nothing was written to the display)' + chr(92) + 'n");',
     "	bind_ops(k);" + NL + "	k->p = p;" + NL + NL + "	memset(&env, 0, sizeof(env));" + NL +
     "	if (preflight(k) != 0 || fill_cfg(k, &env.cfg) != 0) {" + NL +
     '		kern_logf("i915: parity LCD-B verdict: FAIL (preflight: nothing was written to the display)' + chr(92) + 'n");'),
    ("	env.hw = &k->ops;" + NL +
     "	env.gm = d->gm;" + NL +
     "	env.so = &lcdb_scanout;" + NL +
     "	env.lcd = &d->edp->lcd;" + NL +
     "	env.pipe = 0;",
     "	env.hw = &k->ops;" + NL +
     "	env.gm = d->gm;" + NL +
     "	env.so = &lcdb_scanout;" + NL +
     "	env.lcd = p->output_hdmi ? p->state : &d->edp->lcd;" + NL +
     "	env.pipe = p->pipe;" + NL +
     "	if (p->output_hdmi) {" + NL +
     "		/* what intel_ddi_init() would have left for the HDMI encoder of this port */" + NL +
     "		env.cfg.output_hdmi = 1;" + NL +
     "		env.cfg.port = p->port;" + NL +
     "		env.cfg.pipe = p->pipe;" + NL +
     "		env.cfg.cpu_transcoder = p->cpu_transcoder;" + NL +
     "		env.cfg.dpll_id = p->dpll_id;" + NL +
     "		env.cfg.aux_ch = p->port;" + NL +
     "		env.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));" + NL +
     "		env.cfg.vbt_backlight_present = 0;" + NL +
     '		kern_logf("i915: parity HDMI-B input: mode %ux%u %d kHz | port=%d pipe=%d transcoder=%d DPLL%d | ' +
     'PLL cfgcr0=0x%08x cfgcr1=0x%08x div0=0x%08x | saved DDI_BUF_CTL bits 0x%x' + chr(92) + 'n",' + NL +
     "			p->state->mode.hdisplay, p->state->mode.vdisplay, p->state->mode.clock_khz, p->port, p->pipe," + NL +
     "			p->cpu_transcoder, p->dpll_id, p->state->pll.cfgcr0, p->state->pll.cfgcr1, p->state->pll.div0," + NL +
     "			env.cfg.saved_port_bits);" + NL +
     "	}"),
    # LCD-B's own run parameters keep their (zero) output fields
    ("int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d)" + NL +
     "{" + NL +
     "	static const struct lcd_run_params p = { PARITY_LCDB_PATTERN_ID, PARITY_LCDB_PATTERN_FNV, PARITY_LCDB_WINDOW_MS, 0 };" + NL +
     "" + NL +
     "	return lcd_run_one(d, &p);" + NL +
     "}",
     "int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d)" + NL +
     "{" + NL +
     "	static const struct lcd_run_params p = { 0, 0, 0, 0, 0, 0, \"LCD-B\", PARITY_LCDB_PATTERN_ID," + NL +
     "		PARITY_LCDB_PATTERN_FNV, PARITY_LCDB_WINDOW_MS, 0 };" + NL +
     "" + NL +
     "	return lcd_run_one(d, &p);" + NL +
     "}" + NL + NL +
     "/*" + NL +
     " * HDMI-B: the external display on DDI B.  The mode is CEA-861 format 4 (1280x720p60, 74.25 MHz), which every" + NL +
     " * HDMI sink supports; the reference would take the sink's preferred mode from its EDID, which this port cannot" + NL +
     " * read (the DDC does not answer -- the same happens with Linux on this machine), so the mode is given here." + NL +
     " * ADAPTATION, recorded: mode and `connected` do not come from the sink." + NL +
     " */" + NL +
     "int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d)" + NL +
     "{" + NL +
     "	static struct parity_lcd_state hs;" + NL +
     "	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };" + NL +
     "	struct lcd_run_params p;" + NL +
     "	int rc;" + NL + NL +
     "	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hs);" + NL +
     "	if (rc != 0) {" + NL +
     '		kern_logf("i915: parity HDMI-B verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)' +
     chr(92) + 'n", rc);' + NL +
     "		return -1;" + NL +
     "	}" + NL +
     "	memset(&p, 0, sizeof(p));" + NL +
     "	p.output_hdmi = 1; p.port = 1; p.pipe = 1; p.cpu_transcoder = 1; p.dpll_id = 0;" + NL +
     "	p.state = &hs;" + NL +
     '	p.tag = "HDMI-B";' + NL +
     "	p.pattern_id = PARITY_LCDB_PATTERN_ID;" + NL +
     "	p.pattern_fnv = 0;                      /* the picture's hash is pinned at 1920x1080 only */" + NL +
     "	p.window_ms = PARITY_HDMIB_WINDOW_MS;" + NL +
     "	rc = lcd_run_one(d, &p);" + NL +
     '	kern_logf("i915: parity HDMI-B verdict: %s (the LCD-B lines above carry the detail; the photograph is separate ' +
     'evidence)' + chr(92) + 'n", rc == 0 ? "PASS" : "FAIL");' + NL +
     "	return rc;" + NL +
     "}"),
], "parity_lcd_kernel_hdmib_run")

# the run parameters must be reachable from preflight()
edit(L + "parity_lcd_kernel.c", [
    ("struct lcd_kernel {", "struct lcd_run_params;" + NL + "struct lcd_kernel {" + NL +
     "	const struct lcd_run_params *p;         /* the parameters of the run (0 before it starts) */")],
    "const struct lcd_run_params *p;")

edit(L + "parity_lcd_kernel.h", [
    ("int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);",
     "int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);" + NL +
     "/* HDMI-B (-DPARITY_HDMI_B_TEST=1): one picture on the external HDMI display (DDI B, pipe B), then the stop path */" + NL +
     "#ifndef PARITY_HDMIB_WINDOW_MS" + NL +
     "#define PARITY_HDMIB_WINDOW_MS  20000u" + NL +
     "#endif" + NL +
     "int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d);")],
    "parity_lcd_kernel_hdmib_run")

h = open(P + "bios.h").read()
if "PARITY_HDMI_B_TEST 0" not in h:
    h = h.replace("#ifndef PARITY_HDMI_EDID_TEST" + NL,
        "#ifndef PARITY_HDMI_B_TEST" + NL +
        "#define PARITY_HDMI_B_TEST 0          /* E-123: one picture on the external HDMI display, then the stop path */" + NL +
        "#endif" + NL + "#ifndef PARITY_HDMI_EDID_TEST" + NL, 1)
    h = h.replace("#define PARITY_VBT_EXPLICIT (PARITY_HDMI_EDID_TEST || ",
                  "#define PARITY_VBT_EXPLICIT (PARITY_HDMI_B_TEST || PARITY_HDMI_EDID_TEST || ", 1)
    open(P + "bios.h", "w").write(h)

c = open(P + "probe.c").read()
if "parity_lcd_kernel_hdmib_run" not in c:
    old = ("	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST || "
           "PARITY_LCDO_TEST) {")
    assert c.count(old) == 1
    c = c.replace(old, ("	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST || "
                        "PARITY_LCDO_TEST || PARITY_HDMI_B_TEST) {"))
    old2 = "			} else if (PARITY_LCDO_TEST) {"
    assert c.count(old2) == 1
    c = c.replace(old2, "			} else if (PARITY_HDMI_B_TEST) {" + NL +
                  "				(void)parity_lcd_kernel_hdmib_run(&lcdb);" + NL + old2)
    open(P + "probe.c", "w").write(c)
print("done")
