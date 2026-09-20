/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_modeset_setup_port.c (E-124, N1: the display the
 * FIRMWARE left running).  The reference functions above run unchanged; this glue provides
 *
 *   - the device's objects the readout walks: one crtc + one primary plane per pipe, and the encoder and
 *     connector of the screen the DDI callers are bound to (the modeset object's own);
 *   - a device view built from the same configuration the modeset object is built from;
 *   - the power accessors of a READOUT: take a reference only where the well is already on;
 *   - the two entry points of the run: the readout (+ sanitize) and the takeover.
 *
 * Nothing here writes a display register on its own: every write comes from the reference text above.
 */
#include "parity_lcd_ops.h"
#include "parity_lcd_modeset.h"
#include "parity_n1.h"

/* E-124 diagnostics: with the trace on, every walk and every power question of the readout is printed --
 * on a machine whose only console is the screen that is how a readout is followed */
/* the log of this path (kern/klog.h); declared, not included, so the host tests can compile this unit too */
void kern_logf(const char *fmt, ...);
extern int parity_lcd_reg_trace;
#define N1_TRACE(...) do { if (parity_lcd_reg_trace) kern_logf(__VA_ARGS__); } while (0)

#define N1_PIPES 4

static struct {
	struct drm_i915_private i915;
	struct intel_crtc crtc[N1_PIPES];
	struct intel_crtc_state crtc_state[N1_PIPES];
	struct intel_plane plane[N1_PIPES];
	struct intel_plane_state plane_state[N1_PIPES];
	struct intel_connector *connector;
	struct intel_encoder *encoder;
	struct drm_connector_state conn_state;
	struct intel_atomic_state state;
	/* the device-wide states the takeover clears */
	struct intel_bw_state bw;
	struct intel_cdclk_state cdclk;
	struct intel_pmdemand_state pmdemand;
	struct intel_dbuf_state dbuf;
	/* the device vblank objects: the readout of an ACTIVE crtc writes the timestamping constants into
		  * dev->vblank[pipe] (drm_calc_timestamping_constants) */
	struct drm_vblank_crtc vblank[N1_PIPES];
	int live;
	unsigned takeovers, still_active;
} n1;

/* ---------------- the objects the readout walks ---------------- */

struct intel_crtc *parity_n1_crtc_at(unsigned idx)
{
	N1_TRACE("i915: parity N1 walk crtc %u (plane0 hook 0x%llx)\n", idx,
		(unsigned long long)(uintptr_t)n1.plane[0].get_hw_state);
	if (!n1.live || idx >= (unsigned)N1_PIPES)
		return 0;
	return &n1.crtc[idx];
}

struct intel_crtc *parity_n1_crtc_for_pipe(enum pipe pipe)
{
	if (!n1.live || (unsigned)pipe >= (unsigned)N1_PIPES)
		return 0;
	return &n1.crtc[pipe];
}

struct intel_plane *parity_n1_plane_at(unsigned idx)
{
	N1_TRACE("i915: parity N1 walk plane %u | &n1=0x%llx &plane0=0x%llx hook=0x%llx live=%d\n", idx,
		(unsigned long long)(uintptr_t)&n1, (unsigned long long)(uintptr_t)&n1.plane[0],
		(unsigned long long)(uintptr_t)n1.plane[0].get_hw_state, n1.live);
	if (!n1.live || idx >= (unsigned)N1_PIPES)
		return 0;
	return &n1.plane[idx];
}

struct drm_plane *parity_n1_primary_plane(void)
{
	return n1.live ? n1.crtc[0].base.primary : 0;
}

struct intel_encoder *parity_n1_encoder_at(unsigned idx)
{
	N1_TRACE("i915: parity N1 walk encoder %u\n", idx);
	if (!n1.live || idx != 0u)
		return 0;
	return n1.encoder;
}

struct intel_connector *parity_n1_connector_at(unsigned idx)
{
	N1_TRACE("i915: parity N1 walk connector %u\n", idx);
	if (!n1.live || idx != 0u)
		return 0;
	return n1.connector;
}

/*
 * The state of a named crtc.  Outside a readout the registry is empty and the answer is the state the
 * watermark path is working on -- the meaning lcd_wm_compat.h gave this accessor before N1 existed.
 */
struct intel_crtc_state *parity_n1_crtc_state(const struct intel_crtc *crtc)
{
	unsigned i;

	if (n1.live)
		for (i = 0u; i < (unsigned)N1_PIPES; i++)
			if (&n1.crtc[i] == crtc)
				return &n1.crtc_state[i];
	return parity_lcd_wm != 0 ? parity_lcd_wm->crtc_state : 0;
}

/* the plane readout hook, with the trace around it: the call itself is what a console can see */
static bool n1_plane_get_hw_state(struct intel_plane *plane, enum pipe *pipe)
{
	bool (*hook)(struct intel_plane *, enum pipe *);

	N1_TRACE("i915: parity N1 plane hook entered\n");
	hook = parity_lcd_plane_get_hw_state();
	bool ret;

	N1_TRACE("i915: parity N1 plane %d hook %p\n", (int)plane->pipe, (void *)hook);
	ret = hook(plane, pipe);
	N1_TRACE("i915: parity N1 plane %d visible=%d\n", (int)plane->pipe, (int)ret);
	return ret;
}

/* ---------------- the power accessors of a readout ---------------- */

intel_wakeref_t parity_n1_power_get_if_enabled(enum intel_display_power_domain domain)
{
	struct drm_i915_private *i915 = parity_lcd_cur_i915;

	N1_TRACE("i915: parity N1 power domain %d: is it already on?\n", (int)domain);

	if (i915 == 0 || i915->emit == 0)
		return 0;
	if (i915->emit->power_get_if_enabled != 0)
		return i915->emit->power_get_if_enabled(i915->emit->ctx, (int)domain);
	/* a model backend keeps no well state: its wells answer as the reference's "already on" */
	return i915->emit->power_get != 0 ? i915->emit->power_get(i915->emit->ctx, (int)domain) : 1;
}

void parity_n1_power_get_in_set_if_enabled(struct intel_display_power_domain_set *set,
	enum intel_display_power_domain domain)
{
	if (set == 0)
		return;
	if (parity_n1_power_get_if_enabled(domain) != 0)
		set_bit((unsigned)domain, set->mask.bits);
}

void parity_n1_power_put_all_in_set(struct intel_display_power_domain_set *set)
{
	struct drm_i915_private *i915 = parity_lcd_cur_i915;

	if (i915 == 0 || set == 0)
		return;
	intel_display_power_put_mask_in_set(i915, set, &set->mask);
}

/*
 * The reference intel_atomic_get_crtc_state() adds the crtc to the atomic state; the disable that follows
 * reads it back as the OLD state (the takeover disables exactly what the readout found).  Outside a readout
 * this only answers with the state, as lcd_wm_compat.h did before N1 existed.
 */
struct intel_crtc_state *parity_n1_atomic_crtc_state(void *state, struct intel_crtc *crtc)
{
	/* the callers hand either the state or its base, which is its first member: the same address */
	struct intel_atomic_state *st = state;
	struct intel_crtc_state *cs = parity_n1_crtc_state(crtc);

	if (n1.live && st != 0) {
		st->old_crtc_state = cs;
		st->crtc_state = 0;             /* a disable has no new state, as this path builds one elsewhere */
	}
	return cs;
}

/* ---------------- the throw-away atomic state of the takeover ---------------- */

struct drm_atomic_state *parity_n1_atomic_state(struct drm_device *dev)
{
	n1.state.base.dev = dev;
	n1.state.crtc_state = 0;
	n1.state.old_crtc_state = 0;
	n1.state.acquire_ctx = 0;
	n1.state.internal = false;
	return &n1.state;
}

/* ---------------- the device view and the objects ---------------- */

/*
 * The device the readout runs against.  The fields are the ones the modeset object is given from the same
 * configuration (parity_lcd_modeset_prepare): the readout reads the hardware, but it needs to know the
 * platform's DBUF geometry, the watermark latencies, the clocks and the PLL pool to interpret what it reads.
 */
static void n1_build_device(const struct parity_lcd_modeset_cfg *cfg, struct parity_lcd_emit *ops)
{
	struct drm_i915_private *i915 = &n1.i915;
	unsigned i;

	memset(&n1, 0, sizeof(n1));
	i915->emit = ops;
	i915->display.dpll.lock.which = PARITY_LCD_LOCK_DPLL;
	i915->display.backlight.lock.which = PARITY_LCD_LOCK_BACKLIGHT;
	i915->display.vbt.override_afc_startup = cfg->vbt_override_afc_startup != 0;
	i915->display.dmc.fw_mask = cfg->dmc_fw_mask;
	memcpy(i915->display.wm.skl_latency, cfg->wm_latency, sizeof(i915->display.wm.skl_latency));
	i915->display.wm.num_levels = cfg->wm_num_levels;
	i915->display.wm.ipc_enabled = cfg->wm_ipc_enabled != 0;
	i915->display.sagv.block_time_us = cfg->sagv_block_time_us;
	i915->display.device_info.dbuf.size = cfg->dbuf_size;
	i915->display.device_info.dbuf.slice_mask = cfg->dbuf_slice_mask;
	i915->display.device_info.has_ddi = true;
	i915->display.runtime.pipe_mask = 0x0f;
	i915->display.runtime.rawclk_freq = cfg->rawclk_khz;
	i915->display.cdclk.hw.cdclk = cfg->cdclk_khz;
	i915->display.cdclk.hw.vco = cfg->cdclk_vco_khz;
	i915->display.cdclk.hw.ref = cfg->cdclk_ref_khz;
	i915->display.cdclk.hw.bypass = cfg->cdclk_bypass_khz;
	i915->display.cdclk.hw.voltage_level = cfg->cdclk_voltage_level;
	i915->display.cdclk.max_cdclk_freq = cfg->cdclk_max_khz;
	i915->display.funcs.display = parity_lcd_ms_display_funcs();
	i915->display.funcs.color = parity_lcd_ms_color_funcs();
	i915->drm.vblank = n1.vblank;
	i915->display.dbuf.enabled_slices = cfg->dbuf_enabled_slices;
	/*
	 * The global states the readout fills and the takeover clears.  In the reference each is the payload of
	 * a global state object; here the object IS the payload, so the pointer is the address of the payload.
	 */
	i915->display.dbuf.obj.state = (struct intel_global_state *)(void *)&n1.dbuf;
	i915->display.bw.obj.state = (struct intel_global_state *)(void *)&n1.bw;
	i915->display.cdclk.obj.state = (struct intel_global_state *)(void *)&n1.cdclk;
	i915->display.pmdemand.obj.state = (struct intel_global_state *)(void *)&n1.pmdemand;
	/* the device's PLL pool: the same objects every screen sees (intel_get_shared_dpll_by_id) */
	parity_lcd_dpll_pool_bind(i915);

	for (i = 0u; i < (unsigned)N1_PIPES; i++) {
		n1.crtc[i].base.dev = &i915->drm;
		n1.crtc[i].base.name = "pipe";
		n1.crtc[i].base.base.id = (int)i;
		n1.crtc[i].pipe = (enum pipe)i;
		n1.crtc[i].base.state = &n1.crtc_state[i].uapi;
		n1.crtc[i].base.funcs = parity_lcd_ms_crtc_funcs();   /* the hardware frame counter */
		n1.vblank[i].max_vblank_count = 0xffffffffu;
		n1.crtc_state[i].uapi.crtc = &n1.crtc[i].base;
		n1.plane[i].base.dev = &i915->drm;
		n1.plane[i].base.name = "primary";
		n1.plane[i].base.base.id = 16 + (int)i;
		n1.plane[i].base.type = DRM_PLANE_TYPE_PRIMARY;
		n1.plane[i].id = PLANE_PRIMARY;
		n1.plane[i].pipe = (enum pipe)i;
		n1.plane[i].get_hw_state = n1_plane_get_hw_state;
		n1.plane[i].base.state = (struct drm_plane_state *)(void *)&n1.plane_state[i];
		n1.plane_state[i].uapi.plane = &n1.plane[i].base;
		n1.crtc[i].base.primary = &n1.plane[i].base;
	}
	kern_logf("i915: parity N1 registry: %u built | &n1=0x%llx &plane0=0x%llx hook=0x%llx\n",
		(unsigned)N1_PIPES, (unsigned long long)(uintptr_t)&n1,
		(unsigned long long)(uintptr_t)&n1.plane[0], (unsigned long long)(uintptr_t)n1.plane[0].get_hw_state);
}

/* ---------------- the entry points of the run ---------------- */

static void n1_fill_report(struct parity_n1_report *out)
{
	unsigned i;

	memset(out, 0, sizeof(*out));
	out->pipe = -1;
	out->cpu_transcoder = -1;
	out->dpll_id = -1;
	out->port = -1;
	for (i = 0u; i < (unsigned)N1_PIPES; i++) {
		const struct intel_crtc_state *cs = &n1.crtc_state[i];

		if (!cs->hw.active)
			continue;
		out->active_pipes |= 1u << i;
		if (out->pipe >= 0)
			continue;
		out->pipe = (int)i;
		out->cpu_transcoder = cs->cpu_transcoder;
		out->dpll_id = cs->shared_dpll != 0 ? (int)cs->shared_dpll->info->id : -1;
		out->mode_h = (unsigned)cs->hw.adjusted_mode.hdisplay;
		out->mode_v = (unsigned)cs->hw.adjusted_mode.vdisplay;
		out->clock_khz = (unsigned)cs->hw.adjusted_mode.clock;
		out->port_clock_khz = (unsigned)cs->port_clock;
		out->pipe_bpp = cs->pipe_bpp;
		out->output_types = cs->output_types;
		out->plane_visible = n1.plane_state[i].uapi.visible ? 1 : 0;
		out->active_planes = cs->active_planes;
	}
	if (n1.encoder != 0)
		out->port = (int)n1.encoder->port;
	out->encoder_on_crtc = (n1.encoder != 0 && n1.encoder->base.crtc != 0) ? 1 : 0;
	out->connector_dpms = n1.connector != 0 ? n1.connector->base.dpms : -1;
	out->takeovers = n1.takeovers;
	out->still_active = n1.still_active;
	out->live = n1.live;
}

/*
 * The readout of the reference: intel_modeset_setup_hw_state() -- what the hardware says, then the sanitize
 * the reference runs on it.  Nothing of the firmware's picture is disturbed here; the takeover is separate.
 */
int parity_n1_readout(const struct parity_lcd_modeset_cfg *cfg, struct parity_lcd_emit *ops,
	struct parity_n1_report *out)
{
	struct drm_i915_private *saved;

	if (cfg == 0 || ops == 0 || out == 0)
		return -EINVAL;
	if (n1.live)
		return -EBUSY;
	n1_build_device(cfg, ops);
	n1.encoder = parity_lcd_ms_bound_encoder();
	n1.connector = parity_lcd_ms_bound_connector();
	if (n1.encoder == 0 || n1.connector == 0)
		return -ENXIO;          /* no screen is bound: the readout has no encoder to ask */
	n1.connector->base.dev = &n1.i915.drm;
	n1.connector->base.state = &n1.conn_state;
	n1.connector->base.index = 0u;
	n1.connector->base.connector_type = DRM_MODE_CONNECTOR_eDP;
	n1.connector->encoder = n1.encoder;
	n1.connector->get_hw_state = intel_ddi_connector_get_hw_state;
	n1.encoder->base.dev = &n1.i915.drm;
	n1.encoder->base.crtc = 0;
	parity_lcd_ms_bind_readout(n1.encoder);   /* get_hw_state / get_config / sync_state / get_power_domains */
	n1.live = 1;

	saved = parity_lcd_cur_i915;
	parity_lcd_cur_i915 = &n1.i915;        /* the readout's device is the current one while it runs */
	intel_modeset_setup_hw_state(&n1.i915, 0);
	parity_lcd_cur_i915 = saved;

	n1_fill_report(out);
	return 0;
}

/*
 * The takeover: every crtc the readout found active is stopped through the reference's own
 * intel_crtc_disable_noatomic().  After this the firmware's picture is gone and the pipe is ours.
 */
int parity_n1_takeover(struct parity_n1_report *out)
{
	struct drm_i915_private *saved;
	unsigned i;

	if (!n1.live || out == 0)
		return -EINVAL;
	saved = parity_lcd_cur_i915;
	parity_lcd_cur_i915 = &n1.i915;
	for (i = 0u; i < (unsigned)N1_PIPES; i++) {
		if (!n1.crtc_state[i].hw.active)
			continue;
		n1.takeovers++;
		intel_crtc_disable_noatomic(&n1.crtc[i], 0);
	}
	n1.still_active = 0u;
	for (i = 0u; i < (unsigned)N1_PIPES; i++)
		if (n1.crtc[i].active || n1.crtc_state[i].hw.active)
			n1.still_active |= 1u << i;
	parity_lcd_cur_i915 = saved;
	n1_fill_report(out);
	return n1.still_active == 0u ? 0 : -EBUSY;
}

/*
 * The registry goes away: the crtc / plane objects of the readout are not the objects of a modeset, and
 * nothing may walk them afterwards.  The encoder and the connector belong to the modeset object and are
 * handed back the way they were found.
 */
void parity_n1_release(void)
{
	if (!n1.live)
		return;
	if (n1.encoder != 0)
		n1.encoder->base.crtc = 0;
	if (n1.connector != 0) {
		n1.connector->base.state = 0;
		n1.connector->base.encoder = 0;
	}
	n1.live = 0;
}
