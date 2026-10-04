/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The removable media on zedBSD (ws132-p004): volumed's list, read from
 * /run/volumed.sock (plan/ws132/phase004/phase.md V2).  The connection says
 * HELLO and keeps the volumes volumed reports (VOLUME and GONE lines, each
 * batch ended by DONE); a mount or an eject is a line with a request
 * number, answered by a RESULT line.  A volumed that is not running is not
 * a failure: the updates connect again every two seconds, and the list is
 * empty meanwhile.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* volumed's socket (a host test builds with another path). */
#ifndef VOLUME_SOCKET_PATH
#define VOLUME_SOCKET_PATH	"/run/volumed.sock"
#endif

/* The wait before connecting again, the longest line, and the answers kept. */
#define VOLUME_RETRY_MS		2000U
#define VOLUME_LINE_MAX		512U
#define VOLUME_RESULTS_MAX	8U

/* One answer of volumed. */
struct volume_result {
	uint32_t request;
	int error;
	char user[64];
};

/*
 * One following of volumed: the connection (-1 while none), when to connect
 * again, the bytes of a line not ended yet, the volumes of the last DONE and
 * those coming before the next, the next request's number and the answers
 * not taken yet.
 */
struct kl_backend_volumes {
	int socket;
	uint64_t retry_ms;
	char input[VOLUME_LINE_MAX];
	size_t used;
	struct kl_backend_volume list[KL_BACKEND_VOLUMES_MAX];
	size_t count;
	struct kl_backend_volume next[KL_BACKEND_VOLUMES_MAX];
	size_t next_count;
	uint32_t request;
	struct volume_result results[VOLUME_RESULTS_MAX];
	size_t result_count;
};

static int volume_connect(struct kl_backend_volumes *volumes);
static void volume_drop(struct kl_backend_volumes *volumes, unsigned *changed);
static void volume_line(struct kl_backend_volumes *volumes, char *line, unsigned *changed);
static void volume_parse(char *line, struct kl_backend_volume *volume);
static void volume_unescape(const char *text, char *output, size_t size);
static void volume_gone(struct kl_backend_volumes *volumes, const char *id);
static void volume_result(struct kl_backend_volumes *volumes, const char *line, unsigned *changed);
static int volume_ask(struct kl_backend_volumes *volumes, const char *word, const char *id, uint32_t *request);
static uint64_t volume_milliseconds(void);

/*
 * Starts following volumed; one not running yet is connected by a later update.
 */
struct kl_backend_volumes *
kl_backend_volumes_open(
	void)
{
	struct kl_backend_volumes *volumes;

	/* The record, with no connection and no volume. */
	volumes = calloc(1U, sizeof(*volumes));
	if (volumes == NULL)
		return NULL;
	volumes->socket = -1;

	/* The first connection, if volumed runs. */
	(void)volume_connect(volumes);

	/* Succeeded. */
	return volumes;
}

/*
 * Stops following volumed.
 */
void
kl_backend_volumes_close(
	struct kl_backend_volumes *volumes)
{
	/* Nothing was opened. */
	if (volumes == NULL)
		return;

	/* The connection and the record. */
	if (volumes->socket >= 0)
		(void)close(volumes->socket);
	free(volumes);
}

/*
 * Reads what volumed has sent and connects again when it went.
 */
int
kl_backend_volumes_update(
	struct kl_backend_volumes *volumes,
	unsigned *changed)
{
	ssize_t count;
	uint64_t now;
	char *end;
	size_t length;
	int error;

	/* A record and somewhere to say what changed. */
	if (volumes == NULL || changed == NULL)
		return EINVAL;
	*changed = 0U;

	/* No connection: another try once the wait is over. */
	if (volumes->socket < 0) {
		now = volume_milliseconds();
		if (now < volumes->retry_ms)
			return 0;
		error = volume_connect(volumes);
		if (error != 0)
			return 0;
	}

	/* Every byte there now, line by line. */
	for (;;) {
		count = recv(volumes->socket, volumes->input + volumes->used, sizeof(volumes->input) - 1U - volumes->used, MSG_DONTWAIT);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			break;
		if (count <= 0) {
			volume_drop(volumes, changed);
			break;
		}

		/* Each whole line. */
		volumes->used += (size_t)count;
		volumes->input[volumes->used] = '\0';
		for (;;) {
			end = strchr(volumes->input, '\n');
			if (end == NULL)
				break;
			*end = '\0';
			volume_line(volumes, volumes->input, changed);
			length = (size_t)(end + 1 - volumes->input);
			memmove(volumes->input, end + 1, volumes->used - length + 1U);
			volumes->used -= length;
		}

		/* A line longer than any is a broken connection. */
		if (volumes->used >= sizeof(volumes->input) - 1U) {
			volume_drop(volumes, changed);
			break;
		}
	}

	/* Succeeded. */
	return 0;
}

/*
 * Copies the volumes of the last complete report.
 */
size_t
kl_backend_volumes_get(
	const struct kl_backend_volumes *volumes,
	struct kl_backend_volume *list,
	size_t capacity)
{
	size_t count;

	/* As many as fit. */
	if (volumes == NULL || list == NULL)
		return 0U;
	count = volumes->count;
	if (count > capacity)
		count = capacity;
	memcpy(list, volumes->list, count * sizeof(list[0]));

	/* Succeeded. */
	return count;
}

/*
 * Asks volumed to mount a volume.
 */
int
kl_backend_volumes_mount(
	struct kl_backend_volumes *volumes,
	const char *id,
	uint32_t *request)
{
	int error;

	/* The line. */
	error = volume_ask(volumes, "MOUNT", id, request);
	if (error != 0)
		return error;

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Asks volumed to eject a volume.
 */
int
kl_backend_volumes_eject(
	struct kl_backend_volumes *volumes,
	const char *id,
	uint32_t *request)
{
	int error;

	/* The line. */
	error = volume_ask(volumes, "EJECT", id, request);
	if (error != 0)
		return error;

	/* Succeeded: the answer comes as a result. */
	return 0;
}

/*
 * Takes the oldest answer.
 */
int
kl_backend_volumes_take_result(
	struct kl_backend_volumes *volumes,
	uint32_t *request,
	int *error,
	char *user,
	size_t size)
{
	/* None waiting. */
	if (volumes == NULL || volumes->result_count == 0U)
		return 0;

	/* The oldest, then the others move up. */
	*request = volumes->results[0].request;
	*error = volumes->results[0].error;
	if (user != NULL && size > 0U)
		(void)snprintf(user, size, "%s", volumes->results[0].user);
	volumes->result_count--;
	memmove(&volumes->results[0], &volumes->results[1], volumes->result_count * sizeof(volumes->results[0]));

	/* Succeeded: one answer. */
	return 1;
}

/* Connects to volumed and says HELLO; an error leaves the next try two seconds away. */
static int
volume_connect(
	struct kl_backend_volumes *volumes)
{
	struct sockaddr_un address;
	ssize_t sent;
	int descriptor;
	int status;

	/* The next try, should this one fail. */
	volumes->retry_ms = volume_milliseconds() + VOLUME_RETRY_MS;

	/* The connection. */
	descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (descriptor < 0)
		return errno;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	(void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", VOLUME_SOCKET_PATH);
	status = connect(descriptor, (struct sockaddr *)&address, sizeof(address));
	if (status != 0) {
		status = errno;
		(void)close(descriptor);
		return status;
	}

	/* The greeting; the list follows. */
	sent = send(descriptor, "HELLO 1\n", 8U, MSG_DONTWAIT);
	if (sent != 8) {
		(void)close(descriptor);
		return EPIPE;
	}

	/* Succeeded: connected, nothing read yet. */
	volumes->socket = descriptor;
	volumes->used = 0U;
	volumes->next_count = 0U;
	return 0;
}

/* Drops the connection: the list empties, and the answers waited for fail. */
static void
volume_drop(
	struct kl_backend_volumes *volumes,
	unsigned *changed)
{
	/* The connection goes; the next try is two seconds away. */
	(void)close(volumes->socket);
	volumes->socket = -1;
	volumes->retry_ms = volume_milliseconds() + VOLUME_RETRY_MS;
	volumes->used = 0U;

	/* Without volumed there is no volume. */
	if (volumes->count != 0U)
		*changed |= KL_BACKEND_VOLUMES_CHANGED_LIST;
	volumes->count = 0U;
	volumes->next_count = 0U;
}

/* Carries out one line of volumed. */
static void
volume_line(
	struct kl_backend_volumes *volumes,
	char *line,
	unsigned *changed)
{
	struct kl_backend_volume volume;
	int same;

	/* A volume: added to the coming report, in place of one by its ID. */
	same = strncmp(line, "VOLUME ", 7U);
	if (same == 0) {
		memset(&volume, 0, sizeof(volume));
		volume_parse(line + 7, &volume);
		if (volume.id[0] == '\0')
			return;
		volume_gone(volumes, volume.id);
		if (volumes->next_count < KL_BACKEND_VOLUMES_MAX) {
			volumes->next[volumes->next_count] = volume;
			volumes->next_count++;
		}

		/* Kept for the DONE. */
		return;
	}

	/* A volume gone. */
	same = strncmp(line, "GONE id=", 8U);
	if (same == 0) {
		volume_gone(volumes, line + 8);
		return;
	}

	/* One state: the coming report becomes the list (it starts from it again for the next changes). */
	same = strcmp(line, "DONE");
	if (same == 0) {
		memcpy(volumes->list, volumes->next, volumes->next_count * sizeof(volumes->list[0]));
		volumes->count = volumes->next_count;
		*changed |= KL_BACKEND_VOLUMES_CHANGED_LIST;
		return;
	}

	/* An answer. */
	same = strncmp(line, "RESULT ", 7U);
	if (same == 0)
		volume_result(volumes, line + 7, changed);
}

/* Reads a VOLUME line's key=value words into a volume. */
static void
volume_parse(
	char *line,
	struct kl_backend_volume *volume)
{
	char *word;
	char *rest;
	char *value;
	int same;

	/* Each word, ended by a space or the line's end. */
	for (word = line;
	     word != NULL;
	     word = rest) {
		rest = strchr(word, ' ');
		if (rest != NULL) {
			*rest = '\0';
			rest++;
		}

		/* A key and its value. */
		value = strchr(word, '=');
		if (value == NULL)
			continue;
		*value = '\0';
		value++;

		/* The keys a volume has; others are passed over. */
		same = strcmp(word, "id");
		if (same == 0)
			(void)snprintf(volume->id, sizeof(volume->id), "%s", value);
		same = strcmp(word, "fs");
		if (same == 0)
			(void)snprintf(volume->fs, sizeof(volume->fs), "%s", value);
		same = strcmp(word, "label");
		if (same == 0)
			volume_unescape(value, volume->label, sizeof(volume->label));
		same = strcmp(word, "path");
		if (same == 0)
			volume_unescape(value, volume->path, sizeof(volume->path));
		same = strcmp(word, "size");
		if (same == 0)
			volume->bytes = strtoull(value, NULL, 10);
		same = strcmp(word, "new");
		if (same == 0)
			volume->fresh = (unsigned)strtoul(value, NULL, 10);
	}

	/* "-" is no path: the volume is not mounted. */
	same = strcmp(volume->path, "-");
	if (same == 0)
		volume->path[0] = '\0';
}

/* Turns a word's %xx back into bytes. */
static void
volume_unescape(
	const char *text,
	char *output,
	size_t size)
{
	char hex[3];
	size_t used;

	/* Each byte, or the byte of each %xx. */
	used = 0U;
	while (*text != '\0' && used + 1U < size) {
		if (text[0] == '%' && text[1] != '\0' && text[2] != '\0') {
			hex[0] = text[1];
			hex[1] = text[2];
			hex[2] = '\0';
			output[used] = (char)strtoul(hex, NULL, 16);
			text += 3;
		} else {
			output[used] = *text;
			text++;
		}

		/* One byte more. */
		used++;
	}

	/* Ended. */
	output[used] = '\0';
}

/* Takes a volume off the coming report. */
static void
volume_gone(
	struct kl_backend_volumes *volumes,
	const char *id)
{
	size_t index;
	int same;

	/* The one of the ID, if there. */
	for (index = 0U; index < volumes->next_count; index++) {
		same = strcmp(volumes->next[index].id, id);
		if (same != 0)
			continue;
		volumes->next_count--;
		volumes->next[index] = volumes->next[volumes->next_count];
		return;
	}
}

/* Keeps an answer: "<request> <errno> [user=<program>]". */
static void
volume_result(
	struct kl_backend_volumes *volumes,
	const char *line,
	unsigned *changed)
{
	struct volume_result *result;
	const char *user;
	unsigned request;
	int error;
	int count;

	/* The request and its errno. */
	count = sscanf(line, "%u %d", &request, &error);
	if (count != 2 || volumes->result_count >= VOLUME_RESULTS_MAX)
		return;

	/* Kept, with the program that holds a busy volume. */
	result = &volumes->results[volumes->result_count];
	volumes->result_count++;
	memset(result, 0, sizeof(*result));
	result->request = request;
	result->error = error;
	user = strstr(line, " user=");
	if (user != NULL)
		(void)snprintf(result->user, sizeof(result->user), "%s", user + 6);
	*changed |= KL_BACKEND_VOLUMES_CHANGED_RESULT;
}

/* Sends a mount or an eject. */
static int
volume_ask(
	struct kl_backend_volumes *volumes,
	const char *word,
	const char *id,
	uint32_t *request)
{
	char line[96];
	const char *space;
	ssize_t sent;
	int length;

	/* A connection, a volume's name of one word, and somewhere to put the number. */
	if (volumes == NULL || id == NULL || request == NULL || id[0] == '\0')
		return EINVAL;
	space = strpbrk(id, " \n");
	length = (int)strnlen(id, KL_BACKEND_VOLUME_ID_MAX);
	if (space != NULL || length >= (int)KL_BACKEND_VOLUME_ID_MAX)
		return EINVAL;
	if (volumes->socket < 0)
		return ENOTCONN;

	/* The line, numbered. */
	volumes->request++;
	if (volumes->request == 0U)
		volumes->request = 1U;
	length = snprintf(line, sizeof(line), "%s %u %s\n", word, volumes->request, id);
	sent = send(volumes->socket, line, (size_t)length, MSG_DONTWAIT);
	if (sent != (ssize_t)length)
		return EPIPE;

	/* Succeeded: the answer will carry the number. */
	*request = volumes->request;
	return 0;
}

/* The monotonic time in milliseconds. */
static uint64_t
volume_milliseconds(
	void)
{
	struct timespec now;

	/* The clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}
