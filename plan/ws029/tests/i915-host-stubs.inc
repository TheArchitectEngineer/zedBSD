/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host side of the i915 core fixtures (ppgtt, stream).
 *
 * run-i915-host-tests.sh links each fixture with the one driver file it
 * tests, built as the kernel builds it: ppgtt.c for the session address
 * space, command.c for the stream validator.  This file supplies what those
 * files take from the rest of the kernel:
 *
 *  - the kernel allocator, counting live blocks;
 *  - a physical page pool above 4 GiB with its direct map, counting live
 *    pages;
 *  - stand-ins for everything the tested functions never reach (the GT
 *    objects of memory.c, the submission path of command.c, locks and wait
 *    queues), which stop the fixture if they are ever called.
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/gpu.h>
#include <kern/device-io.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>

#include "../../../src/drivers/gpu/i915/i915.h"
#include "../../../src/drivers/gpu/i915/ggtt.h"
#include "../../../src/drivers/gpu/i915/memory.h"
#include "../../../src/drivers/gpu/i915/request-queue.h"
#include "../../../src/drivers/gpu/i915/session.h"
#include "../../../src/drivers/gpu/i915/worker.h"
#include "../../../src/drivers/gpu/i915/render/render.h"
#include "../../../src/drivers/gpu/i915/render/transport.h"

/* The fake physical pool: how many pages it has and the physical address it starts at. */
#define STUB_POOL_PAGES		64U
#define STUB_POOL_BASE		0x100000000ULL

/*
 * The pages of the fake pool and which of them are taken.
 *
 * The pool stands for RAM above 4 GiB; a physical address maps to the host
 * array at the same offset.  Only the fixture's single thread allocates.
 */
static uint8_t stub_pool[STUB_POOL_PAGES * 4096U] __attribute__((aligned(4096)));
static uint8_t stub_pool_taken[STUB_POOL_PAGES];

/* How many pool pages and kernel blocks are live now. */
static unsigned stub_pool_live;
static unsigned stub_live;

static void stub_unreached(const char *function) __attribute__((noreturn));

/*
 * Allocates a kernel block of `size` bytes.
 */
void *
kern_malloc(
	size_t size)
{
	void *pointer;

	/* Allocates the block and counts it. */
	pointer = malloc(size);
	if (pointer != NULL)
		stub_live++;

	/* Reports the block, or NULL when the host is out of memory. */
	return pointer;
}

/*
 * Allocates a zeroed kernel block of `count` elements of `size` bytes.
 */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *pointer;

	/* Allocates the block and counts it. */
	pointer = calloc(count, size);
	if (pointer != NULL)
		stub_live++;

	/* Reports the block, or NULL when the host is out of memory. */
	return pointer;
}

/*
 * Frees a kernel block; NULL is nothing to free.
 */
void
kern_free(
	void *pointer)
{
	/* A real block is one live block fewer. */
	if (pointer != NULL)
		stub_live--;
	free(pointer);
}

/*
 * Orders writes; the fixture has nothing to order.
 */
void
kern_io_write_barrier(void)
{
}

/*
 * Allocates one page of the fake pool below `max_address`.
 *
 * The fixtures only ask for single, page-aligned pages.
 */
int
kern_pmem_alloc_limited(
	size_t size,
	size_t alignment,
	uint64_t max_address,
	size_t boundary,
	struct kern_pmem *run)
{
	unsigned page;

	UNUSED_PARAMETER(boundary);

	/* The fixtures only ask for single, page-aligned pages the GPU reaches. */
	assert(size == 4096U);
	assert(alignment == 4096U);
	assert(max_address >= STUB_POOL_BASE + sizeof(stub_pool) - 1U);

	/* Takes the first free page. */
	for (page = 0U; page < STUB_POOL_PAGES; page++) {
		if (stub_pool_taken[page] == 0U)
			break;
	}

	/* An exhausted pool is out of memory. */
	if (page == STUB_POOL_PAGES)
		return ENOMEM;

	/* Hands out the page as a run. */
	stub_pool_taken[page] = 1U;
	stub_pool_live++;
	run->paddr = (hal_physaddr_t)(STUB_POOL_BASE + (uint64_t)page * 4096U);
	run->size = size;

	/* Succeeded: the run is the caller's until it is freed. */
	return 0;
}

/*
 * Returns a page of the fake pool.
 */
int
kern_pmem_free(
	struct kern_pmem *run)
{
	unsigned page;

	/* Finds the page the run names. */
	assert(run->paddr >= STUB_POOL_BASE);
	page = (unsigned)((run->paddr - STUB_POOL_BASE) / 4096U);
	assert(page < STUB_POOL_PAGES);
	assert(stub_pool_taken[page] != 0U);

	/* Makes the page free again, dirty, as real memory would be. */
	memset(stub_pool + (size_t)page * 4096U, 0xa5, 4096U);
	stub_pool_taken[page] = 0U;
	stub_pool_live--;

	/* Succeeded: the page is back in the pool. */
	return 0;
}

/*
 * Finds the kernel view of a pool address; NULL outside the pool.
 */
void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	/* Only pool addresses have a kernel view. */
	if (address < STUB_POOL_BASE)
		return NULL;
	if (address - STUB_POOL_BASE >= sizeof(stub_pool))
		return NULL;

	/* Succeeded: the host bytes at the same offset. */
	return stub_pool + (address - STUB_POOL_BASE);
}

/*
 * The GT objects of memory.c and the range check of ggtt.c serve the
 * GT-owned address space of ppgtt.c, which the fixtures do not build.
 */
struct i915_gt_object *
drv_i915_gt_object_create(
	struct i915_gt_mem *gm,
	uint32_t bytes)
{
	UNUSED_PARAMETER(gm);
	UNUSED_PARAMETER(bytes);

	/* Stops the fixture: nothing it tests creates a GT object. */
	stub_unreached(__func__);
}

/* Stands in for destroying a GT object; never reached. */
void
drv_i915_gt_object_destroy(
	struct i915_gt_mem *gm,
	struct i915_gt_object *o)
{
	UNUSED_PARAMETER(gm);
	UNUSED_PARAMETER(o);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for a GT object's page address; never reached. */
int
drv_i915_gt_object_page_dma(
	const struct i915_gt_object *o,
	unsigned page,
	uint64_t *dma_out)
{
	UNUSED_PARAMETER(o);
	UNUSED_PARAMETER(page);
	UNUSED_PARAMETER(dma_out);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for a cache-line flush of a GT table; never reached. */
void
drv_i915_gt_clflush(
	const volatile void *address,
	size_t bytes)
{
	UNUSED_PARAMETER(address);
	UNUSED_PARAMETER(bytes);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for the GT's DMA range check; never reached. */
int
drv_i915_dma_in_range(
	uint64_t dma_addr,
	uint64_t length,
	uint64_t mask)
{
	UNUSED_PARAMETER(dma_addr);
	UNUSED_PARAMETER(length);
	UNUSED_PARAMETER(mask);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/*
 * The node's command path of command.c: the fixtures call only the stream
 * validator, which reaches none of these.
 */
void
drv_gpu_complete(
	struct drv_gpu_completion *completion,
	int error)
{
	UNUSED_PARAMETER(completion);
	UNUSED_PARAMETER(error);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for finding the engine of a timeline; never reached. */
int
drv_i915_engine_for_timeline(
	struct i915_device *device,
	uint32_t timeline,
	struct i915_engine **engine)
{
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(timeline);
	UNUSED_PARAMETER(engine);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for creating a GEM object; never reached. */
int
drv_i915_gem_create(
	struct i915_gem_registry *registry,
	uint64_t bytes,
	struct i915_gem_object **result)
{
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(bytes);
	UNUSED_PARAMETER(result);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for destroying a GEM object; never reached. */
void
drv_i915_gem_destroy(
	struct i915_gem_registry *registry,
	struct i915_gem_object *object)
{
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(object);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for binding a GEM object into a session space; never reached. */
int
drv_i915_gem_bind_vm(
	struct i915_ppgtt *vm,
	struct i915_gem_object *object)
{
	UNUSED_PARAMETER(vm);
	UNUSED_PARAMETER(object);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for the Vulkan executor's entry; never reached. */
int
drv_i915_render_execute(
	struct i915_render_session *session,
	const void *wire,
	size_t bytes,
	void *reply,
	size_t *reply_bytes)
{
	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(wire);
	UNUSED_PARAMETER(bytes);
	UNUSED_PARAMETER(reply);
	UNUSED_PARAMETER(reply_bytes);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for the executor's capset; never reached. */
int
drv_i915_render_get_capset(
	const struct i915_render_device *vk,
	struct gpu_capset *capset)
{
	UNUSED_PARAMETER(vk);
	UNUSED_PARAMETER(capset);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for resolving the executor's reply region; never reached. */
void *
drv_i915_render_transport_reply(
	struct i915_session *session,
	const void *wire,
	uint32_t bytes,
	size_t *capacity)
{
	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(wire);
	UNUSED_PARAMETER(bytes);
	UNUSED_PARAMETER(capacity);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for allocating a request; never reached. */
int
drv_i915_request_alloc(
	struct i915_engine *engine,
	struct i915_session *session,
	struct drv_gpu_completion *completion,
	struct i915_request **result)
{
	UNUSED_PARAMETER(engine);
	UNUSED_PARAMETER(session);
	UNUSED_PARAMETER(completion);
	UNUSED_PARAMETER(result);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for queueing a request; never reached. */
void
drv_i915_request_queue(
	struct i915_engine *engine,
	struct i915_request *request)
{
	UNUSED_PARAMETER(engine);
	UNUSED_PARAMETER(request);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for waking an engine's worker; never reached. */
void
drv_i915_worker_kick(
	struct i915_engine *engine)
{
	UNUSED_PARAMETER(engine);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for taking a mutex; never reached. */
void
mutex_lock(
	struct mutex *mutex)
{
	UNUSED_PARAMETER(mutex);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for releasing a mutex; never reached. */
void
mutex_unlock(
	struct mutex *mutex)
{
	UNUSED_PARAMETER(mutex);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for taking a spinlock; never reached. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	UNUSED_PARAMETER(lock);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for releasing a spinlock; never reached. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	UNUSED_PARAMETER(lock);
	UNUSED_PARAMETER(enabled);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for sampling a wait queue; never reached. */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	UNUSED_PARAMETER(queue);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stands in for sleeping on a wait queue; never reached. */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	UNUSED_PARAMETER(queue);
	UNUSED_PARAMETER(condition_lock);
	UNUSED_PARAMETER(observed);
	UNUSED_PARAMETER(deadline);
	UNUSED_PARAMETER(flags);

	/* Stops the fixture. */
	stub_unreached(__func__);
}

/* Stops the fixture when a path it does not test is reached. */
static void
stub_unreached(
	const char *function)
{
	/* Names the function and fails the run. */
	fprintf(stderr, "i915 host fixture: %s is not part of this fixture and was called\n", function);
	abort();
}
