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
