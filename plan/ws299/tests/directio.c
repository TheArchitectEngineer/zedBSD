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
 *
 * read and write move MIB MiB with O_DIRECT in BS_KIB requests (1024 by
 * default) from OFFSET_MIB of DEV, from a page-aligned buffer; write puts
 * a numbered pseudo-random pattern in every 4 KiB block (as patwrite does)
 * and ends with fsync.  One line reports the result:
 *
 *	directio read|write mib=M bs_kib=B ms=T MBps=X
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

/* Fills a buffer with numbered 4 KiB blocks of a seeded xorshift pattern. */
static void
fill(unsigned char *buffer, size_t length, uint64_t seed, uint64_t number)
{
	uint64_t state;
	size_t at;

	state = seed * 0x9e3779b97f4a7c15ULL + 1U;
	for (at = 0; at < length; at += 8) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(buffer + at, &state, length - at < 8 ? length - at : 8);
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
	started = now_ns();
	while (at < end) {
		if (write_mode) {
			fill(buffer, bs, 7, number);
			number += bs / BLOCK;
			done = pwrite(fd, buffer, bs, at);
		} else {
			done = pread(fd, buffer, bs, at);
		}
		if (done != (ssize_t)bs) {
			fprintf(stderr, "directio: %s at %lld: %zd\n", write_mode ? "pwrite" : "pread",
			    (long long)at, done);
			return 1;
		}
		at += (off_t)bs;
	}
	if (write_mode && fsync(fd) != 0) {
		perror("fsync");
		return 1;
	}
	elapsed = now_ns() - started;
	close(fd);
	printf("directio %s mib=%lu bs_kib=%lu ms=%llu MBps=%.1f\n", write_mode ? "write" : "read",
	    mib, bs_kib, (unsigned long long)(elapsed / 1000000U),
	    (double)mib * MIB / 1e6 / ((double)elapsed / 1e9));
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
		return 1;
	}
	printf("directio check: PASS\n");
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned long bs_kib;
	unsigned long offset;

	if (argc >= 4 && strcmp(argv[1], "check") == 0)
		return check(argv[2], argv[3]);
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
