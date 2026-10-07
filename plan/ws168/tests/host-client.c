/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of keiland-preview's callers' side (ws168-p004,
 * userland/desktop/preview/client.c with linux/spawn.c, or with
 * freebsd/spawn.c and the making in this process): a picture made and
 * read back (its size and a colour), a job followed without waiting, an
 * input that is not a regular file refused, a file that is not a picture,
 * and a stamp in the header.
 *   host-client PICTURE TEXT     (PICTURE a 400x300 PNG, blue at the bottom; TEXT a file of words)
 */

#include "userland/desktop/preview/client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The checks failed. */
static int failures;

static void test_check(const char *name, int passed);

/* Runs the checks. */
int
main(
	int argc,
	char **argv)
{
	struct preview_request request;
	struct preview_picture picture;
	struct preview_job job;
	char header[64];
	ssize_t got;
	uint32_t pixel;
	int polls;
	int error;
	int fd;

	/* The files. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-client PICTURE TEXT\n");
		return 2;
	}

	/* A picture made and read back: 256 contain is 256x192, blue at the bottom. */
	memset(&request, 0, sizeof(request));
	request.width = 256;
	request.height = 256;
	error = preview_picture(argv[1], &request, &picture);
	pixel = 0;
	if (error == 0)
		pixel = picture.pixels[150 * 256 + 128];
	test_check("picture", error == 0 && picture.width == 256 && picture.height == 192 && (pixel & 0xffU) > 200U && ((pixel >> 16) & 0xffU) < 80U);
	preview_picture_release(&picture);

	/* The same as a job followed without waiting, with a stamp in the header. */
	(void)snprintf(request.stamp, sizeof(request.stamp), "keiland-thumbnail mtime=5 size=6");
	fd = open("host-client.out", O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	error = preview_start(argv[1], fd, &request, &job);
	polls = 0;
	while (error == 0 && preview_poll(&job) == 0)
		polls++;
	(void)lseek(fd, 0, SEEK_SET);
	got = read(fd, header, sizeof(header) - 1U);
	header[got > 0 ? got : 0] = '\0';
	(void)close(fd);
	(void)unlink("host-client.out");
	test_check("job", error == 0 && job.finished && job.status == PREVIEW_OK && strncmp(header, "P6\n# keiland-thumbnail mtime=5 size=6\n256 192\n", 46U) == 0);
	printf("polls=%d pid=%ld\n", polls, (long)job.pid);

	/* Refused: not a regular file, not a picture. */
	request.stamp[0] = '\0';
	error = preview_picture("/dev/null", &request, &picture);
	test_check("not-regular", error == EINVAL);
	error = preview_picture(argv[2], &request, &picture);
	test_check("not-picture", error == EINVAL);

	/* The result. */
	printf("host-client: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures != 0;
}

/* Writes a check's result. */
static void
test_check(
	const char *name,
	int passed)
{
	/* A line each. */
	printf("%s %s\n", passed ? "ok" : "NOT OK", name);
	if (!passed)
		failures++;
}
