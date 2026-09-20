/*
 * WS031 Linux-parity -- what the readout / sanitize / takeover text of N1 needs (E-124).  zedBSD project code.
 *
 * The generated files of this unit are the reference's own: intel_modeset_setup.c (readout, sanitize,
 * intel_crtc_disable_noatomic) plus the readout halves of intel_display.c, intel_ddi.c, intel_dpll_mgr.c,
 * skl_universal_plane.c and skl_watermark.c.  This header maps what they reach outside the ported set: the walks
 * over the device's crtcs / encoders / connectors (here: the objects the N1 runner built), the power-domain
 * accessors of a readout ("get if the well is already on"), and the parts of other subsystems that this path does
 * not port -- each of those is a recorded step, never a silent success.
 */
#ifndef PARITY_N1_COMPAT_H
#define PARITY_N1_COMPAT_H

/* ---- the device's objects: the N1 runner registers them (parity_modeset_setup_glue.inc) ---- */
struct intel_crtc *parity_n1_crtc_at(unsigned idx);
struct intel_encoder *parity_n1_encoder_at(unsigned idx);
struct intel_connector *parity_n1_connector_at(unsigned idx);
struct intel_crtc_state *parity_n1_crtc_state(const struct intel_crtc *crtc);

#undef for_each_intel_crtc
#define for_each_intel_crtc(dev, crtc) \
	for (unsigned _n1_ci = 0u; ((crtc) = parity_n1_crtc_at(_n1_ci)) != NULL; _n1_ci++)
#undef for_each_intel_encoder
#define for_each_intel_encoder(dev, encoder) \
	for (unsigned _n1_ei = 0u; ((encoder) = parity_n1_encoder_at(_n1_ei)) != NULL; _n1_ei++)
#define for_each_intel_connector_iter(connector, iter) \
	while (((connector) = parity_n1_connector_at((iter)->idx++)) != NULL)
#define for_each_intel_dp(dev, encoder) for_each_intel_encoder(dev, encoder)
#define to_intel_crtc_state(x) ((struct intel_crtc_state *)(x))
#define to_intel_dbuf_state(x) (&parity_lcd_wm->new_dbuf)

/* ---- power domains of a readout: take a reference only where the well is already on ---- */
intel_wakeref_t parity_n1_power_get_if_enabled(enum intel_display_power_domain domain);
void parity_n1_power_get_in_set_if_enabled(struct intel_power_domain_mask *mask,
	enum intel_display_power_domain domain);
#define intel_display_power_get_if_enabled(i915, domain) parity_n1_power_get_if_enabled(domain)
#define intel_display_power_get_in_set_if_enabled(i915, set, domain) \
	(parity_n1_power_get_in_set_if_enabled(&(set)->mask, (domain)), true)
#define with_intel_display_power_if_enabled(i915, domain, wf) \
	for ((wf) = parity_n1_power_get_if_enabled(domain); (wf); \
	     intel_display_power_put((i915), (domain), (wf)), (wf) = 0)
#define intel_display_power_put_all_in_set(i915, set) parity_n1_power_put_all_in_set(&(set)->mask)
void parity_n1_power_put_all_in_set(struct intel_power_domain_mask *mask);

/* ---- register field helpers the readout text uses ---- */
#ifndef REG_FIELD_GET
#define REG_FIELD_GET(__mask, __val) ((u32)(((__val) & (__mask)) / ((__mask) & -(__mask))))
#endif
#ifndef ffs
#define ffs(x) __builtin_ffs((int)(x))
#endif

/* ---- the platform branches this machine never takes (display version 13, no Broxton PHY, no LSPCON) ---- */
#define BXT_PHY_LANE_ENABLED 0u
#define BXT_PHY_LANE_POWERDOWN_ACK 0u
#define BXT_PHY_CMNLANE_POWERDOWN_ACK 0u
#define intel_lspcon_infoframes_enabled(encoder, cs) (PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_lspcon_infoframes_enabled"), false)

#endif /* PARITY_N1_COMPAT_H */
