/*
 * WS031 Linux-parity — one-screen modeset of the internal panel: prepare, enable (the reference's
 * hsw_crtc_enable with the DDI / DP callees below it), plane update, plane disable, disable (the
 * reference's hsw_crtc_disable).  zedBSD-facing surface: plain C types only.
 *
 * It is NOT an atomic-commit framework: one crtc, one eDP encoder on a combo-PHY port, one shared DPLL,
 * one primary plane on one linear XRGB8888 framebuffer.  What it keeps from the reference's commit is
 * the meaning of the steps: the new state is prepared before anything is written; the enable, the plane
 * update and the disable are the reference's own callers run on ONE set of objects; ownership that the
 * enable took (power references, the PLL, panel power, the armed plane) is what the disable gives back;
 * "the call returned" is never reported as "it worked" -- status carries what was read back.
 */
#ifndef PARITY_LCD_MODESET_H
#define PARITY_LCD_MODESET_H

#include <stdint.h>
#include "parity_lcd_ops.h"
#include "parity_lcd_calc.h"

struct parity_lcd_modeset_cfg {
	int port;                       /* enum port: 0 = A, 1 = B (combo PHY only) */
	int pipe, cpu_transcoder;       /* 0 = A */
	int dpll_id;                    /* 0 = DPLL 0, 1 = DPLL 1 */
	int aux_ch;                     /* enum aux_ch: 0 = A */
	uint32_t saved_port_bits;       /* DDI_BUF_CTL readout & DDI_BUF_PORT_REVERSAL, | the VBT lane-reversal flag */
	uint8_t dpcd[15];               /* the sink's receiver capabilities (from the resident eDP) */
	uint8_t edp_dpcd[3];
	int vbt_low_vswing, vbt_hobl;   /* the panel's VBT eDP block */
	/* the VBT backlight block of the panel, and the raw clock the PCH PWM divides (kHz, read out by the caller) */
	int vbt_backlight_present, vbt_backlight_active_low, vbt_backlight_controller;
	uint16_t vbt_backlight_pwm_freq_hz;
	uint8_t vbt_backlight_min_brightness;
	uint32_t rawclk_khz;
	/* watermark / DDB inputs, as the normal initialisation read and keeps them */
	uint16_t wm_latency[8];         /* skl_setup_wm_latency(): usec per level */
	uint8_t wm_num_levels;
	int wm_ipc_enabled;
	uint8_t sagv_block_time_us;
	uint32_t dbuf_size;             /* DISPLAY_INFO()->dbuf.size / slice_mask of the platform */
	uint8_t dbuf_slice_mask;
	uint8_t dbuf_enabled_slices;    /* the slices enabled now (the old global DBUF state) */
	int mbus_joined;                /* MBUS_CTL as found (the old global DBUF state) */
	/* the CDCLK state the normal initialisation left (display.cdclk.hw) and the platform limit */
	uint32_t cdclk_khz, cdclk_vco_khz, cdclk_ref_khz, cdclk_bypass_khz, cdclk_max_khz;
	uint8_t cdclk_voltage_level;
	/* memory bandwidth (MB/s) of the QGV point the initialisation left allowed (SAGV is kept off); 0 = unknown = refuse */
	uint32_t qgv_allowed_bw;
	uint32_t dmc_fw_mask;           /* bit n: DMC firmware id n is loaded (1 = pipe A, 2 = pipe B ...; from the DMC loader) */
	int vbt_override_afc_startup;   /* VBT general feature; the value is already in the state's pll.div0 */
	/* the framebuffer to show: what the scanout object reports */
	uint32_t fb_fourcc;
	uint64_t fb_modifier;
	uint32_t fb_width, fb_height, fb_pitch, fb_surf;
};

struct parity_lcd_modeset_status {
	int prepared, crtc_active, plane_armed;
	/* link */
	int link_rate, lane_count;
	int link_trained_flag;          /* intel_dp->link_trained: set by intel_dp_stop_link_train() whatever the outcome */
	int link_status_rc;             /* reading DPCD 0x202.. after the enable: 0, or a negative errno */
	uint8_t link_status[6];
	int cr_ok, eq_ok;               /* drm_dp_clock_recovery_ok / drm_dp_channel_eq_ok on that readout */
	uint8_t train_set[4];
	uint32_t ddi_buf_ctl_value;     /* intel_dp->DP */
	/* backlight (PWM): what intel_backlight_setup() derived and whether it is on */
	int backlight_present, backlight_enabled, backlight_setup_rc;
	uint32_t backlight_pwm_max, backlight_level;
	/* watermarks / DDB computed for the plane (software range [start, end) in DDB blocks) */
	int wm_rc;
	uint16_t ddb_start, ddb_end;
	uint16_t wm0_blocks, wm0_lines; int wm0_enable;
	uint8_t dbuf_slices_wanted; int mbus_joined;
	/* the commit's outer part */
	int cdclk_rc, cdclk_crtc_min, cdclk_bw_min, cdclk_required_khz, cdclk_required_vco, cdclk_required_level, cdclk_change_needed;
	unsigned bw_data_rate;          /* MB/s this crtc needs */
	int dc_off_held;                /* POWER_DOMAIN_DC_OFF is held (inside a commit, or kept after an unconfirmed stop) */
	unsigned crtc_domains_held;     /* how many domains of get_crtc_power_domains() the crtc holds */
	uint8_t dbuf_slices_now; int mbus_joined_now;   /* the current global DBUF state */
	int stop_unconfirmed, retained;
	int dither;
	uint32_t cur_surf, pend_surf;       /* displayed / pending (valid while flip_pending) */
	int flip_pending, flip_stuck; unsigned flip_gen;
	int flip_event_ref;                     /* the pending event still holds its vblank reference */
	unsigned events_cancelled;              /* events settled by the stop path (drm_crtc_vblank_off) */
	uint32_t backlight_min;         /* panel->backlight.min (hw units) */
	uint32_t backlight_max;
	uint32_t backlight_user, backlight_user_max;   /* the user brightness (restore THIS, not the hw level) */                     /* crtc state: derived from pipe_bpp (intel_modeset_pipe_config) */
	unsigned commits;
	/* ownership */
	int pll_on, pll_active_mask, pll_wakeref;
	int ddi_io_wakeref, aux_wakeref;
	/* the reference's drm_err / WARN lines since prepare */
	unsigned errors;
	const char *first_error;
};

/* results of _enable / _disable */
#define PARITY_LCD_MS_OK 0
#define PARITY_LCD_MS_NOT_PREPARED (-1)
#define PARITY_LCD_MS_ERRORS (-2)            /* the reference reported an error (status.first_error) */
#define PARITY_LCD_MS_LINK_NOT_TRAINED (-3)  /* the sink's link status does not show CR + EQ + symbol lock + alignment */
#define PARITY_LCD_MS_STILL_OWNED (-4)       /* after the disable something is still held (status says what) */

/* -22 for a configuration this path does not cover (checked before anything is touched) */
int parity_lcd_modeset_prepare(const struct parity_lcd_state *s, const struct parity_lcd_modeset_cfg *cfg,
	struct parity_lcd_emit *ops);
/*
 * The two commits of the test, each the reference's intel_atomic_commit_tail() reduced to this crtc:
 *   enable : DC_OFF get -> crtc power domains -> [CDCLK: no change] -> DBUF pre-plane (MBUS, slices) -> MBUS DBOX ->
 *            crtc enable -> plane update -> DBUF post-plane -> put unused domains -> DC_OFF put (async, 17 ms)
 *   disable: DC_OFF get -> domains to drop -> plane disable -> crtc disable -> DBUF pre / DBOX / post -> put the
 *            domains -> DC_OFF put
 * Adaptations (logged): after a first anomaly in the enable the plane is NOT armed; after an error in the disable
 * nothing further is given back (DBUF, power domains and DC_OFF stay as they are -- see stop_unconfirmed).
 */
int parity_lcd_modeset_commit_enable(void);
/*
 * While the picture is up (crtc active):
 *   _brightness(user_level, user_max)   the backlight device's update (user range [0, user_max])
 *   _backlight(on)                      backlight PWM + panel backlight-enable off / on; the pipe, the plane and the
 *                                       buffer stay in use -- this is NOT a display stop
 * PARITY_LCD_MS_OK, _NOT_PREPARED (no active crtc / retained), or _ERRORS (a reference error during the call).
 */
int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);
/*
 * Flip the running picture to another buffer (same format / size / pitch): the reference's update of a running crtc
 * (plane noarm, intel_pipe_update_start -- vblank evasion --, plane arm, intel_pipe_update_end -- the event is armed).
 * COMPLETE only when the event completed AND the pipe's live surface (PLANE_SURFLIVE) is the new buffer; then the old
 * buffer is no longer displayed.  Otherwise both stay protected and further flips are refused (flip_stuck) until the
 * display is stopped.  One flip pending at a time; one display owner.
 */
struct parity_lcd_flip_result {
	unsigned gen;
	uint32_t old_surf, new_surf, live_before, live_after;
	uint32_t frame_before, frame_after;
	int event_rc;
	int update_errors;                      /* reference errors during the update (e.g. "Atomic update failure") */
	int result;                             /* PARITY_LCD_FLIP_* */
};
#define PARITY_LCD_FLIP_DONE        0       /* the new buffer is displayed; the old one is released */
#define PARITY_LCD_FLIP_NOT_LATCHED 1       /* the event came but the live surface is not the new one: both kept */
#define PARITY_LCD_FLIP_TIMEOUT     2       /* no completion: both kept */
#define PARITY_LCD_FLIP_REFUSED     3       /* nothing written (not running, a flip stuck, same buffer, bad address) */
int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out);
/* the reference's vblank-evasion window of the running mode (intel_crtc_vblank_evade_scanlines): scanlines [min, max] */
int parity_lcd_modeset_evade_window(int *min, int *max, int *vblank_start);
int parity_lcd_modeset_backlight(int on);
int parity_lcd_modeset_commit_disable(void);
/*
 * After an unconfirmed stop.  The object does NOT forget what it holds: stop_unconfirmed, the power references,
 * DC_OFF, the DBUF slices and the crtc state stay recorded, and every later prepare is refused before anything is
 * initialised.  _abandoned() marks that the caller has taken the retained resources over (the scanout buffer).
 * The only way out is _discard_model(): permitted solely when the retained state belongs to a MODEL backend, whose
 * discarding is itself the isolation; on real hardware there is no release (a recovery that verifies isolation
 * does not exist yet).
 */
void parity_lcd_modeset_abandoned(void);
int parity_lcd_modeset_retained(void);
int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops);
/* the stages the commits are made of (kept for the word-level tests; the path to a picture is the two commits) */
int parity_lcd_modeset_enable(void);
int parity_lcd_modeset_plane_update(void);
int parity_lcd_modeset_plane_disable(void);
int parity_lcd_modeset_disable(void);
void parity_lcd_modeset_status(struct parity_lcd_modeset_status *out);
/* read the sink's link status now into out->link_status / cr_ok / eq_ok */
void parity_lcd_modeset_link_status(struct parity_lcd_modeset_status *out);
/* the caller confirmed on the hardware that the plane is off: the framebuffer is its own again */
void parity_lcd_modeset_plane_released(void);

#endif /* PARITY_LCD_MODESET_H */
