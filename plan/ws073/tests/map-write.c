/*
 * BUG-026 probe, run in the guest: writes a file the way ld.lld's
 * FileOutputBuffer does on disk: a temporary file beside OUTPUT is made,
 * sized with ftruncate(), mapped shared and read-write, filled through the
 * mapping, and renamed to OUTPUT; the process then leaves with _exit()
 * (with "unmap", after munmap(); with "msync", after msync(); with
 * "probe", after lld's one-byte probe file described below).  The caller
 * checks OUTPUT's blocks, reads it, and runs it.
 *
 *   map-write SOURCE OUTPUT [unmap|msync|probe]
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(
	int argc,
	char **argv)
{
	char temporary[512];
	struct stat status;
	unsigned char *mapped;
	unsigned char *copy;
	ssize_t got;
	size_t size;
	int source;
	int descriptor;

	/* Needs the source and the output. */
	if (argc < 3) {
		fprintf(stderr, "usage: map-write SOURCE OUTPUT [unmap|msync|probe]\n");
		return 2;
	}
	source = open(argv[1], O_RDONLY);
	if (source < 0 || fstat(source, &status) != 0) {
		perror(argv[1]);
		return 1;
	}
	size = (size_t)status.st_size;

	/*
	 * With "probe", lld's first step comes before: a one-byte temporary
	 * file, mapped shared and unlinked while the mapping stays.
	 */
	if (argc > 3 && strcmp(argv[3], "probe") == 0) {
		snprintf(temporary, sizeof(temporary), "%s.tmp1111111", argv[2]);
		descriptor = open(temporary, O_RDWR | O_CREAT | O_EXCL, 0777);
		if (descriptor < 0 || ftruncate(descriptor, 1) != 0) {
			perror(temporary);
			return 1;
		}
		if (mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED,
		    descriptor, 0) == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		unlink(temporary);
		close(descriptor);
	}

	/* The temporary file, sized and mapped shared. */
	snprintf(temporary, sizeof(temporary), "%s.tmp7654321", argv[2]);
	descriptor = open(temporary, O_RDWR | O_CREAT | O_EXCL, 0777);
	if (descriptor < 0 || ftruncate(descriptor, (off_t)size) != 0) {
		perror(temporary);
		return 1;
	}
	mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    descriptor, 0);
	if (mapped == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	close(descriptor);

	/*
	 * Fills it through the mapping with user-mode stores, as lld does (a
	 * read() straight into the mapping would fault in kernel mode instead).
	 */
	copy = malloc(size);
	if (copy == NULL)
		return 1;
	got = read(source, copy, size);
	if (got != (ssize_t)size) {
		perror("read");
		return 1;
	}
	memcpy(mapped, copy, size);

	/* Optionally ends the mapping, then keeps the file under its name. */
	if (argc > 3 && strcmp(argv[3], "msync") == 0)
		(void)msync(mapped, size, MS_SYNC);
	if (argc > 3 && strcmp(argv[3], "unmap") == 0)
		(void)munmap(mapped, size);
	if (rename(temporary, argv[2]) != 0) {
		perror("rename");
		return 1;
	}
	_exit(0);
}
