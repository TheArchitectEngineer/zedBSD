/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The render thread (render-thread.c, ws213-p004; the design is
 * plan/ws213/phase001/phase.md 2 and 4, "the display list and the two
 * steps of the pull").
 *
 * The main thread says it may draw (a want: a frame, or a layer's picture
 * that presents nothing, with a number that moves only when the kind
 * changes or the want is raised again after it went down).  The render
 * thread acquires the images a frame needs (the output's, and the heads'
 * the want lists that it does not hold yet) and answers ready, with every
 * image it holds; from then until the main thread answers it calls no
 * Vulkan, so the main thread may walk its drawing (and use Vulkan as it
 * always has) and write the display list.  The answer is the list, or a
 * cancel (the want changed meanwhile): the images stay held for a later
 * frame.  With the list the render thread records it into the command
 * buffers, submits, presents the output's and the heads' images, waits for
 * the frame's fence and tells the main thread it completed; only then does
 * it look for the next want, so one frame is in flight at most.
 *
 * Every message to the main thread goes through one ordered queue: a
 * frame's completion always comes before the ready that follows it.  A
 * park stops the thread at a point where it calls no Vulkan (after the
 * frame in flight), for the main thread to change the swapchains; images
 * the thread holds stay held unless the main thread forgets them there.
 *
 * The thread knows nothing of the compositor: its Vulkan work is done by
 * the operations it is given (compose.c's, or a host test's fakes).
 */

#ifndef KWL_RENDER_THREAD_H
#define KWL_RENDER_THREAD_H

#include <pthread.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/* The heads (other displays) a frame may draw, as compose.h's KWL_HEADS. */
#define KWL_RENDER_HEADS	7U

/* How long one acquire of the output's image and one wait for a frame's fence last before the thread looks at its requests, in nanoseconds. */
#define KWL_RENDER_ACQUIRE_NS	50000000ULL
#define KWL_RENDER_FENCE_NS	100000000ULL

/* What the main thread may draw. */
enum kwl_render_kind {
	/* Nothing: the thread acquires nothing. */
	KWL_RENDER_NONE = 0,
	/* A frame of the output (and the heads). */
	KWL_RENDER_FRAME,
	/* A layer's picture, which presents nothing and needs no image. */
	KWL_RENDER_LAYER
};

/* The main thread's answer to a ready. */
enum kwl_render_answer {
	KWL_RENDER_ANSWER_NONE = 0,
	/* The display list is written: run it. */
	KWL_RENDER_ANSWER_LIST,
	/* The want changed: no list for this ready. */
	KWL_RENDER_ANSWER_CANCEL
};

/* What one step of the render thread came to. */
enum kwl_render_step {
	/* Did some work (or a bounded wait ran out): step again. */
	KWL_RENDER_STEP_WORKED = 1,
	/* Waits for the main thread: a want, an answer, the end of a park. */
	KWL_RENDER_STEP_BLOCKED,
	/* Asked to stop, with no frame in flight. */
	KWL_RENDER_STEP_STOP
};

/* Where the render thread is in the pull. */
enum kwl_render_state {
	/* Looking for a want. */
	KWL_RENDER_STATE_IDLE = 0,
	/* A ready is out; waiting for the main thread's answer. */
	KWL_RENDER_STATE_ANSWER,
	/* A job is submitted; waiting for its fence. */
	KWL_RENDER_STATE_FLIGHT
};

/* What one message of the render thread to the main thread says. */
enum kwl_render_message_kind {
	/* The images are held: the main thread may walk. */
	KWL_RENDER_READY = 1,
	/* The output's image could not be acquired: the want went down. */
	KWL_RENDER_ACQUIRE_FAILED,
	/* A frame or a layer's picture completed (or failed). */
	KWL_RENDER_COMPLETED
};

/*
 * The want: its number, its kind, and for a frame the heads (slots of
 * compose.h's heads) whose images it needs.
 */
struct kwl_render_want {
	uint64_t number;
	enum kwl_render_kind kind;
	unsigned head_count;
	unsigned heads[KWL_RENDER_HEADS];
};

/*
 * The images the render thread holds (acquired, not presented yet): the
 * output's and each head's, by slot; and for a ready, the result of this
 * round's acquire of each wanted head it did not hold (VK_SUCCESS when it
 * got one or did not try).
 */
struct kwl_render_held {
	unsigned output;
	uint32_t output_image;
	unsigned heads[KWL_RENDER_HEADS];
	uint32_t head_images[KWL_RENDER_HEADS];
	VkResult head_results[KWL_RENDER_HEADS];
};

/*
 * What the main thread gives with its list: the want's number and kind,
 * and for a frame the output's image and the heads it drew (every held
 * head, which the thread presents).
 */
struct kwl_render_job {
	uint64_t number;
	enum kwl_render_kind kind;
	uint32_t output_image;
	unsigned heads[KWL_RENDER_HEADS];
	uint32_t head_images[KWL_RENDER_HEADS];
};

/*
 * A frame's outcome, as the operations report it: the recording's and the
 * submission's result (any failure is the compositor's), the output's
 * present (a display gone is not), each head's present by slot (heads
 * says which were presented), the recording's time in cycles, and the
 * times of the end of the recording, of the submission and of the
 * present (kwl_microseconds).
 */
struct kwl_render_outcome {
	VkResult result;
	VkResult present_result;
	unsigned heads[KWL_RENDER_HEADS];
	VkResult head_results[KWL_RENDER_HEADS];
	uint64_t replay_cycles;
	uint64_t run_cycles;
	uint64_t recorded_us;
	uint64_t queued_us;
	uint64_t presented_us;
	uint64_t completed_us;
};

/*
 * One message of the ordered queue to the main thread; the thread
 * allocates it, the main thread frees it.  number is the want's (or the
 * job's); held is what a ready holds; result an acquire's failure;
 * outcome a completion's; acquired_us when a ready's output image was
 * acquired and acquire_cycles how long that took.
 */
struct kwl_render_message {
	struct kwl_render_message *next;
	enum kwl_render_message_kind kind;
	uint64_t number;
	enum kwl_render_kind want_kind;
	struct kwl_render_held held;
	VkResult result;
	struct kwl_render_job job;
	struct kwl_render_outcome outcome;
	uint64_t acquired_us;
	uint64_t acquire_cycles;
};

/*
 * The Vulkan work the thread does through its owner: acquire the output's
 * next image (waiting at most timeout_ns; it gives how long it took in
 * cycles and when it ended, for the main thread's lines), acquire a head's
 * next image
 * without waiting, record and submit and present a job, and wait for the
 * job's fence (at most timeout_ns; VK_TIMEOUT to be asked again).
 */
struct kwl_render_ops {
	VkResult (*acquire_output)(void *context, uint64_t timeout_ns, uint32_t *image, uint64_t *cycles, uint64_t *at_us);
	VkResult (*acquire_head)(void *context, unsigned slot, uint32_t *image);
	void (*run)(void *context, const struct kwl_render_job *job, struct kwl_render_outcome *outcome);
	VkResult (*wait)(void *context, uint64_t timeout_ns);
};

/*
 * The render thread and what it shares with the main thread.
 *
 * Under lock: the want; the answer to the ready outstanding (for which
 * number) and the job it carries; the park and stop requests and whether
 * the thread is parked (it answers on answered); whether a frame is in
 * flight; the message queue and whether the main thread was woken for it;
 * the held images (the thread's, written by the main thread only while
 * parked); and sequence, which counts every change the thread waits for.
 * The thread's own: the operations and their context, where it is in the
 * pull (state), the number of the ready it waits to have answered, the
 * completion message of the job in flight (made before the job is taken,
 * so that a completion is never lost), and the main thread's wake pipe.
 */
struct kwl_render_thread {
	pthread_mutex_t lock;
	pthread_cond_t changed;
	pthread_cond_t answered;
	uint64_t sequence;
	struct kwl_render_want want;
	enum kwl_render_answer answer;
	uint64_t answer_number;
	struct kwl_render_job job;
	unsigned park;
	unsigned parked;
	unsigned stop;
	unsigned busy;
	struct kwl_render_message *messages;
	struct kwl_render_message *messages_tail;
	unsigned main_woken;
	unsigned dropped;
	struct kwl_render_held held;
	struct kwl_render_ops ops;
	void *context;
	enum kwl_render_state state;
	uint64_t ready_number;
	struct kwl_render_message *completion;
	uint64_t seen_sequence;
	int wake_main;
	pthread_t thread;
	unsigned started;
};

int kwl_render_init(struct kwl_render_thread *render, const struct kwl_render_ops *ops, void *context, int wake_main);
void kwl_render_destroy(struct kwl_render_thread *render);
int kwl_render_start(struct kwl_render_thread *render);
void kwl_render_stop(struct kwl_render_thread *render);
enum kwl_render_step kwl_render_step(struct kwl_render_thread *render);
void kwl_render_set_want(struct kwl_render_thread *render, enum kwl_render_kind kind, const unsigned *heads, unsigned head_count, uint64_t *number);
void kwl_render_answer(struct kwl_render_thread *render, uint64_t number, enum kwl_render_answer answer, const struct kwl_render_job *job);
struct kwl_render_message *kwl_render_take(struct kwl_render_thread *render);
int kwl_render_wait_message(struct kwl_render_thread *render, unsigned wait_ms);
int kwl_render_park(struct kwl_render_thread *render, unsigned wait_ms);
void kwl_render_unpark(struct kwl_render_thread *render);
void kwl_render_forget(struct kwl_render_thread *render, unsigned output, unsigned heads_mask);
void kwl_render_wait_idle(struct kwl_render_thread *render);

#endif
