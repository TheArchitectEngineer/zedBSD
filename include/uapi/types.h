/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zedBSD public ABI scalar types
 */

#ifndef KERN_UAPI_TYPES_H
#define KERN_UAPI_TYPES_H

#include <stdint.h>

/* The user ABI, from the compilation itself when the build did not say; see
 * sys/types.h for why. */
#ifndef KERN_USER_ABI_LP64
#ifdef __LP64__
#define KERN_USER_ABI_LP64 1
#endif
#endif

/*
 * The pointed-to address belongs to the calling user ABI, not the kernel.
 */
#ifdef KERN_USER_ABI_LP64
typedef uint64_t uapi_ptr_t;
#else
typedef uint32_t uapi_ptr_t;
#endif

#endif
