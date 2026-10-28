/*
 * ws308-p004: credential changes against other threads of the same process.  Run as root.
 *
 *	credrace [ROUNDS]
 *
 * - lost: two threads of a root process call setuid(1000) and seteuid(0) at once.  Whatever the order, the
 *   process must end with all three user IDs 1000 (a seteuid(0) built from the root credential must not
 *   overwrite the setuid(1000) that returned first).
 * - drop: thread B calls setuid(1000) and then raises a flag; thread A, once it sees the flag, must no longer
 *   open, chmod or signal as root (open of a root-only file, chmod of it and kill(1, 0) fail).
 * - sleeper: thread A sleeps in read() on a pipe while B calls setuid(1000) and then writes the pipe; A's next
 *   call after the read must be refused the same way.
 * - siginfo: a child calls setuid(1234) and exits; the parent's SIGCHLD carries si_uid 1234 (the child's real
 *   user ID), and a sigqueue() from a process with real ID 1234 and effective ID 0 carries 1234 too.
 * Each round of lost, drop and sleeper runs in a fresh child process.  Prints one line per check and
 * "credrace: PASS" or "credrace: FAIL".  Builds on Linux as well (cc -pthread), where it passes.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SECRET "/tmp/credrace-secret"
#define USER 1000
#define CHILD_USER 1234

static pthread_barrier_t barrier;
static volatile int flag;
static int pipe_fds[2];

/* lost: the thread that drops to USER for good. */
static void *
lost_setuid(void *argument)
{
	(void)argument;
	pthread_barrier_wait(&barrier);
	if (setuid(USER) != 0)
		return (void *)1;
	return NULL;
}

/* lost: the thread that sets the effective ID it already has, from a credential that may be stale. */
static void *
lost_seteuid(void *argument)
{
	(void)argument;
	pthread_barrier_wait(&barrier);
	(void)seteuid(0);
	return NULL;
}

/* One lost round in a child: exits 0 when all three IDs ended as USER. */
static int
lost_round(void)
{
	pthread_t a, b;
	uid_t r, e, s;

	pthread_barrier_init(&barrier, NULL, 2);
	pthread_create(&a, NULL, lost_setuid, NULL);
	pthread_create(&b, NULL, lost_seteuid, NULL);
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	if (getresuid(&r, &e, &s) != 0)
		return 2;
	return r == USER && e == USER && s == USER ? 0 : 1;
}

/* Tries what only root may do; reports 0 when every attempt was refused. */
static int
refused_as_user(void)
{
	int fd;
	int bad = 0;

	fd = open(SECRET, O_RDONLY);
	if (fd >= 0) {
		close(fd);
		bad |= 1;
	}
	if (chmod(SECRET, 0666) == 0)
		bad |= 2;
	if (kill(1, 0) == 0)
		bad |= 4;
	return bad;
}

/* drop: B drops to USER and raises the flag. */
static void *
drop_setuid(void *argument)
{
	(void)argument;
	pthread_barrier_wait(&barrier);
	if (setuid(USER) != 0)
		return (void *)1;
	__atomic_store_n(&flag, 1, __ATOMIC_RELEASE);
	return NULL;
}

/* drop: A waits for the flag, then must be refused. */
static void *
drop_try(void *argument)
{
	(void)argument;
	pthread_barrier_wait(&barrier);
	while (__atomic_load_n(&flag, __ATOMIC_ACQUIRE) == 0)
		;
	return (void *)(long)refused_as_user();
}

/* sleeper: A sleeps in read() until B, having dropped, writes. */
static void *
sleeper_read(void *argument)
{
	char byte;

	(void)argument;
	if (read(pipe_fds[0], &byte, 1) != 1)
		return (void *)8;
	return (void *)(long)refused_as_user();
}

/* sleeper: B drops to USER, then wakes A. */
static void *
sleeper_setuid(void *argument)
{
	(void)argument;
	usleep(2000);
	if (setuid(USER) != 0)
		return (void *)1;
	if (write(pipe_fds[1], "x", 1) != 1)
		return (void *)1;
	return NULL;
}

/* One drop or sleeper round in a child: exits with the refusal bits of A (0 is a pass). */
static int
thread_round(int sleeper)
{
	pthread_t a, b;
	void *a_result;
	void *b_result;

	flag = 0;
	pthread_barrier_init(&barrier, NULL, 2);
	if (sleeper) {
		if (pipe(pipe_fds) != 0)
			return 16;
		pthread_create(&a, NULL, sleeper_read, NULL);
		pthread_create(&b, NULL, sleeper_setuid, NULL);
	} else {
		pthread_create(&a, NULL, drop_try, NULL);
		pthread_create(&b, NULL, drop_setuid, NULL);
	}
	pthread_join(a, &a_result);
	pthread_join(b, &b_result);
	if (b_result != NULL)
		return 32;
	return (int)(long)a_result;
}

/* Runs ROUNDS children of one kind; prints the count of failures. */
static int
run_rounds(const char *name, int kind, int rounds)
{
	int failures = 0;
	int first = 0;
	int i;
	int status;
	pid_t pid;

	for (i = 0; i < rounds; i++) {
		pid = fork();
		if (pid == 0) {
			if (kind == 0)
				_exit(lost_round());
			_exit(thread_round(kind == 2));
		}
		if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			if (failures == 0)
				first = pid < 0 ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
			failures++;
		}
	}
	printf("credrace %s rounds=%d failures=%d first=%d\n", name, rounds, failures, first);
	return failures;
}

static volatile uid_t chld_uid = (uid_t)-1;
static volatile uid_t queue_uid = (uid_t)-1;

/* Records the sender's user ID of SIGCHLD and of the queued SIGUSR1. */
static void
on_signal(int signo, siginfo_t *info, void *context)
{
	(void)context;
	if (signo == SIGCHLD && info->si_code == CLD_EXITED)
		chld_uid = info->si_uid;
	if (signo == SIGUSR1 && info->si_code == SI_QUEUE)
		queue_uid = info->si_uid;
}

/* siginfo: SIGCHLD and sigqueue() carry the real user ID of the sender. */
static int
siginfo_check(int rounds)
{
	struct sigaction action;
	sigset_t mask, old;
	union sigval value;
	int failures = 0;
	int i;
	pid_t pid;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = on_signal;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &action, NULL);
	sigaction(SIGUSR1, &action, NULL);
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGUSR1);
	for (i = 0; i < rounds; i++) {
		chld_uid = (uid_t)-1;
		queue_uid = (uid_t)-1;
		sigprocmask(SIG_BLOCK, &mask, &old);
		pid = fork();
		if (pid == 0) {
			/* Real ID CHILD_USER, effective 0, then the queued signal, then all IDs CHILD_USER. */
			if (setresuid(CHILD_USER, 0, 0) != 0)
				_exit(1);
			value.sival_int = 0;
			if (sigqueue(getppid(), SIGUSR1, value) != 0)
				_exit(1);
			if (setuid(CHILD_USER) != 0)
				_exit(1);
			_exit(0);
		}
		if (pid < 0) {
			failures++;
			sigprocmask(SIG_SETMASK, &old, NULL);
			continue;
		}
		/* Lets the signals in only after the child is gone, then reaps it. */
		while (waitid(P_PID, pid, NULL, WEXITED | WNOWAIT) != 0 && errno == EINTR)
			;
		sigprocmask(SIG_SETMASK, &old, NULL);
		while (waitpid(pid, NULL, 0) != pid && errno == EINTR)
			;
		if (chld_uid != CHILD_USER || queue_uid != CHILD_USER) {
			if (failures == 0)
				printf("credrace siginfo first: chld_uid=%ld queue_uid=%ld\n", (long)chld_uid,
				    (long)queue_uid);
			failures++;
		}
	}
	printf("credrace siginfo rounds=%d failures=%d\n", rounds, failures);
	return failures;
}

int
main(int argc, char **argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 2000;
	int failures = 0;
	int fd;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (geteuid() != 0) {
		printf("credrace: FAIL (run as root)\n");
		return 1;
	}
	fd = open(SECRET, O_CREAT | O_WRONLY | O_TRUNC, 0600);
	if (fd < 0 || fchown(fd, 0, 0) != 0 || fchmod(fd, 0600) != 0) {
		printf("credrace: FAIL (cannot make %s)\n", SECRET);
		return 1;
	}
	close(fd);
	failures += run_rounds("lost", 0, rounds);
	failures += run_rounds("drop", 1, rounds);
	failures += run_rounds("sleeper", 2, rounds / 10 + 1);
	failures += siginfo_check(rounds / 10 + 1);
	printf("credrace: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
