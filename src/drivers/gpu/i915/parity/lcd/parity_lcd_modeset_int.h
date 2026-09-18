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

struct parity_lcd_modeset {
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
	int prepared;
	int backlight_setup_rc;                 /* intel_backlight_setup(): 0, or a negative errno */
	int plane_armed;                        /* a PLANE_SURF write armed the plane: the buffer may be scanned out */
};

/* entry points implemented by the glue at the end of each generated file (the callers are static there) */
void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id);              /* intel_dpll_port.c */
void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms);                       /* intel_ddi_port.c */
void parity_lcd_ms_bind_buf_trans(struct intel_encoder *encoder);                     /* intel_ddi_buf_trans_port.c */
void parity_lcd_ms_color_check(struct parity_lcd_modeset *ms);                         /* intel_color_port.c */
int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms);                      /* intel_backlight_port.c */
void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms);                        /* intel_display_port.c */
void parity_lcd_ms_crtc_disable(struct parity_lcd_modeset *ms);
int parity_lcd_ms_plane_prepare(struct parity_lcd_modeset *ms, u32 fourcc, u64 modifier, u32 width, u32 height,
	u32 pitch, u32 surf_ggtt_offset);                                                 /* skl_plane_port.c */
void parity_lcd_ms_plane_update(struct parity_lcd_modeset *ms);
void parity_lcd_ms_plane_disable(struct parity_lcd_modeset *ms);

#endif /* PARITY_LCD_MODESET_INT_H */
