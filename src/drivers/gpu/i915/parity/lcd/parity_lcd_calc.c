/*
 * WS031 Linux-parity — LCD-A, first slice (see parity_lcd_calc.h).  zedBSD project code.
 *
 * What is the reference's arithmetic (generated files) and what is decided here:
 *   mode            drm_mode_detailed() on the base block's first detailed timing
 *                   (the preferred timing of an EDID 1.3+/1.4 panel)
 *   bpp             intel_dp_max_bpp()'s rule for eDP: the sink's depth, capped by the VBT's
 *   rate / lanes    the reference trains an eDP sink older than eDP 1.4 at its maximum
 *                   (intel_dp->use_max_params); the general search of
 *                   intel_dp_compute_link_config() is NOT ported -- only that rule is applied,
 *                   and the result is checked with the reference's bandwidth functions
 *   M/N             intel_link_compute_m_n() with the SST non-FEC overhead (1000000 ppm)
 *   PLL             icl_calc_dp_combo_pll() + icl_calc_dpll_state()
 */
#include "lcd_compat.h"
#include "parity_lcd_calc.h"

int parity_edid_preferred_mode(const u8 *edid128, struct drm_display_mode *mode, unsigned *index);
int parity_icl_dp_combo_pll(int port_clock, int ref_nssc, u32 *cfgcr0, u32 *cfgcr1, u32 *div0);
int parity_display_emit_transcoder(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int src_w, int src_h);

static unsigned lcd_notes;

static unsigned lcd_errors;
static void (*lcd_error_hook)(void *ctx, const char *what);
static void *lcd_error_ctx;

void parity_lcd_error(const char *what)
{
	lcd_errors++;
	if (lcd_error_hook != 0)
		lcd_error_hook(lcd_error_ctx, what);
}

unsigned parity_lcd_errors(void)
{
	return lcd_errors;
}

void parity_lcd_error_bind(void (*hook)(void *ctx, const char *what), void *ctx)
{
	lcd_error_hook = hook;
	lcd_error_ctx = ctx;
	lcd_errors = 0u;
}

void parity_lcd_note(const char *fmt)
{
	(void)fmt;
	lcd_notes++;
}

/* drm_dp_bw_code_to_link_rate() for the 8b/10b codes: the code times 0.27 Gbps, in 10 kbit/s units */
static int rate_from_bw_code(u8 code)
{
	switch (code) {
	case 0x06: return 162000;
	case 0x0a: return 270000;
	case 0x14: return 540000;
	case 0x1e: return 810000;
	default:   return 0;
	}
}

int parity_lcd_compute(const uint8_t *edid128, const uint8_t *dpcd, const uint8_t *edp_dpcd,
	int vbt_bpp, int ref_nssc_khz, struct parity_lcd_state *out)
{
	struct drm_display_mode mode;
	struct intel_link_m_n m_n;
	const struct edid *edid = (const struct edid *)edid128;
	unsigned index = 0;
	int bpc, rc;

	if (edid128 == 0 || dpcd == 0 || edp_dpcd == 0 || out == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	lcd_notes = 0;

	/* ---- mode ---- */
	rc = parity_edid_preferred_mode(edid128, &mode, &index);
	if (rc != 0)
		return rc;
	out->mode.clock_khz = mode.clock;
	out->mode.hdisplay = mode.hdisplay; out->mode.hsync_start = mode.hsync_start;
	out->mode.hsync_end = mode.hsync_end; out->mode.htotal = mode.htotal;
	out->mode.vdisplay = mode.vdisplay; out->mode.vsync_start = mode.vsync_start;
	out->mode.vsync_end = mode.vsync_end; out->mode.vtotal = mode.vtotal;
	out->mode.hsync_positive = (mode.flags & DRM_MODE_FLAG_PHSYNC) != 0;
	out->mode.vsync_positive = (mode.flags & DRM_MODE_FLAG_PVSYNC) != 0;
	out->mode.width_mm = mode.width_mm; out->mode.height_mm = mode.height_mm;
	out->mode.descriptor_index = index;

	/* EDID 1.4 digital input: colour depth in bits 6:4 of the video input byte (1 = 6 bpc ... 6 = 16 bpc) */
	bpc = 0;
	if (edid->version == 1 && edid->revision >= 4 && (edid->input & DRM_EDID_INPUT_DIGITAL) != 0) {
		unsigned depth = (edid->input >> 4) & 7u;

		if (depth >= 1u && depth <= 6u)
			bpc = 4 + 2 * (int)depth;
	}
	out->mode.edid_bpc = bpc;

	/* ---- bpp: the sink's depth (24 when undefined), capped by the VBT's eDP colour depth ---- */
	out->link.bpp = bpc != 0 ? 3 * bpc : 24;
	if (vbt_bpp != 0 && vbt_bpp < out->link.bpp)
		out->link.bpp = vbt_bpp;

	/* ---- link: capability, then the use_max_params rule ---- */
	out->link.sink_max_rate_khz = rate_from_bw_code(dpcd[1]);
	out->link.sink_max_lanes = dpcd[2] & 0x1f;
	if (out->link.sink_max_rate_khz == 0 || (out->link.sink_max_lanes != 1 && out->link.sink_max_lanes != 2 &&
	    out->link.sink_max_lanes != 4))
		return -EINVAL;
	out->link.use_max_params = edp_dpcd[0] < 0x03;          /* DP_EDP_14 */
	if (!out->link.use_max_params)
		return -EINVAL;                                  /* eDP 1.4+ rate tables: not ported */
	out->link.rate_khz = out->link.sink_max_rate_khz;       /* the ADL-P combo PHY source limit (810000) is higher */
	out->link.lanes = out->link.sink_max_lanes;

	out->link.required_kbps = intel_dp_link_required(mode.clock, out->link.bpp);
	out->link.available_kbps = intel_dp_max_data_rate(out->link.rate_khz, out->link.lanes);
	if (out->link.required_kbps > out->link.available_kbps)
		return -28;

	/* ---- M/N ---- */
	intel_link_compute_m_n((u16)(out->link.bpp * 16), out->link.lanes, mode.clock, out->link.rate_khz,
		1000000, &m_n);
	out->link.tu = m_n.tu;
	out->link.data_m = m_n.data_m; out->link.data_n = m_n.data_n;
	out->link.link_m = m_n.link_m; out->link.link_n = m_n.link_n;

	/* ---- combo PLL ---- */
	out->pll.ref_khz = ref_nssc_khz;
	rc = parity_icl_dp_combo_pll(out->link.rate_khz, ref_nssc_khz, &out->pll.cfgcr0, &out->pll.cfgcr1,
		&out->pll.div0);
	out->notes = lcd_notes;
	return rc;
}

static void record_write(void *ctx, u32 reg, u32 value)
{
	struct parity_lcd_words *out = ctx;

	if (out->n >= PARITY_LCD_MAX_REGWRITES) {
		out->overflow++;
		return;
	}
	out->w[out->n].reg = reg;
	out->w[out->n].value = value;
	out->w[out->n].clear = 0u;
	out->w[out->n].rmw = 0u;
	out->w[out->n].step = 0;
	out->n++;
}

static u32 record_rmw(void *ctx, u32 reg, u32 clear, u32 set)
{
	struct parity_lcd_words *out = ctx;

	if (out->n >= PARITY_LCD_MAX_REGWRITES) {
		out->overflow++;
		return 0u;
	}
	out->w[out->n].reg = reg;
	out->w[out->n].value = set;
	out->w[out->n].clear = clear;
	out->w[out->n].rmw = 1u;
	out->w[out->n].step = 0;
	out->n++;
	return 0u;
}

static void record_step(void *ctx, const char *name)
{
	struct parity_lcd_words *out = ctx;

	if (out->n >= PARITY_LCD_MAX_REGWRITES) {
		out->overflow++;
		return;
	}
	memset(&out->w[out->n], 0, sizeof(out->w[out->n]));
	out->w[out->n].step = name;
	out->n++;
}

int parity_plane_emit(struct parity_lcd_emit *emit, int pipe, int plane_id, u32 fourcc, u64 modifier,
	u32 width, u32 height, u32 pitch, u32 surf_ggtt_offset);

int parity_lcd_emit_plane(int pipe, int plane_id, uint32_t fourcc, uint64_t modifier, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t surf_ggtt_offset, struct parity_lcd_words *out)
{
	struct parity_lcd_emit emit;
	int rc;

	if (out == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&emit, 0, sizeof(emit));
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	emit.step = record_step;
	rc = parity_plane_emit(&emit, pipe, plane_id, fourcc, modifier, width, height, pitch, surf_ggtt_offset);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	return rc;
}

int parity_lcd_words_step(const struct parity_lcd_words *w, const char *name, unsigned from)
{
	unsigned i;

	for (i = from; w != 0 && name != 0 && i < w->n; i++)
		if (w->w[i].step != 0 && strcmp(w->w[i].step, name) == 0)
			return (int)i;
	return -1;
}

unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)
{
	unsigned i, hits = 0u;

	for (i = 0u; w != 0 && i < w->n; i++) {
		if (w->w[i].reg == reg && w->w[i].rmw == 0u && w->w[i].step == 0) {
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
	emit.step = record_step;
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
	emit.step = record_step;
	rc = parity_ddi_emit(&emit, &mode, port, pipe, cpu_transcoder, s->link.rate_khz, s->link.lanes, s->link.bpp, saved_port_bits, &buf);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	*ddi_buf_ctl_value = buf;
	return rc;
}

int parity_lcd_emit_transcoder(const struct parity_lcd_state *s, int pipe, int cpu_transcoder,
	uint32_t src_width, uint32_t src_height, struct parity_lcd_words *out)
{
	struct parity_lcd_emit emit;
	struct drm_display_mode mode;
	struct intel_link_m_n m_n;
	int rc;

	if (s == 0 || out == 0 || pipe < 0 || pipe > 3 || cpu_transcoder < 0 || cpu_transcoder > 3 ||
	    src_width == 0u || src_height == 0u)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&mode, 0, sizeof(mode));
	mode.clock = s->mode.clock_khz;
	mode.hdisplay = s->mode.hdisplay; mode.hsync_start = s->mode.hsync_start;
	mode.hsync_end = s->mode.hsync_end; mode.htotal = s->mode.htotal;
	mode.vdisplay = s->mode.vdisplay; mode.vsync_start = s->mode.vsync_start;
	mode.vsync_end = s->mode.vsync_end; mode.vtotal = s->mode.vtotal;
	m_n.tu = s->link.tu;
	m_n.data_m = s->link.data_m; m_n.data_n = s->link.data_n;
	m_n.link_m = s->link.link_m; m_n.link_n = s->link.link_n;
	memset(&emit, 0, sizeof(emit));
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	emit.step = record_step;
	rc = parity_display_emit_transcoder(&emit, &mode, &m_n, pipe, cpu_transcoder, (int)src_width, (int)src_height);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	return rc;
}
