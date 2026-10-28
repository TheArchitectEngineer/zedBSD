/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Random reads of a disk or file for a fixed time, at a queue depth made of
 * that many threads, each with one read outstanding.  It builds on Linux
 * (cc -O2 -pthread) and on zedBSD (build-randread.sh), so both systems are
 * measured the same way.
 *
 *	randread FILE BLOCK DEPTH SECONDS OFFSET_MIB LENGTH_MIB [direct] [fdeach]
 *	    [procs] [notime] [nullcall]
 *
 * Each thread reads BLOCK bytes at block-aligned offsets drawn from its own
 * xorshift sequence inside [OFFSET_MIB, OFFSET_MIB + LENGTH_MIB).  "direct"
 * opens with O_DIRECT where the system has it; "fdeach" gives each thread
 * its own open of FILE instead of sharing one.  "procs" runs the readers as
 * that many processes instead of threads of one (each opens FILE itself);
 * "notime" leaves the clock out of the loop (the mean and largest read times
 * are then 0); "nullcall" makes each read a getpid() instead, the cost of a
 * system call alone at that concurrency.  One line reports the
 * reads, the bytes per second, and the mean and largest read time:
 *
 *	randread bs=4096 qd=32 reads=N MBps=X iops=Y mean_us=Z max_us=W
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The most threads one run takes. */
#define RANDREAD_MAX_DEPTH 64

/* The alignment of the read buffers, enough for O_DIRECT. */
#define RANDREAD_ALIGN 4096

struct worker {
	pthread_t thread;
	pid_t pid;
	int fd;
	uint64_t state;
	uint64_t reads;
	uint64_t busy_ns;
	uint64_t max_ns;
	int failed;
};

static int fd;
static size_t block;
static uint64_t first_block;
static uint64_t block_count;
static volatile int *stopping;
static int timed = 1;
static int nullcall;

static uint64_t now_ns(void);
static void *run(void *argument);

/*
 * Starts DEPTH threads, lets them read for SECONDS, and reports the sum.
 */
int
main(
	int argc,
	char **argv)
{
	struct worker *workers;
	uint64_t started;
	uint64_t elapsed;
	uint64_t reads;
	uint64_t busy;
	uint64_t largest;
	unsigned long seconds;
	unsigned long offset_mib;
	unsigned long length_mib;
	int depth;
	int flags;
	int each = 0;
	int procs = 0;
	int status;
	int index;
	struct timespec pause;
	char line[256];
	int length;

	if (argc < 7) {
		fprintf(stderr, "usage: randread FILE BLOCK DEPTH SECONDS OFFSET_MIB LENGTH_MIB [direct]\n");
		return 2;
	}
	block = strtoul(argv[2], NULL, 0);
	depth = atoi(argv[3]);
	seconds = strtoul(argv[4], NULL, 0);
	offset_mib = strtoul(argv[5], NULL, 0);
	length_mib = strtoul(argv[6], NULL, 0);
	if (block == 0 || block % 512 != 0 || depth < 1 || depth > RANDREAD_MAX_DEPTH ||
	    seconds == 0 || length_mib == 0) {
		fprintf(stderr, "randread: bad arguments\n");
		return 2;
	}

	flags = O_RDONLY;
#ifdef O_DIRECT
	for (index = 7; index < argc; index++) {
		if (strcmp(argv[index], "direct") == 0)
			flags |= O_DIRECT;
	}
#endif
	for (index = 7; index < argc; index++) {
		if (strcmp(argv[index], "fdeach") == 0)
			each = 1;
		if (strcmp(argv[index], "procs") == 0)
			procs = 1;
		if (strcmp(argv[index], "notime") == 0)
			timed = 0;
		if (strcmp(argv[index], "nullcall") == 0)
			nullcall = 1;
	}

	/* The workers' records and the stop flag are shared with reader processes. */
	workers = mmap(NULL, sizeof(*workers) * RANDREAD_MAX_DEPTH + sizeof(int),
	    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (workers == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	stopping = (volatile int *)(workers + RANDREAD_MAX_DEPTH);
	*stopping = 0;
	fd = open(argv[1], flags);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	first_block = ((uint64_t)offset_mib << 20) / block;
	block_count = ((uint64_t)length_mib << 20) / block;

	/* Starts the readers, each with its own sequence. */
	started = now_ns();
	for (index = 0; index < depth; index++) {
		memset(&workers[index], 0, sizeof(workers[index]));
		workers[index].state = 0x9e3779b97f4a7c15ULL * (uint64_t)(index + 1);
		workers[index].fd = each || procs ? open(argv[1], flags) : fd;
		if (workers[index].fd < 0) {
			perror(argv[1]);
			return 1;
		}
		if (procs) {
			workers[index].pid = fork();
			if (workers[index].pid == 0) {
				(void)run(&workers[index]);
				_exit(0);
			}
			if (workers[index].pid < 0) {
				perror("fork");
				return 1;
			}
			continue;
		}
		if (pthread_create(&workers[index].thread, NULL, run, &workers[index]) != 0) {
			fprintf(stderr, "randread: pthread_create failed\n");
			return 1;
		}
	}

	pause.tv_sec = (time_t)seconds;
	pause.tv_nsec = 0;
	nanosleep(&pause, NULL);
	*stopping = 1;

	/* Sums what the readers did. */
	reads = 0;
	busy = 0;
	largest = 0;
	for (index = 0; index < depth; index++) {
		if (procs)
			(void)waitpid(workers[index].pid, &status, 0);
		else
			pthread_join(workers[index].thread, NULL);
		if (workers[index].failed) {
			fprintf(stderr, "randread: a read failed\n");
			return 1;
		}
		reads += workers[index].reads;
		busy += workers[index].busy_ns;
		if (workers[index].max_ns > largest)
			largest = workers[index].max_ns;
	}
	elapsed = now_ns() - started;
	if (reads == 0) {
		fprintf(stderr, "randread: no reads\n");
		return 1;
	}

	/* One line, written at once (no stdio buffer left to flush at exit). */
	length = snprintf(line, sizeof(line),
	    "randread bs=%lu qd=%d reads=%llu MBps=%llu.%llu iops=%llu mean_us=%llu.%llu max_us=%llu\n",
	    (unsigned long)block, depth, (unsigned long long)reads,
	    (unsigned long long)(reads * block * 1000U / elapsed),
	    (unsigned long long)(reads * block * 10000U / elapsed % 10U),
	    (unsigned long long)(reads * 1000000000U / elapsed),
	    (unsigned long long)(busy / reads / 1000U),
	    (unsigned long long)(busy / reads / 100U % 10U),
	    (unsigned long long)(largest / 1000U));
	if (length > 0)
		(void)write(1, line, (size_t)length);
	return 0;
}

/*
 * Reads random blocks until the main thread says stop.
 */
static void *
run(
	void *argument)
{
	struct worker *worker;
	void *buffer;
	uint64_t x;
	uint64_t before;
	uint64_t spent;
	off_t where;

	worker = argument;
	if (posix_memalign(&buffer, RANDREAD_ALIGN, block) != 0) {
		worker->failed = 1;
		return NULL;
	}
	while (!*stopping) {
		/* xorshift64* */
		x = worker->state;
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		worker->state = x;
		where = (off_t)((first_block + (x * 0x2545f4914f6cdd1dULL) % block_count) * block);

		before = timed ? now_ns() : 0;
		if (nullcall) {
			(void)getpid();
		} else if (pread(worker->fd, buffer, block, where) != (ssize_t)block) {
			worker->failed = 1;
			break;
		}
		if (timed) {
			spent = now_ns() - before;
			worker->busy_ns += spent;
			if (spent > worker->max_ns)
				worker->max_ns = spent;
		}
		worker->reads++;
	}
	free(buffer);
	return NULL;
}

/* Reads the monotonic clock in nanoseconds. */
static uint64_t
now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}
