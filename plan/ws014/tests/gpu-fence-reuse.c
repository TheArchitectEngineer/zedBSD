/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* A dependency wait must pin both fd capabilities before another thread can close and reuse one. */
#define GPU_JOB_HELPERS_ONLY
#include "gpu-job.c"

/* The one explicit interleaving replaces a signal descriptor while the real wait has released its lock. */
static struct process *reuse_process;
static struct test_file *reuse_receiver;
static uint64_t reuse_wait_sequence;
static int reuse_signal_fd;
static struct gpu_fence_create reuse_created;

extern int (*gpu_test_wait_hook)(struct wait_queue *, struct spinlock *, uint64_t, uint64_t, unsigned);
static int reuse_wait(struct wait_queue *queue, struct spinlock *lock, uint64_t observed, uint64_t deadline, unsigned flags);

/* Exercises descriptor reuse at the production dependency wait's real scheduler handoff. */
int
main(void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_command_ops commands;
	struct drv_gpu_job_ops job_operations;
	struct drv_gpu_recovery_ops recovery;
	struct gpu_job_reserve job;
	struct drv_gpu_device *device;
	struct test_backend backend;
	struct test_file source;
	struct test_file receiver;
	struct process process;
	struct gpu_fence_create wait_created;
	struct gpu_fence_create signal_created;
	struct gpu_fence_state state;
	struct gpu_command_submit_sync submit;
	struct kernel_handle *handle;
	int alias;
	int error;

	/* One producer and one independent observer share the real process descriptor namespace. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);
	memset(&backend, 0, sizeof(backend));
	memset(&operations, 0, sizeof(operations));
	commands.submit = fence_submit;
	commands.drain = job_drain;
	memset(&job_operations, 0, sizeof(job_operations));
	job_operations.reserve = job_reserve;
	job_operations.commit = job_commit;
	job_operations.cancel = job_cancel;
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_NOTIFICATION | GPU_CAP_FENCE | GPU_CAP_JOB;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.commands = &commands;
	operations.jobs = &job_operations;

	/* The peer proves native retirement through its exact retained job and command slots. */
	memset(&recovery, 0, sizeof(recovery));
	recovery.fault = job_fault;
	recovery.stop_begin = job_stop_begin;
	recovery.stop_poll = job_stop_poll;
	operations.recovery = &recovery;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == 0);
	job_device = device;
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&receiver, "gpu0", O_RDWR);
	assert(error == 0);

	/* The prerequisite is pending and belongs to the independent producer open. */
	fence_create_request(&wait_created);
	error = fence_ioctl(&receiver, GPU_FENCE_CREATE, &wait_created);
	assert(error == 0);
	job_request(&job, wait_created.fd, 1U);
	error = fence_ioctl(&receiver, GPU_JOB_RESERVE, &job);
	assert(error == 0);
	error = job_action(&receiver, GPU_JOB_COMMIT, job.sequence, 0U);
	assert(error == 0);
	fence_create_request(&signal_created);
	error = fence_ioctl(&source, GPU_FENCE_CREATE, &signal_created);
	assert(error == 0);

	/* A separate alias lets the test observe which original capability acquired signal authority. */
	handle = handle_fd_get(signal_created.fd, KERNEL_HANDLE_DRIVER);
	assert(handle != NULL);
	alias = handle_fd_create(handle, 0);
	assert(alias >= 0);
	handle_put(handle);

	/* The scheduler peer closes and reuses the supplied signal fd before resolving the prerequisite. */
	reuse_process = &process;
	reuse_receiver = &receiver;
	reuse_wait_sequence = job.sequence;
	reuse_signal_fd = signal_created.fd;
	gpu_test_wait_hook = reuse_wait;
	fence_submit_request(&submit, signal_created.fd, 1U);
	submit.wait_fd = wait_created.fd;
	submit.wait_generation = 1U;
	error = fence_ioctl(&source, GPU_COMMAND_SUBMIT_SYNC, &submit);
	assert(error == 0 && gpu_test_wait_hook == NULL);
	fence_notification(&source, submit.command.sequence, 0);

	/* Only the original payload may have been bound, even though its original fd now names a new payload. */
	error = fence_state_call(&source, GPU_FENCE_SIGNAL, alias, 1U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_SIGNALED);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, reuse_created.fd, 1U, 0, &state);
	assert(error == 0 && state.state == GPU_FENCE_PENDING);
	error = fence_bind_call(&receiver, reuse_created.fd, 1U, 0U, 0U);
	assert(error == 0);
	error = fence_bind_call(&receiver, reuse_created.fd, 1U, 0U, GPU_FENCE_BIND_RELEASE);
	assert(error == 0);

	/* Every replaced fd, alias, pending callback and GPU session reaches its real final cleanup. */
	close_file(&source);
	close_file(&receiver);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U && held_spinlocks == 0U);
	puts("GPU fence dependency: signal fd close/reuse during real wait preserves original capability PASS");

	/* Succeeded: the test interleaving changed scheduling without replacing kernel ownership logic. */
	return 0;
}

/* Models another thread's actual descriptor and signal operations while the waiting lock is released. */
static int
reuse_wait(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	int status;
	int error;

	/* Exactly one requested interleaving is allowed; any other blocking path remains a fixture failure. */
	(void)queue;
	(void)observed;
	(void)deadline;
	assert(flags == WAITQ_INTERRUPTIBLE);
	gpu_test_wait_hook = NULL;
	spin_unlock_irqrestore(lock, 1U);

	/* The same integer now names a different valid GPU fence payload, proving a late lookup would be wrong. */
	error = filedesc_close(reuse_process->fd, reuse_signal_fd);
	assert(error == 0);
	fence_create_request(&reuse_created);
	error = fence_ioctl(reuse_receiver, GPU_FENCE_CREATE, &reuse_created);
	assert(error == 0 && reuse_created.fd == reuse_signal_fd);
	/* The admitted backend job completes without any userspace signal operation. */
	job_deliver(&jobs[0], 0);
	error = job_observe(reuse_receiver, reuse_wait_sequence, 1U, &status);
	assert(error == 0 && status == 0);

	/* The actual condition changed, so the waiting core rechecks instead of treating a wake as completion. */
	(void)spin_lock_irqsave(lock);
	return EAGAIN;
}
