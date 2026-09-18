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
	unsigned training_pattern_writes, link_status_cr_done, link_status_eq_done;
	/* ordering the hardware needs; each counts a violation */
	unsigned ddi_enabled_without_pll;       /* DDI_BUF_CTL enable while the PLL is not locked / the DDI clock is off */
	unsigned pipe_enabled_without_link;     /* TRANSCONF enable while the DDI buffer is off */
	unsigned plane_armed_without_pipe;      /* PLANE_SURF written with PLANE_CTL enable while the pipe is off */
	unsigned pll_disabled_with_pipe_on;     /* PLL_ENABLE cleared while the pipe still runs */
	unsigned training_without_panel_power;  /* a training-pattern write while panel power is off */
	unsigned pattern_mismatch;              /* DPCD training pattern != the pattern DP_TP_CTL transmits */
};

/* `dpf` must already be initialised (dp_fake_init) -- the sink's link status is reset to "not trained" */
void lcd_fake_init(struct lcd_fake_hw *hw, struct dp_fake_hw *dpf, int pipe, int port, int dpll_id, unsigned vtotal);
uint32_t lcd_fake_reg(const struct lcd_fake_hw *hw, uint32_t reg);
int lcd_fake_power_refs_total(const struct lcd_fake_hw *hw);
unsigned lcd_fake_violations(const struct lcd_fake_hw *hw);

#endif /* PARITY_LCD_FAKE_HW_H */
