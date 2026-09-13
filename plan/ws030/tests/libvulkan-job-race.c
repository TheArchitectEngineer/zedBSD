/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reuses native collaborators while testing the real completion ledger's concurrent observers. */
#define main sync_peer_main
#include "libvulkan-sync.c"
#undef main
#include <assert.h>

/* Owns the real completion ledger and its serialized terminal error for this finite case. */
static struct vulkan_context race_context;
/* Names the exact committed sequence observed by both test callers. */
static struct vulkan_sync race_sync;
/* Protects the independent fixture handshake while production retains its own context mutex. */
static pthread_mutex_t race_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Wakes the finite handshakes after K consumption and before U error publication. */
static pthread_cond_t race_condition = PTHREAD_COND_INITIALIZER;
/* Joins the second actual ledger observer before the fixture retires its context. */
static pthread_t reaper_thread;
/* Publishes the second observer before the first loses sight of its K record. */
static int race_started;
/* The fixture mutex protects this proof that K consumed ERROR before U published it. */
static int consume_paused;
/* Permits the real consumer to publish ERROR after the first observer attempts serialization. */
static int resume_reaper;
/* Atomically marks the ENOENT observation while the context error remains unpublished. */
static int missing_returned;
/* Retains the actual second observer result until pthread_join makes it stable. */
static VkResult reaper_result;
/* Selects loss published immediately before initial lock acquisition instead of after lookup. */
static int before_lock_case;

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
static void *race_reap(void *argument);
static void race_wait(int *predicate);

/*
 * Releases the paused consumer only when the other observer serializes its ENOENT result.
 */
int
__wrap_pthread_mutex_lock(
	pthread_mutex_t *mutex)
{
	int status;
	int observed_missing;

	/* The failing record has already left K, while its U error is still unpublished. */
	observed_missing = __atomic_load_n(&missing_returned, __ATOMIC_ACQUIRE);
	if (mutex == &race_context.mutex && observed_missing != 0) {
		status = __real_pthread_mutex_lock(&race_mutex);
		assert(status == 0);
		resume_reaper = 1;
		pthread_cond_broadcast(&race_condition);
		pthread_mutex_unlock(&race_mutex);
	}

	/* A second variant publishes loss after the first atomic sample but before its context lock. */
	if (mutex == &race_context.mutex &&
	    before_lock_case != 0 &&
	    race_started == 0) {
		race_started = 1;
		resume_reaper = 1;
		status = pthread_create(&reaper_thread, NULL, race_reap, NULL);
		assert(status == 0);
		status = pthread_join(reaper_thread, NULL);
		assert(status == 0);
	}

	/* The actual context mutex orders both variants with the real reaper's publication. */
	status = __real_pthread_mutex_lock(mutex);
	if (status != 0)
		return status;

	/* Succeeded: the real mutex still controls error publication and ledger ownership. */
	return status;
}

/*
 * Separates K consumption from U error publication to expose the historical ENOENT race.
 */
int
race_ioctl(
	int fd,
	unsigned long operation,
	...)
{
	struct gpu_command_wait *wait;
	va_list arguments;
	int status;

	/* Only the exact observed job participates in this finite race. */
	assert(fd == 61 && operation == GPU_COMMAND_WAIT);
	va_start(arguments, operation);
	wait = va_arg(arguments, struct gpu_command_wait *);
	va_end(arguments);
	assert(wait->sequence == 91);

	/* Initial local reaping observes the still-pending K record. */
	if ((wait->flags & GPU_WAIT_CONSUME) != 0 && race_started == 0) {
		errno = EAGAIN;
		return -1;
	}

	/* The second observer consumes ERROR, retaining the context mutex until publication. */
	if ((wait->flags & GPU_WAIT_CONSUME) != 0) {
		pthread_mutex_lock(&race_mutex);
		consume_paused = 1;
		pthread_cond_broadcast(&race_condition);
		race_wait(&resume_reaper);
		pthread_mutex_unlock(&race_mutex);
		wait->status = EIO;
		return 0;
	}

	/* Force consumption between the first observer's U lookup and nonconsuming K WAIT. */
	race_started = 1;
	status = pthread_create(&reaper_thread, NULL, race_reap, NULL);
	assert(status == 0);
	pthread_mutex_lock(&race_mutex);
	race_wait(&consume_paused);
	pthread_mutex_unlock(&race_mutex);
	__atomic_store_n(&missing_returned, 1, __ATOMIC_RELEASE);
	errno = ENOENT;

	/* Failure: K has consumed the record but U has not yet published its ERROR. */
	return -1;
}

/*
 * Requires both observers to report ERROR rather than promoting a consumed failure to success.
 */
int
main(
	int argc,
	char **argv)
{
	struct vulkan_notification *notification;
	VkResult status;
	int error;

	/* Each process runs one bounded interleaving against fresh production ownership. */
	(void)argv;
	if (argc > 1)
		before_lock_case = 1;

	/* The real ledger starts with one already-committed independently chosen sequence. */
	memset(&race_context, 0, sizeof(race_context));
	memset(&race_sync, 0, sizeof(race_sync));
	race_context.fd = 61;
	race_context.capabilities = GPU_CAP_JOB;
	race_sync.object.context = &race_context;
	race_sync.notification = 91;
	notification = calloc(1, sizeof(*notification));
	assert(notification != NULL);
	notification->sequence = 91;
	race_context.notifications = notification;
	error = pthread_mutex_init(&race_context.mutex, NULL);
	assert(error == 0);

	/* ENOENT cannot bypass the reaper's context-protected error publication. */
	status = vulkan_sync_job_status(&race_sync, UINT64_MAX);
	assert(status == VK_ERROR_DEVICE_LOST);
	/* The initial-lock variant already joined its consumer before allowing that lock attempt. */
	if (before_lock_case == 0) {
		error = pthread_join(reaper_thread, NULL);
		assert(error == 0);
	}

	assert(reaper_result == VK_ERROR_DEVICE_LOST);
	assert(race_context.error == VK_ERROR_DEVICE_LOST);
	assert(race_context.notifications == NULL);
	error = pthread_mutex_destroy(&race_context.mutex);
	assert(error == 0);

	/* Succeeded: exact K consumption and terminal U error form one ordered observation. */
	puts("PASS job consume race: ENOENT waits for reaper ERROR publication, both observers DEVICE_LOST");
	return 0;
}

/*
 * Runs a real second ledger observation while the first caller is outside its context mutex.
 */
static void *
race_reap(
	void *argument)
{
	/* The independent consumer has no mocked shortcut for updating context->error. */
	(void)argument;
	reaper_result = vulkan_sync_job_status(&race_sync, 0);

	/* Succeeded: the actual reaper performed both K consumption and U publication. */
	return NULL;
}

/*
 * Bounds every fixture handshake so a missing ordering edge fails without hanging.
 */
static void
race_wait(
	int *predicate)
{
	struct timespec deadline;
	int status;

	/* The fixture condition uses its default realtime clock independently from production timeouts. */
	status = clock_gettime(CLOCK_REALTIME, &deadline);
	assert(status == 0);
	deadline.tv_sec += 5;

	/* The caller retains race_mutex while registering the finite condition wait. */
	while (*predicate == 0) {
		status = pthread_cond_timedwait(&race_condition, &race_mutex, &deadline);
		assert(status == 0);
	}

	/* Succeeded: the exact counterpart transition has occurred. */
	return;
}
