/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The HDMI hotplug receive path on the real hardware: the HDMI-EDID and the
 * HPD-TEST scenarios.
 *
 * Both run once the device has started, with the display interrupts
 * installed, the hotplug registers programmed and the hotplug path of the
 * started display running.  Neither submits to the GPU, and neither writes
 * a display register besides the GMBUS registers of the EDID read and the
 * reference's own SHOTPLUG_CTL_DDI write-back in icp_irq_handler().
 *
 *   HDMI-EDID  the HDMI sink stays connected; its detection runs the
 *              reference's intel_hdmi_detect() -> intel_hdmi_set_edid() ->
 *              drm_edid_read_ddc() over GMBUS pin 2.  PASS: live status set,
 *              an EDID base block with a valid header and checksum, digital
 *              input, status connected.
 *
 *   HPD-TEST   a window of I915_TEST_HPD_WINDOW_S seconds in which the
 *              operator plugs and unplugs the HDMI cable of DDI B; every
 *              interrupt and every encoder->hotplug() is recorded by the
 *              hotplug path.  PASS: the HDMI connector went to connected and
 *              to disconnected (each at least once), no HPD storm, no WARN.
 *              NO-EVENT: no DDI hotplug interrupt arrived in the window.
 *              FAIL otherwise.  The window ends early 30 s after the last
 *              interrupt once both transitions were seen.
 *
 * The verdicts are log lines ("i915: HDMI-EDID verdict:" and "i915:
 * HPD-TEST verdict:").
 */

#include "../../i915.h"
#include "../../mmio.h"
#include "../../display/hotplug.h"
#include "../../display/hdmi.h"

#include <kern/clock.h>
#include <kern/klog.h>
#include <kern/sched.h>

#include <stdint.h>

/* The south display interrupt and hotplug registers the scenarios log. */
#define I915_TEST_SDEISR		0xc4000u
#define I915_TEST_SDEIMR		0xc4004u
#define I915_TEST_SDEIER		0xc400cu
#define I915_TEST_SHOTPLUG_CTL_DDI	0xc4030u
#define I915_TEST_SHPD_FILTER_CNT	0xc4038u

/* Where DDI B's live status sits in SDEISR. */
#define I915_TEST_SDEISR_DDI_B_SHIFT	17

/* The long and short pulse status bits of DDI B in SHOTPLUG_CTL_DDI. */
#define I915_TEST_SHOTPLUG_B_LONG	0x20u
#define I915_TEST_SHOTPLUG_B_SHORT	0x10u

/* The connector statuses (enum drm_connector_status). */
#define I915_TEST_STATUS_CONNECTED	1
#define I915_TEST_STATUS_DISCONNECTED	2
#define I915_TEST_STATUS_UNKNOWN	3

/* How long the operator has to plug and unplug the cable, in seconds. */
#define I915_TEST_HPD_WINDOW_S		240u

/* How many quiet seconds after both transitions end the window early. */
#define I915_TEST_HPD_QUIET_S		30u

/* How often, in seconds, the window logs its progress. */
#define I915_TEST_HPD_PROGRESS_S	10u

/*
 * The scenarios' entries, which the test runner's table names.
 *
 * Declared here until the runner's header of the display scenarios
 * (tests/display/scenarios.h) exists.
 */
void drv_i915_test_display_hdmi_edid(struct i915_device *device);
void drv_i915_test_display_hdmi_hpd(struct i915_device *device);

/*
 * The completion the HPD-TEST window sleeps on, one second at a time.
 *
 * Nobody signals it; each wait simply lasts until its deadline.  Prepared
 * at the start of each window.
 */
static struct i915_completion hpd_test_sleep;

static const char *i915_status_name(int status);
static void i915_hdmi_edid_run(struct i915_display *display, struct i915_mmio *mmio);
static void i915_hdmi_edid_dump(const uint8_t *edid, unsigned size);
static void i915_hpd_test_run(struct i915_display *display, struct i915_mmio *mmio, unsigned window_s);
static void i915_hpd_test_window(struct i915_display *display, unsigned window_s);
static void i915_hpd_test_records(struct i915_display *display, const struct i915_hpd_summary *summary);

/*
 * Runs HDMI-EDID: one detection of the connected HDMI sink and its EDID.
 *
 * Only a display whose hotplug path started has the HDMI connector to
 * detect; otherwise the scenario fails at once.
 */
void
drv_i915_test_display_hdmi_edid(
	struct i915_device *device)
{
	struct i915_display *display;

	/* Refuses a device whose hotplug path did not start. */
	display = device->display;
	if (display == NULL || !display->hpd_started) {
		kern_logf("i915: HDMI-EDID verdict: FAIL (the hotplug path did not start)\n");
		return;
	}

	/* Detects the sink and reports its EDID. */
	i915_hdmi_edid_run(display, &device->gt.mmio);
}

/*
 * Runs HPD-TEST: a window in which the HDMI cable of DDI B is plugged and
 * unplugged, then what the hotplug path recorded.
 *
 * Only a display whose hotplug path started receives the interrupts;
 * otherwise the scenario fails at once.
 */
void
drv_i915_test_display_hdmi_hpd(
	struct i915_device *device)
{
	struct i915_display *display;

	/* Refuses a device whose hotplug path did not start. */
	display = device->display;
	if (display == NULL || !display->hpd_started) {
		kern_logf("i915: HPD-TEST verdict: FAIL (the hotplug path did not start)\n");
		return;
	}

	/* Opens the window and judges what arrived. */
	i915_hpd_test_run(display, &device->gt.mmio, I915_TEST_HPD_WINDOW_S);
}

/* Names a connector status for the log. */
static const char *
i915_status_name(
	int status)
{
	/* Names the three statuses; anything else is not one. */
	switch (status) {
	case I915_TEST_STATUS_CONNECTED:
		return "connected";
	case I915_TEST_STATUS_DISCONNECTED:
		return "disconnected";
	case I915_TEST_STATUS_UNKNOWN:
		return "unknown";
	default:
		break;
	}

	/* Succeeded: the value is no status. */
	return "?";
}

/* Detects the HDMI connector, logs the EDID it read, and logs the verdict. */
static void
i915_hdmi_edid_run(
	struct i915_display *display,
	struct i915_mmio *mmio)
{
	struct i915_hpd_summary summary;
	struct i915_hpd_edid_info edid_info;
	const uint8_t *edid;
	const char *name;
	unsigned size;
	unsigned live;
	uint32_t sdeisr;
	int status;
	int passed;

	/* Refuses a path that is not running or has no HDMI connector. */
	drv_i915_hpd_summary(display, &summary);
	if (!summary.started || summary.hdmi_connector < 0) {
		kern_logf("i915: HDMI-EDID verdict: FAIL (hotplug path not started or no HDMI connector)\n");
		return;
	}

	/* Reads the live status, then detects the connector the way its first probe does. */
	sdeisr = drv_i915_read32(mmio, I915_TEST_SDEISR);
	live = (sdeisr >> I915_TEST_SDEISR_DDI_B_SHIFT) & 1u;
	status = drv_i915_hpd_probe_connector(display, (unsigned)summary.hdmi_connector);

	/* Collects the EDID the detection read and the record of the read. */
	drv_i915_hpd_edid_info(display->hpd_world, &edid_info);
	size = 0u;
	edid = drv_i915_hpd_edid_bytes(display->hpd_world, (unsigned)summary.hdmi_connector, &size);

	/* Logs the detection and the decoded EDID. */
	name = drv_i915_hpd_connector_name(display, (unsigned)summary.hdmi_connector);
	kern_logf("i915: HDMI-EDID %s: SDEISR=0x%08x (B live %u) status %s | reads %u fails %u rc %d | %s product 0x%04x serial 0x%08x EDID %u.%u %s ext %u | DTD1 %ux%u %u kHz\n",
	    name,
	    sdeisr,
	    live,
	    i915_status_name(status),
	    edid_info.reads,
	    edid_info.fails,
	    edid_info.rc,
	    edid_info.mfg,
	    edid_info.product,
	    edid_info.serial,
	    edid_info.version,
	    edid_info.revision,
	    edid_info.digital ? "digital" : "analog",
	    edid_info.extensions,
	    edid_info.hactive,
	    edid_info.vactive,
	    edid_info.pixel_clock_khz);

	/* Logs the raw bytes. */
	if (edid != NULL)
		i915_hdmi_edid_dump(edid, size);

	/* A live, connected sink with a digital EDID of at least one block passes. */
	passed = 0;
	if (live != 0u &&
	    status == I915_TEST_STATUS_CONNECTED &&
	    edid_info.rc >= 1 &&
	    edid_info.digital &&
	    edid != NULL)
		passed = 1;
	kern_logf("i915: HDMI-EDID verdict: %s (live %u, status %s, EDID blocks %d, %s)\n",
	    passed ? "PASS" : "FAIL",
	    live,
	    i915_status_name(status),
	    edid_info.rc,
	    edid_info.digital ? "digital" : "not digital");
}

/* Logs EDID bytes, 16 to a line. */
static void
i915_hdmi_edid_dump(
	const uint8_t *edid,
	unsigned size)
{
	unsigned i;

	/* Logs each whole line of 16 bytes with its offset. */
	for (i = 0u; i + 16u <= size; i += 16u) {
		kern_logf("i915: HDMI-EDID %03x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		    i,
		    edid[i],
		    edid[i + 1u],
		    edid[i + 2u],
		    edid[i + 3u],
		    edid[i + 4u],
		    edid[i + 5u],
		    edid[i + 6u],
		    edid[i + 7u],
		    edid[i + 8u],
		    edid[i + 9u],
		    edid[i + 10u],
		    edid[i + 11u],
		    edid[i + 12u],
		    edid[i + 13u],
		    edid[i + 14u],
		    edid[i + 15u]);
	}
}

/* Logs the starting state, opens the window, then logs the records and the verdict. */
static void
i915_hpd_test_run(
	struct i915_display *display,
	struct i915_mmio *mmio,
	unsigned window_s)
{
	struct i915_hpd_summary summary;
	const char *name;
	const char *verdict;
	uint32_t sdeisr;
	uint32_t sdeimr;
	uint32_t sdeier;
	uint32_t shotplug;
	uint32_t filter;
	int status;

	/* Prepares the window's sleep. */
	drv_i915_completion_init(&hpd_test_sleep, "hpd-test-sleep");

	/* Refuses a path that is not running or has no HDMI connector. */
	drv_i915_hpd_summary(display, &summary);
	if (!summary.started || summary.hdmi_connector < 0) {
		kern_logf("i915: HPD-TEST verdict: FAIL (hotplug path not started or no HDMI connector)\n");
		return;
	}

	/* Logs the interrupt and hotplug registers the window starts from. */
	name = drv_i915_hpd_connector_name(display, (unsigned)summary.hdmi_connector);
	sdeisr = drv_i915_read32(mmio, I915_TEST_SDEISR);
	sdeimr = drv_i915_read32(mmio, I915_TEST_SDEIMR);
	sdeier = drv_i915_read32(mmio, I915_TEST_SDEIER);
	shotplug = drv_i915_read32(mmio, I915_TEST_SHOTPLUG_CTL_DDI);
	filter = drv_i915_read32(mmio, I915_TEST_SHPD_FILTER_CNT);
	kern_logf("i915: HPD-TEST start: %s (DDI B, pin 5) SDEISR=0x%08x (B live %u) SDEIMR=0x%08x SDEIER=0x%08x SHOTPLUG_CTL_DDI=0x%08x SHPD_FILTER_CNT=0x%08x\n",
	    name,
	    sdeisr,
	    (sdeisr >> I915_TEST_SDEISR_DDI_B_SHIFT) & 1u,
	    sdeimr,
	    sdeier,
	    shotplug,
	    filter);

	/* Detects the connector once before the window. */
	status = drv_i915_hpd_probe_connector(display, (unsigned)summary.hdmi_connector);
	kern_logf("i915: HPD-TEST initial detection: %s %s\n", name, i915_status_name(status));

	/* Lets the operator plug and unplug the cable. */
	kern_logf("i915: HPD-TEST window open: %u s -- plug / unplug the HDMI cable now\n", window_s);
	i915_hpd_test_window(display, window_s);

	/* Logs every interrupt and every detection the path recorded. */
	drv_i915_hpd_summary(display, &summary);
	i915_hpd_test_records(display, &summary);

	/*
	 * No DDI trigger is no event; both transitions without a storm or a
	 * warning pass; anything else fails.
	 */
	if (summary.ddi_triggers == 0u) {
		verdict = "NO-EVENT";
	} else if (summary.to_connected > 0u &&
		   summary.to_disconnected > 0u &&
		   summary.storms == 0u &&
		   summary.warnings == 0u) {
		verdict = "PASS";
	} else {
		verdict = "FAIL";
	}

	/* Logs the verdict with the counters it was made from. */
	kern_logf("i915: HPD-TEST verdict: %s (irqs=%u ddi_triggers=%u dropped=%u works=%u events=%u retries=%u to_connected=%u to_disconnected=%u storms=%u warnings=%u gmbus_irqs=%u status=%s epoch=%llu)\n",
	    verdict,
	    summary.irq_entries,
	    summary.ddi_triggers,
	    summary.irq_dropped,
	    summary.hotplug_works,
	    summary.hotplug_events,
	    summary.retries_armed,
	    summary.to_connected,
	    summary.to_disconnected,
	    summary.storms,
	    summary.warnings,
	    summary.gmbus_irqs,
	    i915_status_name(summary.hdmi_status),
	    summary.hdmi_epoch);
}

/* Waits out the window one second at a time, logging progress, and ends it early once it has seen enough. */
static void
i915_hpd_test_window(
	struct i915_display *display,
	unsigned window_s)
{
	struct i915_hpd_summary summary;
	uint64_t deadline;
	unsigned second;
	unsigned last_irq;
	unsigned quiet;

	/* Counts the seconds since the last interrupt, starting with none seen. */
	last_irq = 0u;
	quiet = 0u;
	for (second = 1u; second <= window_s; second++) {
		/* Sleeps one second. */
		deadline = sched_ticks() + KERN_CLOCK_HZ;
		(void)drv_i915_wait_for_completion(&hpd_test_sleep, deadline);

		/* A new interrupt restarts the quiet count. */
		drv_i915_hpd_summary(display, &summary);
		if (summary.irq_entries != last_irq) {
			last_irq = summary.irq_entries;
			quiet = 0u;
		} else {
			quiet++;
		}

		/* Logs the progress every few seconds. */
		if (second % I915_TEST_HPD_PROGRESS_S == 0u) {
			kern_logf("i915: HPD-TEST t=%us irqs=%u ddi_triggers=%u events=%u to_connected=%u to_disconnected=%u status=%s\n",
			    second,
			    summary.irq_entries,
			    summary.ddi_triggers,
			    summary.hotplug_events,
			    summary.to_connected,
			    summary.to_disconnected,
			    i915_status_name(summary.hdmi_status));
		}

		/* Both transitions seen and the line quiet for a while: nothing more is coming. */
		if (summary.to_connected > 0u &&
		    summary.to_disconnected > 0u &&
		    quiet >= I915_TEST_HPD_QUIET_S)
			break;
	}
}

/* Logs the hotplug interrupts and the encoder->hotplug() calls the path recorded. */
static void
i915_hpd_test_records(
	struct i915_display *display,
	const struct i915_hpd_summary *summary)
{
	const struct i915_hpd_irq_record *irq;
	const struct i915_hpd_hotplug_record *record;
	const char *name;
	unsigned i;

	/* Logs every hotplug interrupt with what its SHOTPLUG_CTL_DDI read said about B. */
	for (i = 0u; i < summary->irq_records; i++) {
		irq = drv_i915_hpd_irq_record(display, i);
		if (irq == NULL)
			break;
		kern_logf("i915: HPD-TEST irq[%u] t=%llu SDEIIR=0x%08x SHOTPLUG_CTL_DDI=0x%08x (B: %s%s) event_bits=0x%x\n",
		    i,
		    (unsigned long long)irq->tick,
		    irq->sde_iir,
		    irq->shotplug_ddi,
		    (irq->shotplug_ddi & I915_TEST_SHOTPLUG_B_LONG) != 0u ? "long" : "",
		    (irq->shotplug_ddi & I915_TEST_SHOTPLUG_B_SHORT) != 0u ? " short" : "",
		    irq->event_bits_after);
	}

	/* Logs every detection with its transition. */
	for (i = 0u; i < summary->hotplug_records; i++) {
		record = drv_i915_hpd_hotplug_record(display, i);
		if (record == NULL)
			break;
		name = drv_i915_hpd_connector_name(display, record->connector);
		kern_logf("i915: HPD-TEST hotplug[%u] t=%llu %s pin=%d retry=%d %s -> %s state=%d live=%d\n",
		    i,
		    (unsigned long long)record->tick,
		    name,
		    record->pin,
		    record->retries,
		    i915_status_name(record->old_status),
		    i915_status_name(record->new_status),
		    record->state,
		    record->live);
	}
}
