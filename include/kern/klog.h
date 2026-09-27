/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef KERN_KERN_KLOG_H
#define KERN_KERN_KLOG_H

#include <stddef.h>
#include <stdint.h>

void
kern_log_init(void);

void
kern_log_write(
	const char *bytes,
	size_t length);

void
kern_logf(
	const char *format,
	...)
__attribute__((format(printf, 1, 2)));

size_t
kern_log_snapshot(
	char *buffer,
	size_t capacity,
	uint64_t *dropped);

size_t
kern_log_capacity(void);

/*
 * The quiet log (the boot parameter kmsg=quiet, ws035-p097): records go to
 * the ring (dmesg) and the platform debug port, not to the console.
 */
void
kern_log_set_quiet(
	int quiet);

int
kern_log_quiet(void);

#endif
