/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the ACPI power devices (ws132-p002).
 *
 * The unchanged driver (src/drivers/acpi/acpi-power.c, compiled
 * freestanding) runs against a stand-in namespace of a laptop: a lid
 * (PNP0C0D, _LID), an AC adapter (ACPI0003, _PSR), a battery (PNP0C0A, _STA
 * 0x1f, _BIX and _BST), a second battery whose _STA says it is absent, a
 * control-method power button (PNP0C0C), a PCI root (PNP0A08) and a node
 * that is not a device.  The test checks:
 *   - attach takes the lid, the AC adapter, the present battery and the
 *     button, installs their notifies and starts the thread;
 *   - attach enables at runtime the GPE the lid's and the button's _PRW
 *     name (BUG-253), and no other;
 *   - KERN_SYSTEM_GET_POWER's state (lid open, AC online, 50 %, charging);
 *   - a Notify 0x80 on the lid or the AC adapter that changes their state
 *     posts one CHANGE with the new value; one that changes nothing posts
 *     nothing;
 *   - a Notify 0x81 on the battery posts its new percent;
 *   - the button's Notify 0x80 posts a POWER PRESS at once, from the
 *     handler; other Notify values are ignored;
 *   - the battery is read again after BATTERY_PERIOD_MS without a notify;
 *   - an _BIX that fails falls back to _BIF.
 * The thread's endless loop is driven from its sleeps, one script step a
 * sleep, and left with a longjmp after the last.
 *
 *   plan/ws132/tests/run-host-acpi-power.sh
 */

#include <drivers/acpi/acpi.h>
#include <uapi/system.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The EISA identifiers of the PNP0Cxx devices, and of the PCI root. */
#define EISA_LID		0x0d0cd041U
#define EISA_BATTERY		0x0a0cd041U
#define EISA_POWER_BUTTON	0x0c0cd041U
#define EISA_PCI_ROOT		0x080ad041U

/* The GPE the lid's and the button's _PRW name (the Latitude 5330's, BUG-253). */
#define WAKE_GPE		0x18U

/* The most GPEs enabled at runtime the test keeps. */
#define GPES_MAX		8U

/* The most elements of a package, and of events kept. */
#define ELEMENTS_MAX		20U
#define EVENTS_MAX		32U

/* The nodes of the stand-in namespace. */
enum node_index {
	NODE_LID,
	NODE_AC,
	NODE_BATTERY,
	NODE_ABSENT,
	NODE_BUTTON,
	NODE_PCI,
	NODE_SCOPE,
	NODE_COUNT
};

/* An ACPI object as the stand-ins make it: an integer, a string, or a package of integers. */
struct drv_acpi_object {
	enum drv_acpi_type type;
	uint64_t integer;
	char string[16];
	unsigned count;
	struct drv_acpi_object *elements[ELEMENTS_MAX];
};

/* One node: only its address is handed out, and its notify handler is kept. */
struct stand_in_node {
	int unused;
	drv_acpi_notify_handler_t handler;
	void *argument;
};

/* One event the driver posted. */
struct posted_event {
	uint32_t class_bit;
	uint32_t action;
	int32_t value;
	char subject[32];
	char detail[64];
};

struct thread;
struct spinlock;
struct wait_queue;

void kern_system_event_post(uint32_t class_bit, uint32_t action, int32_t value, const char *subject, const char *detail);
int kern_snprintf(char *buffer, size_t size, const char *format, ...);
int kern_strcmp(const char *left, const char *right);
void kern_logf(const char *format, ...);
uint64_t sched_ticks(void);
int kthread_create(void (*entry)(void *), void *argument, int priority, struct thread **result);
void thread_start(struct thread *thread);
void spin_init(struct spinlock *lock, int rank, const char *name);
unsigned long spin_lock_irqsave(struct spinlock *lock);
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled);
void waitq_init(struct wait_queue *queue, const char *name);
uint64_t waitq_sequence(const struct wait_queue *queue);
int waitq_sleep(struct wait_queue *queue, struct spinlock *lock, uint64_t observed, uint64_t deadline, unsigned flags);
void waitq_wake_all(struct wait_queue *queue);
int drv_acpi_gpe_runtime_enable(unsigned gpe);
static void check(int condition, const char *what);
static struct drv_acpi_object *integer_new(uint64_t value);
static struct drv_acpi_object *package_new(const uint64_t *values, unsigned count);
static void notify(enum node_index index, uint32_t value);
static void script_step(uint64_t deadline);

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* The namespace and the state its methods report. */
static struct stand_in_node nodes[NODE_COUNT];
static uint64_t lid_value = 1;
static uint64_t psr_value = 1;
static uint64_t battery_state = 0x02;
static uint64_t battery_remaining = 2500;
static uint64_t battery_full = 5000;
static int bix_fails;
static int bif_asked;

/* The GPEs the driver enabled at runtime, in order. */
static unsigned runtime_gpes[GPES_MAX];
static unsigned runtime_gpe_count;

/* The events posted, the lock's depth, and the clock in ticks. */
static struct posted_event events[EVENTS_MAX];
static unsigned event_count;
static int lock_depth;
static uint64_t now_ticks = 1000;

/* The thread the driver made, where its loop is left, and the script's step. */
static void (*thread_entry)(void *);
static int thread_started;
static jmp_buf thread_exit;
static unsigned step;

void
kern_system_event_post(
	uint32_t class_bit,
	uint32_t action,
	int32_t value,
	const char *subject,
	const char *detail)
{
	struct posted_event *event;

	/* Kept, as many as fit; posting is done with the driver's lock free. */
	check(lock_depth == 0, "posts are made with the power lock free");
	if (event_count == EVENTS_MAX)
		return;
	event = &events[event_count];
	event->class_bit = class_bit;
	event->action = action;
	event->value = value;
	snprintf(event->subject, sizeof(event->subject), "%s", subject);
	snprintf(event->detail, sizeof(event->detail), "%s", detail);
	event_count++;
}

int
kern_snprintf(
	char *buffer,
	size_t size,
	const char *format,
	...)
{
	va_list arguments;
	int length;

	/* The host C library. */
	va_start(arguments, format);
	length = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return length;
}

int
kern_strcmp(
	const char *left,
	const char *right)
{
	/* The host C library. */
	return strcmp(left, right);
}

void
kern_logf(
	const char *format,
	...)
{
	/* The log is not checked. */
	(void)format;
}

uint64_t
sched_ticks(void)
{
	/* The test's clock. */
	return now_ticks;
}

int
kthread_create(
	void (*entry)(void *),
	void *argument,
	int priority,
	struct thread **result)
{
	/* Recorded; the test runs the thread itself. */
	(void)argument;
	(void)priority;
	thread_entry = entry;
	*result = (struct thread *)&thread_entry;
	return 0;
}

void
thread_start(
	struct thread *thread)
{
	/* The thread the driver made. */
	check(thread == (struct thread *)&thread_entry, "the thread started is the one made");
	thread_started++;
}

void
spin_init(
	struct spinlock *lock,
	int rank,
	const char *name)
{
	/* Nothing to make. */
	(void)lock;
	(void)rank;
	(void)name;
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* One level. */
	(void)lock;
	check(lock_depth == 0, "the power lock is not taken twice");
	lock_depth++;
	return 1;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	/* Back to none. */
	(void)lock;
	(void)enabled;
	check(lock_depth == 1, "the power lock is held at its release");
	lock_depth--;
}

void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* Nothing to make. */
	(void)queue;
	(void)name;
}

uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Any value. */
	(void)queue;
	return 0;
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* The thread sleeps holding the lock; the script's step runs with it free. */
	(void)queue;
	(void)lock;
	(void)observed;
	(void)flags;
	check(lock_depth == 1, "the thread sleeps holding the lock");
	lock_depth--;
	script_step(deadline);
	lock_depth++;
	return 0;
}

void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* Nothing to wake: the thread runs from the script. */
	(void)queue;
}

int
drv_acpi_walk(
	struct drv_acpi_node *scope,
	drv_acpi_walk_visitor_t visitor,
	void *argument)
{
	unsigned index;
	int stop;

	/* Every node, from the root. */
	check(scope == NULL, "the walk is from the root");
	for (index = 0; index < NODE_COUNT; index++) {
		stop = visitor((struct drv_acpi_node *)&nodes[index], 2U, argument);
		if (stop < 0)
			return stop;
	}

	/* Walked every node. */
	return 0;
}

enum drv_acpi_type
drv_acpi_node_type(
	const struct drv_acpi_node *node)
{
	/* The scope is not a device. */
	if (node == (const struct drv_acpi_node *)&nodes[NODE_SCOPE])
		return DRV_ACPI_TYPE_SCOPE;
	return DRV_ACPI_TYPE_DEVICE;
}

int
drv_acpi_notify_install(
	struct drv_acpi_node *node,
	drv_acpi_notify_handler_t handler,
	void *argument)
{
	struct stand_in_node *stand_in;

	/* Kept for the script's notifies. */
	stand_in = (struct stand_in_node *)node;
	check(stand_in->handler == NULL, "one notify handler a node");
	stand_in->handler = handler;
	stand_in->argument = argument;
	return 0;
}

int
drv_acpi_gpe_runtime_enable(
	unsigned gpe)
{
	/* Keeps the GPE for the checks. */
	check(runtime_gpe_count < GPES_MAX, "room for the GPE enabled");
	if (runtime_gpe_count < GPES_MAX)
		runtime_gpes[runtime_gpe_count++] = gpe;
	return 0;
}

int
drv_acpi_evaluate(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_object **arguments,
	unsigned argument_count,
	struct drv_acpi_object **result)
{
	uint64_t values[ELEMENTS_MAX];
	struct stand_in_node *node;
	int battery;
	int is_hid;
	int is_bix;
	int is_bif;
	int is_bst;
	int is_prw;

	/* No method here takes arguments. */
	(void)arguments;
	check(argument_count == 0U, "no arguments");
	node = (struct stand_in_node *)scope;

	/* Which method of which node. */
	battery = node == &nodes[NODE_BATTERY];
	is_hid = strcmp(path, "_HID") == 0;
	is_bix = battery && strcmp(path, "_BIX") == 0;
	is_bif = battery && strcmp(path, "_BIF") == 0;
	is_bst = battery && strcmp(path, "_BST") == 0;
	is_prw = strcmp(path, "_PRW") == 0;

	/* _HID of each device. */
	if (is_hid) {
		if (node == &nodes[NODE_LID]) {
			*result = integer_new(EISA_LID);
		} else if (node == &nodes[NODE_AC]) {
			*result = integer_new(0);
			(*result)->type = DRV_ACPI_TYPE_STRING;
			strcpy((*result)->string, "ACPI0003");
		} else if (node == &nodes[NODE_BATTERY] || node == &nodes[NODE_ABSENT]) {
			*result = integer_new(EISA_BATTERY);
		} else if (node == &nodes[NODE_BUTTON]) {
			*result = integer_new(EISA_POWER_BUTTON);
		} else {
			*result = integer_new(EISA_PCI_ROOT);
		}

		/* The identifier. */
		return 0;
	}

	/* The lid's and the button's wake event: the GPE and S3; the others have none. */
	if (is_prw) {
		if (node != &nodes[NODE_LID] && node != &nodes[NODE_BUTTON])
			return 6;
		values[0] = WAKE_GPE;
		values[1] = 3;
		*result = package_new(values, 2U);
		return 0;
	}

	/* The battery's information: _BIX (revision, unit, design, last full, ...) or _BIF (unit, design, last full, ...). */
	memset(values, 0, sizeof(values));
	if (is_bix) {
		if (bix_fails)
			return 6;
		values[2] = 6000;
		values[3] = battery_full;
		*result = package_new(values, 20U);
		return 0;
	}

	/* The older _BIF. */
	if (is_bif) {
		bif_asked++;
		values[1] = 6000;
		values[2] = battery_full;
		*result = package_new(values, 13U);
		return 0;
	}

	/* The battery's status: state, rate, remaining, voltage. */
	if (is_bst) {
		values[0] = battery_state;
		values[1] = 1000;
		values[2] = battery_remaining;
		values[3] = 12000;
		*result = package_new(values, 4U);
		return 0;
	}

	/* Anything else is not there. */
	return 6;
}

int
drv_acpi_evaluate_integer(
	struct drv_acpi_node *scope,
	const char *path,
	uint64_t *value)
{
	struct stand_in_node *node;
	int is_sta;
	int is_lid;
	int is_psr;

	/* Which method of which node. */
	node = (struct stand_in_node *)scope;
	is_sta = strcmp(path, "_STA") == 0;
	is_lid = node == &nodes[NODE_LID] && strcmp(path, "_LID") == 0;
	is_psr = node == &nodes[NODE_AC] && strcmp(path, "_PSR") == 0;

	/* _STA: the absent battery says so; the others are present (a battery with its bit). */
	if (is_sta) {
		*value = 0x0fU;
		if (node == &nodes[NODE_BATTERY])
			*value = 0x1fU;
		if (node == &nodes[NODE_ABSENT])
			*value = 0x00U;
		return 0;
	}

	/* _LID and _PSR. */
	if (is_lid) {
		*value = lid_value;
		return 0;
	}

	/* The AC adapter. */
	if (is_psr) {
		*value = psr_value;
		return 0;
	}

	/* Anything else is not there. */
	return 6;
}

void
drv_acpi_object_release(
	struct drv_acpi_object *object)
{
	unsigned index;

	/* NULL is allowed; a package goes with its elements. */
	if (object == NULL)
		return;
	for (index = 0; index < object->count; index++)
		free(object->elements[index]);
	free(object);
}

enum drv_acpi_type
drv_acpi_object_type(
	const struct drv_acpi_object *object)
{
	/* The type it was made with. */
	return object->type;
}

uint64_t
drv_acpi_object_integer(
	const struct drv_acpi_object *object)
{
	/* The value it was made with. */
	return object->integer;
}

const char *
drv_acpi_object_string(
	const struct drv_acpi_object *object,
	size_t *length)
{
	/* The text it was made with. */
	*length = strlen(object->string);
	return object->string;
}

struct drv_acpi_object *
drv_acpi_object_package_element(
	const struct drv_acpi_object *object,
	unsigned index)
{
	/* An element, or none past the end. */
	if (index >= object->count)
		return NULL;
	return object->elements[index];
}

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Makes an integer object. */
static struct drv_acpi_object *
integer_new(
	uint64_t value)
{
	struct drv_acpi_object *object;

	/* An integer. */
	object = calloc(1, sizeof(*object));
	object->type = DRV_ACPI_TYPE_INTEGER;
	object->integer = value;
	return object;
}

/* Makes a package of integers. */
static struct drv_acpi_object *
package_new(
	const uint64_t *values,
	unsigned count)
{
	struct drv_acpi_object *object;
	unsigned index;

	/* A package with its elements. */
	object = calloc(1, sizeof(*object));
	object->type = DRV_ACPI_TYPE_PACKAGE;
	object->count = count;
	for (index = 0; index < count; index++)
		object->elements[index] = integer_new(values[index]);
	return object;
}

/* Delivers a Notify to a node's handler, as the interpreter would. */
static void
notify(
	enum node_index index,
	uint32_t value)
{
	check(nodes[index].handler != NULL, "the node has a notify handler");
	if (nodes[index].handler == NULL)
		return;
	nodes[index].handler((struct drv_acpi_node *)&nodes[index], value, nodes[index].argument);
}

/*
 * One step of the script a sleep of the thread: it checks what the thread
 * did since the last sleep, then changes the firmware's state and notifies.
 */
static void
script_step(
	uint64_t deadline)
{
	/* Every sleep has the batteries' deadline, in the future. */
	check(deadline > now_ticks, "the sleep has a deadline in the future");

	/* What the step checks and changes. */
	switch (step) {
	case 0:
		/* Nothing posted at attach; the lid closes. */
		check(event_count == 0U, "attach posts nothing");
		lid_value = 0;
		notify(NODE_LID, 0x80U);
		break;
	case 1:
		/* The lid's CHANGE; then the AC adapter is unplugged, and the lid notifies without a change. */
		check(event_count == 1U, "one event for the lid");
		check(events[0].class_bit == KERN_SYSTEM_EVENT_LID && events[0].action == KERN_SYSTEM_EVENT_CHANGE &&
		      events[0].value == 0 && strcmp(events[0].subject, "lid") == 0, "the lid closed");
		psr_value = 0;
		notify(NODE_AC, 0x80U);
		notify(NODE_LID, 0x80U);
		break;
	case 2:
		/* Only the AC adapter's CHANGE; the battery's information changes. */
		check(event_count == 2U, "one event for the AC adapter, none for the unchanged lid");
		check(events[1].class_bit == KERN_SYSTEM_EVENT_AC && events[1].value == 0 &&
		      strcmp(events[1].subject, "ac") == 0, "the AC adapter unplugged");
		battery_state = 0x01;
		battery_remaining = 2000;
		notify(NODE_BATTERY, 0x81U);
		break;
	case 3:
		/* The battery's 40 %, discharging; the button is pressed, and an unknown notify comes. */
		check(event_count == 3U, "one event for the battery");
		check(events[2].class_bit == KERN_SYSTEM_EVENT_BATTERY && events[2].value == 40 &&
		      strcmp(events[2].subject, "battery0") == 0 && strcmp(events[2].detail, "charging=0") == 0,
		      "the battery at 40 %, discharging");
		notify(NODE_BUTTON, 0x80U);
		check(event_count == 4U, "the button posts from its handler");
		check(events[3].class_bit == KERN_SYSTEM_EVENT_POWER && events[3].action == KERN_SYSTEM_EVENT_PRESS &&
		      strcmp(events[3].subject, "power-button") == 0, "the power button pressed");
		notify(NODE_BUTTON, 0x02U);
		notify(NODE_LID, 0x02U);
		check(event_count == 4U, "other notify values are ignored");
		break;
	case 4:
		/* Nothing; the battery drains without a notify, and the period passes. */
		check(event_count == 4U, "nothing more");
		battery_remaining = 1500;
		bix_fails = 1;
		now_ticks = deadline;
		break;
	case 5:
		/* The battery read again by the period, through _BIF. */
		check(event_count == 5U, "the period reads the battery");
		check(events[4].class_bit == KERN_SYSTEM_EVENT_BATTERY && events[4].value == 30, "the battery at 30 %");
		check(bif_asked != 0, "_BIF when _BIX fails");
		longjmp(thread_exit, 1);
	default:
		check(0, "no step past the script");
		longjmp(thread_exit, 1);
	}

	/* The next step. */
	step++;
}

int
main(void)
{
	struct system_power_info info;
	int error;

	/* Attach takes four devices and starts the thread. */
	error = drv_acpi_power_attach();
	check(error == 0, "attach");
	check(thread_started == 1, "the thread is started");
	check(nodes[NODE_LID].handler != NULL && nodes[NODE_AC].handler != NULL &&
	      nodes[NODE_BATTERY].handler != NULL && nodes[NODE_BUTTON].handler != NULL,
	      "the notifies of the lid, the AC adapter, the battery and the button");
	check(nodes[NODE_ABSENT].handler == NULL, "not the absent battery's");
	check(nodes[NODE_PCI].handler == NULL && nodes[NODE_SCOPE].handler == NULL, "not the other nodes'");
	check(runtime_gpe_count == 2U && runtime_gpes[0] == WAKE_GPE && runtime_gpes[1] == WAKE_GPE,
	      "the lid's and the button's GPE enabled at runtime, nothing else (BUG-253)");

	/* The state at attach. */
	memset(&info, 0, sizeof(info));
	drv_acpi_power_get(&info);
	check(info.known == (KERN_SYSTEM_POWER_HAS_LID | KERN_SYSTEM_POWER_HAS_AC | KERN_SYSTEM_POWER_HAS_BATTERY),
	      "the lid, the AC adapter and the battery are known");
	check(info.lid_open == 1 && info.ac_online == 1, "the lid open, the AC adapter plugged");
	check(info.battery_percent == 50 && info.battery_charging == 1, "the battery at 50 %, charging");

	/*
	 * The thread, driven by the script.  setjmp() may only be called in a
	 * condition (C99 7.13.1.1), so it stays in one here.
	 */
	if (setjmp(thread_exit) == 0)
		thread_entry(NULL);
	check(step == 5U, "the script ran to its end");

	/* The state after it. */
	lock_depth = 0;
	memset(&info, 0, sizeof(info));
	drv_acpi_power_get(&info);
	check(info.lid_open == 0 && info.ac_online == 0, "the lid closed, the AC adapter unplugged");
	check(info.battery_percent == 30 && info.battery_charging == 0, "the battery at 30 %, discharging");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-acpi-power: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-acpi-power: %d checks passed\n", checks);
	return 0;
}
