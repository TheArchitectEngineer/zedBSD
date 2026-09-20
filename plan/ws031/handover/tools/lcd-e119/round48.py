#!/usr/bin/env python3
"""WS031 E-119 round 48: host tests of the synchronous flip.  usage: round48.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

t = load(T)
t = rep(t, "	/* ================= E. a time-base fault is not a timeout ================= */", r"""	/* ================= I. the synchronous flip of the running picture ================= */
	{
		static const uint32_t seq[4] = { 0xfd000000u, 0xfdfc0000u, 0xfd000000u, 0xfdfc0000u };   /* B A B A */
		struct parity_lcd_flip_result fr;
		unsigned k, ok = 0u, arms0, outside0, ev0;

		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.surf_live == 0xfdfc0000u, "I: the picture is up on buffer A (live surface = A)");
		arms0 = lcd.plane_arms;
		outside0 = lcd.arm_outside_section;
		ev0 = lcd.events_done;
		for (k = 0u; k < 4u; k++) {
			uint32_t old = k == 0u ? 0xfdfc0000u : seq[k - 1u];

			rc = parity_lcd_modeset_flip(seq[k], &fr);
			parity_lcd_modeset_status(&s);
			if (rc == PARITY_LCD_MS_OK && fr.result == PARITY_LCD_FLIP_DONE && fr.gen == k + 1u && fr.old_surf == old &&
			    fr.live_before == old && fr.live_after == seq[k] && fr.frame_after > fr.frame_before && fr.event_rc == 0 &&
			    fr.update_errors == 0 && s.cur_surf == seq[k] && s.flip_pending == 0 && lcd.vblank_refs == 0 &&
			    lcd.plane_surf_at_arm == seq[k] && !lcd.irq_off)
				ok++;
			printf("  I: flip %u gen %u %08x -> %08x live %08x -> %08x frames %u -> %u event %d result %d" "\n", k + 1u, fr.gen,
			       fr.old_surf, fr.new_surf, fr.live_before, fr.live_after, fr.frame_before, fr.frame_after, fr.event_rc, fr.result);
		}
		CHECK(ok == 4u, "I: B, A, B, A: each flip completes only with its event AND the live surface = the new buffer; the old one is released");
		CHECK(lcd.plane_arms == arms0 + 4u && lcd.arm_outside_section == outside0 && lcd.events_done == ev0 + 4u && lcd.lock_errors == 0u &&
		      lcd.irq_off_calls >= 4u,
		      "I: every arm happened inside the update section (interrupts off, balanced), one event per flip, vblank references returned");
		{
			unsigned arms1 = lcd.plane_arms;

			rc = parity_lcd_modeset_flip(0xfdfc0000u, &fr);
			CHECK(rc == PARITY_LCD_MS_NOT_PREPARED && fr.result == PARITY_LCD_FLIP_REFUSED && lcd.plane_arms == arms1,
			      "I: a flip to the buffer already shown is refused, nothing written");
		}
		/* an early (stale) completion: the event reports before any vblank -- the live surface is still the old buffer */
		lcd.fault_early_event = 1;
		rc = parity_lcd_modeset_flip(0xfd000000u, &fr);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_NOT_LATCHED && fr.live_after == 0xfdfc0000u &&
		      s.flip_pending == 1 && s.flip_stuck == 1 && s.cur_surf == 0xfdfc0000u && s.pend_surf == 0xfd000000u,
		      "I: STALE an event without the new surface live is NOT a completion: both buffers stay protected");
		lcd.fault_early_event = 0;
		{
			unsigned arms1 = lcd.plane_arms;

			CHECK(parity_lcd_modeset_flip(0xfdfc0000u, &fr) == PARITY_LCD_MS_NOT_PREPARED && lcd.plane_arms == arms1,
			      "I: STALE and no further flip is submitted while one is unresolved");
		}
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.flip_pending == 0 && s.flip_stuck == 0 && lcd_fake_violations(&lcd) == 0,
		      "I: the stop path still runs; once the display is stopped neither buffer is read any more");
		CHECK(all_released("I"), "I: nothing held after the eDP ends");

		/* the arm never reaches the live surface */
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		lcd.fault_flip_never_latch = 1;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_NOT_LATCHED && fr.event_rc == 0 && fr.live_after == 0xfdfc0000u,
		      "I: NOT-LIVE the register write alone (live surface unchanged after the vblank) is not a completion");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* no vblank arrives */
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		lcd.fault_no_vblank = 1;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_TIMEOUT && fr.event_rc == -110 && s.flip_stuck == 1 &&
		      lcd.vblank_refs == 1,
		      "I: TIMEOUT no completion within the limit: both kept, the event's vblank reference stays (it may still complete)");
		lcd.fault_no_vblank = 0;
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
	}

	/* ================= E. a time-base fault is not a timeout ================= */""")
save(T, t)
print("done")
