/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Selects the evdev types and codes shared by zedBSD and Linux. */
#ifndef ZWL_EVDEV_H
#define ZWL_EVDEV_H

#if defined(__linux__)
#include <linux/input.h>
#else
#include <uapi/input.h>
#endif

#endif
