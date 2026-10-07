/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The callers' side of keiland-preview (WS168 p004; client.h): the input
 * opened (a regular file only: never a FIFO or a device, which would block
 * or reach a driver), the child started with it and the output, followed
 * until it ends or its time is up, and the PPM it wrote read back.
 */

#include "client.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* How long preview_wait sleeps between two looks (ms). */
#define CLIENT_STEP_MS		10L

/* The longest header of a PPM read back. */
#define CLIENT_HEADER_MAX	(PREVIEW_STAMP_MAX + 64U)

static uint64_t client_clock_ms(void);
static int client_temporary(char *path, size_t size);
static int client_number(const unsigned char *data, size_t size, size_t *at, int *number);

/*
 * Starts a preview of an input file into an output descriptor (open for
 * writing; the caller closes it after).  Returns 0 with the job (finished
 * at once when it was made in this process), EINVAL for an input that is
 * not a regular file, or an errno value.
 */
int
preview_start(
	const char *input,
	int output,
	const struct preview_request *request,
	struct preview_job *job)
{
	unsigned char head[5];
	struct stat status;
	ssize_t got;
	int regular;
	int error;
	int pdf;
	int fd;

	/* The input: a regular file, opened without waiting. */
	memset(job, 0, sizeof(*job));
	fd = open(input, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return errno;
	error = fstat(fd, &status);
	regular = error == 0 && S_ISREG(status.st_mode);
	if (!regular) {
		(void)close(fd);
		return EINVAL;
	}

	/* How long it may take: a PDF longer. */
	got = pread(fd, head, sizeof(head), 0);
	pdf = got == (ssize_t)sizeof(head) && memcmp(head, "%PDF-", sizeof(head)) == 0;
	job->limit_ms = PREVIEW_LIMIT_MS;
	if (pdf)
		job->limit_ms = PREVIEW_LIMIT_PDF_MS;

	/* The child (or this process). */
	job->started_ms = client_clock_ms();
	error = preview_spawn(fd, output, request, &job->pid, &job->status);
	(void)close(fd);
	if (error != 0)
		return error;
	job->finished = job->pid == 0;
	return 0;
}

/*
 * Looks at a job without waiting: 1 when it has finished (its status
 * kept), 0 while it runs.  A child past its time is ended (SIGKILL) and
 * collected.
 */
int
preview_poll(
	struct preview_job *job)
{
	uint64_t now;
	pid_t ended;
	int status;
	int exited;
	int signalled;

	/* Finished already. */
	if (job->finished)
		return 1;

	/* Ended, or still running. */
	ended = waitpid(job->pid, &status, WNOHANG);
	if (ended == job->pid) {
		job->finished = 1;
		job->status = PREVIEW_KILLED;
		exited = WIFEXITED(status);
		signalled = WIFSIGNALED(status);
		if (exited)
			job->status = WEXITSTATUS(status);
		else if (signalled)
			job->status = PREVIEW_KILLED + WTERMSIG(status);
		return 1;
	}

	/* Past its time: ended and collected. */
	now = client_clock_ms();
	if (now - job->started_ms > job->limit_ms) {
		(void)kill(job->pid, SIGKILL);
		(void)waitpid(job->pid, &status, 0);
		job->finished = 1;
		job->status = PREVIEW_KILLED + SIGKILL;
		return 1;
	}

	/* Still running. */
	return 0;
}

/*
 * Waits for a job to finish.  Returns its status.
 */
int
preview_wait(
	struct preview_job *job)
{
	struct timespec step;
	int finished;

	/* A look every few milliseconds. */
	for (;;) {
		finished = preview_poll(job);
		if (finished)
			return job->status;
		step.tv_sec = 0;
		step.tv_nsec = CLIENT_STEP_MS * 1000000L;
		(void)nanosleep(&step, NULL);
	}
}

/*
 * Makes a preview of a file, waits for it and reads the picture back (the
 * output is a temporary file, removed after).  Returns 0 with the picture
 * (the caller's), the child's status as an errno value of its own kind
 * (EINVAL unknown or damaged, EFBIG too large, ENOMEM, EIO otherwise), or
 * an errno value.
 */
int
preview_picture(
	const char *input,
	const struct preview_request *request,
	struct preview_picture *picture)
{
	struct preview_job job;
	char path[256];
	int status;
	int error;
	int fd;

	/* The output, a new file only this process knows. */
	memset(picture, 0, sizeof(*picture));
	fd = client_temporary(path, sizeof(path));
	if (fd < 0)
		return errno;
	(void)unlink(path);

	/* The preview, waited for. */
	error = preview_start(input, fd, request, &job);
	if (error != 0) {
		(void)close(fd);
		return error;
	}

	/* Its end. */
	status = preview_wait(&job);

	/* Its picture, when it made one. */
	error = EIO;
	if (status == PREVIEW_OK) {
		(void)lseek(fd, 0, SEEK_SET);
		error = preview_read(fd, picture);
	} else if (status == PREVIEW_UNKNOWN || status == PREVIEW_DAMAGED) {
		error = EINVAL;
	} else if (status == PREVIEW_TOO_LARGE) {
		error = EFBIG;
	} else if (status == PREVIEW_NO_MEMORY) {
		error = ENOMEM;
	}

	/* The output goes (it was removed already). */
	(void)close(fd);
	return error;
}

/*
 * Reads the binary PPM a child wrote (P6, a comment line or none, a size
 * of at most PREVIEW_SIDE_MAX each way, 255, and all the pixels).
 * Returns 0 with the picture, EINVAL for anything else, or ENOMEM.
 */
int
preview_read(
	int fd,
	struct preview_picture *picture)
{
	unsigned char header[CLIENT_HEADER_MAX];
	unsigned char *bytes;
	size_t at;
	size_t count;
	size_t index;
	size_t done;
	ssize_t got;
	int maximum;
	int error;

	/* The header's bytes. */
	memset(picture, 0, sizeof(*picture));
	got = read(fd, header, sizeof(header));
	if (got < 3 || header[0] != 'P' || header[1] != '6' || header[2] != '\n')
		return EINVAL;

	/* A comment line, then the size and 255. */
	at = 3;
	if (header[at] == '#') {
		while (at < (size_t)got && header[at] != '\n')
			at++;
		at++;
	}

	/* The numbers. */
	error = client_number(header, (size_t)got, &at, &picture->width);
	if (error == 0)
		error = client_number(header, (size_t)got, &at, &picture->height);
	if (error == 0)
		error = client_number(header, (size_t)got, &at, &maximum);
	if (error != 0 || maximum != 255 || at >= (size_t)got || header[at] != '\n')
		return EINVAL;
	at++;
	if (picture->width < 1 || picture->height < 1 || picture->width > PREVIEW_SIDE_MAX || picture->height > PREVIEW_SIDE_MAX)
		return EINVAL;

	/* The pixels' bytes: those read with the header, then the rest. */
	count = (size_t)picture->width * (size_t)picture->height;
	bytes = malloc(count * 3U);
	if (bytes == NULL)
		return ENOMEM;
	done = (size_t)got - at;
	if (done > count * 3U)
		done = count * 3U;
	memcpy(bytes, header + at, done);
	while (done < count * 3U) {
		got = read(fd, bytes + done, count * 3U - done);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		done += (size_t)got;
	}

	/* All of them. */
	if (done != count * 3U) {
		free(bytes);
		return EINVAL;
	}

	/* The picture, opaque. */
	picture->pixels = malloc(count * sizeof(picture->pixels[0]));
	if (picture->pixels == NULL) {
		free(bytes);
		return ENOMEM;
	}

	/* Each pixel. */
	for (index = 0; index < count; index++)
		picture->pixels[index] = 0xff000000U | ((uint32_t)bytes[index * 3U] << 16) | ((uint32_t)bytes[index * 3U + 1U] << 8) | bytes[index * 3U + 2U];
	free(bytes);
	return 0;
}

/*
 * Frees a picture read back.
 */
void
preview_picture_release(
	struct preview_picture *picture)
{
	/* The pixels. */
	free(picture->pixels);
	memset(picture, 0, sizeof(*picture));
}

/* The time in milliseconds (the monotonic clock). */
static uint64_t
client_clock_ms(void)
{
	struct timespec now;

	/* Monotonic. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Makes a new temporary file in the user's runtime folder (else /tmp); its descriptor, or -1. */
static int
client_temporary(
	char *path,
	size_t size)
{
	const char *folder;

	/* $XDG_RUNTIME_DIR, else /tmp. */
	folder = getenv("XDG_RUNTIME_DIR");
	if (folder == NULL || folder[0] != '/')
		folder = "/tmp";
	(void)snprintf(path, size, "%s/keiland-preview-XXXXXX", folder);
	return mkstemp(path);
}

/* Reads a header's number after one blank; 0, or EINVAL. */
static int
client_number(
	const unsigned char *data,
	size_t size,
	size_t *at,
	int *number)
{
	int digits;

	/* A blank before every number but the first of a line. */
	if (*at < size && (data[*at] == ' ' || data[*at] == '\n'))
		(*at)++;

	/* Up to five digits. */
	*number = 0;
	digits = 0;
	while (*at < size && data[*at] >= '0' && data[*at] <= '9' && digits < 5) {
		*number = *number * 10 + (data[*at] - '0');
		(*at)++;
		digits++;
	}

	/* A number. */
	if (digits == 0)
		return EINVAL;
	return 0;
}
