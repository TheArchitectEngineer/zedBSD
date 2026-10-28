/*
 * ws308-p006: exec of a multi-threaded process while signals arrive.
 *
 *	sigexec [ROUNDS]
 *
 * Each round forks a child that starts three threads making system calls in a loop (with a SIGUSR1 handler) and
 * then calls execve() of this program from a fourth thread with SIGUSR1 blocked.  Meanwhile the parent sends the
 * child SIGUSR1, SIGSTOP and SIGCONT many times.  The new image ("sigexec target") installs its own SIGUSR1
 * handler, unblocks SIGUSR1 (a signal that came during exec is still pending, the mask survives exec), and exits 0.
 * In every fourth round the parent sends SIGKILL at a random moment instead of letting the exec finish, and then
 * the child must have exited 0 or died by SIGKILL.  A round that takes more than 20 s is a hang.
 * Prints "sigexec rounds=N exec_ok=N killed=N bad=N hang=N" and "sigexec: PASS" or "sigexec: FAIL".
 * Builds on Linux as well (cc -pthread), where it passes.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *self_path;
static volatile sig_atomic_t usr1_seen;

/* Counts a SIGUSR1 in either image. */
static void
on_usr1(int signo)
{
	(void)signo;
	usr1_seen++;
}

/* A thread of the old image: takes SIGUSR1, and makes system calls until the exec ends it. */
static void *
busy(void *argument)
{
	sigset_t mask;

	(void)argument;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
	for (;;) {
		(void)getpid();
		(void)sched_yield();
	}
	return NULL;
}

/* The exec'ing thread: SIGUSR1 blocked, then exec of the target. */
static void *
execer(void *argument)
{
	sigset_t mask;
	char *args[3];

	(void)argument;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
	usleep(1000 + (unsigned)(getpid() % 4) * 500U);
	args[0] = (char *)self_path;
	args[1] = "target";
	args[2] = NULL;
	execv(self_path, args);
	_exit(3);
	return NULL;
}

/* The child of a round (SIGUSR1 blocked from the parent): three busy threads and the exec'ing one. */
static void
child(void)
{
	struct sigaction action;
	pthread_t threads[4];
	unsigned index;

	memset(&action, 0, sizeof(action));
	action.sa_handler = on_usr1;
	sigaction(SIGUSR1, &action, NULL);
	for (index = 0; index < 3; index++)
		pthread_create(&threads[index], NULL, busy, NULL);
	pthread_create(&threads[3], NULL, execer, NULL);
	for (;;)
		pause();
}

/* The new image: takes the pending SIGUSR1, then exits 0. */
static int
target(void)
{
	struct sigaction action;
	sigset_t mask;

	memset(&action, 0, sizeof(action));
	action.sa_handler = on_usr1;
	sigaction(SIGUSR1, &action, NULL);
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	usleep(500);
	return 0;
}

/* Milliseconds of the monotonic clock. */
static long
now_ms(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (long)time.tv_sec * 1000L + time.tv_nsec / 1000000L;
}

int
main(int argc, char **argv)
{
	int rounds = 300;
	int round;
	int status;
	int exec_ok = 0;
	int killed = 0;
	int bad = 0;
	int hang = 0;
	int kill_round;
	unsigned seed = 12345;
	long started;
	pid_t pid;
	pid_t got;
	sigset_t usr1_mask;

	self_path = argv[0];
	if (argc > 1 && strcmp(argv[1], "target") == 0)
		return target();
	if (argc > 1)
		rounds = atoi(argv[1]);
	setvbuf(stdout, NULL, _IONBF, 0);

	/* A child is born with SIGUSR1 blocked, until it has a handler for it. */
	sigemptyset(&usr1_mask);
	sigaddset(&usr1_mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &usr1_mask, NULL);
	for (round = 0; round < rounds; round++) {
		kill_round = round % 4 == 3;
		pid = fork();
		if (pid == 0)
			child();
		if (pid < 0) {
			bad++;
			continue;
		}

		/* Signals until the child is gone (or, in a kill round, until the random moment). */
		started = now_ms();
		seed = seed * 1103515245U + 12345U;
		got = 0;
		while (got == 0) {
			if (kill_round && now_ms() - started >= (long)(seed % 8U)) {
				kill(pid, SIGKILL);
				kill_round = 0;
			}
			kill(pid, SIGUSR1);
			if ((seed >> 8) % 4U == 0) {
				kill(pid, SIGSTOP);
				kill(pid, SIGCONT);
			}
			seed = seed * 1103515245U + 12345U;
			got = waitpid(pid, &status, WNOHANG);
			if (now_ms() - started > 20000) {
				hang++;
				kill(pid, SIGKILL);
				got = waitpid(pid, &status, 0);
				break;
			}
		}
		if (got != pid) {
			bad++;
			continue;
		}
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			exec_ok++;
		else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
			killed++;
		else {
			if (bad == 0)
				printf("sigexec first bad status=0x%x\n", status);
			bad++;
		}
	}
	printf("sigexec rounds=%d exec_ok=%d killed=%d bad=%d hang=%d\n", rounds, exec_ok, killed, bad, hang);
	printf("sigexec: %s\n", bad == 0 && hang == 0 && exec_ok > rounds / 2 ? "PASS" : "FAIL");
	return bad == 0 && hang == 0 ? 0 : 1;
}
