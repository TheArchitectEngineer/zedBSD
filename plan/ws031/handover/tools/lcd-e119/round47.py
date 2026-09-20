#!/usr/bin/env python3
"""WS031 E-119 round 47: the synchronous flip of the running picture.
  glue: intel_crtc_update_active_timings at the reference's place (intel_enable_crtc), the update sequence of
        intel_update_crtc for a plane-only update: noarm -> intel_pipe_update_start -> arm -> intel_pipe_update_end
  modeset: parity_lcd_modeset_flip(): generation, old / new surface, event completion AND the live surface
  model: PLANE_SURF -> PLANE_SURFLIVE only at a frame boundary; vblank / event / irq ops; faults
  host test: A -> B -> A -> B, stale / early event, never latched, no vblank, the stop with a flip pending
usage: round47.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

open(root + L + "parity_flip_glue.inc", "w").write(r"""/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_crtc_port.c: the vblank bookkeeping the update helpers read,
 * and the reference's update of a running crtc reduced to its one primary plane (intel_update_crtc():
 * intel_crtc_planes_update_noarm -> intel_pipe_update_start -> commit_pipe_pre_planes (nothing for a plane-only
 * update) -> intel_crtc_planes_update_arm -> commit_pipe_post_planes (nothing) -> intel_pipe_update_end).
 */
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_flip_pipe;

/* i915's crtc funcs on display version 5+: the hardware frame counter (g4x_get_vblank_counter) */
static const struct drm_crtc_funcs parity_lcd_crtc_funcs = { g4x_get_vblank_counter };

/* intel_enable_crtc(): intel_crtc_update_active_timings(new_crtc_state, false) before the crtc_enable hook;
 * drm_crtc_set_max_vblank_count(intel_crtc_max_vblank_count()) = the full 32-bit counter (not DSI) */
void parity_lcd_ms_active_timings(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->i915.drm.vblank = ms->vblank;
	ms->vblank[ms->crtc.pipe].max_vblank_count = 0xffffffffu;
	ms->crtc.base.funcs = &parity_lcd_crtc_funcs;
	intel_crtc_update_active_timings(&ms->crtc_state, false);
}

void parity_lcd_ms_plane_update_flip(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	parity_lcd_flip_pipe = ms->crtc.pipe;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;         /* same timings: old and new crtc state agree */
	ms->state.old_crtc_state = &ms->crtc_state;
	ms->crtc_state.uapi.mode_changed = false;       /* an update of a running crtc, not a modeset */
	ms->crtc_state.uapi.event = &ms->flip_event;    /* the completion the update arms */
	icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	intel_pipe_update_start(&ms->state, &ms->crtc);
	icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	intel_pipe_update_end(&ms->state, &ms->crtc);
	ms->crtc_state.uapi.event = 0;
}
""")

h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "	unsigned commits;" + NL, "	unsigned commits;" + NL +
        "	struct drm_vblank_crtc vblank[4];        /* dev->vblank[]: hwmode, max_vblank_count */" + NL +
        "	int flip_event;                          /* the event token of the pending update */" + NL +
        "	u32 fb_fourcc, fb_width, fb_height, fb_pitch; u64 fb_modifier;" + NL +
        "	u32 cur_surf, pend_surf, old_surf; int flip_pending, flip_stuck; unsigned flip_gen;" + NL)
h = rep(h, "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);",
        "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);" + NL +
        "void parity_lcd_ms_active_timings(struct parity_lcd_modeset *ms);                         /* intel_crtc_port.c */" + NL +
        "void parity_lcd_ms_plane_update_flip(struct parity_lcd_modeset *ms);")
h = rep(h, '#include "lcd_wm_compat.h"', '#include "lcd_wm_compat.h"' + NL + '#include "lcd_flip_compat.h"')
save(L + "parity_lcd_modeset_int.h", h)

a = load(L + "parity_lcd_modeset.h")
a = rep(a, "int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);", """int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);
/*
 * Flip the running picture to another buffer (same format / size / pitch): the reference's update of a running crtc
 * (plane noarm, intel_pipe_update_start -- vblank evasion --, plane arm, intel_pipe_update_end -- the event is armed).
 * COMPLETE only when the event completed AND the pipe's live surface (PLANE_SURFLIVE) is the new buffer; then the old
 * buffer is no longer displayed.  Otherwise both stay protected and further flips are refused (flip_stuck) until the
 * display is stopped.  One flip pending at a time; one display owner.
 */
struct parity_lcd_flip_result {
	unsigned gen;
	uint32_t old_surf, new_surf, live_before, live_after;
	uint32_t frame_before, frame_after;
	int event_rc;
	int update_errors;                      /* reference errors during the update (e.g. "Atomic update failure") */
	int result;                             /* PARITY_LCD_FLIP_* */
};
#define PARITY_LCD_FLIP_DONE        0       /* the new buffer is displayed; the old one is released */
#define PARITY_LCD_FLIP_NOT_LATCHED 1       /* the event came but the live surface is not the new one: both kept */
#define PARITY_LCD_FLIP_TIMEOUT     2       /* no completion: both kept */
#define PARITY_LCD_FLIP_REFUSED     3       /* nothing written (not running, a flip stuck, same buffer, bad address) */
int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out);""")
a = rep(a, "	int dither;", "	int dither;" + NL + "	uint32_t cur_surf, pend_surf;       /* displayed / pending (valid while flip_pending) */" + NL +
        "	int flip_pending, flip_stuck; unsigned flip_gen;")
save(L + "parity_lcd_modeset.h", a)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	rc = parity_lcd_ms_plane_prepare(&ms, cfg->fb_fourcc, cfg->fb_modifier, cfg->fb_width, cfg->fb_height,",
        "	ms.fb_fourcc = cfg->fb_fourcc; ms.fb_modifier = cfg->fb_modifier; ms.fb_width = cfg->fb_width;" + NL +
        "	ms.fb_height = cfg->fb_height; ms.fb_pitch = cfg->fb_pitch; ms.cur_surf = cfg->fb_surf;" + NL +
        "	rc = parity_lcd_ms_plane_prepare(&ms, cfg->fb_fourcc, cfg->fb_modifier, cfg->fb_width, cfg->fb_height,")
r = rep(r, "	parity_lcd_ms_crtc_enable(&ms);" + NL, "	parity_lcd_ms_active_timings(&ms);        /* intel_enable_crtc(): before the crtc_enable hook */" + NL +
        "	parity_lcd_ms_crtc_enable(&ms);" + NL)
r = rep(r, "	out->dither = ms.crtc_state.dither;", "	out->dither = ms.crtc_state.dither;" + NL + "	out->cur_surf = ms.cur_surf;" + NL +
        "	out->pend_surf = ms.pend_surf;" + NL + "	out->flip_pending = ms.flip_pending;" + NL + "	out->flip_stuck = ms.flip_stuck;" + NL + "	out->flip_gen = ms.flip_gen;")
r = rep(r, "void parity_lcd_modeset_plane_released(void)" + NL + "{" + NL + "	ms.plane_armed = 0;",
        "void parity_lcd_modeset_plane_released(void)" + NL + "{" + NL + "	ms.plane_armed = 0;" + NL +
        "	ms.flip_pending = 0;                     /* the display reads neither buffer any more */" + NL + "	ms.flip_stuck = 0;")
r = r.rstrip(NL) + NL + """
static uint32_t live_surf(void)
{
	return ms_ops->read32(ms_ops->ctx, i915_mmio_reg_offset(PLANE_SURFLIVE(ms.crtc.pipe, PLANE_PRIMARY)));
}

static uint32_t frame_now(void)
{
	return ms_ops->read32(ms_ops->ctx, i915_mmio_reg_offset(PIPE_FRMCOUNT_G4X(ms.crtc.pipe)));
}

int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out)
{
	struct parity_lcd_flip_result res;
	unsigned before;
	int rc;

	memset(&res, 0, sizeof(res));
	res.result = PARITY_LCD_FLIP_REFUSED;
	res.old_surf = ms.cur_surf;
	res.new_surf = new_surf;
	if (!ms.prepared || !ms.crtc.active || !ms.plane_armed || parity_lcd_modeset_retained() || ms.flip_pending || ms.flip_stuck ||
	    new_surf == ms.cur_surf || (new_surf & 0xfffu) != 0u || ms_ops->vblank_get == 0 || ms_ops->wait_event == 0) {
		if (out != 0)
			*out = res;
		return PARITY_LCD_MS_NOT_PREPARED;
	}
	parity_lcd_cur_i915 = &ms.i915;
	res.gen = ++ms.flip_gen;
	res.live_before = live_surf();
	res.frame_before = frame_now();
	/* the new plane state: the same layout, another surface */
	rc = parity_lcd_ms_plane_prepare(&ms, ms.fb_fourcc, ms.fb_modifier, ms.fb_width, ms.fb_height, ms.fb_pitch, new_surf);
	if (rc != 0) {
		(void)parity_lcd_ms_plane_prepare(&ms, ms.fb_fourcc, ms.fb_modifier, ms.fb_width, ms.fb_height, ms.fb_pitch, ms.cur_surf);
		if (out != 0)
			*out = res;
		return PARITY_LCD_MS_NOT_PREPARED;
	}
	/* from here both buffers may be read by the display until the completion is known */
	ms.old_surf = ms.cur_surf;
	ms.pend_surf = new_surf;
	ms.flip_pending = 1;
	before = ms_errors;
	parity_lcd_ms_plane_update_flip(&ms);
	res.update_errors = (int)(ms_errors - before);
	/* the event armed by intel_pipe_update_end(): completed by the pipe's next vblank */
	res.event_rc = ms_ops->wait_event(ms_ops->ctx, ms.crtc.pipe, 100u);
	res.live_after = live_surf();
	res.frame_after = frame_now();
	if (res.event_rc == 0) {
		/* drm_send_event: the event's vblank reference (taken in intel_pipe_update_end) is dropped */
		ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
		if (res.live_after == new_surf) {
			ms.cur_surf = new_surf;
			ms.flip_pending = 0;
			res.result = PARITY_LCD_FLIP_DONE;
		} else {
			ms.flip_stuck = 1;
			res.result = PARITY_LCD_FLIP_NOT_LATCHED;
		}
	} else {
		ms.flip_stuck = 1;              /* the event reference stays: the completion may still come */
		res.result = PARITY_LCD_FLIP_TIMEOUT;
	}
	if (out != 0)
		*out = res;
	if (res.result != PARITY_LCD_FLIP_DONE)
		return PARITY_LCD_MS_ERRORS;
	return res.update_errors != 0 ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}
"""
save(L + "parity_lcd_modeset.c", r)

# ---------------- model
mh = load(L + "lcd_fake_hw.h")
mh = rep(mh, "	int fault_power_get;", "	int fault_flip_never_latch;             /* PLANE_SURF never reaches PLANE_SURFLIVE */" + NL +
         "	int fault_early_event;                  /* the event reports completion without a vblank having passed */" + NL +
         "	int fault_no_vblank;                    /* no vblank interrupt arrives (sleeps / waits time out) */" + NL + "	int fault_power_get;")
mh = rep(mh, "	uint8_t obs[48]; unsigned nobs;", "	uint8_t obs[48]; unsigned nobs;" + NL +
         "	/* the double-buffered surface: written -> live at the next frame boundary */" + NL +
         "	uint32_t surf_live, surf_pending; int surf_pending_valid; uint32_t surf_pending_frame;" + NL +
         "	int irq_off, vblank_refs, event_armed; unsigned irq_off_calls, vblank_sleeps, events_armed, events_done;" + NL +
         "	unsigned arm_outside_section;           /* a PLANE_SURF write outside irq_off (counted, see the tests) */")
save(L + "lcd_fake_hw.h", mh)
mc = load(L + "lcd_fake_hw.c")
mc = rep(mc, "#define REG_PLANE_WM0(p)", "#define REG_PLANE_SURFLIVE(p) (0x701acu + 0x1000u * (unsigned)(p))" + NL + "#define REG_PLANE_WM0(p)")
mc = rep(mc, "static uint32_t f_read32(void *ctx, uint32_t reg)" + NL + "{" + NL + "	struct lcd_fake_hw *hw = ctx;" + NL,
         """static uint32_t frame_of(const struct lcd_fake_hw *hw)
{
	return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;
}

/* the next frame boundary after now */
static void to_next_frame(struct lcd_fake_hw *hw)
{
	uint32_t f = frame_of(hw) + 1u;

	if (hw->pipe_on_since_us != 0u)
		hw->dpf->now_us = hw->pipe_on_since_us + ((uint64_t)f * 1000000u + 59u) / 60u;
}

static uint32_t f_read32(void *ctx, uint32_t reg)
{
	struct lcd_fake_hw *hw = ctx;

	if (reg == REG_PLANE_SURFLIVE(hw->pipe)) {
		if (hw->surf_pending_valid && !hw->fault_flip_never_latch && frame_of(hw) > hw->surf_pending_frame) {
			hw->surf_live = hw->surf_pending;
			hw->surf_pending_valid = 0;
		}
		return hw->surf_live;
	}
""")
mc = rep(mc, "		if (ctl & PLANE_CTL_ENABLE) {" + NL + "			hw->plane_arms++;",
         "		/* double buffered: live at the next frame boundary (the first arm of a starting pipe latches at once) */" + NL +
         "		if (hw->surf_live == 0u || hw->pipe_on_since_us == 0u) {" + NL + "			hw->surf_live = value;" + NL + "		} else {" + NL +
         "			hw->surf_pending = value;" + NL + "			hw->surf_pending_valid = 1;" + NL + "			hw->surf_pending_frame = frame_of(hw);" + NL + "		}" + NL +
         "		if (!hw->irq_off)" + NL + "			hw->arm_outside_section++;" + NL +
         "		if (ctl & PLANE_CTL_ENABLE) {" + NL + "			hw->plane_arms++;")
mc = rep(mc, "static void f_observe(void *ctx, int point)", """static int f_vblank_get(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->vblank_refs++;
	return 0;
}

static void f_vblank_put(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (hw->vblank_refs > 0)
		hw->vblank_refs--;
	else
		hw->power_underflows++;
}

static long f_vblank_sleep(void *ctx, int pipe, long ticks)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->vblank_sleeps++;
	if (hw->fault_no_vblank) {
		hw->dpf->now_us += (uint64_t)ticks * 10000u;
		return 0;
	}
	to_next_frame(hw);
	hw->scanline = 0u;                      /* just after the vblank: far from the evasion window */
	return ticks > 1 ? ticks - 1 : 0;
}

static void f_irq_off(void *ctx)
{
	struct lcd_fake_hw *hw = ctx;

	if (hw->irq_off)
		hw->lock_errors++;
	hw->irq_off = 1;
	hw->irq_off_calls++;
}

static void f_irq_on(void *ctx)
{
	struct lcd_fake_hw *hw = ctx;

	if (!hw->irq_off)
		hw->lock_errors++;
	hw->irq_off = 0;
}

static void f_arm_event(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->event_armed = 1;
	hw->events_armed++;
}

static int f_wait_event(void *ctx, int pipe, unsigned timeout_ms)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (!hw->event_armed)
		return -22;
	if (hw->fault_no_vblank) {
		hw->dpf->now_us += (uint64_t)timeout_ms * 1000u;
		return -110;
	}
	if (!hw->fault_early_event)
		to_next_frame(hw);              /* the event completes at the next vblank */
	hw->event_armed = 0;
	hw->events_done++;
	return 0;
}

static void f_observe(void *ctx, int point)""")
mc = rep(mc, "	hw->ops.observe = f_observe;", "	hw->ops.observe = f_observe;" + NL + "	hw->ops.vblank_get = f_vblank_get;" + NL +
         "	hw->ops.vblank_put = f_vblank_put;" + NL + "	hw->ops.vblank_sleep = f_vblank_sleep;" + NL + "	hw->ops.irq_off = f_irq_off;" + NL +
         "	hw->ops.irq_on = f_irq_on;" + NL + "	hw->ops.arm_event = f_arm_event;" + NL + "	hw->ops.wait_event = f_wait_event;")
save(L + "lcd_fake_hw.c", mc)

# the recorder forwards the new ops to its backend (with the backend's ctx); they are not register traffic
tc = load(L + "parity_lcd_trace.c")
tc = rep(tc, "static void t_lock(void *ctx, int which, int take)", """#define TB(ctx) (((struct parity_lcd_trace *)(ctx))->backend)
static int t_vblank_get(void *ctx, int pipe) { return TB(ctx)->vblank_get(TB(ctx)->ctx, pipe); }
static void t_vblank_put(void *ctx, int pipe) { TB(ctx)->vblank_put(TB(ctx)->ctx, pipe); }
static long t_vblank_sleep(void *ctx, int pipe, long ticks) { return TB(ctx)->vblank_sleep(TB(ctx)->ctx, pipe, ticks); }
static void t_irq_off(void *ctx) { TB(ctx)->irq_off(TB(ctx)->ctx); }
static void t_irq_on(void *ctx) { TB(ctx)->irq_on(TB(ctx)->ctx); }
static void t_arm_event(void *ctx, int pipe) { TB(ctx)->arm_event(TB(ctx)->ctx, pipe); }
static int t_wait_event(void *ctx, int pipe, unsigned timeout_ms) { return TB(ctx)->wait_event(TB(ctx)->ctx, pipe, timeout_ms); }

static void t_lock(void *ctx, int which, int take)""")
tc = rep(tc, "	t->ops.observe = t_observe;", "	t->ops.observe = t_observe;" + NL +
         "	if (backend->vblank_get != 0) {" + NL + "		t->ops.vblank_get = t_vblank_get;" + NL + "		t->ops.vblank_put = t_vblank_put;" + NL +
         "		t->ops.vblank_sleep = t_vblank_sleep;" + NL + "		t->ops.irq_off = t_irq_off;" + NL + "		t->ops.irq_on = t_irq_on;" + NL +
         "		t->ops.arm_event = t_arm_event;" + NL + "		t->ops.wait_event = t_wait_event;" + NL + "	}")
save(L + "parity_lcd_trace.c", tc)
print("done")
