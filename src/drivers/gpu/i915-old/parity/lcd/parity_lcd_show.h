/*
 * WS031 Linux-parity -- LCD-B: show ONE known picture on the panel and stop safely.  zedBSD project code.
 *
 * This is the production body shared by the real GPU (parity_lcd_kernel.c) and the GPU-free kernel test
 * (lcd_show_ktest.c): the scanout object's life (create -> pin -> draw -> publish -> begin ... end -> unpin ->
 * destroy, or ABANDON), the two commits of the modeset object, the observation between them, and the decision
 * whether the buffer may be given back.  Only the backend (parity_lcd_ops) and the GGTT behind the gt_mem differ.
 *
 * Three records are kept APART, because they answer different questions:
 *   first anomaly     the first thing that went wrong, where, and how far the run had got -- never overwritten
 *                     by anything the cleanup reports afterwards;
 *   enabled state     what was really switched on when the enable commit returned (software state + what the sink
 *                     and the frame counter said);
 *   cleanup result    what the disable commit and the stop confirmation returned, and what is still held.
 * And three kinds of evidence are not mixed: software flags ("the arm was written"), hardware observation (frame
 * counter, underrun status, sink link status) and -- outside this code -- the photograph of the panel.
 */
#ifndef PARITY_LCD_SHOW_H
#define PARITY_LCD_SHOW_H

#include <stdint.h>
#include "parity_lcd_ops.h"
#include "parity_lcd_calc.h"
#include "parity_lcd_modeset.h"
#include "parity_lcd_observe.h"
#include "parity_lcd_trace.h"
#include "scanout.h"

struct parity_gt_mem;

enum parity_lcd_show_stage {
	PARITY_LCD_SHOW_NONE = 0,
	PARITY_LCD_SHOW_BUFFER_READY,           /* scanout pinned, picture drawn, published and read back */
	PARITY_LCD_SHOW_PREPARED,               /* the modeset's check phase accepted the state */
	PARITY_LCD_SHOW_ENABLE_RETURNED,        /* the enable commit returned (successfully or not) */
	PARITY_LCD_SHOW_PICTURE_UP,             /* enable OK and the frame counter advances */
	PARITY_LCD_SHOW_WINDOW_DONE,            /* the finite observation window passed */
	PARITY_LCD_SHOW_DISABLE_RETURNED,
	PARITY_LCD_SHOW_STOP_CONFIRMED,         /* pipe reported off AND the frame counter stands */
	PARITY_LCD_SHOW_RELEASED,               /* buffer unpinned and destroyed */
	PARITY_LCD_SHOW_ABANDONED               /* stop not confirmed: buffer and everything it needs kept for ever */
};

struct parity_lcd_show_env {
	struct parity_lcd_emit *hw;             /* the backend */
	struct parity_gt_mem *gm;               /* where the scanout buffer lives */
	struct parity_scanout *so;              /* caller-owned storage: it must outlive an abandoned buffer */
	const struct parity_lcd_state *lcd;     /* LCD-A: mode, link, M/N, PLL words (from the resident eDP's DPCD / EDID) */
	struct parity_lcd_modeset_cfg cfg;      /* everything but fb_*: filled by the caller from ITS sources */
	int pipe;
	unsigned pattern_id;
	uint64_t pattern_fnv;                   /* pinned hash of that picture at this size; 0 = do not compare */
	unsigned first_frames_ms;               /* how long to wait for the first frames */
	unsigned window_ms;                     /* the finite observation window (for the camera) */
	/* optional: called when a stage is reached (register readbacks for the log; fault injection in tests) */
	/* optional: a test that runs while the picture is up (after the first frames, before the window); 0 = passed.
	 * A failure is recorded as the first anomaly (if none yet) and the reference stop path follows. */
	int (*in_window)(void *ctx, struct parity_lcd_observer *o);
	void *in_window_ctx;
	void (*at_stage)(void *ctx, int stage);
	void *at_stage_ctx;
};

struct parity_lcd_show_report {
	int output_hdmi;                        /* the run drove an HDMI sink: no DP link to judge it by */
	int stage;                              /* the furthest stage reached */
	/* first anomaly */
	const char *first_anomaly;              /* 0 = none */
	int first_anomaly_stage;
	int first_anomaly_rc;
	int first_error_trace_at;               /* index in the run log of the first reference error; -1 = none */
	/* return codes, in order */
	int window_rc, create_rc, pin_rc, prepare_rc, begin_rc, enable_rc, first_frames_rc, steady_rc, disable_rc, stopped_rc;
	int unpin_rc, destroy_rc;
	/* enabled state / cleanup result */
	struct parity_lcd_modeset_status at_enable, at_window_end, at_disable;
	unsigned enable_errors, cleanup_errors;
	int cleanup_first_error_trace_at;
	/* hardware observation */
	uint32_t frame_first, frame_last, steady_frame_first, steady_frame_last, stop_frame_first, stop_frame_last;
	unsigned steady_rounds;
	int window_hook_rc;
	uint32_t transconf_after_stop;
	/* the buffer */
	uint64_t surf;
	uint64_t pattern_hash;
	uint32_t readback_bad_before, readback_bad_after;
	int released, abandoned;
	int display_acquired;                   /* the buffer was handed to the display (the enable commit was attempted) */
	int display_released;                   /* the display has provably stopped reading it (back to PINNED) */
	struct parity_lcd_observer obs;
	struct parity_lcd_trace *trace;         /* the run log (static storage of the show body) */
	int pass;
};

/* 0 = the picture was up for the whole window, the stop was confirmed and everything was given back */
int parity_lcd_show_run(struct parity_lcd_show_env *env, struct parity_lcd_show_report *out);
/*
 * The device-side latch: set when a run ends ABANDONED, never cleared by a later run.  Every run refuses before it
 * initialises anything while it is set, and the outer teardown asks it before releasing the DMA device, the scratch
 * page, the BARs or bus mastering.  Cleared only by _discard_model() for a run on a register MODEL.
 */
/*
 * Show a buffer somebody else prepared (a GPU render target, a second buffer ...): the pixels are NOT touched, the buffer
 * is NOT created, unpinned or destroyed here.  It must be PINNED (not in use, not abandoned).  On return it is either
 * PINNED again (display_released = 1: the stop was confirmed; the owner releases it when ITS other users -- e.g. the
 * GPU's mappings -- are done) or ABANDONED (the stop was not confirmed: kept for ever, the device latch is set).
 * `verify` (optional) re-checks the pixels after the stop: the number of wrong pixels.
 */
int parity_lcd_show_prepared(struct parity_lcd_show_env *env, struct parity_scanout *so,
	uint32_t (*verify)(void *ctx, const struct parity_scanout *so), void *verify_ctx, struct parity_lcd_show_report *out);
int parity_lcd_show_retained(void);
/*
 * A buffer's OTHER user (the GPU) is not shown to have stopped using it: the same device-side retained state as an
 * unconfirmed display stop.  `gm` identifies the memory manager that owns the kept objects; the latch can only be
 * dropped for a manager that has since been finalised (its objects are gone with it: GPU-free tests).
 */
void parity_lcd_show_retain_gpu(const void *gm, const char *why);
int parity_lcd_show_gpu_retained(void);
int parity_lcd_show_discard_gpu_model(const void *gm, int gm_finalised);
int parity_lcd_show_discard_model(struct parity_lcd_show_env *env);

/* LAST RESORT (zedBSD): stop anything still scanned out; returns how many things it had to stop */
struct osdep_mmio;
unsigned parity_lcd_last_resort_stop(struct osdep_mmio *m);

#endif /* PARITY_LCD_SHOW_H */
