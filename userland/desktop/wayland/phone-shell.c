/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The phone on the wire (ws170-p004, plan/ws170/phase001/phase.md section
 * 3): kl_system_phone_v1, made by the system manager's get_phone (since
 * its version 16) for a client of the compositor's own user.
 *
 *   send(request, channel, to, text)  a message to a number
 *   call(request, channel, to)        a call to a number
 *
 * Each is answered by result(request, OK) when the backend took it, or
 * result(request, UNAVAILABLE) without one; the backend then tells
 * status(request, state) and, for a message that comes,
 * received(channel, from, text, time) to every phone object.
 *
 * The backends are a table; the desktop's setting phone.backend chooses
 * one (0 none).  The only one now is loopback, for the tests and a demo:
 * a message is sent and delivered at once and comes back from the same
 * number as "Echo: <words>"; a call is not answered.  A backend of a modem,
 * a paired phone or VoIP that needs the system goes to libkeiland-backend
 * (Guardrail) later.  Numbers and words are never logged, only their
 * lengths.
 */

#include "kwl.h"

#include "userland/desktop/libkeiland/system/kl-system-protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Marks a parameter a function has to take but does not use (a backend's that ignores the number). */
#define UNUSED_PARAMETER(name)	((void)(name))

/* The longest number and words a request carries, with their NULs (keiland.h's KL_PHONE_NUMBER_MAX and _TEXT_MAX). */
#define PHONE_NUMBER_MAX	64U
#define PHONE_TEXT_MAX		1024U

/* What the loopback backend puts before the words it sends back. */
#define PHONE_ECHO		"Echo: "

/* The longest received event: the channel, two strings, the time's two words. */
#define PHONE_EVENT_MAX		(4U + 4U + PHONE_NUMBER_MAX + 4U + PHONE_TEXT_MAX + 8U + 8U)

/*
 * A backend: its name for the log, and how it sends a message and makes a
 * call for a phone object's request (the result was already sent; the
 * backend tells the states and what comes).
 */
struct phone_backend {
	const char *name;
	void (*send)(struct kwl_object *object, uint32_t request, uint32_t channel, const char *to, const char *text);
	void (*call)(struct kwl_object *object, uint32_t request, uint32_t channel, const char *to);
};

static void phone_loopback_send(struct kwl_object *object, uint32_t request, uint32_t channel, const char *to, const char *text);
static void phone_loopback_call(struct kwl_object *object, uint32_t request, uint32_t channel, const char *to);

/* The backends by phone.backend's value (index 0 is none).  The table is constant for the compositor's life. */
static const struct phone_backend phone_backends[] = {
	{ "none", NULL, NULL },
	{ "loopback", phone_loopback_send, phone_loopback_call }
};

static int phone_send(struct kwl_object *object, const unsigned char *bytes, size_t size);
static int phone_call(struct kwl_object *object, const unsigned char *bytes, size_t size);
static const struct phone_backend *phone_backend(struct kwl_server *server);
static void phone_received(struct kwl_server *server, uint32_t channel, const char *from, const char *text);
static void phone_status(struct kwl_object *object, uint32_t request, uint32_t state);
static void phone_result(struct kwl_object *object, uint32_t request, uint32_t applied);
static int phone_string(const unsigned char *bytes, size_t size, size_t offset, size_t bound, const char **text, size_t *next);
static size_t phone_put_string(unsigned char *payload, size_t offset, const char *text);
static uint32_t phone_word(const unsigned char *bytes, size_t offset);

/*
 * Makes a phone object for a manager's get_phone (new id).  Returns 0, or
 * EPROTO for a malformed request.
 */
int
kwl_phone_create(
	struct kwl_object *manager,
	const unsigned char *bytes,
	size_t size)
{
	struct kwl_object *created;
	uint32_t id;

	/* The new object's ID. */
	if (size != 4U)
		return EPROTO;
	id = phone_word(bytes, 0U);

	/* The object, under the ID the client chose. */
	created = kwl_create(manager->client, id, KWL_SYSTEM_PHONE, manager->version);
	if (created == NULL)
		return EPROTO;

	/* The log the tests read. */
	printf("KWL PHONE object client=%llu id=%u\n", (unsigned long long)manager->client->number, id);

	/* Succeeded: the object is the client's. */
	return 0;
}

/*
 * Carries out a request of a phone object.  Returns 0, or EPROTO for a
 * malformed request.
 */
int
kwl_phone_request(
	struct kwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* The object goes. */
	if (opcode == KL_SYSTEM_PHONE_DESTROY) {
		if (size != 0U)
			return EPROTO;
		kwl_object_destroy(object);
		return 0;
	}

	/* A message to send. */
	if (opcode == KL_SYSTEM_PHONE_SEND) {
		error = phone_send(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* A call to make. */
	if (opcode == KL_SYSTEM_PHONE_CALL) {
		error = phone_call(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* No other request. */
	return EPROTO;
}

/* Carries out send(request, channel, to, text) with the backend. */
static int
phone_send(
	struct kwl_object *object,
	const unsigned char *bytes,
	size_t size)
{
	const struct phone_backend *backend;
	const char *to;
	const char *text;
	uint32_t request;
	uint32_t channel;
	size_t offset;
	int error;

	/* The request's number and the channel. */
	if (size < 8U)
		return EPROTO;
	request = phone_word(bytes, 0U);
	channel = phone_word(bytes, 4U);

	/* The number. */
	error = phone_string(bytes, size, 8U, PHONE_NUMBER_MAX, &to, &offset);
	if (error != 0)
		return error;

	/* The words, the last argument. */
	error = phone_string(bytes, size, offset, PHONE_TEXT_MAX, &text, &offset);
	if (error != 0)
		return error;
	if (offset != size)
		return EPROTO;

	/* A channel of messages, and a number. */
	if (channel > 2U || to[0] == '\0') {
		phone_result(object, request, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* Without a backend: unavailable. */
	backend = phone_backend(object->client->server);
	printf("KWL PHONE send client=%llu channel=%u to=%lu text=%lu backend=%s\n", (unsigned long long)object->client->number,
	    channel, (unsigned long)strlen(to), (unsigned long)strlen(text), backend->name);
	if (backend->send == NULL) {
		phone_result(object, request, KL_SYSTEM_RESULT_UNAVAILABLE);
		return 0;
	}

	/* Taken, then the backend's. */
	phone_result(object, request, KL_SYSTEM_RESULT_OK);
	backend->send(object, request, channel, to, text);
	return 0;
}

/* Carries out call(request, channel, to) with the backend. */
static int
phone_call(
	struct kwl_object *object,
	const unsigned char *bytes,
	size_t size)
{
	const struct phone_backend *backend;
	const char *to;
	uint32_t request;
	uint32_t channel;
	size_t offset;
	int error;

	/* The request's number and the channel. */
	if (size < 8U)
		return EPROTO;
	request = phone_word(bytes, 0U);
	channel = phone_word(bytes, 4U);

	/* The number, the last argument. */
	error = phone_string(bytes, size, 8U, PHONE_NUMBER_MAX, &to, &offset);
	if (error != 0)
		return error;
	if (offset != size)
		return EPROTO;

	/* A channel of calls, and a number. */
	if ((channel != 3U && channel != 4U) || to[0] == '\0') {
		phone_result(object, request, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* Without a backend: unavailable. */
	backend = phone_backend(object->client->server);
	printf("KWL PHONE call client=%llu channel=%u to=%lu backend=%s\n", (unsigned long long)object->client->number,
	    channel, (unsigned long)strlen(to), backend->name);
	if (backend->call == NULL) {
		phone_result(object, request, KL_SYSTEM_RESULT_UNAVAILABLE);
		return 0;
	}

	/* Taken, then the backend's. */
	phone_result(object, request, KL_SYSTEM_RESULT_OK);
	backend->call(object, request, channel, to);
	return 0;
}

/* The loopback backend's message: sent, delivered, and back from the same number. */
static void
phone_loopback_send(
	struct kwl_object *object,
	uint32_t request,
	uint32_t channel,
	const char *to,
	const char *text)
{
	char echo[PHONE_TEXT_MAX];

	/* Sent and delivered at once. */
	phone_status(object, request, KL_SYSTEM_PHONE_SENT);
	phone_status(object, request, KL_SYSTEM_PHONE_DELIVERED);

	/* The answer from the same number, on the same channel. */
	(void)snprintf(echo, sizeof(echo), "%s%s", PHONE_ECHO, text);
	phone_received(object->client->server, channel, to, echo);
}

/* The loopback backend's call: nobody answers. */
static void
phone_loopback_call(
	struct kwl_object *object,
	uint32_t request,
	uint32_t channel,
	const char *to)
{
	UNUSED_PARAMETER(channel);
	UNUSED_PARAMETER(to);

	/* Not answered. */
	phone_status(object, request, KL_SYSTEM_PHONE_NO_ANSWER);
}

/* Finds the backend the desktop's setting chooses (none for a value not known or without settings). */
static const struct phone_backend *
phone_backend(
	struct kwl_server *server)
{
	int number;
	int error;

	/* The setting. */
	error = kwl_settings_number(server, KL_SYSTEM_PHONE_SETTING, &number);
	if (error != 0)
		return &phone_backends[0];

	/* A value of the table. */
	if (number < 0 || (size_t)number >= sizeof(phone_backends) / sizeof(phone_backends[0]))
		return &phone_backends[0];

	/* Its backend. */
	return &phone_backends[number];
}

/* Tells every phone object of every client that a message came. */
static void
phone_received(
	struct kwl_server *server,
	uint32_t channel,
	const char *from,
	const char *text)
{
	static unsigned char payload[PHONE_EVENT_MAX];
	struct kwl_client *client;
	struct kwl_object *object;
	uint64_t now;
	uint32_t words[2];
	size_t length;
	unsigned told;

	/* The event: the channel, the number, the words and the time in seconds (high and low words). */
	now = (uint64_t)time(NULL);
	memcpy(payload, &channel, 4U);
	length = phone_put_string(payload, 4U, from);
	length = phone_put_string(payload, length, text);
	words[0] = (uint32_t)(now >> 32);
	words[1] = (uint32_t)(now & 0xffffffffU);
	memcpy(payload + length, words, sizeof(words));
	length += sizeof(words);

	/* Each live phone object of each client that is not being ended. */
	told = 0;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (object = client->objects; object != NULL; object = object->next) {
			if (object->dead || object->kind != KWL_SYSTEM_PHONE)
				continue;
			(void)kwl_emit(client, object->id, KL_SYSTEM_PHONE_EVENT_RECEIVED, payload, length);
			told++;
		}
	}

	/* The log the tests read: lengths only. */
	printf("KWL PHONE received channel=%u from=%lu text=%lu told=%u\n", channel, (unsigned long)strlen(from), (unsigned long)strlen(text), told);
}

/* Tells a request's state: status(request, state). */
static void
phone_status(
	struct kwl_object *object,
	uint32_t request,
	uint32_t state)
{
	uint32_t words[2];

	/* request, state. */
	words[0] = request;
	words[1] = state;
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_PHONE_EVENT_STATUS, words, sizeof(words));
	printf("KWL PHONE status client=%llu request=%u state=%u\n", (unsigned long long)object->client->number, request, state);
}

/* Answers a request: result(request, applied, saved). */
static void
phone_result(
	struct kwl_object *object,
	uint32_t request,
	uint32_t applied)
{
	uint32_t words[3];

	/* request, applied, saved. */
	words[0] = request;
	words[1] = applied;
	words[2] = applied;
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_PHONE_EVENT_RESULT, words, sizeof(words));
}

/*
 * Reads a string argument in place, at most bound bytes with its NUL;
 * *next is the offset after it.  Returns 0, or EPROTO.
 */
static int
phone_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	size_t bound,
	const char **text,
	size_t *next)
{
	const void *inner;
	uint32_t length;
	size_t padded;

	/* The length, with the NUL, within the request. */
	if (offset + 4U > size)
		return EPROTO;
	length = phone_word(bytes, offset);

	/* Within the bound. */
	if (length == 0U || length > bound)
		return EPROTO;

	/* The bytes, padded to a word, within the request. */
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	if (offset + 4U + padded > size)
		return EPROTO;

	/* The text ends with its NUL. */
	if (bytes[offset + 4U + length - 1U] != '\0')
		return EPROTO;

	/* And has no other. */
	inner = memchr(bytes + offset + 4U, '\0', length - 1U);
	if (inner != NULL)
		return EPROTO;

	/* Succeeded: the text where it is, and where the next argument starts. */
	*text = (const char *)(bytes + offset + 4U);
	*next = offset + 4U + padded;
	return 0;
}

/* Writes a string argument and reports the offset after it. */
static size_t
phone_put_string(
	unsigned char *payload,
	size_t offset,
	const char *text)
{
	uint32_t length;
	size_t padded;

	/* The length with the NUL, the bytes, and zeros to a four-byte boundary. */
	length = (uint32_t)strlen(text) + 1U;
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	memcpy(payload + offset, &length, sizeof(length));
	memset(payload + offset + 4U, 0, padded);
	memcpy(payload + offset + 4U, text, length - 1U);

	/* The offset after the string. */
	return offset + 4U + padded;
}

/* Reads a 32-bit word of a request in the wire's native byte order. */
static uint32_t
phone_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The word. */
	memcpy(&word, bytes + offset, sizeof(word));
	return word;
}
