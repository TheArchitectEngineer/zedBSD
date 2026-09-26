/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Synchronization objects and notifications: AML mutexes and events
 * (ACPI 6.5 sections 19.6.1, 19.6.87 and 19.6.144), and the delivery of
 * Notify to the handlers drivers install.
 *
 * One interpreter lock serializes all AML, so an AML mutex that another
 * evaluation owns can only become free while that evaluation sleeps; the
 * waits here poll with drv_acpi_os_sleep(), which lets the lock go.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The timeout that means waiting for ever.
 */
#define TIMEOUT_FOREVER 0xffffU

/*
 * How long one poll of a busy mutex or an unsignaled event sleeps, in
 * milliseconds.
 */
#define POLL_MILLISECONDS 1U

static int sync_object(struct drv_acpi_eval *eval, enum drv_acpi_type type, struct drv_acpi_object **result);
static int mutex_acquire(struct drv_acpi_object *mutex, uint64_t timeout, bool *timed_out);
static int mutex_release(struct drv_acpi_object *mutex);
static int event_wait(struct drv_acpi_object *event, uint64_t timeout, bool *timed_out);

/*
 * Installs a handler for the notifications a node receives.
 */
int
drv_acpi_notify_install(
	struct drv_acpi_node *node,
	drv_acpi_notify_handler_t handler,
	void *argument)
{
	struct drv_acpi_notify *notify;

	/* Allocates the entry. */
	notify = drv_acpi_os_alloc(sizeof(*notify));
	if (notify == NULL)
		return ENOMEM;

	/* Puts it at the head of the node's list. */
	notify->handler = handler;
	notify->argument = argument;
	notify->next = node->notify;
	node->notify = notify;

	/* Succeeded. */
	return 0;
}

/*
 * Hands a Notify to every handler installed on the node.
 *
 * A notification nobody handles is logged and dropped, as firmware
 * expects of an operating system without the driver.
 */
int
drv_acpi_notify(
	struct drv_acpi_node *node,
	uint32_t value)
{
	struct drv_acpi_notify *notify;
	char path[128];
	int error;

	/* Logs a notification that has no handler. */
	if (node->notify == NULL) {
		error = drv_acpi_node_path(node, path, sizeof(path));
		if (error != 0)
			kern_strcpy(path, "(long path)");
		drv_acpi_os_log("ACPI: Notify(%s, 0x%x) has no handler\n", path, (unsigned)value);
		return 0;
	}

	/* Calls each handler. */
	for (notify = node->notify; notify != NULL; notify = notify->next)
		notify->handler(node, value, notify->argument);

	/* Succeeded. */
	return 0;
}

/*
 * Runs Acquire, Release, Signal, Wait or Reset.
 */
int
drv_acpi_sync_operator(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	struct drv_acpi_object *answer;
	enum drv_acpi_type type;
	uint64_t timeout;
	bool timed_out;
	int error;

	/* Finds the mutex or event the operator names. */
	type = DRV_ACPI_TYPE_EVENT;
	if (opcode == DRV_ACPI_OP_ACQUIRE || opcode == DRV_ACPI_OP_RELEASE)
		type = DRV_ACPI_TYPE_MUTEX;
	error = sync_object(eval, type, &object);
	if (error != 0)
		return error;

	/* Runs the operator. */
	timed_out = false;
	error = 0;
	switch (opcode) {
	case DRV_ACPI_OP_ACQUIRE:
		/* The timeout is a word in the AML, not a TermArg. */
		error = drv_acpi_stream_integer(eval, 2, &timeout);
		if (error != 0)
			break;
		error = mutex_acquire(object, timeout, &timed_out);
		break;
	case DRV_ACPI_OP_RELEASE:
		error = mutex_release(object);
		break;
	case DRV_ACPI_OP_SIGNAL:
		/* A signal is counted until a Wait takes it. */
		object->value.event.pending++;
		break;
	case DRV_ACPI_OP_WAIT:
		error = drv_acpi_eval_integer(eval, &timeout);
		if (error != 0)
			break;
		error = event_wait(object, timeout, &timed_out);
		break;
	default:
		/* Reset forgets every signal not yet taken. */
		object->value.event.pending = 0;
		break;
	}

	/* Lets go of the object and reports a failed operator. */
	drv_acpi_object_release(object);
	if (error != 0)
		return error;

	/* Acquire and Wait report true when they timed out; the rest report zero. */
	answer = drv_acpi_object_integer_new(0);
	if (answer == NULL)
		return ENOMEM;
	if (timed_out)
		answer->value.integer = drv_acpi_integer_mask();

	/* Succeeded. */
	*result = answer;
	return 0;
}

/* Parses the SuperName of a mutex or event and checks its type. */
static int
sync_object(
	struct drv_acpi_eval *eval,
	enum drv_acpi_type type,
	struct drv_acpi_object **result)
{
	struct drv_acpi_target target;
	struct drv_acpi_object *object;
	struct drv_acpi_node *node;
	int error;

	/* Parses the operand. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;

	/* A name is the object; a reference is followed to its node. */
	object = NULL;
	if (target.kind == DRV_ACPI_TARGET_NODE) {
		node = drv_acpi_ns_resolve_alias(target.node);
		object = node->object;
	} else if (target.kind == DRV_ACPI_TARGET_REFERENCE) {
		node = drv_acpi_object_reference_node(target.reference);
		if (node != NULL)
			object = drv_acpi_ns_resolve_alias(node)->object;
	} else if (target.kind == DRV_ACPI_TARGET_LOCAL) {
		object = eval->frame->locals[target.index];
	} else if (target.kind == DRV_ACPI_TARGET_ARGUMENT) {
		object = eval->frame->arguments[target.index];
	}

	/* The object stays alive in its node or slot; the target is done with. */
	drv_acpi_target_release(&target);

	/* A local or an argument may hold a reference to the object. */
	if (object != NULL && object->type == DRV_ACPI_TYPE_REFERENCE) {
		node = drv_acpi_object_reference_node(object);
		object = NULL;
		if (node != NULL)
			object = drv_acpi_ns_resolve_alias(node)->object;
	}

	/* Refuses anything but the expected type. */
	if (object == NULL || object->type != type) {
		drv_acpi_os_log("ACPI: synchronization operand of the wrong type\n");
		return EINVAL;
	}

	/* Succeeded: the caller shares the object. */
	drv_acpi_object_ref(object);
	*result = object;
	return 0;
}

/* Acquires an AML mutex, waiting up to the timeout in milliseconds. */
static int
mutex_acquire(
	struct drv_acpi_object *mutex,
	uint64_t timeout,
	bool *timed_out)
{
	const void *self;
	uint64_t waited;

	/* The evaluating thread is the would-be owner. */
	self = drv_acpi_os_thread();
	*timed_out = false;

	/* A mutex the thread already owns is acquired again. */
	if (mutex->value.mutex.owner == self) {
		mutex->value.mutex.depth++;
		return 0;
	}

	/* Waits for the owner to release it, sleeping so that it can. */
	waited = 0;
	while (mutex->value.mutex.owner != NULL) {
		/* Gives up when the timeout has passed. */
		if (timeout != TIMEOUT_FOREVER && waited >= timeout) {
			*timed_out = true;
			return 0;
		}

		/* Sleeps, which lets the owner run. */
		drv_acpi_os_sleep(POLL_MILLISECONDS);
		waited += POLL_MILLISECONDS;
	}

	/*
	 * The thread owns the mutex now; depth counts its acquisitions so
	 * that the last Release frees it.
	 */
	mutex->value.mutex.owner = self;
	mutex->value.mutex.depth = 1;

	/* Succeeded. */
	return 0;
}

/* Releases an AML mutex the evaluating thread owns. */
static int
mutex_release(
	struct drv_acpi_object *mutex)
{
	const void *self;

	/* Refuses a release by a thread that does not own the mutex. */
	self = drv_acpi_os_thread();
	if (mutex->value.mutex.owner != self) {
		drv_acpi_os_log("ACPI: Release of a mutex the thread does not own\n");
		return EPERM;
	}

	/* The last matching Release makes the mutex free. */
	mutex->value.mutex.depth--;
	if (mutex->value.mutex.depth == 0)
		mutex->value.mutex.owner = NULL;

	/* Succeeded. */
	return 0;
}

/* Takes one signal of an event, waiting up to the timeout in milliseconds. */
static int
event_wait(
	struct drv_acpi_object *event,
	uint64_t timeout,
	bool *timed_out)
{
	uint64_t waited;

	/* Waits for a signal, sleeping so that another evaluation can give one. */
	*timed_out = false;
	waited = 0;
	while (event->value.event.pending == 0) {
		/* Gives up when the timeout has passed. */
		if (timeout != TIMEOUT_FOREVER && waited >= timeout) {
			*timed_out = true;
			return 0;
		}

		/* Sleeps, which lets a signaler run. */
		drv_acpi_os_sleep(POLL_MILLISECONDS);
		waited += POLL_MILLISECONDS;
	}

	/* Takes the signal. */
	event->value.event.pending--;

	/* Succeeded. */
	return 0;
}
