/*
 * WS031 Linux-parity — DP link-layer environment of the LCD modeset bodies (link training, sink power,
 * source OUI) on top of lcd_compat.h + lcd_modeset_compat.h.  zedBSD project code.
 *
 * The sink is reached through the ops' DPCD hooks: on the real GPU they are the resident eDP's AUX
 * channel (parity/dp -- the reference's drm_dp_dpcd_read / _write run there, with its retries and
 * locks); in the GPU-free tests they are the sink model.  The helper names that the DP directory
 * already exports are mapped to local wrappers so that the two do not collide at link time.
 */
#ifndef PARITY_LCD_DP_COMPAT_H
#define PARITY_LCD_DP_COMPAT_H

#include "lcd_dp_phy_enum.h"            /* reference, extracted: enum drm_dp_phy */

#include "lcd_mreg_link_training.h"     /* reference, extracted: the TRAIN_*_FMT / _ARGS log macros */
#include "lcd_dp_helper_inlines.h"      /* reference, extracted: drm_dp_tps3_supported, drm_dp_tps4_supported, drm_dp_is_branch */
#undef ERANGE
#define ERANGE 34                       /* Linux numbering, returned negative */
#define USEC_PER_MSEC 1000L
#define hweight8(x) ((unsigned int)__builtin_popcount((unsigned int)(x) & 0xffu))
#define ilog2(x) (31 - __builtin_clz((unsigned int)(x)))
/* jiffies: only intel_edp_init_source_oui() uses it, to remember WHEN the OUI was written (the wait that
 * consumes it, intel_dp_wait_source_oui(), belongs to the backlight / PPS side) */
#define jiffies (0ul)
#undef EIO
#define EIO 5                           /* Linux numbering, returned negative */
typedef long ssize_t_lcd;
#define ssize_t ssize_t_lcd

/* ---- DPCD access [ops] ---- */
static inline long parity_lcd_dpcd_read(unsigned int offset, void *buffer, size_t size)
{
	return parity_lcd_cur_i915->emit->dpcd_read != 0 ?
		parity_lcd_cur_i915->emit->dpcd_read(parity_lcd_cur_i915->emit->ctx, offset, buffer, size) : -EIO;
}
static inline long parity_lcd_dpcd_write(unsigned int offset, const void *buffer, size_t size)
{
	return parity_lcd_cur_i915->emit->dpcd_write != 0 ?
		parity_lcd_cur_i915->emit->dpcd_write(parity_lcd_cur_i915->emit->ctx, offset, buffer, size) : -EIO;
}
#define drm_dp_dpcd_read(aux, offset, buffer, size) parity_lcd_dpcd_read((offset), (buffer), (size))
#define drm_dp_dpcd_write(aux, offset, buffer, size) parity_lcd_dpcd_write((offset), (buffer), (size))
static inline long parity_lcd_dpcd_readb(unsigned int offset, u8 *valuep) { return parity_lcd_dpcd_read(offset, valuep, 1); }
static inline long parity_lcd_dpcd_writeb(unsigned int offset, u8 value) { return parity_lcd_dpcd_write(offset, &value, 1); }
#define drm_dp_dpcd_readb(aux, offset, valuep) parity_lcd_dpcd_readb((offset), (valuep))
#define drm_dp_dpcd_writeb(aux, offset, value) parity_lcd_dpcd_writeb((offset), (value))
/* drm_dp_dpcd_probe(): a one-byte read whose only result is "the sink answered" */
static inline int parity_lcd_dpcd_probe(unsigned int offset)
{
	u8 byte;
	long ret = parity_lcd_dpcd_read(offset, &byte, 1);
	return ret == 1 ? 0 : (ret < 0 ? (int)ret : -EIO);
}
#define drm_dp_dpcd_probe(aux, offset) parity_lcd_dpcd_probe(offset)
/* drm_dp_read_dpcd_caps(): executed by the DP directory's own copy of the reference function */
#define drm_dp_read_dpcd_caps(aux, dpcd) (parity_lcd_cur_i915->emit->read_dpcd_caps != 0 ? \
	parity_lcd_cur_i915->emit->read_dpcd_caps(parity_lcd_cur_i915->emit->ctx, (dpcd)) : -EIO)

/* ---- link-training log lines: the format string reaches the debug / error hook ---- */
#define lt_dbg(_intel_dp, _dp_phy, _format, ...) do { if (0) (void)parity_lcd_fmtcheck(_format, ##__VA_ARGS__); parity_lcd_debug("link training: " _format); } while (0)
#define lt_err(_intel_dp, _dp_phy, _format, ...) do { if (0) (void)parity_lcd_fmtcheck(_format, ##__VA_ARGS__); parity_lcd_error("link training: " _format); } while (0)
void parity_lcd_debug(const char *what);

/* ---- answers fixed by the configuration [fixed] ---- */
#define intel_digital_port_connected(encoder) (1)       /* internal panel: only selects the log level of lt_err */
#define dp_to_lspcon(intel_dp) (&dp_to_dig_port(intel_dp)->lspcon)
#define lspcon_resume(dig_port) ((void)0)               /* no LSPCON behind an eDP port (lspcon.active stays false) */
#define lspcon_wait_pcon_mode(lspcon) ((void)0)
#define IS_G4X(i915) 0
/*
 * Link-training FALLBACK is not ported.  When the reference asks for it, that fact is recorded as an
 * error ("fallback requested") and the modeset-retry work is a named step: training is never turned
 * into a success, and no other rate / lane count is tried behind the caller's back.
 */
#define intel_dp_get_link_train_fallback_values(intel_dp, rate, lanes) \
	(parity_lcd_error("link training failed: the reference requests a FALLBACK (lower rate / lane count), which is not ported\n"), -1)
#define queue_work(wq, work) PARITY_LCD_STEP(parity_lcd_cur_i915, "queue_work(modeset_retry_work)")
#define intel_dp_128b132b_link_train(intel_dp, cs, lttpr_count) (PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_dp_128b132b_link_train"), false)
#define intel_dp_128b132b_intra_hop(intel_dp, cs) (0)   /* only evaluated for a UHBR rate */

/* prototypes of kept non-static reference functions called across the generated files */
bool intel_dp_is_edp(struct intel_dp *intel_dp);
bool intel_dp_source_supports_tps3(struct drm_i915_private *i915);
bool intel_dp_source_supports_tps4(struct drm_i915_private *i915);
void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock, u8 *link_bw, u8 *rate_select);
void intel_dp_set_link_params(struct intel_dp *intel_dp, int link_rate, int lane_count);
void intel_dp_set_power(struct intel_dp *intel_dp, u8 mode);
void intel_dp_start_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
void intel_dp_stop_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
void intel_dp_program_link_training_pattern(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, enum drm_dp_phy dp_phy, u8 dp_train_pat);
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state);
bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
u8 drm_dp_get_adjust_tx_ffe_preset(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
int drm_dp_read_clock_recovery_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, bool uhbr);
int drm_dp_read_channel_eq_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, bool uhbr);
int drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux, enum drm_dp_phy dp_phy, u8 link_status[DP_LINK_STATUS_SIZE]);
int drm_dp_lttpr_count(const u8 cap[DP_LTTPR_COMMON_CAP_SIZE]);
bool drm_dp_lttpr_voltage_swing_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
bool drm_dp_lttpr_pre_emphasis_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
int drm_dp_read_lttpr_common_caps(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 caps[DP_LTTPR_COMMON_CAP_SIZE]);
int drm_dp_read_lttpr_phy_caps(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
const char *drm_dp_phy_name(enum drm_dp_phy dp_phy);
u8 drm_dp_link_rate_to_bw_code(int link_rate);

#endif /* PARITY_LCD_DP_COMPAT_H */
