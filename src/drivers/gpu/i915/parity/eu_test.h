/*
 * WS031 Linux-parity — the one-shot real-hardware EU test (explicitly released).
 *
 * The compute positive control of the big-bang investigation (C1: GPGPU_WALKER
 * dispatching one SIMD8 thread whose kernel does an unconditional A64 store of
 * 0xc0ffee02 and then send.ts EOT), executed on the GT that the parity port
 * initialised the Linux way.  The batch, kernel, IDD, VFE state and the VA
 * layout (shared page at 0x100400000, batch at 0x100401000) are byte-for-byte
 * the L-C1 case that completed on Linux i915 on this same GPU (E-25).
 *
 * The request is shaped like an execbuf request (gem/i915_gem_execbuffer.c,
 * gt/gen8_engine_cs.c): i915_request_create (invalidate flush) ->
 * gen8_emit_init_breadcrumb (seqno-1 store, MI_ARB_CHECK) ->
 * gen8_emit_bb_start (ARB on, MI_BATCH_BUFFER_START_GEN8 into the PPGTT,
 * ARB off) -> i915_request_add (fini breadcrumb), on a fresh context of the
 * kernel vm that inherits engine->default_state (lrc_init_state).
 *
 * Outcome: PASS when the CS markers land and the EU store is visible;
 * HANG when the request does not retire within the timeout (the engines are
 * then reset the way intel_gt_set_wedged would).
 */
#ifndef PARITY_EU_TEST_H
#define PARITY_EU_TEST_H

#include <stdint.h>
#include "gt_mem.h"
#include "gt_lrc.h"
#include "gt_request.h"

struct parity_gt_engines;
struct parity_gt_ppgtt;
struct parity_sseu;
struct osdep_mmio;
struct spinlock;

#define PARITY_EU_SHARED_VA      0x100400000ull
#define PARITY_EU_BATCH_VA       0x100401000ull
#define PARITY_EU_IDD_OFFSET     896u
#define PARITY_EU_KSP_OFFSET     1024u
#define PARITY_EU_READY_OFF      0xc00u
#define PARITY_EU_EU_OFF         0xc20u     /* hard-coded in the store kernel */
#define PARITY_EU_DONE_OFF       0xc28u
#define PARITY_EU_CS_OFF         0xc30u
#define PARITY_EU_IDD_RB_OFF     0xd00u
#define PARITY_EU_KERNEL_RB_OFF  0xe00u
#define PARITY_EU_READY_TAG      0xc0ffee10u
#define PARITY_EU_STORE_TAG      0xc0ffee02u
#define PARITY_EU_DONE_TAG       0xc0ffee20u
#define PARITY_EU_CS_TAG         0xc0ffee30u
#define PARITY_EU_KERNEL_DWORDS  36u

/* The real-hardware EU test runs only when explicitly released (build-time). */
#ifndef PARITY_EU_TEST
#define PARITY_EU_TEST 0
#endif

#define PARITY_EU_PASS   1
#define PARITY_EU_HANG   2
#define PARITY_EU_ERROR  3

struct parity_eu_test {
	unsigned engine_idx;
	struct parity_gt_context ce;
	struct parity_gt_object *tl_page;
	uint32_t tl_seqno;
	struct parity_gt_object *shared;
	struct parity_gt_object *batch;
	unsigned batch_dwords;
	uint32_t max_threads;
	unsigned dss_count;

	struct parity_gt_request rq;
	struct parity_gt_request krq;
	int submitted, completed, parked, timed_out, wedged;
	unsigned polls;

	/* what the shared page says afterwards */
	uint32_t ready, eu, done, cs;
	uint32_t idd_rb[8];
	uint32_t kernel_rb[PARITY_EU_KERNEL_DWORDS];
	int idd_rb_ok, kernel_rb_ok;

	int outcome;
	int err;
	const char *err_where;
};

/* Builds the batch (exposed for the GPU-free word check); returns dwords. */
unsigned parity_eu_test_build_batch(uint32_t *cmds, unsigned capacity,
	uint64_t shared_va, uint64_t inst_base, uint32_t max_threads);

int parity_eu_test_run(struct parity_eu_test *t, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);

void parity_eu_test_release(struct parity_eu_test *t, struct parity_gt_mem *gm);

#endif /* PARITY_EU_TEST_H */
