/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws113-p013: exercises the kernel's backlight class (src/drivers/generic/backlight.c)
 * with the real character-device registry (src/kern/cdev.c) on the host.
 *
 * The fixture supplies the allocator, the locks, the user copies and a
 * provider that records what it is asked; a file is a struct file whose
 * data is the device's, as cdev_open_file() makes it.  It does not run devfs
 * (the /dev/backlight directory is checked in the guest).
 */

#include <kern/backlight.h>
#include <kern/cdev.h>
#include <kern/file.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/uaccess.h>
#include <uapi/backlight.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A provider that answers with what the test chose and records what it was asked. */
struct test_provider {
	uint32_t percent;
	int error;
	unsigned gets;
	unsigned sets;
	uint32_t last_set;
};

static int provider_get(void *context, uint32_t *percent);
static int provider_set(void *context, uint32_t percent);
static void check(int condition, const char *what);
static int request(struct cdev *device, int mode, unsigned long code, void *argument);
static void test_registration(void);
static void test_requests(void);
static void test_withdrawal(void);

/* The provider's operations, and a table without a set operation. */
static const struct kern_backlight_ops provider_ops = { provider_get, provider_set };
static const struct kern_backlight_ops broken_ops = { provider_get, NULL };

/* The allocations still held, and the failures seen. */
static unsigned allocations;
static unsigned failures;

/* Runs every scenario. */
int
main(
	void)
{
	/* The scenarios. */
	test_registration();
	test_requests();
	test_withdrawal();

	/* Every record went with its device. */
	check(allocations == 0U, "every allocation was freed");

	/* The verdict. */
	if (failures != 0U) {
		printf("host-backlight: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-backlight: PASS\n");
	return 0;
}

/* Registration: the arguments, the numbers, the names and the device numbers. */
static void
test_registration(
	void)
{
	struct test_provider provider;
	struct kern_backlight *backlights[KERN_BACKLIGHT_MAX + 1U];
	struct kern_backlight *extra;
	struct cdev *device;
	unsigned index;
	int error;

	/* Refuses what is not a provider. */
	memset(&provider, 0, sizeof(provider));
	error = kern_backlight_register(NULL, BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, &extra);
	check(error == EINVAL, "a provider without a name is refused");
	error = kern_backlight_register("i915", 7U, &provider_ops, &provider, &extra);
	check(error == EINVAL, "an unknown type is refused");
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &broken_ops, &provider, &extra);
	check(error == EINVAL, "a provider without set is refused");
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, NULL);
	check(error == EINVAL, "a registration without a result is refused");

	/* Fills every number: backlight0 to backlight3, the major 0x000d. */
	for (index = 0U; index < KERN_BACKLIGHT_MAX; index++) {
		error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, &backlights[index]);
		check(error == 0, "a provider is registered");
	}

	device = cdev_find_ref("backlight0");
	check(device != NULL && device->rdev == 0x000d0000U, "backlight0 is published at 0x000d0000");
	if (device != NULL)
		cdev_release(device);
	device = cdev_find_ref("backlight3");
	check(device != NULL && device->rdev == 0x000d0003U, "backlight3 is published at 0x000d0003");
	if (device != NULL)
		cdev_release(device);

	/* One more is refused. */
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, &backlights[KERN_BACKLIGHT_MAX]);
	check(error == ENOSPC, "a fifth provider is refused with ENOSPC");

	/* A withdrawn number is given again, the lowest first. */
	kern_backlight_unregister(backlights[1]);
	device = cdev_find_ref("backlight1");
	check(device == NULL, "a withdrawn backlight1 is unpublished");
	if (device != NULL)
		cdev_release(device);
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_KEYBOARD, &provider_ops, &provider, &backlights[1]);
	device = cdev_find_ref("backlight1");
	check(error == 0 && device != NULL, "the lowest free number is given again");
	if (device != NULL)
		cdev_release(device);

	/* All withdrawn. */
	for (index = 0U; index < KERN_BACKLIGHT_MAX; index++)
		kern_backlight_unregister(backlights[index]);
	check(cdev_count() == 0U, "every device is unpublished");
	kern_backlight_unregister(NULL);
}

/* The requests: GETSTATUS, UPDATESTATUS, GETINFO and an unknown one. */
static void
test_requests(
	void)
{
	struct test_provider provider;
	struct kern_backlight *backlight;
	struct backlight_props props;
	struct backlight_info info;
	struct cdev *device;
	int error;

	/* A panel at 60 %. */
	memset(&provider, 0, sizeof(provider));
	provider.percent = 60U;
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, &backlight);
	check(error == 0, "the panel is registered");
	device = cdev_find_ref("backlight0");
	check(device != NULL, "backlight0 is found");
	if (device == NULL)
		return;

	/* GETSTATUS reads the provider. */
	memset(&props, 0xff, sizeof(props));
	error = request(device, O_RDONLY, BACKLIGHTGETSTATUS, &props);
	check(error == 0 && props.brightness == 60U && props.nlevels == 0U, "GETSTATUS reports 60 and no levels");

	/* A provider's answer above 100 is the highest light. */
	provider.percent = 150U;
	error = request(device, O_RDONLY, BACKLIGHTGETSTATUS, &props);
	check(error == 0 && props.brightness == 100U, "an answer above 100 is reported as 100");

	/* UPDATESTATUS needs a writer and a percentage. */
	memset(&props, 0, sizeof(props));
	props.brightness = 40U;
	error = request(device, O_RDONLY, BACKLIGHTUPDATESTATUS, &props);
	check(error == EBADF && provider.sets == 0U, "UPDATESTATUS on a read-only open is EBADF");
	props.brightness = 101U;
	error = request(device, O_RDWR, BACKLIGHTUPDATESTATUS, &props);
	check(error == EINVAL && provider.sets == 0U, "a brightness above 100 is EINVAL");
	props.brightness = 40U;
	error = request(device, O_WRONLY, BACKLIGHTUPDATESTATUS, &props);
	check(error == 0 && provider.sets == 1U && provider.last_set == 40U, "UPDATESTATUS sets 40");
	props.brightness = 0U;
	error = request(device, O_RDWR, BACKLIGHTUPDATESTATUS, &props);
	check(error == 0 && provider.last_set == 0U, "UPDATESTATUS sets 0 (the lowest light)");

	/* The provider's refusal comes through. */
	provider.error = EBUSY;
	error = request(device, O_RDWR, BACKLIGHTGETSTATUS, &props);
	check(error == EBUSY, "the provider's EBUSY reaches GETSTATUS");
	error = request(device, O_RDWR, BACKLIGHTUPDATESTATUS, &props);
	check(error == EBUSY, "the provider's EBUSY reaches UPDATESTATUS");
	provider.error = 0;

	/* GETINFO names the provider. */
	memset(&info, 0xff, sizeof(info));
	error = request(device, O_RDONLY, BACKLIGHTGETINFO, &info);
	check(error == 0 && strcmp(info.name, "i915") == 0 && info.type == BACKLIGHT_TYPE_PANEL, "GETINFO reports i915, panel");

	/* An unknown request. */
	error = request(device, O_RDONLY, FIONBIO, &props);
	check(error == ENOTTY, "an unknown request is ENOTTY");

	/* Withdrawn. */
	cdev_release(device);
	kern_backlight_unregister(backlight);
}

/* The withdrawal with a file still open: ENXIO, and the record goes with the last reference. */
static void
test_withdrawal(
	void)
{
	struct test_provider provider;
	struct kern_backlight *backlight;
	struct backlight_props props;
	struct backlight_info info;
	struct cdev *device;
	unsigned held;
	int error;

	/* A panel, opened (the open's reference is the found one). */
	memset(&provider, 0, sizeof(provider));
	held = allocations;
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &provider_ops, &provider, &backlight);
	check(error == 0, "the panel is registered again");
	device = cdev_find_ref("backlight0");
	if (device == NULL) {
		check(0, "backlight0 is found again");
		return;
	}

	/* Withdrawn under the open file: nothing reaches the provider. */
	kern_backlight_unregister(backlight);
	memset(&props, 0, sizeof(props));
	error = request(device, O_RDWR, BACKLIGHTGETSTATUS, &props);
	check(error == ENXIO, "GETSTATUS after the withdrawal is ENXIO");
	error = request(device, O_RDWR, BACKLIGHTUPDATESTATUS, &props);
	check(error == ENXIO, "UPDATESTATUS after the withdrawal is ENXIO");
	error = request(device, O_RDWR, BACKLIGHTGETINFO, &info);
	check(error == ENXIO, "GETINFO after the withdrawal is ENXIO");
	check(provider.gets == 0U && provider.sets == 0U, "the withdrawn provider was not called");
	check(allocations > held, "the record lives while the file is open");

	/* The last reference frees the record. */
	cdev_release(device);
	check(allocations == held, "the record goes with the last reference");
}

/* Answers the provider's get with the chosen brightness or error. */
static int
provider_get(
	void *context,
	uint32_t *percent)
{
	struct test_provider *provider;

	/* The chosen answer. */
	provider = context;
	provider->gets++;
	if (provider->error != 0)
		return provider->error;

	/* Succeeded. */
	*percent = provider->percent;
	return 0;
}

/* Records the provider's set. */
static int
provider_set(
	void *context,
	uint32_t percent)
{
	struct test_provider *provider;

	/* The chosen answer. */
	provider = context;
	if (provider->error != 0)
		return provider->error;

	/* Succeeded: recorded. */
	provider->sets++;
	provider->last_set = percent;
	return 0;
}

/* Sends a request through the device's operation, as on a file opened with mode. */
static int
request(
	struct cdev *device,
	int mode,
	unsigned long code,
	void *argument)
{
	struct file file;
	int error;

	/* A file on the device, opened with mode. */
	memset(&file, 0, sizeof(file));
	file.f_data = device->data;
	__atomic_store_n(&file.f_flags.value, (unsigned)mode, __ATOMIC_RELEASE);

	/* The request. */
	error = device->ops->ioctl(&file, code, (uintptr_t)argument);
	return error;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* Each check is reported. */
	if (condition) {
		printf("ok: %s\n", what);
	} else {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Supplies a counted kernel allocation. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *allocation;

	/* Counted until kern_free. */
	allocation = calloc(count, size);
	if (allocation != NULL)
		allocations++;
	return allocation;
}

/* Supplies a counted kernel allocation without zeroing. */
void *
kern_malloc(
	size_t size)
{
	/* The same accounting. */
	return kern_calloc(1U, size);
}

/* Frees a counted kernel allocation. */
void
kern_free(
	void *allocation)
{
	/* A missing allocation is harmless. */
	if (allocation == NULL)
		return;
	allocations--;
	free(allocation);
}

/* Initializes a host spinlock. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	/* An unowned lock. */
	memset(lock, 0, sizeof(*lock));
	lock->rank = rank;
	lock->name = name;
}

/* Takes a host spinlock (one thread: never contended). */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	unsigned busy;

	/* A recursive acquisition is a defect. */
	busy = __atomic_exchange_n(&lock->held.value, 1U, __ATOMIC_ACQUIRE);
	if (busy != 0U) {
		printf("FAIL: a spinlock was taken twice (%s)\n", lock->name);
		exit(1);
	}
	return 1;
}

/* Releases a host spinlock. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	/* Unowned again. */
	(void)enabled;
	__atomic_store_n(&lock->held.value, 0U, __ATOMIC_RELEASE);
}

/* Initializes a host mutex. */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	/* An unowned mutex. */
	memset(mutex, 0, sizeof(*mutex));
	(void)rank;
	(void)name;
	return 0;
}

/* Takes a host mutex (one thread: never contended). */
void
mutex_lock(
	struct mutex *mutex)
{
	/* A recursive acquisition is a defect. */
	if (mutex->locked != 0U) {
		printf("FAIL: a mutex was taken twice\n");
		exit(1);
	}
	mutex->locked = 1U;
}

/* Releases a host mutex. */
void
mutex_unlock(
	struct mutex *mutex)
{
	/* Unowned again. */
	mutex->locked = 0U;
}

/* Copies a request in from the test's memory. */
int
copyin(
	uintptr_t source,
	void *destination,
	size_t size)
{
	/* A missing address faults. */
	if (source == 0U)
		return EFAULT;
	memcpy(destination, (const void *)source, size);
	return 0;
}

/* Copies an answer out to the test's memory. */
int
copyout(
	const void *source,
	uintptr_t destination,
	size_t size)
{
	/* A missing address faults. */
	if (destination == 0U)
		return EFAULT;
	memcpy((void *)destination, source, size);
	return 0;
}

/* Prints a kernel log line. */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* On standard output. */
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
}
