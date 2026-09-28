/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-090: src/libc/sort.c compiled for the host with its functions renamed.
 *
 * The host's headers are included first, so their declarations of qsort and
 * qsort_r (with nonnull attributes the C library's own header does not have)
 * keep the host's names, and the file's definitions become zed_*.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define qsort zed_qsort
#define qsort_r zed_qsort_r
#define heapsort zed_heapsort
#define mergesort zed_mergesort

#include "src/libc/sort.c"
