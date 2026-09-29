/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-051 probe driver: runs regcheck_spin (regcheck.S) in several
 * processes for SECONDS while other processes load the kernel with the
 * paths a forked SSH child meets: signals whose handler scrambles every
 * xmm register, fork and exit (copy-on-write of the stack), page faults
 * on fresh anonymous memory and plain system calls.
 *
 *   regcheck [SECONDS] [CHECKERS]
 *
 * Prints REGCHECK:PASS with the number of passes, or REGCHECK:FAIL and
 * the number of the register or stack slot that changed (100: a word of
 * the red zone below %rsp, which a signal frame must not overwrite).
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

uint64_t regcheck_spin(uint64_t iterations, uint64_t seed);
uint64_t regcheck_red_zone(uint64_t iterations, uint64_t seed);

static volatile sig_atomic_t signals_seen;

static void scramble(int signo);
static int checker(unsigned index, unsigned seconds);
static void load_forks(unsigned seconds);
static void load_faults(unsigned seconds);
static void load_signals(pid_t *targets, unsigned count, unsigned seconds);

/*
 * Runs the checkers and the load and reports the result.
 */
int
main(
	int argc,
	char **argv)
{
	unsigned seconds, checkers, i, failed;
	pid_t pids[64], loaders[3];
	struct sigaction action;
	int status;

	seconds = argc > 1 ? (unsigned)atoi(argv[1]) : 60;
	checkers = argc > 2 ? (unsigned)atoi(argv[2]) : 6;
	if (checkers > 64)
		checkers = 64;

	/* Installs the handler before any checker exists, so none can be ended by an early signal. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = scramble;
	action.sa_flags = SA_RESTART;
	(void)sigaction(SIGUSR1, &action, NULL);

	/* Starts the checkers. */
	for (i = 0; i < checkers; i++) {
		pids[i] = fork();
		if (pids[i] == 0)
			_exit(checker(i, seconds));
	}

	/* Starts the load. */
	loaders[0] = fork();
	if (loaders[0] == 0) {
		load_forks(seconds);
		_exit(0);
	}
	loaders[1] = fork();
	if (loaders[1] == 0) {
		load_faults(seconds);
		_exit(0);
	}
	loaders[2] = fork();
	if (loaders[2] == 0) {
		load_signals(pids, checkers, seconds);
		_exit(0);
	}

	/* Collects the checkers' results. */
	failed = 0;
	for (i = 0; i < checkers; i++) {
		(void)waitpid(pids[i], &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			printf("REGCHECK:FAIL checker %u status %#x\n", i, status);
			failed++;
		}
	}
	for (i = 0; i < 3; i++)
		(void)waitpid(loaders[i], &status, 0);

	/* Reports the result. */
	if (failed != 0) {
		printf("REGCHECK:FAIL %u of %u checkers\n", failed, checkers);
		return 1;
	}
	printf("REGCHECK:PASS %u checkers, %u s\n", checkers, seconds);
	return 0;
}

/* Scrambles every xmm register in a signal handler. */
static void
scramble(
	int signo)
{
	(void)signo;
	signals_seen++;
	__asm__ volatile(
		"pcmpeqd %%xmm0, %%xmm0\n\tmovdqa %%xmm0, %%xmm1\n\tmovdqa %%xmm0, %%xmm2\n\t"
		"movdqa %%xmm0, %%xmm3\n\tmovdqa %%xmm0, %%xmm4\n\tmovdqa %%xmm0, %%xmm5\n\t"
		"movdqa %%xmm0, %%xmm6\n\tmovdqa %%xmm0, %%xmm7\n\tmovdqa %%xmm0, %%xmm8\n\t"
		"movdqa %%xmm0, %%xmm9\n\tmovdqa %%xmm0, %%xmm10\n\tmovdqa %%xmm0, %%xmm11\n\t"
		"movdqa %%xmm0, %%xmm12\n\tmovdqa %%xmm0, %%xmm13\n\tmovdqa %%xmm0, %%xmm14\n\t"
		"movdqa %%xmm0, %%xmm15"
		: : : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		  "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
}

/* Spins regcheck_spin until SECONDS have passed; returns the failing slot. */
static int
checker(
	unsigned index,
	unsigned seconds)
{
	time_t end;
	uint64_t seed, passes, result;

	end = time(NULL) + (time_t)seconds;
	seed = 0x1122334455667788ULL ^ ((uint64_t)index << 56);
	passes = 0;
	while (time(NULL) < end) {
		result = regcheck_spin(200000, seed + passes);
		if (result == 0 && regcheck_red_zone(200000, seed + passes) != 0)
			result = 100;
		if (result != 0) {
			printf("REGCHECK:FAIL checker %u pass %llu slot %llu signals %d\n", index,
			       (unsigned long long)passes, (unsigned long long)result, (int)signals_seen);
			fflush(stdout);
			return 1;
		}
		passes++;
	}
	printf("checker %u: %llu passes, %d signals\n", index, (unsigned long long)passes,
	       (int)signals_seen);
	fflush(stdout);
	return 0;
}

/* Forks children that write their stack and exit, for copy-on-write. */
static void
load_forks(
	unsigned seconds)
{
	time_t end;
	pid_t child;
	volatile char buffer[16384];
	int status;

	end = time(NULL) + (time_t)seconds;
	while (time(NULL) < end) {
		child = fork();
		if (child == 0) {
			memset((char *)buffer, 1, sizeof(buffer));
			_exit(buffer[100] == 1 ? 0 : 1);
		}
		if (child > 0)
			(void)waitpid(child, &status, 0);
	}
}

/* Maps, touches and unmaps anonymous memory, for page faults and TLB flushes. */
static void
load_faults(
	unsigned seconds)
{
	time_t end;
	char *p;
	size_t i, size;

	size = 4U << 20;
	end = time(NULL) + (time_t)seconds;
	while (time(NULL) < end) {
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p == MAP_FAILED)
			continue;
		for (i = 0; i < size; i += 4096)
			p[i] = (char)i;
		(void)munmap(p, size);
		(void)getpid();
	}
}

/* Sends SIGUSR1 to every checker, often. */
static void
load_signals(
	pid_t *targets,
	unsigned count,
	unsigned seconds)
{
	time_t end;
	unsigned i;
	struct timespec pause;

	pause.tv_sec = 0;
	pause.tv_nsec = 200000;
	end = time(NULL) + (time_t)seconds;
	while (time(NULL) < end) {
		for (i = 0; i < count; i++)
			(void)kill(targets[i], SIGUSR1);
		(void)nanosleep(&pause, NULL);
	}
}
