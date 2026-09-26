/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks fork, vfork and posix_spawn: a fork child's write is its own
 * (copy on write, over more pages than a fork batch), a vfork child's
 * write is the parent's, posix_spawn and posix_spawnp run programs and
 * report a missing one, and several processes fork at once.  Prints one
 * line per check and "ALL OK" at the end; built and run on the guest by
 * guest-vfork.sh.  From ws064-p003.
 */

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The number of pages the copy-on-write check writes, more than a fork batch. */
#define COW_PAGES 100

/* The size of a page the checks assume. */
#define PAGE_BYTES 4096

/* The vforks in a row of the repeated check. */
#define VFORK_ROUNDS 2000

/* The forks each of the concurrent processes makes. */
#define CONCURRENT_FORKS 300

/* The processes that fork at once. */
#define CONCURRENT_PROCESSES 4

extern char **environ;

static int check_fork_cow(void);
static int check_vfork(void);
static int check_vfork_rounds(void);
static int check_spawn(void);
static int check_concurrent(void);
static void concurrent_worker(void);
static int report(const char *name, int passed);
static int wait_status(pid_t child);

/*
 * Runs each check and reports whether all passed.
 */
int
main(
	void)
{
	int failed;

	/* Runs the checks in turn; each reports its own line. */
	failed = 0;
	failed += check_fork_cow();
	failed += check_vfork();
	failed += check_vfork_rounds();
	failed += check_spawn();
	failed += check_concurrent();

	/* Reports a failed check. */
	if (failed != 0) {
		printf("FAILED %d\n", failed);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("ALL OK\n");
	return 0;
}

/* Checks that a fork child's writes to many pages stay its own: 0 or 1. */
static int
check_fork_cow(void)
{
	size_t size;
	char *memory;
	pid_t child;
	int status;
	int index;
	int passed;
	int failed;

	/* Fills the pages in the parent. */
	size = (size_t)COW_PAGES * PAGE_BYTES;
	memory = malloc(size);
	if (memory == NULL) {
		failed = report("fork copy on write (no memory)", 0);
		return failed;
	}

	/* The parent's bytes are p. */
	memset(memory, 'p', size);

	/* The child writes every page and exits 0 when its own copy changed. */
	child = fork();
	if (child == 0) {
		memset(memory, 'c', size);
		if (memory[(COW_PAGES - 1) * PAGE_BYTES] != 'c')
			_exit(1);
		_exit(0);
	}

	/* The child must have seen its own writes. */
	status = wait_status(child);
	passed = 0;
	if (status == 0)
		passed = 1;

	/* The parent must still see its own bytes. */
	for (index = 0; index < COW_PAGES; index++) {
		if (memory[index * PAGE_BYTES] != 'p')
			passed = 0;
	}

	/* The pages were only for the check. */
	free(memory);

	/* Reports the check. */
	failed = report("fork copy on write", passed);
	return failed;
}

/* Checks that a vfork child borrows the parent's memory: 0 or 1. */
static int
check_vfork(void)
{
	volatile int shared;
	pid_t child;
	int status;
	int passed;
	int failed;

	/* The child's write is the parent's; the child exits at once. */
	shared = 0;
	child = vfork();
	if (child == 0) {
		shared = 42;
		_exit(0);
	}

	/* The parent must see the write after the child is done. */
	status = wait_status(child);
	passed = 0;
	if (status == 0 && shared == 42)
		passed = 1;

	/* Reports the check. */
	failed = report("vfork shares memory", passed);
	return failed;
}

/* Checks many vforks in a row, each child execing true: 0 or 1. */
static int
check_vfork_rounds(void)
{
	pid_t child;
	int status;
	int round;
	int failed;

	/* Each round's child must exec and exit 0. */
	for (round = 0; round < VFORK_ROUNDS; round++) {
		child = vfork();
		if (child == 0) {
			execl("/bin/true", "true", (char *)NULL);
			_exit(127);
		}

		/* Stops at the first round that failed. */
		status = wait_status(child);
		if (status != 0) {
			printf("round %d: status %d\n", round, status);
			failed = report("vfork 2000", 0);
			return failed;
		}
	}

	/* Reports the check. */
	failed = report("vfork 2000", 1);
	return failed;
}

/* Checks posix_spawn and posix_spawnp: the number of failures. */
static int
check_spawn(void)
{
	posix_spawn_file_actions_t actions;
	char *echo_arguments[] = { "echo", "spawned", NULL };
	char *true_arguments[] = { "true", NULL };
	pid_t child;
	int status;
	int error;
	int passed;
	int failed;

	/* Runs a program by its path, its output into /dev/null. */
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", 1, 0);
	error = posix_spawn(&child, "/bin/echo", &actions, NULL, echo_arguments, environ);
	posix_spawn_file_actions_destroy(&actions);

	/* The program must have run and exited 0. */
	passed = 0;
	if (error == 0) {
		status = wait_status(child);
		if (status == 0)
			passed = 1;
	}

	/* Reports the first check. */
	failed = report("posix_spawn", passed);

	/* Runs a program looked up on PATH. */
	error = posix_spawnp(&child, "true", NULL, NULL, true_arguments, environ);
	passed = 0;
	if (error == 0) {
		status = wait_status(child);
		if (status == 0)
			passed = 1;
	}

	/* Reports the second check. */
	failed += report("posix_spawnp", passed);

	/* A program that is not there is reported as ENOENT. */
	error = posix_spawn(&child, "/nonexistent/program", NULL, NULL, true_arguments, environ);
	passed = 0;
	if (error == ENOENT)
		passed = 1;
	failed += report("posix_spawn missing", passed);

	/* Succeeded: the number of failed checks. */
	return failed;
}

/* Checks several processes forking at once: 0 or 1. */
static int
check_concurrent(void)
{
	pid_t workers[CONCURRENT_PROCESSES];
	int status;
	int worker;
	int passed;
	int failed;

	/* Starts the workers, which fork at the same time. */
	for (worker = 0; worker < CONCURRENT_PROCESSES; worker++) {
		workers[worker] = fork();
		if (workers[worker] == 0)
			concurrent_worker();
	}

	/* Every worker must succeed. */
	passed = 1;
	for (worker = 0; worker < CONCURRENT_PROCESSES; worker++) {
		status = wait_status(workers[worker]);
		if (status != 0)
			passed = 0;
	}

	/* Reports the check. */
	failed = report("concurrent forks", passed);
	return failed;
}

/*
 * Forks CONCURRENT_FORKS children one after another, each exiting with
 * a status of its own, and exits 0 when every status came back.
 */
static void
concurrent_worker(void)
{
	pid_t child;
	int status;
	int round;

	/* Forks each child and checks the status it exits with. */
	for (round = 0; round < CONCURRENT_FORKS; round++) {
		child = fork();
		if (child == 0)
			_exit(round % 7);

		/* A wrong status fails the worker. */
		status = wait_status(child);
		if (status != round % 7)
			_exit(1);
	}

	/* Every child came back as it should. */
	_exit(0);
}

/* Prints a check's line; returns 1 for a failure and 0 for a pass. */
static int
report(
	const char *name,
	int passed)
{
	/* A failed check. */
	if (!passed) {
		printf("%s: FAIL\n", name);
		return 1;
	}

	/* Succeeded: the check passed. */
	printf("%s: ok\n", name);
	return 0;
}

/* Waits for a child and returns its exit status, or -1. */
static int
wait_status(
	pid_t child)
{
	pid_t done;
	int raw;
	int exited;

	/* Waits through interruptions. */
	do {
		done = waitpid(child, &raw, 0);
	} while (done < 0 && errno == EINTR);

	/* A failed wait has no status. */
	if (done < 0)
		return -1;

	/* A child that did not exit (a signal ended it) has none either. */
	exited = WIFEXITED(raw);
	if (!exited)
		return -1;

	/* Succeeded: the exit status. */
	return WEXITSTATUS(raw);
}
