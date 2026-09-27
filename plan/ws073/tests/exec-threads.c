/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-068 probe: a process with other threads alive calls execve of itself;
 * the new image prints EXEC:PASS and exits 0.
 *
 *   exec-threads MODE
 *
 * MODE is "plain" (no other thread), "thread" (one thread blocked in
 * sem_wait), "detached" or "detach" (the same thread created detached, or
 * detached after it started), "reaper" (a detached thread newer than
 * libc's reaper, which waits for it), "sleeper" (one thread in nanosleep), "timer"
 * (a SIGEV_THREAD timer that has fired, so its worker thread exists), or
 * "spawn" (the timer, then a posix_spawn child waited for, like
 * POSIX-R2.ELF's last steps).
 */

#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static sem_t never;
static sem_t fired;

/* Waits for a post that never comes. */
static void *
blocked_thread(
	void *argument)
{
	(void)argument;
	sem_wait(&never);
	return NULL;
}

/* Ends at once. */
static void *
quick_thread(
	void *argument)
{
	return argument;
}

/* Sleeps for a long time. */
static void *
sleeping_thread(
	void *argument)
{
	(void)argument;
	sleep(1000);
	return NULL;
}

/* Tells the main thread that the timer fired. */
static void
timer_callback(
	union sigval value)
{
	(void)value;
	sem_post(&fired);
}

/* Arms a SIGEV_THREAD timer and waits until its callback has run. */
static int
start_timer(void)
{
	struct sigevent event;
	struct itimerspec when;
	timer_t timer;

	memset(&event, 0, sizeof(event));
	event.sigev_notify = SIGEV_THREAD;
	event.sigev_notify_function = timer_callback;
	if (timer_create(CLOCK_MONOTONIC, &event, &timer) != 0) {
		perror("timer_create");
		return 1;
	}

	memset(&when, 0, sizeof(when));
	when.it_value.tv_nsec = 1000000;
	if (timer_settime(timer, 0, &when, NULL) != 0) {
		perror("timer_settime");
		return 1;
	}

	sem_wait(&fired);
	return 0;
}

/*
 * Runs the probe.
 */
int
main(
	int argc,
	char **argv)
{
	char *final_argv[] = {argv[0], "final", NULL};
	char *spawn_argv[] = {argv[0], "child", NULL};
	pthread_attr_t attributes;
	pthread_t thread;
	pid_t child;
	int status;

	/* The new image and the spawned child report and end. */
	if (argc > 1 && strcmp(argv[1], "final") == 0) {
		(void)write(1, "EXEC:PASS\n", 10);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "child") == 0)
		return 23;
	if (argc < 2) {
		fprintf(stderr, "usage: exec-threads plain|thread|detached|detach|reaper|sleeper|timer|spawn\n");
		return 2;
	}

	sem_init(&never, 0, 0);
	sem_init(&fired, 0, 0);

	/* Starts the other threads the mode asks for. */
	if (strcmp(argv[1], "thread") == 0)
		pthread_create(&thread, NULL, blocked_thread, NULL);
	if (strcmp(argv[1], "detached") == 0) {
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		pthread_create(&thread, &attributes, blocked_thread, NULL);
	}
	if (strcmp(argv[1], "detach") == 0) {
		pthread_create(&thread, NULL, blocked_thread, NULL);
		pthread_detach(thread);
	}
	if (strcmp(argv[1], "reaper") == 0) {
		/*
		 * A first detached thread starts libc's reaper; a second one,
		 * newer than the reaper, is then waited for by it in the
		 * kernel, as the timer worker of POSIX-R2.ELF is.
		 */
		pthread_create(&thread, NULL, quick_thread, NULL);
		pthread_detach(thread);
		usleep(100000);
		pthread_create(&thread, NULL, blocked_thread, NULL);
		pthread_detach(thread);
	}
	if (strcmp(argv[1], "sleeper") == 0)
		pthread_create(&thread, NULL, sleeping_thread, NULL);
	if (strcmp(argv[1], "timer") == 0 || strcmp(argv[1], "spawn") == 0) {
		if (start_timer() != 0)
			return 1;
	}
	if (strcmp(argv[1], "spawn") == 0) {
		if (posix_spawn(&child, argv[0], NULL, NULL, spawn_argv, environ) != 0 ||
		    waitpid(child, &status, 0) != child ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 23) {
			fprintf(stderr, "spawn failed\n");
			return 1;
		}
	}

	/* Lets the threads reach their waits, then replaces the image. */
	usleep(100000);
	(void)write(1, "EXEC:START\n", 11);
	execve(argv[0], final_argv, environ);
	perror("execve");
	return 1;
}
