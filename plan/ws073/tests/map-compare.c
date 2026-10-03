/*
 * BUG-026 probe, run in the guest: compares a file as read() returns it
 * with the same file as a shared read-only mapping shows it, and prints the
 * first offset where they differ (or "same").  Exits 1 when they differ.
 *
 *   map-compare FILE
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(
	int argc,
	char **argv)
{
	struct stat status;
	unsigned char *mapped;
	unsigned char *copy;
	ssize_t got;
	size_t size;
	size_t index;
	int descriptor;

	/* Needs the file. */
	if (argc < 2) {
		fprintf(stderr, "usage: map-compare FILE\n");
		return 2;
	}
	descriptor = open(argv[1], O_RDONLY);
	if (descriptor < 0 || fstat(descriptor, &status) != 0) {
		perror(argv[1]);
		return 2;
	}
	size = (size_t)status.st_size;

	/* Reads the whole file. */
	copy = malloc(size);
	if (copy == NULL)
		return 2;
	got = pread(descriptor, copy, size, 0);
	if (got != (ssize_t)size) {
		perror("pread");
		return 2;
	}

	/* Maps it and compares byte by byte. */
	mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, descriptor, 0);
	if (mapped == MAP_FAILED) {
		perror("mmap");
		return 2;
	}
	for (index = 0; index < size; index++) {
		if (mapped[index] != copy[index]) {
			printf("differ at %zu: read %02x mapped %02x\n", index,
			    copy[index], mapped[index]);
			return 1;
		}
	}
	printf("same %zu bytes\n", size);
	return 0;
}
