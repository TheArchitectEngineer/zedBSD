/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Deterministic scheduler/IRQ collaborators; no host worker thread is emulated. */
struct drv_gpu_completion {
	unsigned calls;
	int error;
};

/* Holds the single installed handler; the deterministic peer has no concurrent host thread. */
static drv_pci_irq_handler_t fixture_irq;
/* Retains the transport cookie until interrupt disestablishment. */
static void *fixture_irq_argument;
/* Represents a running guest caller when the fixture enables scheduler waits. */
static struct thread fixture_thread;
/* Provides an identity for worker creation and join without executing its body. */
static struct thread fixture_worker;
/* Selects scheduler-ready submission instead of the early-boot polling path. */
static unsigned fixture_runtime;
/* Counts terminal device errors reported after queue ownership is released. */
static unsigned fixture_reports;
/* Selects the one PCI delivery mechanism exercised by the current serialized scenario. */
static enum drv_pci_irq_type fixture_irq_type;
/* Counts global readiness wakes and rejects delivery while the transport lock is held. */
static unsigned fixture_poll_wakes;
/* Tracks the next available descriptor chain consumed by the independent peer. */
static uint16_t fixture_peer_available;

/* Allocates the same zeroed ownership records as the kernel collaborator. */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	void *allocation;

	/* Leaves allocation failure visible to the production caller. */
	allocation = calloc(count, bytes);
	if (allocation == NULL)
		return NULL;

	/* Succeeded: the caller owns one zero-initialized record. */
	return allocation;
}

/* Releases one host allocation owned by the code under test. */
void
kern_free(
	void *pointer)
{
	free(pointer);
}

/* Initializes explicit lock ownership so recursive acquisition becomes a test failure. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	memset(lock, 0, sizeof(*lock));
	lock->rank = rank;
	lock->name = name;
}

/* Excludes recursive access in this deterministic single-threaded peer. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	assert(lock->owner_valid == 0U);
	lock->owner_valid = 1U;

	/* Succeeded: synthetic interrupt state needs no restoration bits. */
	return 0U;
}

/* Releases queue ownership before callbacks are allowed to run. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long flags)
{
	(void)flags;
	assert(lock->owner_valid == 1U);
	lock->owner_valid = 0U;
}

/* Initializes the ordinary mutex ownership checked by the peer. */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	memset(mutex, 0, sizeof(*mutex));
	(void)rank;
	(void)name;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Detects a blocking submission that retains the controller mutex recursively. */
void
mutex_lock(
	struct mutex *mutex)
{
	assert(mutex->locked == 0U);
	mutex->locked = 1U;
}

/* Ends the controller operation before another simulated request can enter. */
void
mutex_unlock(
	struct mutex *mutex)
{
	assert(mutex->locked == 1U);
	mutex->locked = 0U;
}

/* Starts a wake-generation counter for the production waiter protocol. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	memset(queue, 0, sizeof(*queue));
	queue->name = name;
}

/* Samples the wake generation used to avoid a lost completion. */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	return queue->sequence;
}

/* Publishes a wake generation to every simulated observer. */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	queue->sequence++;
}

/* Delivers the registered real interrupt handler while the waiter has released its lock. */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	int handled;

	/* Delivery occurs only after the caller releases queue ownership. */
	(void)queue;
	(void)observed;
	(void)deadline;
	(void)flags;
	spin_unlock_irqrestore(lock, 0U);
	assert(fixture_irq != NULL);

	/* The real handler retires the peer-completed descriptor chain. */
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1);

	/* Return with the same lock ownership required by the wait queue API. */
	(void)spin_lock_irqsave(lock);

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Selects the production runtime or early-boot waiting branch. */
struct thread *
thread_current(
	void)
{
	if (fixture_runtime != 0U)
		return &fixture_thread;

	/* No running thread means that early-boot completion must poll. */
	return NULL;
}

/* Converts the deterministic millisecond clock to scheduler ticks. */
uint64_t
sched_ticks(
	void)
{
	return fixture_clock * KERN_CLOCK_HZ / 1000U;
}

/* Advances deterministic time to the requested scheduler deadline. */
void
sched_sleep(
	uint64_t deadline)
{
	fixture_clock = deadline * 1000U / KERN_CLOCK_HZ;
}

/* Checks watchdog creation arguments without executing an emulated worker. */
int
kthread_create(
	void (*entry)(void *),
	void *argument,
	int priority,
	struct thread **result)
{
	assert(entry == venus_worker);
	assert(argument == fixture_transport);
	(void)priority;
	*result = &fixture_worker;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Checks that only the worker created for this transport is started. */
void
thread_start(
	struct thread *thread)
{
	assert(thread == &fixture_worker);
}

/* Checks the identity retired by transport shutdown. */
int
thread_wait(
	struct thread *thread,
	int *status)
{
	assert(thread == &fixture_worker);
	(void)status;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Refuses execution of the watchdog body in this collaborator-only fixture. */
void
thread_exit(
	int status)
{
	(void)status;
	abort();
}

/* Supplies the selected synthetic interrupt through the existing PCI contract. */
int
drv_pci_device_allocate_irqs(
	struct drv_pci_device *device,
	unsigned flags,
	unsigned minimum,
	unsigned maximum,
	struct drv_pci_irq *irq,
	unsigned *count)
{
	(void)device;
	assert(flags == (DRV_PCI_IRQ_ALLOW_MSIX | DRV_PCI_IRQ_ALLOW_INTX));
	assert(minimum == 1U && maximum == 1U);
	memset(irq, 0, sizeof(*irq));
	irq->type = fixture_irq_type;
	irq->vector = 41U;
	*count = 1U;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Retains the real transport interrupt callback for deterministic delivery. */
int
drv_pci_device_establish_irq(
	struct drv_pci_device *device,
	const struct drv_pci_irq *irq,
	drv_pci_irq_handler_t handler,
	void *argument,
	const char *name,
	void **cookie)
{
	(void)device;
	(void)name;
	assert(irq->vector == 41U);
	assert(fixture_irq == NULL);
	fixture_irq = handler;
	fixture_irq_argument = argument;
	*cookie = argument;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Removes the callback before interrupt storage can be freed. */
int
drv_pci_device_disestablish_irq_checked(
	struct drv_pci_device *device,
	void *cookie)
{
	(void)device;
	assert(cookie == fixture_irq_argument);
	fixture_irq = NULL;
	fixture_irq_argument = NULL;

	/* Succeeded: the collaborator accepted the production operation. */
	return 0;
}

/* Checks that teardown cannot free a still-established interrupt. */
void
drv_pci_device_free_irqs(
	struct drv_pci_device *device,
	struct drv_pci_irq *irq,
	unsigned count)
{
	(void)device;
	(void)irq;
	assert(count == 1U);
	assert(fixture_irq == NULL);
}

/* Checks exactly-once completion and verifies that callbacks run outside the queue lock. */
void
drv_gpu_complete(
	struct drv_gpu_completion *completion,
	int error)
{
	assert(fixture_transport->queue_lock.owner_valid == 0U);
	assert(completion->calls == 0U);
	completion->calls++;
	completion->error = error;
}

/* Counts terminal failures exposed to the common GPU subsystem. */
void
drv_gpu_report_error(
	struct drv_gpu_device *device,
	int error)
{
	(void)device;
	assert(error != 0);
	fixture_reports++;
}

/*
 * Observes readiness publication after the transport releases interrupt ownership.
 */
void
poll_notify(
	void)
{
	/* A real poll observer may re-enter the driver and must not inherit its lock. */
	assert(fixture_transport->queue_lock.owner_valid == 0U);
	fixture_poll_wakes++;

	/* Succeeded: the current notification is visible to the fixture observer. */
	return;
}
