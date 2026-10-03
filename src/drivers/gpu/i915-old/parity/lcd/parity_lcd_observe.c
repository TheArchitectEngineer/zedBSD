/*
 * WS031 Linux-parity -- the LCD test's observer (see parity_lcd_observe.h).  zedBSD project code.  Register names,
 * addresses and bits are the reference's own macros (extracted headers), not retyped numbers.
 */
#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_ddi_regs.h"           /* reference, extracted: TRANSCONF */
#include "parity_lcd_observe.h"

void parity_lcd_observer_init(struct parity_lcd_observer *o, struct parity_lcd_emit *hw, int pipe)
{
	memset(o, 0, sizeof(*o));
	o->hw = hw;
	o->pipe = pipe;
	o->status_reg = i915_mmio_reg_offset(ICL_PIPESTATUS(pipe));
	o->frame_reg = i915_mmio_reg_offset(PIPE_FRMCOUNT_G4X(pipe));
	o->imr_reg = i915_mmio_reg_offset(GEN8_DE_PIPE_IMR(pipe));
	/* icl_pipe_status_underrun_mask() on display version 13: the pipe underrun plus the XELPD soft / hard / port bits */
	o->underrun_mask = PIPE_STATUS_UNDERRUN | PIPE_STATUS_SOFT_UNDERRUN_XELPD | PIPE_STATUS_HARD_UNDERRUN_XELPD |
		PIPE_STATUS_PORT_UNDERRUN_XELPD;
	o->vblank_bit = GEN8_PIPE_VBLANK;
}

static void sample(struct parity_lcd_observer *o, int point, int clear_all)
{
	uint32_t status = o->hw->read32(o->hw->ctx, o->status_reg);
	uint32_t imr = o->hw->read32(o->hw->ctx, o->imr_reg);
	uint32_t bits = status & o->underrun_mask;

	if (o->n < PARITY_LCD_OBS_MAX_SAMPLES) {
		struct parity_lcd_obs_sample *s = &o->s[o->n++];

		s->point = point;
		s->status = status;
		s->frame = o->hw->read32(o->hw->ctx, o->frame_reg);
		s->imr = imr;
		s->steady = o->steady;
	} else {
		o->dropped++;
	}
	if (point == PARITY_LCD_OBS_UNDERRUN_ARM)
		o->powered = 1;
	if (o->powered && !(imr & o->vblank_bit))
		o->vblank_unmasked_seen = 1;
	if (point == PARITY_LCD_OBS_PIPE_DISABLED) {
		o->stop_transconf = o->hw->read32(o->hw->ctx, i915_mmio_reg_offset(TRANSCONF((enum transcoder)o->pipe)));
		o->stop_frame0 = o->hw->read32(o->hw->ctx, o->frame_reg);
		o->hw->usleep(o->hw->ctx, 50000u);                /* three frame times at 60 Hz */
		o->stop_frame1 = o->hw->read32(o->hw->ctx, o->frame_reg);
		o->stop_checked = 1;
		o->powered = 0;
	}
	if (!o->begun) {
		o->saved_before = status;       /* left by whoever ran before: kept, not judged */
		o->begun = 1;
		return;
	}
	if (point == PARITY_LCD_OBS_UNDERRUN_ARM) {
		o->saved_before = status;       /* the first read with the pipe's well on: what was really left there */
		/* the reference's position: bdw_set_fifo_underrun_reporting(enable) writes the whole underrun mask */
		o->hw->write32(o->hw->ctx, o->status_reg, o->underrun_mask);
		return;
	}
	/* recorded first, cleared after: nothing is lost, and the next period starts clean */
	if (o->steady)
		o->seen_steady |= bits;
	else
		o->seen_transition |= bits;
	if (bits != 0u || clear_all)
		o->hw->write32(o->hw->ctx, o->status_reg, bits);
}

void parity_lcd_observer_point(void *ctx, int point)
{
	struct parity_lcd_observer *o = ctx;

	if (point == PARITY_LCD_OBS_COMMIT_BEGIN && o->steady)
		parity_lcd_observer_steady_end(o);
	sample(o, point, 0);
}

void parity_lcd_observer_steady_begin(struct parity_lcd_observer *o)
{
	sample(o, PARITY_LCD_OBS_STEADY, 0);    /* still the start-up period */
	o->steady = 1;
}

void parity_lcd_observer_steady_sample(struct parity_lcd_observer *o)
{
	sample(o, PARITY_LCD_OBS_STEADY, 0);
}

void parity_lcd_observer_steady_end(struct parity_lcd_observer *o)
{
	if (o->steady) {
		sample(o, PARITY_LCD_OBS_STEADY, 0);
		o->steady = 0;
	}
}

int parity_lcd_observer_frames(struct parity_lcd_observer *o, unsigned window_ms, unsigned min_frames, uint32_t *first, uint32_t *last)
{
	uint32_t f0 = o->hw->read32(o->hw->ctx, o->frame_reg), f = f0;
	unsigned waited;

	for (waited = 0; waited < window_ms && (uint32_t)(f - f0) < min_frames; waited += 10u) {
		o->hw->usleep(o->hw->ctx, 10000u);
		f = o->hw->read32(o->hw->ctx, o->frame_reg);
	}
	if (first != 0)
		*first = f0;
	if (last != 0)
		*last = f;
	return (uint32_t)(f - f0) >= min_frames ? 0 : -110;
}

int parity_lcd_observer_stopped(struct parity_lcd_observer *o, unsigned window_ms, uint32_t *first, uint32_t *last)
{
	(void)window_ms;
	if (first != 0)
		*first = o->stop_frame0;
	if (last != 0)
		*last = o->stop_frame1;
	if (!o->stop_checked)
		return -22;
	return (!(o->stop_transconf & TRANSCONF_STATE_ENABLE) && o->stop_frame0 == o->stop_frame1) ? 0 : -16;
}

int parity_lcd_observer_pipe_active(struct parity_lcd_observer *o, uint32_t *transconf)
{
	uint32_t v = o->hw->read32(o->hw->ctx, i915_mmio_reg_offset(TRANSCONF((enum transcoder)o->pipe)));

	if (transconf != 0)
		*transconf = v;
	return (v & TRANSCONF_STATE_ENABLE) != 0u;
}
