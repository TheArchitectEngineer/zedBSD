/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws229-p002: the host stand-ins the Gen12 golden links instead of the GT
 * memory, the GGTT and the HAL's interrupt calls.
 *
 * Objects are page-aligned zeroed host memory at made-up GGTT addresses,
 * handed out in order, so both trees give the same addresses.  Nothing is
 * freed: the program is short.
 */

#include "../../memory.h"
#include "../../ggtt.h"

#include <hal/hal.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/lock.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* The next made-up GGTT address an object is bound at. */
static uint64_t golden_next_ggtt = 0x00100000U;

/* Creates a zeroed, page-aligned object. */
struct i915_gt_object *
drv_i915_gt_object_create(
	struct i915_gt_mem *gm,
	uint32_t bytes)
{
	struct i915_gt_object *object;
	uint32_t size;
	void *cpu;

	(void)gm;

	/* Rounds the size up to whole pages and allocates the backing. */
	size = (bytes + 4095U) & ~4095U;
	object = calloc(1U, sizeof(*object));
	if (object == NULL)
		return NULL;
	cpu = aligned_alloc(4096U, size);
	if (cpu == NULL)
		return NULL;
	memset(cpu, 0, size);

	/* Describes the object. */
	object->cpu = cpu;
	object->bytes = size;
	object->pages = size / 4096U;

	/* Succeeded: the object exists. */
	return object;
}

/* Forgets an object; nothing is freed. */
void
drv_i915_gt_object_destroy(
	struct i915_gt_mem *gm,
	struct i915_gt_object *o)
{
	(void)gm;
	(void)o;
}

/* Gives an object the next made-up GGTT address. */
int
drv_i915_gt_ggtt_bind(
	struct i915_gt_mem *gm,
	struct i915_gt_object *o)
{
	(void)gm;

	/* Binds the object after the previous one. */
	o->ggtt_offset = golden_next_ggtt;
	golden_next_ggtt += o->bytes;

	/* Succeeded: the object is bound. */
	return 0;
}

/* Writes nothing back: host memory is the GPU's view here. */
void
drv_i915_gt_clflush(
	const volatile void *address,
	size_t bytes)
{
	(void)address;
	(void)bytes;
}

/* The handler is never attached on the host. */
int
hal_irq_attach_msi(
	int irq,
	hal_irq_handler_t handler,
	void *argument)
{
	(void)irq;
	(void)handler;
	(void)argument;
	return HAL_OK;
}

/* Nothing was attached on the host. */
int
hal_irq_detach_msi_sync(
	int irq,
	hal_irq_handler_t handler,
	void *argument)
{
	(void)irq;
	(void)handler;
	(void)argument;
	return HAL_OK;
}

/* No interrupt is acknowledged on the host. */
void
hal_irq_send_eoi(
	hal_irq_ack_t acknowledge)
{
	(void)acknowledge;
}

/* Orders nothing on the host: the program is single-threaded. */
void
kern_io_write_barrier(void)
{
}

/* Orders nothing on the host. */
void
kern_io_read_barrier(void)
{
}

/* Keeps the compiler from merging the delay loops' reads. */
void
kern_compiler_barrier(void)
{
	__asm__ volatile("" ::: "memory");
}

/* Reads the host's monotonic clock in nanoseconds. */
bool
kern_rtc_read_counter(
	uint64_t *counter,
	uint64_t *frequency_hz)
{
	struct timespec now;

	/* Reads the clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	*counter = (uint64_t)now.tv_sec * 1000000000U + (uint64_t)now.tv_nsec;
	if (frequency_hz != NULL)
		*frequency_hz = 1000000000U;

	/* Succeeded: the counter is valid. */
	return true;
}

/* Takes nothing on the host: the program is single-threaded. */
void
mutex_lock(
	struct mutex *lock)
{
	(void)lock;
}

/* Gives nothing back on the host. */
void
mutex_unlock(
	struct mutex *lock)
{
	(void)lock;
}
