/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_RESOLV_H
#define LIBC_RESOLV_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Base 64, as the name service encodes binary records.
 *
 * This header carries only these, because the resolver zedBSD provides is
 * the one behind getaddrinfo and gethostbyname; there is no res_query here
 * to send a query of your own.  They live here because that is where the
 * software that calls them looks for them.
 */
int b64_ntop(const unsigned char *, size_t, char *, size_t);
int b64_pton(const char *, unsigned char *, size_t);

#ifdef __cplusplus
}
#endif

#endif
