/*
 * BUG-026 probe, run in the guest: the file-system calls ld.lld makes with
 * --no-mmap-output-file.  lld first proves it can create the output by
 * making a one-byte temporary file beside it, mapping it shared and
 * read-write, and unlinking it while the mapping stays (FileOutputBuffer's
 * discard); then it writes the linked image to the output with one write()
 * from an anonymous buffer and leaves.  Copies SOURCE into OUTPUT that way;
 * the caller compares OUTPUT's blocks and runs it.
 *
 *   lld-like SOURCE OUTPUT [plain]
 *
 * With "plain" the temporary file is left out.
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */

#include <fcntl.h>
#include <stdio.h>
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
	unsigned char *buffer;
	unsigned char *probe;
	ssize_t got;
	int source;
	int output;
	int descriptor;
	int plain;

	/* Needs the source and the output. */
	if (argc < 3) {
		fprintf(stderr, "usage: lld-like SOURCE OUTPUT [plain]\n");
		return 2;
	}
	plain = argc > 3 && strcmp(argv[3], "plain") == 0;

	/* Reads the source into an anonymous buffer, as lld builds its image. */
	source = open(argv[1], O_RDONLY);
	if (source < 0 || fstat(source, &status) != 0) {
		perror(argv[1]);
		return 1;
	}
	buffer = mmap(NULL, (size_t)status.st_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buffer == MAP_FAILED)
		return 1;
	got = read(source, buffer, (size_t)status.st_size);
	if (got != status.st_size)
		return 1;
	close(source);

	/* The one-byte temporary file, mapped and unlinked while mapped. */
	if (!plain) {
		snprintf(temporary, sizeof(temporary), "%s.tmp1234567", argv[2]);
		descriptor = open(temporary, O_RDWR | O_CREAT | O_EXCL, 0777);
		if (descriptor < 0 || ftruncate(descriptor, 1) != 0) {
			perror(temporary);
			return 1;
		}
		probe = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED,
		    descriptor, 0);
		if (probe == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		unlink(temporary);
		close(descriptor);
	}

	/* The output, written in one call, then the process leaves. */
	output = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0777);
	if (output < 0) {
		perror(argv[2]);
		return 1;
	}
	got = write(output, buffer, (size_t)status.st_size);
	if (got != status.st_size) {
		perror("write");
		return 1;
	}
	close(output);
	_exit(0);
}
