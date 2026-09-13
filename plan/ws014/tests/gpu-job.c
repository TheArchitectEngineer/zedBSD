/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises real GPU/fd/fence ownership with a bounded strict-completion peer. */
#define GPU_FENCE_HELPERS_ONLY
#include "gpu-fence.c"
#include <uapi/gpu-job.h>

/* Each accepted backend reservation retains the actual common completion pointer. */
struct job_slot {
	void *session;
	struct drv_gpu_completion *completion;
	unsigned committed;
};

/* Retains four deterministic backend callback obligations until cancellation or delivery. */
static struct job_slot jobs[4];

/* Names the actual registered wrapper used by the peer to report explicit transport failure. */
static struct drv_gpu_device *job_device;

/* Selects one same-open terminal consume inside commit; NULL disables the interleaving. */
static struct test_file *job_fast_file;

/* Carries the immutable sequence consumed by the selected immediate-completion interleaving. */
static uint64_t job_fast_sequence;

/* Counts accepted reservations so failed copyout can prove no backend work was retained. */
static unsigned job_reservations;

/* Counts prepared marker publications throughout this finite ownership scenario. */
static unsigned job_commits;

/* Counts producer drains while the independent receiver checks early logical failure. */
static unsigned job_drains;

/* Selects one pre-acceptance backend refusal; zero permits ordinary reservation. */
static int job_rejection;

/* Identifies the retained payload checked at the producer drain boundary. */
static int job_close_fd;

/* Retains the receiver open only during the final-close ordering assertion. */
static struct test_file *job_close_receiver;

static int job_reserve(void *opaque, void *session, uint32_t timeline, struct drv_gpu_completion *completion, void **reservation);
static int job_commit(void *opaque, void *session, void *reservation, struct drv_gpu_completion *completion);
static int job_cancel(void *opaque, void *session, void *reservation, struct drv_gpu_completion *completion, unsigned fault);
static void job_drain(void *opaque, void *session);
static void job_request(struct gpu_job_reserve *request, int fd, uint64_t generation);
static int job_action(struct test_file *file, unsigned long command, uint64_t sequence, unsigned flags);
static int job_observe(struct test_file *file, uint64_t sequence, unsigned consume, int *status);
static void job_deliver(struct job_slot *slot, int error);

#ifndef GPU_JOB_HELPERS_ONLY
/*
 * Checks reservation rollback, exact completion, error isolation and final-close lifetime.
 */
int
main(
	void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_command_ops commands;
	struct drv_gpu_job_ops job_operations;
	struct test_backend backend;
	struct test_file source;
	struct test_file receiver;
	struct process process;
	struct gpu_fence_create created;
	struct gpu_fence_state state;
	struct drv_gpu_fence_state observed;
	struct gpu_resource_create resource;
	struct gpu_job_reserve request;
	struct gpu_job_reserve extra;
	struct kernel_handle *handle;
	uint64_t sequence;
	unsigned before;
	unsigned index;
	short readiness;
	int status;
	int error;

	/* Actual descriptor-table installation and actual cdev opens establish authority. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);

	/* The backend begins without resources or session ownership. */
	memset(&backend, 0, sizeof(backend));

	/* Legacy notification remains available beside the strict job contract. */
	commands.submit = fence_submit;
	commands.drain = job_drain;

	/* Strict reservations have explicit publish and nonacceptance operations. */
	job_operations.reserve = job_reserve;
	job_operations.commit = job_commit;
	job_operations.cancel = job_cancel;

	/* Registration validates the complete immutable operation table. */
	memset(&operations, 0, sizeof(operations));
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_JOB | GPU_CAP_NOTIFICATION | GPU_CAP_FENCE | GPU_CAP_RESOURCE;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.resource_create = backend_create;
	operations.resource_destroy = backend_destroy;
	operations.commands = &commands;
	operations.jobs = &job_operations;
	error = drv_gpu_register(&operations, &backend, &job_device);
	assert(error == 0);
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&receiver, "gpu0", O_RDWR);
	assert(error == 0);
	fence_create_request(&created);
	error = fence_ioctl(&source, GPU_FENCE_CREATE, &created);
	assert(error == 0);
	handle = handle_fd_get(created.fd, KERNEL_HANDLE_DRIVER);
	assert(handle != NULL);

	/* An unsubmitted CPU-waitable payload is not a kernel GPU dependency. */
	error = drv_gpu_fence_wait_work(handle, 1U, &observed);
	assert(error == EAGAIN);
	before = allocations;
	job_request(&request, created.fd, 1U);
	reject_copyout = 1U;
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == EFAULT && allocations == before && job_reservations == 0U);
	error = fence_bind_call(&source, created.fd, 1U, 0U, 0U);
	assert(error == 0);
	error = fence_bind_call(&source, created.fd, 1U, 0U, GPU_FENCE_BIND_RELEASE);
	assert(error == 0);

	/* Backend refusal happens before native work and releases both record and producer binding. */
	job_rejection = EAGAIN;
	job_request(&request, created.fd, 1U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == EAGAIN && jobs[0].completion == NULL);
	job_rejection = 0;
	job_request(&request, created.fd, 1U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == 0 && request.sequence != 0U);
	sequence = request.sequence;
	error = drv_gpu_fence_wait_work(handle, 1U, &observed);
	assert(error == EAGAIN);
	error = fence_state_call(&source, GPU_FENCE_SIGNAL, created.fd, 1U, 0, &state);
	assert(error == EPERM);
	error = job_action(&receiver, GPU_JOB_COMMIT, sequence, 0U);
	assert(error == ENOENT);
	error = job_action(&source, GPU_JOB_CANCEL, sequence, 0U);
	assert(error == 0 && jobs[0].completion == NULL);
	error = job_observe(&source, sequence, 0U, &status);
	assert(error == ENOENT);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 1U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_PENDING);

	/* Commit uses prepared storage even when the very next allocation would fail. */
	job_request(&request, created.fd, 1U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == 0);
	reject_allocation = 1U;
	error = job_action(&source, GPU_JOB_COMMIT, request.sequence, 0U);
	assert(error == 0 && reject_allocation == 1U && jobs[0].committed == 1U);
	reject_allocation = 0U;
	error = job_action(&source, GPU_JOB_CANCEL, request.sequence, 0U);
	assert(error == EALREADY);
	job_deliver(&jobs[0], 0);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 1U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_SIGNALED);
	error = job_observe(&source, request.sequence, 1U, &status);
	assert(error == 0 && status == 0);
	error = fence_state_call(&receiver, GPU_FENCE_RESET, created.fd, 1U, 0, &state);
	assert(error == 0 && state.generation == 2U);
	error = fence_state_call(&source, GPU_FENCE_SIGNAL, created.fd, 1U, 0, &state);
	assert(error != 0);

	/* Local API errors cannot poison another open's pending payload or POLLERR state. */
	error = fence_bind_call(&receiver, created.fd, 2U, 0U, 0U);
	assert(error == 0);
	for (index = 0U; index < 2U; index++) {
		backend.create_error = index == 0U ? ENODEV : ETIMEDOUT;
		prepare_create(&resource);
		error = fence_ioctl(&source, GPU_RESOURCE_CREATE, &resource);
		assert(error == backend.create_error);
		error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 2U, 0, &state);
		assert(error == 0 && state.state == GPU_FENCE_PENDING);
		readiness = 0;
		error = receiver.file.f_ops->poll(&receiver.file, POLLIN | POLLERR, &readiness);
		assert(error == 0 && (readiness & POLLERR) == 0);
	}
	backend.create_error = 0;
	error = fence_bind_call(&receiver, created.fd, 2U, 0U, GPU_FENCE_BIND_RELEASE);
	assert(error == 0);

	/* Immediate terminal delivery and CONSUME cannot recycle the action's still-used record. */
	job_request(&request, created.fd, 2U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == 0);
	job_fast_file = &source;
	job_fast_sequence = request.sequence;
	error = job_action(&source, GPU_JOB_COMMIT, request.sequence, 0U);
	assert(error == 0);
	job_fast_file = NULL;
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 2U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_SIGNALED);
	error = job_observe(&source, request.sequence, 0U, &status);
	assert(error == ENOENT);

	/* Bounded backend saturation rejects before work while sequence-only reservations retain supervision. */
	for (index = 0U; index < 4U; index++) {
		job_request(&request, -1, 0U);
		error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
		assert(error == 0);
	}
	job_request(&extra, -1, 0U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &extra);
	assert(error == EAGAIN);

	/* Explicit backend failure terminates even bindings with no matching callback. */
	error = fence_state_call(&receiver, GPU_FENCE_RESET, created.fd, 2U, 0, &state);
	assert(error == 0 && state.generation == 3U);
	error = fence_bind_call(&receiver, created.fd, 3U, 0U, 0U);
	assert(error == 0);
	error = job_action(&source, GPU_JOB_CANCEL, request.sequence, GPU_JOB_CANCEL_FAULT);
	assert(error == 0);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 3U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_ERROR && state.error == EIO);
	error = job_observe(&source, request.sequence, 1U, &status);
	assert(error == 0 && status == EIO);

	/* Checked recovery belongs to the backend; this fixture explicitly clears its modeled fault. */
	drv_gpu_report_error(job_device, 0);
	fence_create_request(&created);
	error = fence_ioctl(&source, GPU_FENCE_CREATE, &created);
	assert(error == 0);
	job_request(&request, created.fd, 1U);
	error = fence_ioctl(&source, GPU_JOB_RESERVE, &request);
	assert(error == 0);
	error = job_action(&source, GPU_JOB_COMMIT, request.sequence, 0U);
	assert(error == 0);
	job_close_fd = created.fd;
	job_close_receiver = &receiver;
	close_file(&source);
	assert(job_drains == 1U);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 1U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_ERROR && state.error == ENODEV);
	job_close_receiver = NULL;
	close_file(&receiver);

	/* An extra transport-style wrapper reference safely outlives registration withdrawal. */
	drv_gpu_retain(job_device);
	error = drv_gpu_unregister(job_device);
	assert(error == 0);
	drv_gpu_report_error(job_device, EIO);
	drv_gpu_release(job_device);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	handle_put(handle);
	assert(allocations == 0U && held_spinlocks == 0U);
	puts("GPU jobs: rollback, admission, no-allocation commit, exact completion, consume race, fault isolation and retained close PASS");

	/* Succeeded: real common ownership retired independently from the bounded backend peer. */
	return 0;
}

#endif

/* Reserves one callback and token without publishing any native marker. */
static int
job_reserve(
	void *opaque,
	void *session,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	void **reservation)
{
	unsigned index;

	/* No driver callback may execute while the GPU registry lock is held. */
	(void)opaque;
	assert(held_spinlocks == 0U && timeline == 1U);
	*reservation = NULL;

	/* Selected refusal transfers no callback ownership. */
	if (job_rejection != 0)
		return job_rejection;

	/* A free token retains one exact callback until cancel, completion or drain. */
	for (index = 0U; index < 4U; index++) {
		/* In-use tokens cannot be borrowed by another reservation. */
		if (jobs[index].completion != NULL)
			continue;

		/* Publish the exact callback only after finding an unused token. */
		jobs[index].session = session;
		jobs[index].completion = completion;
		jobs[index].committed = 0U;
		*reservation = &jobs[index];
		job_reservations++;
		return 0;
	}

	/* Failed: no callback or native work was accepted. */
	return EAGAIN;
}

/* Publishes prepared work and can exercise an immediate terminal-consume race. */
static int
job_commit(
	void *opaque,
	void *session,
	void *reservation,
	struct drv_gpu_completion *completion)
{
	struct job_slot *slot;
	int status;
	int error;

	/* Token identity and callback ownership remain unchanged through publication. */
	(void)opaque;
	slot = reservation;
	assert(held_spinlocks == 0U && slot->session == session);
	assert(slot->completion == completion && slot->committed == 0U);
	slot->committed = 1U;
	job_commits++;

	/* The selected immediate completion exposes a real same-fd CONSUME race. */
	if (job_fast_file != NULL) {
		job_deliver(slot, 0);
		error = job_observe(job_fast_file, job_fast_sequence, 1U, &status);
		assert(error == 0 && status == 0);
	}

	/* Succeeded: the strict callback now proves native completion. */
	return 0;
}

/* Separates definite nonacceptance from device-wide uncertain native work. */
static int
job_cancel(
	void *opaque,
	void *session,
	void *reservation,
	struct drv_gpu_completion *completion,
	unsigned fault)
{
	struct job_slot *slot;
	unsigned index;

	/* Cancellation must still name the exact callback and originating session. */
	(void)opaque;
	slot = reservation;
	assert(held_spinlocks == 0U && slot->session == session);
	assert(slot->completion == completion);

	/* Native uncertainty terminates all admitted work through explicit backend failure. */
	if (fault != 0U) {
		drv_gpu_report_error(job_device, EIO);
		for (index = 0U; index < 4U; index++) {
			if (jobs[index].completion != NULL)
				job_deliver(&jobs[index], EIO);
		}
	} else {
		assert(slot->committed == 0U);
		slot->completion = NULL;
	}

	/* Succeeded: no canceled callback can arrive later. */
	return 0;
}

/* Verifies early logical loss while the backend still retains the producer context. */
static void
job_drain(
	void *opaque,
	void *session)
{
	struct gpu_fence_state state;
	unsigned index;
	int error;

	/* Backend context destruction must follow callback drain. */
	(void)opaque;
	assert(held_spinlocks == 0U && ((struct test_session *)session)->backend != NULL);

	/* The retained receiver observes logical failure before native cleanup begins. */
	if (job_close_receiver != NULL) {
		error = fence_state_call(job_close_receiver, GPU_FENCE_QUERY, job_close_fd, 1U, 0, &state);
		assert(error == 0 && state.state == GPU_FENCE_ERROR && state.error == ENODEV);
		job_drains++;
	}

	/* Late success cannot overwrite final-close ERROR, and every callback retires before return. */
	for (index = 0U; index < 4U; index++) {
		if (jobs[index].completion != NULL && jobs[index].session == session)
			job_deliver(&jobs[index], 0);
	}

	/* Legacy notification callbacks share the same final-close ownership barrier. */
	fence_drain(opaque, session);

	/* Succeeded: the backend holds no completion pointer into the closing session. */
	return;
}

/* Initializes an exact optional-payload reservation request. */
static void
job_request(
	struct gpu_job_reserve *request,
	int fd,
	uint64_t generation)
{
	/* Output and reserved fields begin zeroed on every new attempt. */
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->fd = fd;
	request->generation = generation;
	request->timeline = 1U;

	/* Succeeded: only a kernel-returned sequence can authorize a later action. */
	return;
}

/* Sends one exact fixed-width action through the actual cdev dispatcher. */
static int
job_action(
	struct test_file *file,
	unsigned long command,
	uint64_t sequence,
	unsigned flags)
{
	struct gpu_job_action request;
	int error;

	/* No test-private state substitutes for ioctl validation or origin ownership. */
	memset(&request, 0, sizeof(request));
	request.version = GPU_ABI_VERSION;
	request.size = sizeof(request);
	request.sequence = sequence;
	request.flags = flags;
	error = fence_ioctl(file, command, &request);

	/* Succeeded or failed: preserve the actual framework result. */
	return error;
}

/* Observes the real sequence ledger without sleeping in the sequential peer. */
static int
job_observe(
	struct test_file *file,
	uint64_t sequence,
	unsigned consume,
	int *status)
{
	struct gpu_command_wait request;
	int error;

	/* Immediate observation leaves pending jobs and failed copyouts intact. */
	memset(&request, 0, sizeof(request));
	request.version = GPU_ABI_VERSION;
	request.size = sizeof(request);
	request.sequence = sequence;
	request.flags = consume != 0U ? GPU_WAIT_CONSUME : 0U;
	error = fence_ioctl(file, GPU_COMMAND_WAIT, &request);
	*status = (int)request.status;

	/* Succeeded or failed: return the actual retained-record result. */
	return error;
}

/* Delivers exactly one retained backend callback to the real GPU framework. */
static void
job_deliver(
	struct job_slot *slot,
	int error)
{
	struct drv_gpu_completion *completion;

	/* Withdrawing the token first prevents a test callback from being delivered twice. */
	completion = slot->completion;
	assert(completion != NULL);
	slot->completion = NULL;
	drv_gpu_complete(completion, error);

	/* Succeeded: the backend owns no further completion obligation for this token. */
	return;
}
