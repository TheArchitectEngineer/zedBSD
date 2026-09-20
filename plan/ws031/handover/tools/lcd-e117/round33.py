#!/usr/bin/env python3
"""WS031 E-117 round 33: GPU-free checks of the power-well IRQ hooks, the IRQ synchronisation, vblank enable /
disable, delivery from the real handler, and the wait's refusal of stale / foreign / time-only evidence.
usage: round33.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

k = load(P + "ktest.c")
HELP = """
/* ---- E-117: vblank wait fixtures: the "frame counter" read also plays the hardware (raises a vblank on a pipe) ---- */
static struct parity_irq_dev *kvb_dev;
static struct osdep_mmio *kvb_m;
static int kvb_raise_pipe = -1;                 /* pipe whose vblank the first frame read raises; -1 none */
static int kvb_frame_moves;                     /* the frame counter advances between reads */
static uint32_t kvb_frame;

static uint32_t kvb_read_frame(void *ctx)
{
	(void)ctx;
	if (kvb_raise_pipe >= 0) {
		osdep_mmio_raw_write32(kvb_m, 0x44200u, 1u << (16u + (unsigned)kvb_raise_pipe));    /* DISPLAY_INT_CTL: pipe */
		osdep_mmio_raw_write32(kvb_m, 0x44408u + 0x10u * (unsigned)kvb_raise_pipe, 1u);   /* GEN8_DE_PIPE_IIR: VBLANK */
		parity_gen11_display_irq_handler(kvb_dev);
		kvb_raise_pipe = -1;
	}
	if (kvb_frame_moves)
		kvb_frame++;
	return kvb_frame;
}
"""
k = rep(k, "int" + NL + "parity_sync_ktest(void)", HELP + NL + "int" + NL + "parity_sync_ktest(void)")
save(P + "ktest.c", k)
k = load(P + "ktest.c")
assert "kvb_read_frame" in k, "helper not placed"
TESTS = """
		/* ---- E-117 IRQ-HOOK: gen8_irq_power_well_post_enable / _pre_disable, synchronize, vblank ---- */
		{
			static struct parity_irq_vblank kv;
			uint32_t extra = 0x00000001u | 0x80600000u | 0x00000008u;   /* VBLANK | XELPD underrun mask | flip done */
			uint32_t seen = 0u;
			int w;

			parity_irq_vblank_init(&id, &kv);
			id.irqs_enabled = 1;
			id.de_irq_mask[0] = ~id.de_pipe_masked;
			/* post-enable: stale IIR cleared, then IER = ~mask | extra, then IMR = the saved mask */
			osdep_mmio_raw_write32(&m, 0x44408u, 0x00000001u);
			f.wt_n = 0u;
			parity_gen8_irq_power_well_post_enable(&id, 1u << 0);
			KCHECK(kv.post_enable_calls == 1u && kv.post_imr[0] == id.de_irq_mask[0] && kv.post_ier[0] == (~id.de_irq_mask[0] | extra) &&
				fake_wt_find(&f, 0x44408u, 0xffffffffu, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x4440cu, ~id.de_irq_mask[0] | extra, 0xffffffffu) > fake_wt_find(&f, 0x44408u, 0xffffffffu, 0xffffffffu) &&
				fake_wt_find(&f, 0x44404u, id.de_irq_mask[0], 0xffffffffu) > fake_wt_find(&f, 0x4440cu, ~id.de_irq_mask[0] | extra, 0xffffffffu) &&
				fake_wt_find(&f, 0x44414u, 0u, 0u) < 0,
				"irq: IRQ-HOOK-POST pipe A only: stale IIR cleared, IER = ~de_irq_mask | vblank | underrun | flip done, then IMR = de_irq_mask");
			KCHECK((kv.post_imr[0] & 1u) == 1u, "irq: IRQ-HOOK-POST the restored mask keeps vblank MASKED (IER enabling it is not delivery)");
			id.irqs_enabled = 0;
			f.wt_n = 0u;
			parity_gen8_irq_power_well_post_enable(&id, 1u << 0);
			KCHECK(kv.post_enable_calls == 1u && kv.skipped_irqs_disabled == 1u && f.wt_n == 0u,
				"irq: IRQ-HOOK-OFF with interrupts not enabled the hook writes nothing (intel_irqs_enabled gate)");
			KCHECK(parity_drm_vblank_get(&id, 0u) == -EINVAL && kv.refs[0] == 0u,
				"irq: VBL-GET refused while interrupts are not enabled");
			id.irqs_enabled = 1;

			/* vblank enable / disable: only the IMR vblank bit changes */
			f.wt_n = 0u;
			KCHECK(parity_drm_vblank_get(&id, 0u) == 0 && kv.refs[0] == 1u && kv.enabled[0] == 1 && (id.de_irq_mask[0] & 1u) == 0u &&
				fake_wt_find(&f, 0x44404u, id.de_irq_mask[0], 0xffffffffu) >= 0 && parity_drm_vblank_get(&id, 0u) == 0 &&
				kv.enable_calls[0] == 1u,
				"irq: VBL-ENABLE the first reference unmasks vblank in IMR (bdw_enable_vblank); the second only counts");

			/* delivery: the real display handler counts a vblank of THIS pipe while enabled */
			kvb_dev = &id; kvb_m = &m; kvb_frame = 100u;
			kvb_raise_pipe = 0; kvb_frame_moves = 0;
			(void)kvb_read_frame(0);
			KCHECK(kv.count[0] == 1u, "irq: VBL-DELIVER a pipe A vblank interrupt reaches the pipe's vblank count");
			kvb_raise_pipe = 1;
			(void)kvb_read_frame(0);
			KCHECK(kv.count[0] == 1u && kv.count[1] == 0u, "irq: VBL-DELIVER pipe B's vblank (not enabled there) counts nowhere");

			/* the wait: new vblank of this pipe AND a moving frame counter */
			kvb_raise_pipe = 0; kvb_frame_moves = 1;
			w = parity_wait_vblank(&id, 0u, 1u, 50u, kvb_read_frame, 0, &seen);
			KCHECK(w == 0 && seen == 1u, "irq: VBL-WAIT a new pipe A vblank + a moving frame counter completes the wait");
			kvb_raise_pipe = 1; kvb_frame_moves = 1;
			w = parity_wait_vblank(&id, 0u, 1u, 50u, kvb_read_frame, 0, &seen);
			KCHECK(w == -ETIMEDOUT && seen == 0u, "irq: VBL-WAIT another pipe's vblank does not complete it (timeout)");
			kvb_raise_pipe = 0; kvb_frame_moves = 0;
			w = parity_wait_vblank(&id, 0u, 1u, 50u, kvb_read_frame, 0, &seen);
			KCHECK(w == -ETIMEDOUT && seen == 1u, "irq: VBL-WAIT an interrupt without the frame counter moving (stale pending bit) does not");
			kvb_raise_pipe = -1; kvb_frame_moves = 1;
			w = parity_wait_vblank(&id, 0u, 1u, 50u, kvb_read_frame, 0, &seen);
			KCHECK(w == -ETIMEDOUT && seen == 0u, "irq: VBL-WAIT a moving counter / elapsed time alone does not");

			parity_drm_vblank_put(&id, 0u);
			KCHECK(kv.refs[0] == 1u && (id.de_irq_mask[0] & 1u) == 0u, "irq: VBL-PUT the non-last put keeps vblank enabled");
			f.wt_n = 0u;
			parity_drm_vblank_put(&id, 0u);
			KCHECK(kv.refs[0] == 0u && kv.enabled[0] == 0 && (id.de_irq_mask[0] & 1u) == 1u &&
				fake_wt_find(&f, 0x44404u, id.de_irq_mask[0], 0xffffffffu) >= 0 && kv.disable_calls[0] == 1u,
				"irq: VBL-PUT the last put masks vblank at once (vblank_disable_immediate)");
			{
				uint32_t before = kv.count[0];
				unsigned acks = id.de_pipe_iir_acks[0];

				kvb_raise_pipe = 0;
				(void)kvb_read_frame(0);
				KCHECK(kv.count[0] == before && id.de_pipe_iir_acks[0] == acks + 1u,
					"irq: VBL-OFF a vblank arriving after the last put is acked but not counted");
			}
			KCHECK(parity_wait_vblank(&id, 0u, 1u, 10u, kvb_read_frame, 0, &seen) == -EINVAL,
				"irq: VBL-WAIT without a reference is refused, not waited out");

			/* pre-disable: sources reset, then the handler synchronisation */
			f.wt_n = 0u;
			parity_gen8_irq_power_well_pre_disable(&id, 1u << 0);
			KCHECK(kv.pre_disable_calls == 1u && kv.sync_calls == 1u && kv.sync_timeouts == 0u &&
				fake_wt_find(&f, 0x44404u, 0xffffffffu, 0xffffffffu) == 0 && fake_wt_find(&f, 0x4440cu, 0u, 0xffffffffu) > 0 &&
				fake_wt_find(&f, 0x44408u, 0xffffffffu, 0xffffffffu) > 0,
				"irq: IRQ-HOOK-PRE pipe A: IMR all masked, IER 0, IIR cleared, then the handler synchronisation");
			id.handler_entries = 5u; id.handler_exits = 4u;          /* one invocation still inside the handler */
			KCHECK(parity_intel_synchronize_irq(&id) == -ETIMEDOUT && kv.sync_timeouts == 1u,
				"irq: IRQ-SYNC a handler that never finishes is reported (bounded), not assumed done");
			id.handler_exits = 5u;
			KCHECK(parity_intel_synchronize_irq(&id) == 0, "irq: IRQ-SYNC returns once the exits reach the entries it saw");
			id.handler_entries = 0u; id.handler_exits = 0u;

			/* through the power-well bodies: PW_A's enable / disable call the bound hooks */
			{
				struct parity_power_well *pwa = 0;
				unsigned before_post = kv.post_enable_calls, before_pre = kv.pre_disable_calls;

				for (k = 0u; k < ipd.num_power_wells; k++)
					if (ipd.power_wells[k].irq_pipe_mask == 1u)
						pwa = &ipd.power_wells[k];
				ipwc.irqs_enabled = 1;
				ipwc.irq_ops = &parity_pw_irq_ops;
				ipwc.irq_ctx = &id;
				KCHECK(pwa != 0 && parity_power_well_enable(pwa, &ipwc) == 0 && kv.post_enable_calls == before_post + 1u &&
					parity_power_well_disable(pwa, &ipwc) == 0 && kv.pre_disable_calls == before_pre + 1u &&
					ipwc.irq_post_enable_calls >= 1u && ipwc.irq_pre_disable_calls == 1u,
					"irq: IRQ-HOOK-WELL PW_A's enable restores and its disable stops pipe A's interrupts through the bound hooks");
				ipwc.irq_ops = 0;
				ipwc.irqs_enabled = 0;
			}
			id.vbl = 0;
			id.irqs_enabled = 0;
		}
"""
k = rep(k, "		/* ---- IRQ-NODISPLAY: HAS_DISPLAY=0 touches no display register ---- */", TESTS + NL + "		/* ---- IRQ-NODISPLAY: HAS_DISPLAY=0 touches no display register ---- */")
save(P + "ktest.c", k)
print("done")
