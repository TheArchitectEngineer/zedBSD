/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The support shared by the host display tests (host-test.c): the tally of
 * checks, the reference files taken on the target laptop, and the log
 * switch the host stand-ins of the kernel services (host-kernel.c) read.
 *
 * Host only: these files are built by the plan/ws031/tests/run-*-host-test.sh
 * scripts with the host compiler and never by the kernel build.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_HOST_TEST_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_HOST_TEST_H

#include <stddef.h>
#include <stdint.h>

/*
 * Whether the run is verbose.
 *
 * Set by a test from its command line before the first check; a verbose
 * run prints every passed check and the kernel log of the driver.
 */
extern int i915_host_verbose;

void i915_host_check(int passed, const char *what);
unsigned i915_host_checks(void);
unsigned i915_host_failures(void);
int i915_host_report(const char *test);
void i915_host_read_reference(const char *directory, const char *name, uint8_t *buffer, size_t size);

#endif
