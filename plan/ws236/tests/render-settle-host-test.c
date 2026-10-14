/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The V3D executor's settling list on the host (ws236 p010).
 *
 * The real src/drivers/gpu/bcm2711/render-worker.c runs with fixture
 * executions and stubbed kernel services: an execution with a settle
 * leaves its session's queue when it has run, so the next one runs; it
 * ends when its settle lets it; its own marker waits for it while later
 * markers do not; a marker that does not know its execution waits for
 * every settling one it covers; a stop ends a settling execution without
 * its settle; a failed settle faults the engine.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/gpu/bcm2711/render-device.h"
#include "drivers/gpu/bcm2711/render-worker.h"

/* The queue domain the fixture's markers and executions use. */
#define TEST_TIMELINE 7U

/* The most settle answers one fixture execution is given. */
#define TEST_ANSWERS 8U

/* The most completions the fixture records. */
#define TEST_COMPLETIONS 32U

/*
 * One fixture execution: the scripted settle answers and the wait flag of
 * each call, and what its end received.
 */
struct test_execution {
	struct bcm2711_render_execution base;
	int answers[TEST_ANSWERS];
	unsigned answer_count;
	unsigned settles;
	bool waits[TEST_ANSWERS];
	unsigned steps;
	bool ended;
	int end_error;
};

/* The engine the controller's native space names; one test at a time. */
static struct bcm2711_v3d test_engine;

/* The controller under test. */
static struct bcm2711_render_device test_controller;

/* The completions delivered, in order, with their outcomes. */
static struct drv_gpu_completion *test_completed[TEST_COMPLETIONS];
static int test_completed_errors[TEST_COMPLETIONS];
static unsigned test_completed_count;

/* How many times the engine's loss was published to common observers. */
static unsigned test_failures;

/* A clock that moves one tick at each reading. */
static uint64_t test_ticks;

static int test_step(struct bcm2711_render_device *controller, struct bcm2711_render_execution *base, bool *done);
static int test_end(struct bcm2711_render_device *controller, struct bcm2711_render_execution *base, int error);
static int test_settle(struct bcm2711_render_device *controller, struct bcm2711_render_execution *base, bool wait);
static void test_setup(void);
static void test_session(struct bcm2711_render_session *session);
static void test_execution(struct test_execution *execution, const int *answers, unsigned count);
static struct bcm2711_render_request *test_reserve(struct bcm2711_render_session *session, uintptr_t completion);
static void test_commit(struct bcm2711_render_session *session, struct bcm2711_render_request *request);
static bool test_completed_with(uintptr_t completion, int error);
static void own_marker_test(void);
static void unknown_marker_test(void);
static void stop_test(void);
static void failure_test(void);
static void contended_test(void);

/*
 * Runs the settling tests.
 */
int
main(
	void)
{
	/* Each test starts from a fresh controller. */
	own_marker_test();
	unknown_marker_test();
	stop_test();
	failure_test();
	contended_test();

	/* Succeeded: every settling test passed. */
	puts("ws236 p010 executor settling list: PASS");
	return 0;
}

/*
 * Returns a ready engine's lock state; the fixture runs on one thread.
 */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* No interrupt state to save on the host. */
	(void)lock;

	/* Succeeded: nothing to restore. */
	return 0;
}

/*
 * Gives back a lock the fixture never really took.
 */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	/* Nothing was saved. */
	(void)lock;
	(void)enabled;

	/* Succeeded: nothing to restore. */
	return;
}

/*
 * Counts the controller mutex as held.
 */
void
mutex_lock(
	struct mutex *mutex)
{
	/* The single thread never finds it held. */
	assert(mutex->locked == 0);
	mutex->locked = 1;

	/* Succeeded: the mutex is held. */
	return;
}

/*
 * Counts the controller mutex as free.
 */
void
mutex_unlock(
	struct mutex *mutex)
{
	/* Only the holder gives it back. */
	assert(mutex->locked == 1);
	mutex->locked = 0;

	/* Succeeded: the mutex is free. */
	return;
}

/*
 * Starts a wait queue with no waiter.
 */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* The fixture only counts wakeups. */
	memset(queue, 0, sizeof(*queue));
	queue->name = name;

	/* Succeeded: the queue is empty. */
	return;
}

/*
 * Reads a wait queue's wakeup count.
 */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Succeeded: the count of wakeups. */
	return queue->sequence;
}

/*
 * Counts one wakeup.
 */
void
waitq_wake_one(
	struct wait_queue *queue)
{
	/* No thread sleeps in the fixture. */
	queue->sequence++;

	/* Succeeded: the wakeup is counted. */
	return;
}

/*
 * Counts a wakeup of every waiter.
 */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* No thread sleeps in the fixture. */
	queue->sequence++;

	/* Succeeded: the wakeup is counted. */
	return;
}

/*
 * Refuses to sleep: the tests never reach a wait.
 */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *guard,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* A sleep would hang the single thread. */
	(void)queue;
	(void)guard;
	(void)observed;
	(void)deadline;
	(void)flags;
	assert(!"the fixture never sleeps");
	return EAGAIN;
}

/*
 * Reads the fixture clock.
 */
uint64_t
clock_ticks(
	void)
{
	/* Each reading is one tick later. */
	test_ticks++;

	/* Succeeded: the tick count. */
	return test_ticks;
}

/*
 * Prints a kernel log line.
 */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* The line goes to the test's output. */
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);

	/* Succeeded: the line is printed. */
	return;
}

/*
 * Records a delivered completion and its outcome.
 */
void
drv_gpu_complete(
	struct drv_gpu_completion *completion,
	int error)
{
	/* The fixture keeps them in order. */
	assert(test_completed_count < TEST_COMPLETIONS);
	test_completed[test_completed_count] = completion;
	test_completed_errors[test_completed_count] = error;
	test_completed_count++;

	/* Succeeded: the completion is recorded. */
	return;
}

/*
 * Ignores a capacity change.
 */
void
drv_gpu_capacity_changed(
	struct drv_gpu_device *device)
{
	/* No producer waits in the fixture. */
	(void)device;

	/* Succeeded: nothing to tell. */
	return;
}

/*
 * Counts the engine's loss published to common observers.
 */
void
bcm2711_render_fail(
	struct bcm2711_render_device *controller,
	int error)
{
	/* The test checks the count. */
	(void)controller;
	(void)error;
	test_failures++;

	/* Succeeded: the loss is counted. */
	return;
}

/*
 * Has no delayed view to unmap.
 */
int
bcm2711_v3d_memory_release_delayed(
	struct bcm2711_v3d_space *space)
{
	/* The fixture maps nothing. */
	(void)space;

	/* Succeeded: nothing was delayed. */
	return 0;
}

/*
 * Has no cost report to flush.
 */
void
bcm2711_vulkan_native_perf_flush(
	void)
{
	/* The fixture formats none. */

	/* Succeeded: nothing to flush. */
	return;
}

/* Runs a fixture execution's one step: it finishes at once. */
static int
test_step(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_execution *base,
	bool *done)
{
	struct test_execution *execution;

	/* One step, and the execution has run. */
	(void)controller;
	execution = (struct test_execution *)base;
	assert(!execution->ended);
	execution->steps++;
	*done = true;

	/* Succeeded: the step ran. */
	return 0;
}

/* Records a fixture execution's end and the outcome it received. */
static int
test_end(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_execution *base,
	int error)
{
	struct test_execution *execution;

	/* An execution ends once. */
	(void)controller;
	execution = (struct test_execution *)base;
	assert(!execution->ended);
	execution->ended = true;
	execution->end_error = error;

	/* Succeeded: nothing to release. */
	return 0;
}

/* Answers a fixture execution's settle from its script; the last answer repeats. */
static int
test_settle(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_execution *base,
	bool wait)
{
	struct test_execution *execution;
	unsigned answer;

	/* The settle comes only after the step, under the mutex, before the end. */
	execution = (struct test_execution *)base;
	assert(controller->mutex.locked == 1);
	assert(execution->steps == 1 && !execution->ended);

	/* The next answer, the last one once the script is used up. */
	answer = execution->settles;
	if (answer >= execution->answer_count)
		answer = execution->answer_count - 1U;
	if (execution->settles < TEST_ANSWERS)
		execution->waits[execution->settles] = wait;
	execution->settles++;

	/* Succeeded: the scripted answer. */
	return execution->answers[answer];
}

/* Starts a fresh controller over a ready engine. */
static void
test_setup(
	void)
{
	/* The engine is ready and the worker empty. */
	memset(&test_engine, 0, sizeof(test_engine));
	memset(&test_controller, 0, sizeof(test_controller));
	test_engine.hardware.ready = true;
	test_engine.hardware.initialized = true;
	test_engine.power.ready = true;
	test_controller.space.native = &test_engine;
	bcm2711_render_worker_init(&test_controller);
	bcm2711_render_jobs_bind(&test_controller.operations);
	test_completed_count = 0;
	test_failures = 0;

	/* Succeeded: the controller is fresh. */
	return;
}

/* Opens a fresh session that owns the fixture's queue domain. */
static void
test_session(
	struct bcm2711_render_session *session)
{
	int error;

	/* The session counts for the marker minimum and owns the domain. */
	memset(session, 0, sizeof(*session));
	session->device = &test_controller;
	session->timelines = UINT64_C(1) << TEST_TIMELINE;
	error = bcm2711_render_worker_session_open(&test_controller);
	assert(error == 0);

	/* Succeeded: the session is open. */
	return;
}

/* Prepares a fixture execution with a settle script, or none when count is zero. */
static void
test_execution(
	struct test_execution *execution,
	const int *answers,
	unsigned count)
{
	unsigned index;

	/* The callbacks, the domain, and the script. */
	memset(execution, 0, sizeof(*execution));
	execution->base.step = test_step;
	execution->base.end = test_end;
	execution->base.timeline = TEST_TIMELINE;
	assert(count <= TEST_ANSWERS);
	for (index = 0; index < count; index++)
		execution->answers[index] = answers[index];
	execution->answer_count = count;
	if (count != 0)
		execution->base.settle = test_settle;

	/* Succeeded: the execution is ready to submit. */
	return;
}

/* Reserves a marker of the fixture's domain for a completion; returns its slot. */
static struct bcm2711_render_request *
test_reserve(
	struct bcm2711_render_session *session,
	uintptr_t completion)
{
	void *token;
	int error;

	/* The producer's reservation, before its submission. */
	error = test_controller.operations.jobs->reserve(&test_controller, session, TEST_TIMELINE, (struct drv_gpu_completion *)completion, &token);
	assert(error == 0 && token != NULL);

	/* Succeeded: the reserved marker. */
	return token;
}

/* Commits a reserved marker and lets the worker run it, so it parks or completes. */
static void
test_commit(
	struct bcm2711_render_session *session,
	struct bcm2711_render_request *request)
{
	int error;

	/* The producer's commit after its submission was accepted. */
	error = test_controller.operations.jobs->commit(&test_controller, session, request, request->completion);
	assert(error == 0);

	/* The worker takes the marker from its FIFO. */
	error = bcm2711_render_worker_step(&test_controller);
	assert(error == 0);

	/* Succeeded: the marker is parked or complete. */
	return;
}

/* Tells whether a completion was delivered with an outcome. */
static bool
test_completed_with(
	uintptr_t completion,
	int error)
{
	unsigned index;

	/* The delivered completions, in order. */
	for (index = 0; index < test_completed_count; index++) {
		if (test_completed[index] != (struct drv_gpu_completion *)completion)
			continue;
		if (test_completed_errors[index] == error)
			return true;
	}

	/* Succeeded: not delivered with that outcome. */
	return false;
}

/* (a) A settling execution lets the next run and holds only its own marker. */
static void
own_marker_test(
	void)
{
	static const int answers[] = { EINPROGRESS, EINPROGRESS, 0 };
	struct bcm2711_render_session session;
	struct test_execution first;
	struct test_execution second;
	struct bcm2711_render_request *first_marker;
	struct bcm2711_render_request *second_marker;
	int error;

	/* Two submissions, each between its marker's reservation and commit, as libvulkan sends them. */
	test_setup();
	test_session(&session);
	test_execution(&first, answers, 3U);
	test_execution(&second, NULL, 0U);
	first_marker = test_reserve(&session, 1U);
	bcm2711_render_execution_submit(&session, &first.base);
	test_commit(&session, first_marker);
	second_marker = test_reserve(&session, 2U);
	bcm2711_render_execution_submit(&session, &second.base);
	test_commit(&session, second_marker);
	assert(first.base.ordinal == 1U && second.base.ordinal == 2U);
	assert(first_marker->ordinal == 1U && second_marker->ordinal == 2U);
	assert(test_completed_count == 0 && test_controller.worker.parked != NULL);

	/* The first runs, its frame has not come: it settles, and the second is still queued. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(first.steps == 1 && !first.ended && first.settles == 1 && !first.waits[0]);
	assert(session.settling_head == &first.base && session.execution_head == &second.base);
	assert(session.executed == 1U && test_completed_count == 0);

	/* The second runs and ends behind it; its marker completes, the first's does not. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(second.ended && second.end_error == 0 && !first.ended && first.settles == 2);
	assert(session.executed == 2U && session.execution_head == NULL && session.settling_head == &first.base);
	assert(test_completed_count == 1 && test_completed_with(2U, 0));
	assert(session.ready && test_controller.worker.ready_head == &session);

	/* With nothing queued, the turn waits a little; the frame comes and the first ends with its marker. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(first.ended && first.end_error == 0 && first.settles == 3 && first.waits[2]);
	assert(test_completed_count == 2 && test_completed_with(1U, 0));
	assert(session.settling_head == NULL && session.pending == 0 && !session.ready);
	assert(test_controller.worker.executions == 0 && test_controller.worker.execution_bytes == 0);
	assert(test_controller.worker.parked == NULL && test_failures == 0);

	/* Nothing waits any more. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == EAGAIN);
	puts("ws236 p010 settling execution holds only its own marker: PASS");

	/* Succeeded: the own marker waited for its execution only. */
	return;
}

/* (b) Markers that do not know their execution wait for every settling one their count covers. */
static void
unknown_marker_test(
	void)
{
	static const int answers[] = { EINPROGRESS, 0 };
	struct bcm2711_render_session session;
	struct test_execution settling;
	struct bcm2711_render_request *first_marker;
	struct bcm2711_render_request *second_marker;
	int error;

	/* Two markers reserved at once: the submission cannot tell which is its own. */
	test_setup();
	test_session(&session);
	test_execution(&settling, answers, 2U);
	first_marker = test_reserve(&session, 3U);
	second_marker = test_reserve(&session, 4U);
	bcm2711_render_execution_submit(&session, &settling.base);
	assert(first_marker->ordinal == 0 && second_marker->ordinal == 0);
	test_commit(&session, first_marker);
	test_commit(&session, second_marker);

	/* It runs and settles; both markers stay parked though executed reached their count. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(!settling.ended && session.executed == 1U && session.settling_head == &settling.base);
	assert(test_completed_count == 0 && test_controller.worker.parked != NULL);

	/* Its frame comes: it ends, and both complete. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(settling.ended && settling.end_error == 0);
	assert(test_completed_with(3U, 0) && test_completed_with(4U, 0));
	assert(session.pending == 0 && test_controller.worker.parked == NULL);
	puts("ws236 p010 unknown marker waits for the settling executions it covers: PASS");

	/* Succeeded: the unknown markers waited for the settling execution. */
	return;
}

/* (c) A stop ends a settling execution without its settle, and its marker reports the cancel. */
static void
stop_test(
	void)
{
	static const int answers[] = { EINPROGRESS };
	struct bcm2711_render_session session;
	struct test_execution settling;
	struct bcm2711_render_request *marker;
	unsigned settles;
	int error;

	/* One settling execution with its marker parked. */
	test_setup();
	test_session(&session);
	test_execution(&settling, answers, 1U);
	marker = test_reserve(&session, 5U);
	bcm2711_render_execution_submit(&session, &settling.base);
	test_commit(&session, marker);
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0 && !settling.ended);

	/* The session stops: the next turn ends it with ECANCELED and asks its settle nothing more. */
	bcm2711_render_worker_stop(&session, ECANCELED);
	settles = settling.settles;
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(settling.ended && settling.end_error == ECANCELED && settling.settles == settles);
	assert(test_completed_with(5U, ECANCELED) && session.pending == 0 && test_failures == 0);
	assert(!test_controller.worker.uncertain);
	puts("ws236 p010 stop ends a settling execution: PASS");

	/* Succeeded: the stop ended the settling execution. */
	return;
}

/* (d) A settle that fails ends its execution with the failure and faults the engine. */
static void
failure_test(
	void)
{
	static const int answers[] = { EINPROGRESS, EFAULT };
	struct bcm2711_render_session session;
	struct test_execution settling;
	int error;

	/* One settling execution. */
	test_setup();
	test_session(&session);
	test_execution(&settling, answers, 2U);
	bcm2711_render_execution_submit(&session, &settling.base);
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0 && !settling.ended);

	/* Its settle fails: it ends with the failure, the engine is faulted and the loss published. */
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0);
	assert(settling.ended && settling.end_error == EFAULT);
	assert(test_controller.worker.uncertain && test_engine.hardware.faulted && test_failures == 1);
	assert(session.pending == 0 && session.settling_head == NULL);
	puts("ws236 p010 failed settle faults the engine: PASS");

	/* Succeeded: the failed settle faulted the engine. */
	return;
}

/*
 * (e) A turn of only settling executions only looks while another
 * session's execution is queued, and waits a little once none is
 * (ws236-p011: the executor goes on with the others' work).
 */
static void
contended_test(
	void)
{
	static const int answers[] = { EINPROGRESS, EINPROGRESS, EINPROGRESS, EINPROGRESS, 0 };
	struct bcm2711_render_session first_session;
	struct bcm2711_render_session second_session;
	struct test_execution settling;
	struct test_execution other;
	unsigned settles;
	unsigned step;
	unsigned looked;
	bool queued;
	int error;

	/* One session's execution runs and settles. */
	looked = 0U;
	test_setup();
	test_session(&first_session);
	test_session(&second_session);
	test_execution(&settling, answers, 5U);
	test_execution(&other, NULL, 0U);
	bcm2711_render_execution_submit(&first_session, &settling.base);
	error = bcm2711_render_executor_step(&test_controller);
	assert(error == 0 && !settling.ended && settling.settles == 1U && !settling.waits[0]);

	/* Another session's execution comes: every settle while it is queued only looks. */
	bcm2711_render_execution_submit(&second_session, &other.base);
	for (step = 0; step < 4U && !settling.ended; step++) {
		queued = !other.ended;
		settles = settling.settles;
		error = bcm2711_render_executor_step(&test_controller);
		assert(error == 0);
		if (queued && settling.settles != settles && !other.ended) {
			assert(!settling.waits[settling.settles - 1U]);
			looked++;
		}
	}
	assert(looked != 0U);

	/* The other ran; with nothing else queued the settling turns wait a little again. */
	assert(other.ended && other.end_error == 0);
	while (!settling.ended) {
		settles = settling.settles;
		error = bcm2711_render_executor_step(&test_controller);
		assert(error == 0 && settling.settles == settles + 1U);
		assert(settling.waits[settling.settles - 1U]);
	}
	assert(settling.end_error == 0 && test_failures == 0);
	puts("ws236 p011 settling turn yields to another session's execution: PASS");

	/* Succeeded: the settling waits gave way to the queued work. */
	return;
}
