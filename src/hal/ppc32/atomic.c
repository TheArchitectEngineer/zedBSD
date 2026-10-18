/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#include <hal/hal.h>

/*
 * The 32-bit PowerPC HAL is uniprocessor.  Excluding interrupts therefore
 * serializes atomic fallback regions against every execution context.
 */
hal_atomic_cookie_t
hal_atomic_enter(void)
{
	hal_atomic_cookie_t cookie;

	cookie = hal_irq_disable() ? 1U : 0U;
	__asm__ volatile("" ::: "memory");
	return cookie;
}

void
hal_atomic_leave(
	hal_atomic_cookie_t cookie)
{
	__asm__ volatile("" ::: "memory");
	if (cookie != 0U)
		hal_irq_enable();
}
