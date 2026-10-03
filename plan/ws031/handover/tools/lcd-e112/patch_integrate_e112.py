#!/usr/bin/env python3
"""WS031 E-112: integrate the universal-plane words (compat, calc API, build lists, host test, ktest, hw check).
usage: patch_integrate_e112.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
T = lambda n: TAB * n
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---- lcd_compat.h: step hook, crtc-state members the plane functions read
c = load(L + "lcd_compat.h")
c = rep(c, T(1) + "void (*posting_read)(void *ctx, u32 reg);               /* may be NULL */" + NL,
        T(1) + "void (*posting_read)(void *ctx, u32 reg);               /* may be NULL */" + NL +
        T(1) + "void (*step)(void *ctx, const char *name);              /* a callee of the reference that is not ported yet */" + NL)
c = rep(c, T(1) + "bool has_pch_encoder, dither, limited_color_range;" + NL,
        T(1) + "bool has_pch_encoder, dither, limited_color_range;" + NL +
        T(1) + "bool gamma_enable, csc_enable, enable_psr2_sel_fetch;" + NL)
save(L + "lcd_compat.h", c)

# ---- parity_lcd_calc.h
h = load(L + "parity_lcd_calc.h")
h = rep(h, T(1) + "uint8_t rmw;                  /* 1 = read-modify-write (the resulting word depends on the hardware) */" + NL,
        T(1) + "uint8_t rmw;                  /* 1 = read-modify-write (the resulting word depends on the hardware) */" + NL +
        T(1) + "const char *step;             /* not a register operation: a reference callee that is not ported yet, at its position */" + NL)
h = rep(h, "/* how many plain writes to `reg` the list holds; the last one's value in *value */",
        """/*
 * The universal-plane words for ONE full-screen primary plane on a linear XRGB8888 buffer, from the
 * reference's skl_plane_ctl() / glk_plane_color_ctl() and its writers icl_plane_update_noarm() /
 * icl_plane_update_arm(), in their order (PLANE_SURF last: it arms the update).  Watermarks are NOT
 * computed here: the list carries the named step "skl_write_plane_wm" where the reference writes them.
 * `fourcc` / `modifier` other than XRGB8888 / linear, a plane other than the primary, a pitch that is
 * not a multiple of 64 or a surface address that is not 4 KiB aligned: -22.
 */
int parity_lcd_emit_plane(int pipe, int plane_id, uint32_t fourcc, uint64_t modifier, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t surf_ggtt_offset, struct parity_lcd_words *out);

/* how many plain writes to `reg` the list holds; the last one's value in *value */""")
save(L + "parity_lcd_calc.h", h)

# ---- parity_lcd_calc.c
k = load(L + "parity_lcd_calc.c")
k = rep(k, "unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)" + NL + "{",
        """static void record_step(void *ctx, const char *name)
{
	struct parity_lcd_words *out = ctx;

	if (out->n >= PARITY_LCD_MAX_REGWRITES) {
		out->overflow++;
		return;
	}
	memset(&out->w[out->n], 0, sizeof(out->w[out->n]));
	out->w[out->n].step = name;
	out->n++;
}

int parity_plane_emit(struct parity_lcd_emit *emit, int pipe, int plane_id, u32 fourcc, u64 modifier,
	u32 width, u32 height, u32 pitch, u32 surf_ggtt_offset);

int parity_lcd_emit_plane(int pipe, int plane_id, uint32_t fourcc, uint64_t modifier, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t surf_ggtt_offset, struct parity_lcd_words *out)
{
	struct parity_lcd_emit emit;
	int rc;

	if (out == 0)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&emit, 0, sizeof(emit));
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	emit.step = record_step;
	rc = parity_plane_emit(&emit, pipe, plane_id, fourcc, modifier, width, height, pitch, surf_ggtt_offset);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	return rc;
}

unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)
{""")
k = rep(k, "		if (w->w[i].reg == reg && w->w[i].rmw == 0u) {", "		if (w->w[i].reg == reg && w->w[i].rmw == 0u && w->w[i].step == 0) {")
k = k.replace("	out->w[out->n].clear = 0u;\n	out->w[out->n].rmw = 0u;\n", "	out->w[out->n].clear = 0u;\n	out->w[out->n].rmw = 0u;\n	out->w[out->n].step = 0;\n")
k = k.replace("	out->w[out->n].clear = clear;\n	out->w[out->n].rmw = 1u;\n", "	out->w[out->n].clear = clear;\n	out->w[out->n].rmw = 1u;\n	out->w[out->n].step = 0;\n")
k = k.replace("	emit.rmw32 = record_rmw;\n	rc = parity_d", "	emit.rmw32 = record_rmw;\n	emit.step = record_step;\n	rc = parity_d")
save(L + "parity_lcd_calc.c", k)

# ---- build lists
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, L + "intel_vrr_port.c ", L + "intel_vrr_port.c " + L + "skl_plane_port.c ")
save("platform/amd64/vmunix.mk", mk)
sh = load("plan/ws031/tests/run-lcd-host-test.sh")
sh = rep(sh, '"$D/intel_vrr_port.c"', '"$D/intel_vrr_port.c" "$D/skl_plane_port.c"')
save("plan/ws031/tests/run-lcd-host-test.sh", sh)
cg = load("plan/ws031/handover/tools/check_generated.sh")
cg = rep(cg, "lcd_dp_msa.h lcd_drm_colorspace.h", "lcd_dp_msa.h lcd_drm_colorspace.h \\\n\tskl_plane_port.c lcd_plane_types.h lcd_i915_colorkey.h lcd_plane_regs.h lcd_psr_selfetch_regs.h lcd_drm_fourcc.h lcd_drm_plane_defs.h")
save("plan/ws031/handover/tools/check_generated.sh", cg)

# ---- host test
t = load("plan/ws031/tests/lcd-host-test.c")
old = T(1) + 'printf("lcd_host_test: %u checks, %u failures\\n", checks, failures);'
new = r'''	/* ---- the universal plane: one full-screen primary plane on the scanout buffer's layout ---- */
	{
		struct parity_lcd_words w;
		uint32_t v = 0;
		unsigned i, surf_at = 99, ctl_at = 99, wm_at = 99, color_at = 99;
		const uint32_t XR24 = 0x34325258u;      /* 'X','R','2','4' */

		rc = parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7680, 0xfdfc0000u, &w);
		for (i = 0; i < w.n; i++) {
			if (w.w[i].step != 0)
				printf("  plane[%2u] STEP  %s (reference callee, not ported)\n", i, w.w[i].step);
			else
				printf("  plane[%2u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);
			if (w.w[i].step != 0 && strcmp(w.w[i].step, "skl_write_plane_wm") == 0) wm_at = i;
			if (w.w[i].step == 0 && w.w[i].reg == 0x7019c) surf_at = i;
			if (w.w[i].step == 0 && w.w[i].reg == 0x70180) ctl_at = i;
			if (w.w[i].step == 0 && w.w[i].reg == 0x701cc) color_at = i;
		}
		CHECK(rc == 0 && w.overflow == 0, "plane words produced, none dropped");
		/* Linux's dump (regs-selected.txt): DSPACNTR 0x94000000, DSPASTRIDE 0x78, 0x70190 0x0437077f, 0x701cc 0x2000 */
		CHECK(parity_lcd_words_find(&w, 0x70180, &v) == 1 && v == 0x94000000u,
		      "PLANE_CTL_1_A = enable | XRGB8888 | ADL-P arbitration slots for 4 bytes/pixel = Linux's dump (0x94000000)");
		CHECK(parity_lcd_words_find(&w, 0x70188, &v) == 1 && v == 0x78u, "PLANE_STRIDE = pitch / 64 = 120 = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x70190, &v) == 1 && v == 0x0437077fu, "PLANE_SIZE = (1080-1) << 16 | (1920-1) = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x7018c, &v) == 1 && v == 0u, "PLANE_POS = 0,0 (dump 0)");
		CHECK(parity_lcd_words_find(&w, 0x701cc, &v) == 1 && v == 0x00002000u, "PLANE_COLOR_CTL = plane gamma disable, alpha disabled (no alpha channel) = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x7019c, &v) == 1 && v == 0xfdfc0000u, "PLANE_SURF = the scanout buffer's GGTT address (Linux's differs: its own buffer)");
		CHECK(parity_lcd_words_find(&w, 0x701a4, &v) == 1 && v == 0u && parity_lcd_words_find(&w, 0x701c0, &v) == 1 && v == 0u,
		      "PLANE_OFFSET 0 and PLANE_AUX_DIST 0 (no aux plane; ADL-P is not a flat-CCS device so the register is written)");
		CHECK(parity_lcd_words_find(&w, 0x701a0, &v) == 1 && v == 0xff000000u && parity_lcd_words_find(&w, 0x70198, &v) == 1 && v == 0u,
		      "colour key off: KEYMAX carries plane alpha 0xff, KEYMSK 0 (opaque, so no alpha-enable bit)");
		CHECK(parity_lcd_words_find(&w, 0x701c8, &v) == 1 && v == 0u, "PLANE_CUS_CTL = 0 (the primary plane is an HDR plane; no chroma upsampler for RGB)");
		CHECK(surf_at == w.n - 1 && ctl_at == w.n - 2, "PLANE_CTL then PLANE_SURF are the last two operations (the surface write arms the update)");
		CHECK(wm_at != 99 && color_at != 99 && wm_at == color_at + 1 && wm_at < ctl_at,
		      "the watermark write is NOT ported: it shows as a named step right after PLANE_COLOR_CTL, before the arm");
		/* pipe B: the same block one pipe up */
		rc = parity_lcd_emit_plane(1, 0, XR24, 0, 1920, 1080, 7680, 0x00100000u, &w);
		CHECK(rc == 0 && parity_lcd_words_find(&w, 0x71180, &v) == 1 && v == 0x94000000u && parity_lcd_words_find(&w, 0x7119c, &v) == 1 && v == 0x00100000u,
		      "pipe B: PLANE_CTL_1_B / PLANE_SURF_1_B");
		/* what this slice does not cover is refused before the reference code runs */
		CHECK(parity_lcd_emit_plane(0, 0, 0x34324241u /* AB24 */, 0, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0x0100000000000002ull /* I915 Y-tiled */, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 1, XR24, 0, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7000, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7680, 0x100800u, &w) == -22,
		      "another format, a tiled modifier, a sprite plane, a pitch that is not x64 and an unaligned surface are refused");
	}

''' + old
t = rep(t, old, new)
save("plan/ws031/tests/lcd-host-test.c", t)

# ---- ktest
e = load(P + "dp/edp_ktest.c")
e = rep(e, T(3) + "rc = parity_lcd_emit_ddi(&lcd, 0, 0, 0, 0u, &dw, &buf);" + NL,
        T(3) + "{" + NL +
        T(4) + "static struct parity_lcd_words pw;" + NL +
        T(4) + "uint32_t ctl = 0u, stride = 0u, size = 0u, color = 0u, surf = 0u;" + NL + NL +
        T(4) + "rc = parity_lcd_emit_plane(0, 0, 0x34325258u, 0ull, 1920u, 1080u, 7680u, 0x00180000u, &pw);" + NL +
        T(4) + "check(rc == 0 && parity_lcd_words_find(&pw, 0x70180u, &ctl) == 1u && ctl == 0x94000000u &&" + NL +
        T(5) + "parity_lcd_words_find(&pw, 0x70188u, &stride) == 1u && stride == 0x78u &&" + NL +
        T(5) + "parity_lcd_words_find(&pw, 0x70190u, &size) == 1u && size == 0x0437077fu &&" + NL +
        T(5) + "parity_lcd_words_find(&pw, 0x701ccu, &color) == 1u && color == 0x2000u &&" + NL +
        T(5) + "parity_lcd_words_find(&pw, 0x7019cu, &surf) == 1u && surf == 0x00180000u && pw.n >= 2u &&" + NL +
        T(5) + "pw.w[pw.n - 1u].reg == 0x7019cu && pw.w[pw.n - 2u].reg == 0x70180u," + NL +
        T(5) + '"lcd: LCD-A-PLANE-WORDS PLANE_CTL / STRIDE / SIZE / COLOR_CTL / SURF equal Linux\'s dump, PLANE_SURF last");' + NL +
        T(3) + "}" + NL +
        T(3) + "rc = parity_lcd_emit_ddi(&lcd, 0, 0, 0, 0u, &dw, &buf);" + NL)
save(P + "dp/edp_ktest.c", e)

# ---- real hardware: the plane words for the REAL scanout buffer (computed, not written)
x = load(L + "lcd_hw_check.c")
x = rep(x, '#include "lcd_hw_check.h"' + NL, '#include "lcd_hw_check.h"' + NL + '#include "parity_lcd_calc.h"' + NL)
x = rep(x, T(1) + "int rc, pin_rc, unpin_rc, destroy_rc, pass;" + NL,
        T(1) + "int rc, pin_rc, unpin_rc, destroy_rc, pass, plane_rc, plane_ok;" + NL +
        T(1) + "static struct parity_lcd_words pw;" + NL +
        T(1) + "uint32_t p_ctl = 0u, p_stride = 0u, p_size = 0u, p_color = 0u, p_surf = 0u;" + NL)
x = rep(x, T(1) + "/* release: nothing scans it out, so the ordinary order applies */" + NL,
        T(1) + "/* the plane words for THIS buffer, from the reference's plane writers (computed; nothing is written) */" + NL +
        T(1) + "plane_rc = parity_lcd_emit_plane(0, 0, so.format, so.modifier, so.width, so.height, so.pitch, (uint32_t)so.surf, &pw);" + NL +
        T(1) + "plane_ok = plane_rc == 0 && parity_lcd_words_find(&pw, 0x70180u, &p_ctl) == 1u && p_ctl == 0x94000000u &&" + NL +
        T(2) + "parity_lcd_words_find(&pw, 0x70188u, &p_stride) == 1u && p_stride == so.stride_units && p_stride == 0x78u &&" + NL +
        T(2) + "parity_lcd_words_find(&pw, 0x70190u, &p_size) == 1u && p_size == 0x0437077fu &&" + NL +
        T(2) + "parity_lcd_words_find(&pw, 0x701ccu, &p_color) == 1u && p_color == 0x2000u &&" + NL +
        T(2) + "parity_lcd_words_find(&pw, 0x7019cu, &p_surf) == 1u && p_surf == (uint32_t)so.surf && pw.n >= 2u &&" + NL +
        T(2) + "pw.w[pw.n - 1u].reg == 0x7019cu;" + NL +
        T(1) + 'kern_logf("i915: parity SCANOUT-TEST plane words (computed; NOT written): rc=%d n=%u PLANE_CTL=0x%08x STRIDE=0x%x SIZE=0x%08x "' + NL +
        T(2) + '"COLOR_CTL=0x%x SURF=0x%08x (Linux dump: 0x94000000 0x78 0x0437077f 0x2000, its own surf) match=%d\\n",' + NL +
        T(2) + "plane_rc, pw.n, p_ctl, p_stride, p_size, p_color, p_surf, plane_ok);" + NL + NL +
        T(1) + "/* release: nothing scans it out, so the ordinary order applies */" + NL)
x = rep(x, "		gm->display_allocated_pages == 0u;" + NL, "		gm->display_allocated_pages == 0u && plane_ok;" + NL)
save(L + "lcd_hw_check.c", x)
print("done")
