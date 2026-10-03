#!/usr/bin/env python3
"""WS031 E-119 round 45: generator spec for the synchronous plane update (vblank evasion + event) and its DRM compat
over new parity_lcd_ops hooks.  usage: round45.py <repo root>"""
import sys, json
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

j = json.load(open(root + J))
for f in j["new_files"]:
    if f["out"] == "intel_crtc_port.c":
        for n in ("intel_crtc_get_vblank_counter", "intel_crtc_needs_vblank_work", "intel_mode_vblank_start",
                  "intel_crtc_vblank_evade_scanlines", "intel_pipe_update_start", "intel_pipe_update_end"):
            if n not in f["functions"]:
                f["functions"].append(n)
        if "lcd_flip_compat.h" not in f["includes"]:
            f["includes"].append("lcd_flip_compat.h")
        f["glue"] = "parity_flip_glue.inc"
    if f["out"] == "intel_vblank_port.c":
        for n in ("g4x_get_vblank_counter", "__intel_get_crtc_scanline", "intel_get_crtc_scanline", "intel_crtc_scanline_offset",
                  "intel_crtc_update_active_timings"):
            if n not in f["functions"]:
                f["functions"].append(n)
        if "lcd_flip_compat.h" not in f["includes"]:
            f["includes"].append("lcd_flip_compat.h")
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

o = load(L + "parity_lcd_ops.h")
o = rep(o, "	/* optional: a named point of the commit was reached (enum parity_lcd_observe); the real device samples here */",
        """	/*
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
	/* optional: a named point of the commit was reached (enum parity_lcd_observe); the real device samples here */""")
save(L + "parity_lcd_ops.h", o)

open(root + L + "lcd_flip_compat.h", "w").write(r"""/*
 * WS031 Linux-parity -- what the reference's synchronous plane update (intel_pipe_update_start / _end, the scanline and
 * vblank counter helpers of intel_vblank.c) needs from the DRM vblank core and the kernel, mapped onto parity_lcd_ops.
 * zedBSD project code.
 *
 * ADAPTATIONS (the rest is the reference text):
 *   - the DRM vblank wait queue + schedule_timeout(): ops->vblank_sleep (sleeps until the pipe's next vblank interrupt
 *     or the timeout, on the real device through the pipe's vblank completion);
 *   - drm_crtc_arm_vblank_event(): ops->arm_event -- one pending event per pipe, completed by the pipe's next vblank;
 *   - local_irq_disable / _enable: ops->irq_off / _on;
 *   - vblank_time_lock and the uncore-lock section around the active timings / scanline read: the modeset object has
 *     one owner and runs serially; these locks guard against concurrent commits and vblank-timestamp readers that do
 *     not exist on this path (recorded, not general);
 *   - ktime / tracepoints / the evasion statistics: not kept (debug only).
 */
#ifndef PARITY_LCD_FLIP_COMPAT_H
#define PARITY_LCD_FLIP_COMPAT_H

#define FLIP_OPS(i915) ((i915)->emit)
#define drm_crtc_index(c) ((unsigned int)to_intel_crtc(c)->pipe)
#define drm_crtc_vblank_get(c) (FLIP_OPS(to_i915((c)->dev))->vblank_get(FLIP_OPS(to_i915((c)->dev))->ctx, (int)to_intel_crtc(c)->pipe))
#define drm_crtc_vblank_put(c) FLIP_OPS(to_i915((c)->dev))->vblank_put(FLIP_OPS(to_i915((c)->dev))->ctx, (int)to_intel_crtc(c)->pipe)
#define drm_crtc_vblank_waitqueue(c) ((wait_queue_head_t *)(c))
typedef struct parity_wq wait_queue_head_t;
#define DEFINE_WAIT(w) int w = 0
#define prepare_to_wait(wq, w, st) ((void)(wq), (void)(w))
#define finish_wait(wq, w) ((void)(wq), (void)(w))
#define TASK_UNINTERRUPTIBLE 0
#define schedule_timeout(t) FLIP_OPS(parity_lcd_cur_i915)->vblank_sleep(FLIP_OPS(parity_lcd_cur_i915)->ctx, parity_lcd_flip_pipe, (t))
extern int parity_lcd_flip_pipe;
/* msecs_to_jiffies_timeout(): msecs_to_jiffies(m) + 1 with the kernel's 10 ms tick */
#define msecs_to_jiffies_timeout(m) ((long)(((m) + 9) / 10) + 1)
#define local_irq_disable() FLIP_OPS(parity_lcd_cur_i915)->irq_off(FLIP_OPS(parity_lcd_cur_i915)->ctx)
#define local_irq_enable() FLIP_OPS(parity_lcd_cur_i915)->irq_on(FLIP_OPS(parity_lcd_cur_i915)->ctx)
#define local_irq_save(f) ((f) = 0, local_irq_disable())
#define local_irq_restore(f) ((void)(f), local_irq_enable())
#define drm_crtc_arm_vblank_event(c, e) FLIP_OPS(to_i915((c)->dev))->arm_event(FLIP_OPS(to_i915((c)->dev))->ctx, (int)to_intel_crtc(c)->pipe)
#define drm_crtc_accurate_vblank_count(c) (parity_lcd_error("drm_crtc_accurate_vblank_count reached (max_vblank_count is set)\n"), 0u)
#define drm_vblank_work_schedule(w, c, n) PARITY_LCD_GUARD(0, "drm_vblank_work_schedule (vblank work: colour updates only)")
#define intel_crtc_vblank_work_init(cs) PARITY_LCD_GUARD(0, "intel_crtc_vblank_work_init (vblank work: colour updates only)")
#define intel_crtc_needs_color_update(cs) (false)       /* a plane-only update: no colour management change */
#define intel_color_uses_dsb(cs) (false)
#define intel_psr_lock(cs) PARITY_LCD_GUARD(!(cs)->has_psr, "intel_psr_lock (PSR)")
#define intel_psr_unlock(cs) PARITY_LCD_GUARD(!(cs)->has_psr, "intel_psr_unlock (PSR)")
#define intel_psr_wait_for_idle_locked(cs) PARITY_LCD_GUARD(!(cs)->has_psr, "intel_psr_wait_for_idle_locked (PSR)")
#define intel_vgpu_active(i915) (false)
#define icl_dsi_frame_update(cs) PARITY_LCD_GUARD(0, "icl_dsi_frame_update (DSI)")
#define intel_vrr_send_push(cs) PARITY_LCD_GUARD(!(cs)->vrr.enable, "intel_vrr_send_push (VRR)")
#define intel_vrr_is_push_sent(cs) (false)
#define intel_vrr_vmin_vblank_start(cs) (parity_lcd_error("VRR vblank start reached (VRR is not used)\n"), 0)
#define intel_vrr_vmax_vblank_start(cs) (parity_lcd_error("VRR vblank start reached (VRR is not used)\n"), 0)
#define __intel_get_crtc_scanline_from_timestamp(c) (parity_lcd_error("scanline from timestamp reached (DSI only)\n"), 0)
#define intel_vblank_section_enter(i915) ((void)0)
#define intel_vblank_section_exit(i915) ((void)0)
#define drm_calc_timestamping_constants(c, m) (to_i915((c)->dev)->drm.vblank[drm_crtc_index(c)].hwmode = *(m))
#define drm_mode_init(dst, src) (*(dst) = *(src))
typedef long long ktime_t;
#define ktime_get() ((ktime_t)0)
#define ktime_us_delta(a, b) ((long long)((a) - (b)))
#define trace_intel_pipe_update_start(c) ((void)0)
#define trace_intel_pipe_update_vblank_evaded(c) ((void)0)
#define trace_intel_pipe_update_end(c, n, s) ((void)0)
#define spin_lock_irq(l) ((void)(l))
#define spin_unlock_irq(l) ((void)(l))
#define spin_lock(l) ((void)(l))
#define spin_unlock(l) ((void)(l))

#endif /* PARITY_LCD_FLIP_COMPAT_H */
""")
open(root + L + "parity_flip_glue.inc", "w").write("/* placeholder */" + NL)
print("done")
