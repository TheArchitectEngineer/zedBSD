/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Writes MIB MiB of pseudo-random data to a file or disk in 1 MiB writes,
 * then makes it durable (fsync, or sync where fsync is refused), and
 * reports the time with and without that last step.  Every 4 KiB block
 * starts with its own number, so no two blocks are the same and no zero
 * block is written.  It builds on Linux (cc -O2) and on zedBSD
 * (build-randread.sh), so both systems write the same bytes the same way.
 *
 *	patwrite FILE MIB [OFFSET_MIB]
 *
 * A regular file is created when missing and written from OFFSET_MIB; a
 * disk is written in place.  One line reports the result:
 *
 *	patwrite mib=M write_ms=W sync_ms=S MBps=X MBps_nosync=Y
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The size of one write. */
#define PATWRITE_CHUNK (1024U * 1024U)

/* The size of a numbered block. */
#define PATWRITE_BLOCK 4096U

static uint64_t now_ns(void);

/*
 * Fills the pattern once, then writes it MIB times with fresh block numbers.
 */
int
main(
	int argc,
	char **argv)
{
	unsigned char *chunk;
	uint64_t state;
	uint64_t number;
	uint64_t started;
	uint64_t written;
	uint64_t synced;
	unsigned long mib;
	unsigned long offset;
	unsigned long index;
	size_t at;
	char line[256];
	int length;
	int fd;

	if (argc < 3) {
		fprintf(stderr, "usage: patwrite FILE MIB [OFFSET_MIB]\n");
		return 2;
	}
	mib = strtoul(argv[2], NULL, 0);
	offset = argc > 3 ? strtoul(argv[3], NULL, 0) : 0;
	if (mib == 0) {
		fprintf(stderr, "patwrite: bad size\n");
		return 2;
	}
	chunk = malloc(PATWRITE_CHUNK);
	if (chunk == NULL) {
		fprintf(stderr, "patwrite: no memory\n");
		return 1;
	}

	/* xorshift64 over the whole chunk. */
	state = 0x243f6a8885a308d3ULL;
	for (at = 0; at < PATWRITE_CHUNK; at += sizeof(state)) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memcpy(chunk + at, &state, sizeof(state));
	}

	fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	if (lseek(fd, (off_t)offset * PATWRITE_CHUNK, SEEK_SET) < 0) {
		perror("lseek");
		return 1;
	}

	/* Writes each chunk with its blocks numbered from the start of the run. */
	number = 0;
	started = now_ns();
	for (index = 0; index < mib; index++) {
		for (at = 0; at < PATWRITE_CHUNK; at += PATWRITE_BLOCK) {
			number++;
			memcpy(chunk + at, &number, sizeof(number));
		}
		if (write(fd, chunk, PATWRITE_CHUNK) != (ssize_t)PATWRITE_CHUNK) {
			perror("write");
			return 1;
		}
	}
	written = now_ns();
	if (fsync(fd) != 0)
		sync();
	synced = now_ns();
	close(fd);

	length = snprintf(line, sizeof(line),
	    "patwrite mib=%lu write_ms=%llu sync_ms=%llu MBps=%llu MBps_nosync=%llu\n", mib,
	    (unsigned long long)((written - started) / 1000000U),
	    (unsigned long long)((synced - written) / 1000000U),
	    (unsigned long long)((uint64_t)mib * PATWRITE_CHUNK / ((synced - started) / 1000U + 1U)),
	    (unsigned long long)((uint64_t)mib * PATWRITE_CHUNK / ((written - started) / 1000U + 1U)));
	if (length > 0)
		(void)write(1, line, (size_t)length);
	free(chunk);
	return 0;
}

/* Reads the monotonic clock in nanoseconds. */
static uint64_t
now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}
