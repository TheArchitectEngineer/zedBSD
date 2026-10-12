/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The render thread (ws213-p004): the two steps of the pull, the frame's
 * recording, submission, presentation and fence apart from the main thread
 * (render-thread.h).
 *
 * The thread never holds its lock while it calls an operation (Vulkan),
 * waits on anything but its own signal, or writes to the wake pipe
 * (design 4.6).
 */

#include "render-thread.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* How long the thread sleeps between its checks of the main thread's requests while it waits, in milliseconds. */
#define RENDER_WAIT_MS		100U

static void *render_main(void *argument);
static enum kwl_render_step render_idle(struct kwl_render_thread *render);
static enum kwl_render_step render_answer(struct kwl_render_thread *render);
static enum kwl_render_step render_flight(struct kwl_render_thread *render);
static void render_post(struct kwl_render_thread *render, struct kwl_render_message *message);
static void render_deadline(struct timespec *deadline, unsigned wait_ms);
static void render_wake(int descriptor);

/*
 * Makes the render thread's state: its lock and signals (timed by the
 * monotonic clock), no want, nothing held.
 */
int
kwl_render_init(
	struct kwl_render_thread *render,
	const struct kwl_render_ops *ops,
	void *context,
	int wake_main)
{
	pthread_condattr_t attributes;
	int error;

	/* Nothing wanted, held or queued. */
	memset(render, 0, sizeof(*render));
	render->ops = *ops;
	render->context = context;
	render->wake_main = wake_main;

	/* The lock. */
	error = pthread_mutex_init(&render->lock, NULL);
	if (error != 0)
		return error;

	/* The signals' clock. */
	error = pthread_condattr_init(&attributes);
	if (error != 0) {
		pthread_mutex_destroy(&render->lock);
		return error;
	}

	/* The main thread's changes, which the thread waits for. */
	error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
	if (error == 0)
		error = pthread_cond_init(&render->changed, &attributes);
	if (error != 0) {
		pthread_condattr_destroy(&attributes);
		pthread_mutex_destroy(&render->lock);
		return error;
	}

	/* The thread's answers and messages to the main thread. */
	error = pthread_cond_init(&render->answered, &attributes);
	pthread_condattr_destroy(&attributes);
	if (error != 0) {
		pthread_cond_destroy(&render->changed);
		pthread_mutex_destroy(&render->lock);
		return error;
	}

	/* Succeeded: the thread can be started. */
	return 0;
}

/*
 * Frees what the stopped render thread leaves: messages not taken, and
 * its lock and signals.
 */
void
kwl_render_destroy(
	struct kwl_render_thread *render)
{
	struct kwl_render_message *message;

	/* The messages not taken. */
	while (render->messages != NULL) {
		message = render->messages;
		render->messages = message->next;
		free(message);
	}

	/* A completion never posted. */
	free(render->completion);
	render->completion = NULL;

	/* The signals, then the lock. */
	pthread_cond_destroy(&render->answered);
	pthread_cond_destroy(&render->changed);
	pthread_mutex_destroy(&render->lock);
}

/*
 * Starts the render thread.
 */
int
kwl_render_start(
	struct kwl_render_thread *render)
{
	int error;

	/* The thread steps until it is asked to stop. */
	error = pthread_create(&render->thread, NULL, render_main, render);
	if (error != 0)
		return error;

	/* Succeeded: stopped by kwl_render_stop. */
	render->started = 1U;
	return 0;
}

/*
 * Stops the render thread after the frame in flight, and waits for it.
 */
void
kwl_render_stop(
	struct kwl_render_thread *render)
{
	/* A thread never started has nothing in flight. */
	if (!render->started)
		return;

	/* The request, which wakes the thread. */
	pthread_mutex_lock(&render->lock);

	render->stop = 1U;
	render->sequence++;
	pthread_cond_broadcast(&render->changed);

	pthread_mutex_unlock(&render->lock);

	/* The thread ends. */
	(void)pthread_join(render->thread, NULL);
	render->started = 0U;
}

/*
 * Runs one step of the render thread: looks for a want and acquires its
 * images, takes an answer to its ready, or waits a while for the frame in
 * flight.  It never waits for the main thread: it reports that it is
 * blocked instead.  The thread calls it in a loop; a host test calls it
 * step by step.
 */
enum kwl_render_step
kwl_render_step(
	struct kwl_render_thread *render)
{
	enum kwl_render_step step;
	unsigned listed;

	/*
	 * The requests first, except while a frame is in flight or its list
	 * has been given: it completes before a stop or a park (design 4.8),
	 * so a park never leaves a given list to run after it.
	 */
	pthread_mutex_lock(&render->lock);

	render->seen_sequence = render->sequence;
	listed = 0U;
	if (render->state == KWL_RENDER_STATE_ANSWER &&
	    render->answer == KWL_RENDER_ANSWER_LIST &&
	    render->answer_number == render->ready_number)
		listed = 1U;
	if (render->state != KWL_RENDER_STATE_FLIGHT && !listed) {
		/* A stop ends the thread. */
		if (render->stop) {
			pthread_mutex_unlock(&render->lock);
			return KWL_RENDER_STEP_STOP;
		}

		/* A park is answered once; the thread calls no Vulkan until it ends. */
		if (render->park) {
			if (!render->parked) {
				render->parked = 1U;
				pthread_cond_broadcast(&render->answered);
			}

			/* Parked until the main thread ends the park. */
			pthread_mutex_unlock(&render->lock);
			return KWL_RENDER_STEP_BLOCKED;
		}

		/* No park, or it is over. */
		render->parked = 0U;
	}

	pthread_mutex_unlock(&render->lock);

	/* By where the thread is in the pull. */
	switch (render->state) {
	case KWL_RENDER_STATE_IDLE:
		step = render_idle(render);
		break;
	case KWL_RENDER_STATE_ANSWER:
		step = render_answer(render);
		break;
	default:
		step = render_flight(render);
		break;
	}

	/* Reports what the step came to. */
	return step;
}

/*
 * Says what the main thread may draw (design 4.1): the kind, and for a
 * frame the heads it needs.  The number moves only when the kind changes
 * to a frame or a layer (from another kind, or from none after the want
 * went down); a new list of heads keeps it.  Gives the want's number.
 */
void
kwl_render_set_want(
	struct kwl_render_thread *render,
	enum kwl_render_kind kind,
	const unsigned *heads,
	unsigned head_count,
	uint64_t *number)
{
	unsigned index;

	/* The want, and the thread woken for it. */
	if (head_count > KWL_RENDER_HEADS)
		head_count = KWL_RENDER_HEADS;
	pthread_mutex_lock(&render->lock);

	if (kind != KWL_RENDER_NONE && kind != render->want.kind)
		render->want.number++;
	render->want.kind = kind;
	render->want.head_count = head_count;
	for (index = 0; index < head_count; index++)
		render->want.heads[index] = heads[index];
	render->sequence++;
	pthread_cond_broadcast(&render->changed);
	*number = render->want.number;

	pthread_mutex_unlock(&render->lock);
}

/*
 * Answers a ready: the list for the job given (the want goes down when the
 * thread takes it), or a cancel (the images stay held).
 */
void
kwl_render_answer(
	struct kwl_render_thread *render,
	uint64_t number,
	enum kwl_render_answer answer,
	const struct kwl_render_job *job)
{
	/* The answer, and the thread woken for it. */
	pthread_mutex_lock(&render->lock);

	render->answer = answer;
	render->answer_number = number;
	if (job != NULL)
		render->job = *job;
	render->sequence++;
	pthread_cond_broadcast(&render->changed);

	pthread_mutex_unlock(&render->lock);
}

/*
 * Takes every message the render thread has queued, in order, for the main
 * thread to apply and free.  The main thread empties its wake pipe before,
 * so a message queued after this wakes it again.
 */
struct kwl_render_message *
kwl_render_take(
	struct kwl_render_thread *render)
{
	struct kwl_render_message *messages;

	/* The whole queue at once. */
	pthread_mutex_lock(&render->lock);

	messages = render->messages;
	render->messages = NULL;
	render->messages_tail = NULL;
	render->main_woken = 0U;

	pthread_mutex_unlock(&render->lock);

	/* Succeeded: the messages, oldest first, or NULL. */
	return messages;
}

/*
 * Waits at most wait_ms for a message of the render thread (the main
 * thread waiting for the frame in flight).  Returns 0 when one is queued,
 * ETIMEDOUT otherwise.
 */
int
kwl_render_wait_message(
	struct kwl_render_thread *render,
	unsigned wait_ms)
{
	struct timespec deadline;
	int error;

	/* Until a message, or the deadline. */
	render_deadline(&deadline, wait_ms);
	error = 0;
	pthread_mutex_lock(&render->lock);

	while (render->messages == NULL && error == 0)
		error = pthread_cond_timedwait(&render->answered, &render->lock, &deadline);
	if (render->messages != NULL)
		error = 0;

	pthread_mutex_unlock(&render->lock);

	/* None came in time. */
	if (error != 0)
		return ETIMEDOUT;

	/* Succeeded: a message is queued. */
	return 0;
}

/*
 * Parks the render thread for the main thread's change of the swapchains:
 * waits at most wait_ms for the thread to answer, which it does after the
 * frame in flight, at a point where it calls no Vulkan.  Returns 0 when
 * parked, ETIMEDOUT otherwise.
 */
int
kwl_render_park(
	struct kwl_render_thread *render,
	unsigned wait_ms)
{
	struct timespec deadline;
	int error;

	/* The request, and the thread's answer. */
	render_deadline(&deadline, wait_ms);
	error = 0;
	pthread_mutex_lock(&render->lock);

	render->park = 1U;
	render->sequence++;
	pthread_cond_broadcast(&render->changed);
	while (!render->parked && error == 0)
		error = pthread_cond_timedwait(&render->answered, &render->lock, &deadline);
	if (render->parked)
		error = 0;

	pthread_mutex_unlock(&render->lock);

	/* The thread did not answer in time. */
	if (error != 0)
		return ETIMEDOUT;

	/* Succeeded: parked until kwl_render_unpark. */
	return 0;
}

/*
 * Ends a park.
 */
void
kwl_render_unpark(
	struct kwl_render_thread *render)
{
	/* The thread steps again from its next look. */
	pthread_mutex_lock(&render->lock);

	render->park = 0U;
	render->sequence++;
	pthread_cond_broadcast(&render->changed);

	pthread_mutex_unlock(&render->lock);
}

/*
 * Forgets images the render thread holds whose swapchains the main thread
 * destroyed while it was parked: the output's (output nonzero) and the
 * heads' of the slots set in heads_mask.
 */
void
kwl_render_forget(
	struct kwl_render_thread *render,
	unsigned output,
	unsigned heads_mask)
{
	unsigned slot;

	/* The held images, the thread parked. */
	pthread_mutex_lock(&render->lock);

	if (output)
		render->held.output = 0U;
	for (slot = 0; slot < KWL_RENDER_HEADS; slot++) {
		if ((heads_mask & (1U << slot)) != 0U)
			render->held.heads[slot] = 0U;
	}

	pthread_mutex_unlock(&render->lock);
}

/*
 * Waits until the render thread has no job in flight: it then records,
 * submits and presents nothing until the main thread gives it the next
 * list, so the main thread may use the frames' command pool and the queue
 * (an image's layout moved now, outside a walk).
 */
void
kwl_render_wait_idle(
	struct kwl_render_thread *render)
{
	/* Until the job in flight completed. */
	pthread_mutex_lock(&render->lock);

	while (render->busy)
		(void)pthread_cond_wait(&render->answered, &render->lock);

	pthread_mutex_unlock(&render->lock);
}

/* Runs the render thread's steps, waiting between them for the main thread when blocked, until it is asked to stop. */
static void *
render_main(
	void *argument)
{
	struct kwl_render_thread *render;
	struct timespec deadline;
	enum kwl_render_step step;

	/* Step after step. */
	render = argument;
	for (;;) {
		step = kwl_render_step(render);
		if (step == KWL_RENDER_STEP_STOP)
			break;
		if (step == KWL_RENDER_STEP_WORKED)
			continue;

		/* Blocked: the next change the main thread makes, looked at every 100 ms at most. */
		render_deadline(&deadline, RENDER_WAIT_MS);
		pthread_mutex_lock(&render->lock);

		if (render->sequence == render->seen_sequence)
			(void)pthread_cond_timedwait(&render->changed, &render->lock, &deadline);

		pthread_mutex_unlock(&render->lock);
	}

	/* The thread ends. */
	return NULL;
}

/*
 * Looks for a want and acquires what it needs: for a frame, the output's
 * image unless one is held (at most KWL_RENDER_ACQUIRE_NS at a time, the
 * requests looked at in between) and each wanted head's not held (without
 * waiting).  Then the ready goes out with everything held.  A failed
 * acquire of the output takes the want down, if it is still the same one
 * (compare and clear, design 4.1), and is told to the main thread.
 */
static enum kwl_render_step
render_idle(
	struct kwl_render_thread *render)
{
	struct kwl_render_message *message;
	struct kwl_render_want want;
	uint64_t acquired_us;
	uint64_t acquire_cycles;
	uint32_t image;
	unsigned index;
	unsigned slot;
	unsigned held;
	VkResult result;
	VkResult head_results[KWL_RENDER_HEADS];

	/* The want as it is now. */
	pthread_mutex_lock(&render->lock);

	want = render->want;
	held = render->held.output;

	pthread_mutex_unlock(&render->lock);
	if (want.kind == KWL_RENDER_NONE)
		return KWL_RENDER_STEP_BLOCKED;

	/* The ready's message, made first: without memory the step is tried again. */
	message = calloc(1, sizeof(*message));
	if (message == NULL) {
		usleep(1000);
		return KWL_RENDER_STEP_WORKED;
	}

	/* The output's image for a frame, unless held from before. */
	acquired_us = 0U;
	acquire_cycles = 0U;
	if (want.kind == KWL_RENDER_FRAME && !held) {
		result = render->ops.acquire_output(render->context, KWL_RENDER_ACQUIRE_NS, &image, &acquire_cycles, &acquired_us);

		/* None free yet: the requests are looked at, then the acquire is tried again. */
		if (result == VK_TIMEOUT || result == VK_NOT_READY) {
			free(message);
			return KWL_RENDER_STEP_WORKED;
		}

		/* A failure: the want goes down if it is still this one, and the main thread hears of it. */
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			pthread_mutex_lock(&render->lock);

			if (render->want.number == want.number)
				render->want.kind = KWL_RENDER_NONE;

			pthread_mutex_unlock(&render->lock);

			/* Told in order. */
			message->kind = KWL_RENDER_ACQUIRE_FAILED;
			message->number = want.number;
			message->want_kind = want.kind;
			message->result = result;
			render_post(render, message);
			return KWL_RENDER_STEP_WORKED;
		}

		/* Held until it is presented. */
		pthread_mutex_lock(&render->lock);

		render->held.output = 1U;
		render->held.output_image = image;

		pthread_mutex_unlock(&render->lock);
	}

	/* Each wanted head not held, without waiting; what came of it is told with the ready. */
	for (index = 0; index < KWL_RENDER_HEADS; index++)
		head_results[index] = VK_SUCCESS;
	for (index = 0; want.kind == KWL_RENDER_FRAME && index < want.head_count; index++) {
		slot = want.heads[index];
		if (slot >= KWL_RENDER_HEADS)
			continue;
		pthread_mutex_lock(&render->lock);

		held = render->held.heads[slot];

		pthread_mutex_unlock(&render->lock);
		if (held)
			continue;

		/* Its next image, or why not. */
		result = render->ops.acquire_head(render->context, slot, &image);
		head_results[slot] = result;
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			continue;
		head_results[slot] = VK_SUCCESS;

		/* Held until it is presented. */
		pthread_mutex_lock(&render->lock);

		render->held.heads[slot] = 1U;
		render->held.head_images[slot] = image;

		pthread_mutex_unlock(&render->lock);
	}

	/* The ready, with everything held: from now on no Vulkan until the answer. */
	message->kind = KWL_RENDER_READY;
	message->number = want.number;
	message->want_kind = want.kind;
	message->acquire_cycles = acquire_cycles;
	message->acquired_us = acquired_us;
	pthread_mutex_lock(&render->lock);

	message->held = render->held;

	pthread_mutex_unlock(&render->lock);
	for (index = 0; index < KWL_RENDER_HEADS; index++)
		message->held.head_results[index] = head_results[index];
	render->state = KWL_RENDER_STATE_ANSWER;
	render->ready_number = want.number;
	render_post(render, message);

	/* Succeeded: waiting for the answer. */
	return KWL_RENDER_STEP_WORKED;
}

/*
 * Takes the main thread's answer to the ready: a cancel goes back to the
 * wants with the images still held; a list is recorded, submitted and
 * presented, and the want goes down if it is still the same one (compare
 * and clear).  The images it presents are no longer held.
 */
static enum kwl_render_step
render_answer(
	struct kwl_render_thread *render)
{
	struct kwl_render_message *completion;
	enum kwl_render_answer answer;
	struct kwl_render_job job;
	unsigned slot;

	/* The completion's message, made before the job is taken. */
	if (render->completion == NULL) {
		render->completion = calloc(1, sizeof(*render->completion));
		if (render->completion == NULL) {
			usleep(1000);
			return KWL_RENDER_STEP_WORKED;
		}
	}

	/* The answer to this ready, if one came. */
	pthread_mutex_lock(&render->lock);

	if (render->answer == KWL_RENDER_ANSWER_NONE || render->answer_number != render->ready_number) {
		pthread_mutex_unlock(&render->lock);
		return KWL_RENDER_STEP_BLOCKED;
	}

	/* Taken: the next answer is for the next ready. */
	answer = render->answer;
	job = render->job;
	render->answer = KWL_RENDER_ANSWER_NONE;

	/* A cancel: back to the wants, the images kept. */
	if (answer == KWL_RENDER_ANSWER_CANCEL) {
		render->state = KWL_RENDER_STATE_IDLE;
		pthread_mutex_unlock(&render->lock);
		return KWL_RENDER_STEP_WORKED;
	}

	/* The list: the want goes down if it is still this one, and the images the job presents are let go. */
	if (render->want.number == job.number)
		render->want.kind = KWL_RENDER_NONE;
	if (job.kind == KWL_RENDER_FRAME)
		render->held.output = 0U;
	for (slot = 0; slot < KWL_RENDER_HEADS; slot++) {
		if (job.heads[slot])
			render->held.heads[slot] = 0U;
	}

	/* A job in flight from now on. */
	render->busy = 1U;

	pthread_mutex_unlock(&render->lock);

	/* Recorded, submitted and presented. */
	completion = render->completion;
	completion->kind = KWL_RENDER_COMPLETED;
	completion->number = job.number;
	completion->want_kind = job.kind;
	completion->job = job;
	render->ops.run(render->context, &job, &completion->outcome);
	render->state = KWL_RENDER_STATE_FLIGHT;

	/* A job that was not submitted has no fence: it completes now. */
	if (completion->outcome.result != VK_SUCCESS) {
		render->completion = NULL;
		render->state = KWL_RENDER_STATE_IDLE;
		pthread_mutex_lock(&render->lock);

		render->busy = 0U;

		pthread_mutex_unlock(&render->lock);
		render_post(render, completion);
	}

	/* Succeeded: in flight. */
	return KWL_RENDER_STEP_WORKED;
}

/*
 * Waits a while for the job's fence: once signalled, the completion goes
 * to the main thread, and the thread looks for the next want.
 */
static enum kwl_render_step
render_flight(
	struct kwl_render_thread *render)
{
	struct kwl_render_message *completion;
	VkResult result;

	/* The fence, for a while. */
	result = render->ops.wait(render->context, KWL_RENDER_FENCE_NS);
	if (result == VK_TIMEOUT)
		return KWL_RENDER_STEP_WORKED;

	/* Done, or its wait failed (the compositor's failure). */
	completion = render->completion;
	render->completion = NULL;
	if (result != VK_SUCCESS)
		completion->outcome.result = result;
	render->state = KWL_RENDER_STATE_IDLE;
	pthread_mutex_lock(&render->lock);

	render->busy = 0U;

	pthread_mutex_unlock(&render->lock);

	/* Told in order, before any ready that follows. */
	render_post(render, completion);
	return KWL_RENDER_STEP_WORKED;
}

/* Queues a message for the main thread after every earlier one, and wakes it when it was not woken for the queue yet. */
static void
render_post(
	struct kwl_render_thread *render,
	struct kwl_render_message *message)
{
	unsigned wake;

	/* After the messages before it; a waiting main thread is signalled. */
	message->next = NULL;
	pthread_mutex_lock(&render->lock);

	if (render->messages_tail == NULL) {
		render->messages = message;
	} else {
		render->messages_tail->next = message;
	}

	/* The main thread is woken once for a queue it has not taken yet. */
	render->messages_tail = message;
	wake = 0U;
	if (!render->main_woken) {
		render->main_woken = 1U;
		wake = 1U;
	}

	/* A main thread waiting for a message wakes. */
	pthread_cond_broadcast(&render->answered);

	pthread_mutex_unlock(&render->lock);

	/* The main thread's poll returns. */
	if (wake)
		render_wake(render->wake_main);
}

/* Gives the deadline wait_ms from now on the monotonic clock. */
static void
render_deadline(
	struct timespec *deadline,
	unsigned wait_ms)
{
	/* Now, and the wait added. */
	clock_gettime(CLOCK_MONOTONIC, deadline);
	deadline->tv_sec += (time_t)(wait_ms / 1000U);
	deadline->tv_nsec += (long)(wait_ms % 1000U) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

/* Writes one byte to a wake pipe; a full pipe already wakes its reader. */
static void
render_wake(
	int descriptor)
{
	unsigned char byte;
	ssize_t written;

	/* One byte; EAGAIN means the reader has bytes to wake on already. */
	if (descriptor < 0)
		return;
	byte = 1U;
	do {
		written = write(descriptor, &byte, 1U);
	} while (written < 0 && errno == EINTR);
}
