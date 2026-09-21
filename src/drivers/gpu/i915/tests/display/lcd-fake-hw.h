/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A model of what the LCD modeset touches (lcd-fake-hw.c): the combo PLL,
 * the DDI buffer and DP transport, the transcoder and pipe (state bit,
 * moving scanline, frame counter), the plane arm and its double-buffered
 * surface, the DBUF slices, the power-domain references, the vblank and its
 * event, plus the sink's link-training behaviour behind the AUX model of
 * dp-fake-hw.c.
 *
 * It models the register and DPCD contract the Linux text relies on
 * (status bits that follow control bits, waits, the sink reporting CR / EQ
 * once the drive levels it asks for are set); it is not a recording of the
 * real GPU.  Panel power, DPCD traffic and the AUX power references go
 * through the resident eDP (drv_i915_edp_*) on the dp-fake-hw.c model, that
 * is through the same production code as on hardware.  Faults are switched
 * on per run; ordering mistakes are counted (the *_without_* fields and
 * drv_i915_lcd_fake_violations()).
 *
 * Test support only: the host tests and the kernel test build link it; the
 * production kernel never does.  It uses no host library, so both can.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_FAKE_HW_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_FAKE_HW_H

#include "dp-fake-hw.h"

#include "../../display/internal.h"

#include <stdint.h>

/* The registers the model keeps, and the power domains it counts. */
#define I915_LCD_FAKE_MAX_REGS 320U
#define I915_LCD_FAKE_MAX_DOMAINS 128

/* The Linux EINVAL the model's event wait answers without an armed event (the hook's Linux numbering). */
#define I915_LCD_FAKE_EINVAL (-22)

/*
 * One register of the model and its value.
 */
struct i915_lcd_fake_reg {
	uint32_t reg;
	uint32_t val;
};

/*
 * The model of one LCD modeset run.
 *
 * One instance stands for the display engine of one test case, next to the
 * dp-fake-hw.c model that holds the clock, the PPS, AUX and the sink's DPCD;
 * drv_i915_lcd_fake_init() starts it afresh and `ops` is the backend handed
 * to the modeset.  Field names are the ones the tests read.
 */
struct i915_lcd_fake_hw {
	/* The clock, PPS, AUX and the sink's DPCD. */
	struct i915_dp_fake_hw *dpf;

	/* The backend the modeset is given. */
	struct i915_lcd_emit ops;

	/* The registers written so far; regs_overflow counts registers that did not fit. */
	struct i915_lcd_fake_reg regs[I915_LCD_FAKE_MAX_REGS];
	unsigned nregs;
	unsigned regs_overflow;

	/* The instances whose behaviour is modelled, and the mode's vertical total. */
	int pipe;
	int port;
	int dpll_id;
	unsigned vtotal;

	/* The faults, switched on by a test. */
	int fault_pll_no_lock;                  /* PLL_LOCK never sets */
	int fault_cr_never;                     /* the sink never reports clock recovery */
	int fault_eq_never;                     /* the sink never reports equalisation */
	int fault_pipe_stuck_on;                /* the TRANSCONF state bit never clears (the pipe does not stop) */
	uint32_t fault_drop_write_reg;          /* writes to this register are lost (a test of the MODEL: does it notice?) */
	int fault_frame_counter_frozen;         /* the pipe reports "on" but its frame counter does not move */
	int fault_underrun_at_arm;              /* the pipe raises its underrun status when the plane is armed */
	int fault_underrun_steady_after;        /* n > 0: the n-th frame-counter read after the arm raises the underrun status */
	int fault_drop_dbuf_update;             /* DBUF slice requests are lost (a test of the MODEL) */
	uint32_t fault_time_base_reg;           /* a wait on this register meets a TIME-BASE fault (not a timeout) */
	int fault_put_refused_domain;           /* domain + 1 whose put is refused like a kept power well; 0 = none */
	int fault_flip_never_latch;             /* PLANE_SURF never reaches PLANE_SURFLIVE */
	int fault_early_event;                  /* the event reports completion without a vblank having passed */
	int fault_no_vblank;                    /* no vblank interrupt arrives (sleeps and waits time out) */
	int fault_power_get;                    /* domain + 1 whose get fails; 0 = none */

	/* The sink's wishes: it reports CR once the voltage swing reaches this, EQ once the pre-emphasis does. */
	uint8_t sink_want_vswing;
	uint8_t sink_want_preemph;

	/* The pipe as the model derives it; pipe_on_since_us = 0 while the pipe is off. */
	uint64_t pipe_on_since_us;
	uint32_t scanline;

	/* The power-domain references and the locks. */
	int power_refs[I915_LCD_FAKE_MAX_DOMAINS];
	unsigned power_gets;
	unsigned power_puts;
	unsigned power_underflows;
	int lock_held[2];
	unsigned lock_errors;

	/* The plane arms and what the plane held at the last one. */
	unsigned plane_arms;
	unsigned plane_disarms;
	uint32_t plane_ctl_at_arm;
	uint32_t plane_surf_at_arm;

	/* The DBUF slices that are powered (bit n = slice n + 1), and the asynchronous puts. */
	uint8_t dbuf_enabled;
	unsigned dbuf_updates;
	unsigned async_puts;
	int last_async_delay_ms;
	unsigned frame_reads_after_arm;

	/* The observe() points in order, and who else is told about them (e.g. drv_i915_lcd_observer_point). */
	void (*on_observe)(void *ctx, int point);
	void *on_observe_ctx;
	uint8_t obs[48];
	unsigned nobs;

	/* The double-buffered surface: written, then live at the next frame boundary. */
	uint32_t surf_live;
	uint32_t surf_pending;
	int surf_pending_valid;
	uint32_t surf_pending_frame;

	/* The update section, the vblank references and the completion event. */
	int irq_off;
	int vblank_refs;
	int event_armed;
	unsigned irq_off_calls;
	unsigned vblank_sleeps;
	unsigned events_armed;
	unsigned events_done;
	unsigned events_cancelled;
	unsigned sleep_irq_off;                 /* a sleep entered with IRQs off: a violation */
	unsigned waits_refused;

	/* PIPEDSL reads scanline_hold this many more times (a test knob). */
	uint32_t scanline_hold;
	unsigned scanline_hold_reads;

	/* A PLANE_SURF write outside the update section (counted; see the tests). */
	unsigned arm_outside_section;

	/* The sink's training as the model saw it. */
	unsigned training_pattern_writes;
	unsigned link_status_cr_done;
	unsigned link_status_eq_done;

	/* The ordering the hardware needs; each counts a violation. */
	unsigned ddi_enabled_without_pll;       /* DDI_BUF_CTL enable while the PLL is not locked or the DDI clock is off */
	unsigned pipe_enabled_without_link;     /* TRANSCONF enable while the DDI buffer is off */
	unsigned plane_armed_without_pipe;      /* PLANE_SURF written with PLANE_CTL enable while the pipe is off */
	unsigned pll_disabled_with_pipe_on;     /* PLL_ENABLE cleared while the pipe still runs */
	unsigned training_without_panel_power;  /* a training-pattern write while panel power is off */
	unsigned pattern_mismatch;              /* the DPCD training pattern is not the pattern DP_TP_CTL transmits */
	unsigned modeset_without_dc_off;        /* a PLL or pipe enable or a plane arm while POWER_DOMAIN_DC_OFF is not held */
	unsigned pipe_enabled_without_power;    /* TRANSCONF enable without the pipe's, transcoder's, DDI lanes' or display core's domain */
	unsigned plane_armed_outside_slices;    /* the DDB range needs a DBUF slice that is off, or MBUS joining that is not set */
	unsigned dbuf_shrunk_under_plane;       /* a slice the armed plane's DDB needs was switched off while the pipe runs */
	unsigned power_dropped_with_pipe_on;    /* the last reference of the pipe's or transcoder's domain went while the pipe runs */
	unsigned plane_armed_without_ddb;       /* armed with PLANE_BUF_CFG empty or outside the DBUF, or watermark level 0 disabled */

	/* The DDB blocks of the platform (for the range check). */
	uint32_t dbuf_size;
};

void drv_i915_lcd_fake_init(struct i915_lcd_fake_hw *hw, struct i915_dp_fake_hw *dpf, int pipe, int port, int dpll_id, unsigned vtotal);
uint32_t drv_i915_lcd_fake_reg(const struct i915_lcd_fake_hw *hw, uint32_t reg);
int drv_i915_lcd_fake_power_refs_total(const struct i915_lcd_fake_hw *hw);
unsigned drv_i915_lcd_fake_violations(const struct i915_lcd_fake_hw *hw);

#endif
