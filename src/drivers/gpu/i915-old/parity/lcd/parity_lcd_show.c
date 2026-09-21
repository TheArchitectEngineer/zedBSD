/*
 * WS031 Linux-parity -- LCD-B: show one known picture and stop safely (see parity_lcd_show.h).  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <errno.h>
#include "../gt_mem.h"
#include "scanout.h"
#include "lcd_pattern.h"
#include "parity_lcd_show.h"

static struct parity_lcd_trace show_trace;
static int show_retained;               /* see parity_lcd_show_retained() */
static const struct parity_lcd_emit *show_retained_hw;

static int show_gpu_retained;
static const void *show_gpu_retained_gm;
static const char *show_gpu_retained_why;

int parity_lcd_show_retained(void)
{
	return show_retained || show_gpu_retained;
}

void parity_lcd_show_retain_gpu(const void *gm, const char *why)
{
	show_gpu_retained = 1;
	show_gpu_retained_gm = gm;
	show_gpu_retained_why = why;
	kern_logf("i915: parity retained (GPU not shown to be done): %s -- the buffer and every object the request may use are kept\n",
		show_gpu_retained_why != 0 ? show_gpu_retained_why : "-");
}

int parity_lcd_show_gpu_retained(void)
{
	return show_gpu_retained;
}

int parity_lcd_show_discard_gpu_model(const void *gm, int gm_finalised)
{
	if (!show_gpu_retained)
		return 0;
	if (gm == 0 || gm != show_gpu_retained_gm || !gm_finalised)
		return -1;
	show_gpu_retained = 0;
	show_gpu_retained_gm = 0;
	show_gpu_retained_why = 0;
	return 0;
}

int parity_lcd_show_discard_model(struct parity_lcd_show_env *env)
{
	if (!show_retained)
		return 0;
	if (env == 0 || env->hw == 0 || env->hw != show_retained_hw || !env->hw->model)
		return -1;
	if (parity_lcd_modeset_discard_model(&show_trace.ops) != 0)
		return -1;
	show_retained = 0;
	show_retained_hw = 0;
	return 0;
}

static void anomaly(struct parity_lcd_show_report *r, const char *what, int rc)
{
	if (r->first_anomaly != 0)
		return;                         /* the FIRST one is the finding; later ones are consequences */
	r->first_anomaly = what;
	r->first_anomaly_stage = r->stage;
	r->first_anomaly_rc = rc;
}

static void reached(struct parity_lcd_show_env *env, struct parity_lcd_show_report *r, int stage)
{
	r->stage = stage;
	if (env->at_stage != 0)
		env->at_stage(env->at_stage_ctx, stage);
}

static void observe_tap(void *ctx, int point)
{
	parity_lcd_observer_point(ctx, point);
}

static int first_error_after(const struct parity_lcd_trace *t, unsigned from)
{
	unsigned i;

	for (i = from; i < t->n; i++)
		if (t->e[i].kind == PARITY_LCD_T_ERROR)
			return (int)i;
	return -1;
}

/*
 * The display part: check phase, the enable commit, the observation window, the disable commit, and the decision whether
 * the display has let go of the buffer.  Never creates, fills, unpins or destroys the buffer.
 * Returns 1 when the buffer was handed to the display (the enable was attempted), 0 when it never was.
 */
static int show_display(struct parity_lcd_show_env *env, struct parity_scanout *so, struct parity_lcd_show_report *r,
	uint32_t (*verify)(void *ctx, const struct parity_scanout *so), void *verify_ctx)
{
	unsigned disable_from, waited;

	r->surf = so->surf;
	/* ---- check phase ---- */
	env->cfg.fb_fourcc = so->format;
	env->cfg.fb_modifier = so->modifier;
	env->cfg.fb_width = so->width;
	env->cfg.fb_height = so->height;
	env->cfg.fb_pitch = so->pitch;
	env->cfg.fb_surf = (uint32_t)so->surf;
	parity_lcd_trace_init(&show_trace, env->hw);
	parity_lcd_observer_init(&r->obs, env->hw, env->pipe);
	show_trace.tap = observe_tap;
	show_trace.tap_ctx = &r->obs;
	r->prepare_rc = parity_lcd_modeset_prepare(env->lcd, &env->cfg, &show_trace.ops);
	parity_lcd_modeset_status(&r->at_enable);
	if (r->prepare_rc != 0) {
		anomaly(r, r->at_enable.first_error != 0 ? r->at_enable.first_error : "modeset prepare refused the state", r->prepare_rc);
		return 0;
	}
	reached(env, r, PARITY_LCD_SHOW_PREPARED);

	/* ---- commit 1: from here on the display may read the buffer ---- */
	r->begin_rc = parity_scanout_begin(so);
	if (r->begin_rc != 0) {
		anomaly(r, "scanout begin", r->begin_rc);
		return 0;
	}
	r->display_acquired = 1;
	parity_lcd_trace_phase(&show_trace, "commit: enable");
	r->enable_rc = parity_lcd_modeset_commit_enable();
	parity_lcd_modeset_status(&r->at_enable);
	parity_lcd_modeset_link_status(&r->at_enable);
	r->enable_errors = r->at_enable.errors;
	r->first_error_trace_at = show_trace.first_error_at;
	if (r->enable_rc != PARITY_LCD_MS_OK)
		anomaly(r, r->at_enable.first_error != 0 ? r->at_enable.first_error : "the enable commit did not succeed", r->enable_rc);
	reached(env, r, PARITY_LCD_SHOW_ENABLE_RETURNED);

	if (r->enable_rc == PARITY_LCD_MS_OK) {
		/* "the arm was written" is a software fact; whether the timing runs is asked of the frame counter */
		r->first_frames_rc = parity_lcd_observer_frames(&r->obs, env->first_frames_ms, 10u, &r->frame_first, &r->frame_last);
		if (r->first_frames_rc != 0)
			anomaly(r, "the pipe's frame counter does not advance after the enable", r->first_frames_rc);
	}
	if (r->enable_rc == PARITY_LCD_MS_OK && r->first_frames_rc == 0) {
		parity_lcd_observer_steady_begin(&r->obs);
		reached(env, r, PARITY_LCD_SHOW_PICTURE_UP);
		if (env->in_window != 0) {
			r->window_hook_rc = env->in_window(env->in_window_ctx, &r->obs);
			if (r->window_hook_rc != 0)
				anomaly(r, "the in-window test failed while the picture was up", r->window_hook_rc);
		}
		/* the finite window: every round wants 30 more frames (about half a second at 60 Hz) within 1.5 s */
		r->steady_frame_first = r->frame_last;
		for (waited = 0u; r->window_hook_rc == 0 && waited < env->window_ms; waited += 500u) {
			uint32_t f0 = 0u;

			r->steady_rc = parity_lcd_observer_frames(&r->obs, 1500u, 30u, &f0, &r->steady_frame_last);
			parity_lcd_observer_steady_sample(&r->obs);
			r->steady_rounds++;
			if (r->steady_rc != 0) {
				anomaly(r, "the frame counter stopped advancing while the picture was up", r->steady_rc);
				break;
			}
		}
		parity_lcd_observer_steady_end(&r->obs);
		parity_lcd_modeset_status(&r->at_window_end);
		parity_lcd_modeset_link_status(&r->at_window_end);
		if (!r->output_hdmi && r->steady_rc == 0 && (!r->at_window_end.cr_ok || !r->at_window_end.eq_ok))
			anomaly(r, "the sink lost the link while the picture was up", -EIO);
		if (r->obs.seen_steady != 0u)
			anomaly(r, "underrun status during the steady picture", -EIO);
		reached(env, r, PARITY_LCD_SHOW_WINDOW_DONE);
	}

	/* ---- commit 2: the reference's stop path, whatever happened above ---- */
	disable_from = show_trace.n;
	parity_lcd_trace_phase(&show_trace, "commit: disable");
	r->disable_rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_status(&r->at_disable);
	r->cleanup_errors = r->at_disable.errors - r->enable_errors;
	r->cleanup_first_error_trace_at = first_error_after(&show_trace, disable_from);
	if (r->disable_rc != PARITY_LCD_MS_OK)
		anomaly(r, "the disable commit did not succeed", r->disable_rc);
	reached(env, r, PARITY_LCD_SHOW_DISABLE_RETURNED);

	/* ---- has the display let go of the buffer?  Only when the hardware says the pipe stands ---- */
	r->stopped_rc = -EBUSY;
	if (r->disable_rc == PARITY_LCD_MS_OK) {
		r->stopped_rc = parity_lcd_observer_stopped(&r->obs, 200u, &r->stop_frame_first, &r->stop_frame_last);
		r->transconf_after_stop = r->obs.stop_transconf;        /* read at the pipe-disabled point, the well still on */
		if (r->stopped_rc != 0)
			anomaly(r, "after the disable the pipe still reports activity", r->stopped_rc);
	}
	if (r->disable_rc == PARITY_LCD_MS_OK && r->stopped_rc == 0) {
		reached(env, r, PARITY_LCD_SHOW_STOP_CONFIRMED);
		parity_lcd_modeset_plane_released();
		parity_scanout_end(so);
		r->display_released = 1;
		if (verify != 0)
			r->readback_bad_after = verify(verify_ctx, so);
	} else {
		/* not confirmed: the backing pages, their DMA mapping, the GGTT entries, the pin and its owner stay for ever;
		 * the modeset object keeps what it holds (DC_OFF, power domains, DBUF) */
		parity_scanout_abandon(so);
		parity_lcd_modeset_abandoned();
		show_retained = 1;
		show_retained_hw = env->hw;
		r->abandoned = 1;
		reached(env, r, PARITY_LCD_SHOW_ABANDONED);
	}
	return 1;
}

static int show_passed(const struct parity_lcd_show_report *r, int released)
{
	return r->first_anomaly == 0 && r->window_hook_rc == 0 && released && r->enable_rc == PARITY_LCD_MS_OK && r->steady_rc == 0 &&
		(r->output_hdmi || (r->at_enable.cr_ok && r->at_enable.eq_ok)) && r->obs.seen_steady == 0u &&
		r->obs.vblank_unmasked_seen == 0 &&
		r->readback_bad_after == 0u && show_trace.steps == 0u && show_trace.dropped == 0u;
}

static int show_begin(struct parity_lcd_show_env *env, struct parity_lcd_show_report *r)
{
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained())
		return -EBUSY;                  /* an earlier run left resources the display or the GPU may still use: nothing is touched */
	memset(r, 0, sizeof(*r));
	r->output_hdmi = env->cfg.output_hdmi;   /* after the report is cleared */
	r->first_error_trace_at = -1;
	r->cleanup_first_error_trace_at = -1;
	r->trace = &show_trace;
	(void)env;
	return 0;
}

int parity_lcd_show_prepared(struct parity_lcd_show_env *env, struct parity_scanout *so,
	uint32_t (*verify)(void *ctx, const struct parity_scanout *so), void *verify_ctx, struct parity_lcd_show_report *r)
{
	if (env == 0 || r == 0 || env->hw == 0 || env->lcd == 0 || so == 0)
		return -EINVAL;
	if (show_begin(env, r) != 0)
		return -EBUSY;
	if (so->state != PARITY_SCANOUT_PINNED) {
		anomaly(r, "the prepared buffer is not a pinned, unused scanout buffer", -EINVAL);
		return -EINVAL;
	}
	reached(env, r, PARITY_LCD_SHOW_BUFFER_READY);
	(void)show_display(env, so, r, verify, verify_ctx);
	r->released = r->display_released;          /* for the prepared path: the display let go; the OWNER releases the buffer */
	r->pass = show_passed(r, r->display_released);
	return r->pass ? 0 : -1;
}

static uint32_t pattern_verify(void *ctx, const struct parity_scanout *so)
{
	const struct parity_lcd_show_env *env = ctx;

	return parity_lcd_pattern_verify(so->cpu, so->pitch, so->width, so->height, env->pattern_id, 0, 0);
}

/* The CPU-pattern wrapper: creates, fills and releases its OWN buffer around the display part. */
int parity_lcd_show_run(struct parity_lcd_show_env *env, struct parity_lcd_show_report *r)
{
	struct parity_scanout *so;

	if (env == 0 || r == 0 || env->hw == 0 || env->gm == 0 || env->so == 0 || env->lcd == 0)
		return -EINVAL;
	if (show_begin(env, r) != 0)
		return -EBUSY;
	so = env->so;
	if (so->state != PARITY_SCANOUT_NONE) {
		anomaly(r, "the scanout storage is still in use (an earlier buffer was abandoned?)", -EBUSY);
		return -EBUSY;
	}

	/* ---- the buffer ---- */
	r->window_rc = parity_gt_display_window_init(env->gm, PARITY_GT_DISPLAY_PAGES);
	if (r->window_rc != 0 && r->window_rc != -EBUSY) {     /* -EBUSY: claimed earlier, which is fine */
		anomaly(r, "display GGTT window", r->window_rc);
		return -1;
	}
	r->create_rc = parity_scanout_create(env->gm, (uint32_t)env->lcd->mode.hdisplay, (uint32_t)env->lcd->mode.vdisplay,
		PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, so);
	if (r->create_rc != 0) {
		anomaly(r, "scanout create", r->create_rc);
		return -1;
	}
	r->pin_rc = parity_scanout_pin(so, "lcd-b");
	if (r->pin_rc != 0) {
		anomaly(r, "scanout pin", r->pin_rc);
		r->destroy_rc = parity_scanout_destroy(so);
		return -1;
	}
	r->surf = so->surf;
	r->pattern_hash = parity_lcd_pattern_fill(so->cpu, so->pitch, so->width, so->height, env->pattern_id);
	parity_scanout_publish(so);
	r->readback_bad_before = parity_lcd_pattern_verify(so->cpu, so->pitch, so->width, so->height, env->pattern_id, 0, 0);
	if (r->readback_bad_before != 0u || (env->pattern_fnv != 0u && r->pattern_hash != env->pattern_fnv)) {
		anomaly(r, "the picture in the buffer is not the expected one", -EIO);
		goto release_unused;
	}
	reached(env, r, PARITY_LCD_SHOW_BUFFER_READY);

	if (!show_display(env, so, r, pattern_verify, env))
		goto release_unused;
	if (r->display_released) {
		r->unpin_rc = parity_scanout_unpin(so);
		r->destroy_rc = r->unpin_rc == 0 ? parity_scanout_destroy(so) : -EBUSY;
		r->released = r->unpin_rc == 0 && r->destroy_rc == 0;
		if (!r->released)
			anomaly(r, "the buffer could not be released after a confirmed stop", r->unpin_rc != 0 ? r->unpin_rc : r->destroy_rc);
		else
			reached(env, r, PARITY_LCD_SHOW_RELEASED);
	}
	r->pass = show_passed(r, r->released);
	return r->pass ? 0 : -1;

release_unused:
	/* the display never saw this buffer: the ordinary order applies */
	r->unpin_rc = parity_scanout_unpin(so);
	r->destroy_rc = r->unpin_rc == 0 ? parity_scanout_destroy(so) : -EBUSY;
	r->released = r->unpin_rc == 0 && r->destroy_rc == 0;
	return -1;
}
