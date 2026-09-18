/*
 * zedBSD WS031: the inline helpers transcoder_is_dsi (display/intel_display.h), intel_crtc_has_type,
 * intel_crtc_has_dp_encoder and intel_crtc_needs_modeset (display/intel_display_types.h) extracted textually
 * from the Linux v6.8.12 i915 reference (MIT; Copyright Intel Corporation -- the full notice is kept in
 * intel_display_port.c) by tools/port_lcd_calc.py.  Do not edit by hand.
 */
#ifndef PARITY_LCD_REF_INLINES_H
#define PARITY_LCD_REF_INLINES_H

static inline bool transcoder_is_dsi(enum transcoder transcoder)
{
	return transcoder == TRANSCODER_DSI_A || transcoder == TRANSCODER_DSI_C;
}

/* intel_display.c */
static inline bool
intel_crtc_has_type(const struct intel_crtc_state *crtc_state,
		    enum intel_output_type type)
{
	return crtc_state->output_types & BIT(type);
}

static inline bool
intel_crtc_has_dp_encoder(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_types &
		(BIT(INTEL_OUTPUT_DP) |
		 BIT(INTEL_OUTPUT_DP_MST) |
		 BIT(INTEL_OUTPUT_EDP));
}

static inline bool
intel_crtc_needs_modeset(const struct intel_crtc_state *crtc_state)
{
	return drm_atomic_crtc_needs_modeset(&crtc_state->uapi);
}

#endif /* PARITY_LCD_REF_INLINES_H */
