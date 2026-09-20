#!/usr/bin/env python3
"""WS031 E-118 round 38: tests of the IRQ-safety round: a failed drain stops the real well disable, the latch refuses
later disables, the closed pipe is refused by the handler, -EIO for a time-base fault, one waiter per pipe; host: a
refused power release in the disable commit is an unconfirmed stop.  usage: round38.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

k = load(P + "ktest.c")
k = rep(k, """				ipwc.irq_ops = 0;
				ipwc.irqs_enabled = 0;
			}
			id.vbl = 0;
			id.irqs_enabled = 0;""", """				/* ---- E-118 IRQ-DRAIN-WELL: a handler still inside pipe A: the real disable entry must keep the well on ---- */
				{
					static const struct parity_time_test_ops fault_ops2 = { ktest_fail_read, 0, 0, 0 };
					struct parity_power_well *other = 0;
					unsigned acks, j;
					int drc;

					for (j = 0u; j < ipd.num_power_wells; j++)
						if (ipd.power_wells[j].irq_pipe_mask == 2u)
							other = &ipd.power_wells[j];
					KCHECK(parity_power_well_get(pwa, &ipwc) == 0 && pwa->refcount == 1u && id.pipe_closed[0] == 0,
						"irq: IRQ-DRAIN-WELL PW_A taken; post-enable left pipe A open");
					id.pipe_inflight[0] = 1u;                       /* an invocation that never leaves pipe A */
					parity_power_well_put(pwa, &ipwc);
					KCHECK(ipwc.irq_sync_failed == 1 && pwa->refcount == 1u && ipwc.kept_wells == 1u &&
						parity_power_well_is_enabled(pwa, &ipwc) == 1 && kv.drain_timeouts == 1u && id.pipe_closed[0] == 1,
						"irq: IRQ-DRAIN-WELL drain timed out -> POWER_REQUEST NOT cleared (the well still reads enabled), kept and owned");
					KCHECK(other != 0 && parity_power_well_get(other, &ipwc) == 0 &&
						parity_power_well_disable(other, &ipwc) == -EBUSY && ipwc.disable_refusals >= 2u &&
						parity_power_well_is_enabled(other, &ipwc) == 1,
						"irq: IRQ-DRAIN-LATCH after that, every later well disable is refused (another pipe's well stays on too)");
					/* the handler refuses the closed pipe: its IIR is neither read nor acked */
					acks = id.de_pipe_iir_acks[0];
					id.pipe_inflight[0] = 0u;
					osdep_mmio_raw_write32(&m, 0x44200u, 1u << 16);
					osdep_mmio_raw_write32(&m, 0x44408u, 1u);
					parity_gen11_display_irq_handler(&id);
					KCHECK(id.de_pipe_iir_acks[0] == acks && id.pipe_refused[0] >= 1u && id.pipe_inflight[0] == 0u,
						"irq: IRQ-GATE the handler does not enter a closed pipe (admission refused, in-flight back to 0)");
					/* a time-base fault during the drain is -EIO, not a timeout */
					id.pipe_inflight[1] = 1u;
					parity_wait_test_set(&fault_ops2);
					drc = parity_irq_drain_pipes(&id, 1u << 1, 1000u);
					parity_wait_test_set(0);
					id.pipe_inflight[1] = 0u;
					KCHECK(drc == -EIO && kv.drain_time_faults == 1u && kv.drain_timeouts == 1u,
						"irq: IRQ-DRAIN-EIO the time base failing during the drain is -EIO, counted apart from a timeout");
					/* one waiter per pipe */
					id.irqs_enabled = 1;
					(void)parity_drm_vblank_get(&id, 2u);
					kv.waiting[2] = 1;
					KCHECK(parity_wait_vblank(&id, 2u, 1u, 10u, kvb_read_frame, 0, &seen) == -EBUSY && kv.second_waiter_refusals == 1u,
						"irq: VBL-ONE a second waiter on the same pipe is refused (it would re-arm the first one's wake-up)");
					kv.waiting[2] = 0;
					parity_drm_vblank_put(&id, 2u);
					/* this test's device is discarded: its latch goes with it */
					ipwc.irq_sync_failed = 0;
					id.pipe_closed[0] = 0;
				}
				ipwc.irq_ops = 0;
				ipwc.irqs_enabled = 0;
			}
			id.vbl = 0;
			id.irqs_enabled = 0;""")
save(P + "ktest.c", k)

t = load(T)
t = rep(t, "	/* ================= E. a time-base fault is not a timeout ================= */",
        """	/* ================= H. the pipe's power cannot be released (interrupt drain failed): the stop is not confirmed ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	lcd.fault_put_refused_domain = POWER_DOMAIN_PIPE_A + 1;
	rc = rc == 0 ? parity_lcd_modeset_commit_disable() : -99;
	parity_lcd_modeset_status(&s);
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.stop_unconfirmed == 1 && s.dc_off_held == 1 && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 &&
	      lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && s.first_error != 0 && strstr(s.first_error, "kept") != 0,
	      "H: a pipe power release refused in the commit tail -> the disable is NOT a success, DC_OFF and the pipe's power stay held");
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "H: and nothing new starts on top of it");
	parity_lcd_modeset_abandoned();
	CHECK(parity_lcd_modeset_discard_model(&trace.ops) == 0, "H: (the model is discarded)");
	(void)parity_edp_end(&res);

	/* ================= E. a time-base fault is not a timeout ================= */""")
save(T, t)
print("done")
