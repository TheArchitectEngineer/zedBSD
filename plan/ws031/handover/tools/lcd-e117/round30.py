#!/usr/bin/env python3
"""WS031 E-117 round 30: the four boundary fixes of the E-116 review.
  1 abandon is a retained state: never forgotten by a re-call; only a confirmed isolation (a MODEL backend being
    discarded) clears it; every entry refuses before it initialises anything
  2 parity_scanout_create() accepts only NONE storage; the object layer refuses to free / unbind a kept object
  3 wait / sleep faults of the time base are not turned into timeouts: -EIO (Linux numbering) + first-anomaly record
  4 the LCD test's result reaches the runner apart from the probe result
usage: round30.py <repo root>"""
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

# ================= ops: model flag, -EIO meaning =================
o = load(L + "parity_lcd_ops.h")
o = rep(o, "struct parity_lcd_emit {" + NL + "	void *ctx;", "struct parity_lcd_emit {" + NL + "	void *ctx;" + NL +
        "	int model;              /* 1: a register / sink MODEL (discarding it isolates whatever it holds); 0: real hardware */")
save(L + "parity_lcd_ops.h", o)
if "PARITY_LCD_EIO" not in o:
    o = load(L + "parity_lcd_ops.h")
    o = rep(o, "struct parity_lcd_emit {", """/* wait_reg results (Linux numbering, as the reference's callers compare them):
 *   0                    the condition held
 *   PARITY_LCD_ETIMEDOUT the device did not reach the condition in time
 *   PARITY_LCD_EIO       the time source / the wait primitive / MMIO access failed: NOT a timeout; the backend has
 *                        also reported it through parity_lcd_backend_fault() so it is the run's first anomaly */
#define PARITY_LCD_ETIMEDOUT (-110)
#define PARITY_LCD_EIO       (-5)
/* a backend reports a fault of its own (time base, MMIO) into the modeset's first-anomaly record */
void parity_lcd_backend_fault(const char *what);

struct parity_lcd_emit {""")
    save(L + "parity_lcd_ops.h", o)

# ================= modeset: retained state =================
a = load(L + "parity_lcd_modeset.h")
a = rep(a, "/* after an unconfirmed stop the caller recorded what stays held for ever (buffer, power, DBUF): the object forgets it */" + NL +
        "void parity_lcd_modeset_abandoned(void);",
        """/*
 * After an unconfirmed stop.  The object does NOT forget what it holds: stop_unconfirmed, the power references,
 * DC_OFF, the DBUF slices and the crtc state stay recorded, and every later prepare is refused before anything is
 * initialised.  _abandoned() marks that the caller has taken the retained resources over (the scanout buffer).
 * The only way out is _discard_model(): permitted solely when the retained state belongs to a MODEL backend, whose
 * discarding is itself the isolation; on real hardware there is no release (a recovery that verifies isolation
 * does not exist yet).
 */
void parity_lcd_modeset_abandoned(void);
int parity_lcd_modeset_retained(void);
int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops);""")
a = rep(a, "	int stop_unconfirmed;" + NL + "	unsigned commits;", "	int stop_unconfirmed, retained;" + NL + "	unsigned commits;")
save(L + "parity_lcd_modeset.h", a)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "void parity_lcd_modeset_abandoned(void)" + NL + "{" + NL + "	ms.prepared = 0;" + NL + "	ms.plane_armed = 0;" + NL +
        "	ms.dc_off_held = 0;" + NL + "	ms.stop_unconfirmed = 0;" + NL + "	ms.crtc.active = false;" + NL + "}",
        """static int ms_retained;                 /* outlives prepare's memset: only _discard_model() clears it */
static const struct parity_lcd_emit *ms_retained_ops;

void parity_lcd_modeset_abandoned(void)
{
	ms.stop_unconfirmed = 1;
	ms_retained = 1;
	ms_retained_ops = ms_ops;
}

int parity_lcd_modeset_retained(void)
{
	return ms_retained || ms.stop_unconfirmed;
}

int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops)
{
	if (!parity_lcd_modeset_retained())
		return 0;
	if (ops == 0 || ops != ms_ops || !ops->model)
		return -1;                      /* real hardware, or not the backend that holds it: nothing is released */
	memset(&ms, 0, sizeof(ms));
	ms_retained = 0;
	ms_retained_ops = 0;
	return 0;
}

void parity_lcd_backend_fault(const char *what)
{
	on_error(0, what);
}""")
r = rep(r, "	if (ms.prepared && (ms.crtc.active || ms.plane_armed || ms.dc_off_held || ms.stop_unconfirmed))" + NL + "		return -EBUSY;",
        "	/* refused BEFORE anything is initialised: a retained state is never overwritten */" + NL +
        "	if (ms_retained || ms.stop_unconfirmed || (ms.prepared && (ms.crtc.active || ms.plane_armed || ms.dc_off_held)))" + NL +
        "		return -EBUSY;")
r = rep(r, "	out->stop_unconfirmed = ms.stop_unconfirmed;", "	out->stop_unconfirmed = ms.stop_unconfirmed;" + NL + "	out->retained = parity_lcd_modeset_retained();")
# the commits refuse too
r = rep(r, "	if (!ms.prepared || ms.crtc.active || ms.dc_off_held)" + NL + "		return PARITY_LCD_MS_NOT_PREPARED;",
        "	if (!ms.prepared || ms.crtc.active || ms.dc_off_held || parity_lcd_modeset_retained())" + NL + "		return PARITY_LCD_MS_NOT_PREPARED;")
r = rep(r, "	if (!ms.prepared || !ms.crtc.active || ms.dc_off_held)" + NL + "		return PARITY_LCD_MS_NOT_PREPARED;",
        "	if (!ms.prepared || !ms.crtc.active || ms.dc_off_held || parity_lcd_modeset_retained())" + NL + "		return PARITY_LCD_MS_NOT_PREPARED;")
save(L + "parity_lcd_modeset.c", r)

# ================= show body: device-side latch =================
sh = load(L + "parity_lcd_show.h")
sh = rep(sh, "int parity_lcd_show_run(struct parity_lcd_show_env *env, struct parity_lcd_show_report *out);",
         """int parity_lcd_show_run(struct parity_lcd_show_env *env, struct parity_lcd_show_report *out);
/*
 * The device-side latch: set when a run ends ABANDONED, never cleared by a later run.  Every run refuses before it
 * initialises anything while it is set, and the outer teardown asks it before releasing the DMA device, the scratch
 * page, the BARs or bus mastering.  Cleared only by _discard_model() for a run on a register MODEL.
 */
int parity_lcd_show_retained(void);
int parity_lcd_show_discard_model(struct parity_lcd_show_env *env);""")
save(L + "parity_lcd_show.h", sh)
s = load(L + "parity_lcd_show.c")
s = rep(s, "static struct parity_lcd_trace show_trace;", "static struct parity_lcd_trace show_trace;" + NL +
        "static int show_retained;               /* see parity_lcd_show_retained() */" + NL +
        "static const struct parity_lcd_emit *show_retained_hw;" + NL + NL +
        "int parity_lcd_show_retained(void)" + NL + "{" + NL + "	return show_retained;" + NL + "}" + NL + NL +
        "int parity_lcd_show_discard_model(struct parity_lcd_show_env *env)" + NL + "{" + NL +
        "	if (!show_retained)" + NL + "		return 0;" + NL +
        "	if (env == 0 || env->hw == 0 || env->hw != show_retained_hw || !env->hw->model)" + NL + "		return -1;" + NL +
        "	if (parity_lcd_modeset_discard_model(&show_trace.ops) != 0)" + NL + "		return -1;" + NL +
        "	show_retained = 0;" + NL + "	show_retained_hw = 0;" + NL + "	return 0;" + NL + "}")
s = rep(s, "	if (env == 0 || r == 0 || env->hw == 0 || env->gm == 0 || env->so == 0 || env->lcd == 0)" + NL + "		return -EINVAL;" + NL +
        "	memset(r, 0, sizeof(*r));",
        "	if (env == 0 || r == 0 || env->hw == 0 || env->gm == 0 || env->so == 0 || env->lcd == 0)" + NL + "		return -EINVAL;" + NL +
        "	if (show_retained || parity_lcd_modeset_retained())" + NL +
        "		return -EBUSY;                  /* an earlier run left resources the display may still read: nothing is touched */" + NL +
        "	memset(r, 0, sizeof(*r));")
s = rep(s, "		parity_scanout_abandon(so);" + NL + "		parity_lcd_modeset_abandoned();",
        "		parity_scanout_abandon(so);" + NL + "		parity_lcd_modeset_abandoned();" + NL +
        "		show_retained = 1;" + NL + "		show_retained_hw = env->hw;")
save(L + "parity_lcd_show.c", s)

# ================= recorder / model: carry the model flag; a time-base fault in the model =================
tc = load(L + "parity_lcd_trace.c")
tc = rep(tc, "	t->ops.ctx = t;", "	t->ops.ctx = t;" + NL + "	t->ops.model = backend->model;")
save(L + "parity_lcd_trace.c", tc)
h = load(L + "lcd_fake_hw.h")
h = rep(h, "	int fault_power_get;", "	uint32_t fault_time_base_reg;           /* a wait on this register meets a TIME-BASE fault (not a timeout) */" + NL + "	int fault_power_get;")
save(L + "lcd_fake_hw.h", h)
c = load(L + "lcd_fake_hw.c")
c = rep(c, "	/* status follows control at once in this model: either it is there, or the whole timeout passes */",
        "	if (hw->fault_time_base_reg != 0u && reg == hw->fault_time_base_reg) {" + NL +
        '		parity_lcd_backend_fault("model: time base fault during a register wait (not a timeout)" "' + BS + 'n");' + NL +
        "		return PARITY_LCD_EIO;" + NL + "	}" + NL +
        "	/* status follows control at once in this model: either it is there, or the whole timeout passes */")
c = rep(c, "	hw->ops.ctx = hw;", "	hw->ops.ctx = hw;" + NL + "	hw->ops.model = 1;")
save(L + "lcd_fake_hw.c", c)

# ================= kernel binding: latch, error meaning, summary =================
k = load(L + "parity_lcd_kernel.c")
k = rep(k, """	if (rc != -ETIMEDOUT)
		k->time_faults++;
	else
		k->wait_timeouts++;
	return -110;                            /* the reference's -ETIMEDOUT (Linux numbering, as the ops define it) */""",
        """	if (rc == -ETIMEDOUT) {
		k->wait_timeouts++;
		return PARITY_LCD_ETIMEDOUT;    /* the device did not reach the condition: the reference's timeout */
	}
	/* anything else is the time source / wait primitive failing: not a timeout, and the run's first anomaly */
	k->time_faults++;
	parity_lcd_backend_fault("time base / wait primitive fault in a register wait (not a timeout)\\n");
	return PARITY_LCD_EIO;""")
k = rep(k, "static void k_usleep(void *ctx, unsigned us)" + NL + "{" + NL + "	parity_dp_kernel_sleep_us(&((struct lcd_kernel *)ctx)->d->edp->k, us);" + NL + "}",
        """static void k_usleep(void *ctx, unsigned us)
{
	struct lcd_kernel *k = ctx;
	unsigned before = k->d->edp->k.time_faults;

	parity_dp_kernel_sleep_us(&k->d->edp->k, us);
	if (k->d->edp->k.time_faults != before) {
		k->time_faults++;
		parity_lcd_backend_fault("time base fault during a sleep (returned early or the clock failed)\\n");
	}
}""")
k = rep(k, "	(void)ctx;" + NL + "	(void)parity_udelay(us);", "	struct lcd_kernel *k = ctx;" + NL + NL +
        "	if (parity_udelay(us) != 0) {" + NL + "		k->time_faults++;" + NL +
        '		parity_lcd_backend_fault("time base fault during a short delay\\n");' + NL + "	}")
k = rep(k, "static int lcdb_abandoned;" + NL, "static struct parity_lcd_test_summary lcdb_summary;" + NL + "static int lcdb_locks_live;" + NL +
        "static struct mutex lcdb_locks[2];       /* device lifetime (the per-run backend state is re-zeroed each run) */" + NL)
k = rep(k, "	struct mutex locks[2];", "	struct mutex *locks;                    /* lcdb_locks */")
k = rep(k, "	memset(k, 0, sizeof(*k));" + NL + "	k->d = d;" + NL +
        '	(void)mutex_init(&k->locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");' + NL +
        '	(void)mutex_init(&k->locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");' + NL,
        """	/* an earlier run left a buffer the display may still read: refuse before anything is initialised */
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		lcdb_summary.ran = 1;
		lcdb_summary.pass = 0;
		lcdb_summary.retained = 1;
		lcdb_summary.first_anomaly = "refused: resources of an earlier run are retained (stop not confirmed)";
		kern_logf("i915: parity LCD-B verdict: FAIL (refused before any initialisation: an earlier run's resources are retained)\\n");
		return -1;
	}
	if (!lcdb_locks_live) {                 /* the mutexes live as long as the device: initialised once */
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	k->locks = lcdb_locks;
	k->d = d;
	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
""")
k = rep(k, "		kern_logf(\"i915: parity LCD-B verdict: FAIL (preflight: nothing was written to the display)\\n\");" + NL + "		return -1;",
        "		kern_logf(\"i915: parity LCD-B verdict: FAIL (preflight: nothing was written to the display)\\n\");" + NL +
        '		lcdb_summary.first_anomaly = "preflight refused (nothing written)";' + NL + "		return -1;")
k = rep(k, "	lcdb_abandoned = rep.abandoned;" + NL + "	return rep.pass ? 0 : -1;" + NL + "}" + NL + NL +
        "int parity_lcd_kernel_abandoned(void)" + NL + "{" + NL + "	return lcdb_abandoned;" + NL + "}",
        """	lcdb_summary.pass = rep.pass;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.stage = stage_name(rep.stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.cleanup_errors = rep.cleanup_errors;
	lcdb_summary.retained = parity_lcd_show_retained();
	return rep.pass ? 0 : -1;
}

int parity_lcd_kernel_abandoned(void)
{
	return parity_lcd_show_retained() || parity_lcd_modeset_retained();
}

void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out)
{
	if (out != 0)
		*out = lcdb_summary;
}""")
save(L + "parity_lcd_kernel.c", k)
kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "/* 1 when a run left a buffer the display may still read: the outer teardown must keep DMA, bus mastering and scratch */" + NL +
         "int parity_lcd_kernel_abandoned(void);",
         """/* 1 while a run's resources are retained (the display may still read): the outer teardown keeps DMA, bus mastering, scratch */
int parity_lcd_kernel_abandoned(void);

/* what the runner reports next to the probe result (the probe outcome itself is not changed by the LCD test) */
struct parity_lcd_test_summary {
	int ran, pass;
	const char *stage;                      /* furthest stage */
	const char *first_anomaly, *first_anomaly_stage;
	int cleanup_rc;                         /* the disable commit's result */
	unsigned cleanup_errors;
	int retained;                           /* resources kept because the stop was not confirmed */
};
void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out);""")
save(L + "parity_lcd_kernel.h", kh)

# ================= probe -> runner =================
ph = load(P + "parity.h")
ph = rep(ph, "	const char *last_completed;  /* last op that actually completed (distinct from where) */",
         "	const char *last_completed;  /* last op that actually completed (distinct from where) */" + NL +
         "	/* a display test run inside the probe (0 = none ran); kept apart from the probe outcome */" + NL +
         "	int lcd_test_ran, lcd_test_pass, lcd_cleanup_rc, lcd_retained;" + NL +
         "	const char *lcd_first_anomaly_stage;")
save(P + "parity.h", ph)
pc = load(P + "probe.c")
pc = rep(pc, '	res.where = "start";', '	res.where = "start";' + NL + "	res.lcd_test_ran = 0; res.lcd_test_pass = 0; res.lcd_cleanup_rc = 0; res.lcd_retained = 0;" + NL +
         '	res.lcd_first_anomaly_stage = "-";')
pc = rep(pc, "		if (gtmem_inited)" + NL + "			(void)parity_lcd_kernel_lcdb_run(&lcdb);" + NL + "		else" + NL +
         '			kern_logf("i915: parity LCD-B verdict: FAIL (no GT memory)\\n");',
         """		if (gtmem_inited) {
			struct parity_lcd_test_summary sum;

			(void)parity_lcd_kernel_lcdb_run(&lcdb);
			parity_lcd_kernel_summary(&sum);
			res.lcd_test_ran = 1;
			res.lcd_test_pass = sum.pass;
			res.lcd_cleanup_rc = sum.cleanup_rc;
			res.lcd_retained = sum.retained;
			res.lcd_first_anomaly_stage = sum.first_anomaly != 0 ? sum.first_anomaly_stage : "none";
		} else {
			kern_logf("i915: parity LCD-B verdict: FAIL (no GT memory)\\n");
			res.lcd_test_ran = 1;
			res.lcd_first_anomaly_stage = "no-gt-memory";
		}""")
save(P + "probe.c", pc)
rn = load(P + "runner.c")
rn = rep(rn, "	int result_valid;                 /* published under the lock after being filled */",
         "	int result_valid;                 /* published under the lock after being filled */" + NL +
         "	int lcd_test_status;              /* enum runner_test_status: a display test inside the probe, reported apart */" + NL +
         "	const char *lcd_first_anomaly_stage;" + NL + "	int lcd_cleanup_rc, lcd_retained;")
rn = rep(rn, "		res.last_completed_op = pr.last_completed;   /* last COMPLETED op, not the frontier */",
         "		res.last_completed_op = pr.last_completed;   /* last COMPLETED op, not the frontier */" + NL +
         "		res.lcd_test_status = !pr.lcd_test_ran ? RUN_TEST_NOT_RUN : pr.lcd_test_pass ? RUN_TEST_PASS : RUN_TEST_FAIL;" + NL +
         "		res.lcd_first_anomaly_stage = pr.lcd_first_anomaly_stage;" + NL +
         "		res.lcd_cleanup_rc = pr.lcd_cleanup_rc;" + NL +
         "		res.lcd_retained = pr.lcd_retained;")
rn = rep(rn, "	res.result_valid = 0;" + NL, "	res.result_valid = 0;" + NL + "	res.lcd_test_status = RUN_TEST_NOT_RUN;" + NL +
         '	res.lcd_first_anomaly_stage = "-";' + NL + "	res.lcd_cleanup_rc = 0;" + NL + "	res.lcd_retained = 0;" + NL)
rn = rep(rn, '		"blocked_at=%s cleanup=%d published=%d\\n",', '		"blocked_at=%s cleanup=%d published=%d lcd_test=%s lcd_first_anomaly_at=%s lcd_cleanup_rc=%d lcd_retained=%d\\n",')
rn = rep(rn, "		res.cleanup_done, res.published);", "		res.cleanup_done, res.published," + NL +
         '		res.lcd_test_status == RUN_TEST_PASS ? "PASS" : (res.lcd_test_status == RUN_TEST_FAIL ? "FAIL" : "NOT_RUN"),' + NL +
         "		res.lcd_first_anomaly_stage, res.lcd_cleanup_rc, res.lcd_retained);")
save(P + "runner.c", rn)

# ================= scanout: create accepts only NONE storage; the object layer refuses kept objects =================
sc = load(L + "scanout.h")
sc = rep(sc, "/* 0 or a negative errno: -EINVAL unsupported layout, -ENOMEM backing.  Nothing stays allocated on failure. */",
         """/*
 * 0 or a negative errno: -EINVAL unsupported layout, -ENOMEM backing, -EBUSY the storage holds a buffer (any state but
 * NONE: it is left exactly as it was).  The storage must start zeroed (static, or memset by its owner before first use);
 * destroy returns it to that state.  Nothing stays allocated on failure.
 */""")
save(L + "scanout.h", sc)
sc = load(L + "scanout.c")
sc = rep(sc, "	if (gm == 0 || so == 0)" + NL + "		return -EINVAL;" + NL + "	memset(so, 0, sizeof(*so));",
         "	if (gm == 0 || so == 0)" + NL + "		return -EINVAL;" + NL +
         "	/* an object in any other state owns backing / GGTT / a pin: never overwrite its record */" + NL +
         "	if (so->state != PARITY_SCANOUT_NONE || so->obj != 0)" + NL + "		return -EBUSY;" + NL +
         "	memset(so, 0, sizeof(*so));")
save(L + "scanout.c", sc)
gh = load(P + "gt_mem.h")
gh = rep(gh, "	int keep;                   /* never destroyed by parity_gt_mem_fini: the display may still read it */",
         "	int keep;                   /* never destroyed / unbound (fini, destroy, display unbind all refuse): the display may still read it */")
save(P + "gt_mem.h", gh)
if "keep_refusals" not in gh:
    gh = load(P + "gt_mem.h")
    gh = rep(gh, "	unsigned window_first;      /* first page of the driver window */",
             "	unsigned keep_refusals;     /* destroy / unbind requests refused because the object is kept */" + NL +
             "	unsigned window_first;      /* first page of the driver window */")
    save(P + "gt_mem.h", gh)
g = load(P + "gt_mem.c")
g = rep(g, "	if (gm == 0 || o == 0 || !o->in_use)" + NL + "		return;" + NL + "	if (o->bound && o->display)",
        "	if (gm == 0 || o == 0 || !o->in_use)" + NL + "		return;" + NL +
        "	if (o->keep) {                  /* kept for the display: reachable only below the scanout wrapper -- refuse there too */" + NL +
        "		gm->keep_refusals++;" + NL + "		return;" + NL + "	}" + NL + "	if (o->bound && o->display)")
g = rep(g, "	if (gm == 0 || o == 0 || !o->bound || !o->display)" + NL + "		return;" + NL + "	first = o->ggtt_page",
        "	if (gm == 0 || o == 0 || !o->bound || !o->display)" + NL + "		return;" + NL +
        "	if (o->keep) {" + NL + "		gm->keep_refusals++;" + NL + "		return;" + NL + "	}" + NL + "	first = o->ggtt_page")
save(P + "gt_mem.c", g)
print("done")
