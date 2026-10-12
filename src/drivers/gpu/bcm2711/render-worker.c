/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Bounded trusted jobs share one worker; callback retirement never substitutes for DMA stop. */
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/gpu/bcm2711/render-device.h"
#include "drivers/gpu/bcm2711/render-worker.h"

/* Free slots acquire a callback, execute once and finish that callback before reuse. */
#define REQUEST_FREE 0U
#define REQUEST_QUEUED 1U
#define REQUEST_ACTIVE 2U
#define REQUEST_FINISHING 3U
#define REQUEST_RESERVED 4U
#define REQUEST_RETAINED 5U
/* A queue domain completion whose session still has executions waits here until they finish. */
#define REQUEST_PARKED 6U

/* The command slots followed by the marker pool. */
#define WORKER_ENTRIES (BCM2711_RENDER_REQUESTS + BCM2711_RENDER_MARKERS)

/* The kernel log takes at most this many failure lines from one controller. */
#define WORKER_FAILURE_LINES 64U

/* Constant operation-table entries need declarations before ANSI C initialization. */
static int job_reserve(void *opaque, void *private_session, uint32_t timeline, struct drv_gpu_completion *completion, void **token);
static int job_commit(void *opaque, void *private_session, void *token, struct drv_gpu_completion *completion);
static int job_cancel(void *opaque, void *private_session, void *token, struct drv_gpu_completion *completion, unsigned fault);
static int job_capacity(void *opaque, void *private_session, uint32_t timeline, unsigned *available);

/* The immutable supervised-job table is bound only when the Vulkan command table is complete. */
static const struct drv_gpu_job_ops job_operations = {
	job_reserve,
	job_commit,
	job_cancel,
	job_capacity
};

static struct bcm2711_render_request *find_reservation(struct bcm2711_render_device *controller, struct bcm2711_render_session *session, void *token, struct drv_gpu_completion *completion);
static void queue_marker(struct bcm2711_render_device *controller, struct bcm2711_render_request *request, int error);
static int execute_marker(struct bcm2711_render_device *controller, struct bcm2711_render_session *session, void *payload, bool *retired);
static int dispose_marker(struct bcm2711_render_device *controller, void *payload, bool retired);
static unsigned marker_capacity(struct bcm2711_render_device *controller, struct bcm2711_render_session *session);
static struct bcm2711_render_request *worker_entry(struct bcm2711_render_worker *worker, uint32_t index);
static void retire_entry(struct bcm2711_render_device *controller, struct bcm2711_render_request *request);

static void worker_main(void *opaque);
static void executor_main(void *opaque);
static bool park_request(struct bcm2711_render_device *controller, struct bcm2711_render_request *request, int error);
static int parked_outcome(struct bcm2711_render_device *controller, struct bcm2711_render_request *request);
static struct bcm2711_render_session *select_session(struct bcm2711_render_worker *worker);
static struct bcm2711_render_request *take_request(struct bcm2711_render_device *controller);
static void finish_request(struct bcm2711_render_device *controller, struct bcm2711_render_request *request, int error);

/*
 * Binds the complete supervised-job table before native Vulkan capability publication.
 */
void
bcm2711_render_jobs_bind(
	struct drv_gpu_ops *operations)
{
	/* The caller publishes JOB only together with the command notification and drain contract. */
	operations->jobs = &job_operations;

	/* Succeeded: the renderer job group uses the private reservation callbacks. */
	return;
}

/*
 * Initializes the private ordered queue before the renderer node publishes opens.
 */
void
bcm2711_render_worker_init(
	struct bcm2711_render_device *controller)
{
	/* No producer exists yet, so zero slots and queues can be initialized without a guard. */
	kern_memset(&controller->worker, 0, sizeof(controller->worker));
	waitq_init(&controller->worker.available, "bcm2711-render-work");
	waitq_init(&controller->worker.retired, "bcm2711-render-retired");
	waitq_init(&controller->worker.executable, "bcm2711-render-execute");
	waitq_init(&controller->worker.window_queue, "bcm2711-render-window");

	/* Succeeded: the worker starts with an empty FIFO and no native uncertainty. */
	return;
}

/*
 * Starts the permanent worker on the first userspace open while the controller mutex is held.
 */
int
bcm2711_render_worker_start(
	struct bcm2711_render_device *controller)
{
	struct thread *thread;
	int error;

	/* Every later open reuses the existing worker and its permanent queue owner. */
	if (controller->worker.thread != NULL)
		return 0;

	/* The executor runs the queued executions; it exists before the worker can accept a QueueSubmit. */
	if (controller->worker.executor == NULL) {
		error = kthread_create(executor_main, controller, SCHED_PRIORITY_DEFAULT, &thread);
		if (error != 0)
			return error;
		controller->worker.executor = thread;
		thread_start(thread);
	}
	error = kthread_create(worker_main, controller, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/* A complete thread owner exists before it can observe newly admitted queue producers. */
	controller->worker.thread = thread;
	thread_start(thread);

	/* Succeeded: one permanent kernel thread serves this controller's trusted native jobs. */
	return 0;
}

/*
 * Accepts one fully prepared payload and callback without allocating or touching native registers.
 * A refusal retains neither the payload nor completion; success consumes both exactly once.
 */
int
bcm2711_render_worker_submit(
	struct bcm2711_render_session *session,
	bcm2711_render_execute execute,
	bcm2711_render_dispose dispose,
	void *payload,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_worker *worker;
	struct bcm2711_render_request *request;
	unsigned long enabled;
	uint32_t slot;
	uint64_t domain;

	/* Payload ownership needs both its executable operation and terminal disposal. */
	if (execute == NULL ||
	    dispose == NULL ||
	    completion == NULL ||
	    timeline >= 64)
		return EINVAL;
	controller = session->device;
	worker = &controller->worker;
	domain = UINT64_C(1) << timeline;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Nonzero markers preserve the exact live native queue domain until final callback retirement. */
	if (timeline != 0 && (session->timelines & domain) == 0) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EINVAL;
	}

	/* Native stop and callback publication share this same guard. */
	if (session->stopping ||
	    worker->uncertain ||
	    !controller->space.native->hardware.ready ||
	    controller->space.native->hardware.faulted) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EIO;
	}

	/* A fixed bounded slot pool prevents commit-time allocation and unbounded queued ownership. */
	request = NULL;
	for (slot = 0; slot < BCM2711_RENDER_REQUESTS; slot++) {
		if (worker->requests[slot].state != REQUEST_FREE)
			continue;
		request = &worker->requests[slot];
		break;
	}

	/* Capacity refusal precedes every callback or payload ownership transfer. */
	if (request == NULL || session->pending == UINT32_MAX) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EAGAIN;
	}

	/* A fully owned callback and immutable payload enter the FIFO together. */
	request->session = session;
	request->completion = completion;
	request->execute = execute;
	request->dispose = dispose;
	request->payload = payload;
	request->timeline = timeline;
	request->state = REQUEST_QUEUED;
	request->canceled = 0;
	request->next = NULL;
	request->queued_ticks = clock_ticks();
	session->pending++;
	if (worker->tail == NULL) {
		worker->head = request;
	} else {
		worker->tail->next = request;
	}

	/* The tail always names the final fully owned queued request. */
	worker->tail = request;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* The permanent worker observes publication before its condition sequence changes. */
	waitq_wake_one(&worker->available);

	/* Succeeded: exactly one queued slot owns this payload and common completion. */
	return 0;
}

/*
 * Executes one queued payload through the same bounded path used by the permanent worker.
 */
int
bcm2711_render_worker_step(
	struct bcm2711_render_device *controller)
{
	struct bcm2711_render_request *request;
	struct bcm2711_v3d_perf *perf;
	unsigned long enabled;
	uint64_t started;
	uint64_t waited;
	uint64_t taken;
	bool parked;
	bool retired;
	bool global_failure;
	bool native_fault;
	int error;
	int disposed;

	/* Dequeue retains the session and callback until final completion delivery ends. */
	request = take_request(controller);
	if (request == NULL)
		return EAGAIN;
	started = clock_ticks();
	mutex_lock(&controller->mutex);

	/* Stop and global fault may arrive after dequeue while this worker waits for the controller. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	error = request->canceled;
	if (error == 0 && request->session->stopping)
		error = ECANCELED;
	if (error == 0 && controller->space.native->hardware.faulted)
		error = EIO;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Only the trusted executor may launch work; cancellation owns no native DMA. */
	retired = true;
	if (error == 0)
		error = request->execute(controller, request->session, request->payload, &retired);
	/* An IRQ fault between queued jobs closes the common device even without a launched payload. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	native_fault = controller->space.native->hardware.faulted;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Global fault publication follows native admission closure before callback retirement. */
	global_failure = false;
	if (native_fault) {
		if (error == 0)
			error = EIO;
		bcm2711_render_worker_fault(controller, error);
		global_failure = true;
	}

	/* A launched operation without retirement proof preserves every independent native view. */
	if (!retired) {
		/* Native uncertainty arms resource teardown before payload disposal drops any job view. */
		if (error == 0)
			error = EIO;
		bcm2711_render_worker_fault(controller, error);
		global_failure = true;
	}

	/* Disposal releases independent job views; their owner quarantines uncertain native storage. */
	disposed = request->dispose(controller, request->payload, retired);
	if (disposed != 0) {
		/* A failed translation retirement also requires checked global recovery before reuse. */
		bcm2711_render_worker_fault(controller, disposed);
		global_failure = true;
		if (error == 0)
			error = disposed;
	}

	/*
	 * Diagnostic: how long the request waited in the FIFO behind earlier
	 * ones, and how long it then held the worker; a synchronous submit
	 * returns after both.
	 */
	perf = &controller->space.native->hardware.perf;
	waited = started - request->queued_ticks;
	taken = clock_ticks() - started;
	perf->requests++;
	perf->request_wait_ticks += waited;
	perf->request_run_ticks += taken;
	if (waited > perf->request_wait_max)
		perf->request_wait_max = waited;
	if (taken > perf->request_run_max)
		perf->request_run_max = taken;

	mutex_unlock(&controller->mutex);

	/* Common observers can see loss only after independent native storage is quarantined. */
	if (global_failure)
		bcm2711_render_fail(controller, error);

	/* A queue domain's completion behind unfinished executions of its session waits for them; the executor completes it. */
	parked = park_request(controller, request, error);
	if (parked)
		return 0;

	/* Completion delivery runs outside controller and IRQ locks, before slot reuse or close. */
	finish_request(controller, request, error);

	/* Succeeded: one accepted payload and callback finished, including an error outcome. */
	return 0;
}

/*
 * Stops new publication for one session and marks queued payloads canceled without waiting.
 */
void
bcm2711_render_worker_stop(
	struct bcm2711_render_session *session,
	int error)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_request *request;
	unsigned long enabled;
	uint32_t slot;

	/* Stop admission and queue cancellation share the same IRQ guard as submission. */
	controller = session->device;
	if (error == 0)
		error = ECANCELED;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	session->stopping = true;

	/* An active bounded operation retains its callback until the worker's actual retirement result. */
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		request = worker_entry(&controller->worker, slot);
		if (request->session != session)
			continue;

		/* Unpublished ordinary reservations remain withdrawable by the common stop monitor. */
		if (request->state == REQUEST_RETAINED)
			queue_marker(controller, request, error);
		if (request->state == REQUEST_QUEUED)
			request->canceled = error;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Queued canceled payloads still require disposal and exactly one common completion; queued executions are canceled by the executor. */
	waitq_wake_one(&controller->worker.available);
	waitq_wake_all(&controller->worker.executable);

	/* Succeeded: the selected session no longer admits new worker requests. */
	return;
}

/*
 * Confirms session callback and native retirement without blocking on an active worker.
 */
int
bcm2711_render_worker_stopped(
	struct bcm2711_render_session *session)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_request *request;
	unsigned long enabled;
	uint32_t slot;
	uint32_t unpublished;
	int error;

	/* Logical callback drain and uncertain native DMA are distinct stopping conditions. */
	controller = session->device;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* A reserved but unpublished callback supplies no native work and remains normally cancelable. */
	unpublished = 0;
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		request = worker_entry(&controller->worker, slot);
		if (request->session == session && request->state == REQUEST_RESERVED)
			unpublished++;
	}

	/* Every posted, retained or finishing callback must end before a local stop is confirmed. */
	error = 0;
	if (!session->stopping || session->pending != unpublished)
		error = EAGAIN;
	if (error == 0 && controller->worker.uncertain)
		error = EIO;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Neither pending callbacks nor uncertain DMA supplies a safe local stop. */
	if (error != 0)
		return error;

	/* Succeeded: no posted callback or uncertain native work remains; unpublished reservations are withdrawable. */
	return 0;
}

/*
 * Joins every accepted callback before common final close destroys its session descriptor.
 * Quarantined native VA allocations persist independently of this logical drain.
 */
void
bcm2711_render_worker_drain(
	struct bcm2711_render_session *session)
{
	struct bcm2711_render_device *controller;
	unsigned long enabled;
	uint64_t observed;
	uint32_t slot;
	struct bcm2711_render_request *request;
	int error;

	/* Closing producers cannot add another callback while queued payloads retire. */
	controller = session->device;
	bcm2711_render_worker_stop(session, ECANCELED);
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Final close joins unpublished callbacks that the common monitor has not withdrawn. */
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		request = worker_entry(&controller->worker, slot);
		if (request->session == session && request->state == REQUEST_RESERVED)
			queue_marker(controller, request, ECANCELED);
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Published close markers wake the permanent worker before drain observes its final condition. */
	waitq_wake_one(&controller->worker.available);
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Worker completion and the wait condition share this guard, preventing a lost final wake. */
	while (session->pending != 0) {
		observed = waitq_sequence(&controller->worker.retired);
		error = waitq_sleep(&controller->worker.retired, &controller->space.native->hardware.guard, observed, 0, 0);
		if (error != 0 && error != EAGAIN)
			__builtin_trap();
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the selected session has no outstanding worker request. */
	return;
}

/*
 * Arms independent storage quarantine and closes all publication before global loss is observed.
 */
void
bcm2711_render_worker_fault(
	struct bcm2711_render_device *controller,
	int error)
{
	struct bcm2711_render_request *request;
	unsigned long enabled;
	uint32_t slot;

	/* Native faults retain all VA storage before ordinary teardown can see the common error. */
	if (error == 0)
		error = EIO;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	controller->space.native->hardware.faulted = true;
	controller->worker.uncertain = true;

	/* Accepted queued callbacks still retire exactly once, without another native launch. */
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		request = worker_entry(&controller->worker, slot);
		if (request->state == REQUEST_QUEUED)
			request->canceled = error;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* The permanent worker disposes canceled payloads without claiming that native DMA stopped; the executor cancels queued executions. */
	waitq_wake_one(&controller->worker.available);
	waitq_wake_all(&controller->worker.executable);
	waitq_wake_all(&controller->worker.window_queue);

	/* Succeeded: the worker fault is visible to subsequent admission. */
	return;
}

/*
 * Tells whether one more failure line fits the kernel log's bound.
 *
 * The caller holds the controller mutex.
 */
bool
bcm2711_render_failure_line(
	struct bcm2711_render_device *controller)
{
	/* The bound keeps a client that fails every submission from flooding the log. */
	if (controller->worker.failure_lines >= WORKER_FAILURE_LINES)
		return false;
	controller->worker.failure_lines++;

	/* Succeeded: the caller may write the line. */
	return true;
}

/*
 * Clears uncertainty only after the caller verifies reset, mappings and all old owners retired.
 */
int
bcm2711_render_worker_recovered(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;
	uint32_t slot;

	/* The controller mutex excludes new producer namespaces during checked global recovery. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Callback delivery must have ended before reset can reopen any native publication. */
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		if (worker_entry(&controller->worker, slot)->state != REQUEST_FREE) {
			spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
			return EBUSY;
		}
	}

	/* No execution or parked completion may outlive the recovery. */
	if (controller->worker.ready_head != NULL || controller->worker.parked != NULL) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EBUSY;
	}

	/* The native reset's readbacks, cache and translation flushes supply the actual retirement proof. */
	if (!controller->space.native->hardware.ready || controller->space.native->hardware.faulted) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EIO;
	}

	/* Only checked native reinitialization clears the queue's DMA uncertainty. */
	controller->worker.uncertain = false;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the next session may publish native work into the recovered engine. */
	return 0;
}

/* Reserves one unpublished marker and its original common callback in an owned Vulkan domain. */
static int
job_reserve(
	void *opaque,
	void *private_session,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	void **token)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_session *session;
	struct bcm2711_render_request *request;
	unsigned long enabled;
	unsigned capacity;
	uint64_t mask;
	uint32_t held;
	uint32_t slot;

	/* A refused reservation transfers no callback or output token to the backend. */
	if (completion == NULL ||
	    token == NULL ||
	    timeline == 0 ||
	    timeline >= 64)
		return EINVAL;
	*token = NULL;
	controller = opaque;
	session = private_session;
	mask = UINT64_C(1) << timeline;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Domain ownership comes from the actual live Vulkan queue, never an arbitrary userspace index. */
	if ((session->timelines & mask) == 0) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EINVAL;
	}

	/* Faulted or stopping namespaces cannot acquire another producer-owned callback. */
	if (session->stopping ||
	    controller->worker.uncertain ||
	    !controller->space.native->hardware.ready ||
	    controller->space.native->hardware.faulted) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EIO;
	}

	/* Backpressure: the producer waits for room in its session and the engine, never failing for it. */
	capacity = marker_capacity(controller, session);
	if (capacity == 0 || session->pending == UINT32_MAX) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EAGAIN;
	}

	/* The capacity snapshot and pool acquisition share this guard, so a free marker must exist. */
	request = NULL;
	held = 0;
	for (slot = 0; slot < BCM2711_RENDER_MARKERS; slot++) {
		if (controller->worker.markers[slot].state != REQUEST_FREE) {
			held++;
			continue;
		}
		if (request == NULL)
			request = &controller->worker.markers[slot];
	}

	/* A slot preserves the callback without entering the FIFO or touching native registers. */
	if (request == NULL)
		__builtin_trap();
	request->session = session;
	request->completion = completion;
	request->supervised = true;
	request->timeline = timeline;
	request->state = REQUEST_RESERVED;
	session->pending++;
	if (session->markers < BCM2711_RENDER_MINIMUM)
		controller->worker.covered++;
	session->markers++;
	*token = request;

	/* Diagnostic: the most markers held at once in the report window, this one included. */
	if (held + 1U > controller->space.native->hardware.perf.markers_max)
		controller->space.native->hardware.perf.markers_max = held + 1U;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the producer owns one bounded marker until exact commit or cancel. */
	return 0;
}

/* Commits already reserved storage as a FIFO marker after the producer's accepted native commands. */
static int
job_commit(
	void *opaque,
	void *private_session,
	void *token,
	struct drv_gpu_completion *completion)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_session *session;
	struct bcm2711_render_request *request;
	unsigned long enabled;

	/* The opaque token is compared against live slots before any dereference. */
	controller = opaque;
	session = private_session;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	request = find_reservation(controller, session, token, completion);
	if (request == NULL || request->state != REQUEST_RESERVED) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return ESTALE;
	}

	/* Stop closes publication while keeping the original reservation withdrawable. */
	if (session->stopping ||
	    controller->worker.uncertain ||
	    !controller->space.native->hardware.ready ||
	    controller->space.native->hardware.faulted) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EIO;
	}

	/* The one native worker proves preceding command retirement before completing this marker. */
	queue_marker(controller, request, 0);

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Commit publishes using only reserved slot storage, with no allocation or native MMIO. */
	waitq_wake_one(&controller->worker.available);

	/* Succeeded: the original callback now belongs to an ordered completion marker. */
	return 0;
}

/* Withdraws definite nonacceptance without a callback, or retains an uncertain callback until stop. */
static int
job_cancel(
	void *opaque,
	void *private_session,
	void *token,
	struct drv_gpu_completion *completion,
	unsigned fault)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_session *session;
	struct bcm2711_render_request *request;
	unsigned long enabled;

	/* Only normal withdrawal and uncertain-work retention have defined callback ownership. */
	if (fault > 1)
		return EINVAL;
	controller = opaque;
	session = private_session;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	request = find_reservation(controller, session, token, completion);
	if (request == NULL) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return ESTALE;
	}

	/* An uncertain marker keeps its callback for stop/drain; active and finishing callbacks remain owned. */
	if (fault != 0) {
		if (request->state == REQUEST_RESERVED) {
			request->state = REQUEST_RETAINED;
			if (session->stopping)
				queue_marker(controller, request, EIO);
		}

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		waitq_wake_one(&controller->worker.available);
		return 0;
	}

	/* Normal cancellation is valid only before marker publication, even during a proven stop. */
	if (request->state != REQUEST_RESERVED) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return ESTALE;
	}

	/* The common core discards this completion after success; the backend must never call it again. */
	retire_entry(controller, request);

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Cancel may release the final close hold and actual reservation capacity independently of a callback. */
	waitq_wake_all(&controller->worker.retired);
	drv_gpu_capacity_changed(controller->gpu);

	/* Succeeded: definite nonacceptance withdrew the original callback without signaling completion. */
	return 0;
}

/* Reports bounded marker capacity only for a live, owned Vulkan completion domain. */
static int
job_capacity(
	void *opaque,
	void *private_session,
	uint32_t timeline,
	unsigned *available)
{
	struct bcm2711_render_device *controller;
	struct bcm2711_render_session *session;
	unsigned long enabled;
	uint64_t mask;

	/* A refused query owns no slot and reports no usable marker capacity. */
	if (available == NULL ||
	    timeline == 0 ||
	    timeline >= 64)
		return EINVAL;
	*available = 0;
	controller = opaque;
	session = private_session;
	mask = UINT64_C(1) << timeline;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	/* Queue creation owns this domain for the retained backend session. */
	if ((session->timelines & mask) == 0) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EINVAL;
	}

	/* Closed native admission cannot advertise capacity even if slots are numerically free. */
	if (session->stopping ||
	    controller->worker.uncertain ||
	    !controller->space.native->hardware.ready ||
	    controller->space.native->hardware.faulted) {
		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
		return EIO;
	}

	/* The snapshot promises no ownership; reserve competes for the same guarded pool. */
	*available = marker_capacity(controller, session);

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the caller may attempt a nonblocking reservation without consuming a slot. */
	return 0;
}

/* Resolves exact slot, owner and original callback identity without dereferencing an untrusted token. */
static struct bcm2711_render_request *
find_reservation(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_session *session,
	void *token,
	struct drv_gpu_completion *completion)
{
	struct bcm2711_render_request *request;
	uint32_t slot;

	/* Null or already withdrawn callbacks cannot name a retained reservation. */
	if (token == NULL || completion == NULL)
		return NULL;

	/* The IRQ guard excludes slot reuse throughout identity validation and the caller's transition. */
	for (slot = 0; slot < BCM2711_RENDER_MARKERS; slot++) {
		request = &controller->worker.markers[slot];
		if (token != request ||
		    request->session != session ||
		    request->completion != completion ||
		    !request->supervised)
			continue;
		return request;
	}

	/* Succeeded: no retained slot matches this exact opaque reservation identity. */
	return NULL;
}

/* Publishes an owned reservation into the ordered worker FIFO without allocation. */
static void
queue_marker(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_request *request,
	int error)
{
	/* A reserved or uncertain unpublished callback now becomes posted work for stop/drain. */
	request->execute = execute_marker;
	request->dispose = dispose_marker;
	request->payload = NULL;
	request->canceled = error;
	request->state = REQUEST_QUEUED;
	request->next = NULL;
	request->queued_ticks = clock_ticks();
	if (controller->worker.tail == NULL) {
		controller->worker.head = request;
	} else {
		controller->worker.tail->next = request;
	}

	/* The marker retains the original pending hold until callback delivery ends. */
	controller->worker.tail = request;

	/* Succeeded: the reservation belongs to the ordered worker FIFO. */
	return;
}

/* Completes an ordered marker only after the single worker retired all preceding native commands. */
static int
execute_marker(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_session *session,
	void *payload,
	bool *retired)
{
	/* A completion marker owns ordering only and borrows no native payload. */
	(void)controller;
	(void)session;
	(void)payload;

	/* This marker launches no DMA; prior command execution supplied the actual native retirement proof. */
	*retired = true;

	/* Succeeded: an ordered marker needs no additional native operation to retire. */
	return 0;
}

/* Releases a marker with no prepared payload or independent native allocation. */
static int
dispose_marker(
	struct bcm2711_render_device *controller,
	void *payload,
	bool retired)
{
	/* An empty marker has no storage whose disposal depends on DMA retirement. */
	(void)controller;
	(void)payload;
	(void)retired;

	/* Succeeded: the marker never acquired any DMA storage that needs disposal. */
	return 0;
}

/*
 * Counts the markers a session may reserve now; the caller holds the native IRQ guard.
 *
 * Up to the minimum, a session needs only a free marker, which the pool keeps
 * for every open session.  Past it, the session's executions, bytes and
 * markers stay within its limits, the engine's executions and bytes within
 * the global ones, and the markers left cover the other sessions' minimum.
 */
static unsigned
marker_capacity(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_session *session)
{
	struct bcm2711_render_worker *worker;
	uint64_t unfinished;
	uint64_t bytes;
	uint32_t free_markers;
	uint32_t minimum;
	uint32_t own;
	uint32_t others;
	uint32_t slot;
	unsigned minimum_room;
	unsigned capacity;

	/* Free markers in the pool. */
	worker = &controller->worker;
	free_markers = 0;
	for (slot = 0; slot < BCM2711_RENDER_MARKERS; slot++) {
		if (worker->markers[slot].state == REQUEST_FREE)
			free_markers++;
	}
	if (free_markers == 0)
		return 0;
	unfinished = session->submitted - session->executed;
	bytes = session->execution_bytes;

	/* The minimum is always available to a session below it. */
	minimum_room = 0;
	if (session->markers < BCM2711_RENDER_MINIMUM &&
	    unfinished < BCM2711_RENDER_MINIMUM &&
	    bytes + BCM2711_RENDER_SUBMISSION_BYTES <= BCM2711_RENDER_MINIMUM_BYTES) {
		minimum_room = BCM2711_RENDER_MINIMUM - session->markers;
		if (unfinished > session->markers)
			minimum_room = BCM2711_RENDER_MINIMUM - (uint32_t)unfinished;
	}

	/* Past the minimum: the session's limits, the engine's, and the other sessions' minimum left free. */
	capacity = 0;
	if (session->markers < BCM2711_RENDER_SESSION_EXECUTIONS &&
	    unfinished < BCM2711_RENDER_SESSION_EXECUTIONS &&
	    bytes + BCM2711_RENDER_SUBMISSION_BYTES <= BCM2711_RENDER_SESSION_BYTES &&
	    worker->executions < BCM2711_RENDER_EXECUTIONS &&
	    worker->execution_bytes + BCM2711_RENDER_SUBMISSION_BYTES <= BCM2711_RENDER_BYTES) {
		own = 0;
		if (session->markers < BCM2711_RENDER_MINIMUM)
			own = BCM2711_RENDER_MINIMUM - session->markers;
		minimum = BCM2711_RENDER_MINIMUM * worker->sessions;
		others = 0;
		if (minimum > worker->covered + own)
			others = minimum - worker->covered - own;
		if (free_markers > others) {
			capacity = free_markers - others;
			if (capacity > BCM2711_RENDER_SESSION_EXECUTIONS - session->markers)
				capacity = BCM2711_RENDER_SESSION_EXECUTIONS - session->markers;
			if (capacity > BCM2711_RENDER_SESSION_EXECUTIONS - unfinished)
				capacity = BCM2711_RENDER_SESSION_EXECUTIONS - (uint32_t)unfinished;
			if (capacity > BCM2711_RENDER_EXECUTIONS - worker->executions)
				capacity = BCM2711_RENDER_EXECUTIONS - worker->executions;
		}
	}

	/* The larger room, within the free markers. */
	if (capacity < minimum_room)
		capacity = minimum_room;
	if (capacity > free_markers)
		capacity = free_markers;

	/* Succeeded: this numerical snapshot grants no marker. */
	return capacity;
}

/* Returns one entry of the command slots followed by the marker pool. */
static struct bcm2711_render_request *
worker_entry(
	struct bcm2711_render_worker *worker,
	uint32_t index)
{
	/* Command slots first. */
	if (index < BCM2711_RENDER_REQUESTS)
		return &worker->requests[index];

	/* Succeeded: a marker of the pool. */
	return &worker->markers[index - BCM2711_RENDER_REQUESTS];
}

/* Frees a finished or withdrawn entry and its session's pending hold; the caller holds the native IRQ guard. */
static void
retire_entry(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_request *request)
{
	struct bcm2711_render_session *session;

	/* A marker also leaves its session's count, and the minimum it covered. */
	session = request->session;
	if (session->pending == 0)
		__builtin_trap();
	if (request->supervised) {
		if (session->markers == 0)
			__builtin_trap();
		session->markers--;
		if (session->markers < BCM2711_RENDER_MINIMUM) {
			if (controller->worker.covered == 0)
				__builtin_trap();
			controller->worker.covered--;
		}
	}
	kern_memset(request, 0, sizeof(*request));
	session->pending--;

	/* Succeeded: the entry is free for reuse. */
	return;
}

/* Serves the permanent FIFO and sleeps only with the queue condition guard held. */
static void
worker_main(
	void *opaque)
{
	struct bcm2711_render_device *controller;
	unsigned long enabled;
	uint64_t observed;
	int error;

	/* The registered controller and its thread retain the queue for the entire kernel lifetime. */
	controller = opaque;

	/* Ordered work always uses the same one-step retirement path before waiting for producers. */
	while (true) {
		error = bcm2711_render_worker_step(controller);
		if (error == 0)
			continue;
		if (error != EAGAIN)
			__builtin_trap();
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		/* A producer racing the empty dequeue cannot disappear between observation and sleep. */
		while (controller->worker.head == NULL) {
			observed = waitq_sequence(&controller->worker.available);
			error = waitq_sleep(&controller->worker.available, &controller->space.native->hardware.guard, observed, 0, 0);
			if (error != 0 && error != EAGAIN)
				__builtin_trap();
		}

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
	}

	/* The permanent FIFO has no ordinary termination path. */
	return;
}

/* Gives the single worker one immutable active request while retaining its session callback count. */
static struct bcm2711_render_request *
take_request(
	struct bcm2711_render_device *controller)
{
	struct bcm2711_render_request *request;
	unsigned long enabled;

	/* The permanent FIFO owner serializes dequeue against publication and queued cancellation. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	request = controller->worker.head;
	if (request != NULL) {
		controller->worker.head = request->next;
		if (controller->worker.head == NULL)
			controller->worker.tail = NULL;
		request->next = NULL;
		request->state = REQUEST_ACTIVE;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: a nonnull request retains its complete payload and common callback. */
	return request;
}

/* Ends the common callback before releasing its slot and the session's last pending hold. */
static void
finish_request(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_request *request,
	int error)
{
	struct drv_gpu_completion *completion;
	unsigned long enabled;

	/* FINISHING keeps the slot and session alive across reentrant common completion observers. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (request->state != REQUEST_ACTIVE && request->state != REQUEST_PARKED)
		__builtin_trap();
	request->state = REQUEST_FINISHING;
	completion = request->completion;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* The common completion owns its own lifetime and may wake a concurrently closing thread. */
	drv_gpu_complete(completion, error);
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (request->state != REQUEST_FINISHING)
		__builtin_trap();
	retire_entry(controller, request);

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Capacity and final close observers see the fully retired slot and callback count. */
	waitq_wake_all(&controller->worker.retired);
	drv_gpu_capacity_changed(controller->gpu);

	/* Succeeded: the callback outcome is delivered and its pending hold is retired. */
	return;
}

/*
 * Queues an accepted execution behind its session's earlier ones.
 *
 * The session's pending count holds it through its end, so drain and close
 * wait for it; the session joins the executor's round robin if it was idle.
 */
void
bcm2711_render_execution_submit(
	struct bcm2711_render_session *session,
	struct bcm2711_render_execution *execution)
{
	struct bcm2711_render_device *controller;
	unsigned long enabled;

	/* The execution joins the tail of its session's queue. */
	controller = session->device;
	execution->session = session;
	execution->next = NULL;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (session->execution_tail == NULL) {
		session->execution_head = execution;
	} else {
		session->execution_tail->next = execution;
	}
	session->execution_tail = execution;
	session->submitted++;
	session->execution_bytes += execution->bytes;
	session->pending++;
	controller->worker.executions++;
	controller->worker.execution_bytes += execution->bytes;

	/* An idle session joins the round robin's tail. */
	if (!session->ready) {
		session->ready = true;
		session->ready_next = NULL;
		if (controller->worker.ready_tail == NULL) {
			controller->worker.ready_head = session;
		} else {
			controller->worker.ready_tail->ready_next = session;
		}
		controller->worker.ready_tail = session;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* The executor sees the new work. */
	waitq_wake_all(&controller->worker.executable);

	/* Succeeded: the execution is queued. */
	return;
}

/*
 * Runs one step of the execution at the head of the next ready session.
 *
 * Sessions take turns a step at a time.  A stopped session's or a faulted
 * engine's executions are ended without running.  When an execution ends,
 * the completions of its session that waited for it are delivered outside every lock.
 */
int
bcm2711_render_executor_step(
	struct bcm2711_render_device *controller)
{
	struct bcm2711_render_session *session;
	struct bcm2711_render_execution *execution;
	struct bcm2711_render_request *finished;
	struct bcm2711_render_request **link;
	struct bcm2711_render_request *request;
	struct bcm2711_render_request *next;
	struct bcm2711_render_session **ready;
	struct bcm2711_render_session *previous;
	unsigned long enabled;
	uint64_t bytes;
	struct bcm2711_v3d_perf *perf;
	const char *stage;
	uint64_t held;
	uint64_t released;
	bool display;
	bool allowed;
	bool failed;
	bool done;
	int ended;
	int error;

	/* The chosen ready session runs its oldest execution. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	session = select_session(&controller->worker);
	execution = NULL;
	display = false;
	if (session != NULL) {
		execution = session->execution_head;
		display = execution->display;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Nothing waits. */
	if (session == NULL || execution == NULL)
		return EAGAIN;
	mutex_lock(&controller->mutex);
	held = clock_ticks();
	released = controller->worker.released_ticks;

	/* A stopped session or a faulted engine ends the execution without running it. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	error = 0;
	if (session->stopping)
		error = ECANCELED;
	if (controller->space.native->hardware.faulted || controller->worker.uncertain)
		error = EIO;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/*
	 * One step, or the end of an execution that cannot run.  A step that
	 * failed while preparing ends only its execution; any other failure is
	 * the engine's.
	 */
	done = true;
	failed = false;
	stage = "cancel";
	if (error == 0) {
		execution->engine = true;
		error = execution->step(controller, execution, &done);
		stage = "step";
		if (error != 0) {
			done = true;
			failed = execution->engine;
			if (!failed)
				stage = "prepare";
		}
	}
	bytes = execution->bytes;
	if (done) {
		/* The finished execution leaves its queue before its end frees it; a submit may be linking behind it. */
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		session->execution_head = execution->next;
		if (session->execution_head == NULL)
			session->execution_tail = NULL;

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		ended = execution->end(controller, execution, error);
		if (ended != 0) {
			failed = true;
			stage = "end";
			if (error == 0)
				error = ended;
		}
	}

	/*
	 * Says which execution failed and whether the engine went with it; a
	 * stopped session's cancel is ordinary.  The session's counts are read
	 * without their guard, for the log only.
	 */
	if (error != 0 && error != ECANCELED) {
		allowed = bcm2711_render_failure_line(controller);
		if (allowed) {
			kern_logf(
				"v3d: execution failed stage=%s error=%d engine=%u display=%u session=%llu/%llu\n",
				stage,
				error,
				(unsigned)failed,
				(unsigned)display,
				(unsigned long long)session->executed + 1U,
				(unsigned long long)session->submitted);
		}
	}

	/* A failure closes native admission before the mutex lets anyone observe it. */
	if (failed)
		bcm2711_render_worker_fault(controller, error);

	/* Diagnostic: the step's hold of the mutex, without the window. */
	held = clock_ticks() - held - (controller->worker.released_ticks - released);
	perf = &controller->space.native->hardware.perf;
	if (held > perf->step_hold_max)
		perf->step_hold_max = held;

	mutex_unlock(&controller->mutex);

	/* Common observers see the loss outside the mutex. */
	if (failed)
		bcm2711_render_fail(controller, error);

	/* The session leaves the round robin's head; a finished execution releases the completions that waited for it. */
	finished = NULL;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	previous = NULL;
	ready = &controller->worker.ready_head;
	while (*ready != session) {
		previous = *ready;
		ready = &previous->ready_next;
	}
	*ready = session->ready_next;
	if (controller->worker.ready_tail == session)
		controller->worker.ready_tail = previous;
	session->ready_next = NULL;
	controller->worker.display_last = display;
	if (done) {
		session->executed++;
		session->execution_bytes -= bytes;
		controller->worker.executions--;
		controller->worker.execution_bytes -= bytes;
		link = &controller->worker.parked;
		while (*link != NULL) {
			request = *link;
			if (request->session == session && request->required <= session->executed) {
				*link = request->parked_next;
				request->parked_next = finished;
				finished = request;
				continue;
			}
			link = &request->parked_next;
		}
	}

	/* A session with more work takes its next turn at the tail. */
	session->ready = false;
	if (session->execution_head != NULL) {
		session->ready = true;
		if (controller->worker.ready_tail == NULL) {
			controller->worker.ready_head = session;
		} else {
			controller->worker.ready_tail->ready_next = session;
		}
		controller->worker.ready_tail = session;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* The released requests complete outside every lock with their own outcome, or the stop or fault that came meanwhile. */
	for (request = finished; request != NULL; request = next) {
		next = request->parked_next;
		request->parked_next = NULL;
		finish_request(controller, request, parked_outcome(controller, request));
	}

	/* A finished execution gives back its session's pending hold. */
	if (done) {
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		if (session->pending == 0)
			__builtin_trap();
		session->pending--;

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		waitq_wake_all(&controller->worker.retired);
		drv_gpu_capacity_changed(controller->gpu);
	}

	/* Succeeded: one step ran or one execution ended. */
	return 0;
}

/*
 * Parks a queue domain's completion while its session has unfinished executions; reports whether it was parked.
 *
 * A request with a domain (a producer's marker or a selected context fence)
 * completes only after every execution its session accepted before it, a
 * canceled one too: its completion proves the earlier work retired.  The
 * request's own outcome is kept for the release.  A request without a domain
 * reports decoder progress and completes at once.
 */
static bool
park_request(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_request *request,
	int error)
{
	struct bcm2711_render_session *session;
	unsigned long enabled;
	bool parked;

	/* Decoder notifications do not wait. */
	if (request->timeline == 0)
		return false;
	session = request->session;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	parked = false;
	if (session->executed < session->submitted) {
		request->state = REQUEST_PARKED;
		request->canceled = error;
		request->required = session->submitted;
		request->parked_next = controller->worker.parked;
		controller->worker.parked = request;
		parked = true;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Reports whether the executor now owns the completion. */
	return parked;
}

/*
 * Chooses the ready session whose oldest execution runs next; the caller holds the native IRQ guard.
 *
 * A display execution (a frame, a present copy, the cursor) goes first, but
 * after one ran another session's turn comes if one is ready, so the display
 * waits at most one other step and no session starves.  Otherwise the round
 * robin's head.
 */
static struct bcm2711_render_session *
select_session(
	struct bcm2711_render_worker *worker)
{
	struct bcm2711_render_session *session;

	/* After a display step, the first session whose oldest execution is not a display one. */
	if (worker->display_last) {
		for (session = worker->ready_head; session != NULL; session = session->ready_next) {
			if (!session->execution_head->display)
				return session;
		}
	} else {
		/* Otherwise the first display execution. */
		for (session = worker->ready_head; session != NULL; session = session->ready_next) {
			if (session->execution_head->display)
				return session;
		}
	}

	/* Succeeded: the round robin's head, or none. */
	return worker->ready_head;
}

/* Returns the outcome a released parked request reports: its own, else the stop or fault that came while it waited. */
static int
parked_outcome(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_request *request)
{
	unsigned long enabled;
	int error;

	/* The request's own outcome first, then stop, then a native fault. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	error = request->canceled;
	if (error == 0 && request->session->stopping)
		error = ECANCELED;
	if (error == 0 && controller->space.native->hardware.faulted)
		error = EIO;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: zero when the marker completes normally. */
	return error;
}

/* Runs executions for the controller's lifetime, sleeping while none waits. */
static void
executor_main(
	void *opaque)
{
	struct bcm2711_render_device *controller;
	unsigned long enabled;
	uint64_t observed;
	int error;

	/* The registered controller and its thread live for the kernel's lifetime. */
	controller = opaque;

	/* Steps run until no execution waits; then the executor sleeps until one is queued. */
	while (true) {
		error = bcm2711_render_executor_step(controller);

		/* The mutex is not handed off: a decoder or ioctl waiting for it gets a turn between steps. */
		if (error == 0) {
			sched_yield();
			continue;
		}
		if (error != EAGAIN)
			__builtin_trap();
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		/* A queued execution cannot appear between the empty observation and the sleep. */
		while (controller->worker.ready_head == NULL) {
			observed = waitq_sequence(&controller->worker.executable);
			error = waitq_sleep(&controller->worker.executable, &controller->space.native->hardware.guard, observed, 0, 0);
			if (error != 0 && error != EAGAIN)
				__builtin_trap();
		}

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);
	}
}

/* Counts an opened session for the marker minimum; EBUSY when the pool cannot cover one more. */
int
bcm2711_render_worker_session_open(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;
	int error;

	/* The count changes under the guard the reservations read it under. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	error = EBUSY;
	if (controller->worker.sessions < BCM2711_RENDER_SESSIONS) {
		controller->worker.sessions++;
		error = 0;
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Reports whether the session may open. */
	return error;
}

/* Uncounts a closed session. */
void
bcm2711_render_worker_session_close(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;

	/* Every close follows a counted open. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (controller->worker.sessions == 0)
		__builtin_trap();
	controller->worker.sessions--;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the session no longer counts. */
	return;
}

/* Tells whether a live request still names the session's domain; the caller holds the native IRQ guard. */
bool
bcm2711_render_worker_timeline_busy(
	struct bcm2711_render_device *controller,
	struct bcm2711_render_session *session,
	uint32_t timeline)
{
	struct bcm2711_render_request *request;
	uint32_t slot;

	/* Every slot and marker, in any state but free (a free entry names no session). */
	for (slot = 0; slot < WORKER_ENTRIES; slot++) {
		request = worker_entry(&controller->worker, slot);
		if (request->session == session && request->timeline == timeline)
			return true;
	}

	/* Succeeded: no request keeps the domain. */
	return false;
}

/*
 * Refuses an execution past the hard limits.
 *
 * Producers that reserve a marker per submission never reach them; a stream
 * submitting without markers is refused instead of growing without bound.
 */
int
bcm2711_render_execution_admit(
	struct bcm2711_render_session *session,
	uint64_t bytes)
{
	struct bcm2711_render_device *controller;
	unsigned long enabled;
	int error;

	/* The decoder holds the controller mutex, so only the executor changes the counts meanwhile, downward. */
	controller = session->device;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	error = 0;
	if (session->submitted - session->executed >= BCM2711_RENDER_HARD_EXECUTIONS ||
	    session->execution_bytes + bytes > BCM2711_RENDER_HARD_BYTES)
		error = ENOMEM;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Reports whether the execution may be queued. */
	return error;
}

/*
 * Opens the window: the executor gives up the controller mutex while a pass's native jobs run.
 *
 * Mappers that were already waiting go first, once: the executor lets them
 * have the mutex and waits until as many have finished as were waiting, so
 * a stream of later mappers cannot keep the window shut.
 */
void
bcm2711_render_window_open(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;
	uint64_t observed;
	uint64_t target;
	int error;

	/* The mappers counted now are the ones to let through. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (controller->worker.pte_waiters != 0) {
		target = controller->worker.pte_done + controller->worker.pte_waiters;

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		mutex_unlock(&controller->mutex);
		controller->worker.released_at = clock_ticks();
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		/* A fault ends the wait; the launch then refuses. */
		while (controller->worker.pte_done < target && !controller->space.native->hardware.faulted) {
			observed = waitq_sequence(&controller->worker.window_queue);
			error = waitq_sleep(&controller->worker.window_queue, &controller->space.native->hardware.guard, observed, 0, 0);
			if (error != 0 && error != EAGAIN)
				__builtin_trap();
		}

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		mutex_lock(&controller->mutex);
		controller->worker.released_ticks += clock_ticks() - controller->worker.released_at;
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);
	}

	/* No translation changes from here until the window closes. */
	if (controller->space.native->hardware.window)
		__builtin_trap();
	controller->space.native->hardware.window = true;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	mutex_unlock(&controller->mutex);
	controller->worker.released_at = clock_ticks();

	/* Succeeded: the window is open and the mutex is free. */
	return;
}

/* Closes the window: takes the controller mutex again and unmaps the views released inside it. */
int
bcm2711_render_window_close(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;
	int error;

	/* The mutex first, so no mapper slips in between; the time without it leaves the step's hold. */
	mutex_lock(&controller->mutex);
	controller->worker.released_ticks += clock_ticks() - controller->worker.released_at;
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (!controller->space.native->hardware.window)
		__builtin_trap();
	controller->space.native->hardware.window = false;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Waiting mappers may proceed once the executor gives up the mutex. */
	waitq_wake_all(&controller->worker.window_queue);

	/* The views whose last reference ended inside the window. */
	error = bcm2711_v3d_memory_release_delayed(&controller->space);
	if (error != 0)
		return error;

	/* Succeeded: the window is closed and translations are current. */
	return 0;
}

/*
 * Takes the controller mutex with the window closed, for an ioctl that maps.
 *
 * The caller is counted as a mapper until it unlocks, so the executor lets
 * it through before opening its next window.
 */
void
bcm2711_render_map_lock(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;
	uint64_t observed;
	int error;

	/* Counted before waiting, so the executor knows to wait for it. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	controller->worker.pte_waiters++;
	while (true) {
		/* The window closes after its native jobs; a fault ends the wait and the map refuses. */
		while (controller->space.native->hardware.window && !controller->space.native->hardware.faulted) {
			observed = waitq_sequence(&controller->worker.window_queue);
			error = waitq_sleep(&controller->worker.window_queue, &controller->space.native->hardware.guard, observed, 0, 0);
			if (error != 0 && error != EAGAIN)
				__builtin_trap();
		}

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		mutex_lock(&controller->mutex);
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

		/* The executor may have opened another window before this thread got the mutex. */
		if (!controller->space.native->hardware.window || controller->space.native->hardware.faulted)
			break;

		spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

		mutex_unlock(&controller->mutex);
		enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);
	}

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	/* Succeeded: the caller holds the mutex and may map. */
	return;
}

/* Gives back the controller mutex taken for a map and counts the mapper as done. */
void
bcm2711_render_map_unlock(
	struct bcm2711_render_device *controller)
{
	unsigned long enabled;

	/* The executor waiting for earlier mappers counts this one. */
	enabled = spin_lock_irqsave(&controller->space.native->hardware.guard);

	if (controller->worker.pte_waiters == 0)
		__builtin_trap();
	controller->worker.pte_waiters--;
	controller->worker.pte_done++;

	spin_unlock_irqrestore(&controller->space.native->hardware.guard, enabled);

	mutex_unlock(&controller->mutex);
	waitq_wake_all(&controller->worker.window_queue);

	/* Succeeded: the mutex is free. */
	return;
}

