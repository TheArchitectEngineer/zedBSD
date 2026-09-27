/*
 * BUG-026 probe, run in the guest: does a regular file keep what write()
 * gave it when the buffer is an anonymous mapping that is unmapped, or the
 * process leaves with _exit(), right after the write?  This is what ld.lld
 * does with its in-memory output buffer.  Writes FILE from a filled
 * 10104-byte anonymous mapping, unmaps it (unless "keep"), and leaves with
 * _exit(0); the caller checks the file's checksum afterwards.
 *
 *   write-unmap FILE [keep]
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PROBE_SIZE 10104U

int
main(
	int argc,
	char **argv)
{
	unsigned char *buffer;
	ssize_t written;
	size_t index;
	int descriptor;
	int keep;

	/* Needs the file to write. */
	if (argc < 2) {
		fprintf(stderr, "usage: write-unmap FILE [keep]\n");
		return 2;
	}
	keep = 0;
	if (argc > 2 && strcmp(argv[2], "keep") == 0)
		keep = 1;

	/* Fills an anonymous mapping with a pattern that is never zero. */
	buffer = mmap(NULL, PROBE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buffer == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (index = 0; index < PROBE_SIZE; index++)
		buffer[index] = (unsigned char)(index % 251U + 1U);

	/* Writes it to a new regular file in one call, as lld's commit does. */
	descriptor = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (descriptor < 0) {
		perror("open");
		return 1;
	}
	written = write(descriptor, buffer, PROBE_SIZE);
	if (written != (ssize_t)PROBE_SIZE) {
		perror("write");
		return 1;
	}

	/* Gives the buffer back at once and leaves without closing. */
	if (!keep)
		(void)munmap(buffer, PROBE_SIZE);
	_exit(0);
}
