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
 * waits here poll with drv_acpi_sleep(), which lets the lock go.
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

/*
 * The bits of the FACS Global Lock (ACPI 6.5 section 5.2.10.1): the lock
 * is owned, and its other side waits for it.
 */
#define GLOBAL_LOCK_PENDING	0x1U
#define GLOBAL_LOCK_OWNED	0x2U

/*
 * The Global Lock's dword in the FACS, which firmware takes too; NULL
 * until drv_acpi_global_lock_attach(), and then \_GL_ is only an AML
 * mutex among the operating system's threads.
 */
static volatile uint32_t *hardware_lock;

static int sync_object(struct drv_acpi_eval *eval, enum drv_acpi_type type, struct drv_acpi_object **result);
static int mutex_acquire(struct drv_acpi_eval *eval, struct drv_acpi_object *mutex, uint64_t timeout, bool *timed_out);
static int mutex_release(struct drv_acpi_eval *eval, struct drv_acpi_object *mutex);
static struct drv_acpi_object *global_lock_object(void);
static bool is_hardware_lock(struct drv_acpi_object *mutex);
static bool hardware_acquire(void);
static bool hardware_release(void);
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
 * Acquires an AML mutex for a thread, waiting up to the timeout in
 * milliseconds (0xffff waits for ever).
 *
 * A thread may acquire only mutexes of its current sync level or higher,
 * and a mutex it already owns is acquired again (ACPI 6.5 section 19.6.2).
 */
int
drv_acpi_mutex_acquire(
	struct drv_acpi_thread *thread,
	struct drv_acpi_object *mutex,
	uint64_t timeout,
	bool *timed_out)
{
	struct drv_acpi_mutex *state;
	uint64_t waited;
	bool hardware;
	bool taken;

	/* Refuses a mutex below the level the thread already holds. */
	state = &mutex->value.mutex;
	*timed_out = false;
	if (state->sync_level < thread->sync_level) {
		drv_acpi_os_log(
			"ACPI: Acquire of a sync level %u mutex while holding level %u\n",
			(unsigned)state->sync_level,
			(unsigned)thread->sync_level);
		return EDEADLK;
	}

	/* A mutex the thread already owns is acquired again. */
	if (state->owner == thread) {
		state->depth++;
		return 0;
	}

	/*
	 * Waits for the owner to release it, sleeping so that it can.  The
	 * Global Lock must also be free of firmware; a try that finds
	 * firmware owning it asks firmware to signal its release.
	 */
	waited = 0;
	hardware = is_hardware_lock(mutex);
	for (;;) {
		/* Takes the mutex when no thread and no firmware owns it. */
		if (state->owner == NULL) {
			taken = true;
			if (hardware)
				taken = hardware_acquire();
			if (taken)
				break;
		}

		/* Gives up when the timeout has passed. */
		if (timeout != TIMEOUT_FOREVER && waited >= timeout) {
			*timed_out = true;
			return 0;
		}

		/* Sleeps, which lets the owner run. */
		drv_acpi_sleep(POLL_MILLISECONDS);
		waited += POLL_MILLISECONDS;
	}

	/*
	 * The thread owns the mutex now and holds it above its older ones;
	 * the list keeps a reference so that the mutex outlives its node.
	 * The thread's level becomes the mutex's until the last Release.
	 */
	state->owner = thread;
	state->depth = 1;
	state->original_sync_level = thread->sync_level;
	state->next_held = thread->held;
	drv_acpi_object_ref(mutex);
	thread->held = mutex;
	thread->sync_level = state->sync_level;

	/* Succeeded. */
	return 0;
}

/*
 * Releases an AML mutex a thread owns.
 *
 * Mutexes are released in the reverse order of their sync levels: the one
 * released must be at the thread's current level.
 */
int
drv_acpi_mutex_release(
	struct drv_acpi_thread *thread,
	struct drv_acpi_object *mutex)
{
	struct drv_acpi_mutex *state;
	struct drv_acpi_object **link;
	bool hardware;
	bool waiting;

	/* Refuses a release by a thread that does not own the mutex. */
	state = &mutex->value.mutex;
	if (state->owner != thread) {
		drv_acpi_os_log("ACPI: Release of a mutex the thread does not own\n");
		return EPERM;
	}

	/* Refuses a release out of the order of the sync levels. */
	if (state->sync_level != thread->sync_level) {
		drv_acpi_os_log("ACPI: Release of a sync level %u mutex at level %u\n",
				(unsigned)state->sync_level,
				(unsigned)thread->sync_level);
		return EDEADLK;
	}

	/* Only the last matching Release frees it. */
	state->depth--;
	if (state->depth != 0)
		return 0;

	/* Unlinks it from the thread's list of held mutexes. */
	for (link = &thread->held; *link != NULL; link = &(*link)->value.mutex.next_held) {
		/* Stops at the link that points at this mutex. */
		if (*link == mutex)
			break;
	}

	/* Takes it out of the list when it is there. */
	if (*link == mutex)
		*link = state->next_held;

	/*
	 * The mutex is free and the thread is back at the level it had
	 * before acquiring it.
	 */
	thread->sync_level = state->original_sync_level;
	state->owner = NULL;
	state->next_held = NULL;

	/* Gives the Global Lock back to firmware, telling it when it waits. */
	hardware = is_hardware_lock(mutex);
	if (hardware) {
		waiting = hardware_release();
		if (waiting)
			drv_acpi_events_global_release();
	}

	/* The list's reference goes. */
	drv_acpi_object_release(mutex);

	/* Succeeded. */
	return 0;
}

/*
 * Releases every mutex a thread still holds when it leaves the
 * interpreter, newest first.
 */
void
drv_acpi_thread_end(
	struct drv_acpi_thread *thread)
{
	struct drv_acpi_object *mutex;

	/* Releases the held mutexes one at a time, whatever their depth. */
	while (thread->held != NULL) {
		mutex = thread->held;
		drv_acpi_os_log("ACPI: a mutex was still held when the evaluation ended\n");
		mutex->value.mutex.depth = 1;
		thread->sync_level = mutex->value.mutex.sync_level;
		drv_acpi_mutex_release(thread, mutex);
	}
}

/*
 * Acquires or releases the ACPI Global Lock (\_GL_) for a field whose
 * lock rule asks for it.
 */
int
drv_acpi_global_lock(
	struct drv_acpi_eval *eval,
	bool acquire)
{
	struct drv_acpi_thread *thread;
	struct drv_acpi_object *lock;
	bool timed_out;
	int error;

	/* Finds the global lock's mutex; a namespace without one needs no locking. */
	lock = global_lock_object();
	if (lock == NULL)
		return 0;
	thread = drv_acpi_eval_thread(eval);

	/* Releases it. */
	if (!acquire) {
		error = drv_acpi_mutex_release(thread, lock);
		return error;
	}

	/* Acquires it, waiting as long as it takes. */
	error = drv_acpi_mutex_acquire(thread, lock, TIMEOUT_FOREVER, &timed_out);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Starts taking the FACS Global Lock with \_GL_, which firmware shares.
 *
 * The word is the lock's dword in the FACS, mapped for the life of the
 * system.  It is attached after drv_acpi_events_init(), which enables the
 * event firmware raises when it lets the lock go and knows where to
 * signal firmware that the operating system let it go.
 */
int
drv_acpi_global_lock_attach(
	volatile uint32_t *word)
{
	/* Refuses a missing lock. */
	if (word == NULL)
		return EINVAL;

	/* Uses it from the next Acquire of \_GL_ on. */
	hardware_lock = word;

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
		error = mutex_acquire(eval, object, timeout, &timed_out);
		break;
	case DRV_ACPI_OP_RELEASE:
		error = mutex_release(eval, object);
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

/* Acquires an AML mutex for the operator, waiting up to the timeout. */
static int
mutex_acquire(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *mutex,
	uint64_t timeout,
	bool *timed_out)
{
	struct drv_acpi_thread *thread;
	int error;

	/* The evaluating thread becomes the owner. */
	thread = drv_acpi_eval_thread(eval);
	error = drv_acpi_mutex_acquire(thread, mutex, timeout, timed_out);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Releases an AML mutex for the operator. */
static int
mutex_release(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *mutex)
{
	struct drv_acpi_thread *thread;
	int error;

	/* Only the owning thread can release it. */
	thread = drv_acpi_eval_thread(eval);
	error = drv_acpi_mutex_release(thread, mutex);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Finds the mutex of the global lock, \\_GL_. */
static struct drv_acpi_object *
global_lock_object(void)
{
	struct drv_acpi_node *node;
	int error;

	/* Looks the predefined name up. */
	error = drv_acpi_lookup(NULL, "\\_GL_", &node);
	if (error != 0)
		return NULL;

	/* Only a mutex can be the lock. */
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_MUTEX)
		return NULL;

	/* Reports the mutex. */
	return node->object;
}

/* Reports whether a mutex is \_GL_ backed by the FACS Global Lock. */
static bool
is_hardware_lock(
	struct drv_acpi_object *mutex)
{
	/* Only an attached lock is shared with firmware. */
	if (hardware_lock == NULL)
		return false;

	/* The mutex must be the one \_GL_ names. */
	return mutex == global_lock_object();
}

/*
 * Tries to take the FACS Global Lock (ACPI 6.5 section 5.2.10.1).  When
 * firmware owns it, the try marks it pending instead, and firmware raises
 * the Global Lock event when it lets it go; the caller tries again.
 */
static bool
hardware_acquire(void)
{
	uint32_t old;
	uint32_t new;
	bool exchanged;

	/* Sets owned, and pending when it was owned already, in one exchange. */
	old = *hardware_lock;
	for (;;) {
		/* Computes the new value from the one read; a failed exchange rereads it. */
		new = (old & ~GLOBAL_LOCK_PENDING) | GLOBAL_LOCK_OWNED;
		if ((old & GLOBAL_LOCK_OWNED) != 0)
			new |= GLOBAL_LOCK_PENDING;
		exchanged = __atomic_compare_exchange_n(hardware_lock, &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		if (exchanged)
			break;
	}

	/* It is taken when the new value is not pending. */
	return (new & GLOBAL_LOCK_PENDING) == 0;
}

/*
 * Lets the FACS Global Lock go and reports whether firmware was waiting
 * for it, in which case the caller signals firmware with GBL_RLS.
 */
static bool
hardware_release(void)
{
	uint32_t old;
	uint32_t new;
	bool exchanged;

	/* Clears owned and pending in one exchange. */
	old = *hardware_lock;
	for (;;) {
		/* Computes the new value from the one read; a failed exchange rereads it. */
		new = old & ~(GLOBAL_LOCK_PENDING | GLOBAL_LOCK_OWNED);
		exchanged = __atomic_compare_exchange_n(hardware_lock, &old, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
		if (exchanged)
			break;
	}

	/* Reports whether firmware waits. */
	return (old & GLOBAL_LOCK_PENDING) != 0;
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
		drv_acpi_sleep(POLL_MILLISECONDS);
		waited += POLL_MILLISECONDS;
	}

	/* Takes the signal. */
	event->value.event.pending--;

	/* Succeeded. */
	return 0;
}
