/*
 * WS031 Linux-parity — the ONE modeset object of the LCD path (internal to parity/lcd: reference types).
 * zedBSD project code.
 *
 * One screen: one crtc / pipe, one eDP encoder on a combo PHY port, one shared DPLL, one primary plane
 * on one framebuffer.  The reference's callers (hsw_crtc_enable, the plane update, hsw_crtc_disable)
 * and every callee below them read and write THESE objects -- prepared once, used by the enable, the
 * plane update and the disable alike, so what the enable left behind (intel_dp->DP, the wakerefs, the
 * PLL's active mask, link_trained, crtc->active) is what the disable finds.  Diagnostics read them too;
 * nothing is copied into a second bookkeeping structure.
 */
#ifndef PARITY_LCD_MODESET_INT_H
#define PARITY_LCD_MODESET_INT_H

#include "lcd_wm_compat.h"
#include "lcd_flip_compat.h"

struct parity_lcd_modeset {
	int output_hdmi;                /* the output this state drives: an HDMI sink instead of the eDP panel */
	int hdmi_level_shift;           /* intel_bios_hdmi_level_shift() of this port (< 0 = not in the VBT) */
	int dpll_id;                    /* the shared DPLL the reference's rule gave this crtc */
	unsigned also_active_pipes;     /* the other pipes of this configuration (cfg) */
	struct drm_i915_private i915;
	struct intel_crtc crtc;
	struct intel_crtc_state crtc_state;     /* the new state of the enable == the old state of the disable */
	struct intel_atomic_state state;
	struct intel_digital_port dig_port;
	struct intel_connector connector;
	struct drm_connector_state conn_state;
	struct intel_shared_dpll pll;
	struct dpll_info pll_info;
	struct intel_plane plane;
	struct intel_plane_state plane_state;
	struct drm_framebuffer fb;
	struct intel_plane cursor;              /* never shown; the reference reserves DDB space for it (skl_cursor_allocation) */
	struct parity_lcd_wm_ctx wm;            /* old / new global DBUF state */
	int wm_rc;
	/* the CDCLK state this crtc requires (bxt_modeset_calc_cdclk) against the current one */
	struct { int crtc_min, bw_min, min_cdclk, cdclk, vco, voltage_level, change_needed; } cdclk;
	int cdclk_rc;
	unsigned int bw_data_rate;              /* MB/s, as intel_bw_check_qgv_points() compares it */
	/* the commit */
	int dc_off_held; int dc_off_wakeref;
	u32 bl_user, bl_user_max;                /* the backlight device's props.brightness / max_brightness */
	int stop_unconfirmed;                   /* the disable reported an error: nothing further was given back */
	unsigned commits;
	struct drm_vblank_crtc vblank[4];        /* dev->vblank[]: hwmode, max_vblank_count */
	int flip_event;                          /* the event token of the pending update */
	u32 fb_fourcc, fb_width, fb_height, fb_pitch; u64 fb_modifier;
	u32 cur_surf, pend_surf, old_surf; int flip_pending, flip_stuck; unsigned flip_gen;
	int flip_event_ref;                      /* the armed event still holds its vblank reference (taken in update_end) */
	unsigned events_cancelled;
	int prepared;
	int backlight_setup_rc;                 /* intel_backlight_setup(): 0, or a negative errno */
	int plane_armed;                        /* a PLANE_SURF write armed the plane: the buffer may be scanned out */
};

/* entry points implemented by the glue at the end of each generated file (the callers are static there) */
void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id);              /* intel_dpll_port.c */
int parity_lcd_ms_alloc_pll(struct parity_lcd_modeset *ms, const struct intel_dpll_hw_state *hw_state);
void parity_lcd_ms_release_pll(struct parity_lcd_modeset *ms);
void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms);                       /* intel_ddi_port.c */
void parity_lcd_ms_bind_buf_trans(struct intel_encoder *encoder);                     /* intel_ddi_buf_trans_port.c */
void parity_lcd_ms_plane_data_rates(struct parity_lcd_modeset *ms);                    /* intel_atomic_plane_port.c */
void parity_lcd_ms_wm_compute_off(struct parity_lcd_modeset *ms);                         /* skl_watermark_port.c */
int parity_lcd_ms_cdclk_check(struct parity_lcd_modeset *ms);                             /* intel_cdclk_port.c */
int parity_lcd_ms_bw_min_cdclk(struct parity_lcd_modeset *ms);                            /* intel_bw_port.c */
unsigned int parity_lcd_ms_bw_data_rate(struct parity_lcd_modeset *ms);
void parity_lcd_ms_plane_min_cdclk(struct parity_lcd_modeset *ms);                        /* skl_plane_port.c */
/* reference functions of the commit's outer part (generated files) */
void intel_ddi_compute_min_voltage_level(struct intel_crtc_state *crtc_state);
void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state, struct intel_power_domain_mask *old_domains);
void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc, struct intel_power_domain_mask *domains);
void intel_dbuf_pre_plane_update(struct intel_atomic_state *state);
void intel_dbuf_post_plane_update(struct intel_atomic_state *state);
void intel_mbus_dbox_update(struct intel_atomic_state *state);
void parity_lcd_ms_set_brightness(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max);     /* intel_backlight_port.c */
void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on);
u32 parity_lcd_ms_user_level(struct parity_lcd_modeset *ms, u32 user_max);
int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);
int parity_lcd_dbuf_current(struct intel_dbuf_state *out);        /* the device's DBUF state (-1 = none yet) */
void parity_lcd_dbuf_publish(const struct intel_dbuf_state *now);
void parity_lcd_dbuf_forget(void);
void parity_lcd_ms_active_timings(struct parity_lcd_modeset *ms);                         /* intel_crtc_port.c */
void parity_lcd_ms_plane_update_flip(struct parity_lcd_modeset *ms);                             /* skl_watermark_port.c */
void parity_lcd_ms_color_check(struct parity_lcd_modeset *ms);                         /* intel_color_port.c */
int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms);                      /* intel_backlight_port.c */
void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms);                        /* intel_display_port.c */
void parity_lcd_ms_crtc_disable(struct parity_lcd_modeset *ms);
int parity_lcd_ms_plane_prepare(struct parity_lcd_modeset *ms, u32 fourcc, u64 modifier, u32 width, u32 height,
	u32 pitch, u32 surf_ggtt_offset);                                                 /* skl_plane_port.c */
void parity_lcd_ms_plane_update(struct parity_lcd_modeset *ms);
void parity_lcd_ms_plane_disable(struct parity_lcd_modeset *ms);

/* intel_hdmi_mode_port.c (glue): the reference's hsw_set_infoframes, as dig_port->set_infoframes */
void (*parity_lcd_hdmi_set_infoframes(void))(struct intel_encoder *, bool, const struct intel_crtc_state *,
	const struct drm_connector_state *);

#endif /* PARITY_LCD_MODESET_INT_H */

/* intel_crtc_vblank_off() of the flip path: settle the pending event (parity_lcd_modeset.c) */
void parity_lcd_ms_vblank_off(void);
int parity_lcd_ms_evade_window(struct parity_lcd_modeset *ms, int *min, int *max, int *vblank_start);
void parity_lcd_ms_set_acpi(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max);
