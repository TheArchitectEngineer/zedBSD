/*
 * WS031 Linux-parity — GPU-free model of what the LCD modeset touches: the combo PLL, the DDI buffer and
 * DP transport, the transcoder / pipe (state bit, moving scanline, frame counter), the plane arm, plus the
 * SINK's link-training behaviour behind the AUX model of parity/dp/dp_fake_hw.  zedBSD project code; test
 * support only.
 *
 * It models the REGISTER / DPCD CONTRACT the reference code relies on (status bits that follow control
 * bits, waits, the sink reporting CR / EQ once the drive levels it asks for are set) -- it is not a
 * recording of the real GPU.  Panel power, DPCD traffic and the AUX power references go through the
 * resident eDP (parity_edp_*) on the dp_fake_hw model, i.e. through the same production code as on
 * hardware.  Faults are switched on per run; ordering mistakes are COUNTED (the *_violations fields).
 */
#ifndef PARITY_LCD_FAKE_HW_H
#define PARITY_LCD_FAKE_HW_H

#include <stdint.h>
#include "parity_lcd_ops.h"
#include "../dp/dp_fake_hw.h"

#define LCD_FAKE_MAX_REGS 320u
#define LCD_FAKE_MAX_DOMAINS 128

struct lcd_fake_hw {
	struct dp_fake_hw *dpf;                 /* the clock, PPS, AUX and the sink's DPCD */
	struct parity_lcd_emit ops;
	struct { uint32_t reg, val; } regs[LCD_FAKE_MAX_REGS];
	unsigned nregs, regs_overflow;
	int pipe, port, dpll_id;                /* which instances the behaviours watch */
	unsigned vtotal;

	/* faults */
	int fault_pll_no_lock;                  /* PLL_LOCK never sets */
	int fault_cr_never, fault_eq_never;     /* the sink never reports clock recovery / equalisation */
	int fault_pipe_stuck_on;                /* TRANSCONF state bit never clears (the pipe does not stop) */
	uint32_t fault_drop_write_reg;          /* writes to this register are lost (a test of the MODEL: does it notice?) */
	int fault_frame_counter_frozen;         /* the pipe reports "on" but its frame counter does not move */
	int fault_underrun_at_arm;              /* the pipe raises its underrun status when the plane is armed */
	int fault_underrun_steady_after;        /* n > 0: the underrun status is raised by the n-th frame-counter read after the arm */
	int fault_drop_dbuf_update;             /* DBUF slice requests are lost (a test of the MODEL) */
	uint32_t fault_time_base_reg;           /* a wait on this register meets a TIME-BASE fault (not a timeout) */
	int fault_put_refused_domain;           /* domain + 1 whose put is refused like a kept power well; 0 = none */
	int fault_flip_never_latch;             /* PLANE_SURF never reaches PLANE_SURFLIVE */
	int fault_early_event;                  /* the event reports completion without a vblank having passed */
	int fault_no_vblank;                    /* no vblank interrupt arrives (sleeps / waits time out) */
	int fault_power_get;                    /* domain + 1 whose get fails; 0 = none */
	/* the sink's wishes: it reports CR once voltage swing >= want_vswing, EQ once pre-emphasis >= want_preemph */
	uint8_t sink_want_vswing, sink_want_preemph;

	/* state derived by the model */
	uint64_t pipe_on_since_us;              /* 0 = pipe off */
	uint32_t scanline;
	/* observations */
	int power_refs[LCD_FAKE_MAX_DOMAINS];
	unsigned power_gets, power_puts, power_underflows;
	int lock_held[2];
	unsigned lock_errors;
	unsigned plane_arms, plane_disarms;
	uint32_t plane_ctl_at_arm, plane_surf_at_arm;
	uint8_t dbuf_enabled;                   /* the slices that are powered (bit n = slice n+1) */
	unsigned dbuf_updates, async_puts; int last_async_delay_ms;
	unsigned frame_reads_after_arm;
	void (*on_observe)(void *ctx, int point); void *on_observe_ctx;      /* e.g. parity_lcd_observer_point */
	uint8_t obs[48]; unsigned nobs;
	/* the double-buffered surface: written -> live at the next frame boundary */
	uint32_t surf_live, surf_pending; int surf_pending_valid; uint32_t surf_pending_frame;
	int irq_off, vblank_refs, event_armed; unsigned irq_off_calls, vblank_sleeps, events_armed, events_done;
	unsigned events_cancelled, sleep_irq_off, waits_refused;   /* sleep entered with IRQs off = a violation */
	uint32_t scanline_hold; unsigned scanline_hold_reads;     /* PIPEDSL reads this value this many times (test knob) */
	unsigned arm_outside_section;           /* a PLANE_SURF write outside irq_off (counted, see the tests) */         /* the observe() points, in order */
	unsigned training_pattern_writes, link_status_cr_done, link_status_eq_done;
	/* ordering the hardware needs; each counts a violation */
	unsigned ddi_enabled_without_pll;       /* DDI_BUF_CTL enable while the PLL is not locked / the DDI clock is off */
	unsigned pipe_enabled_without_link;     /* TRANSCONF enable while the DDI buffer is off */
	unsigned plane_armed_without_pipe;      /* PLANE_SURF written with PLANE_CTL enable while the pipe is off */
	unsigned pll_disabled_with_pipe_on;     /* PLL_ENABLE cleared while the pipe still runs */
	unsigned training_without_panel_power;  /* a training-pattern write while panel power is off */
	unsigned pattern_mismatch;
	unsigned modeset_without_dc_off;        /* PLL / pipe enable or a plane arm while POWER_DOMAIN_DC_OFF is not held */
	unsigned pipe_enabled_without_power;    /* TRANSCONF enable without the pipe's / transcoder's / DDI lanes' / display core's domain */
	unsigned plane_armed_outside_slices;    /* the DDB range needs a DBUF slice that is not enabled, or MBUS joining that is not set */
	unsigned dbuf_shrunk_under_plane;       /* a slice the armed plane's DDB needs was switched off while the pipe runs */
	unsigned power_dropped_with_pipe_on;    /* the last reference of the pipe's / transcoder's domain went while the pipe runs */
	unsigned plane_armed_without_ddb;       /* armed with PLANE_BUF_CFG empty / outside the DBUF, or watermark level 0 disabled */
	uint32_t dbuf_size;                     /* DDB blocks of the platform (for the range check) */              /* DPCD training pattern != the pattern DP_TP_CTL transmits */
};

/* `dpf` must already be initialised (dp_fake_init) -- the sink's link status is reset to "not trained" */
void lcd_fake_init(struct lcd_fake_hw *hw, struct dp_fake_hw *dpf, int pipe, int port, int dpll_id, unsigned vtotal);
uint32_t lcd_fake_reg(const struct lcd_fake_hw *hw, uint32_t reg);
int lcd_fake_power_refs_total(const struct lcd_fake_hw *hw);
unsigned lcd_fake_violations(const struct lcd_fake_hw *hw);

#endif /* PARITY_LCD_FAKE_HW_H */
