/*
 * WS031 E-123 -- HPD-TEST: the HDMI hotplug receive path on the real hardware (-DPARITY_HDMI_HPD_TEST=1).
 * zedBSD project code.  Runs after intel_display_driver_probe() (interrupts installed, the hotplug registers
 * programmed, the hotplug path started).  No GPU submission, no display write besides the reference's own
 * SHOTPLUG_CTL_DDI write-back in icp_irq_handler().  A window of `window_s` seconds in which the operator plugs and
 * unplugs the HDMI cable of DDI B; every interrupt and every encoder->hotplug() is recorded by the hotplug glue.
 *
 *   PASS      the HDMI connector went to connected and to disconnected (each at least once), no HPD storm, no WARN
 *   NO-EVENT  no DDI hotplug interrupt arrived in the window
 *   FAIL      otherwise
 * The window ends early 30 s after the last interrupt once both transitions were seen.
 */
#include <stdint.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "../backend_sync.h"
#include "../osdep/mmio.h"
#include "parity_hotplug.h"

static struct parity_kcompletion hpd_test_sleep_c;

static const char *st_name(int s)
{
	return s == 1 ? "connected" : s == 2 ? "disconnected" : s == 3 ? "unknown" : "?";
}

int parity_hpd_test_run(struct osdep_mmio *m, unsigned window_s)
{
	struct parity_hpd_summary s;
	unsigned t, i, last_irq = 0u, quiet = 0u;
	uint32_t isr;
	int st;
	const char *verdict;

	parity_kcompletion_init(&hpd_test_sleep_c, "hpd-test-sleep");
	parity_hpd_summary(&s);
	if (!s.started || s.hdmi_connector < 0) {
		kern_logf("i915: parity HPD-TEST verdict: FAIL (hotplug path not started or no HDMI connector)\n");
		return -1;
	}
	isr = osdep_mmio_read32(m, 0xc4000u);
	kern_logf("i915: parity HPD-TEST start: %s (DDI B, pin 5) SDEISR=0x%08x (B live %u) SDEIMR=0x%08x SDEIER=0x%08x "
		"SHOTPLUG_CTL_DDI=0x%08x SHPD_FILTER_CNT=0x%08x\n", parity_hpd_connector_name((unsigned)s.hdmi_connector),
		isr, (isr >> 17) & 1u, osdep_mmio_read32(m, 0xc4004u), osdep_mmio_read32(m, 0xc400cu),
		osdep_mmio_read32(m, 0xc4030u), osdep_mmio_read32(m, 0xc4038u));
	st = parity_hpd_probe_connector((unsigned)s.hdmi_connector);
	kern_logf("i915: parity HPD-TEST initial detection: %s %s\n",
		parity_hpd_connector_name((unsigned)s.hdmi_connector), st_name(st));
	kern_logf("i915: parity HPD-TEST window open: %u s -- plug / unplug the HDMI cable now\n", window_s);
	for (t = 1u; t <= window_s; t++) {
		(void)parity_kwait(&hpd_test_sleep_c, sched_ticks() + KERN_CLOCK_HZ);
		parity_hpd_summary(&s);
		if (s.irq_entries != last_irq) {
			last_irq = s.irq_entries;
			quiet = 0u;
		} else {
			quiet++;
		}
		if (t % 10u == 0u)
			kern_logf("i915: parity HPD-TEST t=%us irqs=%u ddi_triggers=%u events=%u to_connected=%u "
				"to_disconnected=%u status=%s\n", t, s.irq_entries, s.ddi_triggers, s.hotplug_events,
				s.to_connected, s.to_disconnected, st_name(s.hdmi_status));
		if (s.to_connected > 0u && s.to_disconnected > 0u && quiet >= 30u)
			break;
	}
	parity_hpd_summary(&s);
	for (i = 0u; i < s.irq_records; i++) {
		const struct parity_hpd_irq_record *r = parity_hpd_irq_record(i);

		kern_logf("i915: parity HPD-TEST irq[%u] t=%llu SDEIIR=0x%08x SHOTPLUG_CTL_DDI=0x%08x (B: %s%s) "
			"event_bits=0x%x\n", i, (unsigned long long)r->tick, r->sde_iir, r->shotplug_ddi,
			(r->shotplug_ddi & 0x20u) ? "long" : "", (r->shotplug_ddi & 0x10u) ? " short" : "",
			r->event_bits_after);
	}
	for (i = 0u; i < s.hotplug_records; i++) {
		const struct parity_hpd_hotplug_record *r = parity_hpd_hotplug_record(i);

		kern_logf("i915: parity HPD-TEST hotplug[%u] t=%llu %s pin=%d retry=%d %s -> %s state=%d live=%d\n", i,
			(unsigned long long)r->tick, parity_hpd_connector_name(r->connector), r->pin, r->retries,
			st_name(r->old_status), st_name(r->new_status), r->state, r->live);
	}
	if (s.ddi_triggers == 0u)
		verdict = "NO-EVENT";
	else if (s.to_connected > 0u && s.to_disconnected > 0u && s.storms == 0u && s.warnings == 0u)
		verdict = "PASS";
	else
		verdict = "FAIL";
	kern_logf("i915: parity HPD-TEST verdict: %s (irqs=%u ddi_triggers=%u dropped=%u works=%u events=%u retries=%u "
		"to_connected=%u to_disconnected=%u storms=%u warnings=%u gmbus_irqs=%u status=%s epoch=%llu)\n", verdict,
		s.irq_entries, s.ddi_triggers, s.irq_dropped, s.hotplug_works, s.hotplug_events, s.retries_armed,
		s.to_connected, s.to_disconnected, s.storms, s.warnings, s.gmbus_irqs, st_name(s.hdmi_status), s.hdmi_epoch);
	return verdict[0] == 'P' ? 0 : -1;
}

/*
 * HDMI-EDID (-DPARITY_HDMI_EDID_TEST=1): the HDMI sink stays connected; its first detection runs the reference's
 * intel_hdmi_detect() -> intel_hdmi_set_edid() -> drm_edid_read_ddc() over GMBUS pin 2.  No GPU submission, no display
 * write besides the GMBUS registers.
 *   PASS  live status set, EDID base block with a valid header and checksum, digital input, status connected
 */
int parity_hdmi_edid_test_run(struct osdep_mmio *m)
{
	struct parity_hpd_summary s;
	struct parity_hpd_edid_info ei;
	const uint8_t *b;
	unsigned size = 0u, i;
	uint32_t isr;
	int st, ok;

	parity_hpd_summary(&s);
	if (!s.started || s.hdmi_connector < 0) {
		kern_logf("i915: parity HDMI-EDID verdict: FAIL (hotplug path not started or no HDMI connector)\n");
		return -1;
	}
	isr = osdep_mmio_read32(m, 0xc4000u);
	st = parity_hpd_probe_connector((unsigned)s.hdmi_connector);
	parity_hpd_edid_info(&ei);
	b = parity_hpd_edid_bytes((unsigned)s.hdmi_connector, &size);
	kern_logf("i915: parity HDMI-EDID %s: SDEISR=0x%08x (B live %u) status %s | reads %u fails %u rc %d | %s product "
		"0x%04x serial 0x%08x EDID %u.%u %s ext %u | DTD1 %ux%u %u kHz\n",
		parity_hpd_connector_name((unsigned)s.hdmi_connector), isr, (isr >> 17) & 1u, st_name(st), ei.reads, ei.fails,
		ei.rc, ei.mfg, ei.product, ei.serial, ei.version, ei.revision, ei.digital ? "digital" : "analog", ei.extensions,
		ei.hactive, ei.vactive, ei.pixel_clock_khz);
	for (i = 0u; b != NULL && i < size; i += 16u)
		kern_logf("i915: parity HDMI-EDID %03x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x\n", i, b[i], b[i + 1], b[i + 2], b[i + 3], b[i + 4], b[i + 5], b[i + 6], b[i + 7], b[i + 8],
			b[i + 9], b[i + 10], b[i + 11], b[i + 12], b[i + 13], b[i + 14], b[i + 15]);
	ok = ((isr >> 17) & 1u) != 0u && st == 1 && ei.rc >= 1 && ei.digital && b != NULL;
	kern_logf("i915: parity HDMI-EDID verdict: %s (live %u, status %s, EDID blocks %d, %s)\n", ok ? "PASS" : "FAIL",
		(isr >> 17) & 1u, st_name(st), ei.rc, ei.digital ? "digital" : "not digital");
	return ok ? 0 : -1;
}
