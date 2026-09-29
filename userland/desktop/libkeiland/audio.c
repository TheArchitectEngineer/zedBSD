/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sound output's volume through audiod (ws100-p003, plan/ws100/
 * design.md section 3.3): one connection that says HELLO and SUBSCRIBE,
 * reads audiod's VOLUME_CHANGED reports as they come, and sends
 * DEVICE_VOLUME and FEEDBACK requests.  audiod's protocol is
 * userland/base/audiod/protocol.h.
 *
 * Nothing waits: the socket does not block, a request that cannot be
 * written whole drops the connection (the next update connects again), and
 * the answers (DONE, ERROR) are read and passed over.  audiod sends each
 * message whole; the bytes are kept and cut by the headers' length.
 */

#include <keiland.h>

#include "userland/base/audiod/protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* audiod's socket (a host test builds with another path; plan/ws100/tests/host-audio.sh). */
#ifndef AUDIO_SOCKET_PATH
#define AUDIO_SOCKET_PATH	AUDIOD_SOCKET_PATH
#endif

/* The wait before connecting again, in milliseconds. */
#define AUDIO_RETRY_MS		1000U

/* The bytes kept of messages not read whole yet. */
#define AUDIO_PENDING		(AUDIOD_MESSAGE_MAX * 8U)

/* The subscription's mask: the device volume. */
#define AUDIO_SUBSCRIBE_VOLUME	1U

/*
 * One following of audiod: the connection (-1 while none), the serial of
 * the next request, when to connect again, the bytes of messages not read
 * whole, and what audiod reported.
 */
struct keiland_audio {
	int socket;
	uint32_t serial;
	uint64_t retry_ms;
	uint8_t pending[AUDIO_PENDING];
	size_t pending_used;
	struct keiland_audio_state state;
};

static int audio_connect(struct keiland_audio *audio);
static void audio_drop(struct keiland_audio *audio, unsigned *changed);
static int audio_send(struct keiland_audio *audio, const void *message, uint32_t length);
static void audio_read(struct keiland_audio *audio, unsigned *changed);
static void audio_message(struct keiland_audio *audio, const uint8_t *bytes, uint32_t length, unsigned *changed);
static uint64_t audio_milliseconds(void);

/*
 * Starts following audiod's volume; an audiod not running yet is connected
 * by a later update.
 */
struct keiland_audio *
keiland_audio_open(
	void)
{
	struct keiland_audio *audio;

	/* The record, with no connection. */
	audio = calloc(1, sizeof(*audio));
	if (audio == NULL)
		return NULL;
	audio->socket = -1;
	audio->serial = 1U;

	/* The connection, when audiod is there (otherwise the updates try again). */
	(void)audio_connect(audio);

	/* Succeeded: the record exists, connected or not. */
	return audio;
}

/*
 * Stops following audiod.
 */
void
keiland_audio_close(
	struct keiland_audio *audio)
{
	/* Nothing to close. */
	if (audio == NULL)
		return;

	/* The connection and the record. */
	if (audio->socket >= 0)
		(void)close(audio->socket);
	free(audio);
}

/*
 * Gives the descriptor to poll, or -1 while not connected.
 */
int
keiland_audio_fd(
	const struct keiland_audio *audio)
{
	/* No record, no descriptor. */
	if (audio == NULL)
		return -1;

	/* Succeeded: the connection's descriptor (-1 when there is none). */
	return audio->socket;
}

/*
 * Reads what audiod has sent, and connects again when the connection went
 * and its wait is over.
 */
int
keiland_audio_update(
	struct keiland_audio *audio,
	unsigned *changed)
{
	uint64_t now;
	int error;

	/* Nothing has changed yet. */
	*changed = 0U;
	if (audio == NULL)
		return EINVAL;

	/* A lost connection is made again when its wait is over. */
	if (audio->socket < 0) {
		now = audio_milliseconds();
		if (now >= audio->retry_ms) {
			error = audio_connect(audio);
			if (error == 0)
				*changed |= KEILAND_AUDIO_CHANGED_REACHABLE;
		}
	}

	/* The messages that have come. */
	if (audio->socket >= 0)
		audio_read(audio, changed);

	/* Succeeded: *changed says what the reads found. */
	return 0;
}

/*
 * Copies what audiod last reported.
 */
void
keiland_audio_get_state(
	const struct keiland_audio *audio,
	struct keiland_audio_state *state)
{
	/* A missing record knows nothing. */
	memset(state, 0, sizeof(*state));
	if (audio == NULL)
		return;

	/* The state as reported. */
	*state = audio->state;
}

/*
 * Asks audiod for a volume and mute (DEVICE_VOLUME).
 */
int
keiland_audio_set_volume(
	struct keiland_audio *audio,
	unsigned left,
	unsigned right,
	unsigned muted)
{
	struct audiod_volume volume;
	int error;

	/* A volume within range, on a connection. */
	if (audio == NULL || left > 100U || right > 100U || muted > 1U)
		return EINVAL;
	if (audio->socket < 0)
		return ENOTCONN;

	/* The request. */
	memset(&volume, 0, sizeof(volume));
	volume.header.type = AUDIOD_DEVICE_VOLUME;
	volume.header.length = sizeof(volume);
	volume.header.serial = audio->serial++;
	volume.left = left;
	volume.right = right;
	volume.muted = muted;
	error = audio_send(audio, &volume, sizeof(volume));
	if (error != 0)
		return error;

	/* Succeeded: the new volume comes back as a report. */
	return 0;
}

/*
 * Asks audiod for its short feedback sound (FEEDBACK).
 */
int
keiland_audio_feedback(
	struct keiland_audio *audio)
{
	struct audiod_header header;
	int error;

	/* A connection to send on. */
	if (audio == NULL)
		return EINVAL;
	if (audio->socket < 0)
		return ENOTCONN;

	/* The request, the header only (an older audiod answers ERROR, which is passed over). */
	memset(&header, 0, sizeof(header));
	header.type = AUDIOD_FEEDBACK;
	header.length = sizeof(header);
	header.serial = audio->serial++;
	error = audio_send(audio, &header, sizeof(header));
	if (error != 0)
		return error;

	/* Succeeded: the sound plays from audiod's next period. */
	return 0;
}

/* Connects to audiod, says HELLO and SUBSCRIBE; returns 0 or an errno value. */
static int
audio_connect(
	struct keiland_audio *audio)
{
	struct sockaddr_un address;
	struct audiod_hello hello;
	struct audiod_subscribe subscribe;
	int descriptor;
	int flags;
	int error;

	/* Whatever happens, the next try waits a while. */
	audio->retry_ms = audio_milliseconds() + AUDIO_RETRY_MS;

	/* A stream socket that the programs the desktop starts do not inherit. */
	descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (descriptor < 0)
		return errno;

	/* audiod's socket; one that is not there is audiod not running. */
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	(void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", AUDIO_SOCKET_PATH);
	error = connect(descriptor, (struct sockaddr *)&address, sizeof(address));
	if (error != 0) {
		error = errno;
		(void)close(descriptor);
		return error;
	}

	/* From now on nothing waits. */
	flags = fcntl(descriptor, F_GETFL);
	if (flags >= 0)
		(void)fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
	audio->socket = descriptor;
	audio->pending_used = 0U;

	/* HELLO: audiod answers WELCOME with the device. */
	memset(&hello, 0, sizeof(hello));
	hello.header.type = AUDIOD_HELLO;
	hello.header.length = sizeof(hello);
	hello.header.serial = audio->serial++;
	hello.version = AUDIOD_VERSION;
	error = audio_send(audio, &hello, sizeof(hello));
	if (error != 0)
		return error;

	/* SUBSCRIBE: audiod answers the volume as it stands, and each change. */
	memset(&subscribe, 0, sizeof(subscribe));
	subscribe.header.type = AUDIOD_SUBSCRIBE;
	subscribe.header.length = sizeof(subscribe);
	subscribe.header.serial = audio->serial++;
	subscribe.mask = AUDIO_SUBSCRIBE_VOLUME;
	error = audio_send(audio, &subscribe, sizeof(subscribe));
	if (error != 0)
		return error;

	/* Succeeded: connected; reachable once WELCOME comes. */
	return 0;
}

/* Drops the connection; the next update after the wait connects again. */
static void
audio_drop(
	struct keiland_audio *audio,
	unsigned *changed)
{
	/* The connection. */
	if (audio->socket >= 0)
		(void)close(audio->socket);
	audio->socket = -1;
	audio->pending_used = 0U;
	audio->retry_ms = audio_milliseconds() + AUDIO_RETRY_MS;

	/* No longer reachable: said once. */
	if (audio->state.reachable) {
		audio->state.reachable = 0U;
		audio->state.device = 0U;
		if (changed != NULL)
			*changed |= KEILAND_AUDIO_CHANGED_REACHABLE;
	}
}

/* Writes one whole request; a connection that cannot take it is dropped. Returns 0 or an errno value. */
static int
audio_send(
	struct keiland_audio *audio,
	const void *message,
	uint32_t length)
{
	ssize_t sent;
	int error;

	/* All of it at once (the requests are small), never raising SIGPIPE. */
	sent = send(audio->socket, message, length, MSG_NOSIGNAL);
	if (sent == (ssize_t)length)
		return 0;

	/* A short or failed write: the connection is not usable. */
	error = EIO;
	if (sent < 0)
		error = errno;
	audio_drop(audio, NULL);
	return error;
}

/* Reads the bytes that have come and handles each whole message. */
static void
audio_read(
	struct keiland_audio *audio,
	unsigned *changed)
{
	struct audiod_header header;
	ssize_t count;

	/* Until nothing more is waiting. */
	for (;;) {
		/* Bytes, as many as there is room for; none waiting ends the read. */
		count = recv(audio->socket, audio->pending + audio->pending_used, sizeof(audio->pending) - audio->pending_used, 0);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if (count <= 0) {
			audio_drop(audio, changed);
			return;
		}

		/* Kept after what came before. */
		audio->pending_used += (size_t)count;

		/* Each whole message kept. */
		while (audio->pending_used >= sizeof(header)) {
			memcpy(&header, audio->pending, sizeof(header));
			if (header.length < sizeof(header) || header.length > AUDIOD_MESSAGE_MAX) {
				/* A broken stream: connect again. */
				audio_drop(audio, changed);
				return;
			}

			/* A message not whole yet waits for more bytes. */
			if (audio->pending_used < header.length)
				break;

			/* The message, then the bytes after it. */
			audio_message(audio, audio->pending, header.length, changed);
			audio->pending_used -= header.length;
			memmove(audio->pending, audio->pending + header.length, audio->pending_used);
		}
	}
}

/* Handles one message from audiod: WELCOME and VOLUME_CHANGED change the state, the rest is passed over. */
static void
audio_message(
	struct keiland_audio *audio,
	const uint8_t *bytes,
	uint32_t length,
	unsigned *changed)
{
	struct audiod_welcome welcome;
	struct audiod_volume volume;
	struct audiod_header header;

	/* Its type. */
	memcpy(&header, bytes, sizeof(header));

	/* WELCOME: reachable, with the device. */
	if (header.type == AUDIOD_WELCOME && length >= sizeof(welcome)) {
		memcpy(&welcome, bytes, sizeof(welcome));
		audio->state.reachable = 1U;
		audio->state.device = welcome.device != 0U;
		audio->state.rate = welcome.rate;
		audio->state.channels = welcome.channels;
		*changed |= KEILAND_AUDIO_CHANGED_REACHABLE;
		return;
	}

	/* VOLUME_CHANGED: the volume as it stands. */
	if (header.type == AUDIOD_VOLUME_CHANGED && length >= sizeof(volume)) {
		memcpy(&volume, bytes, sizeof(volume));
		audio->state.left = volume.left;
		audio->state.right = volume.right;
		audio->state.muted = volume.muted != 0U;
		*changed |= KEILAND_AUDIO_CHANGED_VOLUME;
	}
}

/* Gives the monotonic clock in milliseconds (0 when it cannot be read). */
static uint64_t
audio_milliseconds(
	void)
{
	struct timespec now;
	int error;

	/* The clock; one that fails reads as the start of time. */
	error = clock_gettime(CLOCK_MONOTONIC, &now);
	if (error != 0)
		return 0;

	/* Succeeded: seconds and nanoseconds as milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}
