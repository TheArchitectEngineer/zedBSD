#!/usr/bin/env python3
"""WS031 E-115 round 7: three callees settled by reading the reference -- no watermark hook on skl+, underrun
reporting and DRM vblank bookkeeping are DECIDED not to be connected (recorded as such, with the reason).
usage: round7.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

q = load(L + "lcd_seq_compat.h")
for n in ["intel_initial_watermarks", "intel_set_cpu_fifo_underrun_reporting", "intel_crtc_vblank_on", "intel_crtc_vblank_off"]:
    lines = [l for l in q.split(NL) if l.startswith("#define " + n + "(")]
    assert len(lines) == 1, n
    q = q.replace(lines[0] + NL, "")
q = rep(q, "/* ---- hsw_crtc_enable(): callees that are not ported ---- */", """/*
 * ---- DECIDED: callees that are deliberately not connected in the one-screen path.  Each leaves a
 * "(decided)" entry in the run log at the position the reference calls it, with the reason here.
 */
#define PARITY_LCD_DECIDED(i915, what) PARITY_LCD_STEP(i915, "(decided) " what)
/* intel_initial_watermarks(): calls display.funcs.wm->initial_watermarks, which skl_wm_funcs (display version 9+)
 * does not set: nothing happens there.  The watermarks are written by the plane update (skl_write_plane_wm). */
#define intel_initial_watermarks(state, crtc) (false)
/* intel_set_cpu_fifo_underrun_reporting(): clears the sticky underrun bits of ICL_PIPESTATUS and unmasks the
 * pipe's underrun interrupt -- error REPORTING only.  The display interrupt is not wired in this path; the
 * LCD test reads the sticky bits itself after the observation window and logs them. */
#define intel_set_cpu_fifo_underrun_reporting(i915, pipe, enable) PARITY_LCD_DECIDED(i915, "intel_set_cpu_fifo_underrun_reporting: underrun IRQ not wired; PIPESTATUS is read by the test")
/* intel_crtc_vblank_on() / _off(): the DRM core's software vblank bookkeeping (drm_crtc_vblank_on / _off).  No
 * register is written there; nothing in this path waits on a DRM vblank event (frames are observed through the
 * pipe's frame counter / scanline). */
#define intel_crtc_vblank_on(cs) PARITY_LCD_DECIDED(SEQ_I915_CRTC_STATE(cs), "intel_crtc_vblank_on: DRM vblank bookkeeping, no hardware access")
#define intel_crtc_vblank_off(cs) PARITY_LCD_DECIDED(parity_lcd_cur_i915, "intel_crtc_vblank_off: DRM vblank bookkeeping, no hardware access")

/* ---- hsw_crtc_enable(): callees that are not ported ---- */""")
save(L + "lcd_seq_compat.h", q)

t = load(L + "parity_lcd_trace.c")
t = rep(t, """	t->steps++;
	named(t, PARITY_LCD_T_STEP, name);""", """	if (strncmp(name, "(decided) ", 10) == 0) {
		t->decided++;
		named(t, PARITY_LCD_T_DECIDED, name);
	} else {
		t->steps++;
		named(t, PARITY_LCD_T_STEP, name);
	}""")
t = rep(t, "		if (kind == PARITY_LCD_T_STEP || kind == PARITY_LCD_T_ERROR || kind == PARITY_LCD_T_PHASE) {",
        "		if (kind == PARITY_LCD_T_STEP || kind == PARITY_LCD_T_ERROR || kind == PARITY_LCD_T_PHASE || kind == PARITY_LCD_T_DECIDED) {")
save(L + "parity_lcd_trace.c", t)
h = load(L + "parity_lcd_trace.h")
h = rep(h, "	PARITY_LCD_T_PHASE,         /* name: a marker written by the caller */",
        "	PARITY_LCD_T_PHASE,         /* name: a marker written by the caller */" + NL +
        "	PARITY_LCD_T_DECIDED,       /* name: a reference callee deliberately not connected (reason in lcd_seq_compat.h) */")
h = rep(h, "	unsigned writes, rmws, waits, wait_timeouts, steps, errors, sleeps;",
        "	unsigned writes, rmws, waits, wait_timeouts, steps, decided, errors, sleeps;")
save(L + "parity_lcd_trace.h", h)

x = load("plan/ws031/tests/lcd-modeset-host-test.c")
x = rep(x, '"STEP", "ERROR", "PHASE" };', '"STEP", "ERROR", "PHASE", "decide" };')
save("plan/ws031/tests/lcd-modeset-host-test.c", x)
print("done")
