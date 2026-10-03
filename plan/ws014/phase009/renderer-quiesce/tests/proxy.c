/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Tests actual proxy retirement against a private connected or closed socket pair. */
#include <stdio.h>
#include <stdarg.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include "proxy_context.c"

#ifdef NDEBUG
#error "The proxy completion fixture requires active assertions"
#endif

/* The fixture owns the only renderer configuration observed by the linked proxy code. */
struct proxy_renderer proxy_renderer;

/* Every callback must correspond to the independently advertised successful prefix. */
static unsigned fixture_callbacks;
static uint64_t fixture_last_id;

static void fixture_retire(struct virgl_context *context, uint32_t ring, uint64_t id);
static void fixture_append(struct proxy_context *context, uint32_t sequence, uint64_t id);
static void fixture_free(struct list_head *list);

/*
 * Accepts real socket diagnostics without interpreting their text as evidence.
 */
void
proxy_log(
	const char *format,
	...)
{
	(void)format;
}

/*
 * Verifies successful prefixes and rejects invented completions after renderer death.
 */
int
main(
	void)
{
	struct proxy_context context;
	atomic_uint timelines[64];
	int pair[2];
	unsigned index;
	int error;
	bool retired;

	/* Both socket endpoints belong to this fixture and never touch another host process. */
	memset(&context, 0, sizeof(context));
	memset(&proxy_renderer, 0, sizeof(proxy_renderer));
	error = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair);
	assert(error == 0);
	context.socket.fd = pair[0];
	context.base.fence_retire = fixture_retire;
	list_inithead(&context.free_fences);

	/* Shared sequence zero means that the server has verified no GPU completion yet. */
	for (index = 0U; index < 64U; index++) {
		atomic_init(&timelines[index], 0U);
		list_inithead(&context.timelines[index].fences);
	}
	context.timeline_seqnos = timelines;
	error = mtx_init(&context.timeline_mutex, mtx_plain);
	assert(error == thrd_success);
	error = mtx_init(&context.free_fences_mutex, mtx_plain);
	assert(error == thrd_success);
	fixture_append(&context, 1U, 41U);
	fixture_append(&context, 2U, 42U);

	/* An unchanged connected timeline leaves both submitted fences pending. */
	retired = proxy_context_retire_timeline_fences_locked(&context, 0U, 0U);
	assert(!retired);
	assert(fixture_callbacks == 0U);

	/* An exact server watermark retires only the completed prefix. */
	atomic_store(&timelines[0], 1U);
	retired = proxy_context_retire_timeline_fences_locked(&context, 0U, 1U);
	assert(!retired);
	assert(fixture_callbacks == 1U);
	assert(fixture_last_id == 41U);

	/* Crossing the stock force-retire threshold after a real socket disconnect must not signal 42. */
	close(pair[1]);
	context.timelines[0].cur_seqno_stall_count = 99;
	retired = proxy_context_retire_timeline_fences_locked(&context, 0U, 1U);
	assert(!retired);
	assert(fixture_callbacks == 1U);
	assert(!list_is_empty(&context.timelines[0].fences));

	/* The real asynchronous loop must leave HUP instead of spinning or retiring pending work. */
	proxy_renderer.flags = VIRGL_RENDERER_ASYNC_FENCE_CB;
	context.timeline_busy_mask = UINT64_C(1) << 0U;
	context.sync_thread.fence_eventfd = eventfd(0, EFD_NONBLOCK);
	assert(context.sync_thread.fence_eventfd >= 0);
	error = proxy_context_sync_thread(&context);
	assert(error == 0);
	assert(fixture_callbacks == 1U);
	assert(fixture_last_id == 41U);

	/* Kernel watchdog ownership remains pending while fixture-owned host metadata retires. */
	close(context.sync_thread.fence_eventfd);
	close(pair[0]);
	fixture_free(&context.timelines[0].fences);
	fixture_free(&context.free_fences);
	mtx_destroy(&context.free_fences_mutex);
	mtx_destroy(&context.timeline_mutex);

	/* Succeeded: renderer death never manufactured a successful queue watermark. */
	puts("QUIESCE PROXY CPU0 verified-prefix/disconnect/no-false-ACK/HUP-exit PASS");
	return 0;
}

/* Records the same success callback that QEMU uses to retire fenced guest descriptors. */
static void
fixture_retire(
	struct virgl_context *context,
	uint32_t ring,
	uint64_t id)
{
	assert(context != NULL);
	assert(ring == 0U);
	assert(id == 41U);
	fixture_callbacks++;
	fixture_last_id = id;
}

/* Appends an independently numbered pending fence using the real proxy record layout. */
static void
fixture_append(
	struct proxy_context *context,
	uint32_t sequence,
	uint64_t id)
{
	struct proxy_fence *fence;

	/* No record is shared with production or another test process. */
	fence = calloc(1U, sizeof(*fence));
	assert(fence != NULL);
	fence->seqno = sequence;
	fence->fence_id = id;
	list_addtail(&fence->head, &context->timelines[0].fences);
}

/* Frees only fixture-owned records after the production loop has returned. */
static void
fixture_free(
	struct list_head *list)
{
	struct proxy_fence *fence;
	bool empty;

	/* Completed and still-pending records both remain host-owned bookkeeping. */
	while (true) {
		/* Stop before dereferencing the real list's empty sentinel. */
		empty = list_is_empty(list);
		if (empty)
			break;

		/* The fixture owns these records after the real asynchronous loop has returned. */
		fence = LIST_ENTRY(struct proxy_fence, list->next, head);
		list_del(&fence->head);
		free(fence);
	}
}
