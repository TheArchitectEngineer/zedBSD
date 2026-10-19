/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS context switch.
 */

#ifndef KERN_HAL_MIPS_TASK_H
#define KERN_HAL_MIPS_TASK_H

#include <hal/types.h>

void
mips_task_dispatch(
	uintptr_t *save,
	const uintptr_t *load);

void
mips_kernel_task_entry(
	void);

void
mips_task_returned(
	void) __attribute__((noreturn));

void
mips_context_selftest(
	void);

#endif
