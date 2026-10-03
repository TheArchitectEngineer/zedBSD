/*
 * GPU-free contract tests for the Linux-parity MMIO / forcewake / MCR layer.
 * Verifies the CONTRACT against a mock register file, not real hardware.
 */
#include <stdio.h>
#include "mock_mmio.h"

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

int
main(void)
{
	static struct osdep_trace trace;
	static struct osdep_mmio u;
	struct mock_mmio mock;

	printf("== MMIO/forcewake/MCR contract tests (mock, GPU-free) ==\n");
	osdep_trace_init(&trace);
	mock_mmio_reset(&mock);
	osdep_mmio_init(&u, mock_mmio_backend(), &mock,
			mock_mmio_ranges, mock_mmio_range_count, &trace);

	/* domain classification */
	printf("[domain] register-to-forcewake-domain mapping\n");
	CHECK(osdep_mmio_domain_of(&u, 0x2244u) == OSDEP_FW_RENDER, "0x2244 is render domain");
	CHECK(osdep_mmio_domain_of(&u, 0xe18cu) == OSDEP_FW_RENDER, "0xe18c is render domain");
	CHECK(osdep_mmio_domain_of(&u, 0x9424u) == OSDEP_FW_GT, "0x9424 is GT domain");
	CHECK(osdep_mmio_domain_of(&u, 0x1234u) == -1, "0x1234 is always-on");

	/* FW-1: get / nested get / put / put */
	printf("[FW-1] refcounted get/nested/put/put with one wake + one sleep\n");
	CHECK(osdep_fw_get(&u, OSDEP_FW_RENDER) == 0, "get render ok");
	CHECK(osdep_fw_is_held(&u, OSDEP_FW_RENDER), "render held");
	CHECK(mock.fw_request_calls[OSDEP_FW_RENDER] == 1, "one wake request on 0->1");
	CHECK(osdep_fw_get(&u, OSDEP_FW_RENDER) == 0, "nested get ok");
	CHECK(mock.fw_request_calls[OSDEP_FW_RENDER] == 1, "no extra request on nested get");
	CHECK(osdep_fw_put(&u, OSDEP_FW_RENDER) == 0, "first put ok");
	CHECK(osdep_fw_is_held(&u, OSDEP_FW_RENDER), "still held after first put");
	CHECK(mock.fw_request_calls[OSDEP_FW_RENDER] == 1, "no sleep yet");
	CHECK(osdep_fw_put(&u, OSDEP_FW_RENDER) == 0, "second put ok");
	CHECK(!osdep_fw_is_held(&u, OSDEP_FW_RENDER), "released after last put");
	CHECK(mock.fw_request_calls[OSDEP_FW_RENDER] == 2, "sleep request on 1->0");

	/* MMIO-1: forcewaked read requires the domain held */
	printf("[MMIO-1] forcewaked access requires the domain held\n");
	mock_mmio_preset(&mock, 0x2244u, 0x00000008u);
	CHECK(osdep_mmio_read32(&u, 0x2244u) == 0xffffffffu, "read without forcewake -> sentinel");
	CHECK(osdep_fw_get(&u, OSDEP_FW_RENDER) == 0, "get render");
	CHECK(osdep_mmio_read32(&u, 0x2244u) == 0x00000008u, "read with forcewake -> real value");
	/* always-on register needs no forcewake */
	mock_mmio_preset(&mock, 0x1234u, 0xdeadbeefu);
	CHECK(osdep_mmio_read32(&u, 0x1234u) == 0xdeadbeefu, "always-on read ok without forcewake");

	/* MMIO-2: masked RMW */
	printf("[MMIO-2] masked RMW touches only masked bits\n");
	mock_mmio_preset(&mock, 0x2580u, 0x0000ffffu);
	osdep_mmio_write32_masked(&u, 0x2580u, 0xff00u, 0x1234u);
	CHECK(mock_mmio_peek(&mock, 0x2580u) == 0x000012ffu, "RMW: (0xffff & ~0xff00)|(0x1234 & 0xff00)=0x12ff");

	/* MMIO-2b: Intel masked write is a single write (mask in high half, no RMW) */
	printf("[MMIO-2b] Intel masked write: single write, no read-modify-write\n");
	{
		int reads_before = mock.read_calls;
		int writes_before = mock.write_calls;
		/* _MASKED_BIT_ENABLE(bit2)|_MASKED_BIT_DISABLE(bit1) style composed word. */
		osdep_mmio_write32_mask_enable(&u, 0x2588u, 0x00060004u);
		CHECK(mock.read_calls == reads_before, "no read performed (not an RMW)");
		CHECK(mock.write_calls == writes_before + 1, "exactly one write");
		CHECK(mock_mmio_peek(&mock, 0x2588u) == 0x00060004u, "composed word written verbatim");
	}

	/* MMIO-3: posting read issues a read */
	printf("[MMIO-3] posting read issues a bus read\n");
	{
		int before = mock.read_calls;
		osdep_mmio_posting_read32(&u, 0x2244u);
		CHECK(mock.read_calls == before + 1, "posting read performed one read");
	}
	CHECK(osdep_fw_put(&u, OSDEP_FW_RENDER) == 0, "put render");
	CHECK(!osdep_fw_is_held(&u, OSDEP_FW_RENDER), "render released");

	/* MMIO-4: normal (auto) access manages forcewake itself */
	printf("[MMIO-4] auto access takes/releases forcewake around the access\n");
	{
		int req_before = mock.fw_request_calls[OSDEP_FW_RENDER];
		uint32_t v;
		mock_mmio_preset(&mock, 0x2244u, 0x11223344u);
		CHECK(!osdep_fw_is_held(&u, OSDEP_FW_RENDER), "nothing held before auto access");
		v = osdep_mmio_read32_auto(&u, 0x2244u);          /* no explicit forcewake by caller */
		CHECK(v == 0x11223344u, "auto read returns the real value without caller forcewake");
		CHECK(mock.fw_request_calls[OSDEP_FW_RENDER] == req_before + 2,
		      "auto access performed one wake + one sleep");
		CHECK(!osdep_fw_is_held(&u, OSDEP_FW_RENDER), "domain released after auto access");
		osdep_mmio_write32_auto(&u, 0x2248u, 0xcafef00du);
		CHECK(mock_mmio_peek(&mock, 0x2248u) == 0xcafef00du, "auto write reached the register");
	}

	/* FW-2: ACK timeout unwinds the reference */
	printf("[FW-2] ACK timeout returns error and does not leave a held ref\n");
	mock.fw_never_ack[OSDEP_FW_GT] = 1;
	CHECK(osdep_fw_get(&u, OSDEP_FW_GT) == -62, "get with no ack -> -ETIME");
	CHECK(!osdep_fw_is_held(&u, OSDEP_FW_GT), "no held ref after ack timeout");
	CHECK(u.fw_ack_timeouts == 1u, "ack timeout counted");
	mock.fw_never_ack[OSDEP_FW_GT] = 0;

	/* FW-3: over-put detected */
	printf("[FW-3] over-put is detected, not silently ignored\n");
	CHECK(osdep_fw_put(&u, OSDEP_FW_GT) == -22, "put with count 0 -> -EINVAL");
	CHECK(u.fw_underflow == 1, "underflow flagged");

	/* MCR: exclusive steering section */
	printf("[MCR] steering is an exclusive locked section\n");
	CHECK(osdep_mcr_lock(&u, 0x0u) == 0, "lock steer ok");
	CHECK(osdep_mcr_is_locked(&u), "mcr locked");
	CHECK(osdep_mcr_lock(&u, 0x1u) == -16, "re-lock while held -> -EBUSY");
	osdep_mcr_unlock(&u);
	CHECK(!osdep_mcr_is_locked(&u), "mcr unlocked");
	CHECK(osdep_mcr_lock(&u, 0x2u) == 0, "lock again after unlock ok");
	osdep_mcr_unlock(&u);

	/* balance: no forcewake domain left held */
	printf("[balance] no forcewake left held at end\n");
	CHECK(!osdep_fw_is_held(&u, OSDEP_FW_RENDER), "render balanced");
	CHECK(!osdep_fw_is_held(&u, OSDEP_FW_GT), "GT balanced");

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
