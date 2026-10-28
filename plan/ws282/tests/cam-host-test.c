/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the production camera framework (cam.c) on the host.
 *
 * The fixture supplies the allocator, the locks, the wait queue, the user
 * copies, the cdev registry, the physical pages and the device mappings.  A
 * fake source plays the camera: it offers YUYV and MJPEG at 640x480, refuses
 * anything wider with ENOSPC, and counts its starts and stops.  The test
 * produces frames through the slot API as a source's worker would.  A sleep
 * runs a hook in place of another thread; one case uses a real thread to
 * hold an ioctl open across the retirement.
 */

#include <drivers/camera/cam.h>
#include <kern/cdev.h>
#include <kern/clock.h>
#include <kern/file.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/page.h>
#include <kern/pmem.h>
#include <kern/poll.h>
#include <kern/uaccess.h>
#include <kern/vm-device.h>
#include <kern/waitq.h>
#include <uapi/camera.h>
#include <uapi/poll.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The mode the fake source offers for both formats. */
#define FAKE_WIDTH	640U
#define FAKE_HEIGHT	480U

/* The largest payload the fake host takes per interval. */
#define FAKE_HOST_PAYLOAD	1024U

/* The fake source's two frame intervals, 30 and 15 frames per second. */
#define FAKE_INTERVAL_30	333333U
#define FAKE_INTERVAL_15	666666U

/* How many polls a threaded sleep makes before it declares a hang. */
#define THREADED_SLEEP_POLLS	1000000U

/*
 * The host C library's threads.  The test links against glibc, whose
 * pthread_t is an unsigned long; the tree's own pthread.h describes the
 * zedBSD library instead, so it is not included.
 */
typedef unsigned long host_thread_t;
extern int pthread_create(host_thread_t *thread, const void *attributes, void *(*start)(void *), void *argument);
extern int pthread_join(host_thread_t thread, void **result);

/* One registered cdev generation. */
struct fake_node {
	struct cdev cdev;
	unsigned refs;
	unsigned published;
};

/* What a sleep does in place of another CPU. */
enum sleep_mode {
	SLEEP_REFUSE,
	SLEEP_HOOK,
	SLEEP_SIGNAL,
	SLEEP_THREADED
};

/* The state of the fake camera. */
struct fake_source {
	unsigned starts;
	unsigned stops;
	int start_error;
	unsigned set_modes;
	unsigned enum_block;
	volatile unsigned enum_inside;
	volatile unsigned enum_release;
	volatile unsigned enum_returned;
};

/*
 * The fixture's accounting.
 *
 * Each counter is checked back to zero at the end, so a leak of memory,
 * pages or mappings fails the test.
 */
static struct fake_node *nodes[16];
static unsigned node_count;
static __thread unsigned held_spinlocks;
static unsigned poll_notifications;
static unsigned live_allocations;
static unsigned live_pmem;
static unsigned live_mappings;
static uint64_t ticks;
static enum sleep_mode sleep_mode;
static void (*sleep_hook)(void);
static volatile unsigned sleeps;

/* The fake camera and the device the framework made of it. */
static struct fake_source source;
static struct drv_cam_device *camera;

/* ---------------------------------------------------------------- */
/* Kernel services.                                                 */

/* Lets another thread run while this one waits for it. */
static void
relax(
	void)
{
	__asm__ __volatile__("" ::: "memory");
}

/* Reports a failed assertion of the production code. */
void
__libc_assert_fail(
	const char *expression,
	const char *file,
	int line)
{
	fprintf(stderr, "ASSERT %s:%d: %s\n", file, line, expression);
	abort();
}

/* Allocates zeroed kernel memory, counting it. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *memory;

	memory = calloc(count, size);
	if (memory != NULL)
		live_allocations++;

	return memory;
}

/* Frees kernel memory, counting it. */
void
kern_free(
	void *memory)
{
	if (memory == NULL)
		return;

	assert(live_allocations != 0U);
	live_allocations--;
	free(memory);
}

/* Fills memory. */
void *
kern_memset(
	void *destination,
	int value,
	size_t count)
{
	return memset(destination, value, count);
}

/* Formats a string. */
int
kern_snprintf(
	char *buffer,
	size_t size,
	const char *format,
	...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);

	return length;
}

/* Discards a kernel log line. */
void
kern_logf(
	const char *format,
	...)
{
	(void)format;
}

/* Reports the monotonic clock in ticks of one millisecond. */
int
kern_clock_gettime(
	clockid_t clock,
	struct timespec *result)
{
	(void)clock;
	result->tv_sec = (time_t)(ticks / 1000U);
	result->tv_nsec = (long)((ticks % 1000U) * 1000000U);

	return 0;
}

/* Prepares a spin lock. */
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

/* Takes a spin lock, spinning while another thread holds it. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* Only one spin lock is ever held at a time by one thread. */
	assert(held_spinlocks == 0U);
	while (__atomic_exchange_n(&lock->held.value, 1U, __ATOMIC_ACQUIRE) != 0U)
		relax();
	held_spinlocks++;

	return 1;
}

/* Releases a spin lock. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	assert(enabled == 1);
	assert(held_spinlocks != 0U);
	held_spinlocks--;
	__atomic_store_n(&lock->held.value, 0U, __ATOMIC_RELEASE);
}

/* Prepares a mutex. */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	memset(mutex, 0, sizeof(*mutex));
	(void)rank;
	(void)name;

	return 0;
}

/* Takes a mutex; no case ever contends for one. */
void
mutex_lock(
	struct mutex *mutex)
{
	assert(held_spinlocks == 0U);
	assert(mutex->locked == 0);
	mutex->locked = 1;
}

/* Releases a mutex. */
void
mutex_unlock(
	struct mutex *mutex)
{
	assert(mutex->locked == 1);
	mutex->locked = 0;
}

/* Prepares a wait queue. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	memset(queue, 0, sizeof(*queue));
	queue->name = name;
}

/* Reports the wake generation of a wait queue. */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	return __atomic_load_n(&queue->sequence, __ATOMIC_ACQUIRE);
}

/* Wakes every sleeper, which only moves the wake generation here. */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	__atomic_add_fetch(&queue->sequence, 1U, __ATOMIC_ACQ_REL);
}

/*
 * Sleeps by letting the rest of the world run, as another CPU would.
 *
 * SLEEP_HOOK runs the one-shot hook; SLEEP_THREADED waits for another thread
 * to wake the queue; SLEEP_SIGNAL reports a signal; SLEEP_REFUSE fails the
 * test.  Each sleep moves time on by one tick, so a deadline is reached.
 */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	void (*hook)(void);
	unsigned polls;

	/* A wake that came first is not a sleep. */
	if (waitq_sequence(queue) != observed)
		return EAGAIN;

	/* A deadline that has passed is not a sleep. */
	if (deadline != 0U && ticks >= deadline)
		return ETIMEDOUT;

	/* The sleep releases the condition lock, as the real one does. */
	sleeps++;
	assert(held_spinlocks == 1U);
	spin_unlock_irqrestore(lock, 1);

	/* Lets the world run. */
	switch (sleep_mode) {
	case SLEEP_HOOK:
		hook = sleep_hook;
		sleep_hook = NULL;
		if (hook != NULL)
			hook();
		break;
	case SLEEP_SIGNAL:
		assert((flags & WAITQ_INTERRUPTIBLE) != 0U);
		(void)spin_lock_irqsave(lock);
		return EINTR;
	case SLEEP_THREADED:
		for (polls = 0U; polls < THREADED_SLEEP_POLLS; polls++) {
			if (waitq_sequence(queue) != observed)
				break;
			relax();
		}
		assert(polls < THREADED_SLEEP_POLLS);
		break;
	case SLEEP_REFUSE:
	default:
		fprintf(stderr, "unexpected sleep\n");
		abort();
	}

	/* Takes the condition lock back, and moves time on. */
	(void)spin_lock_irqsave(lock);
	ticks++;
	if (deadline != 0U && ticks >= deadline)
		return ETIMEDOUT;

	return 0;
}

/* Reports the scheduler's tick count. */
uint64_t
sched_ticks(
	void)
{
	return ticks;
}

/* Counts a poll notification, which must be made with no spin lock held. */
void
poll_notify(
	void)
{
	assert(held_spinlocks == 0U);
	poll_notifications++;
}

/* Copies from the "user" address, which is a host address here. */
int
copyin(
	uintptr_t source_address,
	void *destination,
	size_t size)
{
	if (source_address == 0U)
		return EFAULT;

	memcpy(destination, (const void *)source_address, size);

	return 0;
}

/* Copies to the "user" address, which is a host address here. */
int
copyout(
	const void *source_address,
	uintptr_t destination,
	size_t size)
{
	if (destination == 0U)
		return EFAULT;

	memcpy((void *)destination, source_address, size);

	return 0;
}

/*
 * Allocates physical pages; the "physical" address is the host address.
 *
 * The pages come back filled with 0x5a, so a buffer the framework did not
 * clear shows it.
 */
int
kern_pmem_alloc(
	size_t size,
	size_t alignment,
	struct kern_pmem *run)
{
	void *memory;

	assert(held_spinlocks == 0U);
	memory = aligned_alloc(alignment, size);
	if (memory == NULL)
		return ENOMEM;

	memset(memory, 0x5a, size);
	run->paddr = (hal_physaddr_t)(uintptr_t)memory;
	run->size = size;
	live_pmem++;

	return 0;
}

/* Frees physical pages. */
int
kern_pmem_free(
	struct kern_pmem *run)
{
	assert(live_pmem != 0U);
	live_pmem--;
	free((void *)(uintptr_t)run->paddr);
	run->paddr = 0;

	return 0;
}

/* Maps physical pages into the kernel, which is the identity here. */
void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	return (void *)(uintptr_t)address;
}

/* Records a device mapping; the test drops it as munmap would. */
int
vm_device_create(
	struct file *file,
	uint64_t physical,
	void *address,
	size_t bytes,
	uint32_t attributes,
	uint32_t max_prot,
	void (*release)(void *),
	void *owner,
	struct vm_device_mapping **result)
{
	struct vm_device_mapping *mapping;

	assert(file != NULL);
	assert(release != NULL);
	assert(physical == (uintptr_t)address);
	assert(attributes == 0U);
	assert(max_prot == KERN_PROT_READ);
	mapping = calloc(1, sizeof(*mapping));
	assert(mapping != NULL);
	mapping->file = file;
	mapping->physical = physical;
	mapping->address = address;
	mapping->bytes = bytes;
	mapping->max_prot = max_prot;
	mapping->release = release;
	mapping->owner = owner;
	live_mappings++;
	*result = mapping;

	return 0;
}

/* Drops a device mapping, as munmap would. */
static void
mapping_drop(
	struct vm_device_mapping *mapping)
{
	assert(live_mappings != 0U);
	live_mappings--;
	mapping->release(mapping->owner);
	free(mapping);
}

/* ---------------------------------------------------------------- */
/* cdev registry: the registry and the caller each hold a reference. */

/* Drops one reference of a node, finalizing it with the last. */
static void
node_put(
	struct fake_node *node)
{
	assert(node->refs != 0U);
	node->refs--;
	if (node->refs == 0U) {
		node->cdev.finalizer(node->cdev.data);
		free(node);
	}
}

/* Publishes a character device. */
int
cdev_register_managed(
	const char *name,
	dev_t rdev,
	const struct cdev_ops *ops,
	void *data,
	cdev_finalizer_t finalizer,
	struct cdev **result)
{
	struct fake_node *node;
	unsigned index;

	for (index = 0U; index < node_count; index++) {
		if (nodes[index] != NULL &&
		    nodes[index]->published &&
		    strcmp(nodes[index]->cdev.name, name) == 0)
			return EEXIST;
	}

	node = calloc(1, sizeof(*node));
	assert(node != NULL);
	snprintf(node->cdev.name, sizeof(node->cdev.name), "%s", name);
	node->cdev.rdev = rdev;
	node->cdev.ops = ops;
	node->cdev.data = data;
	node->cdev.finalizer = finalizer;
	node->refs = 2U;
	node->published = 1U;
	assert(node_count < sizeof(nodes) / sizeof(nodes[0]));
	nodes[node_count++] = node;
	*result = &node->cdev;

	return 0;
}

/* Withdraws a character device. */
int
cdev_unregister(
	struct cdev *device)
{
	unsigned index;

	for (index = 0U; index < node_count; index++) {
		if (nodes[index] != NULL && &nodes[index]->cdev == device) {
			assert(nodes[index]->published);
			nodes[index]->published = 0U;
			nodes[index] = NULL;
			node_put((struct fake_node *)device);
			return 0;
		}
	}

	abort();
}

/* Drops the caller's reference of a character device. */
void
cdev_release(
	struct cdev *device)
{
	node_put((struct fake_node *)device);
}

/* Finds a published node by name. */
static struct fake_node *
node_find(
	const char *name)
{
	unsigned index;

	for (index = 0U; index < node_count; index++) {
		if (nodes[index] != NULL &&
		    nodes[index]->published &&
		    strcmp(nodes[index]->cdev.name, name) == 0)
			return nodes[index];
	}

	return NULL;
}

/* ---------------------------------------------------------------- */
/* The fake source.                                                 */

/* Lists YUYV, then MJPEG. */
static int
fake_enum_format(
	void *argument,
	unsigned index,
	struct cam_format_desc *desc)
{
	struct fake_source *fake = argument;

	/* One case holds this call open across the retirement. */
	if (fake->enum_block) {
		fake->enum_inside = 1U;
		while (!fake->enum_release)
			relax();
		fake->enum_returned = 1U;
	}

	if (index == 0U) {
		desc->format = CAM_FORMAT_YUYV;
		desc->flags = CAM_FORMAT_SUPPORTED;
		desc->bits_per_pixel = 16U;
		desc->mode_count = 1U;
		return 0;
	}
	if (index == 1U) {
		desc->format = CAM_FORMAT_MJPEG;
		desc->flags = CAM_FORMAT_SUPPORTED | CAM_FORMAT_COMPRESSED;
		desc->mode_count = 1U;
		return 0;
	}

	return ENOENT;
}

/* Lists the one 640x480 mode of each format. */
static int
fake_enum_mode(
	void *argument,
	uint32_t format,
	unsigned index,
	struct cam_mode_desc *desc)
{
	(void)argument;
	if (index != 0U)
		return ENOENT;
	if (format != CAM_FORMAT_YUYV && format != CAM_FORMAT_MJPEG)
		return EINVAL;

	desc->width = FAKE_WIDTH;
	desc->height = FAKE_HEIGHT;
	desc->interval_type = CAM_INTERVAL_DISCRETE;
	desc->interval_count = 2U;
	desc->intervals[0] = FAKE_INTERVAL_30;
	desc->intervals[1] = FAKE_INTERVAL_15;

	return 0;
}

/* Lists the two intervals of the mode. */
static int
fake_enum_interval(
	void *argument,
	uint32_t format,
	unsigned mode,
	unsigned index,
	uint32_t *interval_100ns)
{
	(void)argument;
	(void)format;
	if (mode != 0U)
		return ENOENT;
	if (index == 0U) {
		*interval_100ns = FAKE_INTERVAL_30;
		return 0;
	}
	if (index == 1U) {
		*interval_100ns = FAKE_INTERVAL_15;
		return 0;
	}

	return ENOENT;
}

/* Answers a mode request; anything wider than 640 does not fit the host. */
static int
fake_try_mode(
	void *argument,
	const struct drv_cam_mode_request *request,
	struct drv_cam_mode_result *result)
{
	(void)argument;
	if (request->format != CAM_FORMAT_YUYV &&
	    request->format != CAM_FORMAT_MJPEG)
		return EINVAL;

	result->format = request->format;
	result->width = request->width;
	result->height = request->height;
	result->interval_100ns = FAKE_INTERVAL_30;
	result->mode_index = 0U;
	if (request->format == CAM_FORMAT_YUYV) {
		result->stride_bytes = request->width * 2U;
		result->image_bytes = request->width * request->height * 2U;
	} else {
		result->stride_bytes = 0U;
		result->image_bytes = request->width * request->height;
	}
	result->payload_bytes = request->width * 4U;

	/* The host cannot carry the payload of a wider mode. */
	if (request->width > FAKE_WIDTH)
		return ENOSPC;

	return 0;
}

/* Sets a mode, counting it. */
static int
fake_set_mode(
	void *argument,
	const struct drv_cam_mode_request *request,
	struct drv_cam_mode_result *result)
{
	struct fake_source *fake = argument;
	int error;

	error = fake_try_mode(argument, request, result);
	if (error == 0)
		fake->set_modes++;

	return error;
}

/* Starts streaming, or fails as the case asks. */
static int
fake_start(
	void *argument)
{
	struct fake_source *fake = argument;

	assert(held_spinlocks == 0U);
	if (fake->start_error != 0)
		return fake->start_error;

	fake->starts++;

	return 0;
}

/* Stops streaming, counting it. */
static void
fake_stop(
	void *argument)
{
	struct fake_source *fake = argument;

	assert(held_spinlocks == 0U);
	fake->stops++;
}

/* Reports the source's own counters. */
static int
fake_read_counters(
	void *argument,
	struct cam_counters *counters)
{
	(void)argument;
	counters->missed_packets = 7U;

	return 0;
}

/* Reports the payload the host can carry. */
static uint32_t
fake_host_payload_max(
	void *argument)
{
	(void)argument;

	return FAKE_HOST_PAYLOAD;
}

/* The fake source's function table. */
static const struct drv_cam_source_ops fake_ops = {
	.enum_format = fake_enum_format,
	.enum_mode = fake_enum_mode,
	.enum_interval = fake_enum_interval,
	.try_mode = fake_try_mode,
	.set_mode = fake_set_mode,
	.start = fake_start,
	.stop = fake_stop,
	.read_counters = fake_read_counters,
	.host_payload_max = fake_host_payload_max
};

/* ---------------------------------------------------------------- */
/* Helpers.                                                         */

/* Registers the fake camera as cam0. */
static void
camera_register(
	void)
{
	struct drv_cam_device_desc desc;
	int error;

	memset(&source, 0, sizeof(source));
	memset(&desc, 0, sizeof(desc));
	desc.driver_name = "fakecam";
	snprintf(desc.device_name, sizeof(desc.device_name), "Fake Camera");
	snprintf(desc.location, sizeof(desc.location), "usb-1.2");
	desc.source = &fake_ops;
	desc.source_argument = &source;
	desc.flags = DRV_CAM_SOURCE_CPU_WRITES;
	desc.capabilities = CAM_CAP_YUV | CAM_CAP_COMPRESSED;
	desc.max_buffers = 4U;
	desc.clock_hz = 48000000U;
	error = drv_cam_register(&desc, &camera);
	assert(error == 0);
}

/* Opens /dev/cam0 as a new file. */
static struct file *
camera_open(
	void)
{
	struct fake_node *node;
	struct file *file;
	int error;

	node = node_find("cam0");
	assert(node != NULL);
	file = calloc(1, sizeof(*file));
	assert(file != NULL);
	file->f_data = node->cdev.data;
	node->refs++;
	error = node->cdev.ops->open(file);
	assert(error == 0);

	return file;
}

/* Closes a file of /dev/cam0; the node may already be withdrawn. */
static void
camera_close(
	struct fake_node *node,
	struct file *file)
{
	int error;

	error = node->cdev.ops->close(file);
	assert(error == 0);
	node_put(node);
	free(file);
}

/* Issues an ioctl on a file of /dev/cam0. */
static int
camera_ioctl(
	struct fake_node *node,
	struct file *file,
	unsigned long command,
	void *argument)
{
	return node->cdev.ops->ioctl(file, command, (uintptr_t)argument);
}

/* Fills a format record. */
static void
format_fill(
	struct cam_format *format,
	uint32_t pixel_format,
	uint32_t width,
	uint32_t height)
{
	memset(format, 0, sizeof(*format));
	format->version = CAM_ABI_VERSION;
	format->size = sizeof(*format);
	format->format = pixel_format;
	format->width = width;
	format->height = height;
	format->interval_100ns = FAKE_INTERVAL_30;
}

/* Fills a buffer record. */
static void
buffer_fill(
	struct cam_buffer *buffer,
	uint32_t index)
{
	memset(buffer, 0, sizeof(*buffer));
	buffer->version = CAM_ABI_VERSION;
	buffer->size = sizeof(*buffer);
	buffer->index = index;
}

/* Asks for a number of buffers. */
static int
buffers_request(
	struct fake_node *node,
	struct file *file,
	uint32_t count,
	struct cam_request_buffers *request)
{
	memset(request, 0, sizeof(*request));
	request->version = CAM_ABI_VERSION;
	request->size = sizeof(*request);
	request->count = count;
	request->memory = CAM_MEMORY_DEVICE;

	return camera_ioctl(node, file, CAM_REQUEST_BUFFERS, request);
}

/* Queues one buffer. */
static int
buffer_queue(
	struct fake_node *node,
	struct file *file,
	uint32_t index)
{
	struct cam_buffer buffer;

	buffer_fill(&buffer, index);

	return camera_ioctl(node, file, CAM_QUEUE_BUFFER, &buffer);
}

/* Produces one frame as a source's worker would; reports the take. */
static int
frame_produce(
	size_t used_bytes,
	uint32_t pts)
{
	struct drv_cam_frame_info info;
	struct drv_cam_slot *slot;
	uint32_t sequence;
	uint8_t *bytes;
	int error;

	sequence = drv_cam_frame_begin(camera);
	error = drv_cam_slot_take(camera, sequence, &slot);
	if (error != 0) {
		drv_cam_frame_dropped(camera, CAM_DROP_NO_BUFFER);
		return error;
	}

	bytes = drv_cam_slot_address(slot);
	assert(bytes != NULL);
	if (used_bytes != 0U && used_bytes <= drv_cam_slot_capacity(slot))
		memset(bytes, 0xc3, used_bytes);
	memset(&info, 0, sizeof(info));
	info.flags = CAM_METADATA_PTS;
	info.device_pts = pts;
	drv_cam_slot_done(camera, slot, used_bytes, &info);

	return 0;
}

/* Reads the framework's counters. */
static void
counters_read(
	struct fake_node *node,
	struct file *file,
	struct cam_counters *counters)
{
	int error;

	memset(counters, 0, sizeof(*counters));
	counters->version = CAM_ABI_VERSION;
	counters->size = sizeof(*counters);
	error = camera_ioctl(node, file, CAM_GET_COUNTERS, counters);
	assert(error == 0);
}

/* Polls a file and reports the events. */
static short
camera_poll(
	struct fake_node *node,
	struct file *file)
{
	short revents;
	int error;

	revents = 0;
	error = node->cdev.ops->poll(file, POLLIN, &revents);
	assert(error == 0);

	return revents;
}

/* ---------------------------------------------------------------- */
/* Cases.                                                           */

/* The hook that produces one frame while a dequeue sleeps. */
static void
hook_produce_frame(
	void)
{
	int error;

	error = frame_produce(100U, 9U);
	assert(error == 0);
}

/* The open file whose STREAM_OFF a sleeping dequeue sees. */
static struct file *hook_file;

/* The node of that file. */
static struct fake_node *hook_node;

/* The hook that stops the stream while a dequeue sleeps. */
static void
hook_stream_off(
	void)
{
	int error;

	error = camera_ioctl(hook_node, hook_file, CAM_STREAM_OFF, NULL);
	assert(error == 0);
}

/* The hook that faults the source while a dequeue sleeps. */
static void
hook_fault(
	void)
{
	drv_cam_source_fault(camera, EIO);
}

/* Checks the info, the enumerations and the record checks. */
static void
case_info_and_enumeration(
	struct fake_node *node,
	struct file *file)
{
	struct cam_interval_desc interval;
	struct cam_format_desc format;
	struct cam_mode_desc mode;
	struct cam_info info;
	int error;

	/* The node is cam0 at the first camera number. */
	assert(node->cdev.rdev == (dev_t)0x00170000U);

	/* GET_INFO describes the source. */
	memset(&info, 0, sizeof(info));
	info.version = CAM_ABI_VERSION;
	info.size = sizeof(info);
	error = camera_ioctl(node, file, CAM_GET_INFO, &info);
	assert(error == 0);
	assert(info.version == CAM_ABI_VERSION);
	assert(info.size == sizeof(info));
	assert(strcmp(info.driver, "fakecam") == 0);
	assert(strcmp(info.name, "Fake Camera") == 0);
	assert(strcmp(info.location, "usb-1.2") == 0);
	assert(info.format_count == 2U);
	assert(info.max_buffers == 4U);
	assert(info.host_payload_max == FAKE_HOST_PAYLOAD);
	assert(info.reserved[0] == 0U);

	/* A record of another version or size is refused. */
	info.version = CAM_ABI_VERSION + 1U;
	error = camera_ioctl(node, file, CAM_GET_INFO, &info);
	assert(error == EINVAL);
	info.version = CAM_ABI_VERSION;
	info.size = sizeof(info) - 4U;
	error = camera_ioctl(node, file, CAM_GET_INFO, &info);
	assert(error == EINVAL);

	/* ENUM_FORMAT lists YUYV and MJPEG and ends. */
	memset(&format, 0, sizeof(format));
	format.version = CAM_ABI_VERSION;
	format.size = sizeof(format);
	format.index = 1U;
	error = camera_ioctl(node, file, CAM_ENUM_FORMAT, &format);
	assert(error == 0);
	assert(format.format == CAM_FORMAT_MJPEG);
	assert(format.index == 1U);
	format.index = 2U;
	error = camera_ioctl(node, file, CAM_ENUM_FORMAT, &format);
	assert(error == ENOENT);

	/* ENUM_MODE lists the one mode. */
	memset(&mode, 0, sizeof(mode));
	mode.version = CAM_ABI_VERSION;
	mode.size = sizeof(mode);
	mode.format = CAM_FORMAT_YUYV;
	error = camera_ioctl(node, file, CAM_ENUM_MODE, &mode);
	assert(error == 0);
	assert(mode.width == FAKE_WIDTH);
	assert(mode.height == FAKE_HEIGHT);
	assert(mode.interval_count == 2U);

	/* ENUM_INTERVAL lists the two intervals of the mode and ends. */
	memset(&interval, 0, sizeof(interval));
	interval.version = CAM_ABI_VERSION;
	interval.size = sizeof(interval);
	interval.format = CAM_FORMAT_YUYV;
	interval.index = 1U;
	error = camera_ioctl(node, file, CAM_ENUM_INTERVAL, &interval);
	assert(error == 0);
	assert(interval.interval_100ns == FAKE_INTERVAL_15);
	interval.index = 2U;
	error = camera_ioctl(node, file, CAM_ENUM_INTERVAL, &interval);
	assert(error == ENOENT);

	/* An unknown command is not a camera command. */
	error = camera_ioctl(node, file, 0x12345678UL, &info);
	assert(error == ENOTTY);
	printf("info and enumeration: PASS\n");
}

/* Checks the ownership of the format and the buffers between two opens. */
static void
case_ownership(
	struct fake_node *node,
	struct file *owner,
	struct file *other)
{
	struct cam_request_buffers request;
	struct cam_format format;
	int error;

	/* GET_FORMAT before any SET_FORMAT is invalid. */
	format_fill(&format, CAM_FORMAT_YUYV, 0U, 0U);
	error = camera_ioctl(node, owner, CAM_GET_FORMAT, &format);
	assert(error == EINVAL);

	/* A mode the host cannot carry is ENOSPC, with the payload it needs. */
	format_fill(&format, CAM_FORMAT_YUYV, 1280U, 720U);
	error = camera_ioctl(node, owner, CAM_SET_FORMAT, &format);
	assert(error == ENOSPC);
	assert(format.payload_bytes == 1280U * 4U);

	/* The first SET_FORMAT makes its open the owner. */
	format_fill(&format, CAM_FORMAT_YUYV, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, owner, CAM_SET_FORMAT, &format);
	assert(error == 0);
	assert(format.image_bytes == FAKE_WIDTH * FAKE_HEIGHT * 2U);
	format_fill(&format, CAM_FORMAT_YUYV, 0U, 0U);
	error = camera_ioctl(node, owner, CAM_GET_FORMAT, &format);
	assert(error == 0);
	assert(format.width == FAKE_WIDTH);

	/* Another open cannot set the format or ask for buffers. */
	format_fill(&format, CAM_FORMAT_MJPEG, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, other, CAM_SET_FORMAT, &format);
	assert(error == EBUSY);
	error = buffers_request(node, other, 2U, &request);
	assert(error == EBUSY);

	/* Another open may still try a format while the owner has no buffers. */
	format_fill(&format, CAM_FORMAT_MJPEG, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, other, CAM_TRY_FORMAT, &format);
	assert(error == 0);

	/* Once the owner has buffers, another open's TRY_FORMAT is EBUSY. */
	error = buffers_request(node, owner, 2U, &request);
	assert(error == 0);
	format_fill(&format, CAM_FORMAT_MJPEG, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, other, CAM_TRY_FORMAT, &format);
	assert(error == EBUSY);

	/* REQUEST_BUFFERS 0 by the owner gives everything up. */
	error = buffers_request(node, owner, 0U, &request);
	assert(error == 0);
	assert(request.count == 0U);
	format_fill(&format, CAM_FORMAT_MJPEG, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, other, CAM_SET_FORMAT, &format);
	assert(error == 0);

	/* The other open gives it back. */
	error = buffers_request(node, other, 0U, &request);
	assert(error == 0);
	printf("ownership: PASS\n");
}

/* Checks buffer allocation, zeroing, the mmap offsets and the mappings. */
static void
case_buffers_and_mapping(
	struct fake_node *node,
	struct file *owner,
	struct file *other)
{
	struct vm_device_mapping *mapping;
	struct cam_request_buffers request;
	struct cam_buffer buffer;
	struct cam_format format;
	const uint8_t *bytes;
	size_t slot_bytes;
	size_t index;
	int error;

	/* REQUEST_BUFFERS needs a format first. */
	error = buffers_request(node, owner, 2U, &request);
	assert(error == EINVAL);

	/* The owner gets the buffers it asks for, capped at max_buffers. */
	format_fill(&format, CAM_FORMAT_YUYV, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, owner, CAM_SET_FORMAT, &format);
	assert(error == 0);
	error = buffers_request(node, owner, 8U, &request);
	assert(error == 0);
	assert(request.count == 4U);
	assert(request.bytes == FAKE_WIDTH * FAKE_HEIGHT * 2U);

	/* QUERY_BUFFER names each buffer's place in the mmap space. */
	slot_bytes = ((size_t)request.bytes + KERN_PAGE_SIZE - 1U) & ~((size_t)KERN_PAGE_SIZE - 1U);
	buffer_fill(&buffer, 3U);
	error = camera_ioctl(node, owner, CAM_QUERY_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.mmap_offset == 3U * slot_bytes);
	assert(buffer.bytes == request.bytes);
	assert(buffer.fd == -1);
	assert(buffer.flags == 0U);
	buffer_fill(&buffer, 4U);
	error = camera_ioctl(node, owner, CAM_QUERY_BUFFER, &buffer);
	assert(error == EINVAL);

	/* A writable, unaligned or foreign mapping is refused. */
	error = node->cdev.ops->mmap(owner, (off_t)slot_bytes, slot_bytes, KERN_PROT_READ | KERN_PROT_WRITE, &mapping);
	assert(error == EACCES);
	error = node->cdev.ops->mmap(owner, 1, slot_bytes, KERN_PROT_READ, &mapping);
	assert(error == EINVAL);
	error = node->cdev.ops->mmap(other, 0, slot_bytes, KERN_PROT_READ, &mapping);
	assert(error == EBUSY);
	error = node->cdev.ops->mmap(owner, (off_t)(4U * slot_bytes), slot_bytes, KERN_PROT_READ, &mapping);
	assert(error == EINVAL);

	/* A buffer is mapped whole and reads as zeros after allocation. */
	error = node->cdev.ops->mmap(owner, (off_t)slot_bytes, slot_bytes, KERN_PROT_READ, &mapping);
	assert(error == 0);
	bytes = mapping->address;
	for (index = 0U; index < slot_bytes; index++)
		assert(bytes[index] == 0U);
	buffer_fill(&buffer, 1U);
	error = camera_ioctl(node, owner, CAM_QUERY_BUFFER, &buffer);
	assert(error == 0);
	assert((buffer.flags & CAM_BUFFER_MAPPED) != 0U);

	/* The buffers cannot be replaced while a mapping remains. */
	error = buffers_request(node, owner, 2U, &request);
	assert(error == EBUSY);
	error = buffers_request(node, owner, 0U, &request);
	assert(error == EBUSY);

	/* After munmap they can. */
	mapping_drop(mapping);
	error = buffers_request(node, owner, 2U, &request);
	assert(error == 0);
	assert(request.count == 2U);
	printf("buffers and mapping: PASS\n");
}

/* Checks the stream: queueing, frames, exhaustion, discard and dequeue. */
static void
case_stream(
	struct fake_node *node,
	struct file *owner,
	struct file *other)
{
	struct cam_request_buffers request;
	struct cam_counters counters;
	struct drv_cam_slot *slot;
	struct cam_buffer buffer;
	uint32_t sequence;
	short revents;
	int error;

	/* STREAM_ON needs a queued buffer. */
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == EINVAL);

	/* A buffer is queued once; a second QUEUE_BUFFER of it is EBUSY. */
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = buffer_queue(node, owner, 0U);
	assert(error == EBUSY);
	error = buffer_queue(node, owner, 1U);
	assert(error == 0);
	error = buffer_queue(node, other, 1U);
	assert(error == EBUSY);

	/* A failed start leaves the buffers queued and the stream off. */
	source.start_error = EIO;
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == EIO);
	assert(source.starts == 0U);
	source.start_error = 0;

	/* STREAM_ON starts the source once; a second one is EBUSY. */
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);
	assert(source.starts == 1U);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == EBUSY);

	/* The buffers cannot be replaced while streaming. */
	error = buffers_request(node, owner, 2U, &request);
	assert(error == EBUSY);

	/* A dequeue with nothing done does not block a non-blocking open. */
	__atomic_store_n(&owner->f_flags.value, (unsigned)O_NONBLOCK, __ATOMIC_RELEASE);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == EAGAIN);
	__atomic_store_n(&owner->f_flags.value, 0U, __ATOMIC_RELEASE);

	/* A dequeue with a timeout gives up with ETIMEDOUT. */
	sleep_mode = SLEEP_HOOK;
	buffer_fill(&buffer, 0U);
	buffer.timeout_ms = 20U;
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == ETIMEDOUT);

	/* A signal interrupts a dequeue. */
	sleep_mode = SLEEP_SIGNAL;
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == EINTR);

	/* A sleeping dequeue is woken by a frame and returns it. */
	sleep_mode = SLEEP_HOOK;
	sleep_hook = hook_produce_frame;
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.index == 0U);
	assert(buffer.metadata.used_bytes == 100U);
	assert(buffer.metadata.sequence == 0U);
	assert(buffer.metadata.device_pts == 9U);
	assert((buffer.metadata.flags & CAM_METADATA_PTS) != 0U);
	assert(buffer.metadata.exposure == CAM_METADATA_UNKNOWN);
	assert(buffer.reserved[0] == 0U);
	assert(buffer.reserved[1] == 0U);
	sleep_mode = SLEEP_REFUSE;

	/*
	 * With buffer 0 dequeued only buffer 1 is free: one frame takes it,
	 * the next finds none and is dropped, which leaves a sequence gap.
	 */
	error = frame_produce(200U, 10U);
	assert(error == 0);
	error = frame_produce(200U, 11U);
	assert(error == EAGAIN);

	/* A non-owner's poll sees nothing; the owner's sees the frame. */
	revents = camera_poll(node, other);
	assert(revents == 0);
	revents = camera_poll(node, owner);
	assert((revents & POLLIN) != 0);

	/* The frame comes out; a taken and discarded slot goes back free. */
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.index == 1U);
	assert(buffer.metadata.sequence == 1U);
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	sequence = drv_cam_frame_begin(camera);
	error = drv_cam_slot_take(camera, sequence, &slot);
	assert(error == 0);
	drv_cam_slot_discard(camera, slot);
	error = frame_produce(300U, 12U);
	assert(error == 0);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.index == 0U);
	assert(buffer.metadata.sequence == 4U);

	/*
	 * Sequences 0, 1 and 4 were delivered: 2 was dropped and 3 discarded,
	 * so the gap count is 2 and one frame had no buffer.
	 */
	counters_read(node, owner, &counters);
	assert(counters.frames == 3U);
	assert(counters.dropped_no_buffer == 1U);
	assert(counters.sequence_gaps == 2U);
	assert(counters.missed_packets == 7U);
	assert(counters.version == CAM_ABI_VERSION);

	/* A sleeping dequeue sees STREAM_OFF as EINVAL. */
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	sleep_mode = SLEEP_HOOK;
	sleep_hook = hook_stream_off;
	hook_node = node;
	hook_file = owner;
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == EINVAL);
	assert(source.stops == 1U);
	sleep_mode = SLEEP_REFUSE;

	/* STREAM_OFF leaves every buffer unused. */
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_QUERY_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.flags == 0U);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == EINVAL);
	printf("stream: PASS\n");
}

/* Checks a source fault and the recovery by STREAM_OFF. */
static void
case_fault(
	struct fake_node *node,
	struct file *owner)
{
	struct cam_buffer buffer;
	short revents;
	int error;

	/* Streams with two buffers and one finished frame. */
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = buffer_queue(node, owner, 1U);
	assert(error == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);
	error = frame_produce(64U, 1U);
	assert(error == 0);

	/* The fault shows in poll, but the finished frame still comes out first. */
	drv_cam_source_fault(camera, EIO);
	revents = camera_poll(node, owner);
	assert((revents & POLLERR) != 0);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == EIO);

	/* A fault while a dequeue sleeps ends it with EIO. */
	error = camera_ioctl(node, owner, CAM_STREAM_OFF, NULL);
	assert(error == 0);
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);
	sleep_mode = SLEEP_HOOK;
	sleep_hook = hook_fault;
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == EIO);
	sleep_mode = SLEEP_REFUSE;

	/* STREAM_OFF recovers, and the stream starts again. */
	error = camera_ioctl(node, owner, CAM_STREAM_OFF, NULL);
	assert(error == 0);
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);
	revents = camera_poll(node, owner);
	assert((revents & POLLERR) == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_OFF, NULL);
	assert(error == 0);
	printf("fault: PASS\n");
}

/* Checks an MJPEG frame's used bytes, and a frame larger than its buffer. */
static void
case_mjpeg(
	struct fake_node *node,
	struct file *owner)
{
	struct cam_request_buffers request;
	struct cam_buffer buffer;
	struct cam_format format;
	int error;

	/* Switches the owner to MJPEG with two buffers. */
	error = buffers_request(node, owner, 0U, &request);
	assert(error == 0);
	format_fill(&format, CAM_FORMAT_MJPEG, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, owner, CAM_SET_FORMAT, &format);
	assert(error == 0);
	error = buffers_request(node, owner, 2U, &request);
	assert(error == 0);
	assert(request.bytes == FAKE_WIDTH * FAKE_HEIGHT);
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = buffer_queue(node, owner, 1U);
	assert(error == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);

	/* A compressed frame reports the bytes it used. */
	error = frame_produce(1234U, 1U);
	assert(error == 0);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.metadata.used_bytes == 1234U);

	/* A frame reported larger than its buffer is clipped to the buffer. */
	error = frame_produce((size_t)request.bytes + 4096U, 2U);
	assert(error == 0);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, owner, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == 0);
	assert(buffer.metadata.used_bytes == request.bytes);
	error = camera_ioctl(node, owner, CAM_STREAM_OFF, NULL);
	assert(error == 0);
	printf("mjpeg: PASS\n");
}

/*
 * Checks that the stream outlives the owner's close while a mapping remains,
 * and stops at munmap.
 */
static void
case_close_with_mapping(
	struct fake_node *node,
	struct file *owner)
{
	struct vm_device_mapping *mapping;
	unsigned stops;
	int error;

	/* Streams with a mapped buffer. */
	error = buffer_queue(node, owner, 0U);
	assert(error == 0);
	error = camera_ioctl(node, owner, CAM_STREAM_ON, NULL);
	assert(error == 0);
	error = node->cdev.ops->mmap(owner, 0, KERN_PAGE_SIZE, KERN_PROT_READ, &mapping);
	assert(error == 0);

	/* The close keeps the stream while the mapping remains. */
	stops = source.stops;
	camera_close(node, owner);
	assert(source.stops == stops);

	/* The munmap ends it. */
	mapping_drop(mapping);
	assert(source.stops == stops + 1U);
	printf("close with mapping: PASS\n");
}

/* The open file a thread holds an ioctl on during the retirement. */
static struct file *thread_file;

/* The node of that file. */
static struct fake_node *thread_node;

/* Runs an ENUM_FORMAT that the fake source holds open. */
static void *
thread_enum_format(
	void *argument)
{
	struct cam_format_desc format;
	int error;

	(void)argument;
	memset(&format, 0, sizeof(format));
	format.version = CAM_ABI_VERSION;
	format.size = sizeof(format);
	error = camera_ioctl(thread_node, thread_file, CAM_ENUM_FORMAT, &format);
	assert(error == 0);

	return NULL;
}

/* Releases the held ENUM_FORMAT once the retirement waits for it. */
static void *
thread_release(
	void *argument)
{
	(void)argument;
	while (sleeps == 0U)
		relax();
	source.enum_release = 1U;

	return NULL;
}

/* Checks the retirement: admission, one stop, and the dead node. */
static void
case_unregister(
	void)
{
	struct cam_request_buffers request;
	struct cam_format format;
	struct cam_buffer buffer;
	struct fake_node *node;
	struct file *file;
	host_thread_t holder;
	host_thread_t releaser;
	short revents;
	int error;

	/* A fresh camera streams on one open. */
	camera_register();
	node = node_find("cam0");
	assert(node != NULL);
	file = camera_open();
	format_fill(&format, CAM_FORMAT_YUYV, FAKE_WIDTH, FAKE_HEIGHT);
	error = camera_ioctl(node, file, CAM_SET_FORMAT, &format);
	assert(error == 0);
	error = buffers_request(node, file, 2U, &request);
	assert(error == 0);
	error = buffer_queue(node, file, 0U);
	assert(error == 0);
	error = camera_ioctl(node, file, CAM_STREAM_ON, NULL);
	assert(error == 0);

	/* An ioctl is held open inside the source. */
	source.enum_block = 1U;
	thread_node = node;
	thread_file = file;
	error = pthread_create(&holder, NULL, thread_enum_format, NULL);
	assert(error == 0);
	while (!source.enum_inside)
		relax();

	/* The retirement waits for it, and stops the source exactly once. */
	sleeps = 0U;
	sleep_mode = SLEEP_THREADED;
	error = pthread_create(&releaser, NULL, thread_release, NULL);
	assert(error == 0);
	drv_cam_unregister(camera);
	assert(source.enum_returned);
	assert(source.stops == 1U);
	error = pthread_join(holder, NULL);
	assert(error == 0);
	error = pthread_join(releaser, NULL);
	assert(error == 0);
	sleep_mode = SLEEP_REFUSE;

	/* The node is gone; the open file sees ENODEV and POLLERR|POLLHUP. */
	assert(node_find("cam0") == NULL);
	buffer_fill(&buffer, 0U);
	error = camera_ioctl(node, file, CAM_DEQUEUE_BUFFER, &buffer);
	assert(error == ENODEV);
	revents = camera_poll(node, file);
	assert((revents & (POLLERR | POLLHUP)) == (POLLERR | POLLHUP));

	/* The close after the retirement does not stop the source again. */
	camera_close(node, file);
	assert(source.stops == 1U);
	printf("unregister: PASS\n");
}

/* Runs every case and checks that nothing leaked. */
int
main(
	void)
{
	struct fake_node *node;
	struct file *owner;
	struct file *other;

	/* One camera, two opens. */
	sleep_mode = SLEEP_REFUSE;
	camera_register();
	node = node_find("cam0");
	assert(node != NULL);
	owner = camera_open();
	other = camera_open();

	case_info_and_enumeration(node, owner);
	case_ownership(node, owner, other);
	case_buffers_and_mapping(node, owner, other);
	case_stream(node, owner, other);
	case_fault(node, owner);
	case_mjpeg(node, owner);

	/* The other open goes first; the owner's close is part of the case. */
	camera_close(node, other);
	case_close_with_mapping(node, owner);
	drv_cam_unregister(camera);

	/* A second camera life tests the retirement. */
	case_unregister();

	/* Nothing leaked. */
	assert(live_mappings == 0U);
	assert(live_pmem == 0U);
	assert(live_allocations == 0U);
	assert(held_spinlocks == 0U);
	printf("cam-host-test: PASS (%u poll notifications)\n", poll_notifications);

	return 0;
}
