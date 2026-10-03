/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Stands in for the target's <net/if.h> while a host test is built.
 *
 * The host has a header of that name belonging to its own C library, and
 * it and the zedBSD one disagree about what the names mean.  The policy
 * under test wants one thing from it, so that one thing is given here and
 * the host's header is kept out of the way.
 */

#ifndef KERN_WS033_TEST_NET_IF_H
#define KERN_WS033_TEST_NET_IF_H

#include <uapi/netif.h>

#endif
