/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the printers' settings file on the host's thread
 * (userland/desktop/libkeiland-backend/print/print.c, kl_backend_print_host):
 * a change waits without a writer thread of the backend's, is written by
 * kl_backend_print_write on another thread, and answered by the update with
 * the file written; the update does not read the file while hosted; a
 * check reads another session's change; a check begun before a change made
 * is not applied after it, and the next check applies the file; a hosted
 * backend answers whether the daemon's program can be run as its last
 * check found.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The test's file, its main thread, and the failures. */
static char test_config[512];
static unsigned failures;

static void check(int condition, const char *what);
static void *write_run(void *argument);
static void *check_run(void *argument);
static void write_elsewhere(struct kl_backend_print *print);
static void sleep_ms(unsigned milliseconds);
static size_t printers(struct kl_backend_print *print);

/* A check run on another thread: the backend and the check. */
struct test_check {
	struct kl_backend_print *print;
	struct kl_backend_print_check *check;
};

int
main(void)
{
	struct kl_backend_print *print;
	struct kl_backend_print *other;
	struct kl_backend_print *daemon;
	struct kl_backend_print_check *early;
	struct test_check run;
	pthread_t thread;
	uint32_t request;
	uint32_t answered;
	unsigned changed;
	unsigned saved;
	char folder[256];
	char program[320];
	int descriptor;
	char *made;
	int error;
	int taken;

	/* A folder of its own. */
	(void)snprintf(folder, sizeof(folder), "%s/print-XXXXXX", getenv("TEST_DIR") != NULL ? getenv("TEST_DIR") : "/tmp");
	made = mkdtemp(folder);
	if (made == NULL) {
		perror("mkdtemp");
		return 1;
	}
	(void)snprintf(test_config, sizeof(test_config), "%s/printers.conf", folder);

	/* A change waits for the host. */
	print = kl_backend_print_open(test_config, folder, "/nonexistent/keiland-printd");
	check(print != NULL, "open");
	kl_backend_print_host(print);
	check(kl_backend_print_write_wanted(print) == 0, "nothing waits at first");
	error = kl_backend_print_add(print, KL_BACKEND_PRINTER_LPD, "10.0.0.1", 515U, "lp", &request);
	check(error == 0, "add asked");
	check(kl_backend_print_write_wanted(print) == 1, "the change waits for the host's write");
	sleep_ms(50);
	check(access(test_config, F_OK) != 0, "no writer thread of the backend's wrote it");

	/* Written on another thread, answered by the update. */
	(void)pthread_create(&thread, NULL, write_run, print);
	(void)pthread_join(thread, NULL);
	check(access(test_config, F_OK) == 0, "the host's write wrote the file");
	check(kl_backend_print_write_wanted(print) == 0, "nothing waits after the write");
	(void)kl_backend_print_update(print, &changed);
	check((changed & KL_BACKEND_PRINT_CHANGED_LIST) != 0U && (changed & KL_BACKEND_PRINT_CHANGED_RESULT) != 0U, "the update told the list and the result");
	taken = kl_backend_print_take_result(print, &answered, &error, &saved);
	check(taken && answered == request && error == 0 && saved == 1U, "the result says written");
	check(printers(print) == 1U, "one printer");

	/* Another session's change: the update does not read it while hosted, a check does. */
	sleep_ms(20);
	other = kl_backend_print_open(test_config, folder, "/nonexistent/keiland-printd");
	write_elsewhere(other);
	(void)kl_backend_print_update(print, &changed);
	check((changed & KL_BACKEND_PRINT_CHANGED_LIST) == 0U && printers(print) == 1U, "the hosted update did not read the file");
	run.print = print;
	run.check = kl_backend_print_check_begin(print);
	(void)pthread_create(&thread, NULL, check_run, &run);
	(void)pthread_join(thread, NULL);
	kl_backend_print_check_end(print, run.check);
	(void)kl_backend_print_update(print, &changed);
	check((changed & KL_BACKEND_PRINT_CHANGED_LIST) != 0U && printers(print) == 2U, "a check read the other session's printer");

	/* A check begun before a change made is not applied after it. */
	early = kl_backend_print_check_begin(print);
	error = kl_backend_print_add(print, KL_BACKEND_PRINTER_LPD, "10.0.0.3", 515U, "lp", &request);
	(void)pthread_create(&thread, NULL, write_run, print);
	(void)pthread_join(thread, NULL);
	(void)kl_backend_print_update(print, &changed);
	check(error == 0 && printers(print) == 3U, "the third printer made");
	sleep_ms(20);
	write_elsewhere(other);
	run.check = early;
	(void)pthread_create(&thread, NULL, check_run, &run);
	(void)pthread_join(thread, NULL);
	kl_backend_print_check_end(print, early);
	(void)kl_backend_print_update(print, &changed);
	check((changed & KL_BACKEND_PRINT_CHANGED_LIST) == 0U && printers(print) == 3U, "the early check is not applied");
	run.check = kl_backend_print_check_begin(print);
	(void)pthread_create(&thread, NULL, check_run, &run);
	(void)pthread_join(thread, NULL);
	kl_backend_print_check_end(print, run.check);
	(void)kl_backend_print_update(print, &changed);
	check((changed & KL_BACKEND_PRINT_CHANGED_LIST) != 0U && printers(print) == 4U, "the next check applies the file");

	/* The daemon's program: a hosted backend answers what its last check found, not what is there now. */
	(void)snprintf(program, sizeof(program), "%s/keiland-printd", folder);
	daemon = kl_backend_print_open(test_config, folder, program);
	check(kl_backend_print_can(daemon) == 0, "no program at the open");
	kl_backend_print_host(daemon);
	descriptor = open(program, O_WRONLY | O_CREAT, 0755);
	check(descriptor >= 0, "the program made");
	if (descriptor >= 0)
		(void)close(descriptor);
	check(kl_backend_print_can(daemon) == 0, "hosted: the program is not looked at by the call");
	run.print = daemon;
	run.check = kl_backend_print_check_begin(daemon);
	(void)pthread_create(&thread, NULL, check_run, &run);
	(void)pthread_join(thread, NULL);
	kl_backend_print_check_end(daemon, run.check);
	check(kl_backend_print_can(daemon) == 1, "hosted: the check found the program");
	kl_backend_print_close(daemon);

	/* A change still waiting is made by the close. */
	error = kl_backend_print_add(print, KL_BACKEND_PRINTER_LPD, "10.0.0.5", 515U, "lp", &request);
	kl_backend_print_close(print);
	print = kl_backend_print_open(test_config, folder, "/nonexistent/keiland-printd");
	check(error == 0 && printers(print) == 5U, "the close made the change waiting");
	kl_backend_print_close(print);
	kl_backend_print_close(other);

	/* The verdict. */
	if (failures != 0U) {
		printf("host-print-file: FAIL (%u)\n", failures);
		return 1;
	}
	printf("host-print-file: PASS\n");
	return 0;
}

static void *
write_run(
	void *argument)
{
	kl_backend_print_write(argument);
	return NULL;
}

static void *
check_run(
	void *argument)
{
	struct test_check *run;

	run = argument;
	kl_backend_print_check_run(run->print, run->check);
	return NULL;
}

/* Adds a printer through another backend on the same file, as another session would. */
static void
write_elsewhere(
	struct kl_backend_print *print)
{
	static unsigned host = 100U;
	char name[32];
	uint32_t request;

	kl_backend_print_host(print);
	(void)snprintf(name, sizeof(name), "10.0.1.%u", host);
	host++;
	(void)kl_backend_print_add(print, KL_BACKEND_PRINTER_LPD, name, 515U, "lp", &request);
	kl_backend_print_write(print);
}

static size_t
printers(
	struct kl_backend_print *print)
{
	struct kl_backend_printer list[KL_BACKEND_PRINTERS_MAX];

	return kl_backend_print_printers(print, list, KL_BACKEND_PRINTERS_MAX);
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

static void
sleep_ms(
	unsigned milliseconds)
{
	struct timespec pause;

	pause.tv_sec = 0;
	pause.tv_nsec = (long)milliseconds * 1000000L;
	(void)nanosleep(&pause, NULL);
}
