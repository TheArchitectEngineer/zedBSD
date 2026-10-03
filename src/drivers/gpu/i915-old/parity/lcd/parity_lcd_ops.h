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

/* points of the commit at which the caller may look at the hardware (no writes of the path depend on them) */
enum parity_lcd_observe {
	PARITY_LCD_OBS_COMMIT_BEGIN = 0,        /* DC_OFF held, nothing written yet */
	PARITY_LCD_OBS_UNDERRUN_ARM,            /* where the reference clears the pipe's underrun status and unmasks its interrupt */
	PARITY_LCD_OBS_PIPE_ENABLED,            /* the crtc enable returned */
	PARITY_LCD_OBS_PLANE_ARMED,             /* PLANE_SURF written */
	PARITY_LCD_OBS_PLANE_DISABLED,          /* the plane disable was armed */
	PARITY_LCD_OBS_UNDERRUN_DISARM,         /* where the reference masks the underrun interrupt again */
	PARITY_LCD_OBS_PIPE_DISABLED,           /* the crtc disable returned */
	PARITY_LCD_OBS_COMMIT_END,              /* before DC_OFF is dropped */
	PARITY_LCD_OBS_NUM
};

/* wait_reg results (Linux numbering, as the reference's callers compare them):
 *   0                    the condition held
 *   PARITY_LCD_ETIMEDOUT the device did not reach the condition in time
 *   PARITY_LCD_EIO       the time source / the wait primitive / MMIO access failed: NOT a timeout; the backend has
 *                        also reported it through parity_lcd_backend_fault() so it is the run's first anomaly */
#define PARITY_LCD_ETIMEDOUT (-110)
#define PARITY_LCD_EIO       (-5)
/* a backend reports a fault of its own (time base, MMIO) into the modeset's first-anomaly record */
void parity_lcd_backend_fault(const char *what);

struct parity_lcd_emit {
	void *ctx;
	int model;              /* 1: a register / sink MODEL (discarding it isolates whatever it holds); 0: real hardware */
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
	/* intel_display_power_get_if_enabled(): the cookie, or 0 when the well is OFF (a readout never
	 * turns a well on).  NULL: the backend has no well state of its own (a model) -- the caller then
	 * falls back to power_get. */
	int (*power_get_if_enabled)(void *ctx, int domain);
	void (*power_put)(void *ctx, int domain, int wakeref);
	/* intel_display_power_put_async_delay(): the reference drops DC_OFF this way at the end of a commit */
	void (*power_put_async)(void *ctx, int domain, int wakeref, int delay_ms);
	/* gen9_dbuf_slices_update(): request exactly these DBUF slices (bit n = slice n+1) */
	void (*dbuf_slices_update)(void *ctx, unsigned req_slices);
	/*
	 * The synchronous plane update (intel_pipe_update_start / _end) and its completion event:
	 *   vblank_get / vblank_put   drm_crtc_vblank_get / _put of the pipe (0 / -EINVAL)
	 *   vblank_sleep              schedule_timeout() on the pipe's vblank wait queue: sleep until the pipe's next vblank
	 *                             interrupt or `ticks` 10 ms ticks; returns the ticks left (0 = timed out)
	 *   irq_off / irq_on          local_irq_disable / _enable around the short update section
	 *   arm_event                 drm_crtc_arm_vblank_event(): the event completes at the pipe's next vblank after now
	 *   wait_event                wait for that completion (0; -110 not within timeout_ms; -5 time base / wait fault)
	 */
	int (*vblank_get)(void *ctx, int pipe);
	void (*vblank_put)(void *ctx, int pipe);
	long (*vblank_sleep)(void *ctx, int pipe, long ticks);
	void (*irq_off)(void *ctx);
	void (*irq_on)(void *ctx);
	void (*arm_event)(void *ctx, int pipe);
	int (*wait_event)(void *ctx, int pipe, unsigned timeout_ms);
	/* drm_crtc_vblank_off() on a pending event: it will never be waited for again (no completion after this) */
	void (*cancel_event)(void *ctx, int pipe);
	/* optional: a named point of the commit was reached (enum parity_lcd_observe); the real device samples here */
	void (*observe)(void *ctx, int point);
	void (*lock)(void *ctx, int which, int take);   /* PARITY_LCD_LOCK_*; take = 1 lock, 0 unlock */
	/* drm_err / drm_WARN in the reference text: the FIRST one is what a failed run is read from */
	void (*error)(void *ctx, const char *what);
	void (*debug)(void *ctx, const char *what);     /* drm_dbg_kms: the format string only */
};

#endif /* PARITY_LCD_OPS_H */
