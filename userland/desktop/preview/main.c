/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * keiland-preview's program (WS168 p003; preview.h): the arguments read,
 * the system's confinement entered (nothing on zedBSD, where the process
 * starts in its sandbox), fd 0 read whole, the picture decoded and scaled,
 * and the PPM written to fd 1.  The exit status tells the caller what
 * happened (PREVIEW_*); nothing is printed (fd 2 is not there).
 */

#include "preview.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* The piece read at once. */
#define MAIN_CHUNK		(256U * 1024U)

int main(int argc, char **argv);
static int main_parse(int argc, char **argv, struct preview_request *request);
static int main_number(const char *text, int *number);
static int main_read(int fd, unsigned char **data, size_t *size);
static int main_write_all(int fd, const void *data, size_t size);
static void main_escape(int escape);

/*
 * Makes the preview of fd 0 into fd 1.
 */
int
main(
	int argc,
	char **argv)
{
	struct preview_request request;
	struct preview_image decoded;
	struct preview_image scaled;
	struct stat status;
	unsigned char *data;
	size_t size;
	int regular;
	int error;
	int result;

	/* The arguments. */
	error = main_parse(argc, argv, &request);
	if (error != 0)
		return PREVIEW_USAGE;

	/* The input and the output: regular files (fstat before the confinement, which may not allow it). */
	error = fstat(0, &status);
	regular = error == 0 && S_ISREG(status.st_mode);
	if (!regular)
		return PREVIEW_USAGE;
	error = fstat(1, &status);
	regular = error == 0 && S_ISREG(status.st_mode);
	if (!regular)
		return PREVIEW_NO_OUTPUT;

	/* The confinement, before a byte of the input is read. */
	error = preview_confine();
	if (error != 0)
		return PREVIEW_NO_SANDBOX;

	/* The input, whole. */
	error = main_read(0, &data, &size);
	if (error == EFBIG)
		return PREVIEW_TOO_LARGE;
	if (error == ENOMEM)
		return PREVIEW_NO_MEMORY;
	if (error != 0)
		return PREVIEW_DAMAGED;

	/* A test build's escape, where planted code would run. */
	if (PREVIEW_TEST_ESCAPE && request.escape != PREVIEW_ESCAPE_NONE)
		main_escape(request.escape);

	/* Decoded. */
	result = preview_decode(data, size, &request, &decoded);
	free(data);
	if (result != PREVIEW_OK)
		return result;

	/* Scaled. */
	error = preview_scale(&decoded, &request, &scaled);
	preview_image_release(&decoded);
	if (error != 0)
		return PREVIEW_NO_MEMORY;

	/* Written. */
	error = preview_write_ppm(1, &scaled, request.stamp);
	preview_image_release(&scaled);
	if (error != 0)
		return PREVIEW_NO_OUTPUT;
	return PREVIEW_OK;
}

/*
 * Writes a picture as a binary PPM (its comment "# STAMP" when a stamp is
 * given; the premultiplied colours as they are).  Returns 0 or an errno
 * value.
 */
int
preview_write_ppm(
	int fd,
	const struct preview_image *image,
	const char *stamp)
{
	unsigned char *row;
	char header[PREVIEW_STAMP_MAX + 64U];
	uint32_t pixel;
	int length;
	int x;
	int y;
	int error;

	/* The header. */
	if (stamp != NULL && stamp[0] != '\0')
		length = snprintf(header, sizeof(header), "P6\n# %s\n%d %d\n255\n", stamp, image->width, image->height);
	else
		length = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", image->width, image->height);
	if (length < 0 || (size_t)length >= sizeof(header))
		return EINVAL;
	error = main_write_all(fd, header, (size_t)length);
	if (error != 0)
		return error;

	/* Each row as bytes. */
	row = malloc((size_t)image->width * 3U);
	if (row == NULL)
		return ENOMEM;
	for (y = 0; y < image->height; y++) {
		for (x = 0; x < image->width; x++) {
			pixel = image->pixels[(size_t)y * (size_t)image->width + (size_t)x];
			row[x * 3] = (unsigned char)(pixel >> 16);
			row[x * 3 + 1] = (unsigned char)(pixel >> 8);
			row[x * 3 + 2] = (unsigned char)pixel;
		}

		/* Written. */
		error = main_write_all(fd, row, (size_t)image->width * 3U);
		if (error != 0)
			break;
	}

	/* The row's room goes. */
	free(row);
	return error;
}

/* Reads the arguments; 0, or -1 when they are wrong. */
static int
main_parse(
	int argc,
	char **argv,
	struct preview_request *request)
{
	const char *newline;
	size_t length;
	int index;
	int same;
	int error;

	/* Nothing yet. */
	memset(request, 0, sizeof(*request));

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The size. */
		same = strncmp(argv[index], "--width=", 8U);
		if (same == 0) {
			error = main_number(argv[index] + 8, &request->width);
			if (error != 0)
				return -1;
			continue;
		}

		/* The height. */
		same = strncmp(argv[index], "--height=", 9U);
		if (same == 0) {
			error = main_number(argv[index] + 9, &request->height);
			if (error != 0)
				return -1;
			continue;
		}

		/* The fit. */
		same = strcmp(argv[index], "--fit=cover");
		if (same == 0) {
			request->cover = 1;
			continue;
		}

		/* Contain. */
		same = strcmp(argv[index], "--fit=contain");
		if (same == 0) {
			request->cover = 0;
			continue;
		}

		/* A test build's escape. */
		same = strncmp(argv[index], "--test-escape=", 14U);
		if (same == 0 && PREVIEW_TEST_ESCAPE) {
			same = strcmp(argv[index] + 14, "open");
			if (same == 0)
				request->escape = PREVIEW_ESCAPE_OPEN;
			same = strcmp(argv[index] + 14, "socket");
			if (same == 0)
				request->escape = PREVIEW_ESCAPE_SOCKET;
			same = strcmp(argv[index] + 14, "fork");
			if (same == 0)
				request->escape = PREVIEW_ESCAPE_FORK;
			if (request->escape == PREVIEW_ESCAPE_NONE)
				return -1;
			continue;
		}

		/* The stamp: one line that fits. */
		same = strncmp(argv[index], "--stamp=", 8U);
		if (same != 0)
			return -1;
		length = strlen(argv[index] + 8);
		newline = strchr(argv[index] + 8, '\n');
		if (length >= sizeof(request->stamp) || newline != NULL)
			return -1;
		memcpy(request->stamp, argv[index] + 8, length + 1U);
	}

	/* Both sides were given. */
	if (request->width == 0 || request->height == 0)
		return -1;
	return 0;
}

/* Reads a side: 1 to PREVIEW_SIDE_MAX; 0, or -1. */
static int
main_number(
	const char *text,
	int *number)
{
	char *end;
	long value;

	/* Decimal digits only. */
	value = strtol(text, &end, 10);
	if (end == text || *end != '\0' || value < 1L || value > (long)PREVIEW_SIDE_MAX)
		return -1;
	*number = (int)value;
	return 0;
}

/* Reads a descriptor to its end; 0 with the bytes (the caller's), EFBIG, ENOMEM, or EIO. */
static int
main_read(
	int fd,
	unsigned char **data,
	size_t *size)
{
	unsigned char *buffer;
	unsigned char *grown;
	size_t length;
	size_t capacity;
	ssize_t got;

	/* Piece by piece, the room grown as it fills. */
	buffer = NULL;
	length = 0;
	capacity = 0;
	for (;;) {
		if (capacity - length < MAIN_CHUNK) {
			if (capacity >= PREVIEW_INPUT_MAX) {
				free(buffer);
				return EFBIG;
			}

			/* More room. */
			capacity += MAIN_CHUNK * 8U;
			grown = realloc(buffer, capacity);
			if (grown == NULL) {
				free(buffer);
				return ENOMEM;
			}

			/* Grown. */
			buffer = grown;
		}

		/* The next piece. */
		got = read(fd, buffer + length, MAIN_CHUNK);
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0) {
			free(buffer);
			return EIO;
		}

		/* = 0)=The end. */
		if (got == 0)
			break;
		length += (size_t)got;
		if (length > PREVIEW_INPUT_MAX) {
			free(buffer);
			return EFBIG;
		}
	}

	/* Read through. */
	*data = buffer;
	*size = length;
	return 0;
}

/* Writes all the bytes; 0, or an errno value. */
static int
main_write_all(
	int fd,
	const void *data,
	size_t size)
{
	const unsigned char *cursor;
	ssize_t wrote;

	/* Until every byte is out. */
	cursor = data;
	while (size > 0U) {
		wrote = write(fd, cursor, size);
		if (wrote < 0 && errno == EINTR)
			continue;
		if (wrote <= 0)
			return EIO;
		cursor += wrote;
		size -= (size_t)wrote;
	}

	/* Written. */
	return 0;
}

/* Tries a call the confinement refuses (a test build's); the process should end in it.  Writes what came of it to fd 1. */
static void
main_escape(
	int escape)
{
	char line[64];
	int result;
	int length;

	/* The call. */
	result = -1;
	if (escape == PREVIEW_ESCAPE_OPEN)
		result = open("/etc/passwd", O_RDONLY | O_CLOEXEC);
	else if (escape == PREVIEW_ESCAPE_SOCKET)
		result = socket(AF_INET, SOCK_STREAM, 0);
	else if (escape == PREVIEW_ESCAPE_FORK)
		result = (int)fork();

	/* Still here: the confinement let it through or refused it without ending the process. */
	length = snprintf(line, sizeof(line), "ESCAPED result=%d errno=%d\n", result, errno);
	if (length > 0)
		(void)main_write_all(1, line, (size_t)length);
	_exit(PREVIEW_NO_SANDBOX);
}
