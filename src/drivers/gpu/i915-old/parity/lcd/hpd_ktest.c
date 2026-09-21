/*
 * WS031 E-123 -- HPD model tests (GPU-free): the generated hotplug chain on fake SHOTPLUG_CTL_DDI / SDEISR registers,
 * with the real kernel work queue, timer queue, spinlock and mutex.  zedBSD project code.
 *   HPD-DECODE   SDEIIR bit 17 + SHOTPLUG B long -> pin 5, the write-back clears the status, the work runs
 *   HPD-PLUG     live status set -> HDMI-A-1 disconnected -> connected (CHANGED, epoch +1, no retry)
 *   HPD-RETRY    an interrupt without a status change -> UNCHANGED -> RETRY once after 1000 ms, then UNCHANGED
 *   HPD-EDID     the plug detection reads the sink's EDID over the GMBUS model (index read at 0x50, header / checksum)
 *   HPD-EDIDCHG  a different EDID behind the same live status: epoch +1 -> CHANGED (connected -> connected)
 *   HPD-UNPLUG   live status clear -> connected -> disconnected (CHANGED), no DDC access
 *   HPD-NODDC    live status set but no DDC answer (NAK, retried once): disconnected
 *   HPD-EDP      pin 4 (eDP, has hpd_pulse) -> the dig-port work (step), no hotplug work for the pin
 *   HPD-STORM    6 long pulses within the period -> MARK_DISABLED, irq_setup, the work switches the connector to
 *                polling; the re-enable work restores HPD
 *   HPD-GATE     after stop, an interrupt is dropped (no register access)
 */
#include <stdint.h>
#include <string.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "../backend_sync.h"
#include "../driver_probe.h"
#include "../display_nogem.h"
#include "../pch.h"
#include "parity_hotplug.h"

static struct parity_hotplug khp;
static struct parity_display_nogem kng;
static struct parity_hpd_fake_regs kfake;
static struct parity_kcompletion ksleep;
static uint8_t kedid[128];

/* a plain EDID 1.4 block: digital input, one detailed timing (1920x1080 148.5 MHz), valid checksum */
static void make_edid(uint32_t serial)
{
	static const uint8_t head[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	unsigned i, sum = 0u;

	memset(kedid, 0, sizeof(kedid));
	memcpy(kedid, head, 8);
	kedid[8] = 0x68; kedid[9] = 0xa4;                  /* "ZED": 0 11010 00101 00100 */
	kedid[10] = 0x34; kedid[11] = 0x12;
	kedid[12] = (uint8_t)serial; kedid[13] = (uint8_t)(serial >> 8);
	kedid[14] = (uint8_t)(serial >> 16); kedid[15] = (uint8_t)(serial >> 24);
	kedid[18] = 1; kedid[19] = 4; kedid[20] = 0x80;
	kedid[54] = 0x02; kedid[55] = 0x3a;                /* 14850 x 10 kHz */
	kedid[56] = 0x80; kedid[58] = 0x70;                /* hactive 0x780 */
	kedid[59] = 0x38; kedid[61] = 0x40;                /* vactive 0x438 */
	for (i = 0u; i < 127u; i++)
		sum += kedid[i];
	kedid[127] = (uint8_t)(256u - (sum & 0xffu));
}

static void ksleep_ticks(unsigned t)
{
	(void)parity_kwait(&ksleep, sched_ticks() + t);
}

/* waits until `n` hotplug records exist (bounded) */
static int wait_records(unsigned n, unsigned ticks)
{
	struct parity_hpd_summary s;
	unsigned waited = 0u;

	for (;;) {
		parity_hpd_summary(&s);
		if (s.hotplug_records >= n)
			return 1;
		if (waited >= ticks)
			return 0;
		ksleep_ticks(2u);
		waited += 2u;
	}
}

static void set_encoder(unsigned i, int port, int phy, int tc, int hdmi, int dp, uint32_t type)
{
	kng.encoders[i].port = port;
	kng.encoders[i].phy = phy;
	kng.encoders[i].is_tc = tc;
	kng.encoders[i].init_hdmi = hdmi;
	kng.encoders[i].init_dp = dp;
	kng.encoders[i].device_type = type;
}

void parity_hpd_ktest(parity_hpd_ktest_check check)
{
	struct parity_hpd_summary s;
	const struct parity_hpd_hotplug_record *r;
	unsigned i, base;
	int hdmi, st;

	if (!parity_hpd_model_allowed()) {
		/* the device's own hotplug path has run in this boot: its interrupts would enter this model instance */
		kern_logf("i915: parity hpd model tests skipped: the device's hotplug path ran in this boot (the model must "
			"not share the state the interrupt handler reaches)\n");
		return;
	}
	parity_kcompletion_init(&ksleep, "hpd-ktest-sleep");
	memset(&khp, 0, sizeof(khp));
	memset(&kng, 0, sizeof(kng));
	memset(&kfake, 0, sizeof(kfake));
	/* the Latitude 5330's outputs: eDP on A, HDMI on B, DP on TC1 / TC2 */
	set_encoder(0u, 0, 0, 0, 0, 1, 0x1806u);
	set_encoder(1u, 1, 1, 0, 1, 0, 0x60d2u);
	set_encoder(2u, 3, 5, 1, 0, 1, 0x68c6u);
	set_encoder(3u, 4, 6, 1, 0, 1, 0x68c6u);
	kng.num_encoders = 4u;
	parity_intel_hpd_init_pins(&khp, 13, PARITY_PCH_ADP);
	check(khp.pch_hpd[5] == 0x20000u && khp.pch_hpd[4] == 0x10000u, "hpd: HPD-PINS pch_hpd[B] = SDE bit 17, [A] = bit 16");
	kfake.sdeisr = 0x10000u;            /* eDP live, HDMI not */
	kfake.shotplug_ddi = 0x88u;         /* A and B enabled, no status */
	make_edid(0x1u);
	kfake.ddc_edid = kedid;
	kfake.ddc_edid_len = sizeof(kedid);
	kfake.ddc_present = 1;
	check(parity_hpd_start(&khp, NULL, &kng, NULL, NULL, PARITY_PCH_ADP, 1, &kfake) == 0, "hpd: HPD-START model backend");
	parity_hpd_summary(&s);
	hdmi = s.hdmi_connector;
	check(s.num_connectors == 4u && hdmi == 1, "hpd: HPD-OBJECTS 4 connectors, HDMI-A-1 on DDI B");
	check(hdmi >= 0 && strcmp(parity_hpd_connector_name((unsigned)hdmi), "HDMI-A-1") == 0 &&
		strcmp(parity_hpd_connector_name(0u), "eDP-1") == 0 && strcmp(parity_hpd_connector_name(2u), "DP-1") == 0,
		"hpd: HPD-NAMES eDP-1 HDMI-A-1 DP-1");
	if (hdmi < 0)
		return;
	st = parity_hpd_probe_connector((unsigned)hdmi);
	parity_hpd_summary(&s);
	check(st == 2 && s.hdmi_epoch == 1ull, "hpd: HPD-INIT first detection: disconnected (live status clear), epoch 1");

	/* ---- plug ---- */
	kfake.sdeisr = 0x30000u;
	kfake.shotplug_ddi = 0xa8u;         /* B: enable + long */
	parity_hpd_model_irq(0x20000u);
	check(kfake.rmw_writes == 1u && kfake.last_rmw_write == 0xa8u && kfake.shotplug_ddi == 0x88u,
		"hpd: HPD-DECODE the rmw wrote the read value back (status cleared, enables kept)");
	check(parity_hpd_irq_record(0u) != NULL && parity_hpd_irq_record(0u)->shotplug_ddi == 0xa8u &&
		(parity_hpd_irq_record(0u)->event_bits_after & (1u << 5)) != 0u,
		"hpd: HPD-DECODE pin 5 in event_bits (no hpd_pulse on HDMI: the hotplug work handles it)");
	check(wait_records(1u, 100u), "hpd: HPD-PLUG the hotplug work ran");
	r = parity_hpd_hotplug_record(0u);
	check(r != NULL && r->pin == 5 && r->retries == 0 && r->old_status == 2 && r->new_status == 1 && r->state == 1 &&
		r->live == 1, "hpd: HPD-PLUG disconnected -> connected, CHANGED, retry 0");
	ksleep_ticks(20u);
	parity_hpd_summary(&s);
	check(s.to_connected == 1u && s.hotplug_records == 1u && parity_hpd_retry_bits() == 0u && s.hdmi_epoch == 2ull,
		"hpd: HPD-PLUG no retry armed after a change, epoch 2");
	{
		struct parity_hpd_edid_info ei;
		unsigned sz = 0u;
		const uint8_t *eb = parity_hpd_edid_bytes((unsigned)hdmi, &sz);

		parity_hpd_edid_info(&ei);
		check(ei.rc == 1 && ei.blocks == 1u && ei.digital == 1u && ei.mfg[0] == 'Z' && ei.mfg[1] == 'E' &&
			ei.mfg[2] == 'D' && ei.product == 0x1234u && ei.hactive == 1920u && ei.vactive == 1080u &&
			ei.pixel_clock_khz == 148500u, "hpd: HPD-EDID base block over GMBUS: ZED 0x1234, digital, DTD1 1920x1080 148.5 MHz");
		check(eb != NULL && sz == 128u && memcmp(eb, kedid, 128) == 0 && kfake.gm_reads >= 32u && kfake.gm_naks == 0u,
			"hpd: HPD-EDID the 128 bytes equal the sink's (32+ GMBUS3 reads, no NAK)");
	}

	/* ---- an interrupt without a change: the second detection pass ---- */
	base = s.hotplug_records;
	kfake.shotplug_ddi = 0xa8u;
	parity_hpd_model_irq(0x20000u);
	check(wait_records(base + 2u, 300u), "hpd: HPD-RETRY two detections (event + retry after HPD_RETRY_DELAY)");
	r = parity_hpd_hotplug_record(base);
	check(r != NULL && r->retries == 0 && r->state == 2, "hpd: HPD-RETRY first pass UNCHANGED -> RETRY");
	r = parity_hpd_hotplug_record(base + 1u);
	check(r != NULL && r->retries == 1 && r->state == 0 && r->tick >= parity_hpd_hotplug_record(base)->tick + 90u,
		"hpd: HPD-RETRY second pass ~1000 ms later, retry 1, UNCHANGED (no further retry)");
	ksleep_ticks(20u);
	parity_hpd_summary(&s);
	check(s.hotplug_records == base + 2u && s.retries_armed == 1u, "hpd: HPD-RETRY exactly one retry");

	/* ---- a different EDID behind the same live status ---- */
	base = s.hotplug_records;
	make_edid(0x2u);
	kfake.shotplug_ddi = 0xa8u;
	parity_hpd_model_irq(0x20000u);
	check(wait_records(base + 1u, 100u), "hpd: HPD-EDIDCHG the hotplug work ran");
	r = parity_hpd_hotplug_record(base);
	check(r != NULL && r->old_status == 1 && r->new_status == 1 && r->state == 1,
		"hpd: HPD-EDIDCHG connected -> connected, CHANGED (the EDID changed: epoch +1)");
	ksleep_ticks(20u);
	parity_hpd_summary(&s);
	check(s.hdmi_epoch == 3ull && s.hotplug_records == base + 1u, "hpd: HPD-EDIDCHG epoch 3, no retry after a change");

	/* ---- unplug ---- */
	base = s.hotplug_records;
	i = kfake.gm_reads;
	kfake.sdeisr = 0x10000u;
	kfake.shotplug_ddi = 0xa8u;
	parity_hpd_model_irq(0x20000u);
	check(wait_records(base + 1u, 100u), "hpd: HPD-UNPLUG the hotplug work ran");
	r = parity_hpd_hotplug_record(base);
	check(r != NULL && r->old_status == 1 && r->new_status == 2 && r->state == 1 && r->live == 0,
		"hpd: HPD-UNPLUG connected -> disconnected, CHANGED");
	parity_hpd_summary(&s);
	check(s.to_disconnected == 1u && parity_hpd_connector_status((unsigned)hdmi) == 2 && kfake.gm_reads == i,
		"hpd: HPD-UNPLUG status disconnected, the DDC not touched (live status gate)");

	/* ---- live status set, no DDC answer ---- */
	ksleep_ticks(20u);
	parity_hpd_summary(&s);
	base = s.hotplug_records;
	kfake.ddc_present = 0;
	kfake.sdeisr = 0x30000u;
	kfake.shotplug_ddi = 0xa8u;
	parity_hpd_model_irq(0x20000u);
	check(wait_records(base + 1u, 300u), "hpd: HPD-NODDC the detection ran");
	r = parity_hpd_hotplug_record(base);
	/* the EDID read fails (NAK at 0x50, the first message retried once, then the bit-banging step): the stored EDID
	 * goes away (drm_edid_connector_update(NULL)), so the epoch moves although the status stays disconnected */
	check(r != NULL && r->old_status == 2 && r->new_status == 2 && r->state == 1 && r->live == 1 && kfake.gm_naks == 2u,
		"hpd: HPD-NODDC NAK at 0x50 (retried once): disconnected, EDID dropped -> epoch +1, CHANGED");
	kfake.sdeisr = 0x10000u;
	kfake.ddc_present = 1;
	ksleep_ticks(20u);

	/* ---- eDP pin: the dig-port path ---- */
	ksleep_ticks(20u);
	parity_hpd_summary(&s);
	base = s.hotplug_records;
	kfake.shotplug_ddi = 0x8au;         /* A: enable + long */
	parity_hpd_model_irq(0x10000u);
	ksleep_ticks(30u);
	parity_hpd_summary(&s);
	check(s.digport_works >= 1u && s.hpd_pulse_steps == 1u && s.hotplug_records == base,
		"hpd: HPD-EDP pin 4 goes to the dig-port work (hpd_pulse), not to the hotplug work");

	/* ---- storm ---- */
	/* until the pin leaves ENABLED (at most 6: 6 x 10 > 50); none after that, so no pulse meets a DISABLED pin */
	for (i = 0u; i < 6u && parity_hpd_pin_state(5) == 0; i++) {
		kfake.shotplug_ddi = 0xa8u;
		parity_hpd_model_irq(0x20000u);
	}
	check(parity_hpd_pin_state(5) != 0 && khp.state[5] != 0, "hpd: HPD-STORM pin 5 no longer enabled, irq_setup masked it");
	/* the hotplug work switches the pin to polling when it next runs (it may be busy with a retry): bounded wait */
	for (i = 0u; i < 150u && parity_hpd_pin_state(5) != 1; i++)
		ksleep_ticks(2u);
	parity_hpd_summary(&s);
	check(parity_hpd_pin_state(5) == 1 && parity_hpd_connector_polled((unsigned)hdmi) == 6 && s.storms == 1u,
		"hpd: HPD-STORM the work switched HDMI-A-1 to polling (connect | disconnect), pin DISABLED");
	/* the same work arms the re-enable work just after it switched the pin: flush once it is armed (bounded) */
	for (i = 0u; i < 150u && (st = parity_hpd_flush_reenable()) != 1; i++)
		ksleep_ticks(2u);
	check(st == 1, "hpd: HPD-STORM the armed re-enable work ran (flushed)");
	parity_hpd_summary(&s);
	check(parity_hpd_pin_state(5) == 0 && parity_hpd_connector_polled((unsigned)hdmi) == 1 && khp.state[5] == 0 &&
		s.reenable_works == 1u, "hpd: HPD-STORM re-enabled: pin ENABLED, HPD polling mode, register state enabled");
	/* the storm's hotplug works may still be retrying: let them finish before the stop */
	ksleep_ticks(150u);

	/* ---- stop: the entry is closed ---- */
	parity_hpd_stop();
	i = kfake.rmw_writes;
	parity_hpd_model_irq(0x20000u);
	parity_hpd_summary(&s);
	check(s.irq_dropped == 1u && kfake.rmw_writes == i && s.warnings == 0u,
		"hpd: HPD-GATE after stop the interrupt is dropped; no WARN in the whole run");
}
