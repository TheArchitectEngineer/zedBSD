/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the render thread's pull (ws213-p004, the design's 9 (b),
 * (c), (d), (e), (h) and (m); userland/desktop/wayland/render-thread.c).
 * The Vulkan work is fake operations on a fake swapchain of three images
 * and heads of two images each.
 *
 * The first part runs the thread's steps one at a time against the main
 * thread's side: nothing is acquired without a want; a want acquires the
 * output's image and the wanted heads' and answers ready; a failed acquire
 * takes the want down only if it is still the same (compare and clear) and
 * nothing is acquired again until it is raised anew; a new list of heads
 * keeps the number; a cancel keeps the images, and the next ready reuses
 * them without a second acquire; a held head is presented with the next
 * list; a completion comes before the ready that follows it; a list takes
 * the want down only if it is still the same; a park waits for the frame
 * in flight, holds the images, and a forget there drops them; a job that
 * could not be submitted completes at once.
 *
 * The second part runs the real thread against a main thread that wants,
 * walks, answers, cancels, parks and forgets at random for 100000 steps;
 * the fake operations check that no Vulkan is called between a ready and
 * its answer (the main thread's walk), while parked, or with the thread's
 * lock held, and that no image is acquired twice before it is presented.
 * Built with -fsanitize=thread it is the design's 9 (m) for the render
 * thread.
 */

#include "userland/desktop/wayland/render-thread.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* The fake output's images and each fake head's. */
#define OUTPUT_IMAGES		3U
#define HEAD_IMAGES		2U

/* The steps of the threaded part. */
#define THREAD_STEPS		100000U

/* The checks run and failed. */
static unsigned checks;
static unsigned failures;

/* The render thread under test. */
static struct kwl_render_thread render;

/*
 * The fake swapchains: whether each image is acquired and not presented,
 * the next image each gives, and what the next acquire answers (VK_SUCCESS
 * normally).
 */
static atomic_int output_acquired[OUTPUT_IMAGES];
static atomic_int head_acquired[KWL_RENDER_HEADS][HEAD_IMAGES];
static atomic_uint output_next;
static atomic_uint head_next[KWL_RENDER_HEADS];
static atomic_int output_answer;
static atomic_int head_answer;
static atomic_int run_answer;
static atomic_int wait_answer;

/* The counts the checks read. */
static atomic_uint output_acquires;
static atomic_uint head_acquires;
static atomic_uint runs;
static atomic_uint waits;
static atomic_uint double_acquires;
static atomic_uint bad_presents;

/* The faults the operations find: a call during the walk, or with the lock held. */
static atomic_int walking;
static atomic_uint walk_calls;
static atomic_uint locked_calls;

/* A want the fake acquire changes while it runs (the compare and clear's case), and its kind. */
static atomic_int change_want_in_acquire;

static void check(int condition, const char *what);
static void operation(void);
static VkResult fake_acquire_output(void *context, uint64_t timeout_ns, uint32_t *image, uint64_t *cycles, uint64_t *at_us);
static VkResult fake_acquire_head(void *context, unsigned slot, uint32_t *image);
static void fake_run(void *context, const struct kwl_render_job *job, struct kwl_render_outcome *outcome);
static VkResult fake_wait(void *context, uint64_t timeout_ns);
static struct kwl_render_message *take_one(void);
static unsigned drain(struct kwl_render_message **list, unsigned room);
static void reset_fakes(void);
static void stepwise(void);
static void threaded(void);
static void job_from(const struct kwl_render_message *ready, struct kwl_render_job *job);

int
main(
	void)
{
	/* The steps one at a time, then the threads. */
	stepwise();
	threaded();

	/* The verdict. */
	if (failures != 0U) {
		printf("render-thread-host-test: FAIL (%u of %u checks)\n", failures, checks);
		return 1;
	}
	printf("render-thread-host-test: PASS (%u checks)\n", checks);
	return 0;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check. */
	checks++;
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Checks one operation's call: not during the walk, not with the lock held. */
static void
operation(void)
{
	/* The main thread's walk. */
	if (atomic_load(&walking))
		atomic_fetch_add(&walk_calls, 1U);

	/* glibc's mutex keeps its owner's thread number. */
	if (render.lock.__data.__owner == (int)syscall(SYS_gettid))
		atomic_fetch_add(&locked_calls, 1U);
}

/* Acquires the fake output's next image, failing when it is acquired already. */
static VkResult
fake_acquire_output(
	void *context,
	uint64_t timeout_ns,
	uint32_t *image,
	uint64_t *cycles,
	uint64_t *at_us)
{
	uint64_t number;
	unsigned next;
	int answer;

	(void)context;
	(void)timeout_ns;
	operation();
	atomic_fetch_add(&output_acquires, 1U);
	*cycles = 1U;
	*at_us = 1U;

	/* The main thread changes the want meanwhile (compare and clear). */
	if (atomic_load(&change_want_in_acquire)) {
		atomic_store(&change_want_in_acquire, 0);
		kwl_render_set_want(&render, KWL_RENDER_LAYER, NULL, 0U, &number);
	}

	/* A failure asked for. */
	answer = atomic_load(&output_answer);
	if (answer != VK_SUCCESS)
		return (VkResult)answer;

	/* The next image not acquired, round robin; none free is not ready. */
	next = atomic_fetch_add(&output_next, 1U) % OUTPUT_IMAGES;
	if (atomic_load(&output_acquired[next])) {
		if (atomic_load(&output_acquired[(next + 1U) % OUTPUT_IMAGES]) && atomic_load(&output_acquired[(next + 2U) % OUTPUT_IMAGES]))
			atomic_fetch_add(&double_acquires, 1U);
		return VK_NOT_READY;
	}
	atomic_store(&output_acquired[next], 1);
	*image = next;
	return VK_SUCCESS;
}

/* Acquires a fake head's next image without waiting. */
static VkResult
fake_acquire_head(
	void *context,
	unsigned slot,
	uint32_t *image)
{
	unsigned next;
	int answer;

	(void)context;
	operation();
	atomic_fetch_add(&head_acquires, 1U);

	/* A failure asked for. */
	answer = atomic_load(&head_answer);
	if (answer != VK_SUCCESS)
		return (VkResult)answer;

	/* A second image of a head while one is held is the double acquire the design forbids (H-A). */
	if (atomic_load(&head_acquired[slot][0]) || atomic_load(&head_acquired[slot][1]))
		atomic_fetch_add(&double_acquires, 1U);

	/* The next image. */
	next = atomic_fetch_add(&head_next[slot], 1U) % HEAD_IMAGES;
	if (atomic_load(&head_acquired[slot][next]))
		return VK_NOT_READY;
	atomic_store(&head_acquired[slot][next], 1);
	*image = next;
	return VK_SUCCESS;
}

/* Runs a job: presents its images, which must be acquired. */
static void
fake_run(
	void *context,
	const struct kwl_render_job *job,
	struct kwl_render_outcome *outcome)
{
	unsigned slot;

	(void)context;
	operation();
	atomic_fetch_add(&runs, 1U);
	memset(outcome, 0, sizeof(*outcome));
	outcome->result = (VkResult)atomic_load(&run_answer);
	if (outcome->result != VK_SUCCESS)
		return;

	/* The output's image, presented. */
	if (job->kind == KWL_RENDER_FRAME) {
		if (!atomic_load(&output_acquired[job->output_image % OUTPUT_IMAGES])) {
			atomic_fetch_add(&bad_presents, 1U);
			fprintf(stderr, "bad output present image=%u number=%llu\n", job->output_image, (unsigned long long)job->number);
		}
		atomic_store(&output_acquired[job->output_image % OUTPUT_IMAGES], 0);
	}

	/* Each head's. */
	for (slot = 0; slot < KWL_RENDER_HEADS; slot++) {
		if (!job->heads[slot])
			continue;
		if (!atomic_load(&head_acquired[slot][job->head_images[slot] % HEAD_IMAGES])) {
			atomic_fetch_add(&bad_presents, 1U);
			fprintf(stderr, "bad head present slot=%u image=%u number=%llu\n", slot, job->head_images[slot], (unsigned long long)job->number);
		}
		atomic_store(&head_acquired[slot][job->head_images[slot] % HEAD_IMAGES], 0);
		outcome->heads[slot] = 1U;
	}
}

/* Waits for the fake fence: done, or a timeout asked for. */
static VkResult
fake_wait(
	void *context,
	uint64_t timeout_ns)
{
	(void)context;
	(void)timeout_ns;
	operation();
	atomic_fetch_add(&waits, 1U);
	return (VkResult)atomic_load(&wait_answer);
}

/* Takes the queue and gives its first message, freeing the rest (the first part sends one at a time). */
static struct kwl_render_message *
take_one(void)
{
	struct kwl_render_message *messages;
	struct kwl_render_message *rest;

	/* The first; the others are freed. */
	messages = kwl_render_take(&render);
	if (messages == NULL)
		return NULL;
	while (messages->next != NULL) {
		rest = messages->next;
		messages->next = rest->next;
		free(rest);
	}
	return messages;
}

/* Takes every message in order into a list, up to room. */
static unsigned
drain(
	struct kwl_render_message **list,
	unsigned room)
{
	struct kwl_render_message *messages;
	unsigned count;

	/* Each in order. */
	count = 0U;
	messages = kwl_render_take(&render);
	while (messages != NULL) {
		if (count < room) {
			list[count] = messages;
			count++;
			messages = messages->next;
			list[count - 1U]->next = NULL;
			continue;
		}
		list[0] = messages->next;
		free(messages);
		messages = list[0];
	}
	return count;
}

/* Makes a job from a ready: its number and kind, the output's image and every held head. */
static void
job_from(
	const struct kwl_render_message *ready,
	struct kwl_render_job *job)
{
	unsigned slot;

	/* All held images presented. */
	memset(job, 0, sizeof(*job));
	job->number = ready->number;
	job->kind = ready->want_kind;
	job->output_image = ready->held.output_image;
	for (slot = 0; slot < KWL_RENDER_HEADS; slot++) {
		if (!ready->held.heads[slot])
			continue;
		job->heads[slot] = 1U;
		job->head_images[slot] = ready->held.head_images[slot];
	}
}

/* Starts the fakes afresh. */
static void
reset_fakes(void)
{
	unsigned slot;

	/* Every image free, every answer a success. */
	for (slot = 0; slot < OUTPUT_IMAGES; slot++)
		atomic_store(&output_acquired[slot], 0);
	for (slot = 0; slot < KWL_RENDER_HEADS; slot++) {
		atomic_store(&head_acquired[slot][0], 0);
		atomic_store(&head_acquired[slot][1], 0);
	}
	atomic_store(&output_answer, VK_SUCCESS);
	atomic_store(&head_answer, VK_SUCCESS);
	atomic_store(&run_answer, VK_SUCCESS);
	atomic_store(&wait_answer, VK_SUCCESS);
}

/* Runs the thread's steps one at a time against the main thread's side. */
static void
stepwise(void)
{
	struct kwl_render_ops ops;
	struct kwl_render_message *message;
	struct kwl_render_message *list[8];
	struct kwl_render_job job;
	enum kwl_render_step step;
	unsigned heads[2];
	unsigned before;
	unsigned count;
	uint64_t number;
	uint64_t layer_number;

	/* The thread's state with the fakes. */
	reset_fakes();
	memset(&ops, 0, sizeof(ops));
	ops.acquire_output = fake_acquire_output;
	ops.acquire_head = fake_acquire_head;
	ops.run = fake_run;
	ops.wait = fake_wait;
	check(kwl_render_init(&render, &ops, NULL, -1) == 0, "the render thread's state starts");

	/* (b) No want: nothing is acquired, and the thread is blocked. */
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_BLOCKED && atomic_load(&output_acquires) == 0U, "no want: no acquire");

	/* (b, c) A frame with head 1: the output's image and head 1's are acquired, then ready. */
	heads[0] = 1U;
	kwl_render_set_want(&render, KWL_RENDER_FRAME, heads, 1U, &number);
	check(number == 1U, "the first want is number 1");
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_READY && message->number == 1U, "ready for the want");
	check(message->held.output && message->held.heads[1] && !message->held.heads[0], "the output's and head 1's images held");
	check(atomic_load(&output_acquires) == 1U && atomic_load(&head_acquires) == 1U, "each acquired once");

	/* (b) A new list of heads keeps the number; the ready stands. */
	heads[1] = 2U;
	kwl_render_set_want(&render, KWL_RENDER_FRAME, heads, 2U, &number);
	check(number == 1U, "heads alone do not move the number");
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_BLOCKED, "the thread waits for the answer, calling nothing");

	/* (b) The kind changes: a new number; the old ready is cancelled and the images stay held. */
	kwl_render_set_want(&render, KWL_RENDER_LAYER, NULL, 0U, &layer_number);
	check(layer_number == 2U, "a new kind moves the number");
	kwl_render_answer(&render, 1U, KWL_RENDER_ANSWER_CANCEL, NULL);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	free(message);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_READY && message->number == 2U && message->want_kind == KWL_RENDER_LAYER, "ready for the layer");
	check(message->held.output && message->held.heads[1], "the images stay held across the cancel");

	/* (c) Back to a frame: the held images are reused, head 2 is acquired, nothing twice. */
	kwl_render_set_want(&render, KWL_RENDER_FRAME, heads, 2U, &number);
	kwl_render_answer(&render, 2U, KWL_RENDER_ANSWER_CANCEL, NULL);
	before = atomic_load(&output_acquires);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	free(message);
	message = take_one();
	check(number == 3U && message != NULL && message->number == 3U, "ready for the frame again");
	check(atomic_load(&output_acquires) == before, "the held output image is not acquired again");
	check(message->held.heads[1] && message->held.heads[2], "both heads held");
	check(atomic_load(&double_acquires) == 0U, "no head acquired twice");

	/* (c, e) The list presents every held image; the want goes down; the completion comes first. */
	job_from(message, &job);
	free(message);
	atomic_store(&walking, 0);
	kwl_render_answer(&render, 3U, KWL_RENDER_ANSWER_LIST, &job);
	(void)kwl_render_step(&render);
	check(atomic_load(&runs) == 1U && render.want.kind == KWL_RENDER_NONE, "the list ran and the want went down");
	check(atomic_load(&bad_presents) == 0U, "every presented image was acquired");
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	count = drain(list, 8U);
	check(count == 2U && list[0]->kind == KWL_RENDER_COMPLETED && list[1]->kind == KWL_RENDER_READY, "the completion before the next ready");
	check(list[0]->outcome.heads[1] && list[0]->outcome.heads[2], "the heads were presented");
	job_from(list[1], &job);
	free(list[0]);
	free(list[1]);

	/* (b) A list for a want raised anew meanwhile does not take the new want down (compare and clear). */
	kwl_render_set_want(&render, KWL_RENDER_LAYER, NULL, 0U, &number);
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
	(void)kwl_render_step(&render);
	check(render.want.kind == KWL_RENDER_LAYER && render.want.number == number, "the new want stays after the old list");
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_COMPLETED, "the job completed");
	free(message);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_READY && message->want_kind == KWL_RENDER_LAYER, "then ready for the layer");
	job_from(message, &job);
	free(message);
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_COMPLETED, "the layer's picture completed");
	free(message);

	/*
	 * (b) The same kind wanted again right after its list was given, before
	 * the thread took the list (place20's stall: the main thread's next pass
	 * wanted the next frame while the thread had not yet woken): a new
	 * number, and the want stays up after the list is taken, so the thread
	 * acquires for it after the completion.
	 */
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_READY && message->number == number, "ready for a frame");
	job_from(message, &job);
	free(message);
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	check(number == job.number + 1U, "a frame wanted again after its list is a new want");
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	check(render.want.kind == KWL_RENDER_FRAME && render.want.number == number, "the want raised again stays after the list is taken");
	(void)kwl_render_step(&render);
	count = drain(list, 8U);
	check(count == 2U && list[0]->kind == KWL_RENDER_COMPLETED && list[1]->kind == KWL_RENDER_READY && list[1]->number == number, "then ready for the new want");
	if (count == 2U) {
		job_from(list[1], &job);
		free(list[0]);
		free(list[1]);
	} else if (count == 1U) {
		free(list[0]);
	}

	/* The new want's list completes; the want goes down with it. */
	kwl_render_answer(&render, number, KWL_RENDER_ANSWER_LIST, &job);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_COMPLETED && render.want.kind == KWL_RENDER_NONE, "its list completed and the want went down");
	free(message);

	/* (b) A failed acquire takes the want down and is told; nothing is tried again until it is raised. */
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	atomic_store(&output_answer, VK_ERROR_OUT_OF_DATE_KHR);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_ACQUIRE_FAILED && message->result == VK_ERROR_OUT_OF_DATE_KHR, "the failure is told");
	free(message);
	check(render.want.kind == KWL_RENDER_NONE, "the want went down");
	before = atomic_load(&output_acquires);
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_BLOCKED && atomic_load(&output_acquires) == before, "no acquire until the want is raised again");

	/* (b) A failure while the main thread raised another want keeps that want (compare and clear). */
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	atomic_store(&change_want_in_acquire, 1);
	(void)kwl_render_step(&render);
	check(render.want.kind == KWL_RENDER_LAYER && render.want.number == number + 1U, "the want raised meanwhile stays");
	message = take_one();
	free(message);
	atomic_store(&output_answer, VK_SUCCESS);

	/* (h) A park waits for the frame in flight, then holds the images; a forget there drops them. */
	(void)kwl_render_step(&render);
	message = take_one();
	job_from(message, &job);
	free(message);
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_CANCEL, NULL);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->held.output, "ready for a frame, holding the output's image");
	job_from(message, &job);
	free(message);
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
	atomic_store(&wait_answer, VK_TIMEOUT);
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	render.park = 1U;
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_WORKED && !render.parked, "a park waits for the frame in flight");
	atomic_store(&wait_answer, VK_SUCCESS);
	(void)kwl_render_step(&render);
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_BLOCKED && render.parked, "then the thread parks");
	before = atomic_load(&output_acquires);
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	step = kwl_render_step(&render);
	check(step == KWL_RENDER_STEP_BLOCKED && atomic_load(&output_acquires) == before, "nothing is acquired while parked");
	kwl_render_unpark(&render);
	(void)kwl_render_step(&render);
	count = drain(list, 8U);
	check(count == 2U && list[1]->kind == KWL_RENDER_READY && list[1]->held.output, "after the park, ready with an image");
	free(list[0]);
	free(list[1]);
	render.park = 1U;
	(void)kwl_render_step(&render);
	check(render.parked, "parked while a ready waits for its answer");
	kwl_render_forget(&render, 1U, 0x7fU);
	check(!render.held.output, "the forget drops the held image");
	reset_fakes();
	kwl_render_answer(&render, number, KWL_RENDER_ANSWER_CANCEL, NULL);
	kwl_render_unpark(&render);
	(void)kwl_render_step(&render);
	before = atomic_load(&output_acquires);
	(void)kwl_render_step(&render);
	check(atomic_load(&output_acquires) == before + 1U, "a forgotten image is acquired again");
	message = take_one();
	free(message);

	/* (h) A park asked after the list was given waits for that job: the list never runs after the park. */
	check(render.state == KWL_RENDER_STATE_ANSWER && render.held.output, "a ready waits for its answer");
	memset(&job, 0, sizeof(job));
	job.number = render.ready_number;
	job.kind = KWL_RENDER_FRAME;
	job.output_image = render.held.output_image;
	kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
	render.park = 1U;
	before = atomic_load(&runs);
	(void)kwl_render_step(&render);
	check(atomic_load(&runs) == before + 1U && !render.parked, "the given list runs before the park");
	(void)kwl_render_step(&render);
	(void)kwl_render_step(&render);
	check(render.parked, "then the thread parks");
	message = take_one();
	free(message);
	render.park = 0U;
	kwl_render_set_want(&render, KWL_RENDER_FRAME, NULL, 0U, &number);
	(void)kwl_render_step(&render);
	message = take_one();
	free(message);

	/* A job that could not be submitted completes at once, without a wait. */
	message = NULL;
	job.number = number;
	job.kind = KWL_RENDER_FRAME;
	job.output_image = render.held.output_image;
	memset(job.heads, 0, sizeof(job.heads));
	atomic_store(&run_answer, VK_ERROR_DEVICE_LOST);
	before = atomic_load(&waits);
	kwl_render_answer(&render, number, KWL_RENDER_ANSWER_LIST, &job);
	(void)kwl_render_step(&render);
	message = take_one();
	check(message != NULL && message->kind == KWL_RENDER_COMPLETED && message->outcome.result == VK_ERROR_DEVICE_LOST && atomic_load(&waits) == before, "a failed job completes at once");
	free(message);
	check(atomic_load(&walk_calls) == 0U && atomic_load(&locked_calls) == 0U, "no operation with the lock held");
	kwl_render_destroy(&render);
}

/* Runs the real thread against a main thread that wants, walks, answers, parks and forgets at random. */
static void
threaded(void)
{
	struct kwl_render_ops ops;
	struct kwl_render_message *messages;
	struct kwl_render_message *message;
	struct kwl_render_message ready;
	struct kwl_render_job job;
	unsigned heads[2];
	enum kwl_render_kind want_kind;
	enum kwl_render_kind kind;
	unsigned have_ready;
	unsigned in_flight;
	unsigned completed;
	unsigned lost;
	unsigned step;
	unsigned seed;
	uint64_t number;
	int wake[2];
	unsigned char drained[64];

	/* The thread with the fakes, its wake pipe not blocking. */
	reset_fakes();
	atomic_store(&walk_calls, 0U);
	atomic_store(&locked_calls, 0U);
	atomic_store(&double_acquires, 0U);
	atomic_store(&bad_presents, 0U);
	if (pipe(wake) != 0)
		exit(2);
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);
	memset(&ops, 0, sizeof(ops));
	ops.acquire_output = fake_acquire_output;
	ops.acquire_head = fake_acquire_head;
	ops.run = fake_run;
	ops.wait = fake_wait;
	check(kwl_render_init(&render, &ops, NULL, wake[1]) == 0, "the thread's state starts (threads)");
	check(kwl_render_start(&render) == 0, "the thread starts");

	/* The main thread's side, at random. */
	seed = 12345U;
	want_kind = KWL_RENDER_NONE;
	have_ready = 0U;
	in_flight = 0U;
	completed = 0U;
	lost = 0U;
	number = 0U;
	for (step = 0; step < THREAD_STEPS; step++) {
		seed = seed * 1103515245U + 12345U;

		/* The messages in order. */
		while (read(wake[0], drained, sizeof(drained)) > 0)
			;
		messages = kwl_render_take(&render);
		while (messages != NULL) {
			message = messages;
			messages = message->next;
			if (message->kind == KWL_RENDER_COMPLETED) {
				in_flight = 0U;
				completed++;
			} else if (message->kind == KWL_RENDER_READY) {
				if (message->number == number && message->want_kind == want_kind && !in_flight) {
					ready = *message;
					have_ready = 1U;
				} else {
					kwl_render_answer(&render, message->number, KWL_RENDER_ANSWER_CANCEL, NULL);
				}
			}
			free(message);
		}

		/*
		 * A want now and then, a frame with a head or two or a layer, told
		 * only when its kind changed (compose.c tells a want it gave once,
		 * and a frame wanted again just after its list is given).
		 */
		kind = KWL_RENDER_NONE;
		if (((seed >> 8) % 7U) == 0U) {
			kind = KWL_RENDER_FRAME;
			if (((seed >> 20) % 3U) == 0U)
				kind = KWL_RENDER_LAYER;
		}
		if (kind != KWL_RENDER_NONE && kind != want_kind) {
			heads[0] = (seed >> 12) % KWL_RENDER_HEADS;
			heads[1] = (seed >> 16) % KWL_RENDER_HEADS;
			if (heads[1] == heads[0])
				heads[1] = (heads[0] + 1U) % KWL_RENDER_HEADS;
			if (kind == KWL_RENDER_LAYER) {
				kwl_render_set_want(&render, KWL_RENDER_LAYER, NULL, 0U, &number);
			} else {
				kwl_render_set_want(&render, KWL_RENDER_FRAME, heads, 1U + ((seed >> 24) % 2U), &number);
			}
			want_kind = kind;

			/* A ready for another want is cancelled. */
			if (have_ready && ready.number != number) {
				kwl_render_answer(&render, ready.number, KWL_RENDER_ANSWER_CANCEL, NULL);
				have_ready = 0U;
			}
		}

		/* The want the main thread gave is still the thread's (no acquire fails here): one lost stops the frames. */
		pthread_mutex_lock(&render.lock);

		if (want_kind != KWL_RENDER_NONE && (render.want.kind != want_kind || render.want.number != number))
			lost++;

		pthread_mutex_unlock(&render.lock);

		/* A walk and its list, the operations watched for calls meanwhile. */
		if (have_ready && !in_flight && ((seed >> 10) % 3U) == 0U) {
			atomic_store(&walking, 1);
			usleep(20);
			atomic_store(&walking, 0);
			job_from(&ready, &job);
			have_ready = 0U;
			in_flight = 1U;
			want_kind = KWL_RENDER_NONE;
			kwl_render_answer(&render, job.number, KWL_RENDER_ANSWER_LIST, &job);
		}

		/* A park now and then, with a forget of everything held (a swapchain made again). */
		if (((seed >> 14) % 997U) == 0U) {
			check(kwl_render_park(&render, 2000U) == 0, "the thread parks in time");
			messages = kwl_render_take(&render);
			while (messages != NULL) {
				message = messages;
				messages = message->next;
				if (message->kind == KWL_RENDER_COMPLETED) {
					in_flight = 0U;
					completed++;
				} else if (message->kind == KWL_RENDER_READY && message->number == number && message->want_kind == want_kind) {
					ready = *message;
					have_ready = 1U;
				} else if (message->kind == KWL_RENDER_READY) {
					kwl_render_answer(&render, message->number, KWL_RENDER_ANSWER_CANCEL, NULL);
				}
				free(message);
			}
			usleep(50);
			kwl_render_forget(&render, 1U, 0x7fU);
			reset_fakes();
			if (have_ready) {
				kwl_render_answer(&render, ready.number, KWL_RENDER_ANSWER_CANCEL, NULL);
				have_ready = 0U;
			}
			kwl_render_unpark(&render);
		}
	}

	/* The frame in flight completes; the thread stops. */
	while (in_flight) {
		(void)kwl_render_wait_message(&render, 1000U);
		messages = kwl_render_take(&render);
		while (messages != NULL) {
			message = messages;
			messages = message->next;
			if (message->kind == KWL_RENDER_COMPLETED)
				in_flight = 0U;
			free(message);
		}
	}
	kwl_render_stop(&render);
	kwl_render_destroy(&render);
	close(wake[0]);
	close(wake[1]);
	check(completed > 100U, "many jobs completed (threads)");
	check(lost == 0U, "no want lost, also one raised again before its list was taken (threads)");
	check(atomic_load(&walk_calls) == 0U, "no operation during a walk (threads)");
	check(atomic_load(&locked_calls) == 0U, "no operation with the lock held (threads)");
	check(atomic_load(&bad_presents) == 0U, "no image presented that was not acquired (threads)");
	check(atomic_load(&double_acquires) == 0U, "no head acquired twice (threads)");
}
