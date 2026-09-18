/*
 * WS031 Linux-parity — what the generated LCD modeset code (the *_port.c files of parity/lcd) needs from the
 * outside: registers, waits, time, the sink's DPCD, the panel power sequencer, display power
 * domains, locks and an error sink.  zedBSD project code; plain C types only.
 *
 * The SAME reference-derived callers, callees and state run on three backends:
 *   - the register / sink model (lcd_fake_hw.c): host and GPU-free integration tests;
 *   - the real GPU (parity_lcd_kernel.c): MMIO, the resident eDP's AUX and PPS, the power layer;
 *   - a recorder stacked on either of them (parity_lcd_trace.c): the ordered log of what ran.
 * A list of writes captured on one backend is never replayed on another: conditions, readbacks,
 * sink answers, waits and reference counts are evaluated where the code runs.
 *
 * Hooks from `read32` down may be NULL in the pure word recorder of parity_lcd_calc.c (the
 * state-calculation tests): a read then returns 0 and a wait succeeds.
 */
#ifndef PARITY_LCD_OPS_H
#define PARITY_LCD_OPS_H

#include <stdint.h>
#include <stddef.h>

/* panel power sequencer / backlight-power operations, executed by the resident eDP (parity/dp) */
enum parity_lcd_panel_op {
	PARITY_LCD_PANEL_ON = 0,          /* intel_pps_on() */
	PARITY_LCD_PANEL_OFF,             /* intel_pps_off() */
	PARITY_LCD_PANEL_VDD_ON,          /* intel_pps_vdd_on() */
	PARITY_LCD_PANEL_VDD_OFF_SYNC,    /* intel_pps_vdd_off_sync() */
	PARITY_LCD_PANEL_BACKLIGHT_ON,    /* intel_pps_backlight_on(): the PPS's backlight-enable bit */
	PARITY_LCD_PANEL_BACKLIGHT_OFF,   /* intel_pps_backlight_off() */
};

#define PARITY_LCD_LOCK_DPLL 0           /* i915->display.dpll.lock */
#define PARITY_LCD_LOCK_BACKLIGHT 1      /* i915->display.backlight.lock */

struct parity_lcd_emit {
	void *ctx;
	void (*write32)(void *ctx, uint32_t reg, uint32_t value);
	uint32_t (*rmw32)(void *ctx, uint32_t reg, uint32_t clear, uint32_t set);   /* returns the old value */
	void (*posting_read)(void *ctx, uint32_t reg);                              /* may be NULL */
	void (*step)(void *ctx, const char *name);      /* a callee of the reference that is not ported */
	/* ---- may be NULL in the pure word recorder ---- */
	uint32_t (*read32)(void *ctx, uint32_t reg);
	/* intel_de_wait_for_set / _clear / _register: 0, or -110 (-ETIMEDOUT, Linux numbering) */
	int (*wait_reg)(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned timeout_ms);
	void (*usleep)(void *ctx, unsigned us);         /* usleep_range / msleep: may yield */
	void (*udelay)(void *ctx, unsigned us);         /* udelay: short, does not yield */
	/* drm_dp_dpcd_read / _write on the panel's AUX channel: bytes transferred, or a negative errno */
	long (*dpcd_read)(void *ctx, unsigned offset, uint8_t *buf, size_t size);
	long (*dpcd_write)(void *ctx, unsigned offset, const uint8_t *buf, size_t size);
	/* drm_dp_read_dpcd_caps(): receiver caps incl. the extended field; 0, or a negative errno */
	int (*read_dpcd_caps)(void *ctx, uint8_t dpcd[15]);
	int (*panel)(void *ctx, int op);                /* enum parity_lcd_panel_op; 0 = done */
	/* intel_display_power_get / _put; `domain` = the reference's enum intel_display_power_domain
	 * value.  get returns a non-zero wakeref cookie (0 = failed); put takes it back. */
	int (*power_get)(void *ctx, int domain);
	void (*power_put)(void *ctx, int domain, int wakeref);
	void (*lock)(void *ctx, int which, int take);   /* PARITY_LCD_LOCK_*; take = 1 lock, 0 unlock */
	/* drm_err / drm_WARN in the reference text: the FIRST one is what a failed run is read from */
	void (*error)(void *ctx, const char *what);
	void (*debug)(void *ctx, const char *what);     /* drm_dbg_kms: the format string only */
};

#endif /* PARITY_LCD_OPS_H */
