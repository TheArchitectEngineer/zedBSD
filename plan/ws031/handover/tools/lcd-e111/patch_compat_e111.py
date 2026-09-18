#!/usr/bin/env python3
"""WS031 E-111a: lcd_compat.h / parity_lcd_calc.{h,c} / glue / build lists for the DDI + cpu-transcoder writers.
usage: patch_compat_e111.py <repo root>"""
import sys, os
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ------------------------------------------------------------------ lcd_compat.h
c = load(L + "lcd_compat.h")
c = rep(c, " * (drm_edid_mode_port.c, intel_link_port.c, intel_dpll_port.c) are compiled in.",
           " * (drm_edid_mode_port.c, intel_link_port.c, intel_dpll_port.c, intel_display_port.c, intel_ddi_port.c,\n * intel_vrr_port.c) are compiled in.")
c = rep(c, "#define REG_GENMASK(h, l)", "#define BIT(n) (1u << (n))\n#define REG_BIT(n) ((u32)1u << (n))\n#define REG_GENMASK(h, l)")
c = rep(c, "#define MISSING_CASE(x) parity_lcd_note(\"Missing case (\" #x \")\\n\")\n",
           "#define MISSING_CASE(x) parity_lcd_note(\"Missing case (\" #x \")\\n\")\n"
           "#define drm_WARN_ON(dev, cond) ({ int _w = !!(cond); if (_w) parity_lcd_note(\"WARN_ON(\" #cond \")\\n\"); _w; })\n")
c = rep(c, "struct drm_device { int unused; };\n",
           "struct drm_device { int unused; };\n"
           "#include \"lcd_drm_colorspace.h\"  /* reference, extracted: enum drm_colorspace */\n"
           "struct drm_connector_state { enum drm_colorspace colorspace; };\n")
c = rep(c, "#define IS_ALDERLAKE_P(i915) 1\n",
           "#define IS_ALDERLAKE_P(i915) 1\n#define IS_DG2(i915) 0\n#define IS_DG1(i915) 0\n#define IS_ROCKETLAKE(i915) 0\n"
           "#define IS_JASPERLAKE(i915) 0\n#define IS_ICELAKE(i915) 0\n#define IS_CHERRYVIEW(i915) 0\n"
           "#define IS_DISPLAY_VER(i915, from, until) (13 >= (from) && 13 <= (until))\n#define HAS_VRR(i915) (DISPLAY_VER(i915) >= 11)\n")
c = rep(c, """struct parity_lcd_emit {
	void *ctx;
	void (*write32)(void *ctx, u32 reg, u32 value);
};
""", """struct parity_lcd_emit {
	void *ctx;
	void (*write32)(void *ctx, u32 reg, u32 value);
	void (*rmw32)(void *ctx, u32 reg, u32 clear, u32 set);  /* read-modify-write: only the hardware knows the result */
	void (*posting_read)(void *ctx, u32 reg);               /* may be NULL */
};
""")
c = rep(c, "struct drm_crtc { struct drm_device *dev; };\n",
           "struct drm_crtc { struct drm_device *dev; };\n"
           "/* drm_atomic.h: the three flags drm_atomic_crtc_needs_modeset() looks at */\n"
           "struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed; };\n"
           "static inline bool drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state)\n"
           "{\n\treturn state->mode_changed || state->active_changed || state->connectors_changed;\n}\n"
           "#include \"lcd_ddi_types.h\"       /* reference, extracted: enum port, phy, intel_output_type, intel_output_format */\n")
c = rep(c, """struct intel_crtc_state {
	struct { struct drm_crtc *crtc; } uapi;
	struct { struct drm_display_mode adjusted_mode; } hw;
	int port_clock;
	int cpu_transcoder;         /* enum transcoder */
	struct drm_rect pipe_src;
};
#define intel_crtc_has_type(state, type) (0)   /* no SDVO output on this platform */
#define INTEL_OUTPUT_SDVO 0
#define IS_HASWELL(i915) 0
""", """struct intel_crtc_state {
	struct drm_crtc_state uapi;
	struct { struct drm_display_mode adjusted_mode; } hw;
	int port_clock;
	int cpu_transcoder;         /* enum transcoder */
	int master_transcoder, mst_master_transcoder;
	struct drm_rect pipe_src;
	unsigned int output_types;  /* bitmask of enum intel_output_type */
	enum intel_output_format output_format;
	int pipe_bpp, lane_count, fdi_lanes;
	u8 pixel_multiplier, framestart_delay;
	bool has_pch_encoder, dither, limited_color_range;
	bool has_hdmi_sink, hdmi_scrambling, hdmi_high_tmds_clock_ratio;
	struct { bool force_thru; } pch_pfit;
	struct intel_link_m_n dp_m_n, dp_m2_n2, fdi_m_n;
	struct { u16 flipline, vmin, vmax, guardband, pipeline_full; } vrr;
};
#define IS_HASWELL(i915) 0
/* encoder / digital port: the members the kept intel_ddi.c functions use */
struct intel_encoder { struct { struct drm_device *dev; } base; enum port port; };
struct intel_dp { u32 DP; };
struct intel_digital_port { struct intel_encoder base; struct intel_dp dp; u32 saved_port_bits; };
#define enc_to_dig_port(encoder) container_of(encoder, struct intel_digital_port, base)
#define enc_to_intel_dp(encoder) (&enc_to_dig_port(encoder)->dp)
#define intel_tc_port_in_tbt_alt_mode(dig_port) (0)   /* only reached for a Type-C PHY */
""")
c = rep(c, "#define _MMIO_TRANS2(tran, r) _MMIO(parity_lcd_trans_offset(tran) - 0x60000u + (r))\n",
           "#define _MMIO_TRANS2(tran, r) _MMIO(parity_lcd_trans_offset(tran) - 0x60000u + (r))\n"
           "/* pipe register blocks sit at the same 0x1000 spacing (the reference's pipe_offsets[]) */\n"
           "#define _MMIO_PIPE2(pipe, r) _MMIO(0x1000u * (u32)((pipe) < 0 || (pipe) > 3 ? 0 : (pipe)) + (r))\n"
           "#define _PICK_EVEN(index, a, b) ((a) + (index) * ((b) - (a)))\n"
           "#define _PICK(index, ...) (((const u32 []){ __VA_ARGS__ })[index])\n"
           "#define _MMIO_PORT(port, a, b) _MMIO(_PICK_EVEN(port, a, b))\n"
           "#define _MMIO_TRANS(tran, a, b) _MMIO(_PICK_EVEN(tran, a, b))\n")
c = rep(c, "#define intel_de_write(i915, r, v) (i915)->emit->write32((i915)->emit->ctx, (r).reg, (v))\n",
           "#define intel_de_write(i915, r, v) (i915)->emit->write32((i915)->emit->ctx, (r).reg, (v))\n"
           "#define intel_de_rmw(i915, r, clear, set) (i915)->emit->rmw32((i915)->emit->ctx, (r).reg, (clear), (set))\n"
           "#define intel_de_posting_read(i915, r) do { if ((i915)->emit->posting_read != 0) (i915)->emit->posting_read((i915)->emit->ctx, (r).reg); } while (0)\n")
c = rep(c, "/* prototypes of the kept non-static reference functions */\n",
           "#include \"lcd_trans_regs.h\"      /* reference, extracted: enum transcoder (the inlines below use it) */\n"
           "#include \"lcd_ref_inlines.h\"     /* reference, extracted: intel_crtc_has_type, _has_dp_encoder, _needs_modeset, transcoder_is_dsi */\n"
           "/* prototypes of the kept non-static reference functions */\n"
           "bool intel_phy_is_tc(struct drm_i915_private *dev_priv, enum phy phy);\n"
           "enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port);\n"
           "bool intel_dp_is_uhbr(const struct intel_crtc_state *crtc_state);\n"
           "bool intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);\n"
           "bool intel_cpu_transcoder_has_m2_n2(struct drm_i915_private *dev_priv, enum transcoder transcoder);\n"
           "void intel_cpu_transcoder_set_m1_n1(struct intel_crtc *crtc, enum transcoder transcoder, const struct intel_link_m_n *m_n);\n"
           "void intel_cpu_transcoder_set_m2_n2(struct intel_crtc *crtc, enum transcoder transcoder, const struct intel_link_m_n *m_n);\n"
           "void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state);\n"
           "void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);\n"
           "void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);\n"
           "i915_reg_t hsw_chicken_trans_reg(struct drm_i915_private *i915, enum transcoder cpu_transcoder);\n")
save(L + "lcd_compat.h", c)

# ------------------------------------------------------------------ parity_lcd_calc.h
h = load(L + "parity_lcd_calc.h")
h = rep(h, """struct parity_lcd_regwrite { uint32_t reg, value; };
#define PARITY_LCD_MAX_REGWRITES 24u
""", """struct parity_lcd_regwrite {
	uint32_t reg, value;          /* a write: the value.  A read-modify-write: the bits SET */
	uint32_t clear;               /* read-modify-write only: the bits cleared first */
	uint8_t rmw;                  /* 1 = read-modify-write (the resulting word depends on the hardware) */
};
#define PARITY_LCD_MAX_REGWRITES 32u
""")
h = rep(h, """/*
 * 0, or a negative Linux errno: -22 malformed input""", """/*
 * What the reference's hsw_configure_cpu_transcoder() issues for `s`: M/N, timings, the VRR words
 * (CHICKEN_TRANS bit, TRANS_VRR_CTL), TRANS_MULT, the frame start delay and TRANSCONF -- in ITS order,
 * because the caller itself is reference text.  TRANSCONF comes out without the enable bit (a modeset:
 * the transcoder is enabled later by intel_enable_transcoder()).
 */
int parity_lcd_emit_cpu_transcoder(const struct parity_lcd_state *s, int pipe, int cpu_transcoder,
	struct parity_lcd_words *out);

/*
 * The DDI-side words for an eDP/DP SST output on `port` (0 = A): TRANS_MSA_MISC (intel_ddi_set_dp_msa),
 * TRANS_DDI_FUNC_CTL2 and TRANS_DDI_FUNC_CTL (intel_ddi_enable_transcoder_func), and -- a VALUE, not a
 * write -- what intel_ddi_init_dp_buf_reg() leaves in intel_dp->DP for DDI_BUF_CTL (the enable bit is
 * added later by the link-training preparation).  `saved_port_bits` = the port's DDI_BUF_CTL readout masked
 * with DDI_BUF_PORT_REVERSAL, plus the VBT lane-reversal flag (intel_ddi_init).  The three steps are separate calls in the enable
 * sequence; their relative position there is NOT expressed by this list.
 */
int parity_lcd_emit_ddi(const struct parity_lcd_state *s, int port, int pipe, int cpu_transcoder,
	uint32_t saved_port_bits, struct parity_lcd_words *out, uint32_t *ddi_buf_ctl_value);

/* how many plain writes to `reg` the list holds; the last one's value in *value */
unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value);

/*
 * 0, or a negative Linux errno: -22 malformed input""")
save(L + "parity_lcd_calc.h", h)

# ------------------------------------------------------------------ parity_lcd_calc.c
k = load(L + "parity_lcd_calc.c")
k = rep(k, """	out->w[out->n].reg = reg;
	out->w[out->n].value = value;
	out->n++;
}
""", """	out->w[out->n].reg = reg;
	out->w[out->n].value = value;
	out->w[out->n].clear = 0u;
	out->w[out->n].rmw = 0u;
	out->n++;
}

static void record_rmw(void *ctx, u32 reg, u32 clear, u32 set)
{
	struct parity_lcd_words *out = ctx;

	if (out->n >= PARITY_LCD_MAX_REGWRITES) {
		out->overflow++;
		return;
	}
	out->w[out->n].reg = reg;
	out->w[out->n].value = set;
	out->w[out->n].clear = clear;
	out->w[out->n].rmw = 1u;
	out->n++;
}

unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)
{
	unsigned i, hits = 0u;

	for (i = 0u; w != 0 && i < w->n; i++) {
		if (w->w[i].reg == reg && w->w[i].rmw == 0u) {
			hits++;
			if (value != 0)
				*value = w->w[i].value;
		}
	}
	return hits;
}

int parity_display_emit_cpu_transcoder(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp);
int parity_ddi_emit(struct parity_lcd_emit *emit, const struct drm_display_mode *mode, int port, int pipe,
	int cpu_transcoder, int port_clock, int lanes, int pipe_bpp, u32 saved_port_bits, u32 *ddi_buf_ctl_value);

static void state_to_mode(const struct parity_lcd_state *s, struct drm_display_mode *mode, struct intel_link_m_n *m_n)
{
	memset(mode, 0, sizeof(*mode));
	mode->clock = s->mode.clock_khz;
	mode->hdisplay = s->mode.hdisplay; mode->hsync_start = s->mode.hsync_start;
	mode->hsync_end = s->mode.hsync_end; mode->htotal = s->mode.htotal;
	mode->vdisplay = s->mode.vdisplay; mode->vsync_start = s->mode.vsync_start;
	mode->vsync_end = s->mode.vsync_end; mode->vtotal = s->mode.vtotal;
	mode->flags = (s->mode.hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) |
		(s->mode.vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);
	memset(m_n, 0, sizeof(*m_n));
	m_n->tu = s->link.tu;
	m_n->data_m = s->link.data_m; m_n->data_n = s->link.data_n;
	m_n->link_m = s->link.link_m; m_n->link_n = s->link.link_n;
}

int parity_lcd_emit_cpu_transcoder(const struct parity_lcd_state *s, int pipe, int cpu_transcoder,
	struct parity_lcd_words *out)
{
	struct parity_lcd_emit emit;
	struct drm_display_mode mode;
	struct intel_link_m_n m_n;
	int rc;

	if (s == 0 || out == 0 || pipe < 0 || pipe > 3 || cpu_transcoder < 0 || cpu_transcoder > 3)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&emit, 0, sizeof(emit));
	state_to_mode(s, &mode, &m_n);
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	rc = parity_display_emit_cpu_transcoder(&emit, &mode, &m_n, pipe, cpu_transcoder, s->link.rate_khz,
		s->link.lanes, s->link.bpp);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	return rc;
}

int parity_lcd_emit_ddi(const struct parity_lcd_state *s, int port, int pipe, int cpu_transcoder,
	uint32_t saved_port_bits, struct parity_lcd_words *out, uint32_t *ddi_buf_ctl_value)
{
	struct parity_lcd_emit emit;
	struct drm_display_mode mode;
	struct intel_link_m_n m_n;
	u32 buf = 0u;
	int rc;

	/* combo PHY ports only (A, B): a Type-C port needs the TC state this slice does not have */
	if (s == 0 || out == 0 || ddi_buf_ctl_value == 0 || port < 0 || port > 1 || pipe < 0 || pipe > 3 ||
	    cpu_transcoder < 0 || cpu_transcoder > 3)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&emit, 0, sizeof(emit));
	state_to_mode(s, &mode, &m_n);
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	rc = parity_ddi_emit(&emit, &mode, port, pipe, cpu_transcoder, s->link.rate_khz, s->link.lanes, s->link.bpp, saved_port_bits, &buf);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	*ddi_buf_ctl_value = buf;
	return rc;
}
""")
k = rep(k, "	emit.ctx = out;\n	emit.write32 = record_write;\n	rc = parity_display_emit_transcoder(",
           "	memset(&emit, 0, sizeof(emit));\n	emit.ctx = out;\n	emit.write32 = record_write;\n	emit.rmw32 = record_rmw;\n	rc = parity_display_emit_transcoder(")
save(L + "parity_lcd_calc.c", k)

# ------------------------------------------------------------------ display glue: the caller-driven variant
g = load(L + "parity_display_emit_glue.inc")
g = g.rstrip(NL) + NL + """
/*
 * The same state handed to the reference's OWN caller, hsw_configure_cpu_transcoder(): M/N, timings,
 * VRR words, TRANS_MULT, frame start delay, TRANSCONF -- the order between them is the reference's.
 * State as intel_crtc_compute_config / the DP encoder's compute_config leave it for an SST panel:
 * a full modeset (mode_changed), pixel_multiplier 1, framestart_delay 1, VRR not in use (flipline 0),
 * RGB output, no PCH encoder.
 */
int parity_display_emit_cpu_transcoder(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp)
{
	static struct drm_i915_private i915;
	struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hw.adjusted_mode = *mode;
	drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	crtc_state.pixel_multiplier = 1;
	crtc_state.framestart_delay = 1;
	crtc_state.dp_m_n = *m_n;

	hsw_configure_cpu_transcoder(&crtc_state);
	return 0;
}
"""
save(L + "parity_display_emit_glue.inc", g)

# ------------------------------------------------------------------ DDI glue (new file)
open(root + L + "parity_ddi_emit_glue.inc", "w").write("""/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_ddi_port.c.
 *
 * Builds the encoder / digital-port / crtc state the kept intel_ddi.c functions read and calls them:
 *   intel_ddi_init_dp_buf_reg()          -> intel_dp->DP, the DDI_BUF_CTL value (returned, not written)
 *   intel_ddi_set_dp_msa()               -> TRANS_MSA_MISC
 *   intel_ddi_enable_transcoder_func()   -> TRANS_DDI_FUNC_CTL2, TRANS_DDI_FUNC_CTL
 * saved_port_bits is the caller's: intel_ddi_init() takes it from the DDI_BUF_CTL readout masked with
 * DDI_BUF_PORT_REVERSAL (display version >= 11) plus the VBT's lane-reversal flag.
 */
int parity_ddi_emit(struct parity_lcd_emit *emit, const struct drm_display_mode *mode, int port, int pipe,
	int cpu_transcoder, int port_clock, int lanes, int pipe_bpp, u32 saved_port_bits, u32 *ddi_buf_ctl_value)
{
	static struct drm_i915_private i915;
	struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	struct intel_digital_port dig_port;
	struct drm_connector_state conn_state;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&dig_port, 0, sizeof(dig_port));
	memset(&conn_state, 0, sizeof(conn_state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hw.adjusted_mode = *mode;
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	dig_port.base.base.dev = &i915.drm;
	dig_port.base.port = (enum port)port;
	dig_port.saved_port_bits = saved_port_bits;
	conn_state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	intel_ddi_init_dp_buf_reg(&dig_port.base, &crtc_state);
	*ddi_buf_ctl_value = dig_port.dp.DP;
	intel_ddi_set_dp_msa(&crtc_state, &conn_state);
	intel_ddi_enable_transcoder_func(&dig_port.base, &crtc_state);
	return 0;
}
""")
print("wrote", L + "parity_ddi_emit_glue.inc")

# ------------------------------------------------------------------ build lists
P = "src/drivers/gpu/i915/parity/"
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, P + "lcd/intel_display_port.c ", P + "lcd/intel_display_port.c " + P + "lcd/intel_ddi_port.c " + P + "lcd/intel_vrr_port.c ")
save("platform/amd64/vmunix.mk", mk)
sh = load("plan/ws031/tests/run-lcd-host-test.sh")
assert sh.count("intel_display_port.c") >= 1
sh = sh.replace("intel_display_port.c", "intel_display_port.c intel_ddi_port.c intel_vrr_port.c", 1) if sh.count("$L/intel_display_port.c") == 0 else sh.replace("$L/intel_display_port.c", "$L/intel_display_port.c $L/intel_ddi_port.c $L/intel_vrr_port.c", 1)
save("plan/ws031/tests/run-lcd-host-test.sh", sh)
cg = load("plan/ws031/handover/tools/check_generated.sh")
cg = rep(cg, "intel_display_port.c drm_dp_bw_port.c drm_modes_port.c lcd_trans_regs.h lcd_ref_types.h port_lcd_calc.manifest.json",
             "intel_display_port.c drm_dp_bw_port.c drm_modes_port.c lcd_trans_regs.h lcd_ref_types.h port_lcd_calc.manifest.json \\\n\tintel_ddi_port.c intel_vrr_port.c lcd_ddi_types.h lcd_ref_inlines.h lcd_ddi_regs.h lcd_dp_msa.h lcd_drm_colorspace.h")
save("plan/ws031/handover/tools/check_generated.sh", cg)
print("done")
