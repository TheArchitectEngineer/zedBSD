/*
 * GPU-free contract tests for the GGTT/PPGTT PTE encoders.
 * Verifies the two encoders differ, that >4 GiB DMA addresses are preserved
 * (never truncated to 32 bits), and that out-of-range/misaligned inputs are
 * rejected without ANDing bits off.
 */
#include <stdio.h>
#include "../pte.h"

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

#define MASK39 ((((uint64_t)1) << 39) - 1u)

int
main(void)
{
	uint64_t pte;

	printf("== GGTT/PPGTT PTE encoder contract tests (GPU-free) ==\n");

	/* range check: overflow-safe */
	printf("[range] whole [addr,addr+len) must fit under the mask\n");
	CHECK(parity_dma_in_range(0x1000u, 0x1000u, MASK39) == 1, "in range");
	CHECK(parity_dma_in_range(0u, 0u, MASK39) == 0, "zero length rejected");
	CHECK(parity_dma_in_range(MASK39 - 0xfffu, 0x1000u, MASK39) == 1, "ends exactly at mask ok");
	CHECK(parity_dma_in_range(MASK39 - 0xfffu, 0x2000u, MASK39) == 0, "crossing mask rejected");
	CHECK(parity_dma_in_range(MASK39, 0x1000u, MASK39) == 0, "addr at mask + len rejected");

	/* GGTT encode: PRESENT only (LM bit clear) */
	printf("[ggtt] SMEM PTE = dma | PRESENT, LM bit clear\n");
	CHECK(parity_ggtt_pte_encode(osdep_dma_addr(0x12345000u), 0x1000u, MASK39, &pte) == 1, "encode ok");
	CHECK(pte == (0x12345000u | 0x1u), "PTE = addr | PRESENT");
	CHECK((pte & 0x2u) == 0u, "GGTT LM bit (bit1) is clear for SMEM");

	/* PPGTT encode: PRESENT | RW — different bit1 meaning */
	printf("[ppgtt] leaf PTE = dma | PRESENT | RW (bit1 = RW, not LM)\n");
	CHECK(parity_ppgtt_pte_encode(osdep_dma_addr(0x12345000u), 0x1000u, MASK39, &pte) == 1, "encode ok");
	CHECK(pte == (0x12345000u | 0x1u | 0x2u), "PTE = addr | PRESENT | RW");

	/* the two encoders differ on the same input */
	printf("[distinct] GGTT and PPGTT encode the same address differently\n");
	{
		uint64_t g, p;
		parity_ggtt_pte_encode(osdep_dma_addr(0x40000000u), 0x1000u, MASK39, &g);
		parity_ppgtt_pte_encode(osdep_dma_addr(0x40000000u), 0x1000u, MASK39, &p);
		CHECK(g != p, "GGTT != PPGTT for the same DMA address");
		CHECK((g & 0x2u) == 0u && (p & 0x2u) != 0u, "bit1 differs (LM clear vs RW set)");
	}

	/* >4 GiB DMA address: full 64-bit preserved, not truncated to 32 bits */
	printf("[wide] a DMA address above 4 GiB keeps its high bits\n");
	{
		uint64_t hi = 0x0000001234567000ull;   /* > 0xffffffff, within 39-bit mask */
		CHECK(parity_ggtt_pte_encode(osdep_dma_addr(hi), 0x1000u, MASK39, &pte) == 1, "encode ok");
		CHECK(pte == (hi | 0x1u), "high bits preserved (no 32-bit truncation)");
		CHECK((pte >> 32) != 0u, "PTE really carries bits above 32");
	}

	/* misaligned address rejected, no PTE written */
	printf("[align] a misaligned DMA address is rejected (not masked into range)\n");
	{
		uint64_t sentinel = 0xdeadbeefu;
		pte = sentinel;
		CHECK(parity_ggtt_pte_encode(osdep_dma_addr(0x12345800u), 0x1000u, MASK39, &pte) == 0,
		      "misaligned rejected");
		CHECK(pte == sentinel, "pte_out untouched on rejection");
	}

	/* out-of-range address rejected, not AND-masked to fit */
	printf("[oob] an out-of-range DMA address is rejected, not corrected\n");
	{
		uint64_t over = (((uint64_t)1) << 40);   /* beyond the 39-bit mask */
		CHECK(parity_ggtt_pte_encode(osdep_dma_addr(over), 0x1000u, MASK39, &pte) == 0,
		      "out-of-range rejected");
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
