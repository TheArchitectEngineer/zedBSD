/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the render thread's tasks
 * (userland/desktop/wayland/render-thread.c): a task is submitted before
 * a list given after it, completions come in the order the tasks were
 * given, a failed submission is told in its turn, tasks run while the
 * thread is parked, a park asked without waiting is answered only after
 * the task given before it (drained) and told after its completion, and a
 * stop waits for the tasks in flight.
 */

#include "userland/desktop/wayland/render-thread.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* How many fake fences and submissions the test keeps. */
#define TEST_FENCES	16U
#define TEST_LOG	64U

/* What the fake operations saw, under test_lock: the submissions in order (task numbers, 1000 + a job's number), and each fence's state. */
static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long submitted[TEST_LOG];
static unsigned submitted_count;
static unsigned signalled[TEST_FENCES];
static unsigned fail_submit[TEST_FENCES];
static unsigned failures;

static VkResult op_acquire_output(void *context, uint64_t timeout_ns, uint32_t *image, uint64_t *cycles, uint64_t *at_us);
static VkResult op_acquire_head(void *context, unsigned slot, uint32_t *image);
static void op_run(void *context, const struct kwl_render_job *job, struct kwl_render_outcome *outcome);
static VkResult op_wait(void *context, unsigned slot, uint64_t timeout_ns);
static VkResult op_submit_task(void *context, VkCommandBuffer command, VkFence fence);
static VkResult op_wait_task(void *context, VkFence fence, uint64_t timeout_ns);
static struct kwl_render_message *task_new(unsigned number);
static void log_add(unsigned long entry);
static void signal_fence(unsigned number);
static void check(int condition, const char *what);
static unsigned take_tasks(struct kwl_render_thread *render, unsigned *numbers, VkResult *results, unsigned capacity, unsigned *ready);
static void steps(struct kwl_render_thread *render, unsigned count);
static void *stop_main(void *argument);
static void sleep_ms(unsigned milliseconds);

/* The fake operations. */
static const struct kwl_render_ops test_ops = {
	op_acquire_output,
	op_acquire_head,
	op_run,
	op_wait,
	NULL,
	op_submit_task,
	op_wait_task
};

int
main(void)
{
	struct kwl_render_thread render;
	struct kwl_render_message *message;
	struct kwl_render_message *next;
	struct kwl_render_job job;
	pthread_t stopper;
	unsigned numbers[8];
	VkResult results[8];
	unsigned count;
	unsigned index;
	unsigned ready;
	uint64_t number;
	int pipes[2];
	int error;

	/* The main thread's wake pipe, never blocking. */
	error = pipe(pipes);
	check(error == 0, "pipe");
	(void)fcntl(pipes[1], F_SETFL, O_NONBLOCK);
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);

	/* A: a task, a want, its ready, a second task, then the list: both tasks are submitted before the job. */
	error = kwl_render_init(&render, &test_ops, NULL, pipes[1]);
	check(error == 0, "init");
	kwl_render_task(&render, task_new(1U));
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	steps(&render, 3U);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 0U, "A: no task complete before its fence");
	check(ready == 1U, "A: the ready came");
	kwl_render_task(&render, task_new(2U));
	memset(&job, 0, sizeof(job));
	job.number = number;
	job.kind = KWL_RENDER_FRAME;
	kwl_render_answer(&render, number, KWL_RENDER_ANSWER_LIST, &job);
	steps(&render, 2U);
	pthread_mutex_lock(&test_lock);

	check(submitted_count == 3U, "A: three submissions");
	check(submitted[0] == 1U, "A: task 1 first");
	check(submitted[1] == 2U, "A: task 2 before the job given after it");
	check(submitted[2] == 1000U + number, "A: the job last");

	pthread_mutex_unlock(&test_lock);

	/* The second fence first: nothing is told until the first is signalled, then both in order. */
	signal_fence(2U);
	steps(&render, 3U);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 0U, "A: task 2 waits for task 1");
	signal_fence(1U);
	steps(&render, 3U);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 2U, "A: both completions");
	check(numbers[0] == 1U && numbers[1] == 2U, "A: completions in order");

	/* B: a failed submission is told in its turn, after the task before it. */
	fail_submit[4] = 1U;
	kwl_render_task(&render, task_new(3U));
	kwl_render_task(&render, task_new(4U));
	steps(&render, 2U);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 0U, "B: the failure waits for task 3");
	signal_fence(3U);
	steps(&render, 2U);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 2U, "B: both told");
	check(numbers[0] == 3U && results[0] == VK_SUCCESS, "B: task 3 succeeded");
	check(numbers[1] == 4U && results[1] == VK_ERROR_DEVICE_LOST, "B: task 4 failed");

	/* C: while parked, a task is submitted and its completion told. */
	error = kwl_render_start(&render);
	check(error == 0, "start");
	error = kwl_render_park(&render, 1000U);
	check(error == 0, "C: parked");
	kwl_render_task(&render, task_new(5U));
	sleep_ms(20U);
	signal_fence(5U);
	count = 0U;
	for (index = 0U; index < 100U && count == 0U; index++) {
		sleep_ms(10U);
		count = take_tasks(&render, numbers, results, 8U, &ready);
	}
	check(count == 1U && numbers[0] == 5U, "C: task 5 completed and told while parked");
	kwl_render_unpark(&render);

	/* E: a park asked without waiting drains the task given before it, and is told after the task's completion. */
	kwl_render_task(&render, task_new(7U));
	sleep_ms(20U);
	message = calloc(1, sizeof(*message));
	check(message != NULL, "E: message");
	kwl_render_park_request(&render, message);
	sleep_ms(50U);
	pthread_mutex_lock(&render.lock);

	check(render.parked == 0U, "E: not parked before task 7 completes");

	pthread_mutex_unlock(&render.lock);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 0U, "E: nothing told before task 7's fence");
	signal_fence(7U);
	count = 0U;
	for (index = 0U; index < 100U && count < 2U; index++) {
		sleep_ms(10U);
		count += take_tasks(&render, numbers + count, results + count, 8U - count, &ready);
	}
	check(count == 2U && numbers[0] == 7U && numbers[1] == 0U, "E: task 7's completion, then the park's message");
	pthread_mutex_lock(&render.lock);

	check(render.parked == 1U, "E: parked");

	pthread_mutex_unlock(&render.lock);
	kwl_render_unpark(&render);

	/* D: a stop waits for the task in flight. */
	kwl_render_task(&render, task_new(6U));
	sleep_ms(20U);
	error = pthread_create(&stopper, NULL, stop_main, &render);
	check(error == 0, "D: stopper");
	sleep_ms(50U);
	pthread_mutex_lock(&test_lock);

	check(submitted[submitted_count - 1U] == 6U, "D: task 6 submitted");

	pthread_mutex_unlock(&test_lock);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 0U, "D: task 6 not complete before its fence");
	signal_fence(6U);
	(void)pthread_join(stopper, NULL);
	count = take_tasks(&render, numbers, results, 8U, &ready);
	check(count == 1U && numbers[0] == 6U, "D: the stop came after task 6's completion");

	/* The rest. */
	message = kwl_render_take(&render);
	while (message != NULL) {
		next = message->next;
		free(message);
		message = next;
	}
	kwl_render_destroy(&render);
	close(pipes[0]);
	close(pipes[1]);

	/* The summary. */
	if (failures != 0U) {
		printf("FAIL failures=%u\n", failures);
		return 1;
	}
	printf("PASS render tasks\n");
	return 0;
}

/* Gives the output's image 0 at once. */
static VkResult
op_acquire_output(
	void *context,
	uint64_t timeout_ns,
	uint32_t *image,
	uint64_t *cycles,
	uint64_t *at_us)
{
	(void)context;
	(void)timeout_ns;

	/* Always free. */
	*image = 0U;
	*cycles = 0U;
	*at_us = 0U;
	return VK_SUCCESS;
}

/* Gives a head's image 0 at once. */
static VkResult
op_acquire_head(
	void *context,
	unsigned slot,
	uint32_t *image)
{
	(void)context;
	(void)slot;

	/* Always free. */
	*image = 0U;
	return VK_SUCCESS;
}

/* Logs a job's submission. */
static void
op_run(
	void *context,
	const struct kwl_render_job *job,
	struct kwl_render_outcome *outcome)
{
	(void)context;

	/* Submitted. */
	log_add(1000U + (unsigned long)job->number);
	outcome->result = VK_SUCCESS;
}

/* A job's fence is signalled at once. */
static VkResult
op_wait(
	void *context,
	unsigned slot,
	uint64_t timeout_ns)
{
	(void)context;
	(void)slot;
	(void)timeout_ns;

	/* Done. */
	return VK_SUCCESS;
}

/* Logs a task's submission, or fails the one asked to. */
static VkResult
op_submit_task(
	void *context,
	VkCommandBuffer command,
	VkFence fence)
{
	unsigned number;

	(void)context;
	(void)fence;

	/* The task's number is its command buffer's. */
	number = (unsigned)(uintptr_t)command;
	if (number < TEST_FENCES && fail_submit[number])
		return VK_ERROR_DEVICE_LOST;
	log_add(number);
	return VK_SUCCESS;
}

/* A task's fence: signalled once the test says so. */
static VkResult
op_wait_task(
	void *context,
	VkFence fence,
	uint64_t timeout_ns)
{
	unsigned number;
	unsigned state;

	(void)context;

	/* The fence's state now, then again after the wait asked for. */
	number = (unsigned)(uintptr_t)fence;
	pthread_mutex_lock(&test_lock);

	state = signalled[number % TEST_FENCES];

	pthread_mutex_unlock(&test_lock);
	if (!state && timeout_ns > 0U) {
		sleep_ms((unsigned)(timeout_ns / 1000000U) + 1U);
		pthread_mutex_lock(&test_lock);

		state = signalled[number % TEST_FENCES];

		pthread_mutex_unlock(&test_lock);
	}
	if (!state)
		return VK_TIMEOUT;
	return VK_SUCCESS;
}

/* Makes a task's message: its number is its command buffer's and its fence's. */
static struct kwl_render_message *
task_new(
	unsigned number)
{
	struct kwl_render_message *task;

	/* The record the thread posts back. */
	task = calloc(1, sizeof(*task));
	if (task == NULL)
		abort();
	task->number = number;
	task->task_command = (VkCommandBuffer)(uintptr_t)number;
	task->task_fence = (VkFence)(uintptr_t)number;
	return task;
}

/* Adds a submission to the log. */
static void
log_add(
	unsigned long entry)
{
	/* In order. */
	pthread_mutex_lock(&test_lock);

	if (submitted_count < TEST_LOG)
		submitted[submitted_count++] = entry;

	pthread_mutex_unlock(&test_lock);
}

/* Signals a task's fence. */
static void
signal_fence(
	unsigned number)
{
	/* Seen by the next wait. */
	pthread_mutex_lock(&test_lock);

	signalled[number % TEST_FENCES] = 1U;

	pthread_mutex_unlock(&test_lock);
}

/* Counts a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failure is printed. */
	if (condition)
		return;
	printf("FAIL %s\n", what);
	failures++;
}

/* Takes the messages: the tasks' completions (number and result), freed; whether a ready came. */
static unsigned
take_tasks(
	struct kwl_render_thread *render,
	unsigned *numbers,
	VkResult *results,
	unsigned capacity,
	unsigned *ready)
{
	struct kwl_render_message *messages;
	struct kwl_render_message *message;
	unsigned count;

	/* Each message, oldest first. */
	count = 0U;
	*ready = 0U;
	messages = kwl_render_take(render);
	while (messages != NULL) {
		message = messages;
		messages = message->next;
		if (message->kind == KWL_RENDER_READY)
			*ready = 1U;
		if (message->kind == KWL_RENDER_TASK_DONE && count < capacity) {
			numbers[count] = (unsigned)message->number;
			results[count] = message->result;
			count++;
		}

		/* A park's message is told as the number 0. */
		if (message->kind == KWL_RENDER_PARKED && count < capacity) {
			numbers[count] = 0U;
			results[count] = VK_SUCCESS;
			count++;
		}
		free(message);
	}
	return count;
}

/* Runs steps of the render thread on the test's thread. */
static void
steps(
	struct kwl_render_thread *render,
	unsigned count)
{
	unsigned index;

	/* Each step. */
	for (index = 0U; index < count; index++)
		(void)kwl_render_step(render);
}

/* Stops the render thread. */
static void *
stop_main(
	void *argument)
{
	/* Returns once the thread has ended. */
	kwl_render_stop(argument);
	return NULL;
}

/* Sleeps a while. */
static void
sleep_ms(
	unsigned milliseconds)
{
	struct timespec wait;

	/* The whole time. */
	wait.tv_sec = (time_t)(milliseconds / 1000U);
	wait.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&wait, NULL);
}
