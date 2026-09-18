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
	    ops->panel == 0 || ops->power_get == 0 || ops->power_put == 0 || ops->lock == 0 || ops->step == 0)
		return -EINVAL;
	/* combo PHY ports, pipes / transcoders A..D, the two combo PLLs, 8b/10b rates, 1 / 2 / 4 lanes */
	if (cfg->port < 0 || cfg->port > 1 || cfg->pipe < 0 || cfg->pipe > 3 || cfg->cpu_transcoder < 0 || cfg->cpu_transcoder > 3 ||
	    cfg->dpll_id < 0 || cfg->dpll_id > 1 || cfg->aux_ch != cfg->port || s->link.rate_khz <= 0 || s->link.rate_khz > 810000 ||
	    (s->link.lanes != 1 && s->link.lanes != 2 && s->link.lanes != 4) || s->link.bpp <= 0)
		return -EINVAL;
	if (ms.prepared && (ms.crtc.active || ms.plane_armed))
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
	ms.crtc_state.pixel_multiplier = 1;
	ms.crtc_state.framestart_delay = 1;
	ms.crtc_state.enhanced_framing = (cfg->dpcd[2] & 0x80u) != 0u;      /* DP_ENHANCED_FRAME_CAP */
	ms.crtc_state.dp_m_n.tu = s->link.tu;
	ms.crtc_state.dp_m_n.data_m = s->link.data_m; ms.crtc_state.dp_m_n.data_n = s->link.data_n;
	ms.crtc_state.dp_m_n.link_m = s->link.link_m; ms.crtc_state.dp_m_n.link_n = s->link.link_n;
	ms.crtc_state.active_planes = (u8)BIT(PLANE_PRIMARY);

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

	rc = parity_lcd_ms_plane_prepare(&ms, cfg->fb_fourcc, cfg->fb_modifier, cfg->fb_width, cfg->fb_height,
		cfg->fb_pitch, cfg->fb_surf);
	if (rc != 0)
		return rc;
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
	parity_lcd_ms_crtc_enable(&ms);
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

/* the caller confirmed (frame counter / scanline) that the plane is off: the buffer is its own again */
void parity_lcd_modeset_plane_released(void)
{
	ms.plane_armed = 0;
}

/* read the sink's link status now (for the run log) */
void parity_lcd_modeset_link_status(struct parity_lcd_modeset_status *out)
{
	if (out != 0 && ms.prepared) {
		parity_lcd_cur_i915 = &ms.i915;
		read_link_status(out);
	}
}
