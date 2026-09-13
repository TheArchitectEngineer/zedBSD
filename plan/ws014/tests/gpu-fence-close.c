/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Separates producer-death notification from asynchronous callback retirement.
 * The real GPU/fd/fence cores remain linked; only native completion is modeled.
 */

#define GPU_FENCE_HELPERS_ONLY
#include "gpu-fence.c"

/* One independent receiver remains open while its producer enters final close. */
static struct test_file *close_receiver;

/* The retained exported capability identifies the pending producer generation. */
static int close_fence_fd;

/* The closing backend context remains valid until its pending callback is drained. */
static void *close_session;

/* Exactly one producer drain must precede destruction of its live resource. */
static unsigned close_drains;

static void close_drain(void *opaque, void *session_data);
static void close_destroy(void *opaque, void *session_data, void *resource_data);

/*
 * Observes terminal error at drain entry and then delivers a late normal completion.
 */
int
main(
	void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_command_ops commands;
	struct drv_gpu_device *device;
	struct test_backend backend;
	struct test_file source;
	struct test_file receiver;
	struct process process;
	struct gpu_fence_create created;
	struct gpu_fence_state state;
	struct gpu_resource_create resource;
	struct gpu_command_submit_sync submit;
	int error;

	/* Real descriptor lookup and references keep the shared payload independently owned. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);

	/* The backend retains one resource and one actual common completion until drain. */
	memset(&backend, 0, sizeof(backend));
	memset(&operations, 0, sizeof(operations));
	commands.submit = fence_submit;
	commands.drain = close_drain;
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_RESOURCE | GPU_CAP_NOTIFICATION | GPU_CAP_FENCE;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.resource_create = backend_create;
	operations.resource_destroy = close_destroy;
	operations.commands = &commands;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == 0);

	/* Independent GPU opens share only the exported fence capability. */
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&receiver, "gpu0", O_RDWR);
	assert(error == 0);
	close_receiver = &receiver;

	/* A pending source resource proves final-close notification does not imply memory retirement. */
	prepare_create(&resource);
	error = fence_ioctl(&source, GPU_RESOURCE_CREATE, &resource);
	assert(error == 0);
	assert(backend.live == 1U);

	/* The accepted marker owns a signal binding but does not complete before producer close. */
	fence_create_request(&created);
	error = fence_ioctl(&source, GPU_FENCE_CREATE, &created);
	assert(error == 0);
	close_fence_fd = created.fd;
	fence_submit_request(&submit, created.fd, 1U);
	error = fence_ioctl(&source, GPU_COMMAND_SUBMIT_SYNC, &submit);
	assert(error == 0);
	assert(pending[0].completion != NULL);
	close_session = pending[0].session;

	/* An extra file reference delays final-close semantics just as a retained alias or mapping does. */
	file_ref(&source.file);
	close_file(&source);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 1U, 0, &state);
	assert(error == 0);
	assert(state.state == GPU_FENCE_PENDING);
	assert(close_drains == 0U);
	assert(backend.closes == 0U);
	assert(backend.destroyed == 0U);

	/* Final close must notify the receiver before entering the potentially blocking native drain. */
	close_file(&source);
	assert(close_drains == 1U);
	assert(pending[0].completion == NULL);
	assert(backend.closes == 1U);
	assert(backend.destroyed == 1U);
	assert(backend.live == 0U);
	error = fence_state_call(&receiver, GPU_FENCE_QUERY, created.fd, 1U, 0, &state);
	assert(error == 0);
	assert(state.state == GPU_FENCE_ERROR);
	assert(state.error == ENODEV);

	/* Receiver cleanup consumes only its independent open and the remaining descriptor reference. */
	close_session = NULL;
	close_file(&receiver);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U);
	assert(held_spinlocks == 0U);
	puts("GPU fence close: final-reference boundary, error before drain, retained resource/session/callback and late-success exclusion PASS");

	/* Succeeded: logical producer loss and safe native retirement have separate verified boundaries. */
	return 0;
}

/* Checks the independent receiver before permitting the native callback to retire. */
static void
close_drain(
	void *opaque,
	void *session_data)
{
	struct test_backend *backend;
	struct test_session *session;
	struct gpu_fence_state state;
	int error;

	/* A backend drain begins without a common spinlock and with its own session alive. */
	backend = opaque;
	session = session_data;
	assert(held_spinlocks == 0U);
	assert(session->backend == backend);

	/* The independent receiver has no pending native work when it closes later. */
	if (session_data != close_session) {
		assert(session->live == 0U);
		assert(pending[0].completion == NULL);
		return;
	}

	/* Producer error must already be visible even if native drain would wait until its watchdog. */
	error = fence_state_call(close_receiver, GPU_FENCE_QUERY, close_fence_fd, 1U, 0, &state);
	assert(error == 0);
	assert(state.state == GPU_FENCE_ERROR);
	assert(state.error == ENODEV);

	/* Signaling error cannot destroy backend resources or the session embedded callback storage. */
	assert(session->live == 1U);
	assert(backend->live == 1U);
	assert(backend->destroyed == 0U);
	assert(backend->closes == 0U);
	assert(pending[0].session == session_data);
	assert(pending[0].completion != NULL);

	/* A late normal transport callback still reaches its valid common record without signaling success. */
	drv_gpu_complete(pending[0].completion, 0);
	error = fence_state_call(close_receiver, GPU_FENCE_QUERY, close_fence_fd, 1U, 0, &state);
	assert(error == 0);
	assert(state.state == GPU_FENCE_ERROR);
	assert(state.error == ENODEV);

	/* Native callback ownership ends only after actual common publication returns. */
	pending[0].completion = NULL;
	close_drains++;

	/* Succeeded: subsequent resource and session destruction can no longer race this callback. */
	return;
}

/* Verifies resource retirement remains after the callback drain despite earlier error publication. */
static void
close_destroy(
	void *opaque,
	void *session_data,
	void *resource_data)
{
	/* Only the producer allocated storage, and its pending callback must already be gone. */
	assert(session_data == close_session);
	assert(close_drains == 1U);
	assert(pending[0].completion == NULL);

	/* The existing strict destructor verifies resource ownership and final allocation accounting. */
	backend_destroy(opaque, session_data, resource_data);

	/* Succeeded: memory retirement follows the safe native boundary. */
	return;
}
