/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises text subscriptions against concurrent writers and owner retirement.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kern/lock.h>
#include <kern/text-display.h>

/* Every subscription owns one condition lock and notification sequence. */
struct notification {
	struct kern_text_observer observer;
	struct spinlock lock;
	struct wait_queue queue;
};

/* Thread-local lock observations enforce the production registry-to-condition ordering. */
static __thread enum lock_rank ranks[4];

/* Each host thread records only its own current lock nesting depth. */
static __thread unsigned depth;

/* Writers and subscription retirement overlap until the finite main test stops the writer. */
static unsigned writer_stop;

/* Atomic completed mutations are inspected only after the writer has joined. */
static unsigned writes;

/* Atomic delivered notifications remain live through the complete fixture. */
static unsigned wakes;

/* Only clear is used here, rasterization and all other dispatch entries have their own fixture. */
static struct kern_text_ops text_operations;

static void clear_text(void);
static void *write_text(void *argument);
static void notification_init(struct notification *notification);
static void test_delivery(void);
static void test_retirement(void);

/*
 * Checks notification delivery, unsubscribed quietness and concurrent storage retirement.
 */
int
main(
	void)
{
	/* A retained-text backend is independent from every observer's lifetime. */
	text_operations.clear = clear_text;
	kern_text_register(&text_operations);
	test_delivery();
	test_retirement();

	/* Succeeded: every linked consumer was notified without a callback into any display driver. */
	puts("text notifications: PASS (generation, duplicate/rank rejection, quiet unsubscribe, concurrent retirement)");
	return 0;
}

/*
 * Models IRQ masking while retaining actual mutual exclusion across host threads.
 */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* The same acquisition rules apply to interrupt and ordinary lock entry. */
	spin_lock(lock);

	/* Succeeded: the synthetic IRQ state is consumed by the matching restore. */
	return 1UL;
}

/*
 * Ends the synthetic IRQ-disabled region only after releasing its actual host lock.
 */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long flags)
{
	/* The dispatcher must preserve the state received at lock entry. */
	assert(flags == 1UL);
	spin_unlock(lock);

	/* Succeeded: another writer may now enter the same protected notification boundary. */
	return;
}

/*
 * Implements a small test spinlock while checking ordered, nonrecursive acquisition.
 */
void
spin_lock(
	struct spinlock *lock)
{
	unsigned busy;

	/* A registry may enter a subscriber condition, which cannot reenter the registry. */
	assert(depth < 4U);

	/* Only nested acquisition needs an earlier rank to compare. */
	if (depth != 0U)
		assert(lock->rank > ranks[depth - 1U]);

	/* Actual host threads contend for the same storage, exercising removal against a live writer. */
	while (1) {
		/* A released owner permits entry without advancing the observed lock depth early. */
		busy = __atomic_exchange_n(&lock->held.value, 1U, __ATOMIC_ACQUIRE);
		if (busy == 0U)
			break;

		/* Let the other participant finish its finite critical section. */
		sched_yield();
	}

	/* Record the acquired rank only once this thread owns the condition storage. */
	ranks[depth++] = lock->rank;

	/* Succeeded: ordered publication and wake access are serialized. */
	return;
}

/*
 * Releases one observed lock before another host participant may reuse its storage.
 */
void
spin_unlock(
	struct spinlock *lock)
{
	/* Every acquisition must unwind in reverse order on all error paths. */
	assert(depth != 0U);
	assert(ranks[depth - 1U] == lock->rank);
	depth--;
	__atomic_store_n(&lock->held.value, 0U, __ATOMIC_RELEASE);

	/* Succeeded: this thread retains no access protected by the released lock. */
	return;
}

/*
 * Observes the actual dispatcher's wake while the subscriber condition is locked.
 */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	uint32_t generation;

	/* The subscriber lock must nest after the registry lock, and text must already be published. */
	assert(depth == 2U);
	assert(ranks[0] == LOCK_RANK_CONSOLE_TEXT);
	assert(ranks[1] == LOCK_RANK_POLL);
	generation = kern_text_generation();
	assert(generation != 0U);

	/* Sequence advancement models wake readiness without involving a test scheduler. */
	queue->sequence++;
	__atomic_add_fetch(&wakes, 1U, __ATOMIC_RELAXED);

	/* Succeeded: the consumer's next condition observation detects this completed mutation. */
	return;
}

/* Completes a retained-text operation before the dispatcher advances its generation. */
static void
clear_text(
	void)
{
	/* Concurrent writers need no fake framebuffer or display driver side effects. */
	__atomic_add_fetch(&writes, 1U, __ATOMIC_RELAXED);

	/* Succeeded: the actual dispatcher may now notify current consumers. */
	return;
}

/* Keeps actual mutation notifications racing with subscription registration and retirement. */
static void *
write_text(
	void *argument)
{
	unsigned stopping;

	/* The worker uses only the public text mutation boundary. */
	(void)argument;

	/* Keep emitting mutations until the owner asks this finite test to finish. */
	while (1) {
		/* Retirement remains concurrent with mutations until the final join boundary. */
		stopping = __atomic_load_n(&writer_stop, __ATOMIC_ACQUIRE);
		if (stopping != 0U)
			break;

		/* The shell timeout bounds the test even if a production registry bug deadlocks. */
		kern_text_clear();
	}

	/* Succeeded: the joining test may inspect all final generation and wake observations. */
	return NULL;
}

/* Prepares one caller-owned condition before its first publication. */
static void
notification_init(
	struct notification *notification)
{
	/* Registration does not allocate or initialize the caller's sleeping condition. */
	memset(notification, 0, sizeof(*notification));
	notification->lock.rank = LOCK_RANK_POLL;
	notification->lock.name = "test text destination";
	notification->queue.sequence = 1U;

	/* Succeeded: this storage remains owned by the test through synchronous unobserve. */
	return;
}

/* Checks broadcasts, generation ordering, duplicate registration and unsubscribe quietness. */
static void
test_delivery(
	void)
{
	struct notification first;
	struct notification second;
	uint32_t generation;
	int error;

	/* Two consumers receive the same text generation without sharing their condition queues. */
	notification_init(&first);
	notification_init(&second);
	error = kern_text_observe(&first.observer, &first.lock, &first.queue);
	assert(error == 0);
	error = kern_text_observe(&first.observer, &first.lock, &first.queue);
	assert(error == EBUSY);
	error = kern_text_observe(&second.observer, &second.lock, &second.queue);
	assert(error == 0);
	generation = kern_text_generation();
	kern_text_clear();
	assert(kern_text_generation() != generation);
	assert(first.queue.sequence == 2U && second.queue.sequence == 2U);

	/* A graphics owner may silence its consumer while another display keeps observing text. */
	kern_text_unobserve(&first.observer);
	kern_text_unobserve(&first.observer);
	kern_text_clear();
	assert(first.queue.sequence == 2U && second.queue.sequence == 3U);
	kern_text_unobserve(&second.observer);

	/* Registration rejects a destination that would invert the interrupt-side lock order. */
	first.lock.rank = LOCK_RANK_DEVICE;
	error = kern_text_observe(&first.observer, &first.lock, &first.queue);
	assert(error == EINVAL);
	error = kern_text_observe(NULL, &second.lock, &second.queue);
	assert(error == EINVAL);

	/* Succeeded: both retired queues stay unchanged after subsequent ordinary text mutations. */
	kern_text_clear();
	assert(first.queue.sequence == 2U && second.queue.sequence == 3U);
	return;
}

/* Frees unsubscribed storage immediately while another thread continuously emits real notifications. */
static void
test_retirement(
	void)
{
	struct notification *notification;
	pthread_t writer;
	unsigned iteration;
	int error;

	/* Only the public registry lifetime keeps these independently allocated destinations safe. */
	error = pthread_create(&writer, NULL, write_text, NULL);
	assert(error == 0);

	/* Repeated independent owners exercise immediate storage reuse after unsubscription. */
	for (iteration = 0U; iteration < 1000U; iteration++) {
		/* Fresh allocations and ASan expose late wakes against a retired caller-owned link. */
		notification = malloc(sizeof(*notification));
		assert(notification != NULL);

		/* This owner's initialized queue is live for exactly one subscription interval. */
		notification_init(notification);
		error = kern_text_observe(&notification->observer, &notification->lock, &notification->queue);
		assert(error == 0);
		kern_text_clear();
		kern_text_unobserve(&notification->observer);
		free(notification);
	}

	/* Join the writer before interpreting final counters or destroying process storage. */
	__atomic_store_n(&writer_stop, 1U, __ATOMIC_RELEASE);
	error = pthread_join(writer, NULL);
	assert(error == 0);
	assert(writes >= 1000U && wakes >= 1000U);
	assert(depth == 0U);

	/* Succeeded: no asynchronous notification outlived synchronous subscription removal. */
	return;
}
