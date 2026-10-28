/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The O_DIRECT tool of the block device measurements and checks.  It builds
 * on Linux (cc -O2) and on zedBSD (build-randread.sh), so both systems move
 * the same bytes the same way.
 *
 *	directio read DEV MIB [BS_KIB [OFFSET_MIB]]
 *	directio write DEV MIB [BS_KIB [OFFSET_MIB]]
 *	directio check DEV FILE
 *	directio fillbench DEV
 *	directio fillmix DEV
 *
 * read and write move MIB MiB with O_DIRECT in BS_KIB requests (1024 by
 * default) from OFFSET_MIB of DEV, from a page-aligned buffer; write puts
 * a numbered pseudo-random pattern in every 4 KiB block (as patwrite does)
 * and ends with fsync.  One line reports the result:
 *
 *	directio read|write mib=M bs_kib=B ms=T MBps=X fill_ms=F io_ms=I sync_ms=S
 *	    moved=M/N pcore=P/N
 * (F: the time spent filling the buffer, I: in the reads or writes, S: in
 * the closing fsync; M of the N requests returned on another CPU than they
 * started on, P of them started on a hybrid part's performance core)
 *
 * fillbench writes the first MiB of DEV twice and prints the TSC ticks one
 * fill of a 1 MiB buffer takes before and after a direct write from it.
 *
 * fillmix writes the first 768 MiB of DEV and prints the ticks of one fill
 * of a buffer next to direct I/O from another buffer (fillmix()).
 *
 * check (zedBSD) destroys the first 16 MiB of DEV and runs the semantic
 * checks of O_DIRECT: refusals (a regular FILE, O_TRUNC on it, F_SETFL,
 * misaligned offsets and lengths, /dev/fd/N without the flag), round trips
 * through aligned, unaligned and shared buffers and 4 MiB requests, the
 * buffer cache's agreement both ways, F_SETFL toggling, readv/writev,
 * O_DSYNC, the device's end, and readers in two processes at once.  Every
 * check prints "ok NAME" or "FAIL NAME ..."; the last line is
 * "directio check: PASS" or "directio check: FAIL N".
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef O_DIRECT
#error "O_DIRECT is missing from <fcntl.h>"
#endif

#ifdef __ZEDBSD__
#include <uapi/block.h>
#endif

#define MIB		(1024U * 1024U)
#define BLOCK		4096U

static int failures;

static uint64_t
now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000U + (uint64_t)now.tv_nsec;
}

/* The split's clock: the time stamp counter on x86_64 (the system clock may tick in milliseconds). */
static uint64_t
split_now(void)
{
#if defined(__x86_64__)
	unsigned lo, hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
#else
	return now_ns();
#endif
}

/* The CPU the thread runs on (initial APIC ID) and whether it is a hybrid part's performance core. */
static void
split_where(unsigned *apic, int *pcore)
{
#if defined(__x86_64__)
	unsigned a, b, c, d;

	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0U), "c"(0U));
	*pcore = 0;
	if (a >= 0x1aU) {
		__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x1aU), "c"(0U));
		*pcore = (a >> 24) == 0x40U;
	}
	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1U), "c"(0U));
	*apic = b >> 24;
#else
	*apic = 0;
	*pcore = 0;
#endif
}

/* Fills a buffer with numbered 4 KiB blocks of a seeded xorshift pattern. */
static void
fill(unsigned char *buffer, size_t length, uint64_t seed, uint64_t number)
{
	uint64_t state;
	size_t at;

	/*
	 * Whole words with a copy of a constant size, which the compiler makes a
	 * store (a copy of a variable size calls the C library's memcpy for
	 * every word, which measures the library and not the I/O), then the
	 * tail.  The bytes are the same either way.
	 */
	state = seed * 0x9e3779b97f4a7c15ULL + 1U;
	for (at = 0; at + 8 <= length; at += 8) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(buffer + at, &state, 8);
	}
	if (at < length) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(buffer + at, &state, length - at);
	}
	for (at = 0; at + 8 <= length; at += BLOCK) {
		memcpy(buffer + at, &number, 8);
		number++;
	}
}

static void
result(int ok, const char *name, const char *detail)
{
	if (ok) {
		printf("ok %s\n", name);
	} else {
		printf("FAIL %s %s (errno %d)\n", name, detail, errno);
		failures++;
	}
	fflush(stdout);
}

static void *
aligned(size_t length)
{
	void *buffer;

	if (posix_memalign(&buffer, BLOCK, length) != 0) {
		perror("posix_memalign");
		exit(2);
	}
	return buffer;
}

/* Moves MIB MiB in BS requests and prints the rate. */
static int
measure(int write_mode, const char *path, unsigned long mib, unsigned long bs_kib,
    unsigned long offset_mib)
{
	unsigned char *buffer;
	uint64_t started;
	uint64_t elapsed;
	uint64_t number;
	uint64_t mark;
	uint64_t split0;
	uint64_t split_hz;
	uint64_t fill_ns;
	uint64_t io_ns;
	uint64_t sync_ns;
	uint64_t refill_ns;
	int refill;
	int trace;
	uint64_t trace_fill;
	uint64_t trace_io;
	unsigned apic;
	unsigned last_apic;
	unsigned moved;
	unsigned pcores;
	unsigned requests;
	int pcore;
	size_t bs;
	off_t at;
	off_t end;
	ssize_t done;
	int fd;

	bs = (size_t)bs_kib * 1024U;
	buffer = aligned(bs);
	fd = open(path, (write_mode ? O_WRONLY : O_RDONLY) | O_DIRECT);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	memset(buffer, 0, bs);
	at = (off_t)offset_mib * MIB;
	end = at + (off_t)mib * MIB;
	number = (uint64_t)at / BLOCK;
	fill_ns = 0;
	io_ns = 0;
	sync_ns = 0;
	refill_ns = 0;
	refill = getenv("DIRECTIO_REFILL") != NULL;
	trace = getenv("DIRECTIO_TRACE") != NULL;
	trace_fill = 0;
	trace_io = 0;
	started = now_ns();
	split0 = split_now();
	moved = 0;
	pcores = 0;
	requests = 0;
	split_where(&last_apic, &pcore);
	while (at < end) {
		split_where(&apic, &pcore);
		if (apic != last_apic)
			moved++;
		pcores += pcore != 0;
		requests++;
		mark = split_now();
		if (write_mode) {
			fill(buffer, bs, 7, number);
			number += bs / BLOCK;
			fill_ns += split_now() - mark;
			if (refill) {
				mark = split_now();
				fill(buffer, bs, 7, number - bs / BLOCK);
				refill_ns += split_now() - mark;
			}
			mark = split_now();
			done = pwrite(fd, buffer, bs, at);
		} else {
			done = pread(fd, buffer, bs, at);
		}
		io_ns += split_now() - mark;
		split_where(&last_apic, &pcore);
		if (trace && requests % 256U == 0U) {
			printf("directio trace request=%u fill_ticks=%llu io_ticks=%llu\n", requests,
			    (unsigned long long)((fill_ns - trace_fill) / 256U),
			    (unsigned long long)((io_ns - trace_io) / 256U));
			trace_fill = fill_ns;
			trace_io = io_ns;
		}
		if (done != (ssize_t)bs) {
			fprintf(stderr, "directio: %s at %lld: %zd\n", write_mode ? "pwrite" : "pread",
			    (long long)at, done);
			return 1;
		}
		at += (off_t)bs;
	}
	mark = split_now();
	if (write_mode && fsync(fd) != 0) {
		perror("fsync");
		return 1;
	}
	sync_ns = split_now() - mark;
	elapsed = now_ns() - started;
	split_hz = (uint64_t)((double)(split_now() - split0) * 1e9 / ((double)elapsed + 1.0));
	fill_ns = (uint64_t)((double)fill_ns * 1e9 / ((double)split_hz + 1.0));
	io_ns = (uint64_t)((double)io_ns * 1e9 / ((double)split_hz + 1.0));
	sync_ns = (uint64_t)((double)sync_ns * 1e9 / ((double)split_hz + 1.0));
	refill_ns = (uint64_t)((double)refill_ns * 1e9 / ((double)split_hz + 1.0));
	if (refill)
		printf("directio refill_ms=%llu (the same fill again at once, DIRECTIO_REFILL)\n",
		    (unsigned long long)(refill_ns / 1000000U));
	close(fd);
	printf("directio %s mib=%lu bs_kib=%lu ms=%llu MBps=%.1f fill_ms=%llu io_ms=%llu sync_ms=%llu"
	    " moved=%u/%u pcore=%u/%u\n",
	    write_mode ? "write" : "read", mib, bs_kib, (unsigned long long)(elapsed / 1000000U),
	    (double)mib * MIB / 1e6 / ((double)elapsed / 1e9), (unsigned long long)(fill_ns / 1000000U),
	    (unsigned long long)(io_ns / 1000000U), (unsigned long long)(sync_ns / 1000000U),
	    moved, requests, pcores, requests);
	fflush(stdout);
	return 0;
}

/* The device's size in bytes. */
static off_t
device_size(int fd)
{
#ifdef __ZEDBSD__
	struct kern_block_info info;

	memset(&info, 0, sizeof(info));
	info.version = KERN_BLOCK_VERSION;
	info.struct_size = sizeof(info);
	if (ioctl(fd, BLKGETINFO, &info) < 0)
		return -1;
	return (off_t)(info.sector_count * info.sector_size);
#else
	return lseek(fd, 0, SEEK_END);
#endif
}

/* Writes length bytes through fd at offset and reads them back through fd, comparing. */
static int
round_trip(int fd, unsigned char *out, unsigned char *in, size_t length, off_t offset, uint64_t seed)
{
	fill(out, length, seed, (uint64_t)offset / BLOCK);
	if (pwrite(fd, out, length, offset) != (ssize_t)length)
		return 0;
	memset(in, 0, length);
	if (pread(fd, in, length, offset) != (ssize_t)length)
		return 0;
	return memcmp(out, in, length) == 0;
}

static int
check(const char *device, const char *file)
{
	unsigned char *a;
	unsigned char *b;
	unsigned char *raw;
	unsigned char *shared;
	struct iovec vectors[2];
	char name[64];
	off_t size;
	pid_t child;
	int status;
	int flags;
	int plain;
	int fd;
	int other;
	int index;
	int ok;

	a = aligned(4 * MIB + BLOCK);
	b = aligned(4 * MIB + BLOCK);
	raw = aligned(4 * MIB + 2 * BLOCK);

	/* A regular file refuses O_DIRECT, and O_TRUNC with it changes nothing. */
	fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && write(fd, "keep", 4) == 4;
	if (fd >= 0)
		close(fd);
	result(ok, "file-made", file);
	errno = 0;
	fd = open(file, O_RDONLY | O_DIRECT);
	result(fd < 0 && errno == EINVAL, "file-refused", "open succeeded or wrong errno");
	if (fd >= 0)
		close(fd);
	errno = 0;
	fd = open(file, O_WRONLY | O_TRUNC | O_DIRECT);
	ok = fd < 0 && errno == EINVAL;
	if (fd >= 0)
		close(fd);
	fd = open(file, O_RDONLY);
	ok = ok && fd >= 0 && lseek(fd, 0, SEEK_END) == 4;
	result(ok, "file-trunc-kept", "content changed");
	errno = 0;
	ok = fd >= 0 && fcntl(fd, F_SETFL, O_DIRECT) == -1 && errno == EINVAL &&
	    (fcntl(fd, F_GETFL) & O_DIRECT) == 0;
	result(ok, "file-setfl-refused", "F_SETFL accepted");
	if (fd >= 0)
		close(fd);

	/* The device takes it and reports it. */
	fd = open(device, O_RDWR | O_DIRECT);
	if (fd < 0) {
		result(0, "device-open", device);
		return 1;
	}
	plain = open(device, O_RDWR);
	result(plain >= 0, "device-open-plain", device);
	result((fcntl(fd, F_GETFL) & O_DIRECT) != 0, "getfl", "no O_DIRECT");
	size = device_size(fd);
	snprintf(name, sizeof(name), "size %lld", (long long)size);
	result(size >= (off_t)(16 * MIB), "device-size", name);

	/* Misaligned offsets and lengths are refused, an empty request is empty. */
	errno = 0;
	result(pread(fd, a, BLOCK, 100) == -1 && errno == EINVAL, "offset-refused", "accepted");
	errno = 0;
	result(pread(fd, a, 100, 0) == -1 && errno == EINVAL, "length-refused", "accepted");
	result(pread(fd, a, 0, 0) == 0, "empty", "not 0");

	/* Round trips: aligned 1 MiB, then 4 MiB requests. */
	result(round_trip(fd, a, b, MIB, 0, 1), "aligned-1m", "mismatch");
	result(round_trip(fd, a, b, 4 * MIB, 4 * MIB, 2), "aligned-4m", "mismatch");

	/* A buffered write still in the cache is on the disk for a direct read. */
	fill(a, MIB, 3, 256);
	ok = pwrite(plain, a, MIB, MIB) == (ssize_t)MIB;
	memset(b, 0, MIB);
	ok = ok && pread(fd, b, MIB, MIB) == (ssize_t)MIB && memcmp(a, b, MIB) == 0;
	result(ok, "cached-dirty-then-direct-read", "direct read missed the buffered write");

	/* A direct write over cached lines is what a buffered read then sees. */
	ok = pread(plain, b, MIB, 2 * MIB) == (ssize_t)MIB;
	fill(a, MIB, 4, 512);
	ok = ok && pwrite(fd, a, MIB, 2 * MIB) == (ssize_t)MIB;
	memset(b, 0, MIB);
	ok = ok && pread(plain, b, MIB, 2 * MIB) == (ssize_t)MIB && memcmp(a, b, MIB) == 0;
	result(ok, "direct-write-then-cached-read", "buffered read saw the old data");

	/* A direct write over dirty cached lines replaces them, and a buffered read sees it. */
	fill(a, MIB, 10, 768);
	ok = pwrite(plain, a, MIB, 3 * MIB) == (ssize_t)MIB;
	fill(a, MIB, 11, 768);
	ok = ok && pwrite(fd, a, MIB, 3 * MIB) == (ssize_t)MIB;
	memset(b, 0, MIB);
	ok = ok && pread(plain, b, MIB, 3 * MIB) == (ssize_t)MIB && memcmp(a, b, MIB) == 0;
	memset(b, 0, MIB);
	ok = ok && pread(fd, b, MIB, 3 * MIB) == (ssize_t)MIB && memcmp(a, b, MIB) == 0;
	result(ok, "dirty-then-direct-write", "an old buffered write came back");

	/* A creation with O_DIRECT is refused and makes nothing. */
	snprintf(name, sizeof(name), "%s.new", file);
	errno = 0;
	other = open(name, O_RDWR | O_CREAT | O_DIRECT, 0644);
	ok = other < 0 && errno == EINVAL && access(name, F_OK) != 0;
	if (other >= 0)
		close(other);
	result(ok, "create-refused", name);

	/* Unaligned buffers: 512 bytes into a page, and an odd address. */
	fill(a, MIB, 5, 8 * MIB / BLOCK);
	memcpy(raw + 512, a, MIB);
	ok = pwrite(fd, raw + 512, MIB, 8 * MIB) == (ssize_t)MIB;
	memset(raw, 0, MIB + 512);
	ok = ok && pread(fd, raw + 512, MIB, 8 * MIB) == (ssize_t)MIB && memcmp(a, raw + 512, MIB) == 0;
	result(ok, "unaligned-512", "mismatch");
	fill(raw + 3, 3 * MIB, 6, 9 * MIB / BLOCK);
	ok = pwrite(fd, raw + 3, 3 * MIB, 9 * MIB) == (ssize_t)(3 * MIB);
	memset(b, 0, 3 * MIB);
	ok = ok && pread(fd, b, 3 * MIB, 9 * MIB) == (ssize_t)(3 * MIB) && memcmp(raw + 3, b, 3 * MIB) == 0;
	memset(raw, 0, 3 * MIB + 8);
	ok = ok && pread(fd, raw + 3, 3 * MIB, 9 * MIB) == (ssize_t)(3 * MIB) && memcmp(raw + 3, b, 3 * MIB) == 0;
	result(ok, "unaligned-odd", "mismatch");

	/* A shared anonymous mapping as the source and the destination. */
	shared = mmap(NULL, 2 * MIB, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ok = shared != MAP_FAILED;
	if (ok) {
		fill(shared, MIB, 7, 12 * MIB / BLOCK);
		ok = pwrite(fd, shared, MIB, 12 * MIB) == (ssize_t)MIB;
		ok = ok && pread(fd, shared + MIB, MIB, 12 * MIB) == (ssize_t)MIB &&
		    memcmp(shared, shared + MIB, MIB) == 0;
		munmap(shared, 2 * MIB);
	}
	result(ok, "map-shared", "mismatch");

	/* Clearing O_DIRECT takes a misaligned read; setting it back refuses it again. */
	flags = fcntl(fd, F_GETFL);
	ok = fcntl(fd, F_SETFL, flags & ~O_DIRECT) == 0 && pread(fd, a, 100, 100) == 100;
	ok = ok && fcntl(fd, F_SETFL, flags) == 0 && (fcntl(fd, F_GETFL) & O_DIRECT) != 0;
	errno = 0;
	ok = ok && pread(fd, a, 100, 100) == -1 && errno == EINVAL;
	result(ok, "setfl-toggle", "toggle failed");

	/* readv and writev move each element whole. */
	fill(a, 2 * MIB, 8, 13 * MIB / BLOCK);
	vectors[0].iov_base = a;
	vectors[0].iov_len = MIB;
	vectors[1].iov_base = raw + 512;
	vectors[1].iov_len = MIB;
	memcpy(raw + 512, a + MIB, MIB);
	ok = lseek(fd, 13 * MIB, SEEK_SET) == (off_t)(13 * MIB) && writev(fd, vectors, 2) == (ssize_t)(2 * MIB);
	vectors[0].iov_base = b;
	vectors[1].iov_base = b + MIB;
	memset(b, 0, 2 * MIB);
	ok = ok && lseek(fd, 13 * MIB, SEEK_SET) == (off_t)(13 * MIB) && readv(fd, vectors, 2) == (ssize_t)(2 * MIB) &&
	    memcmp(a, b, 2 * MIB) == 0 && lseek(fd, 0, SEEK_CUR) == (off_t)(15 * MIB);
	result(ok, "vector", "mismatch or offset");

	/* O_DSYNC direct writes return their bytes. */
	other = open(device, O_RDWR | O_DIRECT | O_DSYNC);
	ok = other >= 0 && round_trip(other, a, b, MIB, 15 * MIB, 9);
	if (other >= 0)
		close(other);
	result(ok, "dsync", "failed");

	/* /dev/fd/N carries the description's flag and cannot add it. */
	snprintf(name, sizeof(name), "/dev/fd/%d", plain);
	errno = 0;
	other = open(name, O_RDWR | O_DIRECT);
	result(other < 0 && errno == EINVAL, "devfd-add-refused", name);
	if (other >= 0)
		close(other);
	snprintf(name, sizeof(name), "/dev/fd/%d", fd);
	other = open(name, O_RDWR | O_DIRECT);
	result(other >= 0, "devfd-same", name);
	if (other >= 0)
		close(other);

	/* The device's end shortens a read and refuses a write. */
	ok = pread(fd, a, 2 * BLOCK, size - BLOCK) == (ssize_t)BLOCK;
	errno = 0;
	ok = ok && pwrite(fd, a, 2 * BLOCK, size - BLOCK) == -1 && errno == ENOSPC;
	result(ok, "device-end", "not short or not ENOSPC");

	/* Two processes read the same regions at once, each 1 MiB of them written alone first. */
	for (index = 0; index < 4; index++) {
		fill(a, MIB, 2, (4 * MIB + (uint64_t)index * MIB) / BLOCK);
		if (pwrite(fd, a, MIB, 4 * MIB + (off_t)index * MIB) != (ssize_t)MIB)
			result(0, "fork-setup", "pwrite");
	}
	child = fork();
	if (child == 0) {
		for (index = 0; index < 50; index++) {
			if (pread(fd, b, MIB, 4 * MIB + (off_t)(index % 4) * MIB) != (ssize_t)MIB)
				_exit(1);
			fill(a, MIB, 2, (4 * MIB + (uint64_t)(index % 4) * MIB) / BLOCK);
			if (memcmp(a, b, MIB) != 0)
				_exit(1);
		}
		_exit(0);
	}
	ok = child > 0;
	for (index = 0; ok && index < 50; index++) {
		ok = pread(fd, b, MIB, 4 * MIB + (off_t)((index + 2) % 4) * MIB) == (ssize_t)MIB;
		fill(a, MIB, 2, (4 * MIB + (uint64_t)((index + 2) % 4) * MIB) / BLOCK);
		ok = ok && memcmp(a, b, MIB) == 0;
	}
	ok = ok && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
	result(ok, "fork-readers", "mismatch");

	close(plain);
	close(fd);
	if (failures != 0) {
		printf("directio check: FAIL %d\n", failures);
		fflush(stdout);
		return 1;
	}
	printf("directio check: PASS\n");
	fflush(stdout);
	return 0;
}

/* The TSC ticks of 64 fills of one 1 MiB buffer, per fill. */
static uint64_t
fill_ticks(unsigned char *buffer)
{
	uint64_t mark;
	unsigned i;

	mark = split_now();
	for (i = 0; i < 64; i++)
		fill(buffer, MIB, 7, i);
	return (split_now() - mark) / 64U;
}

/*
 * How fast the buffer of a direct transfer fills: a page-aligned 1 MiB
 * buffer filled before any I/O, after one direct write from it, after one
 * buffered write from it, and a second fresh buffer for comparison.
 */
static int
fillbench(const char *path)
{
	unsigned char *buffer;
	unsigned char *other;
	uint64_t before;
	uint64_t after_direct;
	uint64_t after_plain;
	uint64_t fresh;
	int fd;

	buffer = aligned(MIB);
	other = aligned(MIB);
	memset(buffer, 0, MIB);
	memset(other, 0, MIB);
	before = fill_ticks(buffer);
	fd = open(path, O_WRONLY | O_DIRECT);
	if (fd < 0 || pwrite(fd, buffer, MIB, 0) != (ssize_t)MIB) {
		perror(path);
		return 1;
	}
	close(fd);
	after_direct = fill_ticks(buffer);
	fd = open(path, O_WRONLY);
	if (fd < 0 || pwrite(fd, buffer, MIB, 0) != (ssize_t)MIB || fsync(fd) != 0) {
		perror(path);
		return 1;
	}
	close(fd);
	after_plain = fill_ticks(buffer);
	fresh = fill_ticks(other);
	printf("directio fillbench ticks_per_MiB before=%llu after_direct=%llu after_buffered=%llu other_buffer=%llu\n",
	    (unsigned long long)before, (unsigned long long)after_direct, (unsigned long long)after_plain,
	    (unsigned long long)fresh);
	fflush(stdout);
	return 0;
}

/*
 * How a fill of one buffer A behaves next to direct I/O from another buffer
 * B: after a direct write, after a direct write and a 2 ms sleep, after a
 * direct read, and after a 0.5 ms sleep with no I/O; then the fill of A
 * right after a direct write from A itself, and a second fill at once; 256
 * rounds each, the mean TSC ticks of one 1 MiB fill.
 */
static int
fillmix(const char *path)
{
	unsigned char *a;
	unsigned char *b;
	uint64_t sum[4];
	uint64_t same[2];
	uint64_t mark;
	unsigned kind;
	unsigned i;
	off_t at;
	int fd;

	a = aligned(MIB);
	b = aligned(MIB);
	memset(a, 0, MIB);
	memset(b, 0, MIB);
	fd = open(path, O_RDWR | O_DIRECT);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	at = 0;
	for (kind = 0; kind < 4; kind++) {
		sum[kind] = 0;
		for (i = 0; i < 256; i++) {
			if (kind == 0 || kind == 1) {
				if (pwrite(fd, b, MIB, at) != (ssize_t)MIB) {
					perror("pwrite");
					return 1;
				}
				at += MIB;
			} else if (kind == 2) {
				if (pread(fd, b, MIB, (off_t)i * MIB) != (ssize_t)MIB) {
					perror("pread");
					return 1;
				}
			}
			if (kind == 1)
				usleep(2000);
			if (kind == 3)
				usleep(500);
			mark = split_now();
			fill(a, MIB, 7, i);
			sum[kind] += split_now() - mark;
		}
	}
	/* The written buffer itself: its first fill after the write, and a second one at once. */
	same[0] = 0;
	same[1] = 0;
	for (i = 0; i < 256; i++) {
		if (pwrite(fd, a, MIB, at) != (ssize_t)MIB) {
			perror("pwrite");
			return 1;
		}
		at += MIB;
		mark = split_now();
		fill(a, MIB, 7, i);
		same[0] += split_now() - mark;
		mark = split_now();
		fill(a, MIB, 7, i + 1);
		same[1] += split_now() - mark;
	}
	close(fd);
	printf("directio fillmix ticks_per_MiB after_write=%llu after_write_sleep=%llu after_read=%llu after_sleep=%llu"
	    " own_after_write=%llu own_again=%llu\n",
	    (unsigned long long)(sum[0] / 256U), (unsigned long long)(sum[1] / 256U),
	    (unsigned long long)(sum[2] / 256U), (unsigned long long)(sum[3] / 256U),
	    (unsigned long long)(same[0] / 256U), (unsigned long long)(same[1] / 256U));
	fflush(stdout);
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned long bs_kib;
	unsigned long offset;

	if (argc >= 4 && strcmp(argv[1], "check") == 0)
		return check(argv[2], argv[3]);
	if (argc >= 3 && strcmp(argv[1], "fillbench") == 0)
		return fillbench(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "fillmix") == 0)
		return fillmix(argv[2]);
	if (argc < 4 || (strcmp(argv[1], "read") != 0 && strcmp(argv[1], "write") != 0)) {
		fprintf(stderr, "usage: directio read|write DEV MIB [BS_KIB [OFFSET_MIB]] | check DEV FILE\n");
		return 2;
	}
	bs_kib = argc > 4 ? strtoul(argv[4], NULL, 0) : 1024;
	offset = argc > 5 ? strtoul(argv[5], NULL, 0) : 0;
	if (bs_kib == 0 || bs_kib % 4 != 0) {
		fprintf(stderr, "directio: BS_KIB must be a multiple of 4\n");
		return 2;
	}
	return measure(strcmp(argv[1], "write") == 0, argv[2], strtoul(argv[3], NULL, 0), bs_kib, offset);
}
