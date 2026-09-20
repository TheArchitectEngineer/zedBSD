/*
 * WS031 Linux-parity -- E-123: the OpRegion service on the FIRMWARE backend (the real shared region), authorised by
 * the user.  Runs after P3.2 and before N0 / any display write.  zedBSD project code.
 *
 * Connectors (for DIDL / CADL) come from the VBT's child devices by the reference intel_ddi_init() rules: a DP output
 * with the internal-connector bit is eDP, another DP output is DP; a TMDS output that is not NOT_HDMI (and not eDP) adds
 * an HDMI connector after the DP one; in port order.  RECORDED ADAPTATION: the driver's own connector objects do not
 * exist at P3.2 (they come with intel_setup_outputs at P5b).
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/sched.h>
#include <string.h>
#include <errno.h>
#include "../bios.h"
/* DEVICE_TYPE_* bits (vbt/intel_vbt_defs.h, reference values; that header is private to the parser) */
#define DEVICE_TYPE_INTERNAL_CONNECTOR (1 << 12)
#define DEVICE_TYPE_NOT_HDMI_OUTPUT (1 << 11)
#define DEVICE_TYPE_TMDS_DVI_SIGNALING (1 << 4)
#define DEVICE_TYPE_DISPLAYPORT_OUTPUT (1 << 2)
#include "../opregion_service.h"
#include "../backend_sync.h"
#include "parity_opregion.h"
#include "opregion_fwtest.h"

static struct parity_kworkqueue fw_wq;
static int fw_wq_live;
static unsigned fw_bl_calls;
static uint32_t fw_bl_level;
static int fw_ran, fw_rc;
static unsigned fw_ok, fw_want;
static uint32_t fw_seen[6];     /* after setup: CHPD ARDY | after register: DRDY ARDY | after unregister: DRDY ARDY */

/* the display is the firmware's at this point: a backlight request is recorded, not applied */
static void fw_backlight(void *ctx, uint32_t level, uint32_t max)
{
	(void)ctx; (void)max;
	fw_bl_calls++;
	fw_bl_level = level;
}

static void log_mbox(const char *when)
{
	kern_logf("i915: parity OPREGION-FW %s: ACPI drdy=0x%x csts=0x%x cevt=0x%x chpd=0x%x clid=0x%x didl=%x,%x,%x,%x cadl=%x,%x,%x,%x "
		"| ASLE ardy=0x%x aslc=0x%x tche=0x%x bclp=0x%x cblv=0x%x\n", when, parity_opregion_mbox_read(0x100),
		parity_opregion_mbox_read(0x104), parity_opregion_mbox_read(0x108), parity_opregion_mbox_read(0x1a8),
		parity_opregion_mbox_read(0x1ac), parity_opregion_mbox_read(0x120), parity_opregion_mbox_read(0x124),
		parity_opregion_mbox_read(0x128), parity_opregion_mbox_read(0x12c), parity_opregion_mbox_read(0x160),
		parity_opregion_mbox_read(0x164), parity_opregion_mbox_read(0x168), parity_opregion_mbox_read(0x16c),
		parity_opregion_mbox_read(0x300), parity_opregion_mbox_read(0x304), parity_opregion_mbox_read(0x308),
		parity_opregion_mbox_read(0x310), parity_opregion_mbox_read(0x318));
}

int parity_opregion_fw_test(uint32_t asls, const struct parity_vbt_state *vbt)
{
	struct parity_acpi_dispatch d;
	unsigned i, p, nconn = 0u, ok = 0u, want_ok = 0u, st, fi, qn, qp, unported, boundaries, unmaps;
	uint32_t ardy_reg, drdy_reg, csts;
	int rc;

	if (asls == 0u) {
		kern_logf("i915: parity OPREGION-FW skipped: ASLS=0 (no OpRegion: the VM)\n");
		return 0;
	}
	parity_acpi_notifier_init();
	if (!fw_wq_live) {
		if (parity_kworkqueue_create(&fw_wq, "parity-opregion-fw") != 0)
			return -1;
		fw_wq_live = 1;
	}
	rc = parity_opregion_firmware_setup(asls);
	kern_logf("i915: parity OPREGION-FW setup: mailbox_backend=%s rc=%d service_epoch=%u (ASLS 0x%08x mapped READ/WRITE)\n",
		parity_opregion_mailbox_backend(), rc, parity_opregion_service_epoch(), asls);
	if (rc != 0)
		return -1;
	log_mbox("after setup (CHPD 1, ARDY NOT_READY expected)");
	fw_ran = 1;
	fw_seen[0] = parity_opregion_mbox_read(0x1a8);
	fw_seen[1] = parity_opregion_mbox_read(0x300);
	want_ok++; ok += parity_opregion_mbox_read(0x1a8) == 1u && parity_opregion_mbox_read(0x300) == 0u;

	(void)parity_opregion_service_start(&fw_wq, 1 /* video: BCLP served (recorded) */);
	/* connectors from the VBT child devices, in port order, DP before HDMI (intel_ddi_init rules) */
	for (p = 0u; p < 16u; p++) {
		for (i = 0u; vbt != 0 && i < vbt->num_display_devices && i < 8u; i++) {
			const struct parity_vbt_child *c = &vbt->display_devices[i];
			uint32_t t = c->device_type;
			int dp = (t & DEVICE_TYPE_DISPLAYPORT_OUTPUT) != 0, edp = dp && (t & DEVICE_TYPE_INTERNAL_CONNECTOR) != 0;
			int hdmi = (t & DEVICE_TYPE_TMDS_DVI_SIGNALING) != 0 && (t & DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0 && !edp;

			if (c->port != p)
				continue;
			if (dp && parity_opregion_add_connector(edp ? 14 : 10, edp ? fw_backlight : 0, 0) == 0)
				nconn++;
			if (hdmi && parity_opregion_add_connector(11, 0, 0) == 0)
				nconn++;
			kern_logf("i915: parity OPREGION-FW connector from VBT child: port %u dvo_port %u device_type 0x%x -> %s%s\n", c->port,
				c->dvo_port, t, edp ? "eDP" : dp ? "DP" : "", hdmi ? (dp ? " + HDMI" : "HDMI") : "");
		}
	}

	parity_opregion_register();
	parity_opregion_counters(&unported, &boundaries, &unmaps);
	log_mbox("after register (DIDL / CADL, CSTS 0, DRDY 1, TCHE 2, ARDY 1 expected)");
	fw_seen[2] = parity_opregion_mbox_read(0x100);
	fw_seen[3] = parity_opregion_mbox_read(0x300);
	kern_logf("i915: parity OPREGION-FW register: %u connector(s), notifier registered %d, boundaries %u, unported accesses %u\n",
		nconn, parity_opregion_notifier_registered(), boundaries, unported);
	want_ok++; ok += parity_opregion_notifier_registered() && parity_opregion_mbox_read(0x104) == 0u &&
		parity_opregion_mbox_read(0x100) == 1u && parity_opregion_mbox_read(0x308) == 2u && parity_opregion_mbox_read(0x300) == 1u &&
		unported == 0u;

	/* synthetic ACPI video event (event_source SYNTHETIC) -- the callback writes CSTS in the real mailbox */
	parity_opregion_mbox_write(0x104, 0x55u);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	csts = parity_opregion_mbox_read(0x104);
	kern_logf("i915: parity OPREGION-FW notify: event_source=%s video 0x80 (CEVT 0x%x as the firmware left it) -> callback_result 0x%x "
		"dispatch_result %d, CSTS 0x55 -> 0x%x\n", d.event_source, parity_opregion_mbox_read(0x108), (unsigned)d.callback_result,
		d.dispatch_result, csts);
	want_ok++; ok += d.calls == 1u && csts == 0u;

	/* synthetic ASLE request: the test writes BCLP / ASLC as the firmware would, the real worker answers */
	parity_opregion_mbox_write(0x310, (1u << 31) | 128u);
	parity_opregion_mbox_write(0x304, 1u << 1);
	parity_opregion_gse_entry();
	(void)parity_opregion_asle_flush(sched_ticks() + 200u);
	parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
	kern_logf("i915: parity OPREGION-FW ASLE: event_source=SYNTHETIC(GSE entry) BCLP 128 -> ASLC 0x%x CBLV 0x%x | backlight request "
		"recorded %u (level %u; the display is the firmware's: not applied) | worker started %u finished %u\n",
		parity_opregion_mbox_read(0x304), parity_opregion_mbox_read(0x318), fw_bl_calls, fw_bl_level, st, fi);
	want_ok++; ok += parity_opregion_mbox_read(0x304) == 0u && parity_opregion_mbox_read(0x318) == (51u | (1u << 31)) && fi == 1u &&
		(nconn == 0u || fw_bl_calls == 1u);

	parity_opregion_unregister();
	ardy_reg = parity_opregion_mbox_read(0x300);
	drdy_reg = parity_opregion_mbox_read(0x100);
	fw_seen[4] = drdy_reg;
	fw_seen[5] = ardy_reg;
	log_mbox("after unregister (ARDY 0, DRDY 0 expected)");
	want_ok++; ok += ardy_reg == 0u && drdy_reg == 0u && !parity_opregion_notifier_registered();
	rc = parity_opregion_cleanup();
	parity_opregion_counters(&unported, &boundaries, &unmaps);
	kern_logf("i915: parity OPREGION-FW cleanup rc=%d backend %s | verdict: %s (%u/%u steps)\n", rc, parity_opregion_mailbox_backend(),
		ok == want_ok && rc == 0 ? "PASS" : "FAIL", ok, want_ok);
	fw_ok = ok;
	fw_want = want_ok;
	fw_rc = rc;
	return ok == want_ok && rc == 0 ? 0 : -1;
}

void parity_opregion_fw_log_again(void)
{
	if (!fw_ran)
		return;
	kern_logf("i915: parity OPREGION-FW summary (repeated): REAL OpRegion written | setup CHPD %u ARDY %u | register DRDY %u ARDY %u | "
		"unregister DRDY %u ARDY %u | synthetic notify + ASLE answered | cleanup rc %d | verdict %s (%u/%u)\n", fw_seen[0], fw_seen[1],
		fw_seen[2], fw_seen[3], fw_seen[4], fw_seen[5], fw_rc, fw_ok == fw_want && fw_rc == 0 ? "PASS" : "FAIL", fw_ok, fw_want);
}
