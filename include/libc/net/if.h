/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_NET_IF_H
#define LIBC_NET_IF_H

#include <uapi/netif.h>

/* An interface's name and index (POSIX; ws130-p004). */
#define IF_NAMESIZE	IFNAMSIZ

unsigned if_nametoindex(const char *name);
char *if_indextoname(unsigned index, char *name);

#endif
