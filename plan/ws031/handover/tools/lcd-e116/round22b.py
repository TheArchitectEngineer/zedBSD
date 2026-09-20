#!/usr/bin/env python3
"""WS031 E-116 round 22b: model + recorder support of the commit's outer part; the observer (production code).
usage: round22b.py <repo root>"""
import sys
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---------------------------------------------------------------- model
h = load(L + "lcd_fake_hw.h")
h = rep(h, "	int fault_power_get;", "	int fault_frame_counter_frozen;         /* the pipe reports \"on\" but its frame counter does not move */" + NL +
        "	int fault_underrun_at_arm;              /* the pipe raises its underrun status when the plane is armed */" + NL +
        "	int fault_underrun_steady_after;        /* n > 0: the underrun status is raised by the n-th frame-counter read after the arm */" + NL +
        "	int fault_drop_dbuf_update;             /* DBUF slice requests are lost (a test of the MODEL) */" + NL +
        "	int fault_power_get;")
h = rep(h, "	unsigned plane_armed_without_ddb;", "	unsigned modeset_without_dc_off;        /* PLL / pipe enable or a plane arm while POWER_DOMAIN_DC_OFF is not held */" + NL +
        "	unsigned pipe_enabled_without_power;    /* TRANSCONF enable without the pipe's / transcoder's / DDI lanes' / display core's domain */" + NL +
        "	unsigned plane_armed_outside_slices;    /* the DDB range needs a DBUF slice that is not enabled, or MBUS joining that is not set */" + NL +
        "	unsigned dbuf_shrunk_under_plane;       /* a slice the armed plane's DDB needs was switched off while the pipe runs */" + NL +
        "	unsigned power_dropped_with_pipe_on;    /* the last reference of the pipe's / transcoder's domain went while the pipe runs */" + NL +
        "	unsigned plane_armed_without_ddb;")
h = rep(h, "	uint32_t plane_ctl_at_arm, plane_surf_at_arm;", "	uint32_t plane_ctl_at_arm, plane_surf_at_arm;" + NL +
        "	uint8_t dbuf_enabled;                   /* the slices that are powered (bit n = slice n+1) */" + NL +
        "	unsigned dbuf_updates, async_puts; int last_async_delay_ms;" + NL +
        "	unsigned frame_reads_after_arm;" + NL +
        "	uint8_t obs[48]; unsigned nobs;         /* the observe() points, in order */")
save(L + "lcd_fake_hw.h", h)

c = load(L + "lcd_fake_hw.c")
c = rep(c, '#include "../dp/parity_edp.h"', '#include "../dp/parity_edp.h"' + NL + '#include "lcd_power_domain_enum.h"      /* reference, extracted: the domain numbers the ops carry */')
c = rep(c, "#define PPC_POWER_ON          1u", """#define REG_PIPESTATUS(p)     (0x70058u + 0x1000u * (unsigned)(p))     /* ICL_PIPESTATUS: write-one-to-clear */
#define  PIPESTATUS_UNDERRUN  (1u << 31)
#define REG_DE_PIPE_IMR(p)    (0x44404u + 0x10u * (unsigned)(p))
#define REG_MBUS_CTL          0x4438cu
#define  MBUS_JOIN_BIT        (1u << 31)
#define DBUF_POWER_REQUEST    (1u << 31)
#define DBUF_POWER_STATE      (1u << 30)
#define PPC_POWER_ON          1u

static const uint32_t dbuf_ctl_reg[4] = { 0x45008u, 0x44fe8u, 0x44300u, 0x44304u };

/* the slices a DDB range [start, last] lies in: with MBUS joined the pipe sees all four as one buffer, else two */
static uint8_t slices_of_range(const struct lcd_fake_hw *hw, uint32_t start, uint32_t last)
{
	uint32_t per = hw->dbuf_size / 4u;
	uint8_t m = 0u;
	unsigned s;

	if (per == 0u)
		return 0u;
	for (s = 0; s < 4u; s++)
		if (start < (s + 1u) * per && last >= s * per)
			m |= (uint8_t)(1u << s);
	return m;
}

static int dc_off_held(const struct lcd_fake_hw *hw)
{
	return hw->power_refs[POWER_DOMAIN_DC_OFF] > 0;
}""")
c = rep(c, "	if (reg == REG_PIPE_FRMCNT(hw->pipe))" + NL + "		return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;",
        """	if (reg == REG_PIPE_FRMCNT(hw->pipe)) {
		if (hw->plane_arms != 0u && ++hw->frame_reads_after_arm == (unsigned)hw->fault_underrun_steady_after)
			*slot(hw, REG_PIPESTATUS(hw->pipe), 1) |= PIPESTATUS_UNDERRUN;
		if (hw->fault_frame_counter_frozen)
			return 7u;
		return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;
	}""")
c = rep(c, "	if (reg == REG_DPLL_ENABLE(hw->dpll_id)) {" + NL + "		value &= ~(PLL_LOCK | PLL_POWER_STATE);",
        "	if (reg == REG_PIPESTATUS(hw->pipe)) {" + NL + "		*v = old & ~value;                      /* write-one-to-clear */" + NL + "		return;" + NL + "	}" + NL +
        "	if (reg == REG_DPLL_ENABLE(hw->dpll_id)) {" + NL +
        "		if ((value & PLL_ENABLE) && !(old & PLL_ENABLE) && !dc_off_held(hw))" + NL + "			hw->modeset_without_dc_off++;" + NL +
        "		value &= ~(PLL_LOCK | PLL_POWER_STATE);")
c = rep(c, "				if (!(lcd_fake_reg(hw, REG_DDI_BUF_CTL(hw->port)) & DDI_BUF_CTL_ENABLE))" + NL + "					hw->pipe_enabled_without_link++;",
        "				if (!(lcd_fake_reg(hw, REG_DDI_BUF_CTL(hw->port)) & DDI_BUF_CTL_ENABLE))" + NL + "					hw->pipe_enabled_without_link++;" + NL +
        "				if (!dc_off_held(hw))" + NL + "					hw->modeset_without_dc_off++;" + NL +
        "				if (hw->power_refs[POWER_DOMAIN_PIPE_A + hw->pipe] <= 0 || hw->power_refs[POWER_DOMAIN_TRANSCODER_A + hw->pipe] <= 0 ||" + NL +
        "				    hw->power_refs[POWER_DOMAIN_PORT_DDI_LANES_A + hw->port] <= 0 || hw->power_refs[POWER_DOMAIN_DISPLAY_CORE] <= 0)" + NL +
        "					hw->pipe_enabled_without_power++;")
c = rep(c, "				    !(lcd_fake_reg(hw, REG_PLANE_WM0(hw->pipe)) & (1u << 31)))" + NL + "					hw->plane_armed_without_ddb++;",
        "				    !(lcd_fake_reg(hw, REG_PLANE_WM0(hw->pipe)) & (1u << 31)))" + NL + "					hw->plane_armed_without_ddb++;" + NL +
        "				else if ((slices_of_range(hw, start, last) & ~hw->dbuf_enabled) != 0u ||" + NL +
        "					 ((slices_of_range(hw, start, last) & 0x0cu) != 0u && hw->pipe == 0 &&" + NL +
        "					  !(lcd_fake_reg(hw, REG_MBUS_CTL) & MBUS_JOIN_BIT)))" + NL +
        "					hw->plane_armed_outside_slices++;" + NL +
        "				if (!dc_off_held(hw))" + NL + "					hw->modeset_without_dc_off++;" + NL +
        "				if (hw->fault_underrun_at_arm)" + NL + "					*slot(hw, REG_PIPESTATUS(hw->pipe), 1) |= PIPESTATUS_UNDERRUN;")
c = rep(c, "	hw->power_refs[domain]--;" + NL + "}", "	hw->power_refs[domain]--;" + NL +
        "	if (hw->power_refs[domain] == 0 && hw->pipe_on_since_us != 0u &&" + NL +
        "	    (domain == POWER_DOMAIN_PIPE_A + hw->pipe || domain == POWER_DOMAIN_TRANSCODER_A + hw->pipe))" + NL +
        "		hw->power_dropped_with_pipe_on++;" + NL + "}" + NL + NL +
        "static void f_power_put_async(void *ctx, int domain, int wakeref, int delay_ms)" + NL + "{" + NL +
        "	struct lcd_fake_hw *hw = ctx;" + NL + NL +
        "	hw->async_puts++;" + NL + "	hw->last_async_delay_ms = delay_ms;" + NL + "	f_power_put(ctx, domain, wakeref);" + NL + "}" + NL + NL +
        "/* gen9_dbuf_slices_update(): request bit -> state bit, slice by slice */" + NL +
        "static void f_dbuf_slices_update(void *ctx, unsigned req_slices)" + NL + "{" + NL +
        "	struct lcd_fake_hw *hw = ctx;" + NL + "	unsigned s;" + NL + NL +
        "	hw->dbuf_updates++;" + NL + "	if (hw->fault_drop_dbuf_update)" + NL + "		return;" + NL +
        "	if (hw->plane_arms > hw->plane_disarms && hw->pipe_on_since_us != 0u) {" + NL +
        "		uint32_t cfg = lcd_fake_reg(hw, REG_PLANE_BUF_CFG(hw->pipe));" + NL + NL +
        "		if ((slices_of_range(hw, cfg & 0xfffu, (cfg >> 16) & 0xfffu) & ~req_slices) != 0u)" + NL +
        "			hw->dbuf_shrunk_under_plane++;" + NL + "	}" + NL +
        "	for (s = 0; s < 4u; s++)" + NL +
        "		*slot(hw, dbuf_ctl_reg[s], 1) = (lcd_fake_reg(hw, dbuf_ctl_reg[s]) & ~(DBUF_POWER_REQUEST | DBUF_POWER_STATE)) |" + NL +
        "			((req_slices & (1u << s)) ? (DBUF_POWER_REQUEST | DBUF_POWER_STATE) : 0u);" + NL +
        "	hw->dbuf_enabled = (uint8_t)(req_slices & 0x0fu);" + NL + "}" + NL + NL +
        "static void f_observe(void *ctx, int point)" + NL + "{" + NL + "	struct lcd_fake_hw *hw = ctx;" + NL + NL +
        "	if (hw->nobs < sizeof(hw->obs))" + NL + "		hw->obs[hw->nobs++] = (uint8_t)point;" + NL + "}")
c = rep(c, "	*slot(hw, REG_DPCLKA_CFGCR0, 1) = (1u << 10) | (1u << 11);", "	*slot(hw, REG_DPCLKA_CFGCR0, 1) = (1u << 10) | (1u << 11);" + NL +
        "	/* as the normal initialisation leaves them: DBUF slice 1 on, MBUS not joined, every pipe interrupt masked */" + NL +
        "	*slot(hw, dbuf_ctl_reg[0], 1) = DBUF_POWER_REQUEST | DBUF_POWER_STATE;" + NL +
        "	hw->dbuf_enabled = 0x01u;" + NL +
        "	*slot(hw, REG_DE_PIPE_IMR(pipe), 1) = 0xffffffffu;")
c = rep(c, "	hw->ops.power_put = f_power_put;", "	hw->ops.power_put = f_power_put;" + NL + "	hw->ops.power_put_async = f_power_put_async;" + NL +
        "	hw->ops.dbuf_slices_update = f_dbuf_slices_update;" + NL + "	hw->ops.observe = f_observe;")
c = rep(c, "hw->pattern_mismatch + hw->plane_armed_without_ddb +", "hw->pattern_mismatch + hw->plane_armed_without_ddb + hw->modeset_without_dc_off +" + NL +
        "		hw->pipe_enabled_without_power + hw->plane_armed_outside_slices + hw->dbuf_shrunk_under_plane + hw->power_dropped_with_pipe_on +")
save(L + "lcd_fake_hw.c", c)

# ---------------------------------------------------------------- recorder
t = load(L + "parity_lcd_trace.h")
t = rep(t, "	PARITY_LCD_T_DECIDED,       /* name: a reference callee deliberately not connected (reason in lcd_seq_compat.h) */",
        "	PARITY_LCD_T_DECIDED,       /* name: a reference callee deliberately not connected (reason in lcd_seq_compat.h) */" + NL +
        "	PARITY_LCD_T_DBUF,          /* a = the DBUF slices requested (gen9_dbuf_slices_update) */" + NL +
        "	PARITY_LCD_T_OBSERVE,       /* a = enum parity_lcd_observe: a point of the commit */")
t = rep(t, "	PARITY_LCD_T_POWER_PUT,     /* a = domain, b = wakeref */", "	PARITY_LCD_T_POWER_PUT,     /* a = domain, b = wakeref; asynchronous: d = 1, c = delay in ms */")
save(L + "parity_lcd_trace.h", t)
tc = load(L + "parity_lcd_trace.c")
tc = rep(tc, "static void t_lock(void *ctx, int which, int take)", """static void t_power_put_async(void *ctx, int domain, int wakeref, int delay_ms)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_POWER_PUT);

	if (e) { e->a = (uint32_t)domain; e->b = (uint32_t)wakeref; e->c = (uint32_t)delay_ms; e->d = 1u; }
	t->backend->power_put_async(t->backend->ctx, domain, wakeref, delay_ms);
}

static void t_dbuf_slices_update(void *ctx, unsigned req_slices)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_DBUF);

	if (e) e->a = req_slices;
	t->backend->dbuf_slices_update(t->backend->ctx, req_slices);
}

static void t_observe(void *ctx, int point)
{
	struct parity_lcd_trace *t = ctx;
	struct parity_lcd_trace_entry *e = add(t, PARITY_LCD_T_OBSERVE);

	if (e) e->a = (uint32_t)point;
	if (t->backend->observe != 0)
		t->backend->observe(t->backend->ctx, point);
}

static void t_lock(void *ctx, int which, int take)""")
tc = rep(tc, "	t->ops.power_put = t_power_put;", "	t->ops.power_put = t_power_put;" + NL +
         "	t->ops.power_put_async = backend->power_put_async != 0 ? t_power_put_async : 0;" + NL +
         "	t->ops.dbuf_slices_update = backend->dbuf_slices_update != 0 ? t_dbuf_slices_update : 0;" + NL +
         "	t->ops.observe = t_observe;")
save(L + "parity_lcd_trace.c", tc)

# ---------------------------------------------------------------- the observer
open(root + L + "parity_lcd_observe.h", "w").write("""/*
 * WS031 Linux-parity -- what the LCD test LOOKS AT while the reference's modeset runs: the pipe's underrun status
 * (ICL_PIPESTATUS), its frame counter and its interrupt mask.  zedBSD project code; plain C over parity_lcd_ops,
 * so the same code runs on the register model and on the real GPU.
 *
 * Why it exists: this private, synchronous, one-buffer test does not connect the DRM vblank machinery nor the
 * underrun interrupt (see lcd_seq_compat.h).  In exchange it OWNS the status register for the run:
 *   - the value found before the run is saved (not judged: it belongs to whoever ran before);
 *   - the status is cleared where the reference clears it (intel_set_cpu_fifo_underrun_reporting(true));
 *   - it is sampled at every observe() point of the commits and by the caller during the steady picture; bits seen
 *     are RECORDED first and only then cleared, so that a later period can be told from an earlier one and no
 *     occurrence is lost;
 *   - start / stop periods and the steady period are accumulated apart: an underrun while the pipe starts is a
 *     different statement from one while a picture stands.
 * The pipe's vblank interrupt must stay masked for the whole run (nothing here could service it): every sample also
 * reads GEN8_DE_PIPE_IMR and remembers if the vblank bit was ever found unmasked.
 * "Frames advance" is decided from the hardware frame counter alone -- never from elapsed time.
 */
#ifndef PARITY_LCD_OBSERVE_H
#define PARITY_LCD_OBSERVE_H

#include <stdint.h>
#include "parity_lcd_ops.h"

#define PARITY_LCD_OBS_STEADY 100               /* sample points of the caller: the steady picture */
#define PARITY_LCD_OBS_MAX_SAMPLES 32u

struct parity_lcd_obs_sample {
	int point;                      /* enum parity_lcd_observe, or PARITY_LCD_OBS_STEADY */
	uint32_t status;                /* ICL_PIPESTATUS as read */
	uint32_t frame;                 /* PIPE_FRMCOUNT_G4X as read */
	uint32_t imr;                   /* GEN8_DE_PIPE_IMR as read */
	int steady;
};

struct parity_lcd_observer {
	struct parity_lcd_emit *hw;     /* the backend that is read (NOT the recorder: these reads are not part of the path) */
	int pipe;
	uint32_t status_reg, frame_reg, imr_reg;
	uint32_t underrun_mask;         /* the bits the reference treats as underrun status on this display version */
	uint32_t vblank_bit;
	uint32_t saved_before;          /* status found before the run */
	int begun, steady;
	uint32_t seen_transition;       /* underrun bits seen while the pipe was started / stopped */
	uint32_t seen_steady;           /* underrun bits seen while the picture stood */
	int vblank_unmasked_seen;
	unsigned n, dropped;
	struct parity_lcd_obs_sample s[PARITY_LCD_OBS_MAX_SAMPLES];
};

void parity_lcd_observer_init(struct parity_lcd_observer *o, struct parity_lcd_emit *hw, int pipe);
/* the ops->observe hook: ctx is the observer */
void parity_lcd_observer_point(void *ctx, int point);
/* the picture is up and has settled: what is pending now still counts as start-up; from here on it is "steady" */
void parity_lcd_observer_steady_begin(struct parity_lcd_observer *o);
void parity_lcd_observer_steady_sample(struct parity_lcd_observer *o);
void parity_lcd_observer_steady_end(struct parity_lcd_observer *o);
/*
 * Watches the frame counter for at most window_ms (sleeping through the backend).  0 when it advanced by at least
 * min_frames; -110 when it did not -- however much time passed.  first / last: the counter values.
 */
int parity_lcd_observer_frames(struct parity_lcd_observer *o, unsigned window_ms, unsigned min_frames, uint32_t *first, uint32_t *last);
/* after the stop: 0 when the counter stood still over window_ms (the pipe no longer scans), -16 when it still moved */
int parity_lcd_observer_stopped(struct parity_lcd_observer *o, unsigned window_ms, uint32_t *first, uint32_t *last);

#endif /* PARITY_LCD_OBSERVE_H */
""")
open(root + L + "parity_lcd_observe.c", "w").write("""/*
 * WS031 Linux-parity -- the LCD test's observer (see parity_lcd_observe.h).  zedBSD project code.  Register names,
 * addresses and bits are the reference's own macros (extracted headers), not retyped numbers.
 */
#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "parity_lcd_observe.h"

void parity_lcd_observer_init(struct parity_lcd_observer *o, struct parity_lcd_emit *hw, int pipe)
{
	memset(o, 0, sizeof(*o));
	o->hw = hw;
	o->pipe = pipe;
	o->status_reg = i915_mmio_reg_offset(ICL_PIPESTATUS(pipe));
	o->frame_reg = i915_mmio_reg_offset(PIPE_FRMCOUNT_G4X(pipe));
	o->imr_reg = i915_mmio_reg_offset(GEN8_DE_PIPE_IMR(pipe));
	/* icl_pipe_status_underrun_mask() on display version 13: the pipe underrun plus the XELPD soft / hard / port bits */
	o->underrun_mask = PIPE_STATUS_UNDERRUN | PIPE_STATUS_SOFT_UNDERRUN_XELPD | PIPE_STATUS_HARD_UNDERRUN_XELPD |
		PIPE_STATUS_PORT_UNDERRUN_XELPD;
	o->vblank_bit = GEN8_PIPE_VBLANK;
}

static void sample(struct parity_lcd_observer *o, int point, int clear_all)
{
	uint32_t status = o->hw->read32(o->hw->ctx, o->status_reg);
	uint32_t imr = o->hw->read32(o->hw->ctx, o->imr_reg);
	uint32_t bits = status & o->underrun_mask;

	if (o->n < PARITY_LCD_OBS_MAX_SAMPLES) {
		struct parity_lcd_obs_sample *s = &o->s[o->n++];

		s->point = point;
		s->status = status;
		s->frame = o->hw->read32(o->hw->ctx, o->frame_reg);
		s->imr = imr;
		s->steady = o->steady;
	} else {
		o->dropped++;
	}
	if (!(imr & o->vblank_bit))
		o->vblank_unmasked_seen = 1;
	if (!o->begun) {
		o->saved_before = status;       /* left by whoever ran before: kept, not judged */
		o->begun = 1;
		return;
	}
	if (point == PARITY_LCD_OBS_UNDERRUN_ARM) {
		/* the reference's position: bdw_set_fifo_underrun_reporting(enable) writes the whole underrun mask */
		o->hw->write32(o->hw->ctx, o->status_reg, o->underrun_mask);
		return;
	}
	/* recorded first, cleared after: nothing is lost, and the next period starts clean */
	if (o->steady)
		o->seen_steady |= bits;
	else
		o->seen_transition |= bits;
	if (bits != 0u || clear_all)
		o->hw->write32(o->hw->ctx, o->status_reg, bits);
}

void parity_lcd_observer_point(void *ctx, int point)
{
	struct parity_lcd_observer *o = ctx;

	if (point == PARITY_LCD_OBS_COMMIT_BEGIN && o->steady)
		parity_lcd_observer_steady_end(o);
	sample(o, point, 0);
}

void parity_lcd_observer_steady_begin(struct parity_lcd_observer *o)
{
	sample(o, PARITY_LCD_OBS_STEADY, 0);    /* still the start-up period */
	o->steady = 1;
}

void parity_lcd_observer_steady_sample(struct parity_lcd_observer *o)
{
	sample(o, PARITY_LCD_OBS_STEADY, 0);
}

void parity_lcd_observer_steady_end(struct parity_lcd_observer *o)
{
	if (o->steady) {
		sample(o, PARITY_LCD_OBS_STEADY, 0);
		o->steady = 0;
	}
}

int parity_lcd_observer_frames(struct parity_lcd_observer *o, unsigned window_ms, unsigned min_frames, uint32_t *first, uint32_t *last)
{
	uint32_t f0 = o->hw->read32(o->hw->ctx, o->frame_reg), f = f0;
	unsigned waited;

	for (waited = 0; waited < window_ms && (uint32_t)(f - f0) < min_frames; waited += 10u) {
		o->hw->usleep(o->hw->ctx, 10000u);
		f = o->hw->read32(o->hw->ctx, o->frame_reg);
	}
	if (first != 0)
		*first = f0;
	if (last != 0)
		*last = f;
	return (uint32_t)(f - f0) >= min_frames ? 0 : -110;
}

int parity_lcd_observer_stopped(struct parity_lcd_observer *o, unsigned window_ms, uint32_t *first, uint32_t *last)
{
	uint32_t f0 = o->hw->read32(o->hw->ctx, o->frame_reg), f = f0;
	unsigned waited;

	for (waited = 0; waited < window_ms && f == f0; waited += 10u) {
		o->hw->usleep(o->hw->ctx, 10000u);
		f = o->hw->read32(o->hw->ctx, o->frame_reg);
	}
	if (first != 0)
		*first = f0;
	if (last != 0)
		*last = f;
	return f == f0 ? 0 : -16;
}
""")
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, "src/drivers/gpu/i915/parity/lcd/parity_lcd_trace.c ", "src/drivers/gpu/i915/parity/lcd/parity_lcd_trace.c src/drivers/gpu/i915/parity/lcd/parity_lcd_observe.c " +
         "src/drivers/gpu/i915/parity/lcd/intel_display_power_set_port.c src/drivers/gpu/i915/parity/lcd/intel_cdclk_port.c src/drivers/gpu/i915/parity/lcd/intel_bw_port.c ")
save("platform/amd64/vmunix.mk", mk)
for sh in ("plan/ws031/tests/run-lcd-modeset-host-test.sh",):
    s = load(sh)
    s = rep(s, '"$L/parity_lcd_trace.c" "$L/lcd_fake_hw.c"', '"$L/parity_lcd_trace.c" "$L/parity_lcd_observe.c" "$L/lcd_fake_hw.c"')
    save(sh, s)
print("done")
