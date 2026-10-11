/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* One private ordered execution queue retains callbacks independently of native DMA. */
#ifndef KERN_DRIVERS_GPU_BCM2711_RENDER_WORKER_H
#define KERN_DRIVERS_GPU_BCM2711_RENDER_WORKER_H

#include <stdbool.h>
#include <stdint.h>
#include <kern/waitq.h>

#define BCM2711_RENDER_REQUESTS 16U
#define BCM2711_RENDER_RESERVATIONS 8U

struct thread;
struct drv_gpu_completion;
struct drv_gpu_ops;
struct bcm2711_render_device;
struct bcm2711_render_session;

/* Execution and disposal run under the controller mutex; callbacks run without it. */
typedef int (*bcm2711_render_execute)(struct bcm2711_render_device *, struct bcm2711_render_session *, void *, bool *);
typedef int (*bcm2711_render_dispose)(struct bcm2711_render_device *, void *, bool);

struct bcm2711_render_execution;

/*
 * One step of an execution (at most one native pass) runs under the controller
 * mutex and reports whether the execution has finished; an error ends it.  The
 * end publishes the outcome (zero, the step's error, ECANCELED for a stopped
 * session or EIO for a faulted engine), releases the execution's owners and
 * frees it, also under the mutex, and returns a release failure.  The executor
 * turns a step's error or a release failure into a global fault; a canceled
 * execution affects only its session.
 */
typedef int (*bcm2711_render_execution_step)(struct bcm2711_render_device *, struct bcm2711_render_execution *, bool *);
typedef int (*bcm2711_render_execution_end)(struct bcm2711_render_device *, struct bcm2711_render_execution *, int);

/*
 * One accepted QueueSubmit's work in its session's execution queue.  The
 * native IRQ guard protects the links; the executor alone runs and ends it.
 */
struct bcm2711_render_execution {
	struct bcm2711_render_execution *next;
	struct bcm2711_render_session *session;
	bcm2711_render_execution_step step;
	bcm2711_render_execution_end end;
	/* The staging bytes the execution's prepared work may hold, for the session's limit. */
	uint64_t bytes;
};

/* The native IRQ guard protects every slot field while its prepared payload remains owned. */
struct bcm2711_render_request {
	struct bcm2711_render_request *next;
	struct bcm2711_render_session *session;
	struct drv_gpu_completion *completion;
	bcm2711_render_execute execute;
	bcm2711_render_dispose dispose;
	void *payload;
	/* A supervised callback retains its old queue domain through final callback retirement. */
	uint32_t timeline;
	uint32_t state;
	int canceled;
	bool supervised;
	/* Diagnostic tick count when the request entered the FIFO. */
	uint64_t queued_ticks;
	/* A parked domain completion is released once its session has executed this many executions. */
	uint64_t required;
	struct bcm2711_render_request *parked_next;
};

/* The native IRQ guard protects the FIFO, slots, pending counts and uncertain flag. */
struct bcm2711_render_worker {
	struct bcm2711_render_request requests[BCM2711_RENDER_REQUESTS];
	struct bcm2711_render_request *head;
	struct bcm2711_render_request *tail;
	struct thread *thread;
	struct wait_queue available;
	struct wait_queue retired;
	bool uncertain;
	/*
	 * The executor: sessions with queued executions in round-robin order,
	 * the domain completions parked until their sessions' executions finish, its thread
	 * and its wake-up.  The native IRQ guard protects the lists.
	 */
	struct bcm2711_render_session *ready_head;
	struct bcm2711_render_session *ready_tail;
	struct bcm2711_render_request *parked;
	struct thread *executor;
	struct wait_queue executable;
};

void bcm2711_render_jobs_bind(struct drv_gpu_ops *operations);
void bcm2711_render_worker_init(struct bcm2711_render_device *controller);
int bcm2711_render_worker_start(struct bcm2711_render_device *controller);
int bcm2711_render_worker_submit(struct bcm2711_render_session *session, bcm2711_render_execute execute, bcm2711_render_dispose dispose, void *payload, uint32_t timeline, struct drv_gpu_completion *completion);
int bcm2711_render_worker_step(struct bcm2711_render_device *controller);
void bcm2711_render_worker_stop(struct bcm2711_render_session *session, int error);
int bcm2711_render_worker_stopped(struct bcm2711_render_session *session);
void bcm2711_render_worker_drain(struct bcm2711_render_session *session);
void bcm2711_render_worker_fault(struct bcm2711_render_device *controller, int error);
int bcm2711_render_worker_recovered(struct bcm2711_render_device *controller);

/* Queues an accepted execution behind its session's earlier ones; the caller holds the controller mutex. */
void bcm2711_render_execution_submit(struct bcm2711_render_session *session, struct bcm2711_render_execution *execution);

/* Runs one step of the next session's execution; EAGAIN when no execution waits. */
int bcm2711_render_executor_step(struct bcm2711_render_device *controller);

#endif
