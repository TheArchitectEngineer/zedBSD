#!/usr/bin/env python3
"""WS031 E-122 round 71: LCD-O (-DPARITY_LCDO_TEST=1) -- synthetic ASLE brightness requests on the real LCD.
The OpRegion service runs on a kernel-owned SHADOW mailbox (the firmware region is never written); the synthetic GSE
entry queues the real worker; the reference asle_set_backlight -> intel_backlight_set_acpi (generated into the backlight
port now) drives the existing real backlight.  Per step: the mailbox answer, the PWM duty vs. the reference scale(), a
photograph.  Finally the normal user-brightness path restores the original level.  usage: round71.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
J = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(J))
for f in j["new_files"]:
    if f["out"] == "intel_backlight_port.c" and "intel_backlight_set_acpi" not in f["functions"]:
        f["functions"].append("intel_backlight_set_acpi")
json.dump(j, open(J, "w"), indent=1, sort_keys=True)

g = open(L + "parity_backlight_glue.inc").read()
g = rep(g, "/* intel_edp_backlight_on() / _off(): the PWM and the panel's backlight-enable only -- the pipe keeps scanning out */",
"""/* the OpRegion ASLE path: asle_set_backlight() -> intel_backlight_set_acpi(conn_state, bclp, 255) (E-122) */
void parity_lcd_ms_set_acpi(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max)
{
	parity_lcd_cur_i915 = &ms->i915;
	intel_backlight_set_acpi(&ms->conn_state, user_level, user_max);
}

/* intel_edp_backlight_on() / _off(): the PWM and the panel's backlight-enable only -- the pipe keeps scanning out */""")
open(L + "parity_backlight_glue.inc", "w").write(g)

i = open(L + "parity_lcd_modeset_int.h").read()
i = i.rstrip(NL) + NL + "void parity_lcd_ms_set_acpi(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max);" + NL
open(L + "parity_lcd_modeset_int.h", "w").write(i)

m = open(L + "parity_lcd_modeset.c").read()
m = rep(m, "int parity_lcd_modeset_backlight(int on)\n{", """/* the OpRegion ASLE request (intel_backlight_set_acpi): hw level = clamp_user_to_hw(level, max); no device update */
int parity_lcd_modeset_backlight_acpi(uint32_t level, uint32_t max)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active || parity_lcd_modeset_retained() || max == 0u || level > max)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_ms_set_acpi(&ms, level, max);
	ms.bl_user = parity_lcd_ms_user_level(&ms, ms.bl_user_max);
	return ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_backlight(int on)
{""")
open(L + "parity_lcd_modeset.c", "w").write(m)
h = open(L + "parity_lcd_modeset.h").read()
h = rep(h, "int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);",
        "int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);" + NL +
        "/* the OpRegion ASLE request path (intel_backlight_set_acpi; level in [0, max], ASLE uses max 255) */" + NL +
        "int parity_lcd_modeset_backlight_acpi(uint32_t level, uint32_t max);")
open(L + "parity_lcd_modeset.h", "w").write(h)

k = open(L + "parity_lcd_kernel.c").read()
k = rep(k, '#include "lcd_pattern.h"', '#include "lcd_pattern.h"' + NL + '#include "parity_opregion.h"' + NL + '#include "../backend_sync.h"' + NL + '#include <kern/sched.h>')
k = k.rstrip(NL) + NL + r'''
/* ---------------- LCD-O (-DPARITY_LCDO_TEST=1): synthetic ASLE brightness requests on the real LCD ---------------- */

#define LCDO_PATTERN 131u
#define LCDO_HOLD_MS 6000u
#define LCDO_ASLS_TOKEN 0x6f000018u     /* a mapping-table token, not a physical address: the shadow only */
static struct parity_scanout lcdo_so;
static uint8_t lcdo_shadow[8192] __attribute__((aligned(4096)));
static struct parity_kworkqueue lcdo_wq;
static int lcdo_wq_live, lcdo_rc, lcdo_steps_ok, lcdo_restore_ok;

static uint32_t lcdo_rd(unsigned off) { uint32_t v; memcpy(&v, lcdo_shadow + off, 4); return v; }
static void lcdo_wr(unsigned off, uint32_t v) { memcpy(lcdo_shadow + off, &v, 4); }

static void lcdo_backlight(void *ctx, uint32_t level, uint32_t max)
{
	(void)ctx;
	lcdo_rc = parity_lcd_modeset_backlight_acpi(level, max);
}

static uint32_t lcdo_verify(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);
	return parity_lcd_pattern_verify(so->cpu, so->pitch, so->width, so->height, LCDO_PATTERN, 0, 0);
}

/* the reference scale(): source [0, 255] -> target [min, max], DIV_ROUND_CLOSEST */
static uint32_t lcdo_expected_duty(uint32_t level, uint32_t min, uint32_t max)
{
	uint64_t t = (uint64_t)level * (uint64_t)(max - min);

	return (uint32_t)((t + 127u) / 255u) + min;
}

static int lcdo_steps(void *ctx, struct parity_lcd_observer *o)
{
	static const uint32_t levels[3] = { 64u, 160u, 255u };
	static const char sig[16] = { 'I','n','t','e','l','G','r','a','p','h','i','c','s','M','e','m' };
	struct lcd_kernel *k = ctx;
	struct parity_lcd_modeset_status st;
	uint32_t mboxes = 0x1du, duty0, user0, umax0, duty;
	unsigned n, started, finished, qn, qp;

	(void)o;
	parity_lcd_modeset_status(&st);
	duty0 = osdep_mmio_read32(k->d->mmio, 0xc8258u);
	user0 = st.backlight_user;
	umax0 = st.backlight_user_max;
	kern_logf("i915: parity LCD-O shown 0: pattern %u at the start brightness (user %u/%u, PWM duty 0x%x, min %u max %u) -- take "
		"the photograph\n", LCDO_PATTERN, user0, umax0, duty0, st.backlight_min, st.backlight_max);
	step_sleep(k, LCDO_HOLD_MS);

	/* the service on a SHADOW mailbox: setup -> register (DIDL / CADL, READY in the shadow only) */
	memset(lcdo_shadow, 0, sizeof(lcdo_shadow));
	memcpy(lcdo_shadow, sig, 16);
	lcdo_shadow[0x10] = 8u; lcdo_shadow[0x16] = 1u; lcdo_shadow[0x17] = 2u;
	memcpy(lcdo_shadow + 0x58, &mboxes, 4);
	if (!lcdo_wq_live) {
		if (parity_kworkqueue_create(&lcdo_wq, "parity-lcdo-opregion") != 0)
			return -5;
		lcdo_wq_live = 1;
	}
	if (parity_opregion_shadow_map(LCDO_ASLS_TOKEN, lcdo_shadow, sizeof(lcdo_shadow)) != 0 ||
	    parity_opregion_shadow_setup(LCDO_ASLS_TOKEN) != 0 ||
	    parity_opregion_service_start(&lcdo_wq, PARITY_OPREGION_POLICY_VIDEO) != 0 ||
	    parity_opregion_add_backlight(lcdo_backlight, k) != 0)
		return -5;
	parity_opregion_register();
	kern_logf("i915: parity LCD-O service: mailbox_backend=%s service_epoch=%u event_source=SYNTHETIC(GSE entry) "
		"display_backend=HARDWARE | shadow ARDY %u DRDY %u TCHE %u | real_opregion_write_count=0 (the firmware region is never "
		"mapped writable)\n", parity_opregion_mailbox_backend(), parity_opregion_service_epoch(), lcdo_rd(0x300), lcdo_rd(0x100),
		lcdo_rd(0x308));

	for (n = 0u; n < 3u; n++) {
		uint32_t want = lcdo_expected_duty(levels[n], st.backlight_min, st.backlight_max);
		uint32_t want_cblv = ((levels[n] * 100u + 254u) / 255u) | (1u << 31);

		lcdo_rc = -1;
		lcdo_wr(0x310, (1u << 31) | levels[n]);     /* BCLP: valid | level */
		lcdo_wr(0x304, 1u << 1);                     /* ASLC: SET_BACKLIGHT */
		parity_opregion_gse_entry();
		(void)parity_opregion_asle_flush(sched_ticks() + 200u);
		duty = osdep_mmio_read32(k->d->mmio, 0xc8258u);
		parity_opregion_worker_stats_get(&started, &finished, &qn, &qp);
		kern_logf("i915: parity LCD-O step %u: request BCLP %u/255 | response ASLC 0x%x CBLV 0x%x (want 0x%x) | backlight rc %d | "
			"PWM duty 0x%x (%u) want %u (reference scale to [%u, %u]) | worker started %u finished %u\n", n + 1u, levels[n],
			lcdo_rd(0x304), lcdo_rd(0x318), want_cblv, lcdo_rc, duty, duty, want, st.backlight_min, st.backlight_max, started,
			finished);
		if (lcdo_rc == PARITY_LCD_MS_OK && lcdo_rd(0x304) == 0u && lcdo_rd(0x318) == want_cblv && duty == want &&
		    started == n + 1u && finished == n + 1u)
			lcdo_steps_ok++;
		kern_logf("i915: parity LCD-O shown %u: brightness %u/255 via the synthetic ASLE request -- take the photograph\n", n + 1u,
			levels[n]);
		step_sleep(k, LCDO_HOLD_MS);
	}

	/* stop the service (gate, reference unregister, cleanup), then restore through the normal user path */
	parity_opregion_unregister();
	(void)parity_opregion_cleanup();
	(void)parity_lcd_modeset_brightness(user0, umax0);
	duty = osdep_mmio_read32(k->d->mmio, 0xc8258u);
	lcdo_restore_ok = duty == duty0;
	kern_logf("i915: parity LCD-O restore: user %u/%u -> PWM duty 0x%x (start 0x%x) %s | service: backend %s, notifier registered %d\n",
		user0, umax0, duty, duty0, lcdo_restore_ok ? "OK" : "DIFFERS", parity_opregion_mailbox_backend(),
		parity_opregion_notifier_registered());
	return 0;
}

int parity_lcd_kernel_lcdo_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	int rc, i, held = 0, released = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	lcdo_steps_ok = 0;
	lcdo_restore_ok = 0;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->irq == 0) {
		kern_logf("i915: parity LCD-O verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || lcdo_so.state != PARITY_SCANOUT_NONE) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity LCD-O verdict: FAIL (refused before any initialisation: retained resources)\n");
		return -1;
	}
	if (!lcdb_locks_live) {
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	k->locks = lcdb_locks;
	k->d = d;
	bind_ops(k);
	memset(&env, 0, sizeof(env));
	if (preflight(k) != 0 || fill_cfg(k, &env.cfg) != 0) {
		kern_logf("i915: parity LCD-O verdict: FAIL (preflight: nothing was written)\n");
		return -1;
	}
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY)
		return -1;
	rc = parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdo_so);
	rc = rc == 0 ? parity_scanout_pin(&lcdo_so, "lcd-o") : rc;
	if (rc != 0) {
		kern_logf("i915: parity LCD-O verdict: FAIL (buffer rc=%d)\n", rc);
		return -1;
	}
	(void)parity_lcd_pattern_fill(lcdo_so.cpu, lcdo_so.pitch, lcdo_so.width, lcdo_so.height, LCDO_PATTERN);
	parity_scanout_publish(&lcdo_so);
	env.hw = &k->ops;
	env.gm = d->gm;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 1000u;
	env.in_window = lcdo_steps;
	env.in_window_ctx = k;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = LCDO_PATTERN;
	k->window_ms = env.window_ms;
	rc = parity_lcd_show_prepared(&env, &lcdo_so, lcdo_verify, 0, &rep);
	log_trace(rep.trace);
	log_observer(&rep.obs);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	if (rep.display_released || !rep.display_acquired)
		released = parity_scanout_unpin(&lcdo_so) == 0 && parity_scanout_destroy(&lcdo_so) == 0;
	else if (lcdo_so.state >= PARITY_SCANOUT_PINNED && lcdo_so.state != PARITY_SCANOUT_ABANDONED)
		parity_scanout_abandon(&lcdo_so);
	lcdb_summary.pass = rc == 0 && lcdo_steps_ok == 3 && lcdo_restore_ok && released && held == 0 && rep.readback_bad_after == 0u &&
		k->unresolved_steps == 0u && k->time_faults == 0u && !parity_opregion_notifier_registered();
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-O verdict: %s (ASLE steps %d/3 with the PWM duty = the reference scale, restore %s, the service "
		"unregistered, stop %s, buffer released=%d, power refs held %d, first anomaly: %s)\n", lcdb_summary.pass ? "PASS" : "FAIL",
		lcdo_steps_ok, lcdo_restore_ok ? "OK" : "DIFFERS", rep.display_released ? "confirmed" : "NOT confirmed", released, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}
'''
open(L + "parity_lcd_kernel.c", "w").write(k)
kh = open(L + "parity_lcd_kernel.h").read()
kh = rep(kh, "int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);",
         "int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);" + NL +
         "/* LCD-O (-DPARITY_LCDO_TEST=1): synthetic ASLE brightness requests (OpRegion service on a SHADOW mailbox) on the real LCD */" + NL +
         "int parity_lcd_kernel_lcdo_run(const struct parity_lcd_kernel_deps *d);")
open(L + "parity_lcd_kernel.h", "w").write(kh)
b = open(P + "bios.h").read()
b = rep(b, "#ifndef PARITY_LCDD_TEST", "#ifndef PARITY_LCDO_TEST" + NL + "#define PARITY_LCDO_TEST 0            /* LCD-O: synthetic ASLE brightness on the real LCD (shadow OpRegion) */" + NL + "#endif" + NL + "#ifndef PARITY_LCDD_TEST")
b = rep(b, "PARITY_LCDC_TEST || PARITY_LCDD_TEST)", "PARITY_LCDC_TEST || PARITY_LCDD_TEST || PARITY_LCDO_TEST)")
open(P + "bios.h", "w").write(b)
pc = open(P + "probe.c").read()
pc = rep(pc, "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST) {",
         "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST || PARITY_LCDO_TEST) {")
pc = rep(pc, "			} else if (PARITY_LCDC_TEST) {", "			} else if (PARITY_LCDO_TEST) {" + NL + "				(void)parity_lcd_kernel_lcdo_run(&lcdb);" + NL + "			} else if (PARITY_LCDC_TEST) {")
open(P + "probe.c", "w").write(pc)
print("done")
