/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The thread that makes the photos' pictures (ws157-p003; app.h): the
 * window queues jobs -- a thumbnail, or the picture of the photo shown
 * whole, which goes before the thumbnails -- and takes the results.  The
 * thread decodes one file at a time (decode.c), so that the window goes on
 * drawing while a large photo is read.  A job already queued, or being
 * made, is not queued twice; the thumbnails no longer wanted (scrolled
 * away) are dropped before each frame's are queued.
 *
 * A thumbnail made is kept in the cache folder ($XDG_CACHE_HOME/keiland/
 * photos, ws157-p005, plan/ws157/phase001/phase.md D4) as <id>.ppm, a
 * binary PPM of the square as decoded (not turned), and read from there
 * the next time instead of decoding the photo.  The cache is not the
 * library's: it can always be made again.
 */

#include "app.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The most jobs waiting, and the most results not yet taken. */
#define WORKER_JOBS		64U
#define WORKER_RESULTS		64U

/* A job: the photo, a whole picture or a thumbnail, its turns, the library's generation, the file and the photo's id. */
struct worker_job {
	size_t photo;
	int whole;
	int turns;
	unsigned generation;
	char path[PH_PATH_MAX];
	char id[PH_ID_SIZE];
};

/*
 * The thread's state: the lock and the signal of a new job; the jobs
 * waiting and the one being made (busy); the results; whether the thread
 * runs and is to stop; the cache folder (empty for none).
 */
struct worker {
	pthread_mutex_t lock;
	pthread_cond_t wake;
	pthread_t thread;
	struct worker_job jobs[WORKER_JOBS];
	size_t job_count;
	struct worker_job current;
	int busy;
	struct ph_result results[WORKER_RESULTS];
	size_t result_count;
	int running;
	int stop;
	char cache[PH_PATH_MAX];
};

/* The program's thread. */
static struct worker worker = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.wake = PTHREAD_COND_INITIALIZER
};

static void *worker_main(void *unused);
static void worker_make(const struct worker_job *job, struct ph_result *result);
static int worker_same(const struct worker_job *job, size_t photo, int whole, unsigned generation);
static int worker_cache_read(const char *id, struct kl_image *image);
static void worker_cache_write(const char *id, const struct kl_image *image);

/*
 * Starts the thread, the thumbnails kept in a cache folder (NULL or empty
 * for none; made when it is not there).  Returns 0 or an errno value.
 */
int
ph_worker_start(
	const char *cache)
{
	char probe[PH_PATH_MAX + 4U];
	int error;

	/* Once. */
	if (worker.running)
		return 0;

	/* The cache folder. */
	worker.cache[0] = '\0';
	if (cache != NULL && cache[0] != '\0') {
		(void)snprintf(worker.cache, sizeof(worker.cache), "%s", cache);
		(void)snprintf(probe, sizeof(probe), "%s/x", cache);
		error = ph_db_folders(probe);
		if (error != 0)
			worker.cache[0] = '\0';
	}

	/* The thread. */
	worker.stop = 0;
	error = pthread_create(&worker.thread, NULL, worker_main, NULL);
	if (error != 0)
		return error;
	worker.running = 1;
	return 0;
}

/*
 * Stops the thread (after the job it makes) and frees the results not
 * taken.
 */
void
ph_worker_stop(void)
{
	size_t index;

	/* Asked to stop, and waited for. */
	if (worker.running) {
		(void)pthread_mutex_lock(&worker.lock);
		worker.stop = 1;
		(void)pthread_cond_signal(&worker.wake);
		(void)pthread_mutex_unlock(&worker.lock);
		(void)pthread_join(worker.thread, NULL);
		worker.running = 0;
	}

	/* What was left. */
	for (index = 0; index < worker.result_count; index++)
		kl_image_release(&worker.results[index].image);
	worker.result_count = 0;
	worker.job_count = 0;
}

/*
 * Queues a job: a whole picture goes first, a thumbnail last.  Returns 0
 * (also for one queued or being made already), EAGAIN when the queue is
 * full, ENAMETOOLONG.
 */
int
ph_worker_queue(
	size_t photo,
	int whole,
	int turns,
	const char *path,
	const char *id,
	unsigned generation)
{
	struct worker_job *job;
	size_t length;
	size_t index;
	int same;

	/* The path fits. */
	length = strlen(path);
	if (length >= PH_PATH_MAX)
		return ENAMETOOLONG;

	/* Already queued or being made. */
	(void)pthread_mutex_lock(&worker.lock);
	same = worker.busy && worker_same(&worker.current, photo, whole, generation);
	for (index = 0; index < worker.job_count && !same; index++)
		same = worker_same(&worker.jobs[index], photo, whole, generation);
	if (same) {
		(void)pthread_mutex_unlock(&worker.lock);
		return 0;
	}

	/* No room. */
	if (worker.job_count == WORKER_JOBS) {
		(void)pthread_mutex_unlock(&worker.lock);
		return EAGAIN;
	}

	/* A whole picture at the front, a thumbnail at the end. */
	if (whole) {
		memmove(&worker.jobs[1], &worker.jobs[0], worker.job_count * sizeof(worker.jobs[0]));
		job = &worker.jobs[0];
	} else {
		job = &worker.jobs[worker.job_count];
	}

	/* The job, and the thread told. */
	job->photo = photo;
	job->whole = whole;
	job->turns = turns;
	job->generation = generation;
	memcpy(job->path, path, length + 1U);
	(void)snprintf(job->id, sizeof(job->id), "%s", id);
	worker.job_count++;
	(void)pthread_cond_signal(&worker.wake);
	(void)pthread_mutex_unlock(&worker.lock);
	return 0;
}

/*
 * Drops the thumbnails waiting (the whole pictures stay).
 */
void
ph_worker_drop_thumbs(void)
{
	size_t index;
	size_t kept;

	/* The whole pictures kept in their order. */
	(void)pthread_mutex_lock(&worker.lock);
	kept = 0;
	for (index = 0; index < worker.job_count; index++) {
		if (!worker.jobs[index].whole)
			continue;
		worker.jobs[kept] = worker.jobs[index];
		kept++;
	}

	/* Those kept. */
	worker.job_count = kept;
	(void)pthread_mutex_unlock(&worker.lock);
}

/*
 * Takes the oldest result: 1 with it (its picture is the caller's), 0 when
 * none waits.
 */
int
ph_worker_take(
	struct ph_result *result)
{
	size_t index;

	/* None. */
	(void)pthread_mutex_lock(&worker.lock);
	if (worker.result_count == 0U) {
		(void)pthread_mutex_unlock(&worker.lock);
		return 0;
	}

	/* The first, the rest moved up. */
	*result = worker.results[0];
	for (index = 1; index < worker.result_count; index++)
		worker.results[index - 1U] = worker.results[index];
	worker.result_count--;

	/* The thread may wait for the room. */
	(void)pthread_cond_signal(&worker.wake);
	(void)pthread_mutex_unlock(&worker.lock);
	return 1;
}

/*
 * Tells whether the thread has work: a job waiting or being made, or a
 * result not taken.
 */
int
ph_worker_busy(void)
{
	int busy;

	/* Under the lock. */
	(void)pthread_mutex_lock(&worker.lock);
	busy = worker.busy || worker.job_count != 0U || worker.result_count != 0U;
	(void)pthread_mutex_unlock(&worker.lock);
	return busy;
}

/* The thread: each job in turn until it is asked to stop. */
static void *
worker_main(
	void *unused)
{
	struct ph_result result;

	/* Each job. */
	(void)unused;
	(void)pthread_mutex_lock(&worker.lock);
	for (;;) {
		/* Waits for a job, or the stop. */
		while (!worker.stop && (worker.job_count == 0U || worker.result_count == WORKER_RESULTS))
			(void)pthread_cond_wait(&worker.wake, &worker.lock);
		if (worker.stop)
			break;

		/* The first job, made without the lock. */
		worker.current = worker.jobs[0];
		worker.job_count--;
		memmove(&worker.jobs[0], &worker.jobs[1], worker.job_count * sizeof(worker.jobs[0]));
		worker.busy = 1;
		(void)pthread_mutex_unlock(&worker.lock);
		worker_make(&worker.current, &result);

		/* Its result. */
		(void)pthread_mutex_lock(&worker.lock);
		worker.busy = 0;
		worker.results[worker.result_count] = result;
		worker.result_count++;
	}

	/* Stopped. */
	(void)pthread_mutex_unlock(&worker.lock);
	return NULL;
}

/* Makes a job's picture into its result. */
static void
worker_make(
	const struct worker_job *job,
	struct ph_result *result)
{
	struct kl_image decoded;
	struct kl_image sized;
	int error;

	/* The result's identity. */
	memset(result, 0, sizeof(*result));
	result->photo = job->photo;
	result->whole = job->whole;
	result->turns = job->turns;
	result->generation = job->generation;

	/* A thumbnail kept in the cache. */
	error = ENOENT;
	if (!job->whole)
		error = worker_cache_read(job->id, &sized);

	/* Else the file's picture. */
	if (error != 0) {
		error = ph_decode(job->path, &decoded);
		if (error != 0) {
			result->error = error;
			return;
		}

		/* Its size: a thumbnail (kept for the next time), or fitted for the window. */
		if (job->whole)
			error = ph_fit(&decoded, PH_VIEW_SIDE, &sized);
		else
			error = ph_thumbnail(&decoded, PH_THUMB_SIDE, &sized);
		kl_image_release(&decoded);
		if (error != 0) {
			result->error = error;
			return;
		}

		/* A new thumbnail is kept. */
		if (!job->whole)
			worker_cache_write(job->id, &sized);
	}

	/* Turned as the user asked. */
	if (job->turns == 0) {
		result->image = sized;
		return;
	}

	/* Else turned. */
	error = ph_turn(&sized, job->turns, &result->image);
	kl_image_release(&sized);
	result->error = error;
}

/* Tells whether a job is the same photo's picture of the same kind and library. */
static int
worker_same(
	const struct worker_job *job,
	size_t photo,
	int whole,
	unsigned generation)
{
	/* All three. */
	return job->photo == photo && job->whole == whole && job->generation == generation;
}

/* Reads a photo's thumbnail from the cache: 0 with it, or ENOENT (not there, or not a thumbnail of the side). */
static int
worker_cache_read(
	const char *id,
	struct kl_image *image)
{
	unsigned char *bytes;
	char path[PH_PATH_MAX + 64U];
	char header[32];
	size_t want;
	size_t index;
	ssize_t got;
	int length;
	int error;
	int same;
	int fd;

	/* The file. */
	if (worker.cache[0] == '\0')
		return ENOENT;
	(void)snprintf(path, sizeof(path), "%s/%s.ppm", worker.cache, id);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return ENOENT;

	/* Its header: exactly the one written. */
	length = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", PH_THUMB_SIDE, PH_THUMB_SIDE);
	want = (size_t)PH_THUMB_SIDE * (size_t)PH_THUMB_SIDE * 3U;
	bytes = malloc((size_t)length + want);
	if (bytes == NULL) {
		(void)close(fd);
		return ENOMEM;
	}

	/* Read whole. */
	got = read(fd, bytes, (size_t)length + want);
	(void)close(fd);
	same = got == (ssize_t)((size_t)length + want) && memcmp(bytes, header, (size_t)length) == 0;
	if (!same) {
		free(bytes);
		return ENOENT;
	}

	/* The pixels, opaque. */
	error = kl_image_create(image, PH_THUMB_SIDE, PH_THUMB_SIDE);
	if (error != 0) {
		free(bytes);
		return error;
	}

	/* Each pixel. */
	for (index = 0; index < (size_t)PH_THUMB_SIDE * (size_t)PH_THUMB_SIDE; index++) {
		image->pixels[index] = 0xff000000U | ((uint32_t)bytes[length + index * 3U] << 16) | ((uint32_t)bytes[length + index * 3U + 1U] << 8) |
		    bytes[length + index * 3U + 2U];
	}

	/* Read. */
	free(bytes);
	return 0;
}

/* Keeps a photo's thumbnail in the cache (a new file renamed over the old; a failure keeps nothing). */
static void
worker_cache_write(
	const char *id,
	const struct kl_image *image)
{
	unsigned char *bytes;
	char path[PH_PATH_MAX + 64U];
	char temporary[PH_PATH_MAX + 96U];
	uint32_t pixel;
	size_t size;
	size_t index;
	ssize_t wrote;
	int length;
	int status;
	int fd;

	/* A cache, and the bytes: the header and the colours (the premultiplied ones). */
	if (worker.cache[0] == '\0' || image->width != PH_THUMB_SIDE || image->height != PH_THUMB_SIDE)
		return;
	size = 32U + (size_t)PH_THUMB_SIDE * (size_t)PH_THUMB_SIDE * 3U;
	bytes = malloc(size);
	if (bytes == NULL)
		return;
	length = snprintf((char *)bytes, 32U, "P6\n%d %d\n255\n", PH_THUMB_SIDE, PH_THUMB_SIDE);
	for (index = 0; index < (size_t)PH_THUMB_SIDE * (size_t)PH_THUMB_SIDE; index++) {
		pixel = image->pixels[(index / PH_THUMB_SIDE) * image->stride + index % PH_THUMB_SIDE];
		bytes[length + index * 3U] = (unsigned char)(pixel >> 16);
		bytes[length + index * 3U + 1U] = (unsigned char)(pixel >> 8);
		bytes[length + index * 3U + 2U] = (unsigned char)pixel;
	}

	/* Written beside, then in place. */
	(void)snprintf(path, sizeof(path), "%s/%s.ppm", worker.cache, id);
	(void)snprintf(temporary, sizeof(temporary), "%s.new-%ld", path, (long)getpid());
	fd = open(temporary, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
	if (fd < 0) {
		free(bytes);
		return;
	}

	/* Its bytes. */
	size = (size_t)length + (size_t)PH_THUMB_SIDE * (size_t)PH_THUMB_SIDE * 3U;
	wrote = write(fd, bytes, size);
	status = close(fd);
	free(bytes);
	if (wrote != (ssize_t)size || status != 0) {
		(void)unlink(temporary);
		return;
	}

	/* In place. */
	status = rename(temporary, path);
	if (status != 0)
		(void)unlink(temporary);
}
