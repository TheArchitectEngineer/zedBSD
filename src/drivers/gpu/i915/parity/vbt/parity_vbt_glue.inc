/*
 * WS031 Linux-parity — zedBSD glue for the generated VBT parser.  zedBSD project
 * code; it is included at the end of intel_bios_port.c because the reference
 * keeps struct intel_bios_encoder_data and its helpers private to that file.
 *
 * Owns: the single drm_i915_private stand-in, the arena every reference
 * allocation comes from, the byte source handed to intel_bios_init(), and the
 * flattening of the reference lists into struct parity_vbt.
 */
#include "parity_vbt.h"

#define PARITY_VBT_ARENA_BYTES (48u * 1024u)

static struct drm_i915_private vbt_i915;
static uint8_t vbt_arena[PARITY_VBT_ARENA_BYTES];
static unsigned vbt_arena_used, vbt_arena_peak, vbt_alloc_failures;
static const void *vbt_provider_bytes;
static struct parity_vbt *vbt_live;
static int vbt_log_level;
static unsigned vbt_log_errors;

void *
parity_vbt_zalloc(size_t bytes)
{
	unsigned need = (unsigned)((bytes + 15u) & ~(size_t)15u);
	void *p;

	if (bytes == 0u || need > PARITY_VBT_ARENA_BYTES - vbt_arena_used) {
		vbt_alloc_failures++;
		return 0;
	}
	p = &vbt_arena[vbt_arena_used];
	vbt_arena_used += need;
	if (vbt_arena_used > vbt_arena_peak)
		vbt_arena_peak = vbt_arena_used;
	memset(p, 0, need);
	return p;
}

void
parity_vbt_free(void *p)
{
	/* A bump arena: individual frees are no-ops, parity_vbt_fini() releases everything. */
	(void)p;
}

const void *
parity_vbt_provider_get(struct drm_i915_private *i915)
{
	(void)i915;
	return vbt_provider_bytes;
}

void
parity_vbt_set_log_level(int level)
{
	vbt_log_level = level;
}

void
parity_vbt_note(int level, const char *fmt)
{
	if (level == PARITY_VBT_LOG_ERR)
		vbt_log_errors++;
	if (level <= vbt_log_level)
		parity_vbt_emit(fmt);
}

int
parity_vbt_log_enabled(int level)
{
	if (level == PARITY_VBT_LOG_ERR)
		vbt_log_errors++;
	return level <= vbt_log_level;
}

void
drm_mode_set_name(struct drm_display_mode *mode)
{
	/* "<hdisplay>x<vdisplay>" without a formatted-print dependency */
	unsigned v[2] = { mode->hdisplay, mode->vdisplay };
	unsigned i, n = 0u;

	for (i = 0u; i < 2u; i++) {
		char tmp[8];
		unsigned k = 0u, x = v[i];

		do { tmp[k++] = (char)('0' + x % 10u); x /= 10u; } while (x != 0u && k < sizeof(tmp));
		while (k > 0u && n + 1u < sizeof(mode->name))
			mode->name[n++] = tmp[--k];
		if (i == 0u && n + 1u < sizeof(mode->name))
			mode->name[n++] = 'x';
	}
	mode->name[n] = '\0';
}

int
parity_vbt_validate(const void *bytes, size_t size)
{
	return intel_bios_is_valid_vbt(bytes, size) ? 1 : 0;
}

int
parity_vbt_init(struct parity_vbt *v, const void *bytes, size_t size, int origin)
{
	struct intel_bios_encoder_data *devdata;
	struct bdb_block_entry *entry;
	unsigned i;

	if (v == 0)
		return -22;
	if (vbt_live != 0)
		return -16;
	memset(v, 0, sizeof(*v));
	memset(&vbt_i915, 0, sizeof(vbt_i915));
	vbt_arena_used = 0u;
	vbt_arena_peak = 0u;
	vbt_alloc_failures = 0u;
	vbt_log_errors = 0u;

	if (bytes != 0 && size != 0u) {
		const struct vbt_header *h = bytes;

		v->bytes = bytes;
		v->size = size;
		v->origin = origin;
		for (i = 0u; i < sizeof(h->signature) && i + 1u < sizeof(v->signature); i++)
			v->signature[i] = (char)h->signature[i];
	} else {
		bytes = 0;
		v->origin = PARITY_VBT_ORIGIN_NONE;
	}
	vbt_provider_bytes = bytes;
	vbt_live = v;

	intel_bios_init(&vbt_i915);

	v->bdb_version = vbt_i915.display.vbt.version;
	v->missing_defaults_used = bytes == 0;
	v->int_lvds_support = (int)vbt_i915.display.vbt.int_lvds_support;
	v->display_clock_mode = (int)vbt_i915.display.vbt.display_clock_mode;
	v->lvds_use_ssc = (int)vbt_i915.display.vbt.lvds_use_ssc;
	v->lvds_ssc_freq = vbt_i915.display.vbt.lvds_ssc_freq;
	v->crt_ddc_pin = vbt_i915.display.vbt.crt_ddc_pin;
	list_for_each_entry(entry, &vbt_i915.display.vbt.bdb_blocks, node)
		v->num_bdb_blocks++;

	list_for_each_entry(devdata, &vbt_i915.display.vbt.display_devices, node) {
		struct parity_vbt_encoder *e;

		if (v->n_encoders >= PARITY_VBT_MAX_ENCODERS)
			break;
		e = &v->enc[v->n_encoders++];
		e->port = (int)intel_bios_encoder_port(devdata);
		e->aux_ch = (int)intel_bios_dp_aux_ch(devdata);
		e->device_type = devdata->child.device_type;
		e->dvo_port = devdata->child.dvo_port;
		e->ddc_pin_raw = devdata->child.ddc_pin;
		e->ddc_pin = intel_bios_hdmi_ddc_pin(devdata);
		e->supports_dp = intel_bios_encoder_supports_dp(devdata);
		e->supports_edp = intel_bios_encoder_supports_edp(devdata);
		e->supports_hdmi = intel_bios_encoder_supports_hdmi(devdata);
		e->supports_dvi = intel_bios_encoder_supports_dvi(devdata);
		e->supports_typec_usb = intel_bios_encoder_supports_typec_usb(devdata);
		e->supports_tbt = intel_bios_encoder_supports_tbt(devdata);
		e->lane_reversal = intel_bios_encoder_lane_reversal(devdata);
		e->hpd_invert = intel_bios_encoder_hpd_invert(devdata);
		e->dp_max_lane_count = intel_bios_dp_max_lane_count(devdata);
		e->dp_max_link_rate = intel_bios_dp_max_link_rate(devdata);
		e->dp_boost_level = intel_bios_dp_boost_level(devdata);
		e->hdmi_boost_level = intel_bios_hdmi_boost_level(devdata);
		e->hdmi_level_shift = intel_bios_hdmi_level_shift(devdata);
	}

	v->arena_used = vbt_arena_used;
	v->arena_peak = vbt_arena_peak;
	v->alloc_failures = vbt_alloc_failures;
	v->log_errors = vbt_log_errors;
	v->inited = 1;
	return 0;
}

const struct parity_vbt_encoder *
parity_vbt_encoder_for_port(const struct parity_vbt *v, int port)
{
	unsigned i;

	if (v == 0 || !v->inited)
		return 0;
	for (i = 0u; i < v->n_encoders; i++)
		if (v->enc[i].port == port)
			return &v->enc[i];
	return 0;
}

int
parity_vbt_init_panel(struct parity_vbt *v, int port, const uint8_t *edid128,
	struct parity_vbt_panel *out)
{
	const struct intel_bios_encoder_data *devdata;
	static struct intel_panel panel;      /* the mode it may allocate lives in the arena */
	struct drm_edid drm_edid;
	const struct drm_display_mode *m;

	if (v == 0 || out == 0 || v != vbt_live || !v->inited)
		return -22;
	memset(out, 0, sizeof(*out));
	devdata = intel_bios_encoder_data_lookup(&vbt_i915, (enum port)port);
	if (devdata == 0)
		return -19;

	memset(&panel, 0, sizeof(panel));
	panel.vbt.panel_type = -1;            /* intel_panel_init_alloc(): "not known yet" */
	drm_edid.edid = (const struct edid *)edid128;

	/* intel_edp_init_connector(): early (no EDID), then late with the EDID read over AUX. */
	intel_bios_init_panel_early(&vbt_i915, &panel, devdata);
	if (panel.vbt.panel_type < 0)
		intel_bios_init_panel_late(&vbt_i915, &panel, devdata, edid128 != 0 ? &drm_edid : 0);
	if (panel.vbt.panel_type < 0)
		return -61;

	out->initialized = 1;
	out->panel_type = panel.vbt.panel_type;
	out->bpp = panel.vbt.edp.bpp;
	out->edp_rate = panel.vbt.edp.rate;
	out->edp_lanes = panel.vbt.edp.lanes;
	out->edp_preemphasis = panel.vbt.edp.preemphasis;
	out->edp_vswing = panel.vbt.edp.vswing;
	out->edp_low_vswing = panel.vbt.edp.low_vswing;
	out->edp_max_link_rate = panel.vbt.edp.max_link_rate;
	out->t1_t3 = panel.vbt.edp.pps.t1_t3;
	out->t8 = panel.vbt.edp.pps.t8;
	out->t9 = panel.vbt.edp.pps.t9;
	out->t10 = panel.vbt.edp.pps.t10;
	out->t11_t12 = panel.vbt.edp.pps.t11_t12;
	out->bl_present = panel.vbt.backlight.present;
	out->bl_type = (int)panel.vbt.backlight.type;
	out->bl_controller = panel.vbt.backlight.controller;
	out->bl_active_low_pwm = panel.vbt.backlight.active_low_pwm;
	out->bl_pwm_freq_hz = panel.vbt.backlight.pwm_freq_hz;
	out->bl_min_brightness = panel.vbt.backlight.min_brightness;
	out->drrs_type = (int)panel.vbt.drrs_type;
	out->vrr = panel.vbt.vrr;
	m = panel.vbt.lfp_lvds_vbt_mode;
	if (m != 0) {
		out->has_lfp_mode = 1;
		out->mode_clock_khz = m->clock;
		out->hdisplay = m->hdisplay; out->hsync_start = m->hsync_start;
		out->hsync_end = m->hsync_end; out->htotal = m->htotal;
		out->vdisplay = m->vdisplay; out->vsync_start = m->vsync_start;
		out->vsync_end = m->vsync_end; out->vtotal = m->vtotal;
	}
	v->arena_used = vbt_arena_used;
	v->arena_peak = vbt_arena_peak;
	v->alloc_failures = vbt_alloc_failures;
	v->log_errors = vbt_log_errors;
	return 0;
}

void
parity_vbt_fini(struct parity_vbt *v)
{
	if (v == 0 || v != vbt_live)
		return;
	intel_bios_driver_remove(&vbt_i915);
	vbt_provider_bytes = 0;
	vbt_arena_used = 0u;
	vbt_live = 0;
	v->inited = 0;
	v->n_encoders = 0u;
	v->bytes = 0;
}
