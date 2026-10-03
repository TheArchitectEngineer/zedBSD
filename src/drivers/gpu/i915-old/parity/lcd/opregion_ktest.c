/*
 * WS031 Linux-parity -- the OpRegion service on a SHADOW mailbox (E-122): OP-LIFECYCLE (setup -> register -> resume ->
 * notify / ASLE -> unregister -> cleanup), OP-NOTIFY (synthetic ACPI video events), OP-ASLE (synthetic GSE entry, real
 * worker, reference request handlers).  The reference functions (generated intel_opregion_port.c / intel_acpi_port.c),
 * the notifier chain (opregion_service.c) and the worker queue are production code; the test owns the mailbox memory,
 * the event source and a fake backlight.  The firmware's region is never written (production: VBT_ONLY).
 * zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/sched.h>
#include <string.h>
#include <errno.h>
#include "../opregion_service.h"
#include "../backend_sync.h"
#include "parity_opregion.h"
#include "opregion_ktest.h"

static uint8_t shadow[8192] __attribute__((aligned(4096)));
static uint8_t shadow_vbt[96] __attribute__((aligned(64)));
#define ASLS_TOKEN 0x6f000018u          /* an unaligned "physical" token, like the target's 0x614e5018 */

static uint32_t rd32(unsigned off) { uint32_t v; memcpy(&v, shadow + off, 4); return v; }
static void wr32(unsigned off, uint32_t v) { memcpy(shadow + off, &v, 4); }
static void wr16v(uint8_t *b, unsigned off, uint16_t v) { memcpy(b + off, &v, 2); }

/* struct opregion_acpi at 0x100, struct opregion_asle at 0x300 */
#define DRDY 0x100u
#define CSTS 0x104u
#define CEVT 0x108u
#define DIDL 0x120u
#define CADL 0x160u
#define CHPD 0x1a8u
#define ASLE_ARDY 0x300u
#define ASLE_ASLC 0x304u
#define ASLE_TCHE 0x308u
#define ASLE_ALSI 0x30cu
#define ASLE_BCLP 0x310u
#define ASLE_CBLV 0x318u
#define ASLE_RVDA 0x3bau
#define ASLE_RVDS 0x3c2u

static void shadow_init(uint32_t mboxes)
{
	static const char sig[16] = { 'I','n','t','e','l','G','r','a','p','h','i','c','s','M','e','m' };

	memset(shadow, 0, sizeof(shadow));
	memcpy(shadow, sig, 16);
	shadow[0x10] = 8u;                      /* size: 8 KiB */
	shadow[0x16] = 1u;                      /* over.minor */
	shadow[0x17] = 2u;                      /* over.major: 2.1 */
	memcpy(shadow + 0x58, &mboxes, 4);
}

/* a minimal valid VBT: struct vbt_header (48 bytes) + struct bdb_header (22 bytes) */
static void shadow_vbt_init(void)
{
	static const char vsig[20] = { '$','V','B','T',' ','S','H','A','D','O','W',' ',' ',' ',' ',' ',' ',' ',' ',' ' };
	static const char bsig[16] = { 'B','I','O','S','_','D','A','T','A','_','B','L','O','C','K',' ' };
	uint32_t bdb = 48u;

	memset(shadow_vbt, 0, sizeof(shadow_vbt));
	memcpy(shadow_vbt, vsig, 20);
	wr16v(shadow_vbt, 20, 100);             /* version */
	wr16v(shadow_vbt, 22, 48);              /* header_size */
	wr16v(shadow_vbt, 24, 96);              /* vbt_size */
	memcpy(shadow_vbt + 28, &bdb, 4);       /* bdb_offset */
	memcpy(shadow_vbt + 48, bsig, 16);
	wr16v(shadow_vbt, 64, 249);             /* bdb version */
	wr16v(shadow_vbt, 66, 22);              /* bdb header_size */
	wr16v(shadow_vbt, 68, 48);              /* bdb_size */
}

static int other_calls;
static int other_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	(void)nb; (void)action; (void)data;
	other_calls++;
	return NOTIFY_OK;
}

static void log_dispatch(const char *id, const struct parity_acpi_dispatch *d, uint32_t before, uint32_t after)
{
	kern_logf("i915: parity OP-NOTIFY %s mailbox_backend=%s service_epoch=%u event_source=%s callback_result=0x%x "
		"dispatch_result=%d calls=%u request_before csts=0x%x response_after csts=0x%x real_opregion_write_count=0\n", id,
		parity_opregion_mailbox_backend(), parity_opregion_service_epoch(), d->event_source, (unsigned)d->callback_result,
		d->dispatch_result, d->calls, before, after);
}

/* ---------------- ASLE ---------------- */
#define ASLC_SET_ALS_ILLUM (1u << 0)
#define ASLC_SET_BACKLIGHT (1u << 1)
#define ASLC_SET_PFIT (1u << 2)
#define ASLC_ALS_ILLUM_FAILED (1u << 10)
#define ASLC_BACKLIGHT_FAILED (1u << 12)
#define ASLC_PFIT_FAILED (1u << 14)
#define BCLP_VALID (1u << 31)
#define CBLV_VALID (1u << 31)

static struct parity_kworkqueue asle_wq;
static int asle_wq_live;
static unsigned bl_calls;
static uint32_t bl_level, bl_max;

static void fake_backlight(void *ctx, uint32_t level, uint32_t max)
{
	(void)ctx;
	bl_calls++;
	bl_level = level;
	bl_max = max;
}

/* the test acts as the firmware: payload, then the request word, then the GSE entry; then it waits for the worker */
static void asle_request(uint32_t aslc, uint32_t bclp)
{
	wr32(ASLE_BCLP, bclp);
	wr32(ASLE_ASLC, aslc);
	parity_opregion_gse_entry();
	(void)parity_opregion_asle_flush(sched_ticks() + 200u);
}

static void log_asle(const char *id, uint32_t req, unsigned calls0)
{
	unsigned st, fi, qn, qp;

	parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
	kern_logf("i915: parity OP-ASLE %s mailbox_backend=%s service_epoch=%u event_source=SYNTHETIC(GSE entry) "
		"display_backend=MODEL request_before aslc=0x%x | response_after aslc=0x%x cblv=0x%x | backlight calls +%u level %u/%u "
		"| worker started %u finished %u queued new %u pending %u | real_opregion_write_count=0\n", id,
		parity_opregion_mailbox_backend(), parity_opregion_service_epoch(), req, rd32(ASLE_ASLC), rd32(ASLE_CBLV),
		bl_calls - calls0, bl_level, bl_max, st, fi, qn, qp);
}

static void asle_tests(parity_opregion_ktest_check check)
{
	unsigned c0, st, fi, qn, qp;

	c0 = bl_calls;
	asle_request(ASLC_SET_BACKLIGHT, BCLP_VALID | 128u);
	log_asle("backlight-128", ASLC_SET_BACKLIGHT, c0);
	parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
	check(bl_calls == c0 + 1u && bl_level == 128u && bl_max == 255u && rd32(ASLE_ASLC) == 0u &&
		rd32(ASLE_CBLV) == (51u | CBLV_VALID) && st == 1u && fi == 1u && qn == 1u,
		"opregion: OP-ASLE-BCLP a valid request: the real worker ran once, intel_backlight_set_acpi(128, 255), "
		"CBLV = DIV_ROUND_UP(128*100,255) | valid, ASLC = 0 (success)");

	c0 = bl_calls;
	wr32(ASLE_CBLV, 0x1234u);
	asle_request(ASLC_SET_BACKLIGHT, 128u);                 /* no valid bit */
	log_asle("backlight-no-valid", ASLC_SET_BACKLIGHT, c0);
	check(bl_calls == c0 && rd32(ASLE_ASLC) == ASLC_BACKLIGHT_FAILED && rd32(ASLE_CBLV) == 0x1234u,
		"opregion: OP-ASLE-INVALID no valid bit: no backlight change, ASLC = BACKLIGHT_FAILED, CBLV untouched");

	c0 = bl_calls;
	asle_request(ASLC_SET_BACKLIGHT, BCLP_VALID | 300u);    /* out of range */
	check(bl_calls == c0 && rd32(ASLE_ASLC) == ASLC_BACKLIGHT_FAILED,
		"opregion: OP-ASLE-RANGE a level above 255: no backlight change, ASLC = BACKLIGHT_FAILED");

	c0 = bl_calls;
	parity_opregion_set_policy(PARITY_OPREGION_POLICY_NATIVE);
	asle_request(ASLC_SET_BACKLIGHT, BCLP_VALID | 200u);
	log_asle("backlight-native-policy", ASLC_SET_BACKLIGHT, c0);
	check(bl_calls == c0 && rd32(ASLE_ASLC) == 0u,
		"opregion: OP-ASLE-NATIVE policy native: the reference ignores the request and answers success (no PWM change)");
	parity_opregion_set_policy(PARITY_OPREGION_POLICY_VIDEO);

	c0 = bl_calls;
	wr32(ASLE_ALSI, 500u);
	asle_request(ASLC_SET_BACKLIGHT | ASLC_SET_ALS_ILLUM | ASLC_SET_PFIT, BCLP_VALID | 64u);
	log_asle("mixed", ASLC_SET_BACKLIGHT | ASLC_SET_ALS_ILLUM | ASLC_SET_PFIT, c0);
	check(bl_calls == c0 + 1u && bl_level == 64u && rd32(ASLE_ASLC) == (ASLC_ALS_ILLUM_FAILED | ASLC_PFIT_FAILED),
		"opregion: OP-ASLE-MIXED backlight served, ALS and pfit answered FAILED as the reference does: the status is the OR");

	c0 = bl_calls;
	asle_request(1u << 9, BCLP_VALID | 10u);                /* no request bit inside ASLC_REQ_MSK */
	check(bl_calls == c0 && rd32(ASLE_ASLC) == (1u << 9),
		"opregion: OP-ASLE-NOREQ no known request bit: the worker returns without an answer (ASLC unchanged)");
}

/* ---------------- OP-LIFECYCLE (unit 3): stop while busy, stale requests, failed setup, re-initialisation ------------- */
static struct parity_kcompletion entered_c, release_c;
static volatile int slow_finished, slow_calls, bl_block, bl_block_finished;
static struct parity_kwork dispatch_work, blocker_work;

static int slow_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	(void)nb; (void)action; (void)data;
	slow_calls++;
	parity_kcomplete(&entered_c);
	(void)parity_kwait(&release_c, sched_ticks() + 30u);    /* held ~300 ms: the unregister must wait for it */
	slow_finished = 1;
	return NOTIFY_OK;
}

static void dispatch_fn(void *ctx)
{
	struct parity_acpi_dispatch d;

	(void)ctx;
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
}

static void blocking_backlight(void *ctx, uint32_t level, uint32_t max)
{
	(void)ctx;
	bl_calls++;
	bl_level = level;
	bl_max = max;
	if (bl_block) {
		parity_kcomplete(&entered_c);
		(void)parity_kwait(&release_c, sched_ticks() + 30u);
		bl_block_finished = 1;
	}
}

static void blocker_fn(void *ctx)
{
	(void)ctx;
	parity_kcomplete(&entered_c);
	(void)parity_kwait(&release_c, sched_ticks() + 30u);     /* keeps the single worker busy */
}

static int start_service(void (*bl)(void *, uint32_t, uint32_t))
{
	shadow_init(0x1du);
	if (parity_opregion_shadow_map(ASLS_TOKEN, shadow, sizeof(shadow)) != 0 || parity_opregion_shadow_setup(ASLS_TOKEN) != 0 ||
	    parity_opregion_service_start(&asle_wq, PARITY_OPREGION_POLICY_VIDEO) != 0 || parity_opregion_add_backlight(bl, 0) != 0)
		return -1;
	parity_opregion_register();
	return parity_opregion_notifier_registered() ? 0 : -1;
}

static void lifecycle_tests(parity_opregion_ktest_check check)
{
	struct parity_acpi_dispatch d;
	struct notifier_block slow;
	unsigned st, fi, qn, qp, st2, fi2, qn2, qp2, dropped, refused, dropped2, e0;
	int calls0;

	parity_kcompletion_init(&entered_c, "op-entered");
	parity_kcompletion_init(&release_c, "op-release");

	/* L0: setup fails half-way (bad signature): the reference returns the error; register then does nothing */
	shadow_init(0x1du);
	shadow[0] = 'X';
	e0 = parity_opregion_service_epoch();
	check(parity_opregion_shadow_map(ASLS_TOKEN, shadow, sizeof(shadow)) == 0 && parity_opregion_shadow_setup(ASLS_TOKEN) == -EINVAL &&
		strcmp(parity_opregion_mailbox_backend(), "NONE") == 0,
		"opregion: OP-L0 a bad signature: intel_opregion_setup fails (-EINVAL), the instance stays unbound");
	parity_opregion_register();
	check(!parity_opregion_notifier_registered() && parity_acpi_notifier_count() == 0u && parity_opregion_cleanup() == 0,
		"opregion: OP-L0-REGISTER register after a failed setup does nothing (no header); cleanup is a no-op");

	/* re-initialisation: a good shadow, a new service epoch */
	check(start_service(blocking_backlight) == 0 && parity_opregion_service_epoch() == e0 + 2u,
		"opregion: OP-L-REINIT setup + register again on a fresh shadow: registered, service epoch advanced");

	/* L1: a callback is running when a notifier is taken off the chain */
	memset(&slow, 0, sizeof(slow));
	slow.notifier_call = slow_cb;
	slow.priority = 10;
	slow_finished = 0;
	slow_calls = 0;
	parity_kreinit_completion(&entered_c);
	parity_kreinit_completion(&release_c);
	check(parity_register_acpi_notifier(&slow) == 0, "opregion: OP-L1 a slow receiver registers");
	parity_kwork_init(&dispatch_work, dispatch_fn, 0);
	(void)parity_kqueue_work(&asle_wq, &dispatch_work);
	check(parity_kwait(&entered_c, sched_ticks() + 100u) == 1, "opregion: OP-L1 the event is being delivered (callback entered)");
	check(parity_unregister_acpi_notifier(&slow) == 0 && slow_finished == 1,
		"opregion: OP-L1-SYNC unregister returned only after the running callback had finished");
	(void)parity_kflush_work(&asle_wq, &dispatch_work, sched_ticks() + 100u);
	calls0 = slow_calls;
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	check(slow_calls == calls0, "opregion: OP-L1-AFTER after the unregister the receiver is never called again");

	/* L2: the ASLE work is running when the service stops */
	bl_block = 1;
	bl_block_finished = 0;
	parity_kreinit_completion(&entered_c);
	parity_kreinit_completion(&release_c);
	wr32(ASLE_BCLP, BCLP_VALID | 100u);
	wr32(ASLE_ASLC, ASLC_SET_BACKLIGHT);
	parity_opregion_gse_entry();
	check(parity_kwait(&entered_c, sched_ticks() + 100u) == 1, "opregion: OP-L2 the worker is inside the backlight request");
	parity_opregion_unregister();
	parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
	kern_logf("i915: parity OP-LIFECYCLE stop-while-running: worker started %u finished %u | aslc 0x%x ardy %u drdy %u | "
		"unregister_synced=%d\n", st, fi, rd32(ASLE_ASLC), rd32(ASLE_ARDY), rd32(DRDY), bl_block_finished);
	check(bl_block_finished == 1 && fi == st && rd32(ASLE_ASLC) == 0u && rd32(ASLE_ARDY) == 0u && rd32(DRDY) == 0u &&
		!parity_opregion_notifier_registered(),
		"opregion: OP-L2-SYNC the stop waited for the running work (its answer written), then ARDY / DRDY 0, notifier off");
	bl_block = 0;
	parity_opregion_gate_counters(&dropped, &refused);
	wr32(ASLE_ASLC, ASLC_SET_BACKLIGHT);
	parity_opregion_gse_entry();
	parity_opregion_worker_stats_get(&st2, &fi2, &qn2, &qp2);
	parity_opregion_gate_counters(&dropped2, &refused);
	check(dropped2 == dropped + 1u && qn2 == qn && qp2 == qp && rd32(ASLE_ASLC) == ASLC_SET_BACKLIGHT,
		"opregion: OP-L2-STALE a request after the stop is dropped at the gate: nothing queued, nothing answered");
	check(parity_opregion_cleanup() == 0 && strcmp(parity_opregion_mailbox_backend(), "NONE") == 0,
		"opregion: OP-L2-CLEANUP the work is idle: cleanup releases the instance");

	/* L3: a pending (not yet running) request at stop is cancelled and never runs */
	check(start_service(fake_backlight) == 0, "opregion: OP-L3 the service runs again");
	parity_kreinit_completion(&entered_c);
	parity_kreinit_completion(&release_c);
	parity_kwork_init(&blocker_work, blocker_fn, 0);
	(void)parity_kqueue_work(&asle_wq, &blocker_work);
	check(parity_kwait(&entered_c, sched_ticks() + 100u) == 1, "opregion: OP-L3 the single worker is busy");
	parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
	wr32(ASLE_BCLP, BCLP_VALID | 90u);
	wr32(ASLE_ASLC, ASLC_SET_BACKLIGHT);
	parity_opregion_gse_entry();
	parity_opregion_unregister();
	(void)parity_kflush_work(&asle_wq, &blocker_work, sched_ticks() + 100u);
	parity_opregion_worker_stats_get(&st2, &fi2, &qn2, &qp2);
	check(qn2 == qn + 1u && st2 == st && rd32(ASLE_ASLC) == ASLC_SET_BACKLIGHT && rd32(ASLE_ARDY) == 0u && rd32(DRDY) == 0u,
		"opregion: OP-L3-CANCEL queued behind a busy worker, the request is cancelled by the stop: never run, never answered");
	check(parity_opregion_cleanup() == 0, "opregion: OP-L3-CLEANUP the instance is released");
}

void parity_opregion_ktest(parity_opregion_ktest_check check)
{
	struct parity_acpi_dispatch d;
	struct notifier_block other;
	unsigned unported, boundaries, unmaps, b0;
	uint32_t before, vsize = 0u;
	const void *vbt;

	parity_acpi_notifier_init();
	if (!asle_wq_live) {
		check(parity_kworkqueue_create(&asle_wq, "parity-opregion") == 0, "opregion: OP-WQ the shared worker queue exists");
		asle_wq_live = 1;
	}

	/* ---- setup: the reference intel_opregion_setup() on the shadow (ASLS -> shadow, ASLS + RVDA -> shadow VBT) ---- */
	shadow_init(0x1du);                     /* the target's mailboxes: ACPI, ASLE, (VBT), ASLE_EXT; no SWSCI */
	shadow_vbt_init();
	{
		uint64_t rvda = 0x2000u;
		uint32_t rvds = sizeof(shadow_vbt);

		memcpy(shadow + ASLE_RVDA, &rvda, 8);
		memcpy(shadow + ASLE_RVDS, &rvds, 4);
	}
	wr32(CHPD, 0u);
	wr32(ASLE_ARDY, 0x77u);
	check(parity_opregion_shadow_map(ASLS_TOKEN, shadow, sizeof(shadow)) == 0 &&
		parity_opregion_shadow_map(ASLS_TOKEN + 0x2000u, shadow_vbt, sizeof(shadow_vbt)) == 0 &&
		parity_opregion_shadow_setup(ASLS_TOKEN) == 0 && strcmp(parity_opregion_mailbox_backend(), "SHADOW") == 0,
		"opregion: OP-SETUP intel_opregion_setup() on the shadow: ASLS token, signature, mailboxes");
	check(rd32(CHPD) == 1u && rd32(ASLE_ARDY) == 0u,
		"opregion: OP-SETUP-WRITES setup itself writes CHPD = 1 and ARDY = NOT_READY (in the shadow only)");
	vbt = parity_opregion_vbt(&vsize);
	check(vbt == (const void *)shadow_vbt && vsize == sizeof(shadow_vbt),
		"opregion: OP-SETUP-RVDA the 2.1 relative RVDA is resolved through the mapping table to the shadow VBT (valid)");

	/* ---- register -> resume_display: DIDL / CADL from the connectors, CSTS / DRDY / TCHE / ARDY published ---- */
	check(parity_opregion_service_start(&asle_wq, PARITY_OPREGION_POLICY_VIDEO) == 0 &&
		parity_opregion_add_backlight(fake_backlight, 0) == 0 &&
		parity_opregion_add_connector(10 /* DRM_MODE_CONNECTOR_DisplayPort */, 0, 0) == 0 &&
		parity_opregion_add_connector(11 /* DRM_MODE_CONNECTOR_HDMIA */, 0, 0) == 0,
		"opregion: OP-START worker queue, policy video, connectors eDP (fake backlight), DP, HDMI");
	wr32(CSTS, 0x55u);
	wr32(DIDL + 12u, 0xdeadu);
	parity_opregion_counters(&unported, &b0, &unmaps);
	parity_opregion_register();
	parity_opregion_counters(&unported, &boundaries, &unmaps);
	kern_logf("i915: parity OP-LIFECYCLE register: DIDL %x %x %x %x | CADL %x %x %x %x | CSTS %x DRDY %x TCHE %x ARDY %x | "
		"boundaries +%u unported %u\n", rd32(DIDL), rd32(DIDL + 4u), rd32(DIDL + 8u), rd32(DIDL + 12u), rd32(CADL),
		rd32(CADL + 4u), rd32(CADL + 8u), rd32(CADL + 12u), rd32(CSTS), rd32(DRDY), rd32(ASLE_TCHE), rd32(ASLE_ARDY),
		boundaries - b0, unported);
	/* ACPI _DOD ids (ACPI 5.0 Appendix B.3.2): type << 8 | per-type index: eDP internal 4, DP / HDMI external 3 */
	check(parity_opregion_notifier_registered() && parity_acpi_notifier_count() == 1u &&
		rd32(DIDL) == 0x400u && rd32(DIDL + 4u) == 0x300u && rd32(DIDL + 8u) == 0x301u && rd32(DIDL + 12u) == 0u &&
		rd32(CADL) == 0x400u && rd32(CADL + 4u) == 0x300u && rd32(CADL + 8u) == 0x301u && rd32(CADL + 12u) == 0u,
		"opregion: OP-REGISTER notifier on the chain; DIDL / CADL = eDP 0x400, DP 0x300, HDMI 0x301, then 0 (terminated)");
	check(rd32(CSTS) == 0u && rd32(DRDY) == 1u && rd32(ASLE_TCHE) == 2u && rd32(ASLE_ARDY) == 1u && boundaries == b0 + 1u &&
		unported == 0u,
		"opregion: OP-READY resume_display publishes CSTS 0, DRDY 1, TCHE BLC_EN, ARDY READY (shadow); _DSM recorded as a "
		"boundary; the SWSCI-absent adapter notification touches no PCI config");
	parity_opregion_register();
	check(parity_acpi_notifier_count() == 1u,
		"opregion: OP-REGISTER-TWICE the chain refuses the block again (-EEXIST, as the reference ignores): still one entry");
	check(parity_opregion_notify_adapter(0 /* PCI_D0 */) == -ENODEV,
		"opregion: OP-SWSCI no SWSCI mailbox: intel_opregion_notify_adapter -> swsci -> check_swsci_function -> -ENODEV");

	/* ---- OP-NOTIFY: the reference decision table on the registered callback ---- */
	wr32(CSTS, 0x55u);
	before = rd32(CSTS);
	(void)parity_acpi_notifier_call_chain("button", "LID0", 0x80u, 0u, "SYNTHETIC", &d);
	log_dispatch("not-video", &d, before, rd32(CSTS));
	check(d.callback_result == NOTIFY_DONE && d.dispatch_result == 0 && d.calls == 1u && rd32(CSTS) == 0x55u,
		"opregion: OP-NOTIFY-CLASS an event that is not of the video class: NOTIFY_DONE, CSTS untouched");

	wr32(CSTS, 0x55u); wr32(CEVT, 0x1u);
	before = rd32(CSTS);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	log_dispatch("0x80-switch", &d, before, rd32(CSTS));
	check(d.callback_result == NOTIFY_OK && d.dispatch_result == 0 && rd32(CSTS) == 0u,
		"opregion: OP-NOTIFY-SWITCH video 0x80 with CEVT bit 0 (display switch): NOTIFY_OK, CSTS written to 0");

	wr32(CSTS, 0x55u); wr32(CEVT, 0x2u);    /* the cause is the lid, not a display switch */
	before = rd32(CSTS);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	log_dispatch("0x80-not-switch", &d, before, rd32(CSTS));
	check(d.callback_result == NOTIFY_BAD && d.dispatch_result == -EINVAL && rd32(CSTS) == 0u,
		"opregion: OP-NOTIFY-BAD video 0x80 without CEVT bit 0: NOTIFY_BAD, dispatch -EINVAL, and CSTS is STILL written to 0");

	wr32(CSTS, 0x55u); wr32(CEVT, 0x0u);
	before = rd32(CSTS);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x81u, 0u, "SYNTHETIC", &d);
	log_dispatch("0x81", &d, before, rd32(CSTS));
	check(d.callback_result == NOTIFY_OK && d.dispatch_result == 0 && rd32(CSTS) == 0u,
		"opregion: OP-NOTIFY-OTHER video type 0x81: NOTIFY_OK in this version, CSTS written to 0");

	memset(&other, 0, sizeof(other));
	other.notifier_call = other_cb;
	other.priority = -1;                    /* after the i915 block */
	other_calls = 0;
	check(parity_register_acpi_notifier(&other) == 0, "opregion: OP-CHAIN a second receiver registers after i915's");
	wr32(CEVT, 0x0u);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	check(d.callback_result == NOTIFY_BAD && d.calls == 1u && other_calls == 0,
		"opregion: OP-CHAIN-STOP NOTIFY_BAD (stop mask) ends the walk: the later receiver is not called");
	wr32(CEVT, 0x1u);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	check(d.callback_result == NOTIFY_OK && d.calls == 2u && other_calls == 1,
		"opregion: OP-CHAIN-WALK NOTIFY_OK continues: both receivers run, in priority order");
	check(parity_unregister_acpi_notifier(&other) == 0 && parity_unregister_acpi_notifier(&other) == -ENOENT,
		"opregion: OP-CHAIN-UNREGISTER removes the block; a second unregister is -ENOENT");

	/* ---- OP-ASLE on the registered, READY service ---- */
	asle_tests(check);

	/* ---- unregister: suspend_display (ARDY NOT_READY, cancel_work_sync, DRDY 0), then the notifier ---- */
	parity_opregion_unregister();
	check(!parity_opregion_notifier_registered() && parity_acpi_notifier_count() == 0u && rd32(ASLE_ARDY) == 0u &&
		rd32(DRDY) == 0u,
		"opregion: OP-UNREGISTER ARDY NOT_READY, the worker synced, DRDY 0, the callback off the chain (reference order)");
	wr32(CSTS, 0x55u); wr32(CEVT, 0x1u);
	before = rd32(CSTS);
	(void)parity_acpi_notifier_call_chain("video", "GFX0", 0x80u, 0u, "SYNTHETIC", &d);
	log_dispatch("after-unregister", &d, before, rd32(CSTS));
	check(d.calls == 0u && d.callback_result == NOTIFY_DONE && rd32(CSTS) == 0x55u,
		"opregion: OP-AFTER-UNREGISTER the same event again reaches nobody: CSTS untouched");
	parity_opregion_unregister();
	check(parity_acpi_notifier_count() == 0u, "opregion: OP-UNREGISTER-AGAIN the reference's second unregister does nothing");
	parity_opregion_cleanup();
	parity_opregion_counters(&unported, &boundaries, &unmaps);
	check(strcmp(parity_opregion_mailbox_backend(), "NONE") == 0 && unmaps >= 2u && unported == 0u,
		"opregion: OP-CLEANUP the reference cleanup unmaps the header and the RVDA mapping; no unported access happened");
	{
		unsigned st, fi, qn, qp, st2, fi2, qn2, qp2;

		parity_opregion_worker_stats_get(&st, &fi, &qn, &qp);
		parity_opregion_gse_entry();
		parity_opregion_worker_stats_get(&st2, &fi2, &qn2, &qp2);
		check(qn2 == qn && qp2 == qp && st2 == st,
			"opregion: OP-ASLE-NOMBOX after cleanup the GSE entry queues nothing (the gate is closed; no ASLE mailbox)");
	}

	lifecycle_tests(check);
}
