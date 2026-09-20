/*
 * WS031 E-120: the N0 decision rules (parity/native_decide.c), including the situation the target laptop is expected
 * to present natively: the firmware (GOP) keeps pipe A lit from GGTT page 0 of the aperture.  zedBSD project code.
 */
#include <stdio.h>
#include <string.h>
#include "native_precheck.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static void base(struct parity_native_report *r)
{
	memset(r, 0, sizeof(*r));
	r->ggtt_pages = 1048576u;                 /* 8 MiB GGTT */
	r->driver_ggtt_first = 1048576u - 256u - 8192u;
	r->pipe[0].readable = 1;
	r->unreadable_pipes = 0xeu;               /* B..D power domains off */
	r->vtd_enabled = 1;
	r->vtd_readable = 1;
	r->vtd_ver = 0x10u;
}

int main(void)
{
	struct parity_native_report r;

	base(&r);
	parity_native_decide(&r);
	CHECK(r.proceed, "IDLE native: pipe A off, B..D power off (counted inactive), VT-d not translating -> PROCEED");

	base(&r);                                 /* the expected native start: GOP lit pipe A from GGTT page 0 */
	r.pipe[0].transconf = 0x80000000u; r.pipe[0].plane_ctl = 0x84000000u; r.pipe[0].plane_surf = 0u;
	r.active_pipes = 1u;
	r.fb_present = 1; r.fb_in_aperture = 1; r.fb_ggtt_first = 0u; r.fb_ggtt_pages = 2025u;
	parity_native_decide(&r);
	CHECK(!r.proceed && strstr(r.reason, "active") != 0 && !r.overlap,
	      "GOP-LIT native: STOP before any display write, reason = active pipe (takeover not ported); no GGTT overlap");

	base(&r);
	r.fb_present = 1; r.fb_in_aperture = 1; r.fb_ggtt_first = r.driver_ggtt_first - 10u; r.fb_ggtt_pages = 2025u;
	parity_native_decide(&r);
	CHECK(!r.proceed && r.overlap && strstr(r.reason, "overlap") != 0, "OVERLAP a firmware scanout in the driver's GGTT pages -> STOP");

	base(&r);
	r.vtd_gsts = 0x80000000u;
	parity_native_decide(&r);
	CHECK(!r.proceed && strstr(r.reason, "VT-d") != 0, "TES native: the GPU's VT-d unit translates -> STOP (no IOMMU driver)");

	base(&r);
	r.vtd_pmen = 0x80000001u;
	parity_native_decide(&r);
	CHECK(!r.proceed && strstr(r.reason, "VT-d") != 0, "PMR native: a protected memory region is enabled -> STOP");

	base(&r);
	r.vtd_readable = 0; r.vtd_ver = 0u;
	parity_native_decide(&r);
	CHECK(!r.proceed && strstr(r.reason, "not readable") != 0, "VTD-UNKNOWN native: enabled but unreadable -> STOP");

	base(&r);
	r.hypervisor = 1; r.vtd_readable = 1; r.vtd_gsts = 0u;   /* the guest reads zeros at the host unit address */
	r.vtd_ver = 0u; r.vtd_readable = 0;
	parity_native_decide(&r);
	CHECK(r.proceed && strstr(r.reason, "guest view") != 0, "GUEST: the VT-d reading is a guest view (host owns it) -> PROCEED as before");

	base(&r);
	r.vtd_enabled = 0;
	parity_native_decide(&r);
	CHECK(r.proceed, "NO-VTD: GFXVTBAR not enabled -> DMA untranslated -> PROCEED");
	/* ---- E-121: every observed condition, the primary one, and the classes that must not round to inactive ---- */
	base(&r);                                 /* the real native picture: GOP lit, ASLS set, GSTS 0x40000000 */
	r.asls = 0x614e5018u;
	r.pipe[0].cls = PARITY_N0_READABLE_ACTIVE; r.active_pipes = 1u;
	r.vtd_gsts = 0x40000000u;
	parity_native_decide(&r);
	CHECK(!r.proceed && r.primary_stop == PARITY_N0_C_ACTIVE_PIPE &&
	      r.conditions == (PARITY_N0_C_ACTIVE_PIPE | PARITY_N0_C_OPREGION_REGISTER),
	      "NATIVE-1: primary ACTIVE_PIPE; also observed: the later intel_opregion_register wall; RTPS alone is no condition");

	base(&r);
	r.pipe[0].cls = PARITY_N0_READABLE_ACTIVE; r.active_pipes = 1u;
	r.vtd_gsts = 0x80000000u;                 /* translation too */
	parity_native_decide(&r);
	CHECK(!r.proceed && r.primary_stop == PARITY_N0_C_ACTIVE_PIPE &&
	      (r.conditions & PARITY_N0_C_VTD_TRANSLATION) != 0u,
	      "TWO-WALLS: active pipe AND translation: primary = ACTIVE_PIPE, the translation is recorded too (not found only later)");

	base(&r);
	r.vtd_gsts = 0x02000000u;                 /* IRES only */
	parity_native_decide(&r);
	CHECK(r.proceed && (r.conditions & PARITY_N0_C_VTD_IR_ENABLED) != 0u && r.primary_stop == 0u,
	      "IRES: interrupt remapping on is recorded for the MSI path, not a DMA stop");

	base(&r);
	r.pipe[1].cls = PARITY_N0_READ_ERROR;
	parity_native_decide(&r);
	CHECK(!r.proceed && r.primary_stop == PARITY_N0_C_PIPE_READ_ERROR,
	      "READ-ERROR: a pipe that did not read as register values is not rounded to inactive -> STOP");

	base(&r);
	r.pipe[1].cls = PARITY_N0_POWER_OFF;      /* a valid read of STATE = off */
	parity_native_decide(&r);
	CHECK(r.proceed && r.conditions == 0u, "POWER-OFF: a valid 'off' reading is inactive -> no condition");

	base(&r);
	r.vbt_valid = 1; r.parser_sha_known = 1; r.vbt_same_bytes = 0;
	parity_native_decide(&r);
	CHECK(r.proceed && (r.conditions & PARITY_N0_C_VBT_DIFFERS) != 0u,
	      "VBT-DIFFERS: the OpRegion VBT is not the bytes the parser consumed -> recorded");
	printf("native_decide_host_test: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
