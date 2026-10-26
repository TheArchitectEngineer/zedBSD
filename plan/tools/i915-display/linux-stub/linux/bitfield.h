/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The field helpers the Linux register headers expect, for the host
 * check of the xd register headers against them.
 */

#ifndef WS245_LINUX_STUB_BITFIELD_H
#define WS245_LINUX_STUB_BITFIELD_H

#include <linux/bits.h>

/* The position of a mask's lowest set bit. */
#define __bf_shf(mask) (__builtin_ffsll(mask) - 1)

/* Places a value into a field, and takes it out again. */
#define FIELD_PREP(mask, value) (((unsigned long long)(value) << __bf_shf(mask)) & (mask))
#define FIELD_GET(mask, word) (((word) & (mask)) >> __bf_shf(mask))

#endif
