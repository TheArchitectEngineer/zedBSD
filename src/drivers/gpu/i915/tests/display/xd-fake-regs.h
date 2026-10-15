/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register model the host tests of the xd display run against
 * (xd-fake-regs.c).
 *
 * The model is a set of registers with values, a clock in microseconds,
 * and rules that say how the hardware answers a write: a request bit
 * that shows up as a state bit some time later, or a test's own function
 * for an exchange (the GT driver mailbox, an AUX transaction).  It is
 * installed into a struct i915_xd_io through its hooks, so the xd code
 * runs unchanged.  It also records the order in which the xd locks are
 * taken and counts the takings that break the documented order.
 *
 * The model is one per test process and is cleared with
 * i915_xd_fake_reset().
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_XD_FAKE_REGS_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_XD_FAKE_REGS_H

#include <stdint.h>

#include "../../display/xd-io.h"

/* A test's answer to a write of one register: the register, the value written, and the test's context. */
typedef void (*i915_xd_fake_write_fn)(uint32_t offset, uint32_t value, void *context);

void i915_xd_fake_reset(void);
void i915_xd_fake_hooks(struct i915_xd_io_hooks *hooks);
void i915_xd_fake_set(uint32_t offset, uint32_t value);
uint32_t i915_xd_fake_get(uint32_t offset);
uint64_t i915_xd_fake_now(void);
void i915_xd_fake_later(unsigned delay_us, uint32_t offset, uint32_t clear, uint32_t set);
void i915_xd_fake_rule_mirror(uint32_t offset, uint32_t source_mask, uint32_t target, int shift, unsigned delay_us);
void i915_xd_fake_rule_call(uint32_t offset, i915_xd_fake_write_fn function, void *context);
unsigned i915_xd_fake_reads(void);
unsigned i915_xd_fake_writes(void);
unsigned i915_xd_fake_lock_violations(void);
unsigned i915_xd_fake_locks_held(void);

#endif /* DRIVERS_GPU_I915_TESTS_DISPLAY_XD_FAKE_REGS_H */
