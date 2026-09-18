/*
 * GPU-free contract tests for the Linux-parity DMA adaptation layer.
 * These verify the CONTRACT (address separation, return conventions, ownership,
 * sync ordering) against a mock backend.  They do NOT prove hardware DMA — that
 * is a separate, real-backend verification.
 */
#include <stdio.h>
#include "mock_dma.h"
#include "../osdep/trace.h"

#define PAGE 0x1000u

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

static int
trace_has_order(struct osdep_trace *t, uint16_t first_op, uint16_t second_op)
{
	struct osdep_trace_record buf[512];
	uint32_t n = osdep_trace_snapshot(t, buf, 512);
	uint32_t i;
	int seen_first = 0;

	for (i = 0u; i < n; i++) {
		if (buf[i].op == first_op)
			seen_first = 1;
		if (buf[i].op == second_op && seen_first)
			return 1;
	}
	return 0;
}

int
main(void)
{
	static struct osdep_trace trace;
	static struct osdep_dma_device dev;
	struct mock_dma mock;
	struct osdep_dma_mapping *m;
	struct osdep_sg_entry in[8];
	int rc;

	printf("== DMA contract tests (mock backend, GPU-free) ==\n");

	/* --- return-contract: set_info --- */
	osdep_trace_init(&trace);
	mock_dma_reset(&mock);
	osdep_dma_device_init(&dev, mock_dma_backend(), &mock, &trace);
	printf("[set_info] return contract 0/-errno\n");
	CHECK(osdep_dma_set_info(&dev, 39u, 0x100000u) == 0, "valid set_info returns 0");
	CHECK(mock.set_info_calls == 1, "backend set_info called once");
	CHECK(mock.last_mask_bits == 39u, "mask bits forwarded (device cap, not phys-bits)");
	CHECK(osdep_dma_set_info(&dev, 8u, 0x1000u) == -22, "bad mask_bits -> -EINVAL");
	CHECK(osdep_dma_set_info(&dev, 39u, 0u) == -22, "zero max_segment -> -EINVAL");

	/* --- DMA-1: non-identity mapping --- */
	printf("[DMA-1] non-identity mapping\n");
	{
		osdep_cpu_phys_t phys = osdep_cpu_phys(0x0000000123456000ULL);
		osdep_dma_addr_t d = osdep_dma_map_page(&dev, phys, PAGE, OSDEP_DMA_TO_DEVICE);
		CHECK(!osdep_dma_mapping_failed(d), "map_page succeeds");
		CHECK(osdep_dma_addr_raw(d) != osdep_cpu_phys_raw(phys),
		      "device address != CPU physical");
		CHECK(mock_dma_untranslate(osdep_dma_addr_raw(d)) == osdep_cpu_phys_raw(phys),
		      "translation is reversible to the same phys");
		osdep_dma_unmap_page(&dev, d, PAGE, OSDEP_DMA_TO_DEVICE);
	}

	/* --- DMA-2: non-contiguous / non-linear per page --- */
	printf("[DMA-2] scattered pages are not base + i*PAGE\n");
	{
		unsigned i;
		in[0].phys = osdep_cpu_phys(0x100000ULL); in[0].length = PAGE; /* page 0x100 */
		in[1].phys = osdep_cpu_phys(0x101000ULL); in[1].length = PAGE; /* page 0x101 */
		in[2].phys = osdep_cpu_phys(0x102000ULL); in[2].length = PAGE; /* page 0x102 */
		mock.coalesce = 0;
		rc = osdep_dma_map_sg(&dev, in, 3u, OSDEP_DMA_TO_DEVICE, &m);
		CHECK(rc == 3, "map_sg returns mapped count 3 (no coalesce)");
		CHECK(m != 0 && m->nents == 3u, "handle has 3 segments");
		for (i = 0u; i < 3u; i++)
			CHECK(osdep_dma_addr_raw(m->segs[i].addr) == mock_dma_translate(in[i].phys.value),
			      "each segment addr == translate(phys[i])");
		/* A "base + i*PAGE" consumer would be wrong: prove segments are not linear. */
		CHECK(osdep_dma_addr_raw(m->segs[1].addr) !=
		      osdep_dma_addr_raw(m->segs[0].addr) + PAGE, "seg1 != seg0 + PAGE");
		CHECK(osdep_dma_addr_raw(m->segs[2].addr) !=
		      osdep_dma_addr_raw(m->segs[0].addr) + 2u * PAGE, "seg2 != seg0 + 2*PAGE");
		CHECK(osdep_dma_unmap_sg(&dev, m) == 0, "unmap ok");
	}

	/* --- DMA-3: SG coalescing splits input count from segment count --- */
	printf("[DMA-3] coalescing: mapped nents < orig nents; unmap uses orig\n");
	{
		in[0].phys = osdep_cpu_phys(0x200000ULL); in[0].length = PAGE;
		in[1].phys = osdep_cpu_phys(0x201000ULL); in[1].length = PAGE;
		in[2].phys = osdep_cpu_phys(0x202000ULL); in[2].length = PAGE;
		in[3].phys = osdep_cpu_phys(0x203000ULL); in[3].length = PAGE;
		mock.coalesce = 1;
		rc = osdep_dma_map_sg(&dev, in, 4u, OSDEP_DMA_TO_DEVICE, &m);
		CHECK(rc >= 1 && (unsigned)rc < 4u, "mapped segment count < 4 (coalesced)");
		CHECK(m->orig_nents == 4u, "handle remembers orig_nents=4 for unmap");
		CHECK(m->segs[0].length == 4u * PAGE, "coalesced segment covers all 4 pages");
		CHECK(osdep_dma_unmap_sg(&dev, m) == 0, "unmap ok");
		CHECK(mock.last_unmap_orig_nents == 4u, "backend unmap received orig_nents=4, not the segment count");
		mock.coalesce = 0;
	}

	/* --- DMA-4: mapping failure -> no proceed, no leak --- */
	printf("[DMA-4] mapping failure: 0 return, resources released\n");
	{
		in[0].phys = osdep_cpu_phys(0x300000ULL); in[0].length = PAGE;
		mock.fail = 1;
		m = (struct osdep_dma_mapping *)0;
		rc = osdep_dma_map_sg(&dev, in, 1u, OSDEP_DMA_TO_DEVICE, &m);
		CHECK(rc == 0, "map_sg failure returns 0 (dma_map_sg contract)");
		CHECK(m == 0, "no mapping handle on failure");
		CHECK(osdep_dma_live_mappings(&dev) == 0u, "no leaked mapping slot after failure");
		/* sgtable contract differs: 0/-errno */
		mock.fail = 1;
		rc = osdep_dma_map_sgtable(&dev, in, 1u, OSDEP_DMA_TO_DEVICE, &m);
		CHECK(rc < 0, "map_sgtable failure returns -errno (not 0)");
	}

	/* --- DMA-5: lifetime violation while pinned --- */
	printf("[DMA-5] unmap refused while pinned into a GPU page table\n");
	{
		osdep_dma_addr_t pinned;
		in[0].phys = osdep_cpu_phys(0x400000ULL); in[0].length = PAGE;
		rc = osdep_dma_map_sg(&dev, in, 1u, OSDEP_DMA_TO_DEVICE, &m);
		CHECK(rc == 1, "map ok");
		pinned = osdep_dma_pin(&dev, m);
		CHECK(!osdep_dma_mapping_failed(pinned), "pin returns a device address");
		CHECK(osdep_dma_addr_raw(pinned) == mock_dma_translate(in[0].phys.value),
		      "pinned address is the DEVICE address (never the CPU physical)");
		CHECK(osdep_dma_unmap_sg(&dev, m) == -16, "unmap while pinned -> -EBUSY");
		CHECK(osdep_dma_live_mappings(&dev) == 1u, "pinned mapping still live (not freed)");
		osdep_dma_unpin(&dev, m);
		CHECK(osdep_dma_unmap_sg(&dev, m) == 0, "unmap ok after unpin");
		CHECK(osdep_dma_live_mappings(&dev) == 0u, "mapping freed after unpin+unmap");
	}

	/* --- DMA-6: sync contract ordering --- */
	printf("[DMA-6] sync_for_device before use, sync_for_cpu after\n");
	{
		osdep_dma_addr_t d;
		osdep_trace_init(&trace);   /* isolate ordering check */
		in[0].phys = osdep_cpu_phys(0x500000ULL); in[0].length = PAGE;
		rc = osdep_dma_map_sg(&dev, in, 1u, OSDEP_DMA_TO_DEVICE, &m);
		d = m->segs[0].addr;
		osdep_dma_sync_for_device(&dev, d, PAGE, OSDEP_DMA_TO_DEVICE);   /* CPU wrote; hand to device */
		/* (device would run here) */
		osdep_dma_sync_for_cpu(&dev, d, PAGE, OSDEP_DMA_FROM_DEVICE);    /* device done; CPU reads */
		CHECK(mock.sync_dev_calls == 1, "backend sync_for_device called");
		CHECK(mock.sync_cpu_calls == 1, "backend sync_for_cpu called");
		CHECK(trace_has_order(&trace, OSDEP_TR_SYNC_DEV, OSDEP_TR_SYNC_CPU),
		      "trace records sync_for_device before sync_for_cpu");
		osdep_dma_unmap_sg(&dev, m);
	}

	/* --- trace overflow is recorded, not silent --- */
	printf("[trace] overflow is counted (a gap != 'not run')\n");
	{
		uint32_t i;
		osdep_trace_init(&trace);
		for (i = 0u; i < OSDEP_TRACE_CAPACITY + 10u; i++)
			osdep_trace_emit(&trace, 0u, OSDEP_TR_NOTE, "flood", i, 0u);
		CHECK(trace.dropped == 10u, "dropped count == overflow amount");
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
