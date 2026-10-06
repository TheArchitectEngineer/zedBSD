/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The system extension's monitor (WS134 p012, plan/ws134/design.md section
 * 1.3; keiland/kl-system-protocol.h's kl_system_monitor_v1): the machine's
 * counters, sampled by libkeiland-backend's monitor area, for the System
 * Monitor.
 *
 * The samples are taken on a thread of their own while a monitor object
 * exists (reading the memory walks the kernel's pages, and a disk's
 * statistics may wait on a lock: the event loop never does either).  The
 * thread keeps the latest sample and the latest info under a lock; the
 * event loop takes them once a pass (kwl_sysmon_tick) and tells every
 * monitor object.
 *
 * The flow is held by the clients (design.md review 2): an object hears
 * no sample before it acked the last, and none when its client's queue
 * has no room for a whole one.  A sample is sent whole or not at all; one
 * an object missed is not owed, since the counters only grow and the next
 * covers the time.
 */

#include "kwl.h"

#include "userland/desktop/keiland/kl-system-protocol.h"
#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A parameter a function does not use. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* How often the event loop looks for the monitor objects (their number and their periods), in milliseconds. */
#define SYSMON_SCAN_MS		250U

/* How many samples the thread takes between two reads of the info (a device that came shows within them). */
#define SYSMON_INFO_EVERY	5U

/* The largest event this file sends (an info with its host, a device with its names). */
#define SYSMON_EVENT_MAX	256U

/*
 * The sampling: the thread and the lock and condition it waits on (ready
 * once made), and, under the lock, whether the thread is asked to stop and
 * whether it has ended, the period it samples at, and the latest sample
 * and info with their serials (0: none yet).
 *
 * The event loop's alone: whether the thread runs, the serials it took
 * last, its copies of what it took (the sample with its serial), when it
 * last looked for the objects, and how many there were.  One per process.
 */
struct sysmon_state {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	unsigned lock_ready;
	unsigned stop;
	unsigned ended;
	uint32_t period_ms;
	struct kl_backend_monitor_sample sample;
	uint32_t sample_serial;
	struct kl_backend_monitor_info info;
	uint32_t info_serial;
	unsigned running;
	uint32_t taken_sample;
	uint32_t taken_info;
	struct kl_backend_monitor_sample current;
	uint32_t current_serial;
	struct kl_backend_monitor_info current_info;
	uint64_t scanned_ms;
	unsigned subscribers;
};

/* The one sampling of the process, zero until the first monitor object (see struct sysmon_state). */
static struct sysmon_state sysmon_state;

static void *sysmon_run(void *argument);
static int sysmon_start(void);
static void sysmon_stop(void);
static void sysmon_scan(struct kwl_server *server);
static void sysmon_tell(struct kwl_server *server);
static void sysmon_send_info(struct kwl_object *object);
static void sysmon_send_sample(struct kwl_object *object);
static size_t sysmon_sample_bytes(const struct kl_backend_monitor_sample *sample);
static uint32_t sysmon_period(uint32_t period_ms);
static size_t sysmon_put_word(unsigned char *payload, size_t offset, uint32_t word);
static size_t sysmon_put_wide(unsigned char *payload, size_t offset, uint64_t wide);
static size_t sysmon_put_string(unsigned char *payload, size_t offset, const char *text);
static uint32_t sysmon_word(const unsigned char *bytes, size_t offset);

/*
 * Makes a monitor object for a manager's get_monitor (new id, period):
 * it hears the info at once when there is one, and the samples from then
 * on.  Returns 0, or EPROTO for a malformed request.
 */
int
kwl_sysmon_create(
	struct kwl_object *manager,
	const unsigned char *bytes,
	size_t size)
{
	struct kwl_object *created;
	uint32_t id;
	uint32_t period;

	/* The new object's ID and its period. */
	if (size != 8U)
		return EPROTO;
	id = sysmon_word(bytes, 0U);
	period = sysmon_word(bytes, 4U);

	/* The object, under the ID the client chose. */
	created = kwl_create(manager->client, id, KWL_SYSTEM_MONITOR, 1U);
	if (created == NULL)
		return EPROTO;
	created->monitor_period = sysmon_period(period);
	created->monitor_waiting = 0;
	created->monitor_info = 0;
	printf("KWL SYSTEM monitor client=%llu id=%u period=%u\n", (unsigned long long)manager->client->number, id, created->monitor_period);

	/* The info, when the thread has read one. */
	if (sysmon_state.taken_info != 0U)
		sysmon_send_info(created);

	/* The sampling starts at the next pass, at the shortest period asked. */
	sysmon_state.scanned_ms = 0;

	/* Succeeded: the object is the client's. */
	return 0;
}

/*
 * Carries out a request of a monitor object: it goes, it acks a sample,
 * or it asks for another period.  Returns 0, or EPROTO for a malformed
 * request.
 */
int
kwl_sysmon_request(
	struct kwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t serial;

	/* The object goes; the sampling stops at a later pass when it was the last. */
	if (opcode == KL_SYSTEM_MONITOR_DESTROY) {
		if (size != 0U)
			return EPROTO;
		kwl_object_destroy(object);
		sysmon_state.scanned_ms = 0;
		return 0;
	}

	/* The sample of a serial is taken: the next may come. */
	if (opcode == KL_SYSTEM_MONITOR_ACK) {
		if (size != 4U)
			return EPROTO;
		serial = sysmon_word(bytes, 0U);
		if (serial == object->monitor_waiting)
			object->monitor_waiting = 0;
		return 0;
	}

	/* Another period, within its bounds. */
	if (opcode != KL_SYSTEM_MONITOR_SET_PERIOD || size != 4U)
		return EPROTO;
	object->monitor_period = sysmon_period(sysmon_word(bytes, 0U));
	sysmon_state.scanned_ms = 0;

	/* Succeeded: the request is carried out. */
	return 0;
}

/*
 * Looks after the monitor once a pass: the objects counted now and then
 * (the thread started for the first, stopped after the last, at the
 * shortest period asked), and a new sample told to every object.
 */
void
kwl_sysmon_tick(
	struct kwl_server *server)
{
	uint64_t now;

	/* The objects, every SYSMON_SCAN_MS or after one came or went. */
	now = kwl_milliseconds();
	if (sysmon_state.scanned_ms == 0U || now >= sysmon_state.scanned_ms + SYSMON_SCAN_MS) {
		sysmon_state.scanned_ms = now;
		sysmon_scan(server);
	}

	/* A new sample, to every object. */
	if (sysmon_state.running)
		sysmon_tell(server);
}

/*
 * Stops the sampling at the compositor's end, waiting for the thread.
 */
void
kwl_sysmon_close(
	struct kwl_server *server)
{
	UNUSED_PARAMETER(server);

	/* The thread, when it runs. */
	if (!sysmon_state.running)
		return;
	sysmon_stop();
	(void)pthread_join(sysmon_state.thread, NULL);
	sysmon_state.running = 0;
}

/* The sampling thread: samples at the period until it is asked to stop, the info every few samples. */
static void *
sysmon_run(
	void *argument)
{
	struct kl_backend_monitor *monitor;
	struct kl_backend_monitor_sample *sample;
	struct kl_backend_monitor_info *info;
	struct timespec until;
	uint64_t generation;
	unsigned count;
	unsigned stop;
	uint32_t period;
	int error;
	int have_info;
	int ready;
	int changed;

	UNUSED_PARAMETER(argument);

	/* The backend's monitor and the room for a sample and an info. */
	monitor = kl_backend_monitor_open();
	sample = calloc(1, sizeof(*sample));
	info = calloc(1, sizeof(*info));
	generation = 0;
	have_info = 0;
	count = 0;

	/* Each sample until asked to stop. */
	for (;;) {
		/* The info first and every few samples; a new one when the devices changed. */
		ready = 0;
		if (monitor == NULL) {
			ready = 0;
		} else if (sample == NULL) {
			ready = 0;
		} else if (info != NULL) {
			ready = 1;
		}

		/* Read when due; published when it is the first or the devices changed. */
		if (ready && count % SYSMON_INFO_EVERY == 0U) {
			error = kl_backend_monitor_info(monitor, info);
			changed = 0;
			if (error != 0) {
				changed = 0;
			} else if (!have_info) {
				changed = 1;
			} else if (info->generation != generation) {
				changed = 1;
			}

			/* A new info for the event loop. */
			if (changed) {
				generation = info->generation;
				have_info = 1;
				pthread_mutex_lock(&sysmon_state.lock);

				/* The info and its serial, which tells the event loop it is new. */
				sysmon_state.info = *info;
				sysmon_state.info_serial++;

				/* The event loop may take it. */
				pthread_mutex_unlock(&sysmon_state.lock);
			}
		}

		/* The sample. */
		if (ready) {
			error = kl_backend_monitor_sample(monitor, sample);
			if (error == 0) {
				pthread_mutex_lock(&sysmon_state.lock);

				/* The sample and its serial, which tells the event loop it is new. */
				sysmon_state.sample = *sample;
				sysmon_state.sample_serial++;

				/* The event loop may take it. */
				pthread_mutex_unlock(&sysmon_state.lock);
			}
		}

		/* The wait until the next, or the stop. */
		count++;
		pthread_mutex_lock(&sysmon_state.lock);

		/* The deadline: the period from now, on the clock the condition waits by. */
		period = sysmon_state.period_ms;
		(void)clock_gettime(CLOCK_REALTIME, &until);
		until.tv_sec += (time_t)(period / 1000U);
		until.tv_nsec += (long)(period % 1000U) * 1000000L;
		if (until.tv_nsec >= 1000000000L) {
			until.tv_sec++;
			until.tv_nsec -= 1000000000L;
		}
		while (!sysmon_state.stop) {
			error = pthread_cond_timedwait(&sysmon_state.wake, &sysmon_state.lock, &until);
			if (error != 0)
				break;
		}

		/* Whether it was asked to stop, before the lock goes. */
		stop = sysmon_state.stop;

		/* The event loop may change the period again. */
		pthread_mutex_unlock(&sysmon_state.lock);

		/* Asked to stop. */
		if (stop)
			break;
	}

	/* The monitor and the room go; the event loop joins the thread when it sees it ended. */
	kl_backend_monitor_close(monitor);
	free(sample);
	free(info);
	pthread_mutex_lock(&sysmon_state.lock);

	/* Ended: the event loop may join it. */
	sysmon_state.ended = 1;

	/* The last the thread touches. */
	pthread_mutex_unlock(&sysmon_state.lock);

	/* Succeeded: the thread ends. */
	return NULL;
}

/* Starts the sampling thread; returns 0 or an errno value. */
static int
sysmon_start(void)
{
	int error;

	/* The lock and the condition, once. */
	if (!sysmon_state.lock_ready) {
		error = pthread_mutex_init(&sysmon_state.lock, NULL);
		if (error != 0)
			return error;
		error = pthread_cond_init(&sysmon_state.wake, NULL);
		if (error != 0) {
			(void)pthread_mutex_destroy(&sysmon_state.lock);
			return error;
		}

		/* Both are ready for good. */
		sysmon_state.lock_ready = 1;
	}

	/* Not asked to stop, not ended. */
	sysmon_state.stop = 0;
	sysmon_state.ended = 0;

	/* The thread. */
	error = pthread_create(&sysmon_state.thread, NULL, sysmon_run, NULL);
	if (error != 0)
		return error;

	/* The thread runs. */
	sysmon_state.running = 1;

	/* Succeeded: the sampling runs. */
	return 0;
}

/* Asks the sampling thread to stop (it ends after the sample it may be taking). */
static void
sysmon_stop(void)
{
	/* The stop, and the thread woken from its wait. */
	pthread_mutex_lock(&sysmon_state.lock);

	/* The flag the thread looks at after each wait, and the wait cut short. */
	sysmon_state.stop = 1;
	pthread_cond_signal(&sysmon_state.wake);

	/* The thread may see it now. */
	pthread_mutex_unlock(&sysmon_state.lock);
}

/*
 * Counts the monitor objects and their shortest period: the thread starts
 * for the first, follows the period, and is asked to stop after the last
 * (and joined once it ended, so that the event loop never waits for a
 * sample).
 */
static void
sysmon_scan(
	struct kwl_server *server)
{
	struct kwl_client *client;
	struct kwl_object *object;
	uint32_t period;
	unsigned count;
	unsigned ended;
	int error;

	/* Each live monitor object, and the shortest period. */
	count = 0;
	period = KL_SYSTEM_MONITOR_PERIOD_MAX;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (object = client->objects; object != NULL; object = object->next) {
			if (object->kind != KWL_SYSTEM_MONITOR || object->dead)
				continue;
			count++;
			if (object->monitor_period < period)
				period = object->monitor_period;
		}
	}

	/* The log says when the count changes. */
	if (count != sysmon_state.subscribers)
		printf("KWL SYSTEM monitor subscribers=%u period=%u\n", count, period);
	sysmon_state.subscribers = count;

	/* A thread asked to stop is joined once it ended. */
	if (sysmon_state.running && sysmon_state.stop) {
		pthread_mutex_lock(&sysmon_state.lock);

		/* Whether the thread has ended. */
		ended = sysmon_state.ended;

		/* The thread may still be ending. */
		pthread_mutex_unlock(&sysmon_state.lock);

		/* Still taking its last sample. */
		if (!ended)
			return;
		(void)pthread_join(sysmon_state.thread, NULL);
		sysmon_state.running = 0;
	}

	/* No object: the thread is asked to stop. */
	if (count == 0U) {
		if (sysmon_state.running && !sysmon_state.stop)
			sysmon_stop();
		return;
	}

	/* The period the thread samples at. */
	if (sysmon_state.lock_ready) {
		pthread_mutex_lock(&sysmon_state.lock);

		/* The shortest period asked. */
		sysmon_state.period_ms = period;

		/* The thread takes it at its next wait. */
		pthread_mutex_unlock(&sysmon_state.lock);
	} else {
		sysmon_state.period_ms = period;
	}

	/* The thread, for the first object. */
	if (!sysmon_state.running) {
		error = sysmon_start();
		if (error != 0)
			printf("KWL SYSTEM monitor thread error=%d\n", error);
	}
}

/* Takes the thread's new sample (and info) and tells every monitor object that may hear it. */
static void
sysmon_tell(
	struct kwl_server *server)
{
	struct kwl_client *client;
	struct kwl_object *object;
	uint32_t sample_serial;
	uint32_t info_serial;
	size_t needed;

	/* The thread's latest, copied when it is new. */
	pthread_mutex_lock(&sysmon_state.lock);

	/* The serials, and copies of what is new. */
	sample_serial = sysmon_state.sample_serial;
	info_serial = sysmon_state.info_serial;
	if (info_serial != sysmon_state.taken_info)
		sysmon_state.current_info = sysmon_state.info;
	if (sample_serial != sysmon_state.taken_sample) {
		sysmon_state.current = sysmon_state.sample;
		sysmon_state.current_serial = sample_serial;
	}

	/* The thread may write the next. */
	pthread_mutex_unlock(&sysmon_state.lock);

	/* Nothing new. */
	if (sample_serial == sysmon_state.taken_sample && info_serial == sysmon_state.taken_info)
		return;
	sysmon_state.taken_info = info_serial;

	/* A sample's size on the wire, to see whether a client has room for it. */
	needed = sysmon_sample_bytes(&sysmon_state.current);

	/* Each live monitor object: a new info, then the new sample when it acked the last and has room. */
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (object = client->objects; object != NULL; object = object->next) {
			if (object->kind != KWL_SYSTEM_MONITOR || object->dead)
				continue;

			/* The info it has not heard. */
			if (object->monitor_info != info_serial && info_serial != 0U)
				sysmon_send_info(object);

			/* The sample, whole or not at all. */
			if (sample_serial == sysmon_state.taken_sample || sample_serial == 0U)
				continue;
			if (object->monitor_waiting != 0U)
				continue;
			if (client->output_bytes + needed > KWL_OUTPUT_MAX)
				continue;
			sysmon_send_sample(object);
		}
	}

	/* The sample is taken. */
	sysmon_state.taken_sample = sample_serial;
}

/* Sends an object the info: the machine, a device a GPU, disk or link, and the done. */
static void
sysmon_send_info(
	struct kwl_object *object)
{
	const struct kl_backend_monitor_info *info;
	unsigned char payload[SYSMON_EVENT_MAX];
	size_t offset;
	unsigned index;

	/* The machine. */
	info = &sysmon_state.current_info;
	offset = sysmon_put_word(payload, 0U, info->cpu_count);
	offset = sysmon_put_string(payload, offset, info->host);
	offset = sysmon_put_wide(payload, offset, info->generation);
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_INFO, payload, offset);

	/* Each GPU. */
	for (index = 0; index < info->gpu_count; index++) {
		offset = sysmon_put_word(payload, 0U, KL_SYSTEM_MONITOR_DEVICE_GPU);
		offset = sysmon_put_wide(payload, offset, info->gpu[index].id);
		offset = sysmon_put_wide(payload, offset, info->gpu[index].generation);
		offset = sysmon_put_word(payload, offset, 0U);
		offset = sysmon_put_string(payload, offset, info->gpu[index].name);
		offset = sysmon_put_string(payload, offset, info->gpu[index].driver);
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_DEVICE, payload, offset);
	}

	/* Each disk, with its kind. */
	for (index = 0; index < info->disk_count; index++) {
		offset = sysmon_put_word(payload, 0U, KL_SYSTEM_MONITOR_DEVICE_DISK);
		offset = sysmon_put_wide(payload, offset, info->disk[index].id);
		offset = sysmon_put_wide(payload, offset, info->disk[index].generation);
		offset = sysmon_put_word(payload, offset, info->disk[index].kind);
		offset = sysmon_put_string(payload, offset, info->disk[index].name);
		offset = sysmon_put_string(payload, offset, "");
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_DEVICE, payload, offset);
	}

	/* Each link. */
	for (index = 0; index < info->link_count; index++) {
		offset = sysmon_put_word(payload, 0U, KL_SYSTEM_MONITOR_DEVICE_LINK);
		offset = sysmon_put_wide(payload, offset, info->link[index].id);
		offset = sysmon_put_wide(payload, offset, info->link[index].generation);
		offset = sysmon_put_word(payload, offset, 0U);
		offset = sysmon_put_string(payload, offset, info->link[index].name);
		offset = sysmon_put_string(payload, offset, "");
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_DEVICE, payload, offset);
	}

	/* The done: the info is whole. */
	offset = sysmon_put_word(payload, 0U, sysmon_state.taken_info);
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_INFO_DONE, payload, offset);
	object->monitor_info = sysmon_state.taken_info;
}

/* Sends an object the current sample: each CPU, the memory, each link, disk and GPU, and the done it acks. */
static void
sysmon_send_sample(
	struct kwl_object *object)
{
	const struct kl_backend_monitor_sample *sample;
	unsigned char payload[SYSMON_EVENT_MAX];
	uint32_t serial;
	size_t offset;
	unsigned index;

	/* Each CPU's ticks. */
	sample = &sysmon_state.current;
	for (index = 0; index < sample->cpu_count; index++) {
		offset = sysmon_put_word(payload, 0U, index);
		offset = sysmon_put_wide(payload, offset, sample->cpu[index].user);
		offset = sysmon_put_wide(payload, offset, sample->cpu[index].system);
		offset = sysmon_put_wide(payload, offset, sample->cpu[index].idle);
		offset = sysmon_put_wide(payload, offset, sample->cpu[index].other);
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_CPU, payload, offset);
	}

	/* The memory. */
	offset = sysmon_put_wide(payload, 0U, sample->memory_total);
	offset = sysmon_put_wide(payload, offset, sample->memory_free);
	offset = sysmon_put_wide(payload, offset, sample->memory_cache);
	offset = sysmon_put_wide(payload, offset, sample->memory_reclaimable);
	offset = sysmon_put_wide(payload, offset, sample->swap_total);
	offset = sysmon_put_wide(payload, offset, sample->swap_used);
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_MEMORY, payload, offset);

	/* Each link. */
	for (index = 0; index < sample->link_count; index++) {
		offset = sysmon_put_wide(payload, 0U, sample->link[index].id);
		offset = sysmon_put_wide(payload, offset, sample->link[index].rx_bytes);
		offset = sysmon_put_wide(payload, offset, sample->link[index].tx_bytes);
		offset = sysmon_put_word(payload, offset, sample->link[index].up);
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_LINK, payload, offset);
	}

	/* Each disk. */
	for (index = 0; index < sample->disk_count; index++) {
		offset = sysmon_put_wide(payload, 0U, sample->disk[index].id);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].read_ops);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].write_ops);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].read_bytes);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].write_bytes);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].read_ns);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].write_ns);
		offset = sysmon_put_wide(payload, offset, sample->disk[index].busy_ns);
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_DISK, payload, offset);
	}

	/* Each GPU. */
	for (index = 0; index < sample->gpu_count; index++) {
		offset = sysmon_put_wide(payload, 0U, sample->gpu[index].id);
		offset = sysmon_put_wide(payload, offset, sample->gpu[index].time_ns);
		offset = sysmon_put_wide(payload, offset, sample->gpu[index].busy_ns);
		offset = sysmon_put_wide(payload, offset, sample->gpu[index].memory_used);
		offset = sysmon_put_wide(payload, offset, sample->gpu[index].memory_total);
		offset = sysmon_put_word(payload, offset, sample->gpu[index].cur_mhz);
		offset = sysmon_put_word(payload, offset, sample->gpu[index].max_mhz);
		offset = sysmon_put_word(payload, offset, (uint32_t)sample->gpu[index].milli_celsius);
		offset = sysmon_put_word(payload, offset, sample->gpu[index].milli_watts);
		(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_GPU, payload, offset);
	}

	/* The done, which the client acks: the sample is whole. */
	serial = sysmon_state.current_serial;
	offset = sysmon_put_word(payload, 0U, serial);
	offset = sysmon_put_wide(payload, offset, sample->time_ns);
	offset = sysmon_put_wide(payload, offset, sample->valid);
	offset = sysmon_put_word(payload, offset, (uint32_t)sample->cpu_hz);
	offset = sysmon_put_word(payload, offset, (uint32_t)sample->cpu_milli_celsius);
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_MONITOR_EVENT_SAMPLE_DONE, payload, offset);
	object->monitor_waiting = serial;
}

/* A sample's size on the wire: each event's header and words. */
static size_t
sysmon_sample_bytes(
	const struct kl_backend_monitor_sample *sample)
{
	size_t bytes;

	/* The CPUs (9 words), the memory (12), the links (7), the disks (16), the GPUs (14), the done (7), each with its 8-byte header. */
	bytes = (size_t)sample->cpu_count * (8U + 9U * 4U);
	bytes += 8U + 12U * 4U;
	bytes += (size_t)sample->link_count * (8U + 7U * 4U);
	bytes += (size_t)sample->disk_count * (8U + 16U * 4U);
	bytes += (size_t)sample->gpu_count * (8U + 14U * 4U);
	bytes += 8U + 7U * 4U;

	/* Succeeded: the size. */
	return bytes;
}

/* A period within the protocol's bounds; 0 is the default. */
static uint32_t
sysmon_period(
	uint32_t period_ms)
{
	/* None asked: the default. */
	if (period_ms == 0U)
		return KL_SYSTEM_MONITOR_PERIOD_DEFAULT;

	/* Within the bounds. */
	if (period_ms < KL_SYSTEM_MONITOR_PERIOD_MIN)
		return KL_SYSTEM_MONITOR_PERIOD_MIN;
	if (period_ms > KL_SYSTEM_MONITOR_PERIOD_MAX)
		return KL_SYSTEM_MONITOR_PERIOD_MAX;

	/* Succeeded: the period as asked. */
	return period_ms;
}

/* Writes a word and gives the offset after it. */
static size_t
sysmon_put_word(
	unsigned char *payload,
	size_t offset,
	uint32_t word)
{
	/* The word in the wire's native byte order. */
	memcpy(payload + offset, &word, sizeof(word));

	/* Succeeded: the offset after it. */
	return offset + sizeof(word);
}

/* Writes a u64 as its high and low words and gives the offset after them. */
static size_t
sysmon_put_wide(
	unsigned char *payload,
	size_t offset,
	uint64_t wide)
{
	size_t next;

	/* The high half, then the low. */
	next = sysmon_put_word(payload, offset, (uint32_t)(wide >> 32));
	next = sysmon_put_word(payload, next, (uint32_t)(wide & 0xffffffffU));

	/* Succeeded: the offset after both. */
	return next;
}

/* Writes a string argument (cut to fit an event) and gives the offset after it. */
static size_t
sysmon_put_string(
	unsigned char *payload,
	size_t offset,
	const char *text)
{
	uint32_t length;
	size_t padded;
	size_t bytes;

	/* The length with the NUL (at most 64 bytes of text), the bytes, and zeros to a four-byte boundary. */
	bytes = strlen(text);
	if (bytes > 64U)
		bytes = 64U;
	length = (uint32_t)bytes + 1U;
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	memcpy(payload + offset, &length, sizeof(length));
	memset(payload + offset + 4U, 0, padded);
	memcpy(payload + offset + 4U, text, bytes);

	/* The offset after the string. */
	return offset + 4U + padded;
}

/* Reads a word of a request. */
static uint32_t
sysmon_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The word. */
	memcpy(&word, bytes + offset, sizeof(word));

	/* Succeeded: the word. */
	return word;
}
