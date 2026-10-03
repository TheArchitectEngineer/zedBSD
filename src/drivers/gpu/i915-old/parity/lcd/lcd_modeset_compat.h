/*
 * WS031 Linux-parity — the environment of the reference's modeset BODIES (PLL, DDI clock, PHY signal
 * levels, lanes, link training hooks, transcoder enable / disable, pipe words, backlight) on top of
 * lcd_compat.h.  zedBSD project code.  Everything that touches the outside goes through the
 * parity_lcd_ops.h hooks of the device (i915->emit), so the same text runs on the register / sink
 * model and on the real GPU.
 *
 * Three kinds of definition live here, and each line says which it is:
 *   [ops]    the reference's primitive, carried by an ops hook (register, wait, sleep, lock, power);
 *   [fixed]  an answer fixed by the supported configuration (ADL-P, combo PHY port A/B, eDP / DP SST,
 *            one pipe, no big joiner, no MST, no DSC) -- the branch that would need more is refused
 *            by the glue before the reference code runs, or is a named step;
 *   [check]  a state checker of the reference that only reads back and warns (no effect on hardware).
 */
#ifndef PARITY_LCD_MODESET_COMPAT_H
#define PARITY_LCD_MODESET_COMPAT_H

/* ---- registers, waits, time [ops] ---- */
#define intel_de_read(i915, r) ((i915)->emit->read32 != 0 ? (i915)->emit->read32((i915)->emit->ctx, (r).reg) : 0u)
#define intel_de_read_fw(i915, r) intel_de_read(i915, r)
#ifndef intel_de_write_fw
#define intel_de_write_fw(i915, r, v) intel_de_write(i915, r, v)
#endif
#define PARITY_LCD_WAIT(i915, r, mask, value, ms) ((i915)->emit->wait_reg != 0 ? (i915)->emit->wait_reg((i915)->emit->ctx, (r).reg, (mask), (value), (ms)) : 0)
#define intel_de_wait_for_set(i915, r, mask, ms) PARITY_LCD_WAIT(i915, r, mask, mask, ms)
#define intel_de_wait_for_clear(i915, r, mask, ms) PARITY_LCD_WAIT(i915, r, mask, 0u, ms)
#define intel_de_wait_for_register(i915, r, mask, value, ms) PARITY_LCD_WAIT(i915, r, mask, value, ms)
#define i915_mmio_reg_valid(r) ((r).reg != 0u)
#define i915_mmio_reg_offset(r) ((r).reg)
#define i915_mmio_reg_equal(a, b) ((a).reg == (b).reg)
/* the sleeps and locks name the device through the pointer the glue sets before it calls in */
extern struct drm_i915_private *parity_lcd_cur_i915;
#define usleep_range(min_us, max_us) do { if (parity_lcd_cur_i915->emit->usleep != 0) parity_lcd_cur_i915->emit->usleep(parity_lcd_cur_i915->emit->ctx, (min_us)); } while (0)
#define msleep(ms) usleep_range((ms) * 1000u, (ms) * 1000u)
#define udelay(us) do { if (parity_lcd_cur_i915->emit->udelay != 0) parity_lcd_cur_i915->emit->udelay(parity_lcd_cur_i915->emit->ctx, (us)); } while (0)
/*
 * _wait_for(COND, US, Wmin, Wmax) / wait_for_us / wait_for (i915_utils.h): poll COND with growing sleeps
 * until the budget is spent.  The reference measures the budget in jiffies; here it is the SUM of the
 * sleeps requested (the backend's sleep decides how long that really is -- never shorter).  COND is
 * checked once more after the budget is spent, as in the reference.
 */
#define _wait_for(COND, US, Wmin, Wmax) ({ \
	long wait_left__ = (long)(US); long wait__ = (Wmin); int ret__; \
	for (;;) { \
		const bool expired__ = wait_left__ <= 0; \
		if (COND) { ret__ = 0; break; } \
		if (expired__) { ret__ = -ETIMEDOUT; break; } \
		usleep_range(wait__, wait__ * 2); wait_left__ -= wait__; \
		if (wait__ < (Wmax)) wait__ <<= 1; \
	} \
	ret__; })
#define wait_for(COND, MS) _wait_for((COND), (MS) * 1000, 10, 1000)
#define wait_for_us(COND, US) _wait_for((COND), (US), 10, 10)
#undef ETIMEDOUT
#define ETIMEDOUT 110                   /* Linux numbering, returned negative */

/* ---- locks [ops] ---- */
struct mutex { int which; };            /* PARITY_LCD_LOCK_* */
#define mutex_lock(m) do { if (parity_lcd_cur_i915->emit->lock != 0) parity_lcd_cur_i915->emit->lock(parity_lcd_cur_i915->emit->ctx, (m)->which, 1); } while (0)
#define mutex_unlock(m) do { if (parity_lcd_cur_i915->emit->lock != 0) parity_lcd_cur_i915->emit->lock(parity_lcd_cur_i915->emit->ctx, (m)->which, 0); } while (0)

/* ---- display power [ops] ---- */
#define intel_display_power_get(i915, domain) ((i915)->emit->power_get != 0 ? (i915)->emit->power_get((i915)->emit->ctx, (int)(domain)) : 1)
#define intel_display_power_put_async_delay(i915, domain, wakeref, delay_ms) (i915)->emit->power_put_async((i915)->emit->ctx, (int)(domain), (wakeref), (delay_ms))
#define intel_display_power_put(i915, domain, wakeref) do { if ((i915)->emit->power_put != 0) (i915)->emit->power_put((i915)->emit->ctx, (int)(domain), (wakeref)); } while (0)
#define fetch_and_zero(ptr) ({ __typeof__(*(ptr)) _v = *(ptr); *(ptr) = 0; _v; })
/* intel_display_power.c maps an AUX channel to its legacy power domain through the platform's port-domain
 * table; for display version 12-13 that table puts AUX channel n (n = A..) at POWER_DOMAIN_AUX_A + n [fixed] */
#define intel_display_power_legacy_aux_domain(i915, aux_ch) ((enum intel_display_power_domain)(POWER_DOMAIN_AUX_A + (int)(aux_ch)))
#define intel_display_power_tbt_aux_domain(i915, aux_ch) POWER_DOMAIN_INVALID          /* Type-C only */
#define intel_display_power_aux_io_domain(i915, aux_ch) ((enum intel_display_power_domain)(POWER_DOMAIN_AUX_IO_A + (int)(aux_ch)))
#define intel_psr_needs_aux_io_power(encoder, crtc_state) (0)      /* [fixed] PSR is not enabled (crtc_state->has_psr stays false) */

/* ---- panel power sequencer: the resident eDP executes these [ops] ---- */
#define PARITY_LCD_PANEL(intel_dp, op) (parity_lcd_cur_i915->emit->panel != 0 ? parity_lcd_cur_i915->emit->panel(parity_lcd_cur_i915->emit->ctx, (op)) : 0)
#define intel_pps_on(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_ON))
#define intel_pps_off(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_OFF))
#define intel_pps_vdd_on(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_VDD_ON))
#define intel_pps_vdd_off_sync(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_VDD_OFF_SYNC))
#define intel_pps_backlight_on(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_BACKLIGHT_ON))
#define intel_pps_backlight_off(intel_dp) ((void)PARITY_LCD_PANEL(intel_dp, PARITY_LCD_PANEL_BACKLIGHT_OFF))

/* ---- platform answers [fixed] ---- */
#define HAS_GMCH(i915) 0
#define BUILD_BUG_ON_ZERO(e) 0                /* a compile-time range check of the reference's register macros */
#define __is_constexpr(x) 1
static inline const char *str_on_off(bool v) { return v ? "on" : "off"; }
static inline const char *str_enable_disable(bool v) { return v ? "enable" : "disable"; }
#include "lcd_plane_types.h"            /* reference, extracted: enum plane_id */
u8 icl_hdr_plane_mask(void);
#define IS_I830(i915) 0
#define IS_PLATFORM(i915, p) 0
/* intel_quirks.c: no entry of intel_quirks[] names PCI device 0x46a8 (they are 0x0046 .. 0x3185), and the DMI
 * quirks name other machines -- so no quirk is set on the target */
#define intel_has_quirk(i915, quirk) (0)
#define intel_crtc_bigjoiner_slave_pipes(crtc_state) (0)
#define for_each_intel_crtc_in_pipe_mask(dev, crtc, mask) for ((crtc) = 0; (mask) != 0 && (crtc) != 0; (crtc) = 0)   /* no big-joiner slave pipes */
#define intel_dp_mst_is_master_trans(crtc_state) (0)
#define intel_tc_port_in_dp_alt_mode(dig_port) (0)
#define intel_tc_port_in_legacy_mode(dig_port) (0)
#define intel_crtc_pch_transcoder(crtc) ((enum pipe)0)             /* PCH transcoders: not on this platform */
/* intel_ddi_hdmi_level() is the reference's own text now (intel_ddi_port.c); the VBT level shift it asks for:
 * ADAPTATION -- the VBT child's hdmi_level_shifter_value is not read here, so the buffer-translation table's
 * own default entry is used, which is what the reference does when the VBT has no level shift. */
int parity_lcd_hdmi_level_shift(void);          /* the VBT value of this port (intel_ddi_port.c glue) */
#define intel_bios_hdmi_level_shift(devdata) parity_lcd_hdmi_level_shift()

/* DMC: which firmware ids are loaded is the DMC loader's knowledge, handed over in i915->display.dmc.fw_mask [ops-like] */
#define has_dmc_id_fw(i915, dmc_id) ((((i915)->display.dmc.fw_mask) >> (dmc_id)) & 1u)

/* backlight: runtime info and switches fixed by the configuration */
#define DISPLAY_RUNTIME_INFO(i915) (&(i915)->display.runtime)
#define KHz(x) (1000 * (x))
#ifndef U16_MAX
#define U16_MAX ((u16)0xffff)
#define U32_MAX ((u32)0xffffffffu)
#endif
#include "lcd_pch_enum.h"               /* reference, extracted: enum intel_pch */
#define INTEL_PCH_TYPE(i915) PCH_ADP    /* [fixed] the PCH the probe identified on the target (Alder Lake PCH) */
#undef ENODEV
#define ENODEV 19                       /* Linux numbering, returned negative */
#ifndef WARN_ON
#define WARN_ON(cond) ({ int _w = !!(cond); if (_w) parity_lcd_error("WARN_ON(" #cond ")\n"); _w; })
#endif
#define clamp(val, lo, hi) ({ __typeof__(val) _v = (val); __typeof__(val) _l = (lo); __typeof__(val) _h = (hi); _v < _l ? _l : (_v > _h ? _h : _v); })
#define clamp_t(type, val, lo, hi) ({ type _v = (type)(val); type _l = (type)(lo); type _h = (type)(hi); _v < _l ? _l : (_v > _h ? _h : _v); })
#define DIV_ROUND_CLOSEST_ULL(x, d) ((u64)(((u64)(x) + ((u64)(d) / 2u)) / (u64)(d)))
#define DRM_SWITCH_POWER_CHANGING 3
#define FB_BLANK_UNBLANK 0
#define FB_BLANK_POWERDOWN 4

/* ---- state checkers [check] ---- */
#define assert_shared_dpll_enabled(i915, pll) ((void)0)
#define assert_planes_disabled(crtc) ((void)0)
#define assert_pll_enabled(i915, pipe) ((void)0)
#define assert_dsi_pll_enabled(i915) ((void)0)
#define assert_fdi_rx_pll_enabled(i915, pipe) ((void)0)
#define assert_fdi_tx_pll_enabled(i915, pipe) ((void)0)

#include "lcd_mreg_reg_defs.h"          /* reference, extracted macros: closure of the roots in tools/port_lcd_modeset.json */
#include "lcd_mreg_display_reg_defs.h"
#include "lcd_mreg_display_device.h"
#include "lcd_mreg_display.h"
#include "lcd_mreg_i915_reg.h"
#include "lcd_mreg_combo_phy.h"
#include "lcd_mreg_vdsc.h"
#include "lcd_mreg_cx0.h"
#include "lcd_mreg_drm_dp.h"
#include "lcd_power_domain_enum.h"      /* reference, extracted: enum intel_display_power_domain */
#include "lcd_buf_trans_types.h"        /* reference, extracted: the DDI buffer translation entry types */
#include "lcd_link_training_inlines.h"  /* reference, extracted: intel_dp_training_pattern_symbol */
#include "lcd_dpll_id_enum.h"           /* reference, extracted: enum intel_dpll_id */

/* ---- shared DPLL objects: the members the kept functions use (intel_dpll_mgr.h) ---- */
typedef int intel_wakeref_t;
void intel_display_power_get_in_set(struct drm_i915_private *i915, struct intel_display_power_domain_set *power_domain_set,
	enum intel_display_power_domain domain);
void intel_display_power_put_mask_in_set(struct drm_i915_private *i915, struct intel_display_power_domain_set *power_domain_set,
	struct intel_power_domain_mask *mask);
#define __maybe_unused __attribute__((unused))
struct intel_shared_dpll;
struct intel_shared_dpll_funcs {
	bool (*get_hw_state)(struct drm_i915_private *i915, struct intel_shared_dpll *pll,
		struct intel_dpll_hw_state *hw_state);
	int (*get_freq)(struct drm_i915_private *i915, const struct intel_shared_dpll *pll,
		const struct intel_dpll_hw_state *pll_state);
	void (*enable)(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
	void (*disable)(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
};
struct dpll_info {
	const char *name;
	const struct intel_shared_dpll_funcs *funcs;
	enum intel_dpll_id id;
	int power_domain;                   /* 0 = none (the reference's adlp_plls[] sets none) */
};
/* intel_dpll_mgr.h: the per-PLL part of the atomic state, and the object the device keeps */
struct intel_shared_dpll_state { u8 pipe_mask; struct intel_dpll_hw_state hw_state; };
struct intel_shared_dpll {
	struct intel_shared_dpll_state state;
	u8 active_mask;
	bool on;
	const struct dpll_info *info;
	intel_wakeref_t wakeref;
	enum intel_dpll_id index;       /* its place in the device's pool (shared_dplls[]) */
};

/* prototypes of kept non-static reference functions that are called across the generated files */
bool intel_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy);
void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);
void intel_color_load_luts(const struct intel_crtc_state *crtc_state);
void intel_color_commit_noarm(const struct intel_crtc_state *crtc_state);
void intel_color_commit_arm(const struct intel_crtc_state *crtc_state);
void intel_backlight_enable(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void intel_backlight_disable(const struct drm_connector_state *old_conn_state);
void intel_dmc_disable_pipe(struct drm_i915_private *i915, enum pipe pipe);
void intel_dp_set_infoframes(struct intel_encoder *encoder, bool enable, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
enum intel_display_power_domain intel_aux_power_domain(struct intel_digital_port *dig_port);
void intel_enable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_disable_shared_dpll(const struct intel_crtc_state *crtc_state);
void intel_enable_transcoder(const struct intel_crtc_state *new_crtc_state);
void intel_disable_transcoder(const struct intel_crtc_state *old_crtc_state);
void intel_ddi_enable_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_clock(struct intel_encoder *encoder);
void intel_ddi_enable_transcoder_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_transcoder_clock(const struct intel_crtc_state *crtc_state);
void intel_combo_phy_power_up_lanes(struct drm_i915_private *dev_priv, enum phy phy, bool is_dsi, int lane_count, bool lane_reversal);
bool is_hobl_buf_trans(const struct intel_ddi_buf_trans *table);
void intel_wait_for_pipe_scanline_moving(struct intel_crtc *crtc);
void intel_wait_for_pipe_scanline_stopped(struct intel_crtc *crtc);
i915_reg_t dp_tp_ctl_reg(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
i915_reg_t dp_tp_status_reg(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);





/* ---- E-123: the shared-DPLL allocation (intel_find_shared_dpll and its reference counting) ---- */
struct intel_shared_dpll_state *parity_lcd_shared_dpll_state(void);   /* the atomic state's shared_dpll[] */
#define intel_atomic_get_shared_dpll_state(state) parity_lcd_shared_dpll_state()
#define for_each_set_bit(bit, addr, size) for ((bit) = 0; (bit) < (int)(size); (bit)++) for_each_if(*(addr) & (1ul << (bit)))
#ifndef fls
#define fls(x) ((x) ? 32 - __builtin_clz((unsigned int)(x)) : 0)
#endif
/* intel_dpll_mgr.h: the walk over the device's pool */
#define for_each_shared_dpll(i915, pll, id) for ((id) = 0; (id) < (i915)->display.dpll.num_shared_dpll && ((pll) = &(i915)->display.dpll.shared_dplls[(id)]); (id)++)

/* ---- E-123: what the generated HDMI text (intel_ddi_port.c, intel_hdmi_mode_port.c) needs ---- */
/* the platform branches of the HDMI enable that DISPLAY_VER 13 never takes */
#define has_buf_trans_select(i915) (0)
#define hsw_prepare_hdmi_ddi_buffers(encoder, cs) ((void)0)
#define mtl_ddi_enable_d2d(encoder) ((void)0)
#define gen9_chicken_trans_reg_by_port(i915, port) CHICKEN_TRANS(0)
/* the sink-side halves that need DDC: recorded steps (the SCDC path is only reached when the sink supports it,
 * the dual-mode adaptor path only with an adaptor present -- neither is the case here) */
/*
 * XXX: UNPORTED -- SCDC, the HDMI 2.0 sink-side status channel.  It is reached only above 340 MHz TMDS
 * (scrambling / clock ratio); the modes this path drives stay below that, and the sink's DDC does not answer
 * on this machine anyway (E-123: the same with Linux).
 *   pseudo (both): read SCDC offset 0x20 (TMDS config) over the sink's DDC, set or clear
 *   SCDC_SCRAMBLING_ENABLE / SCDC_TMDS_BIT_CLOCK_RATIO_BY_40, write it back, and report whether the write
 *   was acknowledged.
 */
#define drm_scdc_set_high_tmds_clock_ratio(connector, set) (PARITY_LCD_STEP(parity_lcd_cur_i915, "drm_scdc_set_high_tmds_clock_ratio"), false)
#define drm_scdc_set_scrambling(connector, enable) (PARITY_LCD_STEP(parity_lcd_cur_i915, "drm_scdc_set_scrambling"), false)
/*
 * XXX: UNPORTED -- a DP++ (dual-mode) adapter's TMDS output switch.  The HDMI port here is a native HDMI
 * output, not a DP++ adapter.
 *   pseudo: for a type 2 adapter write DP_DUAL_MODE_TMDS_OEN over the adapter's DDC (0 = output on).
 */
#define drm_dp_dual_mode_set_tmds_output(drm, type, ddc, enable) PARITY_LCD_STEP(parity_lcd_cur_i915, "drm_dp_dual_mode_set_tmds_output")
/* the infoframe writes: this path runs with has_infoframe false, so the reference returns before them */
/*
 * XXX: UNPORTED -- the HDMI general control packet (deep colour / default phase).  This path sends 8 bpc
 * without deep colour, where the reference writes nothing.
 *   pseudo: if the crtc state asks for GCP, write VIDEO_DIP_GCP(transcoder) with GCP_COLOR_INDICATION /
 *   GCP_DEFAULT_PHASE_ENABLE and enable the GCP DIP in VIDEO_DIP_CTL.
 */
#define intel_hdmi_set_gcp_infoframe(encoder, cs, conn) (PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_hdmi_set_gcp_infoframe"), false)
/*
 * XXX: UNPORTED -- the AVI / SPD / vendor infoframes an HDMI sink reads for aspect ratio, colorimetry and
 * source identification.  A sink shows a correct picture without them (E-123: the photograph proves it), and
 * this path has no EDID to derive them from on this machine.
 *   pseudo: build the frame (drm_hdmi_avi_infoframe_from_display_mode + quantisation range), pack it, then
 *   write it word by word into VIDEO_DIP_DATA of the transcoder with the type selected in VIDEO_DIP_CTL and
 *   enable that frame's send bit.
 */
#define intel_write_infoframe(encoder, cs, type, frame) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_write_infoframe")
#define HDMI_INFOFRAME_TYPE_AVI 0x82
#define HDMI_INFOFRAME_TYPE_SPD 0x83
#define HDMI_INFOFRAME_TYPE_VENDOR 0x81
#define HDMI_INFOFRAME_TYPE_DRM 0x87
#define str_yes_no(v) ((v) ? "yes" : "no")
/* the WRPLL calculation's own diagnostics (i915_utils.h / linux/kernel.h) */
#define WARN(cond, fmt) ({ int _w = !!(cond); if (_w) parity_lcd_error(fmt); _w; })
#define abs(x) ((x) < 0 ? -(x) : (x))
#define intel_hdmi_to_i915(hdmi) parity_lcd_cur_i915
/* the two the HDMI text of intel_ddi_port.c calls (intel_hdmi_mode_port.c) */
void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);
bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder, struct drm_connector *connector,
	bool high_tmds_clock_ratio, bool scrambling);
/* the DISPLAY_VER >= 14 (MTL) half of intel_enable_ddi_hdmi, which this platform never takes */
#define mtl_get_port_width(lane_count) (0u)
#define XELPDP_PORT_WIDTH(width) (0u)
#define XELPDP_PORT_WIDTH_MASK 0u
#define XELPDP_PORT_REVERSAL 0u


/* E-124 (N1): what the readout / sanitize / takeover text needs (every unit of this path sees it) */
/* the readout touches the plane objects too (E-124) */
#include "lcd_plane_compat.h"
#include "n1_compat.h"

#endif /* PARITY_LCD_MODESET_COMPAT_H */
