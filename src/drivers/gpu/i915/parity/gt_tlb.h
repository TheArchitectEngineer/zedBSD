/*
 * WS031 Linux-parity -- the GT TLB invalidation after PTEs were removed (gt/intel_tlb.c: mmio_invalidate_full for
 * graphics IP 12.0 / 12.10, the register table of intel_engine_init_tlb_invalidation()).  zedBSD project code.
 */
#ifndef PARITY_GT_TLB_H
#define PARITY_GT_TLB_H

#include <stdint.h>

struct parity_gt_engines;
struct osdep_mmio;
struct spinlock;

struct parity_gt_tlb {
	uint32_t seqno;                 /* gt->tlb.seqno: even; +2 after every completed full invalidation */
	unsigned invalidations, engines_invalidated, timeouts, time_faults, fw_failures;
	/* who is asking: a timeout log line carries these, so a test's intended fault is told apart mechanically from a
	 * hardware anomaly (backend 0 = "HW", test_id 0 = "-") */
	const char *backend, *test_id;
	int expected_fault;
};

/* the engine's invalidation register, its request word and its done bit (gen12 table); 0 = no such engine class */
int parity_gt_tlb_engine_reg(int class, int instance, uint32_t *reg, uint32_t *request, uint32_t *done);
/*
 * intel_gt_invalidate_tlb_full(): every engine's TLB, plus Wa_2207587034 (GEN12_OA_TLB_INV_CR) on ADL-P, then each
 * engine's done bit.  ADAPTATION: the reference skips engines that are not awake (intel_engine_pm_is_awake) and a GT
 * that is asleep, because their TLBs do not survive power-down.  Here "parked" is a software state that does not
 * power anything down, so no engine is skipped.  Forcewake is taken for the writes; the uncore lock serialises them
 * with a reset.  Each engine's wait is for its done bit to read back 0 (wait_for_invalidate(): mask = done,
 * value = 0).  0; -ETIMEDOUT (an engine's bit did not clear in 4 ms); -EIO (the time base / forcewake failed).
 * ADAPTATIONS beyond the reference (recorded): the reference is void and logs a timeout; here the error is returned so
 * the caller keeps every page (release contract), and the seqno advances only on success, so a failed invalidation is
 * never taken as a completed generation.  No MCR lock is taken (the ADL-P engine registers are not MCR); the forcewake
 * is returned at once (the reference uses a delayed put); callers are serialised by the one owner (no invalidate_lock).
 */
int parity_gt_invalidate_tlb_full(struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m,
	struct spinlock *uncore_lock);

#endif /* PARITY_GT_TLB_H */
