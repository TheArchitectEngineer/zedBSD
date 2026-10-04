/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements the zedBSD syslogd userland command.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOG_SOCKET "/run/log"
#define LOG_PATH "/var/log/messages"
#define BOOT_LOG_PATH "/run/dmesg.boot"

/* The most kernel log text the boot log keeps (the dmesg command's limit). */
#define BOOT_LOG_LIMIT (1024U * 1024U)

/*
 * The kernel log kept on disk as it grows (BUG-158): the ring lives only in
 * memory, so a machine stopped by the power button kept no kernel line.  The
 * previous boot's file is kept beside the new one.
 */
#define KERNEL_LOG_PATH "/var/log/kernel.log"
#define KERNEL_LOG_OLD_PATH "/var/log/kernel.log.old"

/* How often the kernel log is followed and the logs are flushed, in seconds. */
#define KERNEL_LOG_INTERVAL_SECONDS 2

/*
 * The part of the kernel log already on disk.
 *
 * The ring drops its oldest bytes when full and counts them, so the bytes it
 * ever took are the dropped count plus what it holds; written is that sum at
 * the last copy.  The descriptor is -1 while the file cannot be written.
 */
struct kernel_log_follow {
	int descriptor;
	uint64_t written;
};

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t reopening;

static void save_boot_log(void);
static char *read_kernel_log(size_t *length);
static int write_all(int descriptor, const void *buffer, size_t length);
static int open_output(void);
static void handle_signal(int number);
static void kernel_log_open(struct kernel_log_follow *follow);
static void kernel_log_copy(struct kernel_log_follow *follow);
static int kernel_log_total(uint64_t *total);
static int kernel_log_dropped(uint64_t *dropped);
static time_t monotonic_seconds(void);

/*
 * Runs the syslogd command.
 */
int
main(
	int argc,
	char **argv)
{
	int replacement;
	ssize_t length;
	struct sockaddr_un address;
	struct kernel_log_follow follow;
	struct pollfd waiting;
	char message[2048];
	int socket_descriptor, output;
	int output_dirty;
	int ready;
	time_t now;
	time_t last_flush;

	(void)argv;

	/* Validates the command-line arguments. */
	if (argc != 1) {
		fprintf(stderr, "usage: syslogd\n");

		/* Reports operation failure. */
		return 2;
	}
	(void)signal(SIGHUP, handle_signal);
	(void)signal(SIGTERM, handle_signal);
	(void)signal(SIGINT, handle_signal);
	save_boot_log();

	/* Starts the on-disk kernel log with everything the ring holds now. */
	kernel_log_open(&follow);
	kernel_log_copy(&follow);
	last_flush = monotonic_seconds();
	output_dirty = 0;

	output = open_output();

	/* Handles the output condition. */
	if (output < 0) {
		fprintf(stderr, "syslogd: %s: %s\n", LOG_PATH, strerror(errno));

		/* Reports operation failure. */
		return 1;
	}
	socket_descriptor = socket(AF_UNIX, SOCK_DGRAM, 0);

	/* Handles the socket descriptor condition. */
	if (socket_descriptor < 0)
		return 1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, LOG_SOCKET);
	(void)unlink(LOG_SOCKET);

	/* Handles a failed bind operation. */
	if (bind(socket_descriptor, (struct sockaddr *)&address,
		 sizeof(address)) != 0 ||
	    chmod(LOG_SOCKET, 0666) != 0) {
		fprintf(stderr, "syslogd: %s: %s\n", LOG_SOCKET,
			strerror(errno));

		/* Reports operation failure. */
		return 1;
	}
	while (!stopping) {
		/* Handles the reopening condition. */
		if (reopening) {
			replacement = open_output();
			reopening = 0;

			/* Handles the replacement condition. */
			if (replacement >= 0) {
				close(output);
				output = replacement;
			}
		}

		/* Copies new kernel lines and flushes both logs every interval. */
		now = monotonic_seconds();
		if (now - last_flush >= KERNEL_LOG_INTERVAL_SECONDS) {
			kernel_log_copy(&follow);

			/* Puts the messages written since the last flush on the disk. */
			if (output_dirty)
				(void)fsync(output);
			output_dirty = 0;
			last_flush = now;
		}

		/* Waits for a message, but not past the next interval. */
		waiting.fd = socket_descriptor;
		waiting.events = POLLIN;
		waiting.revents = 0;
		ready = poll(&waiting, 1, KERNEL_LOG_INTERVAL_SECONDS * 1000);
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0)
			break;

		/* Nothing arrived within the interval. */
		if (ready == 0)
			continue;

		length =
		    recv(socket_descriptor, message, sizeof(message) - 1, 0);

		/* Handles the reported system error. */
		if (length < 0 && errno == EINTR)
			continue;

		/* Checks the current data length. */
		if (length < 0)
			break;
		message[length] = '\0';

		/* Handles a failed memchr operation. */
		if (memchr(message, '\0', (size_t)length) != NULL)
			continue;

		/* Handles a failed write all operation. */
		if (write_all(output, message, (size_t)length) != 0 ||
		    write_all(output, "\n", 1) != 0) {
			fprintf(stderr, "syslogd: log write failed: %s\n",
				strerror(errno));
		}

		/* The message waits for the next flush. */
		output_dirty = 1;
	}

	/* Copies the kernel's last lines before stopping. */
	kernel_log_copy(&follow);
	if (follow.descriptor >= 0)
		(void)close(follow.descriptor);
	(void)fsync(output);
	close(output);
	close(socket_descriptor);
	unlink(LOG_SOCKET);

	/* Reports successful completion. */
	return 0;
}

/* Copies the kernel's messages so far to the boot log file. */
static void
save_boot_log(
	void)
{
	char *buffer;
	size_t length;
	int descriptor;

	/* Reads the whole kernel log; without it there is no boot log. */
	buffer = read_kernel_log(&length);
	if (buffer == NULL)
		return;

	/* Writes the boot log file and flushes it. */
	descriptor = open(BOOT_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (descriptor >= 0) {
		(void)write_all(descriptor, buffer, length);
		(void)fsync(descriptor);
		(void)close(descriptor);
	}

	free(buffer);
}

/*
 * Reads the kernel log into a buffer of its own size.
 *
 * The log may grow between sizing and reading it, so a read the log has
 * outgrown is sized and tried once more.  Returns the buffer, which the
 * caller frees, or NULL when the log cannot be read.
 */
static char *
read_kernel_log(
	size_t *length)
{
	char *buffer;
	size_t size;
	int attempt;
	int error;

	/* Tries the sized read at most twice. */
	buffer = NULL;
	for (attempt = 0; attempt < 2; attempt++) {
		/* Asks how long the log is now. */
		size = 0;
		error = sysctlbyname("kern.msgbuf", NULL, &size, NULL, 0);
		if (error != 0)
			return NULL;

		/* Refuses a log beyond what the boot log keeps. */
		if (size > BOOT_LOG_LIMIT)
			return NULL;

		/* Allocates room for that length; an empty log still gets a byte. */
		buffer = malloc(size != 0 ? size : 1U);
		if (buffer == NULL)
			return NULL;

		/* Reads the log; a log that grew past the room is sized again. */
		error = sysctlbyname("kern.msgbuf", buffer, &size, NULL, 0);
		if (error == 0)
			break;

		free(buffer);
		buffer = NULL;

		/* Anything but a log that outgrew the room ends the attempt. */
		if (errno != ENOMEM)
			return NULL;
	}

	/* The log kept outgrowing the room. */
	if (buffer == NULL)
		return NULL;

	/* Succeeded: the buffer holds the whole log. */
	*length = size;
	return buffer;
}

/* Supports the write all operation. */
static int
write_all(
	int descriptor,
	const void *buffer,
	size_t length)
{
	ssize_t written;
	const char *cursor;

	/* Process each remaining element. */
	cursor = buffer;
	while (length != 0) {
		written = write(descriptor, cursor, length);

		/* Handles the reported system error. */
		if (written < 0 && errno == EINTR)
			continue;

		/* Handles the written condition. */
		if (written <= 0)
			return -1;
		cursor += written;
		length -= (size_t)written;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the open output operation. */
static int
open_output(
	void)
{
	int function_result;

	/* Obtains the open result. */
	function_result = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0640);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the handle signal operation. */
static void
handle_signal(
	int number)
{
	/* Handles the number condition. */
	if (number == SIGHUP)
		reopening = 1;
	else
		stopping = 1;
}

/*
 * Opens the on-disk kernel log for this boot.
 *
 * The previous boot's file becomes the old one first, so the lines written
 * before a machine was stopped by the power button survive the next start.
 */
static void
kernel_log_open(
	struct kernel_log_follow *follow)
{
	int renamed;

	/* Nothing is on disk yet. */
	follow->descriptor = -1;
	follow->written = 0;

	/* Keeps the previous boot's file; a first boot has none. */
	renamed = rename(KERNEL_LOG_PATH, KERNEL_LOG_OLD_PATH);
	if (renamed != 0 && errno != ENOENT) {
		fprintf(stderr, "syslogd: %s: %s\n", KERNEL_LOG_OLD_PATH,
			strerror(errno));
	}

	/* Starts this boot's file. */
	follow->descriptor = open(KERNEL_LOG_PATH,
				  O_WRONLY | O_CREAT | O_TRUNC | O_APPEND,
				  0640);
	if (follow->descriptor < 0) {
		fprintf(stderr, "syslogd: %s: %s\n", KERNEL_LOG_PATH,
			strerror(errno));
	}
}

/*
 * Appends the kernel log's new bytes to the file and flushes it.
 *
 * Bytes the ring dropped before they could be copied are gone; a line in the
 * file says how many.
 */
static void
kernel_log_copy(
	struct kernel_log_follow *follow)
{
	char note[96];
	char *buffer;
	uint64_t dropped;
	uint64_t total;
	uint64_t fresh;
	uint64_t lost;
	size_t length;
	int error;
	int written;

	/* A file that could not be opened takes nothing. */
	if (follow->descriptor < 0)
		return;

	/* Asks how many bytes the ring ever took; nothing new needs no read. */
	error = kernel_log_total(&total);
	if (error != 0)
		return;
	if (total == follow->written)
		return;

	/*
	 * Reads the dropped count, then what the ring holds.  Bytes dropped
	 * between the two reads make the sum smaller than the truth, which
	 * copies a few bytes twice; the other order would skip some.
	 */
	error = kernel_log_dropped(&dropped);
	if (error != 0)
		return;
	buffer = read_kernel_log(&length);
	if (buffer == NULL)
		return;
	total = dropped + (uint64_t)length;

	/* Counts the bytes not yet on disk, and those the ring dropped first. */
	fresh = 0;
	if (total > follow->written)
		fresh = total - follow->written;
	lost = 0;
	if (fresh > (uint64_t)length) {
		lost = fresh - (uint64_t)length;
		fresh = (uint64_t)length;
	}

	/* Says how much of the kernel log never reached the file. */
	if (lost != 0) {
		(void)snprintf(note, sizeof(note),
			       "syslogd: %llu kernel log bytes were lost\n",
			       (unsigned long long)lost);
		(void)write_all(follow->descriptor, note, strlen(note));
	}

	/* Appends the newest bytes, which are at the end of the ring's copy. */
	written = write_all(follow->descriptor,
			    buffer + (length - (size_t)fresh),
			    (size_t)fresh);
	free(buffer);
	if (written != 0) {
		fprintf(stderr, "syslogd: %s: %s\n", KERNEL_LOG_PATH,
			strerror(errno));
		return;
	}

	/* Puts the copy on the disk before the next interval. */
	(void)fsync(follow->descriptor);
	follow->written = total;
}

/*
 * Reports how many bytes the kernel log ever took: dropped plus held.
 *
 * Returns 0, or -1 when the kernel does not answer.
 */
static int
kernel_log_total(
	uint64_t *total)
{
	uint64_t dropped;
	size_t size;
	int error;

	/* Asks how many bytes the ring dropped. */
	error = kernel_log_dropped(&dropped);
	if (error != 0)
		return -1;

	/* Asks how many bytes it holds. */
	size = 0;
	error = sysctlbyname("kern.msgbuf", NULL, &size, NULL, 0);
	if (error != 0)
		return -1;

	/* Succeeded: the sum only grows while the dropped count can. */
	*total = dropped + (uint64_t)size;
	return 0;
}

/*
 * Reports how many bytes the kernel log dropped since the boot.
 *
 * Returns 0, or -1 when the kernel does not answer.
 */
static int
kernel_log_dropped(
	uint64_t *dropped)
{
	size_t size;
	int error;

	/* Asks the kernel for the count. */
	size = sizeof(*dropped);
	error = sysctlbyname("kern.msgbuf_dropped", dropped, &size, NULL, 0);
	if (error != 0)
		return -1;

	/* Succeeded: the count is filled in. */
	return 0;
}

/* Reports seconds on a clock that never steps back. */
static time_t
monotonic_seconds(
	void)
{
	struct timespec now;
	int error;

	/* Reads the monotonic clock. */
	error = clock_gettime(CLOCK_MONOTONIC, &now);
	if (error != 0)
		return 0;

	/* Succeeded: whole seconds are enough for the interval. */
	return now.tv_sec;
}
