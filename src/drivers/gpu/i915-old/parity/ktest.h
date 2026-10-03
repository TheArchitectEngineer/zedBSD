/*
 * WS031 Linux-parity — in-kernel concurrency tests (Work 2).
 *
 * Validates the sync layer's REAL behaviour with actual zedBSD threads and
 * waitqs — blocking waits, wakeups across threads, counting, timeout — which the
 * single-threaded host mock cannot exercise.  Runs GPU-free (kernel only) and
 * reports pass/fail via the log.
 */
#ifndef PARITY_KTEST_H
#define PARITY_KTEST_H

/* Runs the in-kernel sync tests; returns 0 if all passed, negative on failure. */
int parity_sync_ktest(void);

#endif /* PARITY_KTEST_H */
