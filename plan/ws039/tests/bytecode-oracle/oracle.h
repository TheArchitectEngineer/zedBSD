/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the reference's video BIOS runner (Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/atom.c, built on the host from the temp tree
 * by amdrdna-bytecode-test.sh, never kept in the tree) needs from the
 * kernel, in host terms: the integer types, allocation, the log calls as
 * nothing, the waits handed to the test, a jiffies counter that stays at
 * 0 (so the reference's 20-second loop limit never fires), and the byte
 * order helpers of a little-endian host.  Included before the reference's
 * file with -include.
 */

#ifndef PLAN_WS039_TESTS_BYTECODE_ORACLE_ORACLE_H
#define PLAN_WS039_TESTS_BYTECODE_ORACLE_ORACLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define KERN_DEBUG			""
#define printk(...)			((void)0)
#define pr_info(...)			((void)0)
#define DRM_ERROR(...)			((void)0)
#define GFP_KERNEL			0
#define kcalloc(count, size, flags)	calloc((count), (size))
#define kzalloc(size, flags)		calloc(1U, (size))
#define kfree(pointer)			free(pointer)
#define drm_can_sleep()			1
#define udelay(us)			oracle_wait((uint32_t)(us))
#define mdelay(ms)			oracle_wait((uint32_t)(ms) * 1000U)
#define msleep(ms)			oracle_wait((uint32_t)(ms) * 1000U)
#define time_after(a, b)		((long)((b) - (a)) < 0)
#define jiffies_to_msecs(j)		((unsigned)(j))
#define mutex_lock(m)			((void)(m))
#define mutex_unlock(m)			((void)(m))
#define cpu_to_le32(x)			(x)
#define lower_32_bits(n)		((u32)(n))
#define upper_32_bits(n)		((u32)((n) >> 32))
#define str_yes_no(b)			((b) ? "yes" : "no")
#define strscpy(d, s, n)		snprintf((char *)(d), (n), "%s", (const char *)(s))
#define min(a, b)			((a) < (b) ? (a) : (b))
#define OPTIMIZER_HIDE_VAR(x)		((void)0)
#define do_div(n, base)			((n) /= (base))
#define __counted_by(member)

struct mutex {
	int unused;
};

/* The jiffies counter, always 0. */
extern unsigned long jiffies;

/* A wait of the reference, handed to the test's record. */
void oracle_wait(uint32_t microseconds);

static inline uint32_t
get_unaligned_le32(
	const void *pointer)
{
	uint32_t value;

	/* The four bytes as they lie. */
	memcpy(&value, pointer, sizeof(value));
	return value;
}

#endif
