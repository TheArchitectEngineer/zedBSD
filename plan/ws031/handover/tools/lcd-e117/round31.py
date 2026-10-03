#!/usr/bin/env python3
"""WS031 E-117 round 31: regression tests of the four boundary fixes; independent DBUF address check; dither derivation.
usage: round31.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ---- status: the dither decision, so its derivation can be checked
a = load(L + "parity_lcd_modeset.h")
a = rep(a, "	int stop_unconfirmed, retained;", "	int stop_unconfirmed, retained;" + NL + "	int dither;                     /* crtc state: derived from pipe_bpp (intel_modeset_pipe_config) */")
save(L + "parity_lcd_modeset.h", a)
r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	out->retained = parity_lcd_modeset_retained();", "	out->retained = parity_lcd_modeset_retained();" + NL + "	out->dither = ms.crtc_state.dither;")
save(L + "parity_lcd_modeset.c", r)

# ---- independent DBUF address: the reference's own macro vs the table the power-domain init uses
rg = load(L + "parity_lcd_regs.c")
rg = rg.rstrip(NL) + NL + """
/* DBUF_CTL_S(slice) as the saved reference defines it (extracted skl_watermark_regs.h), for the independent check of the
 * table the power-domain initialisation uses (display_core.c) -- E-116 found that table shifted by one slice */
uint32_t parity_lcd_ref_dbuf_ctl(unsigned slice)
{
	return slice < 4u ? i915_mmio_reg_offset(DBUF_CTL_S((enum dbuf_slice)slice)) : 0u;
}
"""
save(L + "parity_lcd_regs.c", rg)
oh = load(L + "parity_lcd_observe.h")
oh = rep(oh, "uint32_t parity_lcd_reg_by_name(const char *name);", "uint32_t parity_lcd_reg_by_name(const char *name);" + NL +
         "uint32_t parity_lcd_ref_dbuf_ctl(unsigned slice);       /* the reference's DBUF_CTL_S(slice), slice 0 = S1 */")
save(L + "parity_lcd_observe.h", oh)
dc = load(P + "display_core.c")
dc = rep(dc, "/* the same body for the modeset's intel_dbuf_pre/post_plane_update() */",
         "/* the table itself, for the independent check against the reference's macro (GPU-free ktest) */" + NL +
         "uint32_t" + NL + "parity_dbuf_ctl_reg(unsigned slice)" + NL + "{" + NL + "	return slice < 4u ? dbuf_ctl_s[slice] : 0u;" + NL + "}" + NL + NL +
         "/* the same body for the modeset's intel_dbuf_pre/post_plane_update() */")
save(P + "display_core.c", dc)
dh = load(P + "display_core.h")
dh = rep(dh, "/* Exposed for tests. */", "/* Exposed for tests. */" + NL + "uint32_t parity_dbuf_ctl_reg(unsigned slice);")
save(P + "display_core.h", dh)

# ---- host test
t = load(T)
t = rep(t, "	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, \"C: and a new modeset is refused while that is so\");" + NL +
        "	parity_lcd_modeset_abandoned();",
        """	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "C: and a new modeset is refused while that is so");
	parity_lcd_modeset_abandoned();
	{
		struct parity_lcd_emit not_model = trace.ops;

		not_model.model = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16 && parity_lcd_modeset_commit_enable() == PARITY_LCD_MS_NOT_PREPARED,
		      "C: after abandon, a second call of the same entry is refused (prepare and commit)");
		parity_lcd_modeset_status(&s);
		CHECK(s.retained == 1 && s.stop_unconfirmed == 1 && s.dc_off_held == 1 && s.crtc_domains_held == 4 && s.plane_armed == 1 &&
		      lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 && lcd.dbuf_enabled == 0x0f,
		      "C: ... and the retained state is still recorded: nothing was forgotten by the refused calls");
		CHECK(parity_lcd_modeset_discard_model(&not_model) == -1 && parity_lcd_modeset_retained() == 1,
		      "C: a backend that is not a model (real hardware) cannot release it");
		CHECK(parity_lcd_modeset_discard_model(&trace.ops) == 0 && parity_lcd_modeset_retained() == 0,
		      "C: discarding the MODEL that holds it is the isolation: only then is the object free again");
	}""")
t = rep(t, '	printf("lcd_modeset_host_test: %u checks, %u failures' + BS + 'n", checks, failures);',
        """	/* ================= E. a time-base fault is not a timeout ================= */
	bring_up();
	lcd.fault_time_base_reg = 0x46010;              /* the PLL-lock wait */
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	{
		int w = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x46010, 0, 0);

		printf("  E: enable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-""" + BS + """n");
		CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && strstr(s.first_error, "time base") != 0 && w >= 0 && trace.e[w].rc == -5,
		      "E: the wait returns -EIO (not -ETIMEDOUT) and the time-base fault is the FIRST anomaly, ahead of the reference's own PLL error");
		CHECK(lcd.plane_arms == 0, "E: after that anomaly the plane was not armed");
	}
	(void)parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_plane_released();
	(void)parity_edp_end(&res);

	/* ================= dither is derived from the pipe bpp, not fixed ================= */
	{
		static struct parity_lcd_state st24;

		bring_up();
		cfg = ms_cfg();
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == 0, "dither: prepare at 18 bpp");
		parity_lcd_modeset_status(&s);
		CHECK(s.dither == 1, "dither: an 18 bpp pipe dithers (intel_modeset_pipe_config)");
		if (parity_lcd_compute(res.edid, res.dpcd, res.edp_dpcd, 24, 38400, &st24) == 0 &&
		    parity_lcd_modeset_prepare(&st24, &cfg, &trace.ops) == 0) {
			parity_lcd_modeset_status(&s);
			CHECK(s.dither == 0, "dither: a 24 bpp pipe does not");
		} else {
			CHECK(0, "dither: prepare at 24 bpp");
		}
		(void)parity_edp_end(&res);
	}

	printf("lcd_modeset_host_test: %u checks, %u failures""" + BS + """n", checks, failures);""")
save(T, t)

# ---- GPU-free ktest: modeset object
k = load(L + "lcd_modeset_ktest.c")
k = rep(k, "	parity_lcd_modeset_abandoned();" + NL + "}", """	parity_lcd_modeset_abandoned();
	check(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16 && parity_lcd_modeset_retained() == 1,
		"lcd-ms: C-RETAINED abandon is not forgotten: the entry is refused again, the object still holds its state");
	check(parity_lcd_modeset_discard_model(&trace.ops) == 0 && parity_lcd_modeset_retained() == 0,
		"lcd-ms: C-DISCARD released only by discarding the model that holds it");
	{
		unsigned sl, bad = 0u;

		for (sl = 0u; sl < 4u; sl++)
			if (parity_dbuf_ctl_reg(sl) != parity_lcd_ref_dbuf_ctl(sl) || parity_dbuf_ctl_reg(sl) == 0u)
				bad++;
		check(bad == 0u && parity_dbuf_ctl_reg(0u) == parity_lcd_ref_dbuf_ctl(0u),
			"lcd-ms: DBUF-ADDR the power-domain init's DBUF_CTL table = the reference's DBUF_CTL_S(S1..S4) macro, slice by slice");
	}
}""")
k = rep(k, '#include "lcd_modeset_ktest.h"', '#include "lcd_modeset_ktest.h"' + NL + '#include "parity_lcd_observe.h"' + NL + '#include "../display_core.h"')
save(L + "lcd_modeset_ktest.c", k)

# ---- GPU-free ktest: show body
sk = load(L + "lcd_show_ktest.c")
sk = rep(sk, '	check(parity_lcd_show_run(&show, &rep) == -EBUSY, "lcd-show: C-NEXT a further run on the abandoned storage is refused before anything is touched");',
         """	check(parity_lcd_show_run(&show, &rep) == -EBUSY, "lcd-show: C-NEXT a further run on the abandoned storage is refused before anything is touched");
	{
		unsigned live = gm.objects_live, disp = gm.display_allocated_pages;
		uint32_t obj_page = so_c.obj->ggtt_page;
		struct parity_gt_object *obj = so_c.obj;

		show.so = &so_b;                                /* fresh (NONE) storage: still refused by the device latch */
		check(parity_lcd_show_run(&show, &rep) == -EBUSY && parity_lcd_show_retained() == 1 && gm.objects_live == live &&
			gm.display_allocated_pages == disp && so_b.state == PARITY_SCANOUT_NONE,
			"lcd-show: C-LATCH a re-call with fresh storage is refused before anything is allocated; the latch stays set");
		show.so = &so_c;
		check(parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so_c) == -EBUSY &&
			so_c.state == PARITY_SCANOUT_ABANDONED && so_c.obj == obj && so_c.pin_owner != 0,
			"lcd-show: C-CREATE create on an abandoned object is refused and leaves its record untouched");
		parity_gt_object_destroy(&gm, obj);
		parity_gt_display_unbind(&gm, obj);
		check(obj->in_use && obj->bound && obj->ggtt_page == obj_page && gm.keep_refusals == 2u && live_ptes(first, pages) == pages,
			"lcd-show: C-BELOW the object layer below the scanout wrapper refuses to destroy / unbind the kept object too");
	}""")
sk = rep(sk, '		"lcd-show: C-TEARDOWN parity_gt_mem_fini() leaves the abandoned buffer alone: PTEs live, pages readable, object kept");' + NL + "}",
         '		"lcd-show: C-TEARDOWN parity_gt_mem_fini() leaves the abandoned buffer alone: PTEs live, pages readable, object kept");' + NL +
         "	check(parity_lcd_show_retained() == 1 && parity_lcd_show_discard_model(&show) == 0 && parity_lcd_show_retained() == 0 &&" + NL +
         "		parity_lcd_modeset_retained() == 0," + NL +
         '		"lcd-show: C-DISCARD the latch outlives the teardown; only discarding the model run clears it");' + NL + "}")
save(L + "lcd_show_ktest.c", sk)
print("done")
