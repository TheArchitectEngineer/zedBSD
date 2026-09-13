/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reference-bearing completion state independent of an open GPU session. */
#ifndef KERN_KERN_FENCE_H
#define KERN_KERN_FENCE_H

#include <kern/handle.h>
#include <stdint.h>

#define KERNEL_FENCE_PENDING 0U
#define KERNEL_FENCE_SIGNALED 1U
#define KERNEL_FENCE_ERROR 2U

/* One locked observation; generation changes invalidate prior pending work. */
struct kernel_fence_state {
	uint64_t generation;
	uint32_t state;
	int error;
};

/* Creation returns one typed handle reference; device identity is immutable. */
int kernel_fence_create(uint64_t device, unsigned signaled, struct kernel_handle **result);
int kernel_fence_device(struct kernel_handle *handle, uint64_t device);
int kernel_fence_query(struct kernel_handle *handle, uint64_t generation, struct kernel_fence_state *state);
int kernel_fence_wait(struct kernel_handle *handle, uint64_t generation, uint64_t deadline, unsigned immediate, struct kernel_fence_state *state);
int kernel_fence_reset(struct kernel_handle *handle, uint64_t generation, struct kernel_fence_state *state);

/* A retained owner binds before submission and must signal or release before retirement. */
int kernel_fence_bind(struct kernel_handle *handle, uint64_t generation, void *owner);
int kernel_fence_unbind(struct kernel_handle *handle, uint64_t generation, void *owner);
int kernel_fence_signal(struct kernel_handle *handle, uint64_t generation, void *owner, int error);

/* IRQ producers holding a registry lock defer poll_notify until after that lock is released. */
int kernel_fence_signal_deferred(struct kernel_handle *handle, uint64_t generation, void *owner, int error);

#endif
