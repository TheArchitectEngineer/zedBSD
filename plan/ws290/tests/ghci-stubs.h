/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The kernel's services as the GHCI driver's host tests give them: one
 * thread, a fake clock that each sleep advances while the model runs,
 * MMIO into the model's register window, host memory from the model, and
 * a GPU core that checks a registration as gpu.c does.
 *
 * The stubs also check how the driver waits: the order in which it takes
 * its locks, and the locks it must not hold while it waits on a named
 * queue (the gate of the recovery, a ticket).  A test may run a hook in
 * the middle of a wait to change what the driver finds when it wakes.
 */

#ifndef GHCI_STUBS_H
#define GHCI_STUBS_H

#include <stdint.h>

struct drv_gpu_ops;

/* The locks the stubs follow at once. */
#define STUB_HELD_MAX			16U

/* The lock order and the forbidden waits a test may give. */
#define STUB_ORDER_MAX			8U
#define STUB_FORBID_MAX			8U

/* The hook a test runs at each sleep of the driver: the sleep's number and the hook's argument. */
typedef void (*stub_sleep_hook)(unsigned sleep, void *argument);

/*
 * What the stubs saw and what the test set.  The checks read the counts;
 * stub_reset clears them before each scene.
 */
struct stub_state {
	const char *held[STUB_HELD_MAX];
	unsigned held_count;
	const char *order[STUB_ORDER_MAX];
	unsigned order_count;
	const char *forbid_queue[STUB_FORBID_MAX];
	const char *forbid_lock[STUB_FORBID_MAX];
	unsigned forbid_count;
	stub_sleep_hook hook;
	void *hook_argument;
	uint64_t ticks;
	unsigned sleeps;
	unsigned mmio_errors;
	unsigned double_locks;
	unsigned order_violations;
	unsigned wait_violations;
	unsigned unlock_errors;
	unsigned register_errors;
	unsigned registrations;
	unsigned unregistrations;
	unsigned session_errors;
	unsigned capacity_notes;
	unsigned completions;
	int last_completion_error;
	unsigned uncached_maps;
	unsigned uncached_unmaps;
	unsigned log_lines;
	int superuser;
	int quiet;
	const void *core_ops;
	void *core_private;
};

/* The stubs' state; stub_reset clears it. */
extern struct stub_state stub;

void stub_reset(void);
volatile void *stub_bar0(void);
void stub_lock_order(const char *const *names, unsigned count);
void stub_forbid_wait(const char *queue, const char *lock);
void stub_set_sleep_hook(stub_sleep_hook hook, void *argument);
int stub_holds(const char *name);
int stub_gpu_ops_validate(const struct drv_gpu_ops *ops);

#endif
