/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The ws100-p002 test client of audiod: it says HELLO, then carries out its
 * words in order and prints one line for each answer.
 *
 *   audiod-feedback WORD...
 *     volume P       DEVICE_VOLUME, P percent on both channels, unmuted
 *     mute           DEVICE_VOLUME, 100 percent, muted
 *     feedback       FEEDBACK (the short feedback sound)
 *     get            SUBSCRIBE, which answers the device volume as it stands
 *     sleep MS       waits MS milliseconds
 *     raw TYPE       a request of a type with the header only
 *
 * Lines: FEEDBACKTEST welcome device=D rate=R, FEEDBACKTEST done type=T
 * error=E, FEEDBACKTEST volume left=L right=R muted=M, FEEDBACKTEST FAIL ...
 */

#include "userland/base/audiod/protocol.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* How long an answer is waited for, in milliseconds. */
#define TEST_ANSWER_MS	3000

static int test_connect(void);
static int test_send(int fd, const void *message, uint32_t length);
static int test_answer(int fd, uint32_t serial);
static void test_sleep(unsigned milliseconds);

/* The serial of the next request. */
static uint32_t test_serial = 1U;

/*
 * Carries out the words; returns 0 when every request was answered, 1
 * otherwise.
 */
int
main(
	int argc,
	char **argv)
{
	struct audiod_hello hello;
	struct audiod_volume volume;
	struct audiod_subscribe subscribe;
	struct audiod_header header;
	int index;
	int fd;
	int error;
	int same;

	/* The connection and HELLO. */
	fd = test_connect();
	if (fd < 0) {
		printf("FEEDBACKTEST FAIL connect errno=%d\n", errno);
		return 1;
	}
	memset(&hello, 0, sizeof(hello));
	hello.header.type = AUDIOD_HELLO;
	hello.header.length = sizeof(hello);
	hello.header.serial = test_serial++;
	hello.version = AUDIOD_VERSION;
	error = test_send(fd, &hello, sizeof(hello));
	if (error == 0)
		error = test_answer(fd, hello.header.serial);
	if (error != 0)
		return 1;

	/* Each word in order. */
	for (index = 1; index < argc; index++) {
		error = 0;
		same = strcmp(argv[index], "volume");
		if (same == 0 && index + 1 < argc) {
			/* The device volume, unmuted. */
			memset(&volume, 0, sizeof(volume));
			volume.header.type = AUDIOD_DEVICE_VOLUME;
			volume.header.length = sizeof(volume);
			volume.header.serial = test_serial++;
			volume.left = (uint32_t)atoi(argv[index + 1]);
			volume.right = volume.left;
			error = test_send(fd, &volume, sizeof(volume));
			if (error == 0)
				error = test_answer(fd, volume.header.serial);
			index++;
		} else if (strcmp(argv[index], "mute") == 0) {
			/* Full, muted. */
			memset(&volume, 0, sizeof(volume));
			volume.header.type = AUDIOD_DEVICE_VOLUME;
			volume.header.length = sizeof(volume);
			volume.header.serial = test_serial++;
			volume.left = 100U;
			volume.right = 100U;
			volume.muted = 1U;
			error = test_send(fd, &volume, sizeof(volume));
			if (error == 0)
				error = test_answer(fd, volume.header.serial);
		} else if (strcmp(argv[index], "feedback") == 0) {
			/* The feedback sound. */
			memset(&header, 0, sizeof(header));
			header.type = AUDIOD_FEEDBACK;
			header.length = sizeof(header);
			header.serial = test_serial++;
			error = test_send(fd, &header, sizeof(header));
			if (error == 0)
				error = test_answer(fd, header.serial);
		} else if (strcmp(argv[index], "get") == 0) {
			/* SUBSCRIBE: DONE, then the volume. */
			memset(&subscribe, 0, sizeof(subscribe));
			subscribe.header.type = AUDIOD_SUBSCRIBE;
			subscribe.header.length = sizeof(subscribe);
			subscribe.header.serial = test_serial++;
			subscribe.mask = 1U;
			error = test_send(fd, &subscribe, sizeof(subscribe));
			if (error == 0)
				error = test_answer(fd, subscribe.header.serial);
			if (error == 0)
				error = test_answer(fd, 0U);
		} else if (strcmp(argv[index], "sleep") == 0 && index + 1 < argc) {
			/* A wait. */
			test_sleep((unsigned)atoi(argv[index + 1]));
			index++;
		} else if (strcmp(argv[index], "raw") == 0 && index + 1 < argc) {
			/* A request of any type, the header only. */
			memset(&header, 0, sizeof(header));
			header.type = (uint32_t)atoi(argv[index + 1]);
			header.length = sizeof(header);
			header.serial = test_serial++;
			error = test_send(fd, &header, sizeof(header));
			if (error == 0)
				error = test_answer(fd, header.serial);
			index++;
		} else {
			printf("FEEDBACKTEST FAIL word=%s\n", argv[index]);
			error = 1;
		}

		/* A word that failed ends the run. */
		if (error != 0) {
			close(fd);
			return 1;
		}
	}

	/* Succeeded: every word was answered. */
	printf("FEEDBACKTEST end\n");
	close(fd);
	return 0;
}

/* Connects to audiod's socket; returns the descriptor or -1. */
static int
test_connect(
	void)
{
	struct sockaddr_un address;
	int fd;
	int error;

	/* The socket. */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	/* audiod's address. */
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", AUDIOD_SOCKET_PATH);
	error = connect(fd, (struct sockaddr *)&address, sizeof(address));
	if (error != 0) {
		close(fd);
		return -1;
	}

	/* Succeeded: connected. */
	return fd;
}

/* Sends one whole message; returns 0 or 1. */
static int
test_send(
	int fd,
	const void *message,
	uint32_t length)
{
	ssize_t sent;

	/* All of it at once (the messages are small). */
	sent = send(fd, message, length, 0);
	if (sent != (ssize_t)length) {
		printf("FEEDBACKTEST FAIL send errno=%d\n", errno);
		return 1;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Reads messages until the answer to a serial (or, with serial 0, a volume
 * report) and prints it; returns 0 or 1.  The socket is a byte stream, so
 * the bytes are kept and cut into messages by their headers' length (two
 * messages sent one after the other may come in one read).
 */
static int
test_answer(
	int fd,
	uint32_t serial)
{
	static uint8_t pending[AUDIOD_MESSAGE_MAX * 8U];
	static size_t pending_used;
	union {
		struct audiod_header header;
		struct audiod_welcome welcome;
		struct audiod_result result;
		struct audiod_volume volume;
		uint8_t bytes[AUDIOD_MESSAGE_MAX];
	} message;
	struct audiod_header header;
	struct pollfd descriptor;
	ssize_t count;
	int ready;

	for (;;) {
		/* A whole message kept: taken out and looked at. */
		if (pending_used >= sizeof(header)) {
			memcpy(&header, pending, sizeof(header));
			if (header.length < sizeof(header) || header.length > sizeof(message)) {
				printf("FEEDBACKTEST FAIL answer length=%u\n", header.length);
				return 1;
			}
			if (pending_used >= header.length) {
				memset(&message, 0, sizeof(message));
				memcpy(&message, pending, header.length);
				pending_used -= header.length;
				memmove(pending, pending + header.length, pending_used);

				/* The welcome, a result for the serial, or a volume report. */
				if (message.header.type == AUDIOD_WELCOME && message.header.serial == serial) {
					printf("FEEDBACKTEST welcome device=%u rate=%u\n", message.welcome.device, message.welcome.rate);
					return 0;
				}
				if ((message.header.type == AUDIOD_DONE || message.header.type == AUDIOD_ERROR) &&
				    message.header.serial == serial && serial != 0U) {
					printf("FEEDBACKTEST done type=%u error=%u\n", message.header.type, message.result.error);
					return 0;
				}
				if (message.header.type == AUDIOD_VOLUME_CHANGED && serial == 0U) {
					printf("FEEDBACKTEST volume left=%u right=%u muted=%u\n", message.volume.left, message.volume.right, message.volume.muted);
					return 0;
				}
				continue;
			}
		}

		/* More bytes, within the wait. */
		descriptor.fd = fd;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		ready = poll(&descriptor, 1, TEST_ANSWER_MS);
		if (ready <= 0) {
			printf("FEEDBACKTEST FAIL answer serial=%u timeout\n", serial);
			return 1;
		}
		count = recv(fd, pending + pending_used, sizeof(pending) - pending_used, 0);
		if (count <= 0) {
			printf("FEEDBACKTEST FAIL answer serial=%u closed\n", serial);
			return 1;
		}
		pending_used += (size_t)count;
	}
}

/* Waits some milliseconds. */
static void
test_sleep(
	unsigned milliseconds)
{
	struct timespec time;

	/* The whole wait. */
	time.tv_sec = (time_t)(milliseconds / 1000U);
	time.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&time, NULL);
}
