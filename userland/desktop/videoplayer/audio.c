/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The player's sound: a client of audiod (userland/base/audiod/protocol.h,
 * plan/ws035/audiod-design.md) with one playback stream of 16-bit stereo
 * at 48 kHz (audiod converts it to the device's rate).  The samples go into
 * the stream's ring in shared memory; the socket carries the requests, their
 * answers and audiod's events, which are read and passed over whenever the
 * player writes (audiod drops a client whose socket fills up).
 *
 * It is the player's own for beta 1; a shared client in libkeiland is a
 * candidate for later (plan/ws122/phase002/phase.md).
 */

#include "videoplayer.h"

#include "userland/base/audiod/protocol.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* The stream: its number, rate, channels, and its ring of half a second, asked for in fiftieths. */
#define AUDIO_STREAM		1U
#define AUDIO_RATE		48000U
#define AUDIO_CHANNELS		2U
#define AUDIO_BUFFER		(AUDIO_RATE / 2U)
#define AUDIO_PERIOD		(AUDIO_RATE / 50U)

static int audio_request(struct vp_audio *audio, void *message, uint32_t length, uint32_t want, void *answer, uint32_t answer_length, int *descriptor);
static int audio_receive(struct vp_audio *audio, int wait, int *descriptor);
static void audio_drain(struct vp_audio *audio);
static struct audiod_shm_header *audio_header(const struct vp_audio *audio);

/*
 * Connects to audiod and makes the playback stream (not started).
 * Returns 0, or an errno value when there is no sound: the player then
 * plays the picture alone.
 */
int
vp_audio_open(
	struct vp_audio *audio)
{
	struct audiod_stream_create create;
	struct audiod_stream_created created;
	struct audiod_welcome welcome;
	struct audiod_hello hello;
	struct sockaddr_un address;
	void *shm;
	int descriptor;
	int status;
	int error;

	/* Nothing yet. */
	memset(audio, 0, sizeof(*audio));
	audio->socket = -1;
	(void)pthread_mutex_init(&audio->lock, NULL);

	/* audiod's socket. */
	audio->socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (audio->socket < 0)
		return errno;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, AUDIOD_SOCKET_PATH, sizeof(address.sun_path) - 1U);
	status = connect(audio->socket, (struct sockaddr *)&address, sizeof(address));
	if (status != 0) {
		error = errno;
		vp_audio_close(audio);
		return error;
	}

	/* The greeting, and whether there is a sound device. */
	memset(&hello, 0, sizeof(hello));
	hello.header.type = AUDIOD_HELLO;
	hello.header.length = sizeof(hello);
	hello.version = AUDIOD_VERSION;
	error = audio_request(audio, &hello, sizeof(hello), AUDIOD_WELCOME, &welcome, sizeof(welcome), NULL);
	if (error == 0 && welcome.device == 0U)
		error = ENODEV;
	if (error != 0) {
		vp_audio_close(audio);
		return error;
	}

	/* The playback stream, and its shared memory with the answer. */
	memset(&create, 0, sizeof(create));
	create.header.type = AUDIOD_STREAM_CREATE;
	create.header.length = sizeof(create);
	create.header.stream = AUDIO_STREAM;
	create.direction = AUDIOD_PLAYBACK;
	create.format = AUDIOD_FORMAT_S16_LE;
	create.channels = AUDIO_CHANNELS;
	create.rate = AUDIO_RATE;
	create.buffer_frames = AUDIO_BUFFER;
	create.period_frames = AUDIO_PERIOD;
	descriptor = -1;
	error = audio_request(audio, &create, sizeof(create), AUDIOD_STREAM_CREATED, &created, sizeof(created), &descriptor);
	if (error == 0 && descriptor < 0)
		error = EPROTO;
	if (error != 0) {
		vp_audio_close(audio);
		return error;
	}

	/* The ring, mapped. */
	shm = mmap(NULL, created.shm_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
	(void)close(descriptor);
	if (shm == MAP_FAILED) {
		error = errno;
		vp_audio_close(audio);
		return error;
	}

	/* Succeeded: the stream is the player's. */
	audio->shm = shm;
	audio->shm_bytes = created.shm_bytes;
	audio->rate = AUDIO_RATE;
	audio->channels = AUDIO_CHANNELS;
	audio->capacity = created.capacity_frames;
	audio->created = 1;
	return 0;
}

/*
 * Ends the stream and the connection.
 */
void
vp_audio_close(
	struct vp_audio *audio)
{
	struct audiod_header destroy;

	/* The stream goes. */
	if (audio->created) {
		memset(&destroy, 0, sizeof(destroy));
		destroy.type = AUDIOD_STREAM_DESTROY;
		destroy.length = sizeof(destroy);
		destroy.stream = AUDIO_STREAM;
		(void)audio_request(audio, &destroy, sizeof(destroy), AUDIOD_DONE, NULL, 0U, NULL);
	}

	/* Its memory and the socket. */
	if (audio->shm != NULL)
		(void)munmap(audio->shm, audio->shm_bytes);
	if (audio->socket >= 0)
		(void)close(audio->socket);
	audio->shm = NULL;
	audio->socket = -1;
	audio->created = 0;
}

/* Starts the stream: audiod plays what is written. */
int
vp_audio_start(
	struct vp_audio *audio)
{
	struct audiod_header start;
	int error;

	/* Once. */
	if (!audio->created || audio->running)
		return 0;

	/* The request. */
	memset(&start, 0, sizeof(start));
	start.type = AUDIOD_STREAM_START;
	start.length = sizeof(start);
	start.stream = AUDIO_STREAM;
	error = audio_request(audio, &start, sizeof(start), AUDIOD_DONE, NULL, 0U, NULL);
	if (error == 0)
		audio->running = 1;
	return error;
}

/* Stops the stream where it is (a pause): what is written stays. */
int
vp_audio_stop(
	struct vp_audio *audio)
{
	struct audiod_header stop;
	int error;

	/* Only a running stream. */
	if (!audio->created || !audio->running)
		return 0;

	/* The request. */
	memset(&stop, 0, sizeof(stop));
	stop.type = AUDIOD_STREAM_STOP;
	stop.length = sizeof(stop);
	stop.stream = AUDIO_STREAM;
	error = audio_request(audio, &stop, sizeof(stop), AUDIOD_DONE, NULL, 0U, NULL);
	if (error == 0)
		audio->running = 0;
	return error;
}

/* Drops what is written and not played (a seek). */
int
vp_audio_flush(
	struct vp_audio *audio)
{
	struct audiod_header flush;
	int error;

	/* Only a stream. */
	if (!audio->created)
		return 0;

	/* The request. */
	memset(&flush, 0, sizeof(flush));
	flush.type = AUDIOD_STREAM_FLUSH;
	flush.length = sizeof(flush);
	flush.stream = AUDIO_STREAM;
	error = audio_request(audio, &flush, sizeof(flush), AUDIOD_DONE, NULL, 0U, NULL);
	return error;
}

/* Reports how many frames audiod has read from the stream. */
uint64_t
vp_audio_read_position(
	const struct vp_audio *audio)
{
	struct audiod_shm_header *header;

	/* None without a stream. */
	if (!audio->created)
		return 0;
	header = audio_header(audio);
	return audiod_position_load(&header->read_position, &header->read_sequence);
}

/* Reports how many frames the player has written. */
uint64_t
vp_audio_write_position(
	const struct vp_audio *audio)
{
	struct audiod_shm_header *header;

	/* None without a stream. */
	if (!audio->created)
		return 0;
	header = audio_header(audio);
	return audiod_position_load(&header->write_position, &header->write_sequence);
}

/*
 * Writes up to a number of frames into the ring, as many as there is room
 * for; reports how many.  audiod's events are passed over first.
 */
size_t
vp_audio_write(
	struct vp_audio *audio,
	const int16_t *samples,
	size_t frames)
{
	struct audiod_shm_header *header;
	unsigned char *ring;
	uint64_t write;
	uint64_t read;
	size_t room;
	size_t done;
	size_t offset;
	size_t part;

	/* Nothing without a stream; the socket read first. */
	if (!audio->created)
		return 0;
	audio_drain(audio);

	/* The room in the ring. */
	header = audio_header(audio);
	ring = (unsigned char *)audio->shm + AUDIOD_SHM_HEADER;
	write = audiod_position_load(&header->write_position, &header->write_sequence);
	read = audiod_position_load(&header->read_position, &header->read_sequence);
	room = audio->capacity - (size_t)(write - read);
	if (frames > room)
		frames = room;

	/* Copied in at most two parts around the ring's end. */
	done = 0;
	while (done < frames) {
		offset = (size_t)((write + done) % audio->capacity);
		part = audio->capacity - offset;
		if (part > frames - done)
			part = frames - done;
		memcpy(ring + offset * 4U, samples + done * 2U, part * 4U);
		done += part;
	}

	/* Published. */
	audiod_position_store(&header->write_position, &header->write_sequence, write + frames);
	return frames;
}

/*
 * Sends a request and waits for its answer: the message of type want (or
 * DONE) of the same serial, copied out; an ERROR gives its errno.  Events
 * are passed over.
 */
static int
audio_request(
	struct vp_audio *audio,
	void *message,
	uint32_t length,
	uint32_t want,
	void *answer,
	uint32_t answer_length,
	int *descriptor)
{
	struct audiod_header *header;
	struct audiod_result *result;
	ssize_t sent;
	uint32_t serial;
	int error;

	/* One request at a time, numbered. */
	(void)pthread_mutex_lock(&audio->lock);
	header = message;
	audio->serial++;
	serial = audio->serial;
	header->serial = serial;
	sent = send(audio->socket, message, length, 0);
	if (sent != (ssize_t)length) {
		(void)pthread_mutex_unlock(&audio->lock);
		return EPIPE;
	}

	/* The answer, the messages before it passed over. */
	for (;;) {
		error = audio_receive(audio, 1, descriptor);
		if (error != 0)
			break;

		/* The first whole message. */
		header = (struct audiod_header *)(void *)audio->input;
		if (header->serial == serial && (header->type == want || header->type == AUDIOD_DONE || header->type == AUDIOD_ERROR)) {
			/* An error, or the answer. */
			error = 0;
			if (header->type == AUDIOD_ERROR) {
				result = (struct audiod_result *)(void *)audio->input;
				error = (int)result->error;
				if (error == 0)
					error = EIO;
			} else if (answer != NULL && header->length >= answer_length) {
				memcpy(answer, audio->input, answer_length);
			}

			/* Taken off the input. */
			memmove(audio->input, audio->input + header->length, audio->input_used - header->length);
			audio->input_used -= header->length;
			break;
		}

		/* Something else (an event): passed over. */
		memmove(audio->input, audio->input + header->length, audio->input_used - header->length);
		audio->input_used -= header->length;
	}

	/* The answer. */
	(void)pthread_mutex_unlock(&audio->lock);
	return error;
}

/*
 * Reads until the input holds one whole message (waiting, or not); a
 * descriptor that comes with it is kept.  0, EAGAIN when nothing is there
 * without waiting, or an errno value when the connection is broken.
 */
static int
audio_receive(
	struct vp_audio *audio,
	int wait,
	int *descriptor)
{
	union {
		struct cmsghdr header;
		char space[CMSG_SPACE(sizeof(int))];
	} control;
	const struct audiod_header *header;
	struct cmsghdr *message_control;
	struct msghdr message;
	struct iovec vector;
	ssize_t count;
	int flags;

	/* Until a whole message is in. */
	for (;;) {
		/* A whole one already. */
		header = (const struct audiod_header *)(const void *)audio->input;
		if (audio->input_used >= sizeof(*header) && header->length >= sizeof(*header) &&
		    header->length <= sizeof(audio->input) && audio->input_used >= header->length)
			return 0;

		/* A message that cannot fit is a broken connection. */
		if (audio->input_used >= sizeof(*header) && (header->length < sizeof(*header) || header->length > sizeof(audio->input)))
			return EPROTO;

		/* More bytes, and a descriptor with them. */
		memset(&message, 0, sizeof(message));
		memset(&control, 0, sizeof(control));
		vector.iov_base = audio->input + audio->input_used;
		vector.iov_len = sizeof(audio->input) - audio->input_used;
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		message.msg_control = control.space;
		message.msg_controllen = sizeof(control.space);
		flags = 0;
		if (!wait)
			flags = MSG_DONTWAIT;
		count = recvmsg(audio->socket, &message, flags);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return EAGAIN;
		if (count <= 0)
			return EPIPE;
		audio->input_used += (size_t)count;

		/* The descriptor, when one came. */
		message_control = CMSG_FIRSTHDR(&message);
		if (message_control != NULL && message_control->cmsg_level == SOL_SOCKET && message_control->cmsg_type == SCM_RIGHTS) {
			if (descriptor != NULL)
				memcpy(descriptor, CMSG_DATA(message_control), sizeof(int));
		}
	}
}

/* Reads and passes over the events audiod sent, without waiting. */
static void
audio_drain(
	struct vp_audio *audio)
{
	const struct audiod_header *header;
	int error;

	/* Each whole message there now. */
	(void)pthread_mutex_lock(&audio->lock);
	for (;;) {
		error = audio_receive(audio, 0, NULL);
		if (error != 0)
			break;
		header = (const struct audiod_header *)(const void *)audio->input;
		memmove(audio->input, audio->input + header->length, audio->input_used - header->length);
		audio->input_used -= header->length;
	}

	/* The socket is the other thread's again. */
	(void)pthread_mutex_unlock(&audio->lock);
}

/* The stream's shared header. */
static struct audiod_shm_header *
audio_header(
	const struct vp_audio *audio)
{
	/* At the start of the memory. */
	return (struct audiod_shm_header *)audio->shm;
}
