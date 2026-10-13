/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws229-p002: the Gen12 golden of the GT programming, on the host.
 *
 * Runs the Gen12 (Alder Lake-P) GT programming that WS229 branched for Gen9
 * against a mock register file and prints every register write in order,
 * the workaround, whitelist and context lists, the context images, the
 * rings of a request on every engine and the submission writes.  run.sh
 * builds it once against the tree before WS229 and once against the tree
 * under test and compares the two outputs: the Gen12 behaviour is unchanged
 * when they are identical.  Only functions that exist in both trees are
 * called.
 *
 * Built with GOLDEN_GEN9 (the tree under test only) the same program runs
 * the Gen9 (Skylake GT2) programming instead, as a host smoke test whose
 * output run.sh keeps for reading; it is not compared with anything.
 */

#include "contract.h"
#include "mock_mmio.h"

#include "../../context.h"
#include "../../device-info.h"
#include "../../engine.h"
#include "../../gt-power.h"
#include "../../irq.h"
#include "../../memory.h"
#include "../../mmio.h"
#include "../../ppgtt.h"
#include "../../request.h"
#include "../../submit.h"
#include "../../trace.h"
#include "../../workarounds.h"

#include <kern/lock.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GOLDEN_GEN9
/* The Gen9 engine set: RCS0, BCS0, VCS0 and VECS0, and the graphics version. */
#define GOLDEN_ENGINE_MASK	((1U << 0) | (1U << 1) | (1U << 8) | (1U << 16))
#define GOLDEN_VERSION		9
#define GOLDEN_DOMAINS		3
#else
/* The Alder Lake-P engine set: RCS0, BCS0, VCS0, VCS2 and VECS0, and the graphics version. */
#define GOLDEN_ENGINE_MASK	((1U << 0) | (1U << 1) | (1U << 8) | (1U << 10) | (1U << 16))
#define GOLDEN_VERSION		12
#define GOLDEN_DOMAINS		5
#endif

/* How many register-state dwords of a context image are printed. */
#define GOLDEN_STATE_DWORDS	1024U

static void golden_write_hook(struct mock_mmio *mock, uint32_t offset, uint32_t value, void *context);
static void golden_dump_list(const char *what, const struct i915_wa_list *wal);
static void golden_dump_words(const char *what, const uint32_t *words, unsigned count);

/* Nonzero while register writes are printed. */
static int golden_logging;

/*
 * Runs the Gen12 programming and prints what it did.
 */
int
main(void)
{
	static struct i915_trace trace;
	static struct mock_mmio mock;
	static struct i915_mmio mmio;
	static struct i915_gt_info info;
	static struct i915_gt_init init;
	static struct i915_irq_dev irq;
	static struct i915_gt_engine ge[I915_MAX_ENGINES];
	static struct i915_execlists el[I915_MAX_ENGINES];
	static struct i915_gt_context ce[I915_MAX_ENGINES];
	static struct i915_gt_ppgtt vm;
	static struct i915_gt_request rq;
	static uint32_t hwsp_seqno[I915_MAX_ENGINES];
	static struct mutex sb_lock;
	const struct i915_mmio_range *ranges;
	unsigned range_count;
	unsigned index;
	unsigned writes;
#ifndef GOLDEN_GEN9
	int domain;
#endif
	int error;
	char name[64];

	/* Binds the register access to the mock with the Gen12 forcewake map, every domain held. */
	drv_i915_trace_init(&trace);
	mock_mmio_reset(&mock);
	mock.write_hook = golden_write_hook;
#ifdef GOLDEN_GEN9
	ranges = drv_i915_mmio_gen9_ranges(&range_count);
#else
	ranges = drv_i915_mmio_gen12_ranges(&range_count);
#endif
	drv_i915_mmio_init(&mmio, mock_mmio_ops(), &mock, ranges, range_count, &trace);
#ifdef GOLDEN_GEN9
	mmio.graphics_ver = 9;
	(void)drv_i915_forcewake_get(&mmio, I915_FORCEWAKE_RENDER);
	(void)drv_i915_forcewake_get(&mmio, I915_FORCEWAKE_GT);
	(void)drv_i915_forcewake_get(&mmio, I915_FORCEWAKE_MEDIA_GEN9);
#else
	for (domain = 0; domain < GOLDEN_DOMAINS; domain++)
		(void)drv_i915_forcewake_get(&mmio, domain);
#endif

	/* Presets the fuses of a 1x6x16 part with every media engine and L3 bank. */
	mock_mmio_preset(&mock, 0x9138U, 0x1U);
	mock_mmio_preset(&mock, 0x913cU, 0x3fU);
	mock_mmio_preset(&mock, 0x9134U, 0x0U);
	mock_mmio_preset(&mock, 0x9118U, 0x0U);
	mock_mmio_preset(&mock, 0x9140U, 0x0U);
#ifdef GOLDEN_GEN9
	/* A Gen9 GT2: slice 0 with three of four subslices, every EU. */
	mock_mmio_preset(&mock, 0x9120U, (0x1U << 25) | (0x8U << 20));
	info.platform = 1;
	info.revision = 7U;
#endif
	mock_mmio_preset(&mock, 0x145998U, 0x00090f12U);

	/* The GT information. */
	golden_logging = 1;
	printf("== gt_init_mmio\n");
	(void)drv_i915_gt_init_mmio(&info, GOLDEN_VERSION, GOLDEN_ENGINE_MASK, &mmio);
	printf("clock=%u engines=%u mask=0x%x eu_total=%u\n", info.clock_frequency, info.num_engines, info.engine_mask, info.sseu.eu_total);
	for (index = 0U; index < info.num_engines; index++) {
		printf("engine %s class=%d inst=%d base=0x%x reset=0x%x size=%u caps=0x%x\n",
		       info.engines[index].name,
		       info.engines[index].class,
		       info.engines[index].instance,
		       info.engines[index].mmio_base,
		       info.engines[index].reset_domain,
		       info.engines[index].context_size,
		       info.engines[index].uabi_capabilities);
	}

	/* The tables. */
	printf("== gt_init_tables\n");
	drv_i915_gt_init_tables(&init, &info, GOLDEN_VERSION, &sb_lock, &mmio);
	golden_dump_list("gt", &init.gt_wa);
	for (index = 0U; index < info.num_engines; index++) {
		golden_dump_list("engine", &init.engine_wa[index]);
		golden_dump_list("ctx", &init.ctx_wa[index]);
		golden_dump_list("whitelist", &init.whitelist[index]);
	}
	printf("mocs n=%u uc=%u unused=%u\n", init.mocs.n_entries, init.mocs.uc_index, init.mocs.unused_entries_index);

	/* The frequencies the render power state is built with. */
	printf("rps rp0=%u rp1=%u min=%u eff=%u\n", init.rps.rp0_freq, init.rps.rp1_freq, init.rps.min_freq, init.rps.efficient_freq);

	/* The programming of intel_gt_init_hw() and the engines' resume. */
	printf("== gt_init_hw_core\n");
	drv_i915_gt_init_hw_core(&init, &info, &mmio);
	printf("== engine resume\n");
	for (index = 0U; index < info.num_engines; index++) {
		drv_i915_engine_apply_resume_wa(&init, &info, index, &mmio);
		if (info.engines[index].class == I915_RENDER_CLASS)
			drv_i915_init_l3cc_table(&init.mocs, &mmio, &writes);
	}

	/* The power state. */
	printf("== rc6 and rps\n");
	drv_i915_rc6_sanitize(&init.rc6, &mmio);
	drv_i915_gen11_rc6_enable(&init.rc6, &mmio, &info);
	drv_i915_rps_enable(&init.rps, &mmio, info.clock_frequency);

	/* The interrupt reset and postinstall. */
	printf("== irq\n");
	irq.m = &mmio;
	irq.gt = &info;
	irq.submission = I915_SUBMISSION_EXECLISTS;
	drv_i915_irq_reset(&irq);
	drv_i915_irq_postinstall(&irq);

	/* Each engine: its execlists, a context image, a request and its submission. */
	memset(&vm, 0, sizeof(vm));
	vm.top_pd_dma = 0x123456000ULL;
	for (index = 0U; index < info.num_engines; index++) {
		snprintf(name, sizeof(name), "engine %s", info.engines[index].name);
		printf("== %s\n", name);
		error = drv_i915_engine_setup_common(&ge[index], &info.engines[index], (struct i915_gt_mem *)&mock, &info.sseu);
		printf("setup rc=%d\n", error);
		drv_i915_execlists_submission_setup(&ge[index]);
		drv_i915_execlists_init(&el[index]);
		drv_i915_execlists_enable(&ge[index], &mmio);
		drv_i915_execlists_reset_csb_pointers(&ge[index], &mmio);

		/* The context image. */
		error = drv_i915_lrc_alloc(&ce[index], &ge[index], &vm, (struct i915_gt_mem *)&mock, 4096U, 0U);
		printf("lrc_alloc rc=%d bytes=%u wa_page=%u\n", error, ce[index].state_bytes, ce[index].wa_bb_page);
		drv_i915_lrc_init_state(&ce[index]);
		printf("update_regs lrca=0x%x\n", drv_i915_lrc_update_regs(&ce[index], ce[index].ring.tail));
		golden_dump_words("regs", ce[index].lrc_reg_state, GOLDEN_STATE_DWORDS);
		golden_dump_words("wa_bb", (const uint32_t *)((const char *)ce[index].state->cpu + ce[index].wa_bb_page * 4096U), 2048U);

		/* A request with the context workarounds, then its submission. */
		error = drv_i915_request_create(&rq, &ce[index], 2U, 0x2000U + index * 0x100U, &hwsp_seqno[index]);
		printf("request_create rc=%d\n", error);
		error = drv_i915_emit_ctx_wa(&rq, &init.ctx_wa[index], &mmio);
		printf("emit_ctx_wa rc=%d\n", error);
		error = drv_i915_request_add(&rq);
		printf("request_add rc=%d tail=%u wa_tail=%u\n", error, rq.tail, rq.wa_tail);
		golden_dump_words("ring", ce[index].ring.vaddr, rq.wa_tail / 4U);
		error = drv_i915_execlists_submit(&ge[index], &el[index], &mmio, &rq);
		printf("submit rc=%d desc=0x%llx\n", error, (unsigned long long)ce[index].lrc_desc);
	}

	/* Succeeded: the output is the golden. */
	return 0;
}

/* Prints one register write while logging. */
static void
golden_write_hook(
	struct mock_mmio *mock,
	uint32_t offset,
	uint32_t value,
	void *context)
{
	(void)mock;
	(void)context;

	/* Prints the write in order. */
	if (golden_logging != 0)
		printf("W %05x %08x\n", offset, value);
}

/* Prints every entry of a workaround list. */
static void
golden_dump_list(
	const char *what,
	const struct i915_wa_list *wal)
{
	const char *name;
	unsigned index;

	/* Names the list, with a dash for one that has no name. */
	name = "-";
	if (wal->name != NULL)
		name = wal->name;

	/* Prints the list's name and size, then each entry. */
	printf("list %s %s %u\n", what, name, wal->count);
	for (index = 0U; index < wal->count; index++) {
		printf("  reg=%05x clr=%08x set=%08x rmask=%08x kind=%d mcr=%d\n",
		       wal->list[index].reg,
		       wal->list[index].clr,
		       wal->list[index].set,
		       wal->list[index].read_mask,
		       wal->list[index].kind,
		       wal->list[index].is_mcr);
	}
}

/* Prints a run of dwords, eight to a line, skipping all-zero lines. */
static void
golden_dump_words(
	const char *what,
	const uint32_t *words,
	unsigned count)
{
	unsigned index;
	unsigned column;
	int zero;

	/* Prints each line of eight that has a nonzero dword. */
	printf("%s %u dwords\n", what, count);
	for (index = 0U; index < count; index += 8U) {
		zero = 1;
		for (column = 0U; column < 8U && index + column < count; column++) {
			if (words[index + column] != 0U)
				zero = 0;
		}

		/* Skips a line of eight zero dwords. */
		if (zero != 0)
			continue;

		/* Prints the line's offset and its eight dwords. */
		printf("  %04x:", index);
		for (column = 0U; column < 8U && index + column < count; column++)
			printf(" %08x", words[index + column]);
		printf("\n");
	}
}
