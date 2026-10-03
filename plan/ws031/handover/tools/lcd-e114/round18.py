#!/usr/bin/env python3
"""WS031 E-115 round 18: kernel-build fixes; the plane-word test sees real watermark writes instead of a step.
usage: round18.py <repo root>"""
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
q = rep(q, "#define intel_atomic_get_new_crtc_state(state, crtc) ((state)->crtc_state)",
        "#define intel_atomic_get_new_crtc_state(state, crtc) ((struct intel_crtc_state *)(state)->crtc_state)   /* the check phase writes into it */")
save(L + "lcd_seq_compat.h", q)
w = load(L + "lcd_wm_compat.h")
w = rep(w, "#define fls(x)", "#define ffs(x) __builtin_ffs((int)(x))" + NL + "#define fls(x)")
save(L + "lcd_wm_compat.h", w)

t = load("plan/ws031/tests/lcd-host-test.c")
t = rep(t, "			if (w.w[i].step != 0 && strcmp(w.w[i].step, \"skl_write_plane_wm\") == 0) wm_at = i;",
        "			if (w.w[i].step == 0 && w.w[i].reg == 0x7027c) wm_at = i;      /* PLANE_BUF_CFG: the last write of skl_write_plane_wm() */")
t = rep(t, """		CHECK(wm_at != 99 && color_at != 99 && wm_at == color_at + 1 && wm_at < ctl_at,
		      "the watermark write is NOT ported: it shows as a named step right after PLANE_COLOR_CTL, before the arm");""",
        """		CHECK(wm_at != 99 && color_at != 99 && wm_at > color_at && wm_at < ctl_at,
		      "skl_write_plane_wm() runs for real between PLANE_COLOR_CTL and the arm (here with an empty watermark state: the words-only API computes none)");""")
save("plan/ws031/tests/lcd-host-test.c", t)
print("done")
