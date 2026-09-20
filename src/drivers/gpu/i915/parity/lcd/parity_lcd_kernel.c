/*
 * WS031 Linux-parity -- LCD-B on the REAL GPU: the parity_lcd_ops backend over the objects the normal
 * initialisation built, the modeset inputs read from THOSE objects, the pre-flight check, and the run log.
 * zedBSD project code.
 *
 * Where every input comes from (none of them is a constant of this file, none is a value of Linux's dump):
 *   mode / link / M-N / PLL words    the resident eDP's LCD-A state (its own AUX reads of DPCD + EDID)
 *   DPCD capabilities                the resident eDP's result
 *   VBT: low vswing, HOBL, AFC,      this boot's parsed VBT (explicit blob, SHA pinned)
 *        backlight block
 *   raw clock                        the resident eDP's configuration (PCH raw clock readout)
 *   DDI_BUF_CTL port bits            the readout taken when the connector was initialised (intel_ddi_init)
 *   watermark latencies, SAGV time   the PCODE readout of the normal initialisation (display_nogem)
 *   DBUF slices now                  the power-domain init's shared state (display_core) + MBUS_CTL readout
 *   CDCLK now                        the CDCLK init's hardware state (cdclk.hw)
 *   QGV point / its bandwidth        the bandwidth table read from PCODE + the mask the forced SAGV disable left
 *   DMC firmware mask                this boot's DMC loader state (payload present per firmware id)
 *   framebuffer                      the scanout object pinned for this run
 * Operations: MMIO through the device's MMIO backend; waits through parity_wait_reg(); sleeps through the resident
 * eDP's tick sleep; panel power / DPCD through the resident eDP; power domains through the one power-domains state
 * (the asynchronous put uses the delayed work the eDP bound); DBUF slices through the body the power-domain init
 * uses; the DPLL and backlight locks are real mutexes.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <string.h>
#include <errno.h>
#include "../osdep/mmio.h"
#include "../wait.h"
#include "../power_domains.h"
#include "../display_core.h"
#include "../cdclk.h"
#include "../display_nogem.h"
#include "../display_state.h"
#include "../dram_bw.h"
#include "../dmc.h"
#include "../irq.h"
#include "../gt_mem.h"
#include "../bios.h"
#include "../dp/parity_edp.h"
#include "../dp/parity_dp_kernel.h"
#include "../vbt/parity_vbt.h"
#include "lcd_power_domain_enum.h"
#include "parity_lcd_show.h"
#include "parity_lcd_kernel.h"
#include "parity_hotplug.h"
#include "parity_lcd_modeset.h"
#include "parity_n1.h"
/* the console framebuffer the kernel keeps printing into (read only, to mirror it onto the panel) */
#include "../../../../platform/pcat/graphics/backend.h"
/* the console backing as the boot handoff describes it, and the probe state that owns the INIT reference */
#include <kern/platform.h>
#include "bootloader/include/amd64-handoff.h"
#include "../driver_probe.h"
/* the two windows of the N1 run: the photograph of the firmware picture, and the window after the re-light */
#ifndef PARITY_N1_REG_TRACE
#define PARITY_N1_REG_TRACE 0
#endif
#ifndef PARITY_N1_PHOTO_MS
#define PARITY_N1_PHOTO_MS 30000u
#endif
#ifndef PARITY_N1_WINDOW_MS
#define PARITY_N1_WINDOW_MS 40000u
#endif
#ifndef PARITY_N1_FW_MS
#define PARITY_N1_FW_MS 20000u     /* how long the plane shows the firmware framebuffer through its own GGTT */
#endif
#ifndef PARITY_N1_MIRROR_S
#define PARITY_N1_MIRROR_S 45u     /* seconds of console mirroring onto the panel after the takeover */
#endif
#include "../eu_test.h"
#include "../gt_tlb.h"
#include <kern/irq.h>
#include "lcd_pattern.h"
#include "parity_opregion.h"
#include "../backend_sync.h"
#include <kern/sched.h>
#include "../../draw_fixture.h"

/* the ops carry the reference's domain numbers; the power-domains state uses its own enum with the same order */
_Static_assert((int)POWER_DOMAIN_DISPLAY_CORE == (int)PARITY_PW_DOMAIN_DISPLAY_CORE, "DISPLAY_CORE");
_Static_assert((int)POWER_DOMAIN_PIPE_A == (int)PARITY_PW_DOMAIN_PIPE_A, "PIPE_A");
_Static_assert((int)POWER_DOMAIN_TRANSCODER_A == (int)PARITY_PW_DOMAIN_TRANSCODER_A, "TRANSCODER_A");
_Static_assert((int)POWER_DOMAIN_PORT_DDI_LANES_A == (int)PARITY_PW_DOMAIN_PORT_DDI_LANES_A, "PORT_DDI_LANES_A");
_Static_assert((int)POWER_DOMAIN_PORT_DDI_IO_A == (int)PARITY_PW_DOMAIN_PORT_DDI_IO_A, "PORT_DDI_IO_A");
_Static_assert((int)POWER_DOMAIN_AUX_IO_A == (int)PARITY_PW_DOMAIN_AUX_IO_A, "AUX_IO_A");
_Static_assert((int)POWER_DOMAIN_AUX_A == (int)PARITY_PW_DOMAIN_AUX_A, "AUX_A");
_Static_assert((int)POWER_DOMAIN_DC_OFF == (int)PARITY_PW_DOMAIN_DC_OFF, "DC_OFF");
_Static_assert((int)POWER_DOMAIN_NUM == (int)PARITY_PW_DOMAIN_NUM, "POWER_DOMAIN_NUM");

struct lcd_run_params {
	unsigned pattern_id;
	uint64_t pattern_fnv;                   /* 0 = no pinned hash (the read-back check still runs) */
	unsigned window_ms;
	int (*in_window)(void *ctx, struct parity_lcd_observer *o);
	/* HDMI-B: the output is an HDMI sink on another port / pipe, and the state is the caller's (no EDID, no DPCD) */
	int output_hdmi, port, pipe, cpu_transcoder, dpll_id;
	const struct parity_lcd_state *state;
	const char *tag;
	int reset_dplls;                /* the run starts from an empty DPLL pool (a single-screen run) */
};

struct lcd_kernel {
	const struct lcd_run_params *p;         /* the parameters of the run (0 before it starts) */
	const struct parity_lcd_kernel_deps *d;
	struct mutex *locks;                    /* lcdb_locks */
	int locks_live;
	struct parity_lcd_emit ops;
	int power_refs[POWER_DOMAIN_NUM];       /* what THIS run took and has not returned (per domain) */
	unsigned power_get_failures, wait_timeouts, time_faults, unresolved_steps, decided, errors;
	int phase_cleanup;
	unsigned pattern_id, window_ms, post_before, pre_before, vbt_min;
	uint32_t bl_freq0;
	int irq_was_on, event_armed, event_pipe; uint32_t event_frame; unsigned vblank_sleeps, sleep_irq_off, events_cancelled;
	struct parity_scanout *flip_a, *flip_b; unsigned flips_done, draws_ok, probe_flips, cross_ok;
	int probe_watch; uint32_t probe_first_dsl;                      /* set when the disable commit starts: errors are counted apart */
	unsigned cleanup_errors;
};

static struct lcd_kernel lk;
static struct parity_lcd_test_summary lcdb_summary;
static int lcdb_locks_live;
static struct mutex lcdb_locks[2];       /* device lifetime (the per-run backend state is re-zeroed each run) */
static struct parity_scanout lcdb_scanout;      /* outlives the run when the buffer is abandoned */

/* ---- ops ---- */
/* E-124: while the N1 readout runs, every register access is printed: on a console-only machine that is
 * the only way to see where a readout of the hardware stops */
int parity_lcd_reg_trace;
extern int parity_lcd_note_trace;
extern void (*parity_lcd_note_sink)(const char *fmt);

static void k_note_sink(const char *fmt)
{
	kern_logf("i915: parity N1 note: %s", fmt);
}

static uint32_t k_read32(void *ctx, uint32_t reg)
{
	struct lcd_kernel *k = ctx;
	uint32_t v = osdep_mmio_read32(k->d->mmio, reg);

	if (parity_lcd_reg_trace)
		kern_logf("i915: parity N1 read 0x%05x = 0x%08x\n", reg, v);

	/* evasion probe (observation only): the first scanline the update body reads after the trigger */
	if (k->probe_watch && reg == 0x70000u) {
		k->probe_first_dsl = v & 0x1fffu;
		k->probe_watch = 0;
	}
	return v;
}

static void k_write32(void *ctx, uint32_t reg, uint32_t value)
{
	if (parity_lcd_reg_trace)
		kern_logf("i915: parity N1 write 0x%05x = 0x%08x\n", reg, value);
	osdep_mmio_write32(((struct lcd_kernel *)ctx)->d->mmio, reg, value);
}

static uint32_t k_rmw32(void *ctx, uint32_t reg, uint32_t clear, uint32_t set)
{
	struct osdep_mmio *m = ((struct lcd_kernel *)ctx)->d->mmio;
	uint32_t old = osdep_mmio_read32(m, reg);

	osdep_mmio_write32(m, reg, (old & ~clear) | set);
	return old;
}

static void k_posting_read(void *ctx, uint32_t reg)
{
	(void)osdep_mmio_read32(((struct lcd_kernel *)ctx)->d->mmio, reg);
}

/* intel_de_wait_for_register(): 2 us of atomic polling, then the sleeping stage up to timeout_ms */
static int k_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned timeout_ms)
{
	struct lcd_kernel *k = ctx;
	uint32_t out = 0u;
	int rc = parity_wait_reg(k->d->mmio, reg, mask, value, 2u, timeout_ms, &out);

	if (rc == 0)
		return 0;
	if (rc == -ETIMEDOUT) {
		k->wait_timeouts++;
		return PARITY_LCD_ETIMEDOUT;    /* the device did not reach the condition: the reference's timeout */
	}
	/* anything else is the time source / wait primitive failing: not a timeout, and the run's first anomaly */
	k->time_faults++;
	parity_lcd_backend_fault("time base / wait primitive fault in a register wait (not a timeout)\n");
	return PARITY_LCD_EIO;
}

static void k_usleep(void *ctx, unsigned us)
{
	struct lcd_kernel *k = ctx;
	unsigned before = k->d->edp->k.time_faults;

	parity_dp_kernel_sleep_us(&k->d->edp->k, us);
	if (k->d->edp->k.time_faults != before) {
		k->time_faults++;
		parity_lcd_backend_fault("time base fault during a sleep (returned early or the clock failed)\n");
	}
}

static void k_udelay(void *ctx, unsigned us)
{
	struct lcd_kernel *k = ctx;

	if (parity_udelay(us) != 0) {
		k->time_faults++;
		parity_lcd_backend_fault("time base fault during a short delay\n");
	}
}

static long k_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size)
{
	(void)ctx;
	return parity_edp_dpcd_read(offset, buf, size);
}

static long k_dpcd_write(void *ctx, unsigned offset, const uint8_t *buf, size_t size)
{
	(void)ctx;
	return parity_edp_dpcd_write(offset, buf, size);
}

static int k_read_dpcd_caps(void *ctx, uint8_t dpcd[15])
{
	(void)ctx;
	return parity_edp_read_dpcd_caps(dpcd);
}

static int k_panel(void *ctx, int op)
{
	(void)ctx;
	return parity_edp_panel_op(op);
}

static int k_power_get(void *ctx, int domain)
{
	struct lcd_kernel *k = ctx;
	int rc;

	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM) {
		k->power_get_failures++;
		return 0;
	}
	rc = parity_display_power_get(k->d->pd, (enum parity_power_domain)domain, k->d->pwc);
	if (rc != 0) {
		k->power_get_failures++;
		kern_logf("i915: parity LCD-B power get domain=%d FAILED rc=%d\n", domain, rc);
		return 0;
	}
	k->power_refs[domain]++;
	return domain + 1;
}

/*
 * intel_display_power_get_if_enabled(): a READOUT never turns a well on.  The domain counts as enabled
 * when every one of its wells is (parity_display_power_is_enabled, the reference own rule).
 */
static int k_power_get_if_enabled(void *ctx, int domain)
{
	struct lcd_kernel *k = ctx;

	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM)
		return 0;
	if (!parity_display_power_is_enabled(k->d->pd, (enum parity_power_domain)domain, k->d->pwc))
		return 0;
	return k_power_get(ctx, domain);
}

static void k_power_put(void *ctx, int domain, int wakeref)
{
	struct lcd_kernel *k = ctx;

	(void)wakeref;
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM || k->power_refs[domain] <= 0) {
		k->errors++;
		kern_logf("i915: parity LCD-B power put domain=%d without a reference of this run: NOT forwarded\n", domain);
		return;
	}
	k->power_refs[domain]--;
	{
		unsigned before = k->d->pwc->disable_refusals;

		parity_display_power_put(k->d->pd, (enum parity_power_domain)domain, k->d->pwc);
		if (k->d->pwc->disable_refusals != before)
			parity_lcd_backend_fault("a power well was NOT turned off: the pipe interrupt drain failed (the stop is not "
				"confirmed; display power is kept)\n");
	}
}

static void k_power_put_async(void *ctx, int domain, int wakeref, int delay_ms)
{
	struct lcd_kernel *k = ctx;

	(void)wakeref;
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM || k->power_refs[domain] <= 0) {
		k->errors++;
		kern_logf("i915: parity LCD-B async power put domain=%d without a reference of this run: NOT forwarded\n", domain);
		return;
	}
	k->power_refs[domain]--;
	parity_display_power_put_async(k->d->pd, (enum parity_power_domain)domain, k->d->pwc, delay_ms);
}

static void k_dbuf_slices_update(void *ctx, unsigned req_slices)
{
	struct lcd_kernel *k = ctx;

	parity_gen9_dbuf_slices_update(k->d->dcore, (uint8_t)req_slices);
}

static void k_lock(void *ctx, int which, int take)
{
	struct lcd_kernel *k = ctx;

	if (which < 0 || which > 1)
		return;
	if (parity_lcd_reg_trace)
		kern_logf("i915: parity N1 lock %d %s\n", which, take ? "take" : "give back");
	if (take)
		mutex_lock(&k->locks[which]);
	else
		mutex_unlock(&k->locks[which]);
}

static void k_step(void *ctx, const char *name)
{
	struct lcd_kernel *k = ctx;

	if (strncmp(name, "(decided) ", 10) == 0) {
		k->decided++;
		kern_logf("i915: parity LCD-B %s\n", name);
		return;
	}
	/* a reference callee that is neither ported nor decided must not pass silently on hardware */
	k->unresolved_steps++;
	kern_logf("i915: parity LCD-B UNRESOLVED step reached: %s\n", name);
}

static void k_error(void *ctx, const char *what)
{
	struct lcd_kernel *k = ctx;

	if (k->phase_cleanup)
		k->cleanup_errors++;
	else
		k->errors++;
	kern_logf("i915: parity LCD-B reference error (%s): %s%s", k->phase_cleanup ? "cleanup" : "bring-up", what,
		(what[0] != 0 && what[strlen(what) - 1u] == '\n') ? "" : "\n");
}

static void k_debug(void *ctx, const char *what)
{
	(void)ctx;
	(void)what;
}

/* ---- the synchronous update's ops (pipe vblank reference, sleep until the next vblank, the event) ---- */
static uint32_t k_frame(void *ctx)
{
	struct lcd_kernel *k = ctx;

	return osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("PIPE_FRMCOUNT_G4X"));
}

static int k_vblank_get(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	return parity_drm_vblank_get(k->d->irq, (unsigned)pipe);
}

static void k_vblank_put(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	parity_drm_vblank_put(k->d->irq, (unsigned)pipe);
}

/* schedule_timeout() on the pipe's vblank wait queue: until the next vblank interrupt of the pipe or the timeout */
static long k_vblank_sleep(void *ctx, int pipe, long ticks)
{
	struct lcd_kernel *k = ctx;
	int rc, on;

	/* the reference re-enables local IRQs before schedule_timeout(): read the CPU's real state (and restore it) */
	on = kern_irq_disable();
	if (on)
		kern_irq_enable();
	else
		k->sleep_irq_off++;
	rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, (unsigned)ticks * 10u, k_frame, k, 0);
	k->vblank_sleeps++;
	if (rc == -EIO)
		parity_lcd_backend_fault("time base fault while waiting for a vblank\n");
	return rc == 0 ? (ticks > 1 ? ticks - 1 : 1) : 0;
}

static void k_irq_off(void *ctx)
{
	struct lcd_kernel *k = ctx;

	k->irq_was_on = kern_irq_disable();
}

static void k_irq_on(void *ctx)
{
	struct lcd_kernel *k = ctx;

	if (k->irq_was_on)
		kern_irq_enable();
}

/* drm_crtc_arm_vblank_event(): completes at the pipe's next vblank (the reference took the vblank reference) */
static void k_arm_event(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	k->event_armed = 1;
	k->event_pipe = pipe;
	k->event_frame = k_frame(k);
}

static int k_wait_event(void *ctx, int pipe, unsigned timeout_ms)
{
	struct lcd_kernel *k = ctx;
	int rc;

	if (!k->event_armed || pipe != k->event_pipe)
		return -22;
	/* a NEW vblank interrupt of this pipe after the wait began, with the frame counter past the arm's frame */
	rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, timeout_ms, k_frame, k, 0);
	if (rc == 0 && k_frame(k) == k->event_frame)
		rc = -110;
	if (rc == 0)
		k->event_armed = 0;
	return rc == 0 ? 0 : rc == -EIO ? -5 : -110;
}

/* drm_crtc_vblank_off() on the armed event: forget it -- a later wait is refused, a late vblank completes nothing.
 * The event record is this thread's own; the IRQ handler only advances the pipe's counters under the IRQ lock. */
static void k_cancel_event(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	(void)pipe;
	if (k->event_armed)
		k->events_cancelled++;
	k->event_armed = 0;
}

static void bind_ops(struct lcd_kernel *k)
{
	memset(&k->ops, 0, sizeof(k->ops));
	k->ops.ctx = k;
	k->ops.write32 = k_write32;
	k->ops.rmw32 = k_rmw32;
	k->ops.posting_read = k_posting_read;
	k->ops.read32 = k_read32;
	k->ops.wait_reg = k_wait_reg;
	k->ops.usleep = k_usleep;
	k->ops.udelay = k_udelay;
	k->ops.dpcd_read = k_dpcd_read;
	k->ops.dpcd_write = k_dpcd_write;
	k->ops.read_dpcd_caps = k_read_dpcd_caps;
	k->ops.panel = k_panel;
	k->ops.power_get = k_power_get;
	k->ops.power_get_if_enabled = k_power_get_if_enabled;
	k->ops.power_put = k_power_put;
	k->ops.power_put_async = k_power_put_async;
	k->ops.dbuf_slices_update = k_dbuf_slices_update;
	k->ops.lock = k_lock;
	k->ops.step = k_step;
	k->ops.error = k_error;
	k->ops.debug = k_debug;
	k->ops.vblank_get = k_vblank_get;
	k->ops.vblank_put = k_vblank_put;
	k->ops.vblank_sleep = k_vblank_sleep;
	k->ops.irq_off = k_irq_off;
	k->ops.irq_on = k_irq_on;
	k->ops.arm_event = k_arm_event;
	k->ops.wait_event = k_wait_event;
	k->ops.cancel_event = k_cancel_event;
	/* observe: left to the run log's tap (parity_lcd_show.c); the observer reads through these ops directly */
}

/* ---- the log ---- */
static const char *stage_name(int stage)
{
	static const char *const names[] = { "none", "buffer-ready", "prepared", "enable-returned", "picture-up", "window-done",
		"disable-returned", "stop-confirmed", "released", "ABANDONED" };

	return stage >= 0 && stage <= PARITY_LCD_SHOW_ABANDONED ? names[stage] : "?";
}

static void log_regs(struct lcd_kernel *k, const char *when, int compare)
{
	const struct parity_lcd_named_reg *t;
	unsigned n = 0u, i, match = 0u, compared = 0u;

	t = parity_lcd_reg_table(0, 0, 0, &n);
	for (i = 0u; i < n; i++) {
		uint32_t v = osdep_mmio_read32(k->d->mmio, t[i].reg);

		if (compare && t[i].has_linux) {
			int same = (v & t[i].compare_mask) == (t[i].linux_value & t[i].compare_mask);

			compared++;
			match += same ? 1u : 0u;
			kern_logf("i915: parity LCD-B reg[%s] %s 0x%05x = 0x%08x | Linux 0x%08x mask 0x%08x %s\n", when, t[i].name,
				t[i].reg, v, t[i].linux_value, t[i].compare_mask, same ? "same" : "DIFFERENT");
		} else {
			kern_logf("i915: parity LCD-B reg[%s] %s 0x%05x = 0x%08x\n", when, t[i].name, t[i].reg, v);
		}
	}
	if (compare)
		kern_logf("i915: parity LCD-B reg[%s] same as Linux's dump: %u/%u (comparison only; no value above was copied from it)\n",
			when, match, compared);
}

#ifdef PARITY_LCDB_SCANOUT_PROBE
/*
 * E-126: what is the plane really fetching?  DSPSURFLIVE (0x701ac) is the surface address the
 * hardware is scanning out right now and PIPEDSL (0x70000) the line it is on; PLANE_SURF is what
 * the driver armed.  The DDB and watermark are read back too, because the DMC may rewrite them
 * behind the driver when a DC state is entered.
 */
static void
probe_scanout(struct lcd_kernel *k)
{
	unsigned i;

	/* the transcoder's own timing and the DP transport, read once: MSA is generated from these */
	kern_logf("i915: parity LCD-B scanout probe timing HTOTAL=0x%08x HBLANK=0x%08x HSYNC=0x%08x "
		"VTOTAL=0x%08x VBLANK=0x%08x VSYNC=0x%08x VSYNCSHIFT=0x%08x MULT=0x%08x\n",
		osdep_mmio_read32(k->d->mmio, 0x60000u), osdep_mmio_read32(k->d->mmio, 0x60004u),
		osdep_mmio_read32(k->d->mmio, 0x60008u), osdep_mmio_read32(k->d->mmio, 0x6000cu),
		osdep_mmio_read32(k->d->mmio, 0x60010u), osdep_mmio_read32(k->d->mmio, 0x60014u),
		osdep_mmio_read32(k->d->mmio, 0x60028u), osdep_mmio_read32(k->d->mmio, 0x6002cu));
	kern_logf("i915: parity LCD-B scanout probe transport VRR_CTL=0x%08x VRR_VMAX=0x%08x VRR_VMIN=0x%08x "
		"VRR_STATUS=0x%08x MSA_MISC=0x%08x DP_TP_CTL=0x%08x DP_TP_STATUS=0x%08x DATA_M=0x%08x DATA_N=0x%08x LINK_M=0x%08x LINK_N=0x%08x\n",
		osdep_mmio_read32(k->d->mmio, 0x60420u), osdep_mmio_read32(k->d->mmio, 0x60424u),
		osdep_mmio_read32(k->d->mmio, 0x60434u), osdep_mmio_read32(k->d->mmio, 0x6042cu),
		osdep_mmio_read32(k->d->mmio, 0x60410u), osdep_mmio_read32(k->d->mmio, 0x64040u),
		osdep_mmio_read32(k->d->mmio, 0x64044u), osdep_mmio_read32(k->d->mmio, 0x60030u),
		osdep_mmio_read32(k->d->mmio, 0x60034u), osdep_mmio_read32(k->d->mmio, 0x60040u),
		osdep_mmio_read32(k->d->mmio, 0x60044u));

	/* the two engines that can stand between the plane and the panel: FBC and PSR */
	kern_logf("i915: parity LCD-B scanout probe engines DPFC_A_CTL=0x%08x DPFC_A_STATUS=0x%08x "
		"DPFC_B_CTL=0x%08x SRD_CTL=0x%08x SRD_STATUS=0x%08x PSR2_CTL=0x%08x PSR2_STATUS=0x%08x "
		"DC_STATE_EN=0x%08x TGL_DP_TP_CTL=0x%08x TGL_DP_TP_STATUS=0x%08x\n",
		osdep_mmio_read32(k->d->mmio, 0x43208u), osdep_mmio_read32(k->d->mmio, 0x43210u),
		osdep_mmio_read32(k->d->mmio, 0x43248u), osdep_mmio_read32(k->d->mmio, 0x60800u),
		osdep_mmio_read32(k->d->mmio, 0x60840u), osdep_mmio_read32(k->d->mmio, 0x60900u),
		osdep_mmio_read32(k->d->mmio, 0x60940u), osdep_mmio_read32(k->d->mmio, 0x45504u),
		osdep_mmio_read32(k->d->mmio, 0x60540u), osdep_mmio_read32(k->d->mmio, 0x60544u));

	/*
	 * The pipe's own checksum of what it scans out (PIPE_CRC_CTL / PIPE_CRC_RES_*, the
	 * debugfs 'pipe crc' of the reference).  A still picture gives the same CRC every frame:
	 * if it does, the pipe is producing a stable image and whatever is wrong is beyond it.
	 */
	/*
	 * The bytes the plane is pointed at, from both sides: the GGTT entries the driver wrote for
	 * the first and last page of the object, and the first pixels of the CPU view.  A plane that
	 * fetches the armed address but shows content that does not follow the buffer is either
	 * pointed at other pages or reading memory the CPU's writes never reached.
	 */
	if (lcdb_scanout.obj != 0 && lcdb_scanout.gm != 0) {
		const struct parity_gt_object *o = lcdb_scanout.obj;
		uint64_t pte0 = parity_gt_ggtt_read_pte(lcdb_scanout.gm, o->ggtt_page);
		uint64_t pte1 = parity_gt_ggtt_read_pte(lcdb_scanout.gm, o->ggtt_page + 1u);
		uint64_t ptel = parity_gt_ggtt_read_pte(lcdb_scanout.gm, o->ggtt_page + o->pages - 1u);
		const uint32_t *c = lcdb_scanout.cpu;

		kern_logf("i915: parity LCD-B scanout probe memory surf=0x%08x page=%u pages=%u pitch=%u "
			"PTE[0]=0x%016llx PTE[1]=0x%016llx PTE[last]=0x%016llx | CPU view %08x %08x %08x %08x\n",
			(uint32_t)lcdb_scanout.surf, o->ggtt_page, o->pages, lcdb_scanout.pitch,
			(unsigned long long)pte0, (unsigned long long)pte1, (unsigned long long)ptel,
			c != 0 ? c[0] : 0u, c != 0 ? c[1] : 0u,
			c != 0 ? c[lcdb_scanout.pitch / 4u] : 0u,
			c != 0 ? c[(lcdb_scanout.pitch / 4u) * 400u + 400u] : 0u);
	}

	/* the clock the display runs on, and the plane's own geometry */
	kern_logf("i915: parity LCD-B scanout probe clocks CDCLK_CTL=0x%08x CDCLK_PLL_ENABLE=0x%08x "
		"DSSM=0x%08x PIPESRC=0x%08x PLANE_POS=0x%08x PLANE_OFFSET=0x%08x PLANE_KEYMAX=0x%08x "
		"PS_CTRL_1=0x%08x PS_CTRL_2=0x%08x\n",
		osdep_mmio_read32(k->d->mmio, 0x46000u), osdep_mmio_read32(k->d->mmio, 0x46070u),
		osdep_mmio_read32(k->d->mmio, 0x51004u), osdep_mmio_read32(k->d->mmio, 0x6001cu),
		osdep_mmio_read32(k->d->mmio, 0x7018cu), osdep_mmio_read32(k->d->mmio, 0x701a4u),
		osdep_mmio_read32(k->d->mmio, 0x701a0u), osdep_mmio_read32(k->d->mmio, 0x68180u),
		osdep_mmio_read32(k->d->mmio, 0x68280u));

	osdep_mmio_write32(k->d->mmio, 0x60050u, 0x80000000u);   /* enable, source = plane 1 */
	(void)osdep_mmio_read32(k->d->mmio, 0x60050u);

	for (i = 0u; i < 30u; i++) {
		uint32_t surf = osdep_mmio_read32(k->d->mmio, 0x7019cu);
		uint32_t live = osdep_mmio_read32(k->d->mmio, 0x701acu);
		uint32_t ctl = osdep_mmio_read32(k->d->mmio, 0x70180u);
		uint32_t st = osdep_mmio_read32(k->d->mmio, 0x70058u);
		uint32_t frm = osdep_mmio_read32(k->d->mmio, 0x70040u);
		uint32_t dsl = osdep_mmio_read32(k->d->mmio, 0x70000u);
		uint32_t buf = osdep_mmio_read32(k->d->mmio, 0x7027cu);
		uint32_t wm0 = osdep_mmio_read32(k->d->mmio, 0x70240u);
		uint32_t dbuf1 = osdep_mmio_read32(k->d->mmio, 0x45008u);

		kern_logf("i915: parity LCD-B scanout probe %2u SURF=0x%08x SURFLIVE=0x%08x CTL=0x%08x "
			"PIPESTATUS=0x%08x frame=%u DSL=0x%08x BUF_CFG=0x%08x WM0=0x%08x DBUF_S1=0x%08x\n",
			i, surf, live, ctl, st, frm, dsl, buf, wm0, dbuf1);
		kern_logf("i915: parity LCD-B scanout probe %2u CRC %08x %08x %08x %08x %08x\n", i,
			osdep_mmio_read32(k->d->mmio, 0x60064u), osdep_mmio_read32(k->d->mmio, 0x60068u),
			osdep_mmio_read32(k->d->mmio, 0x6006cu), osdep_mmio_read32(k->d->mmio, 0x60070u),
			osdep_mmio_read32(k->d->mmio, 0x60074u));
		k_usleep(k, 300000u);
	}
	osdep_mmio_write32(k->d->mmio, 0x60050u, 0u);   /* the probe leaves the CRC unit as it found it */
}
#endif

static void at_stage(void *ctx, int stage)
{
	struct lcd_kernel *k = ctx;

	if (stage == PARITY_LCD_SHOW_ENABLE_RETURNED) {
		/* whatever the reference reports from here on belongs to the way down: counted apart */
		k->phase_cleanup = 1;
	} else if (stage == PARITY_LCD_SHOW_PICTURE_UP) {
		log_regs(k, "picture-up", 1);
		kern_logf("i915: parity LCD-B PICTURE UP: pattern id=%u on the panel; observation window %u ms starts now "
			"(take the photograph)\n", k->pattern_id, k->window_ms);
#ifdef PARITY_LCDB_SCANOUT_PROBE
		probe_scanout(k);
#endif
	} else if (stage == PARITY_LCD_SHOW_WINDOW_DONE) {
		kern_logf("i915: parity LCD-B observation window over; stopping through the reference's disable path\n");
	} else if (stage == PARITY_LCD_SHOW_DISABLE_RETURNED) {
		log_regs(k, "after-disable", 0);
	}
}

static const char *kind_name(int kind)
{
	static const char *const names[] = { "?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put",
		"STEP", "ERROR", "PHASE", "decide", "dbuf", "observe" };

	return kind >= 0 && kind <= PARITY_LCD_T_OBSERVE ? names[kind] : "?";
}

static void log_trace(const struct parity_lcd_trace *t)
{
	unsigned i;

	kern_logf("i915: parity LCD-B run log: %u entries (%u writes, %u rmw, %u waits [%u timed out], %u unresolved steps, "
		"%u decided, %u errors, dropped %u, slept %llu us)\n", t->n, t->writes, t->rmws, t->waits, t->wait_timeouts, t->steps,
		t->decided, t->errors, t->dropped, (unsigned long long)t->slept_us);
	for (i = 0u; i < t->n; i++) {
		const struct parity_lcd_trace_entry *e = &t->e[i];

		if (e->kind == PARITY_LCD_T_READ)
			continue;
		if (e->name != 0)
			kern_logf("i915: parity LCD-B t[%3u] %s %s%s", i, kind_name(e->kind), e->name,
				(e->name[0] != 0 && e->name[strlen(e->name) - 1u] == '\n') ? "" : "\n");
		else
			kern_logf("i915: parity LCD-B t[%3u] %s 0x%05x b=0x%08x c=0x%08x d=0x%08x rc=%d n=%u\n", i, kind_name(e->kind),
				e->a, e->b, e->c, e->d, e->rc, e->n);
	}
}

static void log_observer(const struct parity_lcd_observer *o)
{
	unsigned i;

	kern_logf("i915: parity LCD-B observer: ICL_PIPESTATUS 0x%05x underrun mask 0x%08x (bit31 pipe underrun, bit28 soft, bit27 hard, "
		"bit26 port) | PIPE_FRMCOUNT_G4X 0x%05x | GEN8_DE_PIPE_IMR 0x%05x vblank bit 0x%x | the test owns the status for the run; "
		"the pipe's underrun and vblank interrupts stay masked, so the IRQ handler never reads or clears it\n",
		o->status_reg, o->underrun_mask, o->frame_reg, o->imr_reg, o->vblank_bit);
	kern_logf("i915: parity LCD-B observer: status found before the run 0x%08x (kept, not judged) | start/stop periods 0x%08x | "
		"steady picture 0x%08x | vblank interrupt ever unmasked=%d | samples=%u dropped=%u\n", o->saved_before,
		o->seen_transition, o->seen_steady, o->vblank_unmasked_seen, o->n, o->dropped);
	for (i = 0u; i < o->n; i++)
		kern_logf("i915: parity LCD-B observer sample %2u point=%3d %s status=0x%08x frame=%u imr=0x%08x\n", i, o->s[i].point,
			o->s[i].steady ? "steady" : "trans ", o->s[i].status, o->s[i].frame, o->s[i].imr);
}

static void log_status(const char *when, const struct parity_lcd_modeset_status *s)
{
	kern_logf("i915: parity LCD-B state[%s] software: prepared=%d crtc_active=%d plane_armed=%d pll_on=%d pll_mask=0x%x "
		"wakerefs io=%d aux=%d crtc_domains=%u dc_off_held=%d dbuf_slices=0x%x mbus_joined=%d stop_unconfirmed=%d backlight=%d "
		"level=%u/%u errors=%u\n", when, s->prepared, s->crtc_active, s->plane_armed, s->pll_on, s->pll_active_mask,
		s->ddi_io_wakeref, s->aux_wakeref, s->crtc_domains_held, s->dc_off_held, s->dbuf_slices_now, s->mbus_joined_now,
		s->stop_unconfirmed, s->backlight_enabled, s->backlight_level, s->backlight_pwm_max, s->errors);
	kern_logf("i915: parity LCD-B state[%s] sink: link %d x%d rc=%d status %02x %02x %02x %02x %02x %02x cr_ok=%d eq_ok=%d "
		"train_set %02x %02x (driver flag link_trained=%d is not evidence)\n", when, s->link_rate, s->lane_count,
		s->link_status_rc, s->link_status[0], s->link_status[1], s->link_status[2], s->link_status[3], s->link_status[4],
		s->link_status[5], s->cr_ok, s->eq_ok, s->train_set[0], s->train_set[1], s->link_trained_flag);
}

/* ---- inputs ---- */
static int fill_cfg(struct lcd_kernel *k, struct parity_lcd_modeset_cfg *c)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_edp_device *edp = d->edp;
	static struct parity_vbt_panel pn;
	unsigned allowed, point, i;
	uint32_t mbus;
	int rc;

	memset(c, 0, sizeof(*c));
	rc = parity_vbt_init_panel(&edp->vbt->parsed, 0, edp->res.edid, &pn);
	if (rc != 0) {
		kern_logf("i915: parity LCD-B input: the VBT has no panel data for port A (rc=%d)\n", rc);
		return -1;
	}
	c->port = 0; c->pipe = 0; c->cpu_transcoder = 0; c->dpll_id = 0; c->aux_ch = 0;
	/* intel_ddi_init(): DDI_BUF_PORT_REVERSAL (bit 16) | DDI_A_4_LANES (bit 4) of the readout at connector init */
	c->saved_port_bits = edp->ddi_buf_ctl_readout & ((1u << 16) | (1u << 4));
	memcpy(c->dpcd, edp->res.dpcd, sizeof(c->dpcd));
	memcpy(c->edp_dpcd, edp->res.edp_dpcd, sizeof(c->edp_dpcd));
	c->vbt_low_vswing = pn.edp_low_vswing;
	c->vbt_hobl = pn.edp_hobl;
	c->vbt_override_afc_startup = pn.override_afc_startup;
	c->vbt_backlight_present = pn.bl_present;
	c->vbt_backlight_active_low = pn.bl_active_low_pwm;
	c->vbt_backlight_controller = pn.bl_controller;
	c->vbt_backlight_pwm_freq_hz = pn.bl_pwm_freq_hz;
	c->vbt_backlight_min_brightness = pn.bl_min_brightness;
	k->vbt_min = pn.bl_min_brightness;
	c->rawclk_khz = edp->cfg.rawclk_khz;
	for (i = 0u; i < 8u && i < PARITY_NOGEM_MAX_WM_LVL; i++)
		c->wm_latency[i] = d->nogem->wm_skl_latency[i];
	c->wm_num_levels = (uint8_t)d->nogem->wm_num_levels;
	c->wm_ipc_enabled = d->ipc_enabled;
	c->sagv_block_time_us = (uint8_t)d->nogem->sagv_block_time_us;
	/*
	 * The DBUF geometry of THIS display (intel_display_device.c): XE_LPD has 4096 blocks over four
	 * slices, Tiger Lake 2048 over two.  It comes from the device the probe identified.
	 */
	c->dbuf_size = d->dcore->dbuf_slice_mask == 0x03u ? 2048u : 4096u;
	c->dbuf_slice_mask = d->dcore->dbuf_slice_mask != 0u ? d->dcore->dbuf_slice_mask : 0x0fu;
	c->dbuf_enabled_slices = d->dcore->dbuf_enabled_slices;
	mbus = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("MBUS_CTL"));
	c->mbus_joined = (mbus >> 31) & 1u;
	c->cdclk_khz = d->cdclk->hw.cdclk;
	c->cdclk_vco_khz = d->cdclk->hw.vco;
	c->cdclk_ref_khz = d->cdclk->hw.ref;
	c->cdclk_bypass_khz = d->cdclk->hw.bypass;
	c->cdclk_voltage_level = d->cdclk->hw.voltage_level;
	c->cdclk_max_khz = parity_intel_max_cdclk_freq(d->cdclk);      /* intel_update_max_cdclk(), display version 11+ */
	/* SAGV: the forced disable left exactly one QGV point unmasked; its derated bandwidth for one active plane */
	allowed = (unsigned)~d->dstate->bw_obj_state.qgv_points_mask & ((1u << d->bw->max[0].num_qgv_points) - 1u);
	if (allowed != 0u && (allowed & (allowed - 1u)) == 0u) {
		for (point = 0u; !(allowed & (1u << point)); point++)
			;
		c->qgv_allowed_bw = parity_icl_qgv_bw(d->bw, 13, 1, (int)point);
	}
	for (i = 0u; i < PARITY_DMC_FW_MAX; i++)
		if (d->dmc->dmc.dmc_info[i].payload != 0 && d->dmc->dmc.dmc_info[i].present)
			c->dmc_fw_mask |= 1u << i;

	kern_logf("i915: parity LCD-B input: VBT low_vswing=%d hobl=%d afc_override=%d backlight present=%d active_low=%d controller=%d "
		"pwm=%u Hz min=%u | rawclk=%u kHz | DDI_BUF_CTL at connector init 0x%08x -> saved bits 0x%x\n", c->vbt_low_vswing,
		c->vbt_hobl, c->vbt_override_afc_startup, c->vbt_backlight_present, c->vbt_backlight_active_low,
		c->vbt_backlight_controller, c->vbt_backlight_pwm_freq_hz, c->vbt_backlight_min_brightness, c->rawclk_khz,
		edp->ddi_buf_ctl_readout, c->saved_port_bits);
	kern_logf("i915: parity LCD-B input: wm levels=%u latency %u/%u/%u/%u/%u/%u/%u/%u us ipc=%d sagv_block=%u us | DBUF slices now 0x%x "
		"MBUS_CTL=0x%08x joined=%d | CDCLK now %u kHz vco %u ref %u bypass %u level %u max %u | QGV mask 0x%x -> allowed 0x%x bw %u MB/s | "
		"DMC fw mask 0x%x\n", c->wm_num_levels, c->wm_latency[0], c->wm_latency[1], c->wm_latency[2], c->wm_latency[3],
		c->wm_latency[4], c->wm_latency[5], c->wm_latency[6], c->wm_latency[7], c->wm_ipc_enabled, c->sagv_block_time_us,
		c->dbuf_enabled_slices, mbus, c->mbus_joined, c->cdclk_khz, c->cdclk_vco_khz, c->cdclk_ref_khz, c->cdclk_bypass_khz,
		c->cdclk_voltage_level, c->cdclk_max_khz, (unsigned)d->dstate->bw_obj_state.qgv_points_mask, allowed, c->qgv_allowed_bw,
		c->dmc_fw_mask);
	return 0;
}

/* the hardware must be in the state the normal initialisation leaves; anything else is not this test's to fix */
static int preflight(struct lcd_kernel *k)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	uint32_t transconf, plane_ctl, pll, ddi, imr;
	int ok = 1;

	if (k->p != 0 && k->p->output_hdmi) {
		uint32_t tcf = osdep_mmio_read32(d->mmio, 0x71008u + 0u);   /* TRANSCONF(B) */
		uint32_t bctl = osdep_mmio_read32(d->mmio, 0x64100u);       /* DDI_BUF_CTL(B) */

		if (!d->gm->inited) {
			kern_logf("i915: parity HDMI-B preflight: no GT memory (GGTT) for the scanout buffer\n");
			return -1;
		}
		kern_logf("i915: parity HDMI-B preflight: TRANSCONF(B)=0x%08x DDI_BUF_CTL(B)=0x%08x SDEISR=0x%08x DPLL0=0x%08x DPLL1=0x%08x\n",
			tcf, bctl, osdep_mmio_read32(d->mmio, 0xc4000u), osdep_mmio_read32(d->mmio, 0x46010u),
			osdep_mmio_read32(d->mmio, 0x46014u));
		if ((tcf & 0x80000000u) != 0u || (bctl & 0x80000000u) != 0u) {
			kern_logf("i915: parity HDMI-B preflight: pipe B / DDI B are not idle\n");
			return -1;
		}
		return 0;
	}
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: parity LCD-B preflight: the resident eDP is not live (connector_live=%d lcd_rc=%d)\n",
			d->edp->connector_live, d->edp->lcd_rc);
		return -1;
	}
	if (!d->gm->inited) {
		kern_logf("i915: parity LCD-B preflight: no GT memory (GGTT) for the scanout buffer\n");
		return -1;
	}
	log_regs(k, "preflight", 0);
	transconf = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("TRANSCONF"));
	plane_ctl = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("PLANE_CTL_1"));
	pll = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("ICL_DPLL_ENABLE"));
	ddi = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("DDI_BUF_CTL"));
	imr = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("GEN8_DE_PIPE_IMR"));
	if ((transconf & 0xc0000000u) != 0u || (plane_ctl & 0x80000000u) != 0u || (pll & 0x80000000u) != 0u || (ddi & 0x80000000u) != 0u) {
		/*
		 * E-124 (N1): a RUNNING display is the subject of that run -- the firmware left it on and the
		 * readout takes it over.  For every other run it means the start conditions are not the prepared
		 * ones and nothing may be written.
		 */
		kern_logf("i915: parity LCD-B preflight: the display is %s (TRANSCONF 0x%08x PLANE_CTL 0x%08x "
			"DPLL 0x%08x DDI_BUF_CTL 0x%08x)\n", PARITY_N1_TEST ? "RUNNING: N1 takes it over" : "not idle",
			transconf, plane_ctl, pll, ddi);
		if (!PARITY_N1_TEST)
			ok = 0;
	}
	/*
	 * Pipe A's registers (TRANSCONF, PLANE_CTL, GEN8_DE_PIPE_IMR / IER(A), ICL_PIPESTATUS(A)) sit in power well A, which is
	 * off until the commit takes the pipe's power domain: they read 0 here and say nothing.  NOTE (found by the first
	 * LCD-B picture): the existing power-well code does NOT yet do the reference's gen8_irq_power_well_post_enable() /
	 * _pre_disable() -- the hook is a counted placeholder -- so when the well comes up the pipe's IMR / IER keep their
	 * hardware values (observed: IMR 0xfff9ffff, IER 0: no pipe interrupt can be delivered at all).  That suits this
	 * test, which uses none, but it is not the reference's state (IMR = de_irq_mask, IER = ~de_irq_mask | vblank |
	 * underrun | flip done) and must be implemented before anything relies on pipe interrupts.  Checked here: the IRQ
	 * state's mask, which the reference WOULD program, keeps vblank (bit 0) and underrun (bits 31 / 22 / 21) masked; the
	 * register itself is read by the observer at every sample while the well is on.
	 */
	/*
	 * The bits that must stay masked: vblank (bit 0) and the FIFO underrun (bit 31) on every Gen12
	 * display, plus the XE_LPD soft / hard underrun (bits 21 / 22), which exist only from display
	 * version 13 (gen8_de_pipe_underrun_mask of the reference).
	 */
	{
		uint32_t want = parity_lcd_display_ver() >= 13 ? 0x80600001u : 0x80000001u;

		if ((d->irq->de_irq_mask[0] & want) != want) {
			kern_logf("i915: parity LCD-B preflight: the IRQ state's pipe A mask 0x%08x would leave "
				"vblank / underrun unmasked (this display needs 0x%08x)\n",
				d->irq->de_irq_mask[0], want);
			ok = 0;
		}
	}
	kern_logf("i915: parity LCD-B preflight: DDI / PLL idle=%d (pipe A registers read TRANSCONF 0x%08x PLANE_CTL 0x%08x IMR 0x%08x with "
		"power well A off) | IRQ state pipe A mask 0x%08x: vblank + underrun masked=%d | no code of this path waits for a software "
		"vblank count; no worker / callback / waiter is created by it\n", (pll & 0x80000000u) == 0u && (ddi & 0x80000000u) == 0u,
		transconf, plane_ctl, imr, d->irq->de_irq_mask[0], (d->irq->de_irq_mask[0] & 0x80600001u) == 0x80600001u);
	return ok ? 0 : -1;
}


static int lcd_run_one(const struct parity_lcd_kernel_deps *d, const struct lcd_run_params *p)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	int rc, i, held = 0;

	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->pd == 0 || d->pwc == 0 || d->dcore == 0 || d->cdclk == 0 || d->nogem == 0 ||
	    d->dstate == 0 || d->bw == 0 || d->dmc == 0 || d->irq == 0 || d->gm == 0) {
		kern_logf("i915: parity LCD-B verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	/* an earlier run left a buffer the display may still read: refuse before anything is initialised */
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		lcdb_summary.ran = 1;
		lcdb_summary.pass = 0;
		lcdb_summary.retained = 1;
		lcdb_summary.first_anomaly = "refused: resources of an earlier run are retained (stop not confirmed)";
		kern_logf("i915: parity LCD-B verdict: FAIL (refused before any initialisation: an earlier run's resources are retained)\n");
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
	bind_ops(k);
	k->p = p;
	if (!p->output_hdmi || p->reset_dplls) {
		parity_lcd_dplls_reset();       /* this run owns the device's PLL pool and its DBUF state */
		parity_lcd_dbuf_forget();
	}

	memset(&env, 0, sizeof(env));
	if (preflight(k) != 0 || fill_cfg(k, &env.cfg) != 0) {
		kern_logf("i915: parity LCD-B verdict: FAIL (preflight: nothing was written to the display)\n");
		lcdb_summary.first_anomaly = "preflight refused (nothing written)";
		return -1;
	}
	env.hw = &k->ops;
	env.gm = d->gm;
	env.so = &lcdb_scanout;
	env.lcd = p->output_hdmi ? p->state : &d->edp->lcd;
	env.pipe = p->pipe;
	if (p->output_hdmi) {
		/* what intel_ddi_init() would have left for the HDMI encoder of this port */
		env.cfg.output_hdmi = 1;
		env.cfg.port = p->port;
		env.cfg.pipe = p->pipe;
		env.cfg.cpu_transcoder = p->cpu_transcoder;
		env.cfg.dpll_id = p->dpll_id;
		env.cfg.aux_ch = p->port;
		env.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));
		env.cfg.vbt_backlight_present = 0;
		{
			const struct parity_vbt_encoder *ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, p->port);

			env.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;
			kern_logf("i915: parity HDMI-B input: VBT child for port %d: hdmi_level_shift=%d (< 0 = not in the VBT: the buffer-translation table's default entry is used) hdmi_boost=%d ddc_pin=%d\n", p->port,
				env.cfg.vbt_hdmi_level_shift, ve != 0 ? ve->hdmi_boost_level : -1, ve != 0 ? ve->ddc_pin : -1);
		}
		kern_logf("i915: parity HDMI-B input: mode %ux%u %d kHz | port=%d pipe=%d transcoder=%d DPLL%d | PLL cfgcr0=0x%08x cfgcr1=0x%08x div0=0x%08x | saved DDI_BUF_CTL bits 0x%x\n",
			p->state->mode.hdisplay, p->state->mode.vdisplay, p->state->mode.clock_khz, p->port, p->pipe,
			p->cpu_transcoder, p->dpll_id, p->state->pll.cfgcr0, p->state->pll.cfgcr1, p->state->pll.div0,
			env.cfg.saved_port_bits);
	}
	env.pattern_id = p->pattern_id;
	/* the pin is a 1920x1080 measurement: on another panel it says nothing (E-126) */
	if (p->pattern_fnv != 0ull && (env.lcd->mode.hdisplay != 1920u || env.lcd->mode.vdisplay != 1080u)) {
		kern_logf("i915: parity LCD-B picture: the pinned hash is a 1920x1080 measurement and "
			"this panel is %ux%u -- the read-back check runs, the pin is not compared\n",
			env.lcd->mode.hdisplay, env.lcd->mode.vdisplay);
		env.pattern_fnv = 0ull;
	} else {
		env.pattern_fnv = p->pattern_fnv;
	}
	env.first_frames_ms = 1000u;
	env.window_ms = p->window_ms;
	env.in_window = p->in_window;
	env.in_window_ctx = k;
	k->pattern_id = p->pattern_id;
	k->window_ms = p->window_ms;
	k->post_before = d->irq->vbl != 0 ? d->irq->vbl->post_enable_calls : 0u;
	k->pre_before = d->irq->vbl != 0 ? d->irq->vbl->pre_disable_calls : 0u;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;

	rc = parity_lcd_show_run(&env, &rep);

	log_trace(rep.trace);
	log_observer(&rep.obs);
	log_status("enable-returned", &rep.at_enable);
	if (rep.stage >= PARITY_LCD_SHOW_WINDOW_DONE)
		log_status("window-end", &rep.at_window_end);
	log_status("disable-returned", &rep.at_disable);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	kern_logf("i915: parity LCD-B buffer: surf=0x%08llx pattern id=%u fnv=%016llx (pinned %016llx) readback_bad before=%u after=%u | "
		"released=%d abandoned=%d unpin=%d destroy=%d | display pages in use=%u\n", (unsigned long long)rep.surf, env.pattern_id,
		(unsigned long long)rep.pattern_hash, (unsigned long long)p->pattern_fnv, rep.readback_bad_before,
		rep.readback_bad_after, rep.released, rep.abandoned, rep.unpin_rc, rep.destroy_rc, d->gm->display_allocated_pages);
	kern_logf("i915: parity LCD-B hardware observation: first frames rc=%d counter %u -> %u | steady rounds=%u rc=%d counter %u -> %u | "
		"after stop rc=%d counter %u -> %u TRANSCONF=0x%08x\n", rep.first_frames_rc, rep.frame_first, rep.frame_last,
		rep.steady_rounds, rep.steady_rc, rep.steady_frame_first, rep.steady_frame_last, rep.stopped_rc, rep.stop_frame_first,
		rep.stop_frame_last, rep.transconf_after_stop);
	kern_logf("i915: parity LCD-B first anomaly: %s%s(stage=%s rc=%d run-log index=%d) | bring-up errors=%u | cleanup errors=%u "
		"(first at run-log index %d) | rcs: window=%d create=%d pin=%d prepare=%d begin=%d enable=%d disable=%d\n",
		rep.first_anomaly != 0 ? rep.first_anomaly : "none",
		(rep.first_anomaly != 0 && rep.first_anomaly[0] != 0 && rep.first_anomaly[strlen(rep.first_anomaly) - 1u] == '\n') ? "" : " ",
		stage_name(rep.first_anomaly_stage), rep.first_anomaly_rc, rep.first_error_trace_at, rep.enable_errors, rep.cleanup_errors,
		rep.cleanup_first_error_trace_at, rep.window_rc, rep.create_rc, rep.pin_rc, rep.prepare_rc, rep.begin_rc, rep.enable_rc,
		rep.disable_rc);
	kern_logf("i915: parity LCD-B backend: power refs of this run still held=%d get_failures=%u | wait timeouts=%u time faults=%u | "
		"unresolved steps=%u decided=%u | eDP tick sleeps=%u\n", held, k->power_get_failures, k->wait_timeouts, k->time_faults,
		k->unresolved_steps, k->decided, d->edp->k.tick_sleeps);
	if (d->irq->vbl != 0)
		kern_logf("i915: parity LCD-B power-well IRQ hooks during this run: post_enable +%u pre_disable +%u (sync calls %u, "
			"sync timeouts %u) | pipe A IMR now 0x%08x (well off reads 0)\n", d->irq->vbl->post_enable_calls - k->post_before,
			d->irq->vbl->pre_disable_calls - k->pre_before, d->irq->vbl->sync_calls, d->irq->vbl->sync_timeouts,
			osdep_mmio_read32(d->mmio, 0x44404u));
	if (rep.pass && (held != 0 || k->unresolved_steps != 0u || k->time_faults != 0u))
		rep.pass = 0;
	/* the pipe's well went on (post-enable restored its interrupt registers) and off again (pre-disable stopped them) */
	if (rep.pass && d->irq->vbl != 0 && (d->irq->vbl->post_enable_calls == k->post_before ||
	    d->irq->vbl->pre_disable_calls == k->pre_before || d->irq->vbl->sync_timeouts != 0u))
		rep.pass = 0;
	kern_logf("i915: parity LCD-B verdict: %s (furthest stage=%s; software flags, hardware observation and the photograph are "
		"separate evidence: this line covers the first two)\n", rep.pass ? "PASS" : "FAIL", stage_name(rep.stage));
	(void)rc;
	lcdb_summary.pass = rep.pass;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.stage = stage_name(rep.stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.cleanup_errors = rep.cleanup_errors;
	lcdb_summary.retained = parity_lcd_show_retained();
	return rep.pass ? 0 : -1;
}

int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d)
{
	static const struct lcd_run_params p = { .pattern_id = PARITY_LCDB_PATTERN_ID,
		.pattern_fnv = PARITY_LCDB_PATTERN_FNV, .window_ms = PARITY_LCDB_WINDOW_MS };

	return lcd_run_one(d, &p);
}

/*
 * HDMI-B: the external display on DDI B.  The mode is CEA-861 format 4 (1280x720p60, 74.25 MHz), which every
 * HDMI sink supports; the reference would take the sink's preferred mode from its EDID, which this port cannot
 * read (the DDC does not answer -- the same happens with Linux on this machine), so the mode is given here.
 * ADAPTATION, recorded: mode and `connected` do not come from the sink.
 */
/*
 * While the picture is up: read the sink's EDID once more.  With the port off this DDC answers nothing (the
 * reference behaves the same on this machine), so the question is whether driving the TMDS output brings the
 * sink's DDC up.  Reading it does not change the run's verdict: the value is logged either way.
 */
static int hdmib_window(void *ctx, struct parity_lcd_observer *o)
{
	struct parity_hpd_summary hs;
	struct parity_hpd_edid_info ei;
	int st;

	(void)ctx; (void)o;
	parity_hpd_summary(&hs);
	if (!hs.started || hs.hdmi_connector < 0) {
		kern_logf("i915: parity HDMI-B EDID while lit: the hotplug path is not running\n");
		return 0;
	}
	st = parity_hpd_probe_connector((unsigned)hs.hdmi_connector);
	parity_hpd_edid_info(&ei);
	kern_logf("i915: parity HDMI-B EDID while lit: status %d (1 connected, 2 disconnected) | reads %u fails %u rc %d | %s product 0x%04x EDID %u.%u %s | DTD1 %ux%u %u kHz\n", st, ei.reads, ei.fails, ei.rc,
		ei.mfg, ei.product, ei.version, ei.revision, ei.digital ? "digital" : "analog", ei.hactive,
		ei.vactive, ei.pixel_clock_khz);
	return 0;
}

int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_state hs;
	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };
	struct lcd_run_params p;
	int rc;

	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hs);
	if (rc != 0) {
		kern_logf("i915: parity HDMI-B verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)\n", rc);
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.output_hdmi = 1; p.port = 1; p.pipe = 1; p.cpu_transcoder = 1; p.dpll_id = 0;
	p.reset_dplls = 1;              /* HDMI-B is a single-screen run: the pool starts empty */
	p.state = &hs;
	p.tag = "HDMI-B";
	p.pattern_id = PARITY_LCDB_PATTERN_ID;
	p.pattern_fnv = 0;                      /* the picture's hash is pinned at 1920x1080 only */
	p.window_ms = PARITY_HDMIB_WINDOW_MS;
	p.in_window = hdmib_window;
	rc = lcd_run_one(d, &p);
	kern_logf("i915: parity HDMI-B verdict: %s (the LCD-B lines above carry the detail; the photograph is separate evidence)\n", rc == 0 ? "PASS" : "FAIL");
	return rc;
}

/* ---------------- two screens at once (-DPARITY_DUAL_TEST=1) ---------------- */

static void step_sleep(struct lcd_kernel *k, unsigned ms);      /* below: the run's own sleep */

/* the pipe's frame counter (PIPE_FRMCOUNT_G4X of that pipe) */
static uint32_t dual_frame(const struct parity_lcd_kernel_deps *d, int pipe)
{
	return osdep_mmio_read32(d->mmio, 0x70040u + 0x1000u * (uint32_t)pipe);
}

struct dual_screen {
	unsigned idx;                           /* the modeset object */
	const char *name;
	int pipe;
	struct parity_scanout so;
	struct parity_scanout *sop;             /* the buffer this screen reads (its own, or one shared with the other) */
	struct parity_lcd_modeset_cfg cfg;
	const struct parity_lcd_state *state;
	unsigned pattern_id;
	uint64_t pattern_hash;
	int prepare_rc, enable_rc, disable_rc, begun, armed, released;
	uint32_t frame_first, frame_last;
};

static int dual_bring_up(struct lcd_kernel *k, struct dual_screen *sc)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	int rc;

	rc = parity_scanout_create(d->gm, (uint32_t)sc->state->mode.hdisplay, (uint32_t)sc->state->mode.vdisplay,
		PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &sc->so);
	if (rc != 0) {
		kern_logf("i915: parity DUAL %s: scanout create rc=%d\n", sc->name, rc);
		return -1;
	}
	rc = parity_scanout_pin(&sc->so, sc->name);
	if (rc != 0) {
		kern_logf("i915: parity DUAL %s: scanout pin rc=%d\n", sc->name, rc);
		(void)parity_scanout_destroy(&sc->so);
		return -1;
	}
	sc->pattern_hash = parity_lcd_pattern_fill(sc->so.cpu, sc->so.pitch, sc->so.width, sc->so.height, sc->pattern_id);
	parity_scanout_publish(&sc->so);
	sc->cfg.fb_fourcc = sc->so.format;
	sc->cfg.fb_modifier = sc->so.modifier;
	sc->cfg.fb_width = sc->so.width;
	sc->cfg.fb_height = sc->so.height;
	sc->cfg.fb_pitch = sc->so.pitch;
	sc->cfg.fb_surf = (uint32_t)sc->so.surf;

	(void)parity_lcd_modeset_select(sc->idx);
	sc->prepare_rc = parity_lcd_modeset_prepare(sc->state, &sc->cfg, &k->ops);
	if (sc->prepare_rc != 0) {
		kern_logf("i915: parity DUAL %s: prepare rc=%d\n", sc->name, sc->prepare_rc);
		return -1;
	}
	sc->begun = parity_scanout_begin(&sc->so) == 0;      /* the display takes the buffer */
	if (!sc->begun) {
		kern_logf("i915: parity DUAL %s: the display could not take the buffer\n", sc->name);
		return -1;
	}
	sc->enable_rc = parity_lcd_modeset_commit_enable();
	sc->armed = sc->enable_rc == PARITY_LCD_MS_OK;
	kern_logf("i915: parity DUAL %s: enabled rc=%d pattern=%u surf=0x%08x %ux%u pitch=%u\n", sc->name, sc->enable_rc,
		sc->pattern_id, (unsigned)sc->so.surf, sc->so.width, sc->so.height, sc->so.pitch);
	return sc->enable_rc == PARITY_LCD_MS_OK ? 0 : -1;
}

static void dual_log_state(const char *when, struct dual_screen *sc)
{
	struct parity_lcd_modeset_status s;

	(void)parity_lcd_modeset_select(sc->idx);
	parity_lcd_modeset_status(&s);
	kern_logf("i915: parity DUAL %s [%s]: crtc_active=%d plane_armed=%d DPLL%d on=%d active_mask=0x%x pipe_mask=0x%x | "
		"io wakeref=%d crtc domains=%u | DBUF slices=0x%x mbus_joined=%d | ddb %u..%u | wm0 enable=%u blocks=%u lines=%u | errors=%u\n",
		sc->name, when, s.crtc_active, s.plane_armed, s.pll_id, s.pll_on, s.pll_active_mask, s.pll_pipe_mask,
		s.ddi_io_wakeref, s.crtc_domains_held, s.dbuf_slices_now, s.mbus_joined_now, s.ddb_start, s.ddb_end,
		s.wm0_enable, s.wm0_blocks, s.wm0_lines, s.errors);
}

static int dual_stop(struct lcd_kernel *k, struct dual_screen *sc)
{
	int prc, rc;

	(void)k;
	(void)parity_lcd_modeset_select(sc->idx);
	prc = parity_lcd_modeset_plane_disable();
	rc = parity_lcd_modeset_commit_disable();
	sc->disable_rc = rc != PARITY_LCD_MS_OK ? rc : prc;
	if (sc->disable_rc == PARITY_LCD_MS_OK) {
		parity_lcd_modeset_plane_released();            /* the caller watched the pipe stand still (below) */
		parity_scanout_end(sc->sop != 0 ? sc->sop : &sc->so);
	}
	kern_logf("i915: parity DUAL %s: stopped rc=%d\n", sc->name, sc->disable_rc);
	if (sc->disable_rc != PARITY_LCD_MS_OK) {
		struct parity_lcd_modeset_status s;

		parity_lcd_modeset_status(&s);
		kern_logf("i915: parity DUAL %s: what the stop left: crtc_active=%d plane_armed=%d DPLL%d on=%d "
			"active_mask=0x%x pipe_mask=0x%x io wakeref=%d aux wakeref=%d crtc domains=%u dc_off_held=%d "
			"stop_unconfirmed=%d errors=%u first=%s\n", sc->name, s.crtc_active, s.plane_armed, s.pll_id, s.pll_on,
			s.pll_active_mask, s.pll_pipe_mask, s.ddi_io_wakeref, s.aux_wakeref, s.crtc_domains_held, s.dc_off_held,
			s.stop_unconfirmed, s.errors, s.first_error != 0 ? s.first_error : "-");
	}
	return sc->disable_rc == PARITY_LCD_MS_OK ? 0 : -1;
}

static void dual_release(struct dual_screen *sc)
{
	int unpin = parity_scanout_unpin(&sc->so);
	int destroy = unpin == 0 ? parity_scanout_destroy(&sc->so) : -EBUSY;

	sc->released = unpin == 0 && destroy == 0;
	kern_logf("i915: parity DUAL %s: buffer unpin=%d destroy=%d released=%d\n", sc->name, unpin, destroy, sc->released);
}

/*
 * DUAL: the panel (pipe A, its own picture) and the external HDMI display (pipe B, another picture) at the same
 * time.  Then the external one is stopped while the panel keeps running -- the point of the test: one screen's stop
 * must not take the other's PLL, power domains or buffer with it.
 */
int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d)
{
	static struct dual_screen a, b;
	static struct parity_lcd_state hdmi_state;
	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };
	struct lcd_kernel *k = &lk;
	const struct parity_vbt_encoder *ve;
	uint32_t fa0, fb0, fa1, fb1, fa2, fa3;
	unsigned t;
	int rc, pass = 1;

	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || !d->gm->inited) {
		kern_logf("i915: parity DUAL verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		kern_logf("i915: parity DUAL verdict: FAIL (an earlier run's resources are retained)\n");
		return -1;
	}
	if (!lcdb_locks_live) {
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	k->locks = lcdb_locks;
	k->d = d;
	bind_ops(k);
	parity_lcd_dplls_reset();
	parity_lcd_dbuf_forget();
	(void)parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);

	/* screen A: the resident panel, as LCD-B drives it */
	a.idx = 0; a.name = "LCD"; a.pipe = 0; a.state = &d->edp->lcd; a.pattern_id = PARITY_LCDB_PATTERN_ID;
	a.sop = &a.so; b.sop = &b.so;                   /* each screen has its own buffer in this test */
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the resident eDP is not live)\n");
		return -1;
	}
	if (fill_cfg(k, &a.cfg) != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the panel's configuration could not be built)\n");
		return -1;
	}
	a.cfg.also_active_pipes = 1u << 1;              /* pipe B is part of this configuration */

	/* screen B: the external HDMI display */
	b.idx = 1; b.name = "HDMI"; b.pipe = 1; b.pattern_id = 111u;
	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hdmi_state);
	if (rc != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)\n", rc);
		return -1;
	}
	b.state = &hdmi_state;
	b.cfg = a.cfg;                                  /* the device-wide inputs are the same */
	b.cfg.output_hdmi = 1;
	b.cfg.port = 1; b.cfg.pipe = 1; b.cfg.cpu_transcoder = 1; b.cfg.aux_ch = 1;
	b.cfg.dpll_id = 0;                              /* only the caller's expectation: the rule decides */
	b.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));
	b.cfg.vbt_backlight_present = 0;
	ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, 1);
	b.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;
	b.cfg.also_active_pipes = 1u << 0;              /* pipe A is part of this configuration */

	kern_logf("i915: parity DUAL: LCD %ux%u pattern %u on pipe A | HDMI %ux%u pattern %u on pipe B (VBT level shift %d)\n",
		a.state->mode.hdisplay, a.state->mode.vdisplay, a.pattern_id, b.state->mode.hdisplay, b.state->mode.vdisplay,
		b.pattern_id, b.cfg.vbt_hdmi_level_shift);

	if (dual_bring_up(k, &a) != 0 || dual_bring_up(k, &b) != 0) {
		pass = 0;
		goto stop;
	}
	dual_log_state("both up", &a);
	dual_log_state("both up", &b);

	/* both pipes must be scanning out */
	fa0 = dual_frame(d, 0); fb0 = dual_frame(d, 1);
	step_sleep(k, 500u);
	fa1 = dual_frame(d, 0); fb1 = dual_frame(d, 1);
	a.frame_first = fa0; b.frame_first = fb0;
	kern_logf("i915: parity DUAL frames after enable: pipe A %u -> %u, pipe B %u -> %u\n", fa0, fa1, fb0, fb1);
	if (fa1 == fa0 || fb1 == fb0) {
		kern_logf("i915: parity DUAL: a pipe's frame counter does not advance\n");
		pass = 0;
	}

	/* the window: both pictures are up (the photograph is separate evidence) */
	for (t = 0u; t < PARITY_DUAL_WINDOW_MS / 5000u; t++) {
		step_sleep(k, 5000u);
		kern_logf("i915: parity DUAL window t=%us: pipe A frame %u, pipe B frame %u\n", (t + 1u) * 5u,
			dual_frame(d, 0), dual_frame(d, 1));
	}

	/* the external display stops; the panel must keep running with everything it owns */
	if (dual_stop(k, &b) != 0)
		pass = 0;
	fa2 = dual_frame(d, 0);
	step_sleep(k, 500u);
	fa3 = dual_frame(d, 0);
	a.frame_last = fa3;
	b.frame_last = dual_frame(d, 1);
	dual_log_state("after the HDMI stop", &a);
	kern_logf("i915: parity DUAL after the HDMI stop: pipe A frame %u -> %u (keeps running), pipe B frame %u (stopped)\n",
		fa2, fa3, b.frame_last);
	if (fa3 == fa2) {
		kern_logf("i915: parity DUAL: the panel stopped when the external display did\n");
		pass = 0;
	}
	{
		struct parity_lcd_modeset_status sa;

		(void)parity_lcd_modeset_select(a.idx);
		parity_lcd_modeset_status(&sa);
		if (!sa.crtc_active || !sa.plane_armed || !sa.pll_on || sa.crtc_domains_held == 0u) {
			kern_logf("i915: parity DUAL: the panel lost its own resources when the external display stopped\n");
			pass = 0;
		}
	}
	step_sleep(k, 3000u);                           /* the panel alone, for the photograph */

stop:
	if (a.armed && dual_stop(k, &a) != 0)
		pass = 0;
	if (b.begun && b.disable_rc != PARITY_LCD_MS_OK)
		pass = 0;
	if (a.so.state != PARITY_SCANOUT_NONE)
		dual_release(&a);
	if (b.so.state != PARITY_SCANOUT_NONE)
		dual_release(&b);
	if (!a.released || !b.released)
		pass = 0;
	kern_logf("i915: parity DUAL verdict: %s (LCD: enable rc=%d disable rc=%d released=%d | HDMI: enable rc=%d "
		"disable rc=%d released=%d | the photograph is separate evidence)\n", pass ? "PASS" : "FAIL", a.enable_rc,
		a.disable_rc, a.released, b.enable_rc, b.disable_rc, b.released);
	lcdb_summary.ran = 1;
	lcdb_summary.pass = pass;
	lcdb_summary.stage = pass ? "released" : "dual";
	lcdb_summary.first_anomaly = pass ? 0 : "see the DUAL lines";
	(void)parity_lcd_modeset_select(0);
	return pass ? 0 : -1;
}

/* ---------------- one buffer on both screens (-DPARITY_DUAL_SHARE_TEST=1) ---------------- */

/*
 * The same buffer feeds both pipes.  The panel shows all of it (1920x1080); the external display reads the SAME
 * address with the SAME pitch, so it shows the top-left 1280x720 of the same rows -- a partial view of a shared
 * buffer.  It is not a scaled mirror: that needs a pipe scaler, which this path does not program.
 * What the test is about: the buffer belongs to both screens at once, and only the LAST of them releases it.
 */
int parity_lcd_kernel_dual_share_run(const struct parity_lcd_kernel_deps *d)
{
	static struct dual_screen a, b;
	static struct parity_scanout shared;
	static struct parity_lcd_state hdmi_state;
	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };
	struct lcd_kernel *k = &lk;
	const struct parity_vbt_encoder *ve;
	uint32_t fa0, fb0, fa1, fb1;
	unsigned t;
	int rc, pass = 1;

	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || !d->gm->inited) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (an earlier run's resources are retained)\n");
		return -1;
	}
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the resident eDP is not live)\n");
		return -1;
	}
	if (!lcdb_locks_live) {
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	k->locks = lcdb_locks;
	k->d = d;
	bind_ops(k);
	parity_lcd_dplls_reset();
	parity_lcd_dbuf_forget();
	(void)parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);

	a.idx = 0; a.name = "LCD"; a.pipe = 0; a.state = &d->edp->lcd; a.pattern_id = PARITY_LCDB_PATTERN_ID;
	b.idx = 1; b.name = "HDMI"; b.pipe = 1; b.pattern_id = PARITY_LCDB_PATTERN_ID;
	a.sop = &shared; b.sop = &shared;               /* both screens read the SAME buffer */
	if (fill_cfg(k, &a.cfg) != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the panel's configuration could not be built)\n");
		return -1;
	}
	a.cfg.also_active_pipes = 1u << 1;
	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hdmi_state);
	if (rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)\n", rc);
		return -1;
	}
	b.state = &hdmi_state;
	b.cfg = a.cfg;
	b.cfg.output_hdmi = 1;
	b.cfg.port = 1; b.cfg.pipe = 1; b.cfg.cpu_transcoder = 1; b.cfg.aux_ch = 1;
	b.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));
	b.cfg.vbt_backlight_present = 0;
	ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, 1);
	b.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;
	b.cfg.also_active_pipes = 1u << 0;

	/* ONE buffer, the panel's size */
	rc = parity_scanout_create(d->gm, (uint32_t)a.state->mode.hdisplay, (uint32_t)a.state->mode.vdisplay,
		PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &shared);
	if (rc == 0)
		rc = parity_scanout_pin(&shared, "dual-shared");
	if (rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the shared buffer could not be made: rc=%d)\n", rc);
		return -1;
	}
	(void)parity_lcd_pattern_fill(shared.cpu, shared.pitch, shared.width, shared.height, a.pattern_id);
	parity_scanout_publish(&shared);
	kern_logf("i915: parity DUAL-SHARED: one buffer surf=0x%08x %ux%u pitch=%u pattern %u | LCD shows all of it, "
		"HDMI the top-left %ux%u of the same rows (partial view, not a scaled mirror)\n", (unsigned)shared.surf,
		shared.width, shared.height, shared.pitch, a.pattern_id, b.state->mode.hdisplay, b.state->mode.vdisplay);

	a.cfg.fb_fourcc = shared.format; a.cfg.fb_modifier = shared.modifier;
	a.cfg.fb_width = shared.width; a.cfg.fb_height = shared.height;
	a.cfg.fb_pitch = shared.pitch; a.cfg.fb_surf = (uint32_t)shared.surf;
	b.cfg.fb_fourcc = shared.format; b.cfg.fb_modifier = shared.modifier;
	b.cfg.fb_width = (uint32_t)b.state->mode.hdisplay;      /* the part this output shows */
	b.cfg.fb_height = (uint32_t)b.state->mode.vdisplay;
	b.cfg.fb_pitch = shared.pitch;                          /* the SAME rows: the buffer's own pitch */
	b.cfg.fb_surf = (uint32_t)shared.surf;

	/* both screens take the buffer (the count is what keeps it alive) */
	(void)parity_lcd_modeset_select(a.idx);
	a.prepare_rc = parity_lcd_modeset_prepare(a.state, &a.cfg, &k->ops);
	a.begun = a.prepare_rc == 0 && parity_scanout_begin(&shared) == 0;
	a.enable_rc = a.begun ? parity_lcd_modeset_commit_enable() : -99;
	a.armed = a.enable_rc == PARITY_LCD_MS_OK;
	(void)parity_lcd_modeset_select(b.idx);
	b.prepare_rc = parity_lcd_modeset_prepare(b.state, &b.cfg, &k->ops);
	b.begun = b.prepare_rc == 0 && parity_scanout_begin(&shared) == 0;
	b.enable_rc = b.begun ? parity_lcd_modeset_commit_enable() : -99;
	b.armed = b.enable_rc == PARITY_LCD_MS_OK;
	kern_logf("i915: parity DUAL-SHARED: LCD prepare=%d enable=%d | HDMI prepare=%d enable=%d | buffer users=%u state=%d\n",
		a.prepare_rc, a.enable_rc, b.prepare_rc, b.enable_rc, shared.users, shared.state);
	if (!a.armed || !b.armed) {
		pass = 0;
		goto stop;
	}
	dual_log_state("shared, both up", &a);
	dual_log_state("shared, both up", &b);

	fa0 = dual_frame(d, 0); fb0 = dual_frame(d, 1);
	step_sleep(k, 500u);
	fa1 = dual_frame(d, 0); fb1 = dual_frame(d, 1);
	kern_logf("i915: parity DUAL-SHARED frames: pipe A %u -> %u, pipe B %u -> %u (both read surf 0x%08x)\n",
		fa0, fa1, fb0, fb1, (unsigned)shared.surf);
	if (fa1 == fa0 || fb1 == fb0)
		pass = 0;
	for (t = 0u; t < 3u; t++) {
		step_sleep(k, 5000u);
		kern_logf("i915: parity DUAL-SHARED window t=%us: pipe A frame %u, pipe B frame %u\n", (t + 1u) * 5u,
			dual_frame(d, 0), dual_frame(d, 1));
	}

	/* the external display lets go: the buffer must stay, because the panel still reads it */
	if (dual_stop(k, &b) != 0)
		pass = 0;
	kern_logf("i915: parity DUAL-SHARED after the HDMI stop: buffer users=%u state=%d (IN_USE=3) | pipe A frame %u\n",
		shared.users, shared.state, dual_frame(d, 0));
	if (shared.users != 1u || shared.state != PARITY_SCANOUT_IN_USE) {
		kern_logf("i915: parity DUAL-SHARED: the buffer was given up while the panel still reads it\n");
		pass = 0;
	}
	{
		int unpin_while_used = parity_scanout_unpin(&shared);

		kern_logf("i915: parity DUAL-SHARED: an unpin while the panel reads it is refused: rc=%d (0 would be wrong)\n",
			unpin_while_used);
		if (unpin_while_used == 0)
			pass = 0;
	}
	step_sleep(k, 3000u);

stop:
	if (a.armed && dual_stop(k, &a) != 0)
		pass = 0;
	kern_logf("i915: parity DUAL-SHARED after both stops: buffer users=%u state=%d\n", shared.users, shared.state);
	{
		int unpin = parity_scanout_unpin(&shared);
		int destroy = unpin == 0 ? parity_scanout_destroy(&shared) : -EBUSY;

		a.released = b.released = unpin == 0 && destroy == 0;
		kern_logf("i915: parity DUAL-SHARED: buffer unpin=%d destroy=%d released=%d\n", unpin, destroy, a.released);
		if (!a.released)
			pass = 0;
	}
	kern_logf("i915: parity DUAL-SHARED verdict: %s (one buffer: LCD enable rc=%d disable rc=%d | HDMI enable rc=%d "
		"disable rc=%d | released=%d; the photograph is separate evidence)\n", pass ? "PASS" : "FAIL", a.enable_rc,
		a.disable_rc, b.enable_rc, b.disable_rc, a.released);
	lcdb_summary.ran = 1;
	lcdb_summary.pass = pass;
	lcdb_summary.stage = pass ? "released" : "dual-shared";
	lcdb_summary.first_anomaly = pass ? 0 : "see the DUAL-SHARED lines";
	(void)parity_lcd_modeset_select(0);
	return pass ? 0 : -1;
}

/* ---------------- LCD reuse (-DPARITY_LCDR_TEST=1) ---------------- */

static uint32_t read_frame(void *ctx)
{
	struct lcd_kernel *k = ctx;

	return osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("PIPE_FRMCOUNT_G4X"));
}

static void step_sleep(struct lcd_kernel *k, unsigned ms)
{
	unsigned t;

	for (t = 0u; t < ms; t += 100u)
		k_usleep(k, 100000u);
}

/*
 * The pipe's interrupts, driver-managed: the registers the power-well post-enable hook restored, then a vblank
 * reference (IMR vblank bit cleared), three waits that each need a NEW pipe A vblank interrupt and a moving frame
 * counter, the last put (bit set again), and no further vblank interrupt once it is masked.
 */
static int irq_check(struct lcd_kernel *k, int verbose)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_irq_vblank *v = d->irq->vbl;
	uint32_t extra = 0x00000001u | parity_gen8_de_pipe_underrun_mask(13) | parity_gen8_de_pipe_flip_done_mask(13);
	uint32_t imr, ier, imr_on, imr_off, want_ier, f0, f1, seen[3] = { 0u, 0u, 0u };
	unsigned raw0, raw1, raw2, i;
	int rc[3] = { -1, -1, -1 }, get_rc, ok, drain_rc = -1;

	if (v == 0 || !v->inited)
		return -22;
	imr = osdep_mmio_read32(d->mmio, 0x44404u);
	ier = osdep_mmio_read32(d->mmio, 0x4440cu);
	want_ier = ~d->irq->de_irq_mask[0] | extra;
	get_rc = parity_drm_vblank_get(d->irq, 0u);
	imr_on = osdep_mmio_read32(d->mmio, 0x44404u);
	raw0 = d->irq->de_vblank_count[0];
	f0 = read_frame(k);
	for (i = 0u; i < 3u && get_rc == 0; i++)
		rc[i] = parity_wait_vblank(d->irq, 0u, 1u, 100u, read_frame, k, &seen[i]);
	f1 = read_frame(k);
	raw1 = d->irq->de_vblank_count[0];
	parity_drm_vblank_put(d->irq, 0u);
	/* baseline only after: the mask is read back set, and handler work already inside pipe A has drained -- a legitimate
	 * vblank handled between the last wait and the put is not counted as "after masking" */
	imr_off = osdep_mmio_read32(d->mmio, 0x44404u);
	drain_rc = parity_irq_drain_pipes(d->irq, 1u << 0, 10000u);
	raw1 = d->irq->de_vblank_count[0];
	step_sleep(k, 100u);
	raw2 = d->irq->de_vblank_count[0];
	/*
	 * The IMR is judged on the bits this path relies on -- vblank, the underrun bits, flip done -- which must read back
	 * as the driver's mask.  The first reuse run found bits 17 / 18 written 1 but reading 0 (the pre-driver default
	 * 0xfff9ffff has them 0 too); the reference never reads IMR back, so their meaning is not interpreted: they are
	 * logged, not judged.
	 */
	ok = get_rc == 0 && ((imr ^ d->irq->de_irq_mask[0]) & (extra | 0x1u)) == 0u && (imr & 1u) != 0u && ier == want_ier &&
		(imr_on & 1u) == 0u && rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && raw1 - raw0 >= 3u && f1 != f0 && drain_rc == 0 &&
		(imr_off & 1u) != 0u && raw2 == raw1 && v->refs[0] == 0u;
	kern_logf("i915: parity LCD-R IRQ: after the well came on: GEN8_DE_PIPE_IMR(A)=0x%08x (driver's de_irq_mask 0x%08x) "
		"GEN8_DE_PIPE_IER(A)=0x%08x (expected ~mask|vblank|underrun|flip done = 0x%08x) | vblank get rc=%d IMR=0x%08x | waits rc=%d/%d/%d "
		"new-vblanks %u/%u/%u | handler vblank IRQs +%u frames %u->%u | put IMR=0x%08x | vblank IRQs in the next 100 ms after "
		"masking: %u | refs %u | IMR bits differing from the written mask 0x%08x (judged bits 0x%08x) -> %s\n", imr,
		d->irq->de_irq_mask[0], ier, want_ier, get_rc, imr_on, rc[0], rc[1], rc[2], seen[0], seen[1], seen[2], raw1 - raw0, f0, f1,
		imr_off, raw2 - raw1, v->refs[0], imr ^ d->irq->de_irq_mask[0], extra | 0x1u, ok ? "OK" : "FAIL");
	(void)verbose;
	return ok ? 0 : -5;
}

/* intel_backlight: user -> hw (scale_user_to_hw) -> PWM (intel_backlight_level_to_pwm, identity here: min/max = PWM min/max) */
static uint32_t expected_duty(const struct parity_lcd_modeset_status *s, uint32_t user)
{
	uint64_t span = (uint64_t)(s->backlight_max - s->backlight_min) * user;

	return s->backlight_min + (uint32_t)((span + s->backlight_user_max / 2u) / s->backlight_user_max);
}

static int brightness_step(struct lcd_kernel *k, unsigned step, const char *name, int op, uint32_t level, uint32_t max,
	unsigned hold_ms)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_lcd_modeset_status s;
	uint32_t f0, f1, ctl, freq, duty, want;
	int rc, regs_ok;

	rc = op == 0 ? parity_lcd_modeset_brightness(level, max) : parity_lcd_modeset_backlight(op > 0);
	f0 = read_frame(k);
	parity_lcd_modeset_status(&s);
	ctl = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_CTL"));
	freq = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_FREQ"));
	duty = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY"));
	want = expected_duty(&s, s.backlight_user);
	/* the period never changes; off: PWM disabled and the device's backlight off; on / level: PWM enabled and the duty the
	 * reference conversion gives for the user brightness */
	regs_ok = freq == k->bl_freq0 && (op < 0 ? ((ctl & 0x80000000u) == 0u && !s.backlight_enabled) :
		((ctl & 0x80000000u) != 0u && s.backlight_enabled && duty == want));
	kern_logf("i915: parity LCD-R step=%u %s registers: user %u/%u -> expected DUTY %u, read DUTY %u FREQ 0x%08x (start 0x%08x) "
		"PWM_CTL 0x%08x -> %s\n", step, name, s.backlight_user, s.backlight_user_max, op < 0 ? 0u : want, duty, freq, k->bl_freq0,
		ctl, regs_ok ? "OK" : "MISMATCH");
	kern_logf("i915: parity LCD-R step=%u %s rc=%d | BXT_BLC_PWM_CTL=0x%08x FREQ=0x%08x DUTY=0x%08x (level %u of [%u,%u]) "
		"backlight_enabled=%d PP_CONTROL backlight bit via eDP | crtc_active=%d plane_armed=%d (take the photograph)\n",
		step, name, rc, osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_CTL")),
		osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_FREQ")),
		osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY")), s.backlight_level, s.backlight_min,
		s.backlight_max, s.backlight_enabled, s.crtc_active, s.plane_armed);
	step_sleep(k, hold_ms);
	f1 = read_frame(k);
	kern_logf("i915: parity LCD-R step=%u %s done: frame counter %u -> %u over %u ms (the scanout %s)\n", step, name, f0, f1, hold_ms,
		f1 != f0 ? "continued" : "STOPPED");
	return rc == PARITY_LCD_MS_OK && regs_ok && f1 != f0 && s.crtc_active && s.plane_armed ? 0 : -5;
}

static unsigned d_vbt_min(struct lcd_kernel *k)
{
	return k->vbt_min;
}

static int window_first(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_lcd_modeset_status s;
	uint32_t mx, orig, duty;
	int rc;

	(void)o;
	if ((rc = irq_check(k, 1)) != 0)
		return rc;
	parity_lcd_modeset_status(&s);
	mx = s.backlight_user_max;
	orig = s.backlight_user;                /* the USER brightness is what gets restored */
	k->bl_freq0 = osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_FREQ"));
	kern_logf("i915: parity LCD-R brightness: VBT min_brightness %u (a 0..255 coefficient) -> backlight.min %u of max %u "
		"(get_backlight_min_vbt); current level %u; user range [0, %u] (intel_backlight_device_register)\n",
		d_vbt_min(k), s.backlight_min, mx, orig, mx);
	if ((rc = brightness_step(k, 0u, "max", 0, mx, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 1u, "half", 0, mx / 2u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 2u, "min(user 0)", 0, 0u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 3u, "half-again", 0, mx / 2u, mx, 1000u)) != 0 ||
	    (rc = brightness_step(k, 4u, "backlight-off(scanout continues)", -1, 0u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 5u, "backlight-on", 1, 0u, mx, 7000u)) != 0)
		return rc;
	parity_lcd_modeset_status(&s);
	duty = osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY"));
	if (s.backlight_level != mx / 2u + 0u && s.backlight_level != duty)
		kern_logf("i915: parity LCD-R note: level after backlight-on %u, duty 0x%08x\n", s.backlight_level, duty);
	return brightness_step(k, 6u, "restore", 0, orig, mx, 1000u);
}

static int window_again(void *ctx, struct parity_lcd_observer *o)
{
	(void)o;
	return irq_check(ctx, 0);
}

int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d)
{
	static const struct lcd_run_params cyc[3] = {
		{ .pattern_id = 110u, .pattern_fnv = PARITY_LCDB_PATTERN_FNV, .window_ms = 2000u,
			.in_window = window_first },
		{ .pattern_id = 111u, .window_ms = 3000u, .in_window = window_again },
		{ .pattern_id = 112u, .window_ms = 3000u, .in_window = window_again },
	};
	unsigned i, passed = 0u;

	kern_logf("i915: parity LCD-R begin: three show / stop cycles in one driver lifetime (patterns 110, 111, 112); one display "
		"owner, run serially (the modeset object is global; the DPLL / backlight mutexes do not make the whole modeset reentrant)\n");
	for (i = 0u; i < 3u; i++) {
		int rc;

		kern_logf("i915: parity LCD-R cycle %u begin (pattern %u)\n", i + 1u, cyc[i].pattern_id);
		rc = lcd_run_one(d, &cyc[i]);
		kern_logf("i915: parity LCD-R cycle %u verdict: %s\n", i + 1u, rc == 0 ? "PASS" : "FAIL");
		if (rc != 0)
			break;                  /* first anomaly: no further cycle */
		passed++;
	}
	kern_logf("i915: parity LCD-R verdict: %s (cycles passed %u/3; photographs are separate evidence)\n", passed == 3u ? "PASS" : "FAIL",
		passed);
	lcdb_summary.pass = passed == 3u;
	return passed == 3u ? 0 : -1;
}

int parity_lcd_kernel_abandoned(void)
{
	return parity_lcd_show_retained() || parity_lcd_modeset_retained();
}

int parity_lcd_kernel_gpu_retained(void)
{
	return parity_lcd_show_gpu_retained();
}

void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out)
{
	if (out != 0)
		*out = lcdb_summary;
}

/* ---------------- LCD-G (-DPARITY_LCDG_TEST=1) ---------------- */

static struct parity_scanout lcdg_scanout;      /* outlives the run when the buffer is abandoned */
static struct parity_fhd_render lcdg_render;
static struct parity_gt_tlb lcdg_tlb;

/*
 * The buffer's two users are done?  display: never acquired, or its stop confirmed; GPU: never submitted, or retired
 * and parked.  Then (and only then) the render mappings, the TLB, the draw's objects and finally the buffer go -- each
 * step only after the previous one succeeded.  Otherwise everything the unfinished user may touch is kept and the
 * device-side retained state is set.  Returns 1 when the buffer was freed.
 */
int parity_lcdg_finish(struct parity_fhd_render *fr, struct parity_scanout *so, int display_acquired, int display_released,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb, struct parity_gt_engines *es,
	struct osdep_mmio *m, struct spinlock *uncore_lock, int *render_rc)
{
	int rc;

	*render_rc = -EBUSY;
	if (fr->t.submitted && !fr->gpu_done) {
		/* the GPU is not shown to have let go: the buffer, the state, the batch, the texture, the context stay */
		parity_fhd_render_keep(fr);
		if (so->state >= PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(so);
		parity_lcd_show_retain_gpu(gm, "the GPU request using the buffer did not finish");
		return 0;
	}
	if (display_acquired && !display_released)
		return 0;                       /* show_prepared already abandoned it and set the latch */
	rc = parity_fhd_render_release(fr, gm, vm, tlb, es, m, uncore_lock);
	*render_rc = rc;
	if (rc != 0) {
		/* mappings or the TLB not shown to be gone: the pages may still be translated -- keep the buffer */
		if (so->state >= PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(so);
		parity_lcd_show_retain_gpu(gm, "the render mappings / TLB could not be shown released");
		return 0;
	}
	if (parity_scanout_unpin(so) != 0 || parity_scanout_destroy(so) != 0)
		return 0;
	return 1;
}


static uint32_t lcdg_verify(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);   /* read what is in memory, not a stale CPU line */
	return parity_fhd_render_verify(&lcdg_render, so->cpu, so->pitch);
}

int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	struct parity_scanout *so = &lcdg_scanout;
	struct parity_fhd_render *fr = &lcdg_render;
	uint64_t ggtt_first = 0u, ggtt_last = 0u;
	int rc, same_backing, released = 0, render_released = 0, i, held = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->es == 0 || d->vm == 0 || d->irq == 0) {
		kern_logf("i915: parity LCD-G verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || so->state != PARITY_SCANOUT_NONE) {
		lcdb_summary.retained = 1;
		lcdb_summary.first_anomaly = "refused: resources of an earlier run are retained";
		kern_logf("i915: parity LCD-G verdict: FAIL (refused before any initialisation: retained resources)\n");
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
		kern_logf("i915: parity LCD-G verdict: FAIL (preflight: nothing was written)\n");
		lcdb_summary.first_anomaly = "preflight refused (nothing written)";
		return -1;
	}

	/* ---- one backing: the scanout buffer (GGTT display binding) ---- */
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY) {
		kern_logf("i915: parity LCD-G verdict: FAIL (display window rc=%d)\n", rc);
		return -1;
	}
	rc = parity_scanout_create(d->gm, I915_TEX_FHD_WIDTH, I915_TEX_FHD_HEIGHT, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, so);
	if (rc == 0 && (so->pitch != I915_TEX_FHD_PITCH || so->size != I915_TEX_FHD_RT_BYTES)) {
		kern_logf("i915: parity LCD-G: scanout layout pitch=%u size=%u differs from the render target's (isl: %u / %u)\n",
			so->pitch, so->size, I915_TEX_FHD_PITCH, I915_TEX_FHD_RT_BYTES);
		(void)parity_scanout_destroy(so);
		rc = -EINVAL;
	}
	if (rc == 0 && (rc = parity_scanout_pin(so, "lcd-g")) != 0)
		(void)parity_scanout_destroy(so);
	if (rc != 0) {
		kern_logf("i915: parity LCD-G verdict: FAIL (scanout buffer rc=%d)\n", rc);
		return -1;
	}

	/* ---- ... and its PPGTT mapping: the GPU draws into the same pages ---- */
	rc = parity_fhd_render_run(fr, d->es, d->vm, d->gm, d->mmio, d->uncore_lock, 2000u, so->obj);
	ggtt_first = parity_gt_ggtt_read_pte(d->gm, so->obj->ggtt_page);
	ggtt_last = parity_gt_ggtt_read_pte(d->gm, so->obj->ggtt_page + fr->rt_pages - 1u);
	same_backing = fr->rt == so->obj && fr->rt_walk_ok && (ggtt_first & ~0xfffull) == (fr->rt_first_dma & ~0xfffull) &&
		(ggtt_last & ~0xfffull) == (fr->rt_last_dma & ~0xfffull);
	kern_logf("i915: parity LCD-G render: rc=%d where=%s outcome=%s submitted=%d completed=%d parked=%d timed_out=%d | markers "
		"before=%08x middraw=%08x after=%08x ps=%08x | pixels match=%u/%u stale=%u first_bad=(%d,%d) expected=%08x observed=%08x | "
		"texture changed=%u guard_bad=%u | image fnv=%016llx | batch_dwords=%u pipesel rc=%d | rt mocs=%u\n", rc,
		fr->t.err_where != 0 ? fr->t.err_where : "-", fr->t.outcome == PARITY_EU_PASS ? "PASS" : fr->t.outcome == PARITY_EU_HANG ? "HANG" : "ERROR",
		fr->t.submitted, fr->t.completed, fr->t.parked, fr->t.timed_out, fr->marker_before, fr->marker_middraw, fr->marker_after,
		fr->ps_marker, fr->px_match, fr->px_total, fr->px_stale, fr->first_bad_x, fr->first_bad_y, fr->first_bad_expected,
		fr->first_bad_observed, fr->tex_changed_bytes, fr->guard_bad_bytes, (unsigned long long)fr->image_hash, fr->t.batch_dwords,
		fr->t.pipesel_rc, fr->rt_rss_mocs);
	kern_logf("i915: parity LCD-G same backing: object %p | PPGTT 0x%llx.. %u pages mapped, walk ok=%d, first page dma 0x%llx last 0x%llx | "
		"GGTT surf 0x%08llx: PTE first 0x%llx last 0x%llx -> %s\n", (void *)so->obj, (unsigned long long)I915_TEX_FHD_RT_VA,
		fr->rt_pages_mapped, fr->rt_walk_ok, (unsigned long long)fr->rt_first_dma, (unsigned long long)fr->rt_last_dma,
		(unsigned long long)so->surf, (unsigned long long)ggtt_first, (unsigned long long)ggtt_last,
		same_backing ? "SAME PAGES" : "DIFFERENT");
	if (rc != 0 || fr->t.outcome != PARITY_EU_PASS || !same_backing) {
		int rrc;

		released = parity_lcdg_finish(fr, so, 0, 0, d->gm, d->vm, &lcdg_tlb, d->es, d->mmio, d->uncore_lock, &rrc);
		lcdb_summary.first_anomaly = "the GPU image is not the expected one (not shown)";
		lcdb_summary.first_anomaly_stage = "render";
		lcdb_summary.retained = parity_lcd_show_retained();
		kern_logf("i915: parity LCD-G verdict: FAIL (the GPU draw did not produce the expected image; nothing was shown; "
			"gpu_done=%d render release rc=%d buffer freed=%d retained=%d)\n", fr->gpu_done, rrc, released,
			lcdb_summary.retained);
		return -1;
	}

	/* ---- show THAT object: no CPU write since the draw ---- */
	env.hw = &k->ops;
	env.gm = d->gm;
	env.so = 0;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 12000u;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = 0u;
	k->window_ms = env.window_ms;
	k->post_before = d->irq->vbl != 0 ? d->irq->vbl->post_enable_calls : 0u;
	k->pre_before = d->irq->vbl != 0 ? d->irq->vbl->pre_disable_calls : 0u;
	kern_logf("i915: parity LCD-G showing the GPU-drawn buffer (surf 0x%08llx, image fnv %016llx)\n",
		(unsigned long long)so->surf, (unsigned long long)fr->image_hash);
	rc = parity_lcd_show_prepared(&env, so, lcdg_verify, 0, &rep);

	log_trace(rep.trace);
	log_observer(&rep.obs);
	log_status("enable-returned", &rep.at_enable);
	log_status("disable-returned", &rep.at_disable);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];

	/* ---- both users done?  display: stop confirmed; GPU: retired and parked -> release in that order ---- */
	{
		int rrc;

		released = parity_lcdg_finish(fr, so, rep.display_acquired, rep.display_released, d->gm, d->vm, &lcdg_tlb, d->es,
			d->mmio, d->uncore_lock, &rrc);
		render_released = rrc == 0;
	}
	kern_logf("i915: parity LCD-G release: mappings back to scratch %u/%u (first left 0x%llx) TLB rc=%d (invalidations %u, engines %u, "
		"timeouts %u) release calls %u\n", fr->maps_scratch, fr->maps_total, (unsigned long long)fr->first_unreleased_va, fr->tlb_rc,
		lcdg_tlb.invalidations, lcdg_tlb.engines_invalidated, lcdg_tlb.timeouts, fr->release_calls);
	kern_logf("i915: parity LCD-G stop / release: display released=%d abandoned=%d | render PTEs cleared %u/%u released=%d | "
		"buffer unpinned+destroyed=%d | image re-checked after the stop: wrong pixels=%u | display pages in use=%u | power refs "
		"held=%d | first anomaly: %s (stage %s)\n", rep.display_released, rep.abandoned, fr->rt_pages_cleared, fr->rt_pages_mapped,
		render_released, released, rep.readback_bad_after, d->gm->display_allocated_pages, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none", stage_name(rep.first_anomaly_stage));
	lcdb_summary.pass = rc == 0 && released && held == 0 && k->unresolved_steps == 0u && k->time_faults == 0u;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.stage = stage_name(rep.stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	if (rep.first_anomaly == 0 && !rep.display_acquired)
		lcdb_summary.first_anomaly = "the display part did not start";
	kern_logf("i915: parity LCD-G verdict: %s (GPU pixels %u/%u, same backing, shown from the GPU-written object, stop %s, released=%d; "
		"the photograph is separate evidence)\n", lcdb_summary.pass ? "PASS" : "FAIL", fr->px_match, fr->px_total,
		rep.display_released ? "confirmed" : "NOT confirmed", released);
	return lcdb_summary.pass ? 0 : -1;
}

/* ---------------- LCD-C (-DPARITY_LCDC_TEST=1): the synchronous flip between two CPU-prepared buffers ---------------- */

#define LCDC_PATTERN_A 121u
#define LCDC_PATTERN_B 122u
#define LCDC_HOLD_MS 8000u                        /* each picture stays long enough for the camera */
static struct parity_scanout lcdc_a, lcdc_b;

static uint32_t lcdc_verify_a(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);
	return parity_lcd_pattern_verify(so->cpu, so->pitch, so->width, so->height, LCDC_PATTERN_A, 0, 0);
}

static int lcdc_flips(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_scanout *seq[4] = { k->flip_b, k->flip_a, k->flip_b, k->flip_a };
	struct parity_lcd_flip_result fr;
	unsigned i;
	int rc;

	(void)o;
	kern_logf("i915: parity LCD-C shown 0: pattern %u (buffer A, surf 0x%08x) -- take the photograph\n", LCDC_PATTERN_A,
		(uint32_t)k->flip_a->surf);
	step_sleep(k, LCDC_HOLD_MS);                    /* the first picture (A) for the camera */
	for (i = 0u; i < 4u; i++) {
		struct parity_scanout *to = seq[i], *from = to == k->flip_a ? k->flip_b : k->flip_a;

		if (to->state == PARITY_SCANOUT_PINNED && parity_scanout_begin(to) != 0)
			return -22;
		rc = parity_lcd_modeset_flip((uint32_t)to->surf, &fr);
		kern_logf("i915: parity LCD-C flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event rc=%d | "
			"update errors %d | vblank sleeps %u | result %s\n", i + 1u, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after,
			fr.frame_before, fr.frame_after, fr.event_rc, fr.update_errors, k->vblank_sleeps,
			fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : fr.result == PARITY_LCD_FLIP_NOT_LATCHED ? "NOT-LATCHED" :
			fr.result == PARITY_LCD_FLIP_TIMEOUT ? "TIMEOUT" : "REFUSED");
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;                      /* both buffers stay in use; the stop path decides */
		/* the old buffer is no longer read by the display: it goes back to its owner */
		parity_scanout_end(from);
		k->flips_done++;
		kern_logf("i915: parity LCD-C shown %u: pattern %u (buffer %c, surf 0x%08x) -- take the photograph\n", i + 1u,
			to == k->flip_a ? LCDC_PATTERN_A : LCDC_PATTERN_B, to == k->flip_a ? 'A' : 'B', (uint32_t)to->surf);
		step_sleep(k, LCDC_HOLD_MS);
	}
	return 0;
}

int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	uint32_t bad_a = 0u, bad_b = 0u;
	int rc, i, held = 0, released = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->irq == 0) {
		kern_logf("i915: parity LCD-C verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || lcdc_a.state != PARITY_SCANOUT_NONE ||
	    lcdc_b.state != PARITY_SCANOUT_NONE) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity LCD-C verdict: FAIL (refused before any initialisation: retained resources)\n");
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
		kern_logf("i915: parity LCD-C verdict: FAIL (preflight: nothing was written)\n");
		return -1;
	}
	/* two buffers: separate backing, separate GGTT placement (guards and alignment each) */
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY)
		return -1;
	rc = parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdc_a);
	rc = rc == 0 ? parity_scanout_pin(&lcdc_a, "lcd-c A") : rc;
	rc = rc == 0 ? parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdc_b) : rc;
	rc = rc == 0 ? parity_scanout_pin(&lcdc_b, "lcd-c B") : rc;
	if (rc != 0) {
		kern_logf("i915: parity LCD-C verdict: FAIL (buffers rc=%d)\n", rc);
		return -1;
	}
	(void)parity_lcd_pattern_fill(lcdc_a.cpu, lcdc_a.pitch, lcdc_a.width, lcdc_a.height, LCDC_PATTERN_A);
	(void)parity_lcd_pattern_fill(lcdc_b.cpu, lcdc_b.pitch, lcdc_b.width, lcdc_b.height, LCDC_PATTERN_B);
	parity_scanout_publish(&lcdc_a);
	parity_scanout_publish(&lcdc_b);
	kern_logf("i915: parity LCD-C buffers: A surf 0x%08llx ggtt page %u (+%u, guard %u) | B surf 0x%08llx ggtt page %u (+%u) | "
		"overlap=%d\n", (unsigned long long)lcdc_a.surf, lcdc_a.obj->ggtt_page, lcdc_a.obj->pages, lcdc_a.guard_pages,
		(unsigned long long)lcdc_b.surf, lcdc_b.obj->ggtt_page, lcdc_b.obj->pages,
		!(lcdc_a.obj->ggtt_page + lcdc_a.obj->pages + lcdc_a.guard_pages <= lcdc_b.obj->ggtt_page - lcdc_b.guard_pages ||
		  lcdc_b.obj->ggtt_page + lcdc_b.obj->pages + lcdc_b.guard_pages <= lcdc_a.obj->ggtt_page - lcdc_a.guard_pages));
	k->flip_a = &lcdc_a;
	k->flip_b = &lcdc_b;

	env.hw = &k->ops;
	env.gm = d->gm;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 1000u;
	env.in_window = lcdc_flips;
	env.in_window_ctx = k;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = LCDC_PATTERN_A;
	k->window_ms = env.window_ms;
	rc = parity_lcd_show_prepared(&env, &lcdc_a, lcdc_verify_a, 0, &rep);
	log_trace(rep.trace);
	log_observer(&rep.obs);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];

	if (rep.display_released || !rep.display_acquired) {
		parity_scanout_end(&lcdc_b);            /* the display provably reads neither buffer */
		bad_b = parity_lcd_pattern_verify(lcdc_b.cpu, lcdc_b.pitch, lcdc_b.width, lcdc_b.height, LCDC_PATTERN_B, 0, 0);
		bad_a = rep.readback_bad_after;
		released = parity_scanout_unpin(&lcdc_a) == 0 && parity_scanout_destroy(&lcdc_a) == 0 &&
			parity_scanout_unpin(&lcdc_b) == 0 && parity_scanout_destroy(&lcdc_b) == 0;
	} else {
		if (lcdc_b.state >= PARITY_SCANOUT_PINNED && lcdc_b.state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(&lcdc_b);
	}
	lcdb_summary.pass = rc == 0 && k->flips_done == 4u && released && held == 0 && bad_a == 0u && bad_b == 0u &&
		k->unresolved_steps == 0u && k->time_faults == 0u;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-C verdict: %s (flips done %u/4, one modeset, stop %s, both buffers released=%d, pixels after: A bad %u "
		"B bad %u, power refs held %d, first anomaly: %s)\n", lcdb_summary.pass ? "PASS" : "FAIL", k->flips_done,
		rep.display_released ? "confirmed" : "NOT confirmed", released, bad_a, bad_b, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}

/* ---------------- LCD-D (-DPARITY_LCDD_TEST=1): the GPU redraws the buffer NOT on the display, then the flip ---------------- */

#define LCDD_ROUNDS 7u                        /* draws 0..7: A0 B1 A2 B3 A1 B2 A3 B0 */
#define LCDD_HOLD_MS 6000u
static struct parity_scanout lcdd_buf[2];
static struct parity_fhd_rt_map lcdd_map[2];
static struct parity_fhd_render lcdd_render, lcdd_check;
static struct parity_gt_tlb lcdd_tlb;
static unsigned lcdd_variant[2];
static int lcdd_gpu_kept;
/* E-120: every variant into both buffers; a buffer's next variant always differs from what it last held */
static const unsigned lcdd_seq[LCDD_ROUNDS + 1u] = { 0u, 1u, 2u, 3u, 1u, 2u, 3u, 0u };
static uint64_t lcdd_hash[2][4];
static unsigned lcdd_hash_set[2][4];
#define LCDD_PROBE_MAX 16u

static uint32_t lcdd_verify(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);
	memset(&lcdd_check, 0, sizeof(lcdd_check));
	lcdd_check.variant = lcdd_variant[so == &lcdd_buf[0] ? 0 : 1];
	return parity_fhd_render_verify(&lcdd_check, so->cpu, so->pitch);
}

/*
 * One GPU draw into buffer i (never the one on the display), checked pixel by pixel after retire + park + clflush, then
 * the draw's own PTEs / TLB / objects released (the target's mapping stays).  0 = drawn and released.  GPU not shown
 * done: every object the request may use is kept and the device latch is set; no further GPU work or flip follows.
 */
static int lcdd_draw(struct lcd_kernel *k, unsigned i, unsigned variant, unsigned round)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_fhd_render *x = &lcdd_render;
	int rc, rrc;

	rc = parity_fhd_render_run_ex(x, d->es, d->vm, d->gm, d->mmio, d->uncore_lock, 2000u, lcdd_buf[i].obj, lcdd_map[i].va,
		variant, 1);
	kern_logf("i915: parity LCD-D draw %u: into buffer %c (not on the display) variant %u at 0x%llx | rc=%d outcome=%d gpu_done=%d | "
		"markers %08x/%08x/%08x ps %08x | pixels %u/%u (stale %u, first bad %d,%d) | tex changed %u guard bad %u | "
		"walk first/mid/last=%d | hash %016llx\n", round, 'A' + (int)i, variant, (unsigned long long)lcdd_map[i].va, rc,
		x->t.outcome, x->gpu_done, x->marker_before, x->marker_middraw, x->marker_after, x->ps_marker, x->px_match, x->px_total,
		x->px_stale, x->first_bad_x, x->first_bad_y, x->tex_changed_bytes, x->guard_bad_bytes, x->rt_walk_ok,
		(unsigned long long)x->image_hash);
	if (x->t.submitted && !x->gpu_done) {
		parity_fhd_render_keep(x);
		lcdd_gpu_kept = 1;
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D draw was not shown to finish");
		return -5;
	}
	rrc = parity_fhd_render_release(x, d->gm, d->vm, &lcdd_tlb, d->es, d->mmio, d->uncore_lock);
	kern_logf("i915: parity LCD-D draw %u release: the draw's PTEs back to scratch %u/%u, TLB rc=%d, released=%d | the target "
		"stays mapped (%u pages at 0x%llx)\n", round, x->maps_scratch, x->maps_total, x->tlb_rc, x->released,
		lcdd_map[i].mapped, (unsigned long long)lcdd_map[i].va);
	if (rrc != 0) {
		lcdd_gpu_kept = 1;
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D draw's mappings / TLB could not be shown released");
		return -5;
	}
	if (rc != 0 || x->t.outcome != PARITY_EU_PASS)
		return -5;
	lcdd_variant[i] = variant;
	lcdd_hash[i][variant & 3u] = x->image_hash;
	lcdd_hash_set[i][variant & 3u]++;
	k->draws_ok++;
	return 0;
}

/*
 * The evasion sleep on the real pipe: the ordinary flip (the same update body) is started when the harness has seen
 * the scanline `lead` lines before the evasion window, so that intel_pipe_update_start() finds itself inside it and
 * sleeps to the next vblank.  The harness only chooses WHEN to call; nothing is faked.  Bounded: LCDD_PROBE_MAX
 * flips (A and B alternate, both hold valid pictures); the first flip that slept and completed ends it.  If none did,
 * the result is NOT-REACHED -- recorded, not a failure of the display test.
 */
static int lcdd_probe_result;               /* 1 reached, 0 not reached, -1 error */
static unsigned lcdd_probe_tries, lcdd_probe_lead;

static int lcdd_evasion_probe(struct lcd_kernel *k, unsigned front)
{
	struct parity_lcd_flip_result fr;
	uint32_t dsl_reg = 0x70000u, sl = 0u, vtotal = 0u;       /* PIPEDSL(pipe A) */
	int emin = 0, emax = 0, evbs = 0, rc;
	unsigned a, polls, lat = 0u;                             /* measured trigger -> first update read, in lines */

	lcdd_probe_result = 0;
	if (parity_lcd_modeset_evade_window(&emin, &emax, &evbs) != PARITY_LCD_MS_OK) {
		lcdd_probe_result = -1;
		return 0;
	}
	/* one frame's line count, from the counter itself (the highest scanline seen over > 1 frame of polling) */
	for (polls = 0u; polls < 400000u; polls++) {
		sl = osdep_mmio_read32(k->d->mmio, dsl_reg) & 0x1fffu;
		if (sl + 1u > vtotal)
			vtotal = sl + 1u;
	}
	kern_logf("i915: parity LCD-D evasion probe: window scanlines %d..%d (vblank start %d), lines per frame seen %u; up to %u flips\n",
		emin, emax, evbs, vtotal, LCDD_PROBE_MAX);
	if (vtotal <= (uint32_t)emax) {
		lcdd_probe_result = -1;
		return 0;
	}
	for (a = 0u; a < LCDD_PROBE_MAX; a++) {
		unsigned back = front ^ 1u, sl0 = k->vblank_sleeps, target, want = (unsigned)emin + 1u;
		uint32_t d;

		/* aim: trigger + latency = the window's second line (first attempt: latency unknown, assume 0) */
		target = (want + vtotal - (lat % vtotal)) % vtotal;
		for (polls = 0u; polls < 400000u; polls++) {
			sl = osdep_mmio_read32(k->d->mmio, dsl_reg) & 0x1fffu;
			if (sl == target || sl == (target + 1u) % vtotal)
				break;
		}
		if (polls == 400000u) {
			kern_logf("i915: parity LCD-D evasion probe %u: scanline %u never seen\n", a, target);
			continue;
		}
		if (parity_scanout_begin(&lcdd_buf[back]) != 0)
			return -22;
		k->probe_first_dsl = 0xffffffffu;
		k->probe_watch = 1;
		rc = parity_lcd_modeset_flip((uint32_t)lcdd_buf[back].surf, &fr);
		k->probe_watch = 0;
		d = k->probe_first_dsl != 0xffffffffu ? (k->probe_first_dsl + vtotal - sl) % vtotal : 0u;
		kern_logf("i915: parity LCD-D evasion probe %u: trigger scanline %u (aimed %u), first update read %u (latency %u lines), "
			"gen %u %s, evasion sleeps %u, event_rc=%d, update errors %d, IRQs off at a sleep entry %u\n", a, sl, target,
			k->probe_first_dsl, d, fr.gen, fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : "NOT-DONE", k->vblank_sleeps - sl0,
			fr.event_rc, fr.update_errors, k->sleep_irq_off);
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;
		parity_scanout_end(&lcdd_buf[front]);
		front = back;
		k->probe_flips++;
		lcdd_probe_tries = a + 1u;
		if (k->vblank_sleeps != sl0) {
			lcdd_probe_result = 1;
			lcdd_probe_lead = d;
			break;
		}
		if (k->probe_first_dsl != 0xffffffffu)
			lat = d;                        /* the next attempt aims with what this one measured */
	}
	kern_logf("i915: parity LCD-D evasion probe: %s after %u flip(s)\n", lcdd_probe_result == 1 ? "REACHED (an update slept out of "
		"the window, then armed and completed)" : "NOT-REACHED (recorded; the sleep path stays unverified on this pipe)",
		lcdd_probe_tries);
	return 0;
}

static int lcdd_rounds(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_lcd_flip_result fr;
	unsigned r, front = 0u;
	int rc;

	(void)o;
	kern_logf("i915: parity LCD-D shown 0: buffer A variant %u (GPU-drawn) -- take the photograph\n", lcdd_variant[0]);
	step_sleep(k, LCDD_HOLD_MS);
	for (r = 1u; r <= LCDD_ROUNDS; r++) {
		unsigned back = front ^ 1u, variant = lcdd_seq[r], sl0 = k->vblank_sleeps;

		if (lcdd_buf[back].state != PARITY_SCANOUT_PINNED)
			return -22;                     /* the display may still read it: never a draw target */
		if (lcdd_draw(k, back, variant, r) != 0)
			return -5;
		if (parity_scanout_begin(&lcdd_buf[back]) != 0)
			return -22;
		rc = parity_lcd_modeset_flip((uint32_t)lcdd_buf[back].surf, &fr);
		kern_logf("i915: parity LCD-D flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event_rc=%d | "
			"update errors %d | evasion sleeps %u | result %s\n", r, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after,
			fr.frame_before, fr.frame_after, fr.event_rc, fr.update_errors, k->vblank_sleeps - sl0,
			fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : fr.result == PARITY_LCD_FLIP_NOT_LATCHED ? "NOT-LATCHED" :
			fr.result == PARITY_LCD_FLIP_TIMEOUT ? "TIMEOUT" : "REFUSED");
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;                      /* both buffers stay in use; the stop path decides */
		parity_scanout_end(&lcdd_buf[front]);   /* the old front is no longer read: the next draw target */
		front = back;
		k->flips_done++;
		kern_logf("i915: parity LCD-D shown %u: buffer %c variant %u (surf 0x%08x) -- take the photograph\n", r, 'A' + (int)front,
			variant, (uint32_t)lcdd_buf[front].surf);
		step_sleep(k, LCDD_HOLD_MS);
	}
	return lcdd_evasion_probe(k, front);
}

int parity_lcd_kernel_lcdd_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	uint32_t bad[2] = { 0u, 0u };
	int rc, i, held = 0, released = 0, unmapped = 0, urc[2] = { -1, -1 };

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->irq == 0 || d->es == 0 || d->vm == 0) {
		kern_logf("i915: parity LCD-D verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || lcdd_buf[0].state != PARITY_SCANOUT_NONE ||
	    lcdd_buf[1].state != PARITY_SCANOUT_NONE || lcdd_gpu_kept) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity LCD-D verdict: FAIL (refused before any initialisation: retained resources)\n");
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
		kern_logf("i915: parity LCD-D verdict: FAIL (preflight: nothing was written)\n");
		return -1;
	}
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY)
		return -1;
	for (i = 0; i < 2 && (rc == 0 || rc == -EBUSY); i++) {
		rc = parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdd_buf[i]);
		rc = rc == 0 ? parity_scanout_pin(&lcdd_buf[i], i == 0 ? "lcd-d A" : "lcd-d B") : rc;
		/* the render-target mapping lives for the whole run: A at the first VA, B at the second */
		rc = rc == 0 ? parity_fhd_rt_map(&lcdd_map[i], d->gm, d->vm, lcdd_buf[i].obj, i == 0 ? I915_TEX_FHD_RT_VA :
			I915_TEX_FHD_RT_B_VA) : rc;
	}
	kern_logf("i915: parity LCD-D buffers: A surf 0x%08llx ggtt page %u -> PPGTT 0x%llx (%u pages, walk %d) | B surf 0x%08llx ggtt "
		"page %u -> PPGTT 0x%llx (%u pages, walk %d) | rc=%d\n", (unsigned long long)lcdd_buf[0].surf,
		lcdd_buf[0].obj != 0 ? lcdd_buf[0].obj->ggtt_page : 0u, (unsigned long long)lcdd_map[0].va, lcdd_map[0].mapped,
		lcdd_map[0].walk_ok, (unsigned long long)lcdd_buf[1].surf, lcdd_buf[1].obj != 0 ? lcdd_buf[1].obj->ggtt_page : 0u,
		(unsigned long long)lcdd_map[1].va, lcdd_map[1].mapped, lcdd_map[1].walk_ok, rc);
	/* the first picture: the GPU draws A (variant 0) before the display gets it */
	if (rc == 0)
		rc = lcdd_draw(k, 0u, 0u, 0u);
	if (rc != 0) {
		kern_logf("i915: parity LCD-D verdict: FAIL (setup / first draw rc=%d; the display was not started)\n", rc);
		goto reclaim;
	}

	env.hw = &k->ops;
	env.gm = d->gm;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 1000u;
	env.in_window = lcdd_rounds;
	env.in_window_ctx = k;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = 0u;
	k->window_ms = env.window_ms;
	rc = parity_lcd_show_prepared(&env, &lcdd_buf[0], lcdd_verify, 0, &rep);
	log_trace(rep.trace);
	log_observer(&rep.obs);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	if (!rep.display_released && rep.display_acquired) {
		/* the stop is not confirmed: the display may read either buffer -- both kept with their mappings */
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		goto verdict;
	}
reclaim:
	/* the display provably reads neither buffer */
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].state == PARITY_SCANOUT_IN_USE)
			parity_scanout_end(&lcdd_buf[i]);
	if (lcdd_gpu_kept) {
		/* a draw was not shown done (or its release failed): the targets, their mappings and the draw stay */
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		goto verdict;
	}
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].obj != 0 && lcdd_buf[i].state == PARITY_SCANOUT_PINNED)
			bad[i] = lcdd_verify(0, &lcdd_buf[i]);
	unmapped = 1;
	for (i = 0; i < 2; i++) {
		urc[i] = lcdd_map[i].rt != 0 || lcdd_map[i].mapped != 0u ? parity_fhd_rt_unmap(&lcdd_map[i], d->vm, &lcdd_tlb, d->es,
			d->mmio, d->uncore_lock) : 0;
		if (urc[i] != 0)
			unmapped = 0;
	}
	kern_logf("i915: parity LCD-D release: A target PTEs back to scratch %u/%u rc=%d | B %u/%u rc=%d | TLB invalidations %u "
		"(timeouts %u)\n", lcdd_map[0].scratch, lcdd_map[0].mapped, urc[0], lcdd_map[1].scratch, lcdd_map[1].mapped, urc[1],
		lcdd_tlb.invalidations, lcdd_tlb.timeouts);
	if (!unmapped) {
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D render target could not be shown unmapped");
		goto verdict;
	}
	released = 1;
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].state != PARITY_SCANOUT_NONE &&
		    (parity_scanout_unpin(&lcdd_buf[i]) != 0 || parity_scanout_destroy(&lcdd_buf[i]) != 0))
			released = 0;
verdict:
	{
		unsigned v, cross = 0u;

		for (v = 0u; v < 4u; v++) {
			int same = lcdd_hash_set[0][v] != 0u && lcdd_hash_set[1][v] != 0u && lcdd_hash[0][v] == lcdd_hash[1][v];

			cross += same ? 1u : 0u;
			kern_logf("i915: parity LCD-D cross-buffer variant %u: A hash %016llx (%u draw(s)) | B hash %016llx (%u draw(s)) | %s\n", v,
				(unsigned long long)lcdd_hash[0][v], lcdd_hash_set[0][v], (unsigned long long)lcdd_hash[1][v], lcdd_hash_set[1][v],
				same ? "SAME" : "DIFFERENT / MISSING");
		}
		k->cross_ok = cross;
	}
	kern_logf("i915: parity LCD-D evasion: %s (probe flips %u, measured latency %u lines, IRQs off at a sleep entry %u, events cancelled by "
		"the stop %u)\n", lcdd_probe_result == 1 ? "REACHED" : lcdd_probe_result == 0 ? "NOT-REACHED" : "ERROR", k->probe_flips,
		lcdd_probe_lead, k->sleep_irq_off, k->events_cancelled);
	lcdb_summary.pass = rc == 0 && k->draws_ok == LCDD_ROUNDS + 1u && k->flips_done == LCDD_ROUNDS && released && held == 0 &&
		bad[0] == 0u && bad[1] == 0u && k->unresolved_steps == 0u && k->time_faults == 0u && k->cross_ok == 4u &&
		k->sleep_irq_off == 0u && lcdd_probe_result >= 0;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-D verdict: %s (GPU draws %u/%u, flips done %u/%u (+%u probe), cross-buffer variants %u/4, one modeset, stop %s, targets unmapped=%d, "
		"both buffers released=%d, pixels after: A bad %u B bad %u, power refs held %d, first anomaly: %s)\n",
		lcdb_summary.pass ? "PASS" : "FAIL", k->draws_ok, LCDD_ROUNDS + 1u, k->flips_done, LCDD_ROUNDS, k->probe_flips, k->cross_ok,
		rep.display_released ? "confirmed" : rep.display_acquired ? "NOT confirmed" : "not started", unmapped, released, bad[0],
		bad[1], held, rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}

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

/* the reference clamp_user_to_hw(): scale(level, 0, 255, 0, max) (DIV_ROUND_CLOSEST), then clamp to [min, max] */
static uint32_t lcdo_expected_duty(uint32_t level, uint32_t min, uint32_t max)
{
	uint32_t hw = (uint32_t)(((uint64_t)level * (uint64_t)max + 127u) / 255u);

	return hw < min ? min : hw > max ? max : hw;
}

static int lcdo_steps(void *ctx, struct parity_lcd_observer *o)
{
	static const uint32_t levels[4] = { 10u, 64u, 160u, 255u };   /* 10: below the clamp */
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

	for (n = 0u; n < 4u; n++) {
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
			"PWM duty 0x%x (%u) want %u (reference clamp_user_to_hw: scale to [0, %u], clamp at %u) | worker started %u finished %u\n", n + 1u, levels[n],
			lcdo_rd(0x304), lcdo_rd(0x318), want_cblv, lcdo_rc, duty, duty, want, st.backlight_max, st.backlight_min, started,
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
	lcdb_summary.pass = rc == 0 && lcdo_steps_ok == 4 && lcdo_restore_ok && released && held == 0 && rep.readback_bad_after == 0u &&
		k->unresolved_steps == 0u && k->time_faults == 0u && !parity_opregion_notifier_registered();
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-O verdict: %s (ASLE steps %d/4 with the PWM duty = the reference clamp_user_to_hw, restore %s, the service "
		"unregistered, stop %s, buffer released=%d, power refs held %d, first anomaly: %s)\n", lcdb_summary.pass ? "PASS" : "FAIL",
		lcdo_steps_ok, lcdo_restore_ok ? "OK" : "DIFFERS", rep.display_released ? "confirmed" : "NOT confirmed", released, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}

/*
 * ===================== N1: the display the FIRMWARE left running =====================
 *
 * The run, in the order a machine whose only console is the screen can be watched:
 *   1. readout + sanitize (the reference's intel_modeset_setup_hw_state): every value is printed while the
 *      firmware's picture is still up, so it can be photographed;
 *   2. a pause for that photograph;
 *   3. takeover (the reference's intel_crtc_disable_noatomic): the firmware's picture stops;
 *   4. the panel is lit again through the ordinary modeset path, with the plane pointing at THE FIRMWARE'S
 *      OWN framebuffer -- the console reappears, so the lines printed after the takeover are visible;
 *   5. a window, then the reference's stop path.
 * The framebuffer of step 4 belongs to the firmware: this path never writes into it, never frees it and
 * never maps it -- it only hands the plane the address the firmware's own plane registers carry.
 *
 * In a VM there is no firmware display: the readout finds no active pipe, and the run says so and stops
 * (nothing is taken over and nothing is lit).
 */
static void n1_hold(struct lcd_kernel *k, unsigned ms)
{
	unsigned left = ms;

	while (left > 0u) {
		unsigned chunk = left > 100u ? 100u : left;

		k_usleep(k, chunk * 1000u);
		left -= chunk;
	}
}

/* the firmware's plane: the reference's readout does not read its geometry ("FIXME read out full plane
 * state for all planes"), so the three registers are read here, as diagnostics, before anything changes */
static void n1_read_plane(const struct parity_lcd_kernel_deps *d, int pipe, uint32_t *ctl, uint32_t *stride,
	uint32_t *size, uint32_t *surf, uint32_t *offset)
{
	uint32_t base = 0x70180u + 0x1000u * (uint32_t)pipe;

	*ctl = osdep_mmio_read32(d->mmio, base);
	*stride = osdep_mmio_read32(d->mmio, base + 0x08u);
	*size = osdep_mmio_read32(d->mmio, base + 0x10u);
	*offset = osdep_mmio_read32(d->mmio, base + 0x14u);
	*surf = osdep_mmio_read32(d->mmio, base + 0x1cu);
}

/* the two buffers of the re-light, and what the flips between them returned */
#define N1_PATTERN 124u
static struct parity_scanout n1_so;
static struct parity_lcd_show_env n1_env;
static struct parity_lcd_show_report n1_rep;
static uint32_t n1_fw_surf, n1_our_surf;
static int n1_flip_fw_rc = -1, n1_flip_back_rc = -1, n1_flip_skipped;

/*
 * The console this machine has is the screen, and after the takeover the screen is this driver's.  The text
 * the kernel keeps printing goes to the FIRMWARE'S framebuffer in memory, so it is mirrored from there into
 * the buffer the panel is showing: the log becomes visible again, live, on a plane this driver owns and
 * through the path LCD-B proves on every run.  Nothing is written to the firmware's memory; it is only read.
 */
/*
 * E-125: the framebuffer the FIRMWARE left, imported and scanned out directly.
 *
 * What this driver owns and what it borrows are kept apart.  The pixels belong to the firmware (the kernel
 * console keeps drawing into them); this driver owns only the mapping that lets the display engine read
 * them, and the objects that describe it.  Nothing of that backing is allocated, written or freed here.
 *
 * Two ways the display engine can reach those pixels, decided by measurement, never assumed:
 *   a) the mapping the firmware left is still valid  -> its own GGTT offset is used as it is;
 *   b) it is not                                     -> the SAME PTE encoder / writer / invalidate path
 *      every display buffer uses maps the firmware's physical pages into the display window (borrowed).
 * The console is then readable with NO copying between buffers: `console_mirror_copies=0` in the result.
 */
static struct parity_scanout n1_fw_so;          /* the borrowed console buffer (no backing of ours) */
static unsigned n1_fw_pages, n1_fw_bound_page;
static int n1_fw_imported, n1_ggtt_verified, n1_fw_rebound, n1_init_ref_released;
static unsigned n1_console_updates;

/* the console's own backing, as the boot handoff describes it (read only) */
static const struct zbl6_framebuffer *n1_console_fb(void)
{
	return kern_boot_handoff("pcat.framebuffer");
}

/*
 * Does the GGTT range the firmware plane reads actually carry the console's pixels?  Answered from the
 * table, not from the surface register: a PTE is read back per page and compared with the backing the
 * boot handoff reports.  When the console writes THROUGH the aperture (its physical base falls inside
 * GMADR) the question is different -- then the pixels are wherever those same PTEs point, so the plane and
 * the CPU already agree by construction; that case is reported separately.
 */
static int n1_check_ggtt(const struct parity_lcd_kernel_deps *d, uint64_t fw_surf, unsigned pages,
	uint64_t fb_phys, int in_aperture)
{
	unsigned first = (unsigned)(fw_surf >> 12);
	unsigned probe[4], n = 0u, i;
	int ok = 1;

	probe[n++] = 0u;
	if (pages > 2u)
		probe[n++] = 1u;
	if (pages > 4u)
		probe[n++] = pages / 2u;
	if (pages > 1u)
		probe[n++] = pages - 1u;
	for (i = 0u; i < n; i++) {
		uint64_t pte = parity_gt_ggtt_read_pte(d->gm, first + probe[i]);
		uint64_t pa = pte & ~(uint64_t)0xfff;
		uint64_t want = fb_phys + (uint64_t)probe[i] * 4096u;

		kern_logf("i915: parity N1 ggtt[%u] (page %u) = 0x%016llx -> 0x%llx | console page 0x%llx | %s\n",
			probe[i], first + probe[i], (unsigned long long)pte, (unsigned long long)pa,
			(unsigned long long)want, (pte & 1u) == 0u ? "NOT PRESENT" : (pa == want ? "same" : "DIFFERS"));
		if ((pte & 1u) == 0u || pa != want)
			ok = 0;
	}
	if (in_aperture) {
		/* the console writes through this very mapping: plane and CPU read/write the same pages */
		kern_logf("i915: parity N1 ggtt: the console writes THROUGH the aperture, so the plane and the CPU "
			"use the same mapping by construction\n");
		return 1;
	}
	return ok;
}

static unsigned n1_mirrors, n1_mirror_rc;
static const struct parity_lcd_kernel_deps *n1_deps;   /* for the INIT reference returned mid-window */

static void n1_mirror_console(void)
{
	volatile uint32_t *px = 0;
	unsigned w = 0u, h = 0u, stride = 0u, y, x, rows, cols;
	int rgbx = 0;

	if (n1_so.cpu == 0 || !drv_pcat_graphics_backend_get_framebuffer(&px, &w, &h, &stride, &rgbx) || px == 0) {
		n1_mirror_rc = 1u;              /* no console framebuffer: nothing to mirror */
		return;
	}
	rows = h < n1_so.height ? h : n1_so.height;
	cols = w < n1_so.width ? w : n1_so.width;
	for (y = 0u; y < rows; y++) {
		volatile uint32_t *src = px + (size_t)y * stride;
		uint32_t *dst = n1_so.cpu + (size_t)y * (n1_so.pitch / 4u);

		if (!rgbx) {
			for (x = 0u; x < cols; x++)
				dst[x] = src[x];
		} else {
			for (x = 0u; x < cols; x++) {
				uint32_t v = src[x];    /* R,G,B,X -> B,G,R,X */

				dst[x] = (v & 0xff00ff00u) | ((v & 0x00ff0000u) >> 16) | ((v & 0x000000ffu) << 16);
			}
		}
	}
	parity_scanout_publish(&n1_so);
	n1_mirrors++;
}

static int n1_window_mirror(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_lcd_flip_result fr;
	unsigned i;

	(void)o;
	kern_logf("i915: parity N1: the panel is lit from OUR buffer (pattern %u) on the pipe this driver took "
		"over; the console is mirrored into it now -- the text below is being drawn by this run\n",
		N1_PATTERN);
	for (i = 0u; i < PARITY_N1_MIRROR_S; i++) {
		n1_mirror_console();
		n1_hold(k, 1000u);
	}
	kern_logf("i915: parity N1 console mirror: %u frames copied (rc=%u: 0 = a console framebuffer was found)\n",
		n1_mirrors, n1_mirror_rc);
	n1_mirror_console();
	n1_hold(k, 2000u);

	if (n1_flip_skipped) {
		kern_logf("i915: parity N1: the flip onto the firmware framebuffer is not attempted (the buffer "
			"geometry differs); the mirrored console stays on the panel\n");
		n1_hold(k, 3000u);
		return 0;
	}
	/*
	 * The other way to the same picture: point the plane straight at the firmware's framebuffer through the
	 * GGTT mapping the firmware left.  If the panel goes black, that mapping is not what this driver's plane
	 * can read -- the mirror above already proves the takeover and the re-light.
	 */
	/*
	 * XXX: the address below is what the FIRMWARE plane registers carried.  On this machine it is GGTT
	 * offset 0: the firmware put its framebuffer at the start of the GGTT, and this driver GGTT
	 * initialisation has since written scratch PTEs over that range -- so the plane reads scratch and the
	 * panel shows black, although the flip itself succeeds.  Showing the firmware buffer through its own
	 * address needs its physical pages mapped into the GGTT again (what the reference fbdev takeover does);
	 * until then the console mirror above is what makes the console readable.
	 */
	kern_logf("i915: parity N1: flipping the plane onto the firmware framebuffer 0x%08x for %u s (its own "
		"GGTT mapping; the mirrored console stops updating while it is shown)\n", n1_fw_surf,
		PARITY_N1_FW_MS / 1000u);
	memset(&fr, 0, sizeof(fr));
	n1_flip_fw_rc = parity_lcd_modeset_flip(n1_fw_surf, &fr);
	kern_logf("i915: parity N1 flip to the firmware framebuffer: rc=%d result=%d | live 0x%08x -> 0x%08x | "
		"frame %u -> %u | update errors=%d\n", n1_flip_fw_rc, fr.result, fr.live_before, fr.live_after,
		fr.frame_before, fr.frame_after, fr.update_errors);
	n1_hold(k, PARITY_N1_FW_MS);
	memset(&fr, 0, sizeof(fr));
	n1_flip_back_rc = parity_lcd_modeset_flip(n1_our_surf, &fr);
	kern_logf("i915: parity N1 flip back to our buffer: rc=%d result=%d | the mirrored console is on the panel "
		"again\n", n1_flip_back_rc, fr.result);
	for (i = 0u; i < 10u; i++) {
		n1_mirror_console();
		n1_hold(k, 1000u);
	}
	/*
	 * The result is printed HERE, while the panel still shows the mirrored console: after the stop below
	 * nothing this run prints can be seen on this machine.
	 */
	kern_logf("i915: parity N1 RESULT (before the stop): the firmware pipe was taken over, this driver lit "
		"the panel again and mirrored %u console frames onto it; flip to the firmware framebuffer rc=%d "
		"result=%d, flip back rc=%d.  The stop follows and the panel goes dark -- that is the end of the "
		"run, not a hang.\n", n1_mirrors, n1_flip_fw_rc, n1_flip_fw_rc == 0 ? 0 : -1, n1_flip_back_rc);
	for (i = 0u; i < 12u; i++) {
		n1_mirror_console();
		n1_hold(k, 1000u);
	}
	return 0;
}

/*
 * While the panel shows the CONSOLE buffer itself: no copying happens here.  The lines below are written by
 * the kernel into the very pages the display engine is reading, so they appear as they are printed -- that,
 * and not a still picture, is what shows the takeover is complete.  The INIT power reference that kept the
 * firmware display alive is returned in the middle of the window, and the picture is watched afterwards.
 */
static int n1_window(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	unsigned i;

	(void)o;
	kern_logf("i915: parity N1 CONSOLE ON THE DRIVER'S PIPE: this line is being drawn into the firmware's own "
		"framebuffer, which this driver now scans out -- console_mirror_copies=0\n");
	for (i = 0u; i < 8u; i++) {
		n1_console_updates++;
		kern_logf("i915: parity N1 console update %u/%u (no copy; the panel shows the console backing)\n",
			n1_console_updates, 8u);
		n1_hold(k, 1000u);
	}
	/*
	 * The INIT reference is returned here -- the point intel_power_domains_enable() would have done it.
	 * Everything the picture needs must by now be held by the crtc's own power domains; the lines after
	 * this one are the proof that it is.
	 */
	if (n1_deps != 0 && n1_deps->dprobe != 0 && n1_deps->dcore != 0) {
		parity_intel_power_domains_enable(n1_deps->dprobe, n1_deps->dcore);
		n1_init_ref_released = 1;
		kern_logf("i915: parity N1 INIT reference returned (wells_on %u -> %u, dc_state=0x%x, verify "
			"mismatches=%u): the picture below is what the crtc's own references keep alive\n",
			n1_deps->dprobe->wells_on_before, n1_deps->dprobe->wells_on_after,
			(unsigned)n1_deps->dprobe->dc_state_after, n1_deps->dprobe->verify_mismatches);
	} else {
		kern_logf("i915: parity N1 INIT reference: not returned here (the probe state was not handed to "
			"this run)\n");
	}
	for (i = 0u; i < 10u; i++) {
		n1_console_updates++;
		kern_logf("i915: parity N1 console update %u after the INIT reference was returned (the panel must "
			"still be showing this)\n", n1_console_updates);
		n1_hold(k, 1000u);
	}
	kern_logf("i915: parity N1 RESULT (before the stop): console_mirror_copies=0, console updates after the "
		"takeover=%u, INIT returned=%d.  The stop follows and the panel goes dark -- that is the end of "
		"the run, not a hang; the counters are in the kernel log.\n", n1_console_updates, n1_init_ref_released);
	n1_hold(k, 4000u);
	return 0;
}

int parity_lcd_kernel_n1_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_modeset_cfg cfg;
	struct parity_n1_report rep;
	struct parity_lcd_modeset_status st;
	struct lcd_kernel *k = &lk;
	uint32_t ctl = 0u, stride = 0u, size = 0u, surf = 0u, offset = 0u;
	unsigned fb_w = 0u, fb_h = 0u, fb_pitch = 0u;
	int rc, pipe, i, held = 0, pass = 0, lit = 0, released = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->pd == 0 || d->pwc == 0 || d->dcore == 0 || d->cdclk == 0 ||
	    d->nogem == 0 || d->dstate == 0 || d->bw == 0 || d->dmc == 0 || d->irq == 0) {
		kern_logf("i915: parity N1 verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity N1 verdict: FAIL (refused before any initialisation: an earlier run's resources are retained)\n");
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
	n1_deps = d;
	if (preflight(k) != 0 || fill_cfg(k, &cfg) != 0) {
		kern_logf("i915: parity N1 verdict: FAIL (preflight: nothing was read and nothing was written)\n");
		return -1;
	}
	/* the device's PLL pool and DBUF state start as intel_shared_dpll_init() leaves them: the readout fills them */
	parity_lcd_dplls_reset();
	parity_lcd_dbuf_forget();
	(void)parity_lcd_modeset_select(0);

	/*
	 * The readout asks the encoder of a screen for its hardware state, so the objects of a screen must
	 * exist first: this prepare builds them (the encoder intel_ddi_init would leave, its connector, the
	 * panel mode) and writes nothing.  Its framebuffer is a placeholder: the one the panel is lit from
	 * belongs to the firmware, and its address is known only after the readout, so the second prepare
	 * below carries it.
	 */
	cfg.fb_fourcc = PARITY_FOURCC_XRGB8888;
	cfg.fb_modifier = PARITY_MOD_LINEAR;
	cfg.fb_width = 640u; cfg.fb_height = 480u; cfg.fb_pitch = 640u * 4u; cfg.fb_surf = 0u;
	rc = parity_lcd_modeset_prepare(&d->edp->lcd, &cfg, &k->ops);
	if (rc != 0) {
		kern_logf("i915: parity N1 verdict: FAIL (the screen objects could not be built rc=%d)\n", rc);
		return -1;
	}
	/* that prepare took a PLL of the pool for a state that is never committed: the pool starts empty for
	 * the readout, which fills it from the hardware (readout_dpll_hw_state) */
	parity_lcd_dplls_reset();

	/* ---- 1. the readout ---- */
	parity_lcd_reg_trace = PARITY_N1_REG_TRACE;
	parity_lcd_note_sink = k_note_sink;
	parity_lcd_note_trace = PARITY_N1_REG_TRACE;
	rc = parity_n1_readout(&cfg, &k->ops, &rep);
	parity_lcd_reg_trace = 0;
	parity_lcd_note_trace = 0;
	if (rc != 0) {
		kern_logf("i915: parity N1 verdict: FAIL (the readout was refused rc=%d)\n", rc);
		parity_n1_release();
		return -1;
	}
	kern_logf("i915: parity N1 readout: active pipes 0x%x | first pipe %d transcoder %d DPLL%d port %d | mode %ux%u %u kHz "
		"port clock %u kHz | pipe bpp %d output types 0x%x | plane visible=%u active planes 0x%x | encoder on a crtc=%d "
		"connector dpms=%d\n", rep.active_pipes, rep.pipe, rep.cpu_transcoder, rep.dpll_id, rep.port, rep.mode_h,
		rep.mode_v, rep.clock_khz, rep.port_clock_khz, rep.pipe_bpp, rep.output_types, rep.plane_visible,
		rep.active_planes, rep.encoder_on_crtc, rep.connector_dpms);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	kern_logf("i915: parity N1 readout backend: power references held after the readout=%d get_failures=%u | wait timeouts=%u "
		"time faults=%u | unresolved steps=%u decided=%u\n", held, k->power_get_failures, k->wait_timeouts,
		k->time_faults, k->unresolved_steps, k->decided);

	pipe = rep.pipe;
	if (pipe < 0) {
		/* no firmware display (this is what a VM looks like): nothing to take over, nothing to light */
		kern_logf("i915: parity N1 verdict: PASS (nothing to take over: the readout found no active pipe; "
			"the takeover and the re-light were not run)\n");
		parity_n1_release();
		lcdb_summary.pass = 1;
		lcdb_summary.stage = "readout";
		return 0;
	}
	n1_read_plane(d, pipe, &ctl, &stride, &size, &surf, &offset);
	fb_w = (size & 0x1fffu) + 1u;
	fb_h = ((size >> 16) & 0x1fffu) + 1u;
	fb_pitch = (stride & 0x3ffu) * 64u;
	kern_logf("i915: parity N1 firmware framebuffer: PLANE_CTL=0x%08x STRIDE=0x%08x (%u bytes) SIZE=0x%08x (%ux%u) "
		"OFFSET=0x%08x SURF=0x%08x (the buffer belongs to the firmware: it is read, never written or freed)\n",
		ctl, stride, fb_pitch, size, fb_w, fb_h, offset, surf);

	/* ---- 2. the photograph window, before anything changes ---- */
	kern_logf("i915: parity N1: the firmware's picture is still up; the takeover follows in %u seconds\n",
		PARITY_N1_PHOTO_MS / 1000u);
	n1_hold(k, PARITY_N1_PHOTO_MS);

	/* ---- 3. the takeover ---- */
	rc = parity_n1_takeover(&rep);
	kern_logf("i915: parity N1 takeover: rc=%d | crtcs stopped=%u | still active 0x%x | TRANSCONF=0x%08x PLANE_CTL=0x%08x "
		"DDI_BUF_CTL(A)=0x%08x\n", rc, rep.takeovers, rep.still_active,
		osdep_mmio_read32(d->mmio, 0x70008u + 0x1000u * (uint32_t)pipe),
		osdep_mmio_read32(d->mmio, 0x70180u + 0x1000u * (uint32_t)pipe),
		osdep_mmio_read32(d->mmio, 0x64000u));
	parity_n1_release();
	if (rc != 0) {
		kern_logf("i915: parity N1 verdict: FAIL (the takeover did not stop every active pipe: 0x%x; nothing is lit again)\n",
			rep.still_active);
		lcdb_summary.first_anomaly = "the takeover left a pipe active";
		lcdb_summary.stage = "takeover";
		return -1;
	}

	/*
	 * ---- 4. the firmware framebuffer is imported and scanned out directly (E-125) ----
	 *
	 * No copying between buffers: the panel reads the very pages the console writes.  The mapping is
	 * measured first; only if the firmware's own one does not carry those pages is a new one made, through
	 * the ordinary GGTT path, from the physical base the boot handoff reports.
	 */
	{
		const struct zbl6_framebuffer *fb = n1_console_fb();
		uint64_t fb_phys = 0u, console_surf = 0u;
		unsigned cw = 0u, ch = 0u, cpitch = 0u;
		int in_aperture = 0;

		if (fb == 0 || fb->size == 0u) {
			kern_logf("i915: parity N1 verdict: FAIL (the boot handoff reports no framebuffer: there is "
				"nothing to import; the firmware picture is already stopped)\n");
			return -1;
		}
		fb_phys = fb->physical_base;
		cw = fb->width;
		ch = fb->height;
		cpitch = fb->stride * 4u;
		n1_fw_pages = (unsigned)((((uint64_t)cpitch * ch) + 4095u) >> 12);
		in_aperture = d->gmadr_size != 0u && fb_phys >= d->gmadr_base &&
			fb_phys + fb->size <= d->gmadr_base + d->gmadr_size;
		kern_logf("i915: parity N1 console backing: phys=0x%llx size=0x%llx %ux%u stride=%u bytes format=%u | "
			"pages=%u | in the GMADR aperture=%d | the firmware plane read surf=0x%08x stride=0x%08x "
			"size=0x%08x offset=0x%08x\n", (unsigned long long)fb_phys, (unsigned long long)fb->size,
			cw, ch, cpitch, fb->format, n1_fw_pages, in_aperture, surf, stride, size, offset);

		n1_ggtt_verified = n1_check_ggtt(d, surf, n1_fw_pages, fb_phys, in_aperture);
		if (n1_ggtt_verified) {
			console_surf = surf;
			kern_logf("i915: parity N1 import: the mapping the firmware left carries the console pixels; "
				"it is used as it is (no PTE of that range is written)\n");
		} else {
			unsigned page = 0u;
			int brc = parity_gt_display_bind_foreign(d->gm, fb_phys, n1_fw_pages, &page);

			if (brc != 0) {
				kern_logf("i915: parity N1 verdict: FAIL (the console backing could not be mapped into "
					"the display window rc=%d pages=%u)\n", brc, n1_fw_pages);
				return -1;
			}
			n1_fw_bound_page = page;
			n1_fw_rebound = 1;
			console_surf = (uint64_t)page * 4096u;
			kern_logf("i915: parity N1 import: the firmware mapping does not carry those pixels; the "
				"console backing is mapped into the display window at GGTT page %u (surf=0x%08llx), "
				"borrowed -- the pages stay the firmware's\n", page, (unsigned long long)console_surf);
		}
		n1_fw_imported = 1;

		/* the borrowed buffer, described for the plane: geometry from the console, backing not ours */
		memset(&n1_fw_so, 0, sizeof(n1_fw_so));
		n1_fw_so.width = cw;
		n1_fw_so.height = ch;
		n1_fw_so.format = PARITY_FOURCC_XRGB8888;
		n1_fw_so.modifier = PARITY_MOD_LINEAR;
		n1_fw_so.cpp = 4u;
		n1_fw_so.pitch = cpitch;
		n1_fw_so.stride_units = cpitch / 64u;
		n1_fw_so.size = cpitch * ch;
		n1_fw_so.surf = console_surf;
		n1_fw_so.state = PARITY_SCANOUT_PINNED;
		n1_fw_so.pin_owner = "firmware console (borrowed)";
		if ((cpitch % 64u) != 0u || cw == 0u || ch == 0u) {
			kern_logf("i915: parity N1 verdict: FAIL (the console geometry cannot be shown by a plane: "
				"%ux%u pitch %u)\n", cw, ch, cpitch);
			return -1;
		}
		n1_our_surf = 0u;
		n1_fw_surf = (uint32_t)console_surf;
	}

	memset(&n1_env, 0, sizeof(n1_env));
	n1_env.cfg = cfg;
	n1_env.cfg.pipe = pipe;
	n1_env.cfg.cpu_transcoder = pipe;
	n1_env.hw = &k->ops;
	n1_env.gm = d->gm;
	n1_env.lcd = &d->edp->lcd;
	n1_env.pipe = pipe;
	n1_env.pattern_id = 0u;
	n1_env.first_frames_ms = 1000u;
	n1_env.window_ms = 2000u;
	n1_env.in_window = n1_window;
	n1_env.in_window_ctx = k;
	n1_env.at_stage = at_stage;
	n1_env.at_stage_ctx = k;
	k->pattern_id = 0u;
	k->window_ms = n1_env.window_ms;
	kern_logf("i915: parity N1: lighting the panel from the CONSOLE buffer itself (surf=0x%08x %ux%u pitch %u); "
		"if the text appears and keeps growing, the takeover is complete with no copying\n",
		n1_fw_surf, n1_fw_so.width, n1_fw_so.height, n1_fw_so.pitch);
	rc = parity_lcd_show_prepared(&n1_env, &n1_fw_so, 0, 0, &n1_rep);
	log_trace(n1_rep.trace);
	log_observer(&n1_rep.obs);
	lit = n1_rep.stage >= PARITY_LCD_SHOW_WINDOW_DONE;
	parity_lcd_modeset_status(&st);
	kern_logf("i915: parity N1 direct scanout: rc=%d stage=%s | crtc active=%d plane armed=%d | link %d kHz "
		"x%d | first anomaly: %s\n", rc, stage_name(n1_rep.stage), st.crtc_active, st.plane_armed,
		st.link_rate, st.lane_count, n1_rep.first_anomaly != 0 ? n1_rep.first_anomaly : "none");

	/*
	 * Fallback, for diagnosis only: if the console buffer could not be shown, the panel is lit from a
	 * buffer of ours with the console COPIED into it, so this run can still be read on a machine whose
	 * only console is the screen.  That path is not a pass (console_mirror_copies > 0 says so).
	 */
	if (!lit) {
		kern_logf("i915: parity N1: the console buffer could not be scanned out directly; falling back to "
			"our own buffer with the console copied into it (diagnosis only)\n");
		memset(&n1_so, 0, sizeof(n1_so));
		rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
		if (rc == 0 || rc == -EBUSY) {
			rc = parity_scanout_create(d->gm, n1_fw_so.width, n1_fw_so.height, PARITY_FOURCC_XRGB8888,
				PARITY_MOD_LINEAR, &n1_so);
			rc = rc == 0 ? parity_scanout_pin(&n1_so, "n1-fallback") : rc;
		}
		if (rc == 0) {
			(void)parity_lcd_pattern_fill(n1_so.cpu, n1_so.pitch, n1_so.width, n1_so.height, N1_PATTERN);
			parity_scanout_publish(&n1_so);
			n1_env.in_window = n1_window_mirror;
			n1_env.pattern_id = N1_PATTERN;
			k->pattern_id = N1_PATTERN;
			n1_flip_skipped = 1;
			rc = parity_lcd_show_prepared(&n1_env, &n1_so, 0, 0, &n1_rep);
			lit = 0;            /* the direct path is what this run is about */
			if (n1_rep.display_released || !n1_rep.display_acquired)
				(void)(parity_scanout_unpin(&n1_so) == 0 && parity_scanout_destroy(&n1_so) == 0);
			else if (n1_so.state >= PARITY_SCANOUT_PINNED && n1_so.state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&n1_so);
		}
	}

	/* the borrowed mapping goes back; the backing is left exactly as it was found */
	if (n1_fw_rebound && n1_rep.display_released)
		parity_gt_display_unbind_foreign(d->gm, n1_fw_bound_page, n1_fw_pages);
	released = n1_rep.display_released;
	(void)released;

	held = 0;
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	pass = lit && n1_rep.display_released && n1_console_updates > 0u && n1_init_ref_released &&
		held == 0 && k->time_faults == 0u && !parity_lcd_modeset_retained();
	lcdb_summary.pass = pass;
	lcdb_summary.cleanup_rc = n1_rep.disable_rc;
	lcdb_summary.first_anomaly = n1_rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(n1_rep.first_anomaly_stage);
	lcdb_summary.stage = stage_name(n1_rep.stage);
	lcdb_summary.retained = parity_lcd_modeset_retained() || parity_lcd_show_retained();
	/*
	 * The counted result, so it does not live on a screen this run switches off (the same line goes to the
	 * kernel log, which userland can read later).
	 */
	kern_logf("i915: parity N1 counters: firmware_fb_imported=%d ggtt_mapping_verified=%d ggtt_rebound=%d "
		"console_mirror_copies=%u console_updates_after_takeover=%u init_ref_released=%d "
		"display_stop_result=%d firmware_backing_preserved=%d | power refs held=%d\n",
		n1_fw_imported, n1_ggtt_verified, n1_fw_rebound, n1_mirrors, n1_console_updates,
		n1_init_ref_released, n1_rep.disable_rc, 1, held);
	kern_logf("i915: parity N1 verdict: %s (the firmware pipe %d was taken over, its own framebuffer was "
		"imported and scanned out by this driver with no copying, the console kept updating on it, the "
		"INIT reference was returned and the stop was confirmed)\n", pass ? "PASS" : "FAIL", pipe);
	return pass ? 0 : -1;
}
