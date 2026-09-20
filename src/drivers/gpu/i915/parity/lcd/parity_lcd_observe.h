/*
 * WS031 Linux-parity -- what the LCD test LOOKS AT while the reference's modeset runs: the pipe's underrun status
 * (ICL_PIPESTATUS), its frame counter and its interrupt mask.  zedBSD project code; plain C over parity_lcd_ops,
 * so the same code runs on the register model and on the real GPU.
 *
 * Why it exists: this private, synchronous, one-buffer test does not connect the DRM vblank machinery nor the
 * underrun interrupt (see lcd_seq_compat.h).  In exchange it OWNS the status register for the run:
 *   - the value found before the run is saved (not judged: it belongs to whoever ran before);
 *   - the status is cleared where the reference clears it (intel_set_cpu_fifo_underrun_reporting(true));
 *   - it is sampled at every observe() point of the commits and by the caller during the steady picture; bits seen
 *     are RECORDED first and only then cleared, so that a later period can be told from an earlier one and no
 *     occurrence is lost;
 *   - start / stop periods and the steady period are accumulated apart: an underrun while the pipe starts is a
 *     different statement from one while a picture stands.
 * The pipe's vblank interrupt must stay masked for the whole run (nothing here could service it): every sample also
 * reads GEN8_DE_PIPE_IMR and remembers if the vblank bit was ever found unmasked -- judged only while the pipe's
 * power well is held (the register lives in that well: before, it reads 0; the power-well enable programs the mask).
 * "Frames advance" is decided from the hardware frame counter alone -- never from elapsed time.
 */
#ifndef PARITY_LCD_OBSERVE_H
#define PARITY_LCD_OBSERVE_H

#include <stdint.h>
#include "parity_lcd_ops.h"

#define PARITY_LCD_OBS_STEADY 100               /* sample points of the caller: the steady picture */
#define PARITY_LCD_OBS_MAX_SAMPLES 96u

struct parity_lcd_obs_sample {
	int point;                      /* enum parity_lcd_observe, or PARITY_LCD_OBS_STEADY */
	uint32_t status;                /* ICL_PIPESTATUS as read */
	uint32_t frame;                 /* PIPE_FRMCOUNT_G4X as read */
	uint32_t imr;                   /* GEN8_DE_PIPE_IMR as read */
	int steady;
};

struct parity_lcd_observer {
	struct parity_lcd_emit *hw;     /* the backend that is read (NOT the recorder: these reads are not part of the path) */
	int pipe;
	uint32_t status_reg, frame_reg, imr_reg;
	uint32_t underrun_mask;         /* the bits the reference treats as underrun status on this display version */
	uint32_t vblank_bit;
	uint32_t saved_before;          /* status found before the run */
	int begun, steady;
	int powered;                    /* between the enable's underrun-arm point and the disable's pipe-disabled point the crtc holds
	                                 * the pipe's power domain; outside, pipe A's registers (power well A) read 0 and say nothing */
	uint32_t seen_transition;       /* underrun bits seen while the pipe was started / stopped */
	uint32_t seen_steady;           /* underrun bits seen while the picture stood */
	int vblank_unmasked_seen;
	/* the stop evidence, taken at the disable's pipe-disabled point -- the last moment the pipe's power well is on */
	int stop_checked; uint32_t stop_transconf, stop_frame0, stop_frame1;
	unsigned n, dropped;
	struct parity_lcd_obs_sample s[PARITY_LCD_OBS_MAX_SAMPLES];
};

void parity_lcd_observer_init(struct parity_lcd_observer *o, struct parity_lcd_emit *hw, int pipe);
/* the ops->observe hook: ctx is the observer */
void parity_lcd_observer_point(void *ctx, int point);
/* the picture is up and has settled: what is pending now still counts as start-up; from here on it is "steady" */
void parity_lcd_observer_steady_begin(struct parity_lcd_observer *o);
void parity_lcd_observer_steady_sample(struct parity_lcd_observer *o);
void parity_lcd_observer_steady_end(struct parity_lcd_observer *o);
/*
 * Watches the frame counter for at most window_ms (sleeping through the backend).  0 when it advanced by at least
 * min_frames; -110 when it did not -- however much time passed.  first / last: the counter values.
 */
int parity_lcd_observer_frames(struct parity_lcd_observer *o, unsigned window_ms, unsigned min_frames, uint32_t *first, uint32_t *last);
/* the stop evidence recorded at the pipe-disabled point (well still on): 0 when TRANSCONF's state bit was clear AND the
 * frame counter stood over 50 ms; -16 when not; -22 when that point was never reached.  window_ms is unused (kept for
 * callers): reading pipe registers LATER, with the well off, returns 0 and would prove nothing. */
int parity_lcd_observer_stopped(struct parity_lcd_observer *o, unsigned window_ms, uint32_t *first, uint32_t *last);

/* TRANSCONF of the pipe's transcoder: returns whether its state bit says "active" */
int parity_lcd_observer_pipe_active(struct parity_lcd_observer *o, uint32_t *transconf);

/* the registers the run log reads back, by the reference's names (parity_lcd_regs.c) */
struct parity_lcd_named_reg {
	const char *name;
	uint32_t reg;
	int has_linux;                  /* Linux's dump of this machine has a value for it */
	uint32_t linux_value, compare_mask;
};
const struct parity_lcd_named_reg *parity_lcd_reg_table(int pipe, int port, int dpll_id, unsigned *n);
uint32_t parity_lcd_reg_by_name(const char *name);
uint32_t parity_lcd_ref_dbuf_ctl(unsigned slice);       /* the reference's DBUF_CTL_S(slice), slice 0 = S1 */      /* pipe A / port A / DPLL 0; 0 = unknown name */

#endif /* PARITY_LCD_OBSERVE_H */
