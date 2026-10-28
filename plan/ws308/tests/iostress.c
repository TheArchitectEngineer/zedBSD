/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Reads a disk or file from many threads while the address space and the
 * process change under them, for the read and write system calls' pinning
 * of user buffers (ws308-p006).  It builds like randread.c (build-randread.sh
 * builds every .c it is given; cc -O2 -pthread on Linux).
 *
 *	iostress FILE SECONDS [direct] [hog MIB] [setuid] [size MIB]
 *
 * For SECONDS it runs at once:
 *   - 8 readers: pread of 4 KiB, or 8 KiB across a page boundary, into a
 *     shared 64-page region at random places; EFAULT is counted when the
 *     place was unmapped or read-only at that moment (with a short read,
 *     which a system that copies page by page returns), any other failure
 *     is an error;
 *   - a mapper: munmap and MAP_FIXED mmap of 4 pages of the region, and
 *     mprotect of 4 pages to read-only and back;
 *   - a checker: preadv with 16 vectors of 300 bytes across pages, compared
 *     with one pread of the same range;
 *   - a forker: fork, the child reads into the middle of a private area with
 *     pread, checks the pattern around the read and the data against a
 *     fresh pread, writes its copy of a page of the parent (which the parent
 *     must not see), and exits 0 on success;
 *   - with "hog MIB": a child process that keeps touching MIB MiB of memory
 *     (pushes pages to swap);
 *   - with "setuid" (as root): a thread that switches the effective user
 *     between 1 and 0 while the readers run on descriptors opened before.
 * The first 256 MiB of FILE are read; "size MIB" gives the size of a device
 * whose lseek does not.  One line reports the counts and ends with "result=PASS" or "result=FAIL".
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PAGE 4096
#define REGION_PAGES 64
#define READERS 8
#define VECTORS 16
#define VECTOR_BYTES 300

static int fd;
static uint64_t blocks;
static unsigned char *region;
static unsigned char *cow_page;
static volatile int stopping;
static int use_setuid;
static unsigned long reads;
static unsigned long faults;
static unsigned long errors;
static unsigned long mapper_rounds;
static unsigned long vector_checks;
static unsigned long vector_mismatches;
static unsigned long forks;
static unsigned long fork_failures;
static unsigned long setuid_rounds;
static unsigned long setuid_errors;
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;

/* xorshift64* of a caller's state. */
static uint64_t
next_random(uint64_t *state)
{
	uint64_t x = *state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545f4914f6cdd1dULL;
}

static void
count(unsigned long *counter, unsigned long amount)
{
	pthread_mutex_lock(&count_lock);
	*counter += amount;
	pthread_mutex_unlock(&count_lock);
}

/* Reads at random places of the shared region. */
static void *
reader(void *argument)
{
	uint64_t state = (uint64_t)(uintptr_t)argument * 0x9e3779b97f4a7c15ULL + 1U;
	unsigned long done = 0;
	unsigned long faulted = 0;
	unsigned long failed = 0;
	size_t length;
	size_t page;
	ssize_t got;

	while (!stopping) {
		length = (next_random(&state) & 1U) ? 2U * PAGE : PAGE;
		page = next_random(&state) % (REGION_PAGES - 2U);
		got = pread(fd, region + page * PAGE + (length == PAGE ? 0 : PAGE / 2), length,
		    (off_t)((next_random(&state) % (blocks - 2U)) * PAGE));
		if (got == (ssize_t)length)
			done++;
		else if ((got < 0 && errno == EFAULT) || (got > 0 && got < (ssize_t)length))
			faulted++;
		else
			failed++;
	}
	count(&reads, done);
	count(&faults, faulted);
	count(&errors, failed);
	return NULL;
}

/* Unmaps, maps again and write-protects parts of the region. */
static void *
mapper(void *argument)
{
	uint64_t state = 77;
	unsigned long rounds = 0;
	unsigned long failed = 0;
	unsigned char *at;
	void *mapped;

	(void)argument;
	while (!stopping) {
		at = region + (next_random(&state) % (REGION_PAGES - 4U)) * PAGE;
		if (munmap(at, 4U * PAGE) != 0)
			failed++;
		mapped = mmap(at, 4U * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
		if (mapped != at)
			failed++;
		at = region + (next_random(&state) % (REGION_PAGES - 4U)) * PAGE;
		if (mprotect(at, 4U * PAGE, PROT_READ) != 0)
			failed++;
		usleep(100);
		if (mprotect(at, 4U * PAGE, PROT_READ | PROT_WRITE) != 0)
			failed++;
		rounds++;
	}
	count(&mapper_rounds, rounds);
	count(&errors, failed);
	return NULL;
}

/* Reads with 16 vectors across pages and compares with one plain read. */
static void *
checker(void *argument)
{
	static unsigned char vectored[VECTORS * VECTOR_BYTES + PAGE] __attribute__((aligned(PAGE)));
	static unsigned char plain[VECTORS * VECTOR_BYTES];
	struct iovec vectors[VECTORS];
	uint64_t state = 99;
	unsigned long checks = 0;
	unsigned long mismatches = 0;
	unsigned long failed = 0;
	unsigned index;
	off_t offset;

	(void)argument;
	while (!stopping) {
		for (index = 0; index < VECTORS; index++) {
			/* Every vector starts 100 bytes before a page boundary or past it, so that most cross one. */
			vectors[index].iov_base = vectored + index * VECTOR_BYTES + 100U;
			vectors[index].iov_len = VECTOR_BYTES;
		}
		offset = (off_t)((next_random(&state) % (blocks - 4U)) * PAGE);
		if (preadv(fd, vectors, VECTORS, offset) != VECTORS * VECTOR_BYTES ||
		    pread(fd, plain, sizeof(plain), offset) != (ssize_t)sizeof(plain)) {
			failed++;
			continue;
		}
		if (memcmp(vectored + 100U, plain, sizeof(plain)) != 0)
			mismatches++;
		checks++;
	}
	count(&vector_checks, checks);
	count(&vector_mismatches, mismatches);
	count(&errors, failed);
	return NULL;
}

/* The child of a fork: its own copy of the region, read into and checked. */
static int
child_check(uint64_t seed)
{
	static unsigned char fresh[PAGE] __attribute__((aligned(PAGE)));
	unsigned char *copy;
	off_t offset;
	unsigned index;

	copy = mmap(NULL, 4U * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (copy == MAP_FAILED)
		return 2;
	memset(copy, 0xa5, 4U * PAGE);
	offset = (off_t)((seed % (blocks - 2U)) * PAGE);
	if (pread(fd, copy + PAGE, PAGE, offset) != PAGE || pread(fd, fresh, PAGE, offset) != PAGE)
		return 3;
	for (index = 0; index < PAGE; index++) {
		if (copy[index] != 0xa5 || copy[2U * PAGE + index] != 0xa5)
			return 4;
	}
	if (memcmp(copy + PAGE, fresh, PAGE) != 0)
		return 5;

	/* Writes its copy of the parent's page, which the parent must not see. */
	if (cow_page[0] != 0x11)
		return 6;
	memset(cow_page, 0x5a, PAGE);
	return 0;
}

/* Forks children that check their own copies. */
static void *
forker(void *argument)
{
	uint64_t state = 1234;
	unsigned long made = 0;
	unsigned long failed = 0;
	int status;
	pid_t pid;

	(void)argument;
	while (!stopping) {
		pid = fork();
		if (pid == 0)
			_exit(child_check(next_random(&state)));
		next_random(&state);
		if (pid < 0) {
			failed++;
			usleep(1000);
			continue;
		}
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			failed++;
		made++;
	}
	count(&forks, made);
	count(&fork_failures, failed);
	return NULL;
}

/* Switches the effective user while the readers run. */
static void *
switcher(void *argument)
{
	unsigned long rounds = 0;
	unsigned long failed = 0;

	(void)argument;
	while (!stopping) {
		if (seteuid(1) != 0 || seteuid(0) != 0)
			failed++;
		rounds++;
	}
	count(&setuid_rounds, rounds);
	count(&setuid_errors, failed);
	return NULL;
}

/* Keeps touching a large private area until killed. */
static void
hog(size_t mib)
{
	unsigned char *area;
	size_t at;

	area = mmap(NULL, mib << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == MAP_FAILED)
		_exit(1);
	for (;;) {
		for (at = 0; at < (mib << 20); at += PAGE)
			area[at]++;
	}
}

int
main(int argc, char **argv)
{
	pthread_t threads[READERS + 4];
	unsigned thread_count = 0;
	unsigned index;
	int flags = O_RDONLY;
	size_t hog_mib = 0;
	size_t size_mib = 0;
	pid_t hog_pid = -1;
	off_t size;
	unsigned seen;
	int seconds;
	int ok;

	if (argc < 3) {
		fprintf(stderr, "usage: iostress FILE SECONDS [direct] [hog MIB] [setuid] [size MIB]\n");
		return 2;
	}
	seconds = atoi(argv[2]);
	for (index = 3; index < (unsigned)argc; index++) {
		if (strcmp(argv[index], "direct") == 0)
			flags |= O_DIRECT;
		else if (strcmp(argv[index], "hog") == 0 && index + 1U < (unsigned)argc)
			hog_mib = (size_t)atol(argv[++index]);
		else if (strcmp(argv[index], "setuid") == 0)
			use_setuid = 1;
		else if (strcmp(argv[index], "size") == 0 && index + 1U < (unsigned)argc)
			size_mib = (size_t)atol(argv[++index]);
	}
	fd = open(argv[1], flags);
	if (fd < 0) {
		perror("open");
		return 2;
	}
	/* A block device may not say its size by lseek; "size MIB" says it. */
	size = size_mib != 0 ? (off_t)size_mib << 20 : lseek(fd, 0, SEEK_END);
	if (size < 64 * PAGE) {
		fprintf(stderr, "iostress: %s is too small\n", argv[1]);
		return 2;
	}
	if (size > (off_t)256 << 20)
		size = (off_t)256 << 20;
	blocks = (uint64_t)size / PAGE;
	region = mmap(NULL, REGION_PAGES * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED) {
		perror("mmap");
		return 2;
	}
	memset(region, 0, REGION_PAGES * PAGE);
	cow_page = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (cow_page == MAP_FAILED) {
		perror("mmap");
		return 2;
	}
	memset(cow_page, 0x11, PAGE);
	if (hog_mib != 0) {
		hog_pid = fork();
		if (hog_pid == 0)
			hog(hog_mib);
	}

	for (index = 0; index < READERS; index++)
		pthread_create(&threads[thread_count++], NULL, reader, (void *)(uintptr_t)(index + 1U));
	pthread_create(&threads[thread_count++], NULL, mapper, NULL);
	pthread_create(&threads[thread_count++], NULL, checker, NULL);
	pthread_create(&threads[thread_count++], NULL, forker, NULL);
	if (use_setuid)
		pthread_create(&threads[thread_count++], NULL, switcher, NULL);
	sleep((unsigned)seconds);
	stopping = 1;
	for (index = 0; index < thread_count; index++)
		pthread_join(threads[index], NULL);
	if (hog_pid > 0) {
		kill(hog_pid, SIGKILL);
		waitpid(hog_pid, NULL, 0);
	}

	/* The parent's region never shows a child's write. */
	seen = 0;
	for (index = 0; index < PAGE; index++)
		seen += cow_page[index] == 0x11;
	ok = errors == 0 && vector_mismatches == 0 && fork_failures == 0 && setuid_errors == 0 &&
	    reads != 0 && vector_checks != 0 && forks != 0 && seen == PAGE;
	printf("iostress reads=%lu efault=%lu errors=%lu mapper=%lu vectors=%lu mismatches=%lu forks=%lu "
	    "fork_failures=%lu setuid=%lu setuid_errors=%lu result=%s\n",
	    reads, faults, errors, mapper_rounds, vector_checks, vector_mismatches, forks, fork_failures,
	    setuid_rounds, setuid_errors, ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
