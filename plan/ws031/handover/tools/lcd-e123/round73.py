#!/usr/bin/env python3
"""WS031 E-123 round 73: the OpRegion service on the FIRMWARE backend (the user authorised writing the real OpRegion
and declaring READY).  -DPARITY_OPREGION_FW_TEST=1: right after P3.2 (VBT parsed) and before N0 / any display write,
the reference lifecycle runs on the real shared region: setup (CHPD 1, ARDY NOT_READY) -> register (DIDL / CADL from the
VBT's child devices, CSTS 0, DRDY 1, TCHE BLC_EN, ARDY READY) -> synthetic ACPI video event -> synthetic ASLE request
(the test writes BCLP / ASLC as the firmware would, the real worker answers; the display is the firmware's, so the
backlight request is recorded, not applied) -> unregister (ARDY 0, worker synced, DRDY 0) -> cleanup (real unmaps).
Real GSE interrupts are not dispatched yet (the kworkqueue's lock is not IRQ-safe); IRQs are not installed at P3.2.
usage: round73.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

g = open(L + "parity_opregion_glue.inc").read()
g = rep(g, '#include "parity_opregion.h"', '#include "parity_opregion.h"' + NL + '#include <hal/hal.h>')
g = rep(g, "static unsigned parity_opregion_dropped, parity_opregion_cleanup_refused;",
        "static unsigned parity_opregion_dropped, parity_opregion_cleanup_refused;" + NL +
        "static struct { void *va; size_t size; } parity_opregion_hal[2];   /* FIRMWARE: the real HAL mappings */" + NL +
        "static unsigned parity_opregion_nhal;")
g = rep(g, """	intel_opregion_cleanup(&parity_opregion_dev);
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
	return 0;
}""", """	intel_opregion_cleanup(&parity_opregion_dev);
	while (parity_opregion_nhal > 0u) {
		parity_opregion_nhal--;
		(void)hal_space_unmap_device(parity_opregion_hal[parity_opregion_nhal].va, parity_opregion_hal[parity_opregion_nhal].size);
	}
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
	return 0;
}

/*
 * The FIRMWARE backend: the real OpRegion at ASLS, mapped READ / WRITE (uncached device view), and the RVDA VBT it
 * names (read).  Then the reference intel_opregion_setup() on it -- from here the driver writes the shared mailboxes.
 */
int parity_opregion_firmware_setup(uint32_t asls)
{
	void *op = 0, *vb = 0;
	uint64_t rvda = 0u;
	uint32_t rvds = 0u, major, minor;
	int rc;

	if (asls == 0u)
		return -ENODEV;
	if (parity_opregion_dev.display.opregion.acpi_notifier.notifier_call != 0 || parity_opregion_nhal != 0u)
		return -EBUSY;
	parity_opregion_nmaps = 0u;
	if (hal_space_map_device((hal_physaddr_t)asls, OPREGION_SIZE, HAL_SPACE_READ | HAL_SPACE_WRITE, &op) != HAL_OK || op == 0)
		return -ENOMEM;
	parity_opregion_hal[parity_opregion_nhal].va = op;
	parity_opregion_hal[parity_opregion_nhal].size = OPREGION_SIZE;
	parity_opregion_nhal++;
	(void)parity_opregion_shadow_map(asls, op, OPREGION_SIZE);
	/* the RVDA the reference will memremap: mapped here (read) at the address it will compute */
	major = ((const volatile u8 *)op)[0x17];
	minor = ((const volatile u8 *)op)[0x16];
	memcpy(&rvda, (const u8 *)op + OPREGION_ASLE_OFFSET + 186, 8);
	memcpy(&rvds, (const u8 *)op + OPREGION_ASLE_OFFSET + 194, 4);
	if (major >= 2u && rvda != 0u && rvds != 0u) {
		uint64_t phys = (major > 2u || minor >= 1u) ? (uint64_t)asls + rvda : rvda;

		if (hal_space_map_device((hal_physaddr_t)phys, rvds, HAL_SPACE_READ, &vb) == HAL_OK && vb != 0) {
			parity_opregion_hal[parity_opregion_nhal].va = vb;
			parity_opregion_hal[parity_opregion_nhal].size = rvds;
			parity_opregion_nhal++;
			(void)parity_opregion_shadow_map(phys, vb, rvds);
		}
	}
	memset(&parity_opregion_dev.display.opregion, 0, sizeof(parity_opregion_dev.display.opregion));
	parity_opregion_dev.display.params.vbt_firmware = NULL;
	parity_opregion_asls = asls;
	parity_opregion_backend = "FIRMWARE";
	parity_opregion_epoch++;
	rc = intel_opregion_setup(&parity_opregion_dev);
	if (rc != 0) {
		parity_opregion_backend = "NONE";
		while (parity_opregion_nhal > 0u) {
			parity_opregion_nhal--;
			(void)hal_space_unmap_device(parity_opregion_hal[parity_opregion_nhal].va, parity_opregion_hal[parity_opregion_nhal].size);
		}
	}
	return rc;
}

/* a read of the bound instance's mailbox (either backend), for the records */
uint32_t parity_opregion_mbox_read(unsigned off)
{
	u32 v = 0u;

	if (parity_opregion_dev.display.opregion.header != 0 && off + 4u <= OPREGION_SIZE)
		memcpy(&v, (const u8 *)parity_opregion_dev.display.opregion.header + off, 4);
	return v;
}

void parity_opregion_mbox_write(unsigned off, uint32_t v)
{
	if (parity_opregion_dev.display.opregion.header != 0 && off + 4u <= OPREGION_SIZE)
		memcpy((u8 *)parity_opregion_dev.display.opregion.header + off, &v, 4);
}""")
open(L + "parity_opregion_glue.inc", "w").write(g)

h = open(L + "parity_opregion.h").read()
h = rep(h, "void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused);",
        "void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused);" + NL +
        "/* E-123: the FIRMWARE backend (the real OpRegion, written) and mailbox access for the records / the synthetic firmware role */" + NL +
        "int parity_opregion_firmware_setup(uint32_t asls);" + NL +
        "uint32_t parity_opregion_mbox_read(unsigned off);" + NL +
        "void parity_opregion_mbox_write(unsigned off, uint32_t v);")
open(L + "parity_opregion.h", "w").write(h)

open(L + "opregion_fwtest.h", "w").write(r"""/* WS031 E-123: the OpRegion service on the real OpRegion (-DPARITY_OPREGION_FW_TEST=1).  zedBSD project code. */
#ifndef PARITY_OPREGION_FWTEST_H
#define PARITY_OPREGION_FWTEST_H

#include <stdint.h>

struct parity_vbt_state;
/* runs only when ASLS != 0; returns 0 when every step matched, -1 otherwise (the probe continues either way) */
int parity_opregion_fw_test(uint32_t asls, const struct parity_vbt_state *vbt);

#endif
""")
open(L + "opregion_fwtest.c", "w").write(r"""/*
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
#include "../vbt/intel_vbt_defs.h"
#include "../opregion_service.h"
#include "../backend_sync.h"
#include "parity_opregion.h"
#include "opregion_fwtest.h"

static struct parity_kworkqueue fw_wq;
static int fw_wq_live;
static unsigned fw_bl_calls;
static uint32_t fw_bl_level;

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
	log_mbox("after unregister (ARDY 0, DRDY 0 expected)");
	want_ok++; ok += ardy_reg == 0u && drdy_reg == 0u && !parity_opregion_notifier_registered();
	rc = parity_opregion_cleanup();
	parity_opregion_counters(&unported, &boundaries, &unmaps);
	kern_logf("i915: parity OPREGION-FW cleanup rc=%d backend %s | verdict: %s (%u/%u steps)\n", rc, parity_opregion_mailbox_backend(),
		ok == want_ok && rc == 0 ? "PASS" : "FAIL", ok, want_ok);
	return ok == want_ok && rc == 0 ? 0 : -1;
}
""")

b = open(P + "bios.h").read()
if "PARITY_OPREGION_FW_TEST" not in b:
    b = rep(b, "#ifndef PARITY_LCDO_TEST", "#ifndef PARITY_OPREGION_FW_TEST" + NL +
            "#define PARITY_OPREGION_FW_TEST 0     /* E-123: the OpRegion service on the REAL OpRegion (native; writes the mailboxes) */" + NL +
            "#endif" + NL + "#ifndef PARITY_LCDO_TEST")
    open(P + "bios.h", "w").write(b)
pr = open(P + "probe.c").read()
pr = rep(pr, '#include "native_precheck.h"' + NL, '#include "native_precheck.h"' + NL + '#include "lcd/opregion_fwtest.h"' + NL)
old = "		(void)parity_intel_bios_init_ex(&vbt_state, &pci, opregion_vbt_present, PARITY_VBT_EXPLICIT, &trace);" + NL
pr = rep(pr, old, old + "		/* E-123: the OpRegion service on the real OpRegion (test builds only; before N0 and any display write) */" + NL +
         "		if (PARITY_OPREGION_FW_TEST)" + NL + "			(void)parity_opregion_fw_test(osdep_pci_read32(&pci, 0xFCu), &vbt_state);" + NL)
open(P + "probe.c", "w").write(pr)
mk = open(root + "platform/amd64/vmunix.mk").read()
if "opregion_fwtest.c" not in mk:
    mk = rep(mk, "src/drivers/gpu/i915/parity/lcd/opregion_ktest.c", "src/drivers/gpu/i915/parity/lcd/opregion_ktest.c src/drivers/gpu/i915/parity/lcd/opregion_fwtest.c")
    open(root + "platform/amd64/vmunix.mk", "w").write(mk)
print("done")
