/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the file I/O thread's writing of desktop.conf
 * (userland/desktop/wayland/settings-store.c's give and run on a lane,
 * lane.c): the session's changes given on the main thread, merged on the
 * lane's thread while the main thread does not wait for a slow flush
 * (fsync wrapped to sleep), taken as written by the finish, a change made
 * after the give written by the finish, a give that did not run written
 * whole by the finish, and one writer a session.
 */

#include "userland/desktop/wayland/lane.h"
#include "userland/desktop/wayland/settings-store.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* How long the wrapped fsync sleeps (ms), and the longest the main thread may take to give and submit (ms). */
#define TEST_FSYNC_MS	300U
#define TEST_MAIN_MS	5U

/* One merge on the lane: the job and the store's writer it runs. */
struct test_save {
	struct kwl_lane_job lane;
	struct kwl_settings_writer *writer;
	unsigned done;
	unsigned dropped;
};

int __real_fsync(int descriptor);
int __wrap_fsync(int descriptor);

/* The test's home, the fsyncs counted, and the failures. */
static char test_home[256];
static pthread_mutex_t fsync_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned fsync_count;
static pthread_t main_thread;
static unsigned fsync_on_main;
static unsigned failures;

static void save_run(struct kwl_lane_job *job);
static void save_done(void *context, struct kwl_lane_job *job);
static void save_drop(struct kwl_lane_job *job);
static void check(int condition, const char *what);
static uint64_t now_ms(void);
static unsigned fsyncs(void);
static int file_has(const char *line);
static void wait_wake(int descriptor);
static void test_given(void);
static void test_not_run(void);

/* The operations of the test's merges. */
static const struct kwl_lane_ops save_ops = { save_run, save_done, save_drop };

int
main(void)
{
	char *made;

	/* A home of its own under the build directory. */
	main_thread = pthread_self();
	(void)snprintf(test_home, sizeof(test_home), "%s/home-XXXXXX", getenv("TEST_DIR") != NULL ? getenv("TEST_DIR") : ".");
	made = mkdtemp(test_home);
	if (made == NULL) {
		perror("mkdtemp");
		return 1;
	}

	/* The cases. */
	test_given();
	test_not_run();

	/* The verdict. */
	if (failures != 0U) {
		printf("host-io-file: FAIL (%u)\n", failures);
		return 1;
	}
	printf("host-io-file: PASS\n");
	return 0;
}

/* The flush, slow, and counted with whether the main thread called it. */
int
__wrap_fsync(
	int descriptor)
{
	struct timespec pause;

	pthread_mutex_lock(&fsync_lock);
	fsync_count++;
	if (pthread_equal(pthread_self(), main_thread))
		fsync_on_main++;
	pthread_mutex_unlock(&fsync_lock);

	pause.tv_sec = 0;
	pause.tv_nsec = (long)TEST_FSYNC_MS * 1000000L;
	(void)nanosleep(&pause, NULL);
	return __real_fsync(descriptor);
}

/* A change given to the lane's thread, written there, and one made after it written by the finish. */
static void
test_given(void)
{
	struct kwl_settings_store store;
	struct kwl_lane lane;
	struct test_save *save;
	uint64_t started;
	uint64_t took;
	int wake[2];
	int error;

	check(pipe(wake) == 0, "given: wake pipe");
	check(kwl_lane_start(&lane, "io-file", 1U, wake[1]) == 0, "given: lane started");
	check(kwl_settings_store_open(&store, test_home) == 0, "given: store opened");
	(void)kwl_settings_store_load(&store);
	(void)kwl_settings_store_choose(&store, "mouse.speed", "80");

	/* Given and submitted on the main thread without waiting for the flush. */
	save = calloc(1U, sizeof(*save));
	started = now_ms();
	error = kwl_settings_store_save_give(&store);
	save->writer = &store.writer;
	kwl_lane_submit(&lane, &save->lane, &save_ops, 0U);
	took = now_ms() - started;
	check(error == 0 && store.writer.given, "given: the changes are given");
	check(took < TEST_MAIN_MS, "given: the main thread did not wait for the flush");
	check(kwl_settings_store_save_give(&store) == EBUSY, "given: one writer a session");
	check(kwl_settings_store_save_later(&store) == EBUSY, "given: no thread of the store's beside it");

	/* The lane wrote it; a change after the give is the finish's. */
	(void)kwl_settings_store_choose(&store, "mouse.natural", "1");
	wait_wake(wake[0]);
	(void)kwl_lane_take(&lane, NULL);
	check(save->done && store.writer.done && store.writer.error == 0, "given: merged on the lane's thread");
	check(file_has("mouse.speed=80") && !file_has("mouse.natural=1"), "given: the file holds the given change only");
	check(fsyncs() == 1U, "given: one flush so far");

	/* The finish takes the given change as written and writes the later one. */
	error = kwl_settings_store_finish(&store);
	check(error == 0 && file_has("mouse.speed=80") && file_has("mouse.natural=1"), "given: the finish writes the later change");
	check(fsyncs() == 2U, "given: the finish flushed once more");
	check(fsync_on_main == 1U, "given: only the finish flushed on the main thread");
	check(!store.writer.given, "given: no writer given after the finish");
	kwl_settings_store_close(&store);
	kwl_lane_stop(&lane);
	free(save);
	(void)close(wake[0]);
	(void)close(wake[1]);
}

/* A give whose job the lane dropped (stopped before it ran): the finish writes everything. */
static void
test_not_run(void)
{
	struct kwl_settings_store store;
	struct test_save *save;
	unsigned before;
	int error;

	check(kwl_settings_store_open(&store, test_home) == 0, "not run: store opened");
	(void)kwl_settings_store_load(&store);
	(void)kwl_settings_store_choose(&store, "mouse.speed", "40");
	error = kwl_settings_store_save_give(&store);
	check(error == 0 && store.writer.given, "not run: given");

	/* The job is dropped without its run, as a stopped lane drops it. */
	save = calloc(1U, sizeof(*save));
	save->writer = &store.writer;
	save_drop(&save->lane);

	/* The finish sees it never ran and writes the change. */
	before = fsyncs();
	error = kwl_settings_store_finish(&store);
	check(error == 0 && file_has("mouse.speed=40"), "not run: the finish writes the change");
	check(fsyncs() == before + 1U, "not run: one flush");
	kwl_settings_store_close(&store);

	/* Nothing changed: nothing is given. */
	check(kwl_settings_store_open(&store, test_home) == 0, "unchanged: store opened");
	(void)kwl_settings_store_load(&store);
	error = kwl_settings_store_save_give(&store);
	check(error == 0 && !store.writer.given, "unchanged: nothing given");
	kwl_settings_store_close(&store);
}

static void
save_run(
	struct kwl_lane_job *job)
{
	struct test_save *save;

	save = (struct test_save *)job;
	kwl_settings_store_save_run(save->writer);
}

static void
save_done(
	void *context,
	struct kwl_lane_job *job)
{
	(void)context;
	((struct test_save *)job)->done = 1U;
}

static void
save_drop(
	struct kwl_lane_job *job)
{
	((struct test_save *)job)->dropped = 1U;
	free(job);
}

static void
check(
	int condition,
	const char *what)
{
	printf("%s %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition)
		failures++;
}

static uint64_t
now_ms(void)
{
	struct timespec now;

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static unsigned
fsyncs(void)
{
	unsigned count;

	pthread_mutex_lock(&fsync_lock);
	count = fsync_count;
	pthread_mutex_unlock(&fsync_lock);
	return count;
}

static int
file_has(
	const char *line)
{
	char path[512];
	char text[4096];
	FILE *file;
	size_t length;

	(void)snprintf(path, sizeof(path), "%s/.config/keiland/desktop.conf", test_home);
	file = fopen(path, "r");
	if (file == NULL)
		return 0;
	length = fread(text, 1U, sizeof(text) - 1U, file);
	(void)fclose(file);
	text[length] = '\0';
	return strstr(text, line) != NULL;
}

static void
wait_wake(
	int descriptor)
{
	struct pollfd watch;
	char byte;

	watch.fd = descriptor;
	watch.events = POLLIN;
	watch.revents = 0;
	check(poll(&watch, 1U, 5000) == 1, "wake byte within 5 s");
	(void)read(descriptor, &byte, 1U);
}
