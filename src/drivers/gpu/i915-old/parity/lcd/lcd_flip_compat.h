/*
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

#include "lcd_mreg_display_types.h"      /* reference, extracted: I915_MODE_FLAG_* */
/* display/intel_crtc.h: CONFIG_PROVE_LOCKING is off */
#define VBLANK_EVASION_TIME_US 100
/* the reference's non-debug build: dbg_vblank_evade() is empty (CONFIG_DRM_I915_DEBUG_VBLANK_EVADE off) */
#define dbg_vblank_evade(crtc, end) ((void)(crtc), (void)(end))
/* drm_vblank.h: what these helpers read */
struct drm_vblank_crtc { struct drm_display_mode hwmode; u32 max_vblank_count; };
struct drm_crtc_funcs { u32 (*get_vblank_counter)(struct drm_crtc *crtc); };
#define spin_lock_irqsave(l, f) ((void)(l), (f) = 0)
#define spin_unlock_irqrestore(l, f) ((void)(l), (void)(f))
int intel_get_crtc_scanline(struct intel_crtc *crtc);
u32 g4x_get_vblank_counter(struct drm_crtc *crtc);
void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state, bool vrr_enable);
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
/* local_irq_*: the section state is tracked so that save / restore nest the way the kernel primitives do */
void parity_lcd_irq_disable(void);
void parity_lcd_irq_enable(void);
unsigned long parity_lcd_irq_save(void);
void parity_lcd_irq_restore(unsigned long was_off);
#define local_irq_disable() parity_lcd_irq_disable()
#define local_irq_enable() parity_lcd_irq_enable()
#define local_irq_save(f) ((f) = parity_lcd_irq_save())
#define local_irq_restore(f) parity_lcd_irq_restore(f)
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
