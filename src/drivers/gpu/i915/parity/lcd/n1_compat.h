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
/* the reference iterates a wakeref in the loop header (with_intel_display_power*): its own style */
#pragma GCC diagnostic ignored "-Wfor-loop-analysis"
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
/* drm_connector_list_iter: this path's connector list is the registry, so the iterator is an index.
 * The objects are static (the runner owns them), so the reference's reference counting has nothing to
 * count here -- an adaptation, not an unported path. */
struct drm_connector_list_iter { unsigned idx; };
#define drm_connector_list_iter_begin(dev, iter) ((iter)->idx = 0u)
#define drm_connector_list_iter_end(iter) ((void)0)
#define drm_connector_get(connector) ((void)0)
#define drm_connector_put(connector) ((void)0)
#define DRM_MODE_DPMS_ON 0
#define DRM_MODE_DPMS_OFF 3
#define for_each_intel_connector_iter(connector, iter) \
	while (((connector) = parity_n1_connector_at((iter)->idx++)) != NULL)
#define for_each_intel_dp(dev, encoder) for_each_intel_encoder(dev, encoder)
/* the reference walks the crtcs of a pipe mask, and the encoders found on one crtc */
#undef for_each_intel_crtc_in_pipe_mask
#define for_each_intel_crtc_in_pipe_mask(dev, crtc, mask) \
	for_each_intel_crtc(dev, crtc) for_each_if((mask) & BIT((crtc)->pipe))
#define for_each_encoder_on_crtc(dev, __crtc, intel_encoder) \
	for_each_intel_encoder(dev, intel_encoder) \
		for_each_if((intel_encoder)->base.crtc == (__crtc))
#define to_intel_crtc_state(x) ((struct intel_crtc_state *)(x))
#define to_intel_dbuf_state(x) (&parity_lcd_wm->new_dbuf)

/* ---- power domains of a readout: take a reference only where the well is already on ---- */
intel_wakeref_t parity_n1_power_get_if_enabled(enum intel_display_power_domain domain);
void parity_n1_power_get_in_set_if_enabled(struct intel_display_power_domain_set *set,
	enum intel_display_power_domain domain);
/* the readout's own mask lives inside the set (intel_display_power.h) */
#define parity_n1_set_of(m) ((struct intel_display_power_domain_set *)(void *)(m))
#define intel_display_power_get_if_enabled(i915, domain) parity_n1_power_get_if_enabled(domain)
#define intel_display_power_get_in_set_if_enabled(i915, set, domain) \
	(parity_n1_power_get_in_set_if_enabled((set), (domain)), true)
/* the put of this path is a statement macro: wrap it so it can sit in the loop's header */
static inline void parity_n1_power_put(struct drm_i915_private *i915,
	enum intel_display_power_domain domain, intel_wakeref_t wf)
{
	intel_display_power_put(i915, domain, wf);
}
#define with_intel_display_power_if_enabled(i915, domain, wf) \
	for ((wf) = parity_n1_power_get_if_enabled(domain); (wf) != 0; \
	     parity_n1_power_put((i915), (domain), (wf)), (wf) = 0)
#define intel_display_power_put_all_in_set(i915, set) parity_n1_power_put_all_in_set(set)
void parity_n1_power_put_all_in_set(struct intel_display_power_domain_set *set);

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


/* ---- the walks and helpers the readout text uses ---- */
#define HAS_TRANSCODER(i915, tr) ((tr) >= 0 && (tr) <= 3)          /* A..D on this platform */
#define for_each_cpu_transcoder_masked(i915, tr, mask) for ((tr) = 0; (tr) <= 3; (tr)++) for_each_if((mask) & BIT(tr))
static inline void drm_rect_init(struct drm_rect *r, int x, int y, int w, int h)
{ r->x1 = x; r->y1 = y; r->x2 = x + w; r->y2 = y + h; }
#define drm_crtc_wait_one_vblank(crtc) PARITY_LCD_STEP(parity_lcd_cur_i915, "drm_crtc_wait_one_vblank")

/* ---- subsystems this path does not port: each call is recorded, never a silent success ---- */
#define N1_STEP(name) PARITY_LCD_STEP(parity_lcd_cur_i915, name)
#define enabled_bigjoiner_pipes(i915, master, slave) do { *(master) = 0u; *(slave) = 0u; } while (0)
#define get_bigjoiner_master_pipe(pipe, master_pipes, slave_pipes) (pipe)
#define intel_bigjoiner_adjust_pipe_src(cs) ((void)0)
#define intel_bigjoiner_get_config(cs) ((void)0)
#define bxt_get_dsi_transcoder_state(crtc, cs, set) (false)         /* no DSI panel on this machine */
#define BXT_PHY_CTL(port) _MMIO(0)
#define bxt_ddi_phy_get_lane_lat_optim_mask(encoder) (0u)
/*
 * XXX: UNPORTED -- the readout halves of the subsystems this path does not program.  Each is a named step,
 * so a run that reaches one prints "UNRESOLVED step reached: <name>" at that exact point; what the
 * reference does there is written below so the work can be picked up without re-deriving it.
 *   intel_dsc_get_config: read DSS_CTL1/2 and the PPS registers of the transcoder into crtc_state->dsc
 *     (compression enable, slice count, bits per pixel).  This link carries uncompressed pixels.
 *   intel_vrr_get_config: read TRANS_VRR_CTL / VMIN / VMAX / FLIPLINE into crtc_state->vrr.  No variable
 *     refresh here (the panel is driven at its fixed mode).
 *   intel_ddi_mso_get_config: read the eDP multi-SO splitter fields of PIPE_MISC2 (stream splitter count,
 *     pixel overlap).  This panel is one stream.
 *   intel_ddi_is_audio_enabled: read TRANS_DDI_FUNC_CTL s AUDIO_OUTPUT_ENABLE for the transcoder.
 *   intel_hdmi_infoframes_enabled / _read_gcp_infoframe / intel_read_infoframe / intel_read_dp_sdp:
 *     read VIDEO_DIP_CTL and the DIP data words back and rebuild the frames the firmware is sending.
 *   intel_edp_fixup_vbt_bpp: compare the pipe bpp read out with the VBT s edp_bpp and correct the VBT copy.
 *   bdw_get_trans_port_sync_config: read TRANS_DDI_FUNC_CTL2 s port sync master select into the state.
 */
#define intel_dsc_get_config(cs) N1_STEP("intel_dsc_get_config")
#define intel_vrr_get_config(cs) N1_STEP("intel_vrr_get_config")
#define intel_ddi_mso_get_config(encoder, cs) N1_STEP("intel_ddi_mso_get_config")
#define intel_ddi_is_audio_enabled(i915, tr) (N1_STEP("intel_ddi_is_audio_enabled"), false)
#define intel_hdmi_infoframes_enabled(encoder, cs) (N1_STEP("intel_hdmi_infoframes_enabled"), 0u)
#define intel_hdmi_read_gcp_infoframe(encoder, cs) N1_STEP("intel_hdmi_read_gcp_infoframe")
#define intel_read_infoframe(encoder, cs, type, frame) N1_STEP("intel_read_infoframe")
#define intel_read_dp_sdp(encoder, cs, type) N1_STEP("intel_read_dp_sdp")
#define intel_edp_fixup_vbt_bpp(encoder, bpp) N1_STEP("intel_edp_fixup_vbt_bpp")
#define bdw_get_trans_port_sync_config(cs) N1_STEP("bdw_get_trans_port_sync_config")
/*
 * XXX: UNPORTED -- port sync (two transcoders driving one stream) and Type-C link reset.
 * This machine has neither: eDP-1 and HDMI-A-1 are combo PHYs and each stream uses one transcoder,
 * so is_trans_port_sync_master() is the reference's own false branch (master_transcoder INVALID) and
 * intel_tc_port_link_needs_reset() would, for a Type-C port, read the port's link state under
 * intel_tc_port_lock() and report whether the link must be re-trained before the takeover.
 *   pseudo: lock(dig_port->tc); ret = tc->link_refcount && tc->mode != TC_PORT_DISCONNECTED &&
 *           !icl_tc_phy_is_owned(dig_port); unlock(tc); return ret;
 * The entry of each is reported as a named step, so a run that ever reaches it says so.
 */
#define is_trans_port_sync_master(cs) (N1_STEP("is_trans_port_sync_master"), false)
#define intel_tc_port_link_needs_reset(dig_port) (N1_STEP("intel_tc_port_link_needs_reset"), false)


/* ---- prototypes of the generated readout functions the other units call (parity_n1_protos) ---- */
struct intel_crtc_state;
void intel_cpu_transcoder_get_m1_n1(struct intel_crtc *crtc, enum transcoder cpu_transcoder,
	struct intel_link_m_n *m_n);
void intel_cpu_transcoder_get_m2_n2(struct intel_crtc *crtc, enum transcoder cpu_transcoder,
	struct intel_link_m_n *m_n);
int intel_crtc_dotclock(const struct intel_crtc_state *pipe_config);
void intel_plane_disable_noatomic(struct intel_crtc *crtc, struct intel_plane *plane);
struct intel_shared_dpll *intel_get_shared_dpll_by_id(struct drm_i915_private *i915, enum intel_dpll_id id);
bool intel_dpll_get_hw_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll,
	struct intel_dpll_hw_state *hw_state);
int intel_dpll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state);
bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb, const struct skl_ddb_entry *entries,
	int num_entries, int ignore_idx);
u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915, const struct skl_ddb_entry *entry);

/* ---- the last callees of other subsystems: recorded steps ---- */
/*
 * XXX: UNPORTED -- the other readout halves reached from the encoder and crtc config.
 *   intel_color_get_config: read GAMMA_MODE / CSC_MODE and the LUT contents back into the state.
 *   intel_psr_get_config: read SRD_CTL / PSR2_CTL of the transcoder (panel self refresh is not used here).
 *   intel_audio_codec_get_config: read the audio ELD / rate from the codec side.
 *   intel_dp_sync_state: re-read the DPCD of a link the firmware trained and align intel_dp with it
 *     (link rate, lane count, training pattern state); this path trains the link itself in the re-light.
 *   intel_tc_port_sanitize_mode: a Type-C port only (this machine has none on the display outputs).
 *   icl_set_active_port_dpll: pick which of a Type-C port s PLLs the crtc uses (combo PHY has one).
 */
#define intel_color_get_config(cs) N1_STEP("intel_color_get_config")
#define intel_psr_get_config(encoder, cs) N1_STEP("intel_psr_get_config")
#define intel_audio_codec_get_config(encoder, cs) N1_STEP("intel_audio_codec_get_config")
#define intel_dp_sync_state(encoder, cs) N1_STEP("intel_dp_sync_state")
#define intel_tc_port_sanitize_mode(dig_port, cs) N1_STEP("intel_tc_port_sanitize_mode")
#define icl_set_active_port_dpll(cs, port_dpll_id) N1_STEP("icl_set_active_port_dpll")
#define intel_dpll_readout_hw_state parity_n1_dpll_readout_hw_state

/* the DBUF state the watermark readout writes: the device's own (parity_wm_glue.inc) */
#define intel_atomic_get_dbuf_state(state) (&parity_lcd_wm->new_dbuf)

/* the readout hands the crtc's own set; the helpers of lcd_modeset_compat.h take that set */
#define intel_display_power_put_all_in_set_mask(i915, set, mask) intel_display_power_put_mask_in_set((i915), (set), (mask))

/* the readout callees of parts this path does not program (IPS, the panel fitter, the scaler) */
/*
 * XXX: UNPORTED -- parts of the pipe this configuration never turns on.
 *   hsw_ips_get_config / hsw_ips_disable: IPS (panel power saving) is Haswell/Broadwell only.
 *   ilk_get_pfit_config: read PF_CTL / PF_WIN_POS / PF_WIN_SZ into crtc_state->pch_pfit (no scaling here).
 *   skl_scaler_get_config: read PS_CTRL / PS_WIN_POS / PS_WIN_SZ of both scalers into scaler_state.
 */
#define hsw_ips_get_config(cs) N1_STEP("hsw_ips_get_config")
#define ilk_get_pfit_config(cs) N1_STEP("ilk_get_pfit_config")
#define skl_scaler_get_config(cs) N1_STEP("skl_scaler_get_config")
/* intel_crtc.c's vblank wait reaches the DRM helper: this path has no DRM vblank layer */
#undef drm_crtc_wait_one_vblank
#define drm_crtc_wait_one_vblank(crtc) N1_STEP("drm_crtc_wait_one_vblank")

/* bigjoiner: one pipe per stream on this display */
#define intel_bigjoiner_num_pipes(cs) (1)
#define intel_bigjoiner_adjust_timings(cs, mode) ((void)0)
#define drm_mode_copy(dst, src) (*(dst) = *(src))

void intel_crtc_wait_for_next_vblank(struct intel_crtc *crtc);

#define drm_plane_mask(plane) (1u << 0)   /* one plane per crtc in this path */

/* the crtc has one plane in this path: the reference walk visits it */
#define drm_for_each_plane_mask(plane, dev, plane_mask) \
	for ((plane) = parity_n1_primary_plane(); (plane) != NULL; (plane) = NULL)
struct drm_plane *parity_n1_primary_plane(void);
#define to_intel_plane_state(x) ((struct intel_plane_state *)(x))
#define hsw_ips_disable(cs) (N1_STEP("hsw_ips_disable"), false)

/* the plane disable the readout path calls directly (our plane writer does it in the modeset) */
/* the plane disable of the takeover is this path own text (skl_plane_port.c) */
void parity_lcd_plane_disable_arm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);
#define intel_plane_disable_arm(plane, cs) parity_lcd_plane_disable_arm((plane), (cs))
#define intel_set_memory_cxsr(i915, enable) (N1_STEP("intel_set_memory_cxsr"), false)

#define is_power_of_2(n) ((n) != 0 && ((n) & ((n) - 1)) == 0)

u32 intel_adjusted_rate(const struct drm_rect *src, const struct drm_rect *dst, u32 rate);

#ifndef POWER_DOMAIN_PIPE_PANEL_FITTER
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) ((enum intel_display_power_domain)(POWER_DOMAIN_PIPE_PANEL_FITTER_A + (pipe)))
#endif

/*
 * The throw-away atomic state intel_crtc_disable_noatomic_begin() builds.  In this path the drm state
 * and the intel state are ONE object (to_intel_atomic_state() is the identity here), so the reference's
 * `struct drm_atomic_state` names our `struct intel_atomic_state`; the runner owns the storage.
 */
#define drm_atomic_state intel_atomic_state
struct intel_atomic_state;
struct intel_crtc_state *parity_n1_atomic_crtc_state(void *state, struct intel_crtc *crtc);
#define intel_atomic_get_crtc_state(state, crtc) parity_n1_atomic_crtc_state((state), (crtc))
struct drm_atomic_state *parity_n1_atomic_state(struct drm_device *dev);
#define drm_atomic_state_alloc(dev) parity_n1_atomic_state(dev)
#define drm_atomic_state_put(state) ((void)0)
#define drm_atomic_add_affected_connectors(state, crtc) (N1_STEP("drm_atomic_add_affected_connectors"), 0)
void intel_unreference_shared_dpll_crtc(const struct intel_crtc *crtc, const struct intel_shared_dpll *pll,
	struct intel_shared_dpll_state *shared_dpll_state);

/* forward declarations the generated prototypes need before their first use */
struct drm_modeset_acquire_ctx;
struct drm_atomic_state;
/* intel_bw.c / intel_cdclk.c / intel_pmdemand.c: the fields the takeover clears for the freed pipe */
struct intel_bw_state { unsigned int data_rate[I915_MAX_PIPES]; u8 num_active_planes[I915_MAX_PIPES];
	bool force_check_qgv; };   /* the QGV point is re-checked at the next bandwidth check */
struct intel_cdclk_state { int min_cdclk[I915_MAX_PIPES]; u8 min_voltage_level[I915_MAX_PIPES]; u8 active_pipes; };
struct intel_pmdemand_state { u8 active_pipes; };
#define to_intel_pmdemand_state(x) ((struct intel_pmdemand_state *)(x))
/* the global states the noatomic disable clears: this path keeps them in the modeset object */
#define to_intel_bw_state(x) ((struct intel_bw_state *)(x))
#define to_intel_cdclk_state(x) ((struct intel_cdclk_state *)(x))
#define intel_atomic_get_bw_state(state) ((struct intel_bw_state *)0)
#define intel_atomic_get_cdclk_state(state) ((struct intel_cdclk_state *)0)
#define __drm_atomic_helper_crtc_destroy_state(cs) N1_STEP("__drm_atomic_helper_crtc_destroy_state")
#define intel_crtc_free_hw_state(cs) N1_STEP("intel_crtc_free_hw_state")

/* the last callees of parts this path does not program */
#define intel_fbc_disable(crtc) N1_STEP("intel_fbc_disable")
#define intel_fbc_sanitize(i915) N1_STEP("intel_fbc_sanitize")
#define intel_update_watermarks(i915) N1_STEP("intel_update_watermarks")
#define intel_pmdemand_update_port_clock(i915, pmdemand, port, clock) N1_STEP("intel_pmdemand_update_port_clock")
/* XXX: UNPORTED -- the pm demand unit (intel_pmdemand.c) of display 14+; this display (13) has no
 * pmdemand programming, so the reference's update of the active-phys mask has nothing to write here.
 *   pseuda-code of the reference: phys_mask = pmdemand_state->active_combo_phys_mask;
 *   set/clear BIT(phy of encoder); store back.  The entry is reported as a named step. */
#define intel_pmdemand_update_phys_mask(i915, encoder, pmdemand, set) N1_STEP("intel_pmdemand_update_phys_mask")
void intel_crtc_state_reset(struct intel_crtc_state *crtc_state, struct intel_crtc *crtc);

#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif
/* drm_atomic_state_helper.h: the reference hands the uapi state and the crtc; here only the link matters */
#define __drm_atomic_helper_crtc_state_reset(state, crtc_base) ((state)->crtc = (crtc_base))

/* ---- the plane type the sanitize distinguishes, and the crtc's bit in a device-wide mask ---- */
#define DRM_PLANE_TYPE_PRIMARY 1
#define drm_crtc_mask(crtc) (1u << (unsigned)to_intel_crtc(crtc)->pipe)
#define IS_SANDYBRIDGE(i915) (0)

/* the uapi copy of the readout: in this path a mode is a value and a LUT blob a pointer (no properties) */
static inline int drm_atomic_set_mode_for_crtc(struct drm_crtc_state *state, const struct drm_display_mode *mode)
{
	state->mode = *mode;
	return 0;
}
#define drm_property_replace_blob(dst, src) (*(dst) = (src))

/*
 * XXX: UNPORTED -- the FIFO underrun reporting (intel_fifo_underrun.c) is an adaptation of this port: the
 * underrun interrupts are not enabled here, so the reference's per-crtc bookkeeping has no reader.
 *   pseudo-code of the reference: crtc->cpu_fifo_underrun_disabled = !enable;
 *                                 if (intel_has_pch_trancoder(i915, crtc->pipe))
 *                                         crtc->pch_fifo_underrun_disabled = !enable;
 * XXX: UNPORTED -- intel_crtc_state_dump() (intel_crtc_state_dump.c) prints the whole state for debugging.
 *   pseudo-code: print the pipe / transcoder names, the output types, the pipe and adjusted modes, the
 *   bpp / dithering, the link m_n, the DPLL hardware state and every plane's source and destination rects.
 * Both report a named step, so a run that reaches them says so.
 */
#define intel_init_fifo_underrun_reporting(i915, crtc, enable) N1_STEP("intel_init_fifo_underrun_reporting")
#define intel_crtc_state_dump(cs, state, context) N1_STEP("intel_crtc_state_dump")

/* ported elsewhere in this path: the DDI clock sanitize (intel_ddi_port.c) and the firmware notification
 * of a sanitized encoder (intel_opregion_port.c) */
void intel_ddi_sanitize_encoder_pll_mapping(struct intel_encoder *encoder);
int parity_opregion_notify_encoder(int port, int output_type, int enable);
#define intel_opregion_notify_encoder(encoder, enable) parity_opregion_notify_encoder((int)(encoder)->port, (encoder)->type, (enable))

/* ---- the plane walk of the readout: the registry's planes (one per crtc in this path) ---- */
struct intel_plane *parity_n1_plane_at(unsigned idx);
#define for_each_intel_plane(dev, plane) \
	for (unsigned _n1_pi = 0u; ((plane) = parity_n1_plane_at(_n1_pi)) != NULL; _n1_pi++)
#undef intel_crtc_for_pipe
#define intel_crtc_for_pipe(i915, pipe) parity_n1_crtc_for_pipe(pipe)
struct intel_crtc *parity_n1_crtc_for_pipe(enum pipe pipe);

/* linux/string_helpers.h */
#define str_enabled_disabled(v) ((v) ? "enabled" : "disabled")
#define str_yes_no(v) ((v) ? "yes" : "no")
#define str_on_off(v) ((v) ? "on" : "off")

/* the object masks of a readout: this path numbers its objects by index */
#define drm_connector_mask(connector) (1u << (connector)->index)
#define drm_encoder_mask(encoder) (1u << (encoder)->index)
#define intel_attached_encoder(connector) ((connector)->encoder)

/* generated elsewhere in this path (intel_display_port.c / intel_dpll_port.c) */
void intel_set_plane_visible(struct intel_crtc_state *crtc_state,
	struct intel_plane_state *plane_state, bool visible);
void intel_plane_fixup_bitmasks(struct intel_crtc_state *crtc_state);
bool intel_crtc_get_pipe_config(struct intel_crtc_state *crtc_state);
void intel_encoder_get_config(struct intel_encoder *encoder, struct intel_crtc_state *crtc_state);
void parity_n1_dpll_readout_hw_state(struct drm_i915_private *i915);
bool intel_ddi_connector_get_hw_state(struct intel_connector *connector);
/* the plane readout is static in its own unit; the registry binds it there (parity_plane_emit_glue.inc) */
bool (*parity_lcd_plane_get_hw_state(void))(struct intel_plane *plane, enum pipe *pipe);

/* ---- the fields the last part of the readout fills ---- */
#define bitmap_empty(bits, nbits) parity_n1_bitmap_empty(bits, nbits)
static inline bool parity_n1_bitmap_empty(const unsigned long *bits, unsigned nbits)
{
	unsigned i;

	for (i = 0u; i < (nbits + PARITY_BITS_PER_LONG - 1u) / PARITY_BITS_PER_LONG; i++)
		if (bits[i] != 0ul)
			return false;
	return true;
}

/* generated elsewhere in this path */
void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state, bool vrr_enable);
int intel_crtc_compute_min_cdclk(const struct intel_crtc_state *crtc_state);
void intel_bw_crtc_update(struct intel_bw_state *bw_state, const struct intel_crtc_state *crtc_state);
void intel_dpll_sanitize_state(struct drm_i915_private *i915);
void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state,
	struct intel_power_domain_mask *old_domains);
void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc, struct intel_power_domain_mask *domains);

/*
 * The watermark readout of this platform IS ported (skl_wm_get_hw_state, skl_watermark_port.c); the
 * reference reaches it through the device's watermark ops, which this path does not carry.
 */
/* the watermark readout is static in its own unit: the alias below reaches it from there */
void parity_lcd_wm_get_hw_state(struct drm_i915_private *i915);
#define intel_wm_get_hw_state(i915) parity_lcd_wm_get_hw_state(i915)

/*
 * Callees whose reference body is DECIDED on this machine -- the condition of the reference is named, and the
 * step is recorded as decided (not as an unresolved step):
 *   intel_pch_sanitize(): its body runs only for HAS_PCH_IBX(); this machine has an ADL-P PCH.
 *   intel_pmdemand_init_pmdemand_params(): the pm demand unit exists from display 14; this display is 13.
 * XXX: UNPORTED -- drm_crtc_vblank_reset() belongs to the DRM vblank layer, which this path does not have
 *   (the flip path of this port waits on the pipe's own vblank interrupt instead).
 *   pseudo-code of the reference: under the vblank spinlock, mark the pipe's vblank disabled and forget
 *   the last recorded vblank count and timestamp.
 * XXX: UNPORTED -- intel_power_domains_sanitize_state() drops the power wells no crtc claims after the
 *   readout.  This path holds its wells through its own domain set and releases them in the takeover, so
 *   nothing here would be released a second time.
 *   pseudo-code of the reference: for each power well in reverse order: if the well is enabled and
 *   !intel_power_well_is_always_on(well) and its domain count is 0, disable it.
 */
#define intel_pch_sanitize(i915) N1_STEP("(decided) intel_pch_sanitize: HAS_PCH_IBX is false on this PCH")
#define intel_pmdemand_init_pmdemand_params(i915, state) N1_STEP("(decided) intel_pmdemand_init_pmdemand_params: display 13 has no pm demand unit")
#define drm_crtc_vblank_reset(crtc) N1_STEP("drm_crtc_vblank_reset")
#define intel_power_domains_sanitize_state(i915) N1_STEP("intel_power_domains_sanitize_state")

/* what the rest of this path lends the readout (its display hooks and the bound screen objects) */
struct intel_display_funcs;
const struct intel_display_funcs *parity_lcd_ms_display_funcs(void);
struct intel_color_funcs;
const struct intel_color_funcs *parity_lcd_ms_color_funcs(void);
struct drm_crtc_funcs;
const struct drm_crtc_funcs *parity_lcd_ms_crtc_funcs(void);
struct intel_encoder *parity_lcd_ms_bound_encoder(void);
struct intel_connector *parity_lcd_ms_bound_connector(void);
void parity_lcd_dpll_pool_bind(struct drm_i915_private *i915);
void parity_lcd_ms_bind_readout(struct intel_encoder *encoder);
void intel_modeset_setup_hw_state(struct drm_i915_private *i915, struct drm_modeset_acquire_ctx *ctx);
/* Linux numbering, returned negative (lcd_compat.h defines EINVAL the same way) */
#ifndef EBUSY
#define EBUSY 16
#endif
#ifndef ENXIO
#define ENXIO 6
#endif
/* drm_mode.h: the connector kinds this path names */
#ifndef DRM_MODE_CONNECTOR_VGA
#define DRM_MODE_CONNECTOR_VGA 1
#endif
#ifndef DRM_MODE_CONNECTOR_eDP
#define DRM_MODE_CONNECTOR_eDP 14
#define DRM_MODE_CONNECTOR_HDMIA 11
#define DRM_MODE_CONNECTOR_DisplayPort 10
#endif

/* intel_display_types.h: the encoders that carry a DP link (the DDI case asks the port s own output register) */
#define intel_encoder_is_dp(encoder) ((encoder)->type == INTEL_OUTPUT_DP || (encoder)->type == INTEL_OUTPUT_EDP)
/*
 * XXX: UNPORTED -- the DSI encoder ports (intel_dsi_encoder_ports, icl_dsi.c).  This machine has no DSI
 * panel, and the reference reaches this only for INTEL_OUTPUT_DSI.
 *   pseudo-code of the reference: return the port mask of the DSI encoder (BIT(port) of each of its ports).
 */
#define intel_dsi_encoder_ports(encoder) (N1_STEP("intel_dsi_encoder_ports"), 0u)
/* drm_print.h: this path has one log */
#ifndef drm_notice
#define drm_notice(dev, fmt, ...) drm_dbg_kms(dev, fmt, ##__VA_ARGS__)
#endif

#endif /* PARITY_N1_COMPAT_H */

/*
 * Re-asserted on EVERY include of this header: the headers a unit includes later (lcd_wm_compat.h) undef
 * these walks for their own path.  A unit that wants the readout's walks lists n1_compat.h last.
 */
#undef for_each_intel_crtc_in_pipe_mask
#define for_each_intel_crtc_in_pipe_mask(dev, crtc, mask) \
	for_each_intel_crtc(dev, crtc) for_each_if((mask) & BIT((crtc)->pipe))
/* the planes of ONE crtc: the registry holds one primary plane per pipe */
#undef for_each_intel_plane_on_crtc
#define for_each_intel_plane_on_crtc(dev, crtc, plane) \
	for ((plane) = parity_n1_plane_at((unsigned)(crtc)->pipe); (plane) != NULL; (plane) = NULL)
#undef intel_atomic_get_crtc_state
#define intel_atomic_get_crtc_state(state, crtc) parity_n1_atomic_crtc_state((state), (crtc))

