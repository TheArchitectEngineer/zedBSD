/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The bit helpers the Linux register headers expect, for the host check
 * of the xd register headers against them (check-xd-regs-linux.sh).
 * Only the values matter there, so the compile-time checks are empty.
 */

#ifndef WS245_LINUX_STUB_BITS_H
#define WS245_LINUX_STUB_BITS_H

/* Bit n, and the contiguous bits high down to low. */
#define BIT(n) (1UL << (n))
#define BIT_ULL(n) (1ULL << (n))
#define GENMASK(high, low) ((~0UL >> (63 - (high))) & (~0UL << (low)))
#define GENMASK_ULL(high, low) ((~0ULL >> (63 - (high))) & (~0ULL << (low)))

/* The compile-time checks, which the value check does not need. */
#define BUILD_BUG_ON_ZERO(condition) 0
#define BUILD_BUG_ON_MSG(condition, message) do { } while (0)
#define __is_constexpr(x) 0

/* The largest 32-bit value. */
#define U32_MAX 0xffffffffU

#endif
