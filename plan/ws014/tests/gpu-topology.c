/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reuses the actual cdev/GPU display fixture's bounded kernel and backend collaborators. */
#define GPU_DISPLAY_TEST_ENTRY gpu_display_unused_main
#define copyout topology_copyout_base
#include "../../ws030/tests/gpu-display.c"
#undef copyout

/* One independently evolving device inventory outlives every fixture open. */
static uint64_t topology_sequence = 1U;

/* A selected copyout publishes a new event before the core may commit its older acknowledgement. */
static unsigned topology_inject_event;

/* A selected ordinary query reenters event observation while its session admission remains held. */
static struct test_file *topology_reenter;

/* A selected snapshot withdraws publication while its retained file still owns the backend. */
static struct drv_gpu_device *topology_retire;

/* Snapshot counters and an explicit driver error verify offline and overflow handling. */
static unsigned topology_snapshots;
static int topology_error;

static int topology_snapshot(void *opaque, void *session, uint64_t *sequence);
static int topology_query(void *opaque, void *session, struct gpu_display_info *request);
static int topology_submit(void *opaque, void *session, const void *command, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);
static void topology_drain(void *opaque, void *session);
static int topology_call(struct file *file, uint64_t acknowledge, struct gpu_display_events *request);
static short topology_poll(struct file *file, short events);

/*
 * Injects an independent device change after successful user publication but before ACK commit.
 */
int
copyout(
	const void *source,
	uintptr_t destination,
	size_t size)
{
	int error;

	/* Existing malformed-output behavior remains the real fixture's independent failure source. */
	error = topology_copyout_base(source, destination, size);
	if (error != 0)
		return error;

	/* The event cannot be consumed by the older snapshot already copied to this caller. */
	if (topology_inject_event != 0U && size == sizeof(struct gpu_display_events)) {
		topology_inject_event = 0U;
		topology_sequence++;
		poll_notify();
	}

	/* Succeeded: the core still has not committed any observation or acknowledgement cursor. */
	return 0;
}

/*
 * Verifies topology readiness, exact acknowledgement and backend lifetime through real GPU dispatch.
 */
int
main(
	void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_display_ops display;
	struct drv_gpu_command_ops commands;
	struct drv_gpu_device *device;
	struct test_backend backend;
	struct test_file first;
	struct test_file second;
	struct test_file writer;
	struct gpu_display_events request;
	struct gpu_display_info inventory;
	struct gpu_command_submit submit;
	struct gpu_command_wait wait;
	struct file *alias;
	uint32_t command;
	unsigned snapshots;
	short ready;
	int error;

	/* The display peer owns ordinary resource lifetime and a separately changing event sequence. */
	memset(&backend, 0, sizeof(backend));
	memset(&display_state, 0, sizeof(display_state));
	display_state.backend = &backend;
	memset(&operations, 0, sizeof(operations));
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_RESOURCE | GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS | GPU_CAP_NOTIFICATION;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.resource_create = backend_create;
	operations.resource_destroy = backend_destroy;

	/* Every ordinary display operation retains its previous dispatch contract. */
	memset(&display, 0, sizeof(display));
	display.query = topology_query;
	display.mode = display_test_mode;
	display.claim = display_test_claim;
	display.release = display_test_release;
	display.present = display_test_present;
	display.wait = display_test_wait;
	operations.display = &display;
	commands.submit = topology_submit;
	commands.drain = topology_drain;
	operations.commands = &commands;

	/* The new capability and callback must agree in both directions. */
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == EINVAL);
	display.events = topology_snapshot;
	operations.capabilities &= ~GPU_CAP_DISPLAY_EVENTS;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == EINVAL);
	operations.capabilities |= GPU_CAP_DISPLAY_EVENTS;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == 0);

	/* Every new readable open begins with an inventory that has never been acknowledged. */
	error = open_file(&first, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&second, "gpu0", O_RDONLY);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_WRONLY);
	assert(error == 0);
	ready = topology_poll(&first.file, POLLIN | POLLPRI);
	assert(ready == POLLPRI);
	ready = topology_poll(&second.file, POLLPRI);
	assert(ready == POLLPRI);
	error = topology_call(&writer.file, 0U, &request);
	assert(error == EACCES);
	ready = topology_poll(&writer.file, POLLPRI);
	assert(ready == 0);

	/* ACK cannot invent an observation that this independent open has not successfully queried. */
	error = topology_call(&first.file, 1U, &request);
	assert(error == EINVAL);
	reject_copyout = 1U;
	error = topology_call(&first.file, 0U, &request);
	assert(error == EFAULT);
	error = topology_call(&first.file, 1U, &request);
	assert(error == EINVAL);
	error = topology_call(&first.file, 0U, &request);
	assert(error == 0 && request.sequence == 1U && request.events == GPU_DISPLAY_EVENT_CHANGE);
	ready = topology_poll(&first.file, POLLPRI);
	assert(ready == POLLPRI);

	/* An ordinary inventory query may safely observe events without recursively taking admission. */
	memset(&inventory, 0, sizeof(inventory));
	inventory.version = GPU_ABI_VERSION;
	inventory.size = sizeof(inventory);
	topology_reenter = &first;
	error = cdev_file_ops.ioctl(&first.file, GPU_DISPLAY_QUERY, (uintptr_t)&inventory);
	assert(error == 0 && topology_reenter == NULL && inventory.generation == 1U);

	/* Copyout failure leaves the earlier event level-ready even though its generation was observed. */
	reject_copyout = 1U;
	error = topology_call(&first.file, 1U, &request);
	assert(error == EFAULT);
	ready = topology_poll(&first.file, POLLPRI);
	assert(ready == POLLPRI);
	error = topology_call(&first.file, 1U, &request);
	assert(error == 0 && request.events == 0U);
	ready = topology_poll(&first.file, POLLPRI);
	assert(ready == 0);
	ready = topology_poll(&second.file, POLLPRI);
	assert(ready == POLLPRI);

	/* An event delivered between snapshot publication and exact ACK remains independently ready. */
	topology_sequence = 2U;
	error = topology_call(&first.file, 0U, &request);
	assert(error == 0 && request.sequence == 2U);
	topology_inject_event = 1U;
	error = topology_call(&first.file, 2U, &request);
	assert(error == 0 && request.sequence == 2U && topology_sequence == 3U);
	ready = topology_poll(&first.file, POLLPRI);
	assert(ready == POLLPRI);
	error = topology_call(&first.file, 3U, &request);
	assert(error == EINVAL);
	error = topology_call(&first.file, 0U, &request);
	assert(error == 0 && request.sequence == 3U);
	error = topology_call(&first.file, 3U, &request);
	assert(error == 0);
	error = topology_call(&first.file, 1U, &request);
	assert(error == 0 && request.events == 0U);

	/* A retained descriptor alias shares exactly the same open's monotonic acknowledgement. */
	alias = &first.file;
	file_ref(alias);
	close_file(&first);
	ready = topology_poll(alias, POLLPRI);
	assert(ready == 0);

	/* Command completion and topology invalidation have distinct simultaneous readiness bits. */
	memset(&submit, 0, sizeof(submit));
	command = 1U;
	submit.version = GPU_ABI_VERSION;
	submit.size = sizeof(submit);
	submit.address = (uint64_t)(uintptr_t)&command;
	submit.bytes = sizeof(command);
	error = cdev_file_ops.ioctl(alias, GPU_COMMAND_SUBMIT, (uintptr_t)&submit);
	assert(error == 0 && submit.sequence != 0U);
	topology_sequence = 4U;
	ready = topology_poll(alias, POLLIN | POLLPRI);
	assert(ready == (POLLIN | POLLPRI));
	error = topology_call(alias, 0U, &request);
	assert(error == 0);
	error = topology_call(alias, 4U, &request);
	assert(error == 0);
	ready = topology_poll(alias, POLLIN | POLLPRI);
	assert(ready == POLLIN);

	/* Consuming the actual terminal work record does not modify the separate event cursor. */
	memset(&wait, 0, sizeof(wait));
	wait.version = GPU_ABI_VERSION;
	wait.size = sizeof(wait);
	wait.sequence = submit.sequence;
	wait.flags = GPU_WAIT_CONSUME;
	error = cdev_file_ops.ioctl(alias, GPU_COMMAND_WAIT, (uintptr_t)&wait);
	assert(error == 0);
	ready = topology_poll(alias, POLLIN | POLLPRI);
	assert(ready == 0);

	/* Malformed snapshots and overflow never silently clear acknowledged history. */
	topology_error = EOVERFLOW;
	error = topology_call(alias, 0U, &request);
	assert(error == EOVERFLOW);
	ready = topology_poll(alias, POLLPRI);
	assert(ready == POLLERR);
	topology_error = 0;
	error = topology_call(alias, 0U, &request);
	assert(error == 0);
	request.reserved = 1U;
	error = cdev_file_ops.ioctl(alias, GPU_DISPLAY_EVENTS, (uintptr_t)&request);
	assert(error == EINVAL);

	/* Withdrawal racing a snapshot cannot free the retained backend or invoke it again offline. */
	topology_retire = device;
	error = topology_call(alias, 0U, &request);
	assert(error == 0 && topology_retire == NULL);
	snapshots = topology_snapshots;
	ready = topology_poll(alias, POLLPRI);
	assert(ready == (POLLERR | POLLHUP) && snapshots == topology_snapshots);
	error = topology_call(alias, 0U, &request);
	assert(error == ENODEV && snapshots == topology_snapshots);

	/* Final alias and independent-open closes release every offline callback owner. */
	error = file_close(alias);
	assert(error == 0);
	close_file(&second);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	assert(backend.opens == backend.closes);
	assert(allocations == 0U && held_spinlocks == 0U);
	puts("GPU topology: exact ACK, copyout rollback, racing event, independent opens, alias lifetime, admission bypass, command coexistence, offline and overflow PASS");

	/* Succeeded: no event observer, cdev reference or backend allocation survived the fixture. */
	return 0;
}

/* Samples an independent backend sequence without borrowing any core lock or admission state. */
static int
topology_snapshot(
	void *opaque,
	void *session,
	uint64_t *sequence)
{
	struct drv_gpu_device *device;
	int error;

	/* The core must retain this open's backend while calling outside its own registry lock. */
	assert(opaque == display_state.backend && session != NULL);
	assert(held_spinlocks == 0U);
	topology_snapshots++;

	/* A registration can become offline while this callback's file still retains its storage. */
	if (topology_retire != NULL) {
		device = topology_retire;
		topology_retire = NULL;
		error = drv_gpu_unregister(device);
		assert(error == EBUSY);
	}

	/* A saturated driver stream reports failure rather than reusing an old sequence. */
	if (topology_error != 0)
		return topology_error;

	/* Succeeded: the event is observed without acknowledgement or hardware traffic. */
	*sequence = topology_sequence;
	return 0;
}

/* Performs actual ordinary display re-enumeration with a controlled event-observer reentry. */
static int
topology_query(
	void *opaque,
	void *session,
	struct gpu_display_info *request)
{
	struct gpu_display_events event;
	struct test_file *opened;
	int error;

	/* Existing validated discovery outputs remain independent from the event sequence interface. */
	error = display_test_query(opaque, session, request);
	if (error != 0)
		return error;

	/* The event ioctl must not wait on the ordinary ioctl that currently owns this session. */
	if (topology_reenter != NULL) {
		opened = topology_reenter;
		topology_reenter = NULL;
		error = topology_call(&opened->file, 0U, &event);
		assert(error == 0);
	}

	/* Succeeded: a real output query has observed the modeled current generation. */
	request->generation = topology_sequence;
	return 0;
}

/* Completes one real core record without pretending its event readiness implies GPU rendering. */
static int
topology_submit(
	void *opaque,
	void *session,
	const void *command,
	uint32_t bytes,
	uint32_t flags,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	/* The immediate callback still follows actual submission ownership and validation. */
	assert(held_spinlocks == 0U);
	assert(opaque == display_state.backend && session != NULL);
	assert(command != NULL && bytes == 4U);
	assert(flags == 0U && timeline == 0U);
	drv_gpu_complete(completion, 0);

	/* Succeeded: no outstanding backend callback remains after the terminal notification. */
	return 0;
}

/* Verifies that immediate test completions leave no deferred callback at final close. */
static void
topology_drain(
	void *opaque,
	void *session)
{
	/* Backend state remains live until the core invokes the actual close callback. */
	assert(opaque == display_state.backend && session != NULL);
	assert(held_spinlocks == 0U);

	/* Succeeded: this backend never retains an unfinished completion. */
	return;
}

/* Builds fresh input framing for one query or an exact previously observed acknowledgement. */
static int
topology_call(
	struct file *file,
	uint64_t acknowledge,
	struct gpu_display_events *request)
{
	int error;

	/* Output fields are cleared instead of recycling a preceding QUERY reply as input. */
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->ack_sequence = acknowledge;
	if (acknowledge != 0U)
		request->flags = GPU_DISPLAY_EVENT_ACK;

	/* Real cdev dispatch reaches the production GPU per-open observer. */
	error = cdev_file_ops.ioctl(file, GPU_DISPLAY_EVENTS, (uintptr_t)request);
	if (error != 0)
		return error;

	/* Succeeded: this returned snapshot has acquired observation authority on its open. */
	return 0;
}

/* Samples actual cdev readiness without consuming command or event state. */
static short
topology_poll(
	struct file *file,
	short events)
{
	short ready;
	int error;

	/* The retained file selects one real GPU open description and its independent ACK cursor. */
	ready = 0;
	error = cdev_file_ops.poll(file, events, &ready);
	assert(error == 0);

	/* Succeeded: callers inspect the independent readiness classes. */
	return ready;
}
