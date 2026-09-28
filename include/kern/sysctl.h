/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef KERN_KERN_SYSCTL_H
#define KERN_KERN_SYSCTL_H

#include <stddef.h>

void
sysctl_init(void);

/*
 * Counts a GPU device whose driver has attached it and will publish its node
 * later (hw.gpu.attaching).
 */
void
kern_gpu_attach_begin(void);

/*
 * Uncounts a GPU device whose node is now published or never will be.
 */
void
kern_gpu_attach_end(void);

int
kern_sysctl(
	const int *name,
	unsigned namelen,
	void *oldp,
	size_t *oldlenp,
	const void *newp,
	size_t newlen,
	int superuser);

#endif
