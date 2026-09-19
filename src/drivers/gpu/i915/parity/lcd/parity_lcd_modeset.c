/*
 * WS031 Linux-parity — the one-screen modeset object and its driver (see parity_lcd_modeset.h).
 * zedBSD project code.  The work is done by the reference's callers and callees in the generated
 * files of this directory; this file builds the state they read (the values an atomic check would
 * have computed: they come from parity_lcd_compute()), calls the per-file entry points, and reads back
 * what the run left behind.
 */
#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"
#include "parity_lcd_modeset.h"

#undef EBUSY
#define EBUSY 16                        /* Linux numbering, returned negative */

static struct parity_lcd_modeset ms;
static struct parity_lcd_emit *ms_ops;
static const char *ms_first_error;
static unsigned ms_errors;
static int ms_retained;                 /* outlives prepare's memset: only _discard_model() clears it */
static const struct parity_lcd_emit *ms_retained_ops;


static void on_error(void *ctx, const char *what)
{
	(void)ctx;
	if (ms_errors++ == 0u)
		ms_first_error = what;
	if (ms_ops != 0 && ms_ops->error != 0)
		ms_ops->error(ms_ops->ctx, what);
}

void parity_lcd_debug(const char *what)
{
	if (ms_ops != 0 && ms_ops->debug != 0)
		ms_ops->debug(ms_ops->ctx, what);
}

int parity_lcd_modeset_prepare(const struct parity_lcd_state *s, const struct parity_lcd_modeset_cfg *cfg,
	struct parity_lcd_emit *ops)
{
	struct drm_display_mode *mode;
	int rc;

	if (s == 0 || cfg == 0 || ops == 0 || ops->write32 == 0 || ops->rmw32 == 0 || ops->read32 == 0 || ops->wait_reg == 0 ||
	    ops->usleep == 0 || ops->udelay == 0 || ops->dpcd_read == 0 || ops->dpcd_write == 0 || ops->read_dpcd_caps == 0 ||
	    ops->panel == 0 || ops->power_get == 0 || ops->power_put == 0 || ops->power_put_async == 0 || ops->dbuf_slices_update == 0 ||
	    ops->lock == 0 || ops->step == 0)
		return -EINVAL;
	/* combo PHY ports, pipes / transcoders A..D, the two combo PLLs, 8b/10b rates, 1 / 2 / 4 lanes */
	if (cfg->port < 0 || cfg->port > 1 || cfg->pipe < 0 || cfg->pipe > 3 || cfg->cpu_transcoder < 0 || cfg->cpu_transcoder > 3 ||
	    cfg->dpll_id < 0 || cfg->dpll_id > 1 || cfg->aux_ch != cfg->port || s->link.rate_khz <= 0 || s->link.rate_khz > 810000 ||
	    (s->link.lanes != 1 && s->link.lanes != 2 && s->link.lanes != 4) || s->link.bpp <= 0)
		return -EINVAL;
	/* refused BEFORE anything is initialised: a retained state is never overwritten */
	if (ms_retained || ms.stop_unconfirmed || (ms.prepared && (ms.crtc.active || ms.plane_armed || ms.dc_off_held)))
		return -EBUSY;

	memset(&ms, 0, sizeof(ms));
	ms_ops = ops;
	ms_errors = 0u;
	ms_first_error = 0;
	parity_lcd_error_bind(on_error, 0);

	ms.i915.emit = ops;
	ms.i915.display.dpll.lock.which = PARITY_LCD_LOCK_DPLL;
	ms.i915.display.backlight.lock.which = PARITY_LCD_LOCK_BACKLIGHT;
	ms.i915.display.vbt.override_afc_startup = cfg->vbt_override_afc_startup != 0;
	ms.i915.display.dmc.fw_mask = cfg->dmc_fw_mask;
	memcpy(ms.i915.display.wm.skl_latency, cfg->wm_latency, sizeof(ms.i915.display.wm.skl_latency));
	ms.i915.display.wm.num_levels = cfg->wm_num_levels;
	ms.i915.display.wm.ipc_enabled = cfg->wm_ipc_enabled != 0;
	ms.i915.display.sagv.block_time_us = cfg->sagv_block_time_us;
	ms.i915.display.device_info.dbuf.size = cfg->dbuf_size;
	ms.i915.display.device_info.dbuf.slice_mask = cfg->dbuf_slice_mask;
	ms.i915.display.runtime.pipe_mask = 0x0f;
	ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;
	ms.wm.old_dbuf.joined_mbus = cfg->mbus_joined != 0;
	ms.i915.display.device_info.has_ddi = true;
	ms.i915.display.cdclk.hw.cdclk = cfg->cdclk_khz;
	ms.i915.display.cdclk.hw.vco = cfg->cdclk_vco_khz;
	ms.i915.display.cdclk.hw.ref = cfg->cdclk_ref_khz;
	ms.i915.display.cdclk.hw.bypass = cfg->cdclk_bypass_khz;
	ms.i915.display.cdclk.hw.voltage_level = cfg->cdclk_voltage_level;
	ms.i915.display.cdclk.max_cdclk_freq = cfg->cdclk_max_khz;
	ms.crtc.base.dev = &ms.i915.drm;
	ms.crtc.base.name = "pipe";
	ms.crtc.pipe = (enum pipe)cfg->pipe;

	/* the crtc state: what intel_dp_compute_config() / intel_crtc_compute_config() leave for an SST panel */
	ms.crtc_state.uapi.crtc = &ms.crtc.base;
	ms.crtc_state.uapi.mode_changed = true;
	ms.crtc_state.cpu_transcoder = cfg->cpu_transcoder;
	ms.crtc_state.master_transcoder = INVALID_TRANSCODER;
	ms.crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	ms.crtc_state.hsw_workaround_pipe = INVALID_PIPE;
	mode = &ms.crtc_state.hw.adjusted_mode;
	mode->clock = s->mode.clock_khz;
	mode->hdisplay = s->mode.hdisplay; mode->hsync_start = s->mode.hsync_start;
	mode->hsync_end = s->mode.hsync_end; mode->htotal = s->mode.htotal;
	mode->vdisplay = s->mode.vdisplay; mode->vsync_start = s->mode.vsync_start;
	mode->vsync_end = s->mode.vsync_end; mode->vtotal = s->mode.vtotal;
	mode->flags = (s->mode.hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) |
		(s->mode.vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);
	drm_mode_set_crtcinfo(mode, 0);
	ms.crtc_state.pixel_rate = mode->crtc_clock;
	ms.crtc_state.pipe_src.x2 = (int)cfg->fb_width;
	ms.crtc_state.pipe_src.y2 = (int)cfg->fb_height;
	ms.crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	ms.crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	ms.crtc_state.port_clock = s->link.rate_khz;
	ms.crtc_state.lane_count = s->link.lanes;
	ms.crtc_state.pipe_bpp = s->link.bpp;
	/* intel_modeset_pipe_config(): "Dithering seems to not pass-through bits correctly when it should, so only enable it
	 * on 6bpc panels"; dither_force_disable is set by DP compliance tests only */
	ms.crtc_state.dither = ms.crtc_state.pipe_bpp == 6 * 3;
	ms.crtc_state.pixel_multiplier = 1;
	ms.crtc_state.framestart_delay = 1;
	ms.crtc_state.enhanced_framing = (cfg->dpcd[2] & 0x80u) != 0u;      /* DP_ENHANCED_FRAME_CAP */
	ms.crtc_state.dp_m_n.tu = s->link.tu;
	ms.crtc_state.dp_m_n.data_m = s->link.data_m; ms.crtc_state.dp_m_n.data_n = s->link.data_n;
	ms.crtc_state.dp_m_n.link_m = s->link.link_m; ms.crtc_state.dp_m_n.link_n = s->link.link_n;
	ms.crtc_state.active_planes = (u8)BIT(PLANE_PRIMARY);
	ms.crtc_state.uapi.encoder_mask = 1u << 0;        /* drm_encoder_mask() of the one encoder */

	/* the encoder / digital port / DP object, as intel_ddi_init() leaves them for an eDP on a combo PHY */
	ms.dig_port.base.base.dev = &ms.i915.drm;
	ms.dig_port.base.base.name = "DDI";
	ms.dig_port.base.port = (enum port)cfg->port;
	ms.dig_port.base.type = INTEL_OUTPUT_EDP;
	ms.dig_port.saved_port_bits = cfg->saved_port_bits;
	ms.dig_port.aux_ch = cfg->aux_ch;
	ms.dig_port.max_lanes = 4;
	ms.dig_port.ddi_io_power_domain = POWER_DOMAIN_PORT_DDI_IO_A + cfg->port;   /* d13_port_domains[]: ports A..C */
	ms.dig_port.dp.attached_connector = &ms.connector;
	memcpy(ms.dig_port.dp.dpcd, cfg->dpcd, sizeof(ms.dig_port.dp.dpcd));
	memcpy(ms.dig_port.dp.edp_dpcd, cfg->edp_dpcd, sizeof(ms.dig_port.dp.edp_dpcd));
	ms.dig_port.dp.aux.name = "AUX";
	ms.connector.base.name = "eDP";
	ms.connector.panel.vbt.edp.low_vswing = cfg->vbt_low_vswing != 0;
	ms.connector.panel.vbt.edp.hobl = cfg->vbt_hobl != 0;
	ms.connector.base.dev = &ms.i915.drm;
	ms.connector.panel.vbt.backlight.present = cfg->vbt_backlight_present != 0;
	ms.connector.panel.vbt.backlight.active_low_pwm = cfg->vbt_backlight_active_low != 0;
	ms.connector.panel.vbt.backlight.controller = (s8)cfg->vbt_backlight_controller;
	ms.connector.panel.vbt.backlight.pwm_freq_hz = cfg->vbt_backlight_pwm_freq_hz;
	ms.connector.panel.vbt.backlight.min_brightness = cfg->vbt_backlight_min_brightness;
	ms.i915.display.runtime.rawclk_freq = cfg->rawclk_khz;
	ms.conn_state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	ms.conn_state.connector = &ms.connector;
	ms.conn_state.best_encoder = &ms.dig_port.base;

	/* the PLL state computed by icl_calc_dpll_state() (parity_lcd_compute) */
	parity_lcd_ms_bind_pll(&ms, cfg->dpll_id);
	ms.pll.state.hw_state.cfgcr0 = s->pll.cfgcr0;
	ms.pll.state.hw_state.cfgcr1 = s->pll.cfgcr1;
	ms.pll.state.hw_state.div0 = s->pll.div0;
	parity_lcd_ms_bind_encoder(&ms);
	parity_lcd_ms_color_check(&ms);

	ms.fb_fourcc = cfg->fb_fourcc; ms.fb_modifier = cfg->fb_modifier; ms.fb_width = cfg->fb_width;
	ms.fb_height = cfg->fb_height; ms.fb_pitch = cfg->fb_pitch; ms.cur_surf = cfg->fb_surf;
	rc = parity_lcd_ms_plane_prepare(&ms, cfg->fb_fourcc, cfg->fb_modifier, cfg->fb_width, cfg->fb_height,
		cfg->fb_pitch, cfg->fb_surf);
	if (rc != 0)
		return rc;
	/* the check phase of the commit: watermarks and DDB for the new state (nothing is written) */
	if (cfg->wm_num_levels == 0u || cfg->wm_num_levels > 8u || cfg->dbuf_size == 0u || cfg->dbuf_slice_mask == 0u)
		return -EINVAL;
	ms.crtc_state.hw.active = true;
	ms.crtc_state.hw.enable = true;
	ms.crtc_state.hw.pipe_mode = ms.crtc_state.hw.adjusted_mode;
	ms.plane_state.uapi.visible = true;
	ms.cursor.base.dev = &ms.i915.drm;
	ms.cursor.id = PLANE_CURSOR;
	ms.cursor.pipe = ms.crtc.pipe;
	ms.crtc.base.cursor = &ms.cursor.base;
	parity_lcd_cur_i915 = &ms.i915;
	ms.wm_rc = parity_lcd_ms_wm_compute(&ms);
	if (ms.wm_rc != 0)
		return ms.wm_rc;
	/* the check phase, continued: the CDCLK this state requires, and the memory bandwidth */
	intel_ddi_compute_min_voltage_level(&ms.crtc_state);
	parity_lcd_ms_plane_min_cdclk(&ms);
	ms.cdclk_rc = parity_lcd_ms_cdclk_check(&ms);
	if (ms.cdclk_rc != 0)
		return ms.cdclk_rc;
	if (ms.cdclk.change_needed) {
		/* the reference would reprogram CDCLK around the planes (intel_set_cdclk_pre/post_plane_update): not connected */
		on_error(0, "the mode requires a CDCLK state other than the current one: CDCLK programming is not connected" "\n");
		return -EINVAL;
	}
	ms.bw_data_rate = parity_lcd_ms_bw_data_rate(&ms);
	if (cfg->qgv_allowed_bw == 0u || ms.bw_data_rate > cfg->qgv_allowed_bw) {
		on_error(0, "the memory bandwidth of the allowed QGV point is unknown or below what the plane needs" "\n");
		return -EINVAL;
	}
	ms.prepared = 1;
	return 0;
}

static void read_link_status(struct parity_lcd_modeset_status *out)
{
	u8 ls[DP_LINK_STATUS_SIZE];

	memset(ls, 0, sizeof(ls));
	out->link_status_rc = drm_dp_dpcd_read_phy_link_status(&ms.dig_port.dp.aux, DP_PHY_DPRX, ls);
	memcpy(out->link_status, ls, sizeof(out->link_status));
	out->cr_ok = out->link_status_rc == 0 && drm_dp_clock_recovery_ok(ls, ms.crtc_state.lane_count);
	out->eq_ok = out->link_status_rc == 0 && drm_dp_channel_eq_ok(ls, ms.crtc_state.lane_count);
}

void parity_lcd_modeset_status(struct parity_lcd_modeset_status *out)
{
	if (out == 0)
		return;
	memset(out, 0, sizeof(*out));
	out->prepared = ms.prepared;
	out->crtc_active = ms.crtc.active;
	out->plane_armed = ms.plane_armed;
	out->link_rate = ms.dig_port.dp.link_rate;
	out->lane_count = ms.dig_port.dp.lane_count;
	out->link_trained_flag = ms.dig_port.dp.link_trained;
	memcpy(out->train_set, ms.dig_port.dp.train_set, sizeof(out->train_set));
	out->ddi_buf_ctl_value = ms.dig_port.dp.DP;
	out->pll_on = ms.pll.on;
	out->pll_active_mask = ms.pll.active_mask;
	out->pll_wakeref = ms.pll.wakeref;
	out->ddi_io_wakeref = ms.dig_port.ddi_io_wakeref;
	out->aux_wakeref = ms.dig_port.aux_wakeref;
	out->backlight_present = ms.connector.panel.backlight.present;
	out->backlight_enabled = ms.connector.panel.backlight.enabled;
	out->backlight_setup_rc = ms.backlight_setup_rc;
	out->backlight_pwm_max = ms.connector.panel.backlight.pwm_level_max;
	out->backlight_level = ms.connector.panel.backlight.level;
	out->wm_rc = ms.wm_rc;
	out->ddb_start = ms.crtc_state.wm.skl.plane_ddb[PLANE_PRIMARY].start;
	out->ddb_end = ms.crtc_state.wm.skl.plane_ddb[PLANE_PRIMARY].end;
	out->wm0_enable = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].enable;
	out->wm0_blocks = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].blocks;
	out->wm0_lines = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].lines;
	out->dbuf_slices_wanted = ms.wm.new_dbuf.enabled_slices;
	out->mbus_joined = ms.wm.new_dbuf.joined_mbus;
	out->cdclk_rc = ms.cdclk_rc;
	out->cdclk_crtc_min = ms.cdclk.crtc_min;
	out->cdclk_bw_min = ms.cdclk.bw_min;
	out->cdclk_required_khz = ms.cdclk.cdclk;
	out->cdclk_required_vco = ms.cdclk.vco;
	out->cdclk_required_level = ms.cdclk.voltage_level;
	out->cdclk_change_needed = ms.cdclk.change_needed;
	out->bw_data_rate = ms.bw_data_rate;
	out->dc_off_held = ms.dc_off_held;
	{
		enum intel_display_power_domain domain;

		for_each_power_domain(domain, &ms.crtc.enabled_power_domains.mask)
			out->crtc_domains_held++;
	}
	out->dbuf_slices_now = ms.wm.old_dbuf.enabled_slices;
	out->mbus_joined_now = ms.wm.old_dbuf.joined_mbus;
	out->stop_unconfirmed = ms.stop_unconfirmed;
	out->retained = parity_lcd_modeset_retained();
	out->dither = ms.crtc_state.dither;
	out->cur_surf = ms.cur_surf;
	out->pend_surf = ms.pend_surf;
	out->flip_pending = ms.flip_pending;
	out->flip_stuck = ms.flip_stuck;
	out->flip_event_ref = ms.flip_event_ref;
	out->events_cancelled = ms.events_cancelled;
	out->flip_gen = ms.flip_gen;
	out->backlight_min = ms.connector.panel.backlight.min;
	out->backlight_max = ms.connector.panel.backlight.max;
	out->backlight_user = ms.bl_user;
	out->backlight_user_max = ms.bl_user_max;
	out->commits = ms.commits;
	out->errors = ms_errors;
	out->first_error = ms_first_error;
	out->link_status_rc = -EINVAL;
}

int parity_lcd_modeset_enable(void)
{
	struct parity_lcd_modeset_status st;

	if (!ms.prepared || ms.crtc.active)
		return PARITY_LCD_MS_NOT_PREPARED;
	/* connector-init work of the reference that touches the hardware (reads only): the backlight setup */
	parity_lcd_cur_i915 = &ms.i915;
	ms.backlight_setup_rc = parity_lcd_ms_backlight_setup(&ms);
	if (ms.backlight_setup_rc != 0)
		on_error(0, "intel_backlight_setup failed (no PWM frequency from the hardware or the VBT)\n");
	parity_lcd_ms_active_timings(&ms);        /* intel_enable_crtc(): before the crtc_enable hook */
	parity_lcd_ms_crtc_enable(&ms);
	/* intel_backlight_device_register(): max_brightness = backlight.max, brightness = the level scaled to it */
	ms.bl_user_max = ms.connector.panel.backlight.max;
	ms.bl_user = ms.bl_user_max != 0u ? parity_lcd_ms_user_level(&ms, ms.bl_user_max) : 0u;
	if (ms_errors != 0u)
		return PARITY_LCD_MS_ERRORS;
	/* the evidence that training worked is the sink's own status, not the flag the stop function sets */
	parity_lcd_modeset_status(&st);
	read_link_status(&st);
	if (!ms.crtc.active || !st.cr_ok || !st.eq_ok)
		return PARITY_LCD_MS_LINK_NOT_TRAINED;
	return PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_plane_update(void)
{
	if (!ms.prepared || !ms.crtc.active)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	parity_lcd_ms_plane_update(&ms);
	return ms_errors != 0u ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_plane_disable(void)
{
	if (!ms.prepared)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	parity_lcd_ms_plane_disable(&ms);
	/* plane_armed is NOT cleared here: the write takes effect at the next vblank, and only the caller,
	 * who watches the hardware, may decide that the buffer is no longer scanned out */
	return ms_errors != 0u ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_disable(void)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_ms_crtc_disable(&ms);
	if (ms_errors != before)
		return PARITY_LCD_MS_ERRORS;
	if (ms.pll.on || ms.pll.active_mask != 0 || ms.dig_port.ddi_io_wakeref != 0 || ms.dig_port.aux_wakeref != 0)
		return PARITY_LCD_MS_STILL_OWNED;
	return PARITY_LCD_MS_OK;
}

/*
 * intel_crtc_vblank_off() -> drm_crtc_vblank_off(): pending events of the crtc are sent (with the current count) and
 * the reference each one held is dropped; nobody waits for them afterwards.  Here: the flip's one event.  The event
 * record is the waiting thread's own (the IRQ side only advances the pipe's counters under the IRQ lock), so settling
 * it is: the backend forgets the armed event (a later wait is refused, a late vblank completes nothing), then the
 * reference goes -- exactly once, whatever happened to the flip (done / timed out / not latched).
 */
void parity_lcd_ms_vblank_off(void)
{
	if (!ms.flip_event_ref || ms_ops == 0)
		return;
	if (ms_ops->cancel_event != 0)
		ms_ops->cancel_event(ms_ops->ctx, ms.crtc.pipe);
	ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
	ms.flip_event_ref = 0;
	ms.events_cancelled++;
}

int parity_lcd_modeset_evade_window(int *min, int *max, int *vblank_start)
{
	if (!ms.prepared || !ms.crtc.active)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	return parity_lcd_ms_evade_window(&ms, min, max, vblank_start) == 0 ? PARITY_LCD_MS_OK : PARITY_LCD_MS_ERRORS;
}

/* the caller confirmed (frame counter / scanline) that the plane is off: the buffer is its own again */
void parity_lcd_modeset_plane_released(void)
{
	ms.plane_armed = 0;
	ms.flip_pending = 0;                     /* the display reads neither buffer any more */
	ms.flip_stuck = 0;
}

/* read the sink's link status now (for the run log) */
void parity_lcd_modeset_link_status(struct parity_lcd_modeset_status *out)
{
	if (out != 0 && ms.prepared) {
		parity_lcd_cur_i915 = &ms.i915;
		read_link_status(out);
	}
}

/* ---- the two commits: intel_atomic_commit_tail() reduced to this crtc (the order and the callees are the reference's) ---- */
static void observe(int point)
{
	if (ms_ops != 0 && ms_ops->observe != 0)
		ms_ops->observe(ms_ops->ctx, point);
}

int parity_lcd_modeset_commit_enable(void)
{
	struct intel_power_domain_mask put_domains;
	int rc, prc = PARITY_LCD_MS_OK;

	if (!ms.prepared || ms.crtc.active || ms.dc_off_held || parity_lcd_modeset_retained())
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	parity_lcd_wm = &ms.wm;
	ms.state.base.dev = &ms.i915.drm;
	ms.commits++;

	/* "During full modesets we write a lot of registers, wait for PLLs, etc. Doing that while DC states are enabled is not a good idea." */
	ms.dc_off_wakeref = intel_display_power_get(&ms.i915, POWER_DOMAIN_DC_OFF);
	ms.dc_off_held = 1;
	observe(PARITY_LCD_OBS_COMMIT_BEGIN);
	/* intel_atomic_prepare_plane_clear_colors(): only for modifiers with a clear-colour plane (this one is linear) */
	intel_modeset_get_crtc_power_domains(&ms.crtc_state, &put_domains);
	/* intel_commit_modeset_disables(): the old crtc state is inactive -- nothing to disable */
	/* intel_pmdemand_pre_plane_update(): returns for display version < 14 */
	/* intel_set_cdclk_pre_plane_update(): the required CDCLK state equals the current one (checked in prepare): returns */
	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it (SAGV off, max-bandwidth point; bandwidth checked in prepare)");
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);

	/* skl_commit_modeset_enables(): intel_enable_crtc(), then intel_update_crtc() -> the planes */
	rc = parity_lcd_modeset_enable();
	observe(PARITY_LCD_OBS_PIPE_ENABLED);
	if (rc == PARITY_LCD_MS_OK) {
		prc = parity_lcd_modeset_plane_update();
		observe(PARITY_LCD_OBS_PLANE_ARMED);
	} else {
		PARITY_LCD_DECIDED(&ms.i915, "plane update skipped: the crtc enable reported an anomaly, the buffer is not handed to the display");
	}

	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_post_plane_update: QGV points are not relaxed (SAGV stays off)");
	/* intel_set_cdclk_post_plane_update() / intel_pmdemand_post_plane_update(): as above */
	ms.wm.old_dbuf = ms.wm.new_dbuf;        /* the new global state is the current one from here on */
	observe(PARITY_LCD_OBS_COMMIT_END);
	/* "Delay re-enabling DC states by 17 ms to avoid the off->on->off toggling overhead at and above 60 FPS." */
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return rc != PARITY_LCD_MS_OK ? rc : prc;
}

int parity_lcd_modeset_commit_disable(void)
{
	static struct intel_crtc_state off_state;       /* the NEW state of this commit: the crtc inactive */
	struct intel_power_domain_mask put_domains;
	unsigned before;
	int rc, prc;

	if (!ms.prepared || !ms.crtc.active || ms.dc_off_held || parity_lcd_modeset_retained())
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	ms.state.base.dev = &ms.i915.drm;
	ms.commits++;
	/* check phase: the global DBUF state without this pipe */
	parity_lcd_ms_wm_compute_off(&ms);
	off_state = ms.crtc_state;
	off_state.hw.active = false;
	off_state.hw.enable = false;

	ms.dc_off_wakeref = intel_display_power_get(&ms.i915, POWER_DOMAIN_DC_OFF);
	ms.dc_off_held = 1;
	observe(PARITY_LCD_OBS_COMMIT_BEGIN);
	intel_modeset_get_crtc_power_domains(&off_state, &put_domains);         /* nothing new; everything held goes to put_domains */

	/* intel_commit_modeset_disables() -> intel_old_crtc_state_disables(): the planes, then the crtc */
	/* only what THIS commit reports decides (an enable that failed earlier has left its errors in the count) */
	before = ms_errors;
	(void)parity_lcd_modeset_plane_disable();
	prc = ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
	observe(PARITY_LCD_OBS_PLANE_DISABLED);
	rc = parity_lcd_modeset_disable();
	observe(PARITY_LCD_OBS_PIPE_DISABLED);
	if (rc != PARITY_LCD_MS_OK || prc != PARITY_LCD_MS_OK) {
		/* the stop is not confirmed: shrinking the DBUF, dropping the pipe's power domains or letting DC states back
		 * in under a pipe that may still run is the one thing not to do.  Everything stays; the caller keeps the buffer. */
		ms.stop_unconfirmed = 1;
		PARITY_LCD_DECIDED(&ms.i915, "commit tail after a failed disable NOT run: DBUF slices, crtc power domains and DC_OFF stay held");
		return rc != PARITY_LCD_MS_OK ? rc : prc;
	}

	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it");
	before = ms_errors;
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);
	/* skl_commit_modeset_enables(): nothing is enabled by this commit */
	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	ms.wm.old_dbuf = ms.wm.new_dbuf;
	observe(PARITY_LCD_OBS_COMMIT_END);
	if (ms_errors != before) {
		/* e.g. the pipe's power well could not be turned off (its interrupt drain failed): the stop is not confirmed;
		 * DC_OFF stays held and the caller keeps the buffer */
		ms.stop_unconfirmed = 1;
		PARITY_LCD_DECIDED(&ms.i915, "commit tail reported an error (power not released): DC_OFF kept, stop unconfirmed");
		return PARITY_LCD_MS_ERRORS;
	}
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return PARITY_LCD_MS_OK;
}

void parity_lcd_modeset_abandoned(void)
{
	ms.stop_unconfirmed = 1;
	ms_retained = 1;
	ms_retained_ops = ms_ops;
}

int parity_lcd_modeset_retained(void)
{
	return ms_retained || ms.stop_unconfirmed;
}

int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops)
{
	if (!parity_lcd_modeset_retained())
		return 0;
	if (ops == 0 || ops != (ms_retained_ops != 0 ? ms_retained_ops : ms_ops) || !ops->model)
		return -1;                      /* real hardware, or not the backend that holds it: nothing is released */
	memset(&ms, 0, sizeof(ms));
	ms_retained = 0;
	ms_retained_ops = 0;
	return 0;
}

void parity_lcd_backend_fault(const char *what)
{
	on_error(0, what);
}

int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active || parity_lcd_modeset_retained() || user_max == 0u || user_level > user_max)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_ms_set_brightness(&ms, user_level, user_max);
	if (user_max == ms.bl_user_max)
		ms.bl_user = user_level;
	else
		ms.bl_user = parity_lcd_ms_user_level(&ms, ms.bl_user_max);
	return ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_backlight(int on)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active || parity_lcd_modeset_retained())
		return PARITY_LCD_MS_NOT_PREPARED;
	{
		u32 before_level = ms.connector.panel.backlight.level;

		parity_lcd_ms_backlight_power(&ms, on);
		/* __intel_backlight_enable(): a level <= min comes back as max, and the device's brightness follows */
		if (on && ms.connector.panel.backlight.level != before_level)
			ms.bl_user = parity_lcd_ms_user_level(&ms, ms.bl_user_max);
	}
	return ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

static uint32_t live_surf(void)
{
	return ms_ops->read32(ms_ops->ctx, i915_mmio_reg_offset(PLANE_SURFLIVE(ms.crtc.pipe, PLANE_PRIMARY)));
}

static uint32_t frame_now(void)
{
	return ms_ops->read32(ms_ops->ctx, i915_mmio_reg_offset(PIPE_FRMCOUNT_G4X(ms.crtc.pipe)));
}

int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out)
{
	struct parity_lcd_flip_result res;
	unsigned before;
	int rc, dc_off;

	memset(&res, 0, sizeof(res));
	res.result = PARITY_LCD_FLIP_REFUSED;
	res.old_surf = ms.cur_surf;
	res.new_surf = new_surf;
	if (!ms.prepared || !ms.crtc.active || !ms.plane_armed || parity_lcd_modeset_retained() || ms.flip_pending || ms.flip_stuck ||
	    new_surf == ms.cur_surf || (new_surf & 0xfffu) != 0u || ms_ops->vblank_get == 0 || ms_ops->wait_event == 0) {
		if (out != 0)
			*out = res;
		return PARITY_LCD_MS_NOT_PREPARED;
	}
	parity_lcd_cur_i915 = &ms.i915;
	res.gen = ++ms.flip_gen;
	res.live_before = live_surf();
	res.frame_before = frame_now();
	/* the new plane state: the same layout, another surface */
	rc = parity_lcd_ms_plane_prepare(&ms, ms.fb_fourcc, ms.fb_modifier, ms.fb_width, ms.fb_height, ms.fb_pitch, new_surf);
	if (rc != 0) {
		(void)parity_lcd_ms_plane_prepare(&ms, ms.fb_fourcc, ms.fb_modifier, ms.fb_width, ms.fb_height, ms.fb_pitch, ms.cur_surf);
		if (out != 0)
			*out = res;
		return PARITY_LCD_MS_NOT_PREPARED;
	}
	/* from here both buffers may be read by the display until the completion is known */
	ms.old_surf = ms.cur_surf;
	ms.pend_surf = new_surf;
	ms.flip_pending = 1;
	before = ms_errors;
	/* intel_atomic_commit_tail(): DC states off around every commit, "during fastsets and other updates" too */
	dc_off = intel_display_power_get(&ms.i915, POWER_DOMAIN_DC_OFF);
	parity_lcd_ms_plane_update_flip(&ms);
	ms.flip_event_ref = 1;                   /* intel_pipe_update_end(): drm_crtc_vblank_get() for the armed event */
	res.update_errors = (int)(ms_errors - before);
	/* the event armed by intel_pipe_update_end(): completed by the pipe's next vblank */
	res.event_rc = ms_ops->wait_event(ms_ops->ctx, ms.crtc.pipe, 100u);
	res.live_after = live_surf();
	res.frame_after = frame_now();
	/* drm_atomic_helper_wait_for_flip_done() came first; then the commit drops DC_OFF (async, 17 ms) */
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, dc_off, 17);
	if (res.event_rc == 0) {
		/* drm_send_event: the event's vblank reference (taken in intel_pipe_update_end) is dropped */
		ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
		ms.flip_event_ref = 0;
		if (res.live_after == new_surf) {
			ms.cur_surf = new_surf;
			ms.flip_pending = 0;
			res.result = PARITY_LCD_FLIP_DONE;
		} else {
			ms.flip_stuck = 1;
			res.result = PARITY_LCD_FLIP_NOT_LATCHED;
		}
	} else {
		ms.flip_stuck = 1;              /* the event reference stays: the completion may still come */
		res.result = PARITY_LCD_FLIP_TIMEOUT;
	}
	if (out != 0)
		*out = res;
	if (res.result != PARITY_LCD_FLIP_DONE)
		return PARITY_LCD_MS_ERRORS;
	return res.update_errors != 0 ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}
