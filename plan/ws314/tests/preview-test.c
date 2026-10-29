/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test of keiland-preview's callers' side (preview/client.c with
 * linux/spawn.c) after its interface lost the descriptor and the process
 * ID: a picture made and read back (waiting and not), a preview written to
 * a file named by its path, a child past its time ended, one given up, an
 * input that is not a regular file, --info, and no child left to collect.
 * Prints one line per case and PASS or FAIL.
 *
 *   preview-test PICTURE.png FOLDER
 */

#include "userland/desktop/preview/client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void
check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static void
nap(void)
{
	struct timespec step = { 0, 10000000L };

	(void)nanosleep(&step, NULL);
}

int
main(int argc, char **argv)
{
	struct preview_request request;
	struct preview_picture picture;
	struct preview_pending pending;
	struct preview_job job;
	struct preview_info info;
	char output[512];
	char head[3];
	pid_t pid;
	int finished;
	int error;
	int fd;

	if (argc != 3)
		return 2;
	memset(&request, 0, sizeof(request));
	request.width = 64;
	request.height = 64;

	error = preview_picture(argv[1], &request, &picture);
	check(error == 0 && picture.width > 0 && picture.height > 0, "picture: made by a child and read back");
	preview_picture_release(&picture);

	error = preview_picture_begin(argv[1], &request, &pending);
	check(error == 0 && pending.output != NULL && pending.job.apart, "begin: a child at work");
	finished = 0;
	while (error == 0 && !finished) {
		finished = preview_picture_follow(&pending, &picture, &error);
		if (!finished)
			nap();
	}
	check(error == 0 && picture.width > 0 && pending.output == NULL, "follow: the picture, the output gone");
	preview_picture_release(&picture);

	snprintf(output, sizeof(output), "%s/start.ppm", argv[2]);
	error = preview_start(argv[1], output, &request, &job);
	check(error == 0 && job.apart && job.child != NULL, "start: into a file by its path");
	(void)preview_wait(&job);
	fd = open(output, O_RDONLY);
	memset(head, 0, sizeof(head));
	if (fd >= 0) {
		(void)read(fd, head, sizeof(head));
		close(fd);
	}
	check(job.status == PREVIEW_OK && job.child == NULL && memcmp(head, "P6\n", 3) == 0, "start: a PPM written, the child collected");

	error = preview_start(argv[1], output, &request, &job);
	job.limit_ms = 0U;
	job.started_ms = 0U;
	finished = preview_poll(&job);
	check(error == 0 && finished && job.child == NULL &&
	    (job.status == PREVIEW_KILLED + 9 || job.status == PREVIEW_OK), "poll: a child past its time ended");

	error = preview_picture_begin(argv[1], &request, &pending);
	preview_picture_cancel(&pending);
	check(error == 0 && pending.output == NULL && pending.job.finished, "cancel: given up");

	error = preview_start("/dev/null", output, &request, &job);
	check(error == EINVAL, "start: EINVAL for an input that is not a regular file");

	fd = open(argv[1], O_RDONLY);
	error = preview_info_read(fd, &info);
	close(fd);
	check(error == 0 && info.kind == 2 && info.width > 0, "info: a PNG and its size");

	pid = waitpid(-1, NULL, WNOHANG);
	check(pid < 0 && errno == ECHILD, "children: none left to collect");

	printf("%s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
