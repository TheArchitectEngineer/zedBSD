/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arrivals of mail on the wire (ws169-p002, plan/ws169/phase001/
 * phase.md section 1): kl_system_mail_v1, made by the system manager's
 * get_mail (since its version 15) for a client of the compositor's own
 * user.
 *
 *   arrived(request, account, from, subject, code)  the mail program tells
 *       of a new message; it is told on as mail(from, subject, code) to
 *       every listener whose mail.codes.<app> setting is on now, and the
 *       request is answered by result(request, OK)
 *   listen(request, app)  a reader asks to hear the arrivals under its
 *       name; a name the settings do not know is answered INVALID
 *
 * The listeners are kept in a small table of their client, object and
 * name; one whose client or object went is skipped and its row reused.
 * The words of a message are never logged, only their lengths, since a
 * sign-in code is a secret.
 */

#include "kwl.h"

#include "userland/desktop/settings-keys/settings-keys.h"

#include "userland/desktop/libkeiland/system/kl-system-protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The longest string an arrived or listen carries, with its NUL (keiland.h's KL_MAIL_TEXT_MAX). */
#define MAIL_WIRE_TEXT_MAX	256U

/* The most listeners at once. */
#define MAIL_LISTENERS_MAX	16U

/* The longest reader's name, with its NUL (the setting's name must fit KL_SETTINGS_KEY_MAX). */
#define MAIL_APP_MAX		32U

/* The longest event: three strings of MAIL_WIRE_TEXT_MAX with their lengths. */
#define MAIL_EVENT_MAX		(3U * (4U + MAIL_WIRE_TEXT_MAX))

/*
 * One reader that asked to hear the arrivals: its client's number and its
 * mail object's ID (the object is found again by them, so that a client
 * that went is never followed), and its name.  A row whose client is 0 is
 * free.
 */
struct mail_listener {
	uint64_t client;
	uint32_t object;
	char app[MAIL_APP_MAX];
};

/*
 * The readers listening, for the compositor's life.  Rows are taken by
 * listen and freed when a send finds their client or object gone; the
 * event loop's thread alone touches them.
 */
static struct mail_listener mail_listeners[MAIL_LISTENERS_MAX];

static int mail_arrived(struct kwl_object *object, const unsigned char *bytes, size_t size);
static int mail_listen(struct kwl_object *object, const unsigned char *bytes, size_t size);
static int mail_tell(struct kwl_server *server, const struct mail_listener *listener, const unsigned char *payload, size_t size);
static int mail_allowed(struct kwl_server *server, const char *app);
static void mail_result(struct kwl_object *object, uint32_t request, uint32_t applied);
static int mail_string(const unsigned char *bytes, size_t size, size_t offset, const char **text, size_t *next);
static size_t mail_put_string(unsigned char *payload, size_t offset, const char *text);
static uint32_t mail_word(const unsigned char *bytes, size_t offset);

/*
 * Makes a mail object for a manager's get_mail (new id).  Returns 0, or
 * EPROTO for a malformed request.
 */
int
kwl_mail_create(
	struct kwl_object *manager,
	const unsigned char *bytes,
	size_t size)
{
	struct kwl_object *created;
	uint32_t id;

	/* The new object's ID. */
	if (size != 4U)
		return EPROTO;
	id = mail_word(bytes, 0U);

	/* The object, under the ID the client chose. */
	created = kwl_create(manager->client, id, KWL_SYSTEM_MAIL, manager->version);
	if (created == NULL)
		return EPROTO;

	/* The log the tests read. */
	printf("KWL MAIL object client=%llu id=%u\n", (unsigned long long)manager->client->number, id);

	/* Succeeded: the object is the client's. */
	return 0;
}

/*
 * Carries out a request of a mail object.  Returns 0, or EPROTO for a
 * malformed request.
 */
int
kwl_mail_request(
	struct kwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* The object goes (a listener's row is freed by the next send). */
	if (opcode == KL_SYSTEM_MAIL_DESTROY) {
		if (size != 0U)
			return EPROTO;
		kwl_object_destroy(object);
		return 0;
	}

	/* A message arrived. */
	if (opcode == KL_SYSTEM_MAIL_ARRIVED) {
		error = mail_arrived(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* A reader asks to listen. */
	if (opcode == KL_SYSTEM_MAIL_LISTEN) {
		error = mail_listen(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* No other request. */
	return EPROTO;
}

/* Carries out arrived(request, account, from, subject, code): told to each allowed listener. */
static int
mail_arrived(
	struct kwl_object *object,
	const unsigned char *bytes,
	size_t size)
{
	static unsigned char payload[MAIL_EVENT_MAX];
	const char *account;
	const char *from;
	const char *subject;
	const char *code;
	size_t offset;
	size_t length;
	uint32_t request;
	unsigned index;
	unsigned told;
	int allowed;
	int error;

	/* The request's number. */
	if (size < 4U)
		return EPROTO;
	request = mail_word(bytes, 0U);

	/* The account. */
	error = mail_string(bytes, size, 4U, &account, &offset);
	if (error != 0)
		return error;

	/* The sender. */
	error = mail_string(bytes, size, offset, &from, &offset);
	if (error != 0)
		return error;

	/* The subject. */
	error = mail_string(bytes, size, offset, &subject, &offset);
	if (error != 0)
		return error;

	/* The sign-in code, the last argument. */
	error = mail_string(bytes, size, offset, &code, &offset);
	if (error != 0)
		return error;
	if (offset != size)
		return EPROTO;

	/* The event every listener hears: mail(from, subject, code). */
	length = mail_put_string(payload, 0U, from);
	length = mail_put_string(payload, length, subject);
	length = mail_put_string(payload, length, code);

	/* Each listener that is still there and allowed now. */
	told = 0U;
	for (index = 0U; index < MAIL_LISTENERS_MAX; index++) {
		/* A free row. */
		if (mail_listeners[index].client == 0U)
			continue;

		/* Not allowed by the user's setting: not told, but kept for a later one. */
		allowed = mail_allowed(object->client->server, mail_listeners[index].app);
		if (!allowed)
			continue;

		/* Told; a listener gone frees its row. */
		error = mail_tell(object->client->server, &mail_listeners[index], payload, length);
		if (error != 0) {
			memset(&mail_listeners[index], 0, sizeof(mail_listeners[index]));
			continue;
		}

		/* One more told. */
		told++;
	}

	/* The log the tests read: lengths only, the words stay private. */
	printf("KWL MAIL arrived client=%llu account=%lu from=%lu subject=%lu code=%lu told=%u\n",
	       (unsigned long long)object->client->number,
	       (unsigned long)strlen(account),
	       (unsigned long)strlen(from),
	       (unsigned long)strlen(subject),
	       (unsigned long)strlen(code),
	       told);

	/* Answered. */
	mail_result(object, request, KL_SYSTEM_RESULT_OK);
	return 0;
}

/* Carries out listen(request, app): a reader the settings know is kept. */
static int
mail_listen(
	struct kwl_object *object,
	const unsigned char *bytes,
	size_t size)
{
	char key[KL_SETTINGS_KEY_MAX];
	const char *app;
	size_t offset;
	size_t length;
	uint32_t request;
	unsigned index;
	unsigned free_row;
	int written;
	int number;
	int error;

	/* The request's number. */
	if (size < 4U)
		return EPROTO;
	request = mail_word(bytes, 0U);

	/* The reader's name, the last argument. */
	error = mail_string(bytes, size, 4U, &app, &offset);
	if (error != 0)
		return error;
	if (offset != size)
		return EPROTO;

	/* A name that fits a row. */
	length = strlen(app);
	if (length == 0U || length >= MAIL_APP_MAX) {
		mail_result(object, request, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* The setting that allows it, which must be one the settings know. */
	written = snprintf(key, sizeof(key), "%s%s", KL_SYSTEM_MAIL_SETTING_PREFIX, app);
	if (written < 0 || (size_t)written >= sizeof(key)) {
		mail_result(object, request, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* Without settings (the login screen) or a setting the table does not have, refused. */
	error = kwl_settings_number(object->client->server, key, &number);
	if (error != 0) {
		printf("KWL MAIL listen-refused client=%llu app=%s\n", (unsigned long long)object->client->number, app);
		mail_result(object, request, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* A free row, or the object's own when it asks again. */
	free_row = MAIL_LISTENERS_MAX;
	for (index = 0U; index < MAIL_LISTENERS_MAX; index++) {
		/* The object's own row. */
		if (mail_listeners[index].client == object->client->number && mail_listeners[index].object == object->id) {
			free_row = index;
			break;
		}

		/* The first free row. */
		if (mail_listeners[index].client == 0U && free_row == MAIL_LISTENERS_MAX)
			free_row = index;
	}

	/* No room. */
	if (free_row == MAIL_LISTENERS_MAX) {
		mail_result(object, request, KL_SYSTEM_RESULT_BUSY);
		return 0;
	}

	/* The row: the client, the object and the name. */
	mail_listeners[free_row].client = object->client->number;
	mail_listeners[free_row].object = object->id;
	memcpy(mail_listeners[free_row].app, app, length + 1U);

	/* Answered, and logged for the tests. */
	printf("KWL MAIL listen client=%llu app=%s\n", (unsigned long long)object->client->number, app);
	mail_result(object, request, KL_SYSTEM_RESULT_OK);
	return 0;
}

/* Sends mail(...) to a listener; ENOENT when its client or its object went. */
static int
mail_tell(
	struct kwl_server *server,
	const struct mail_listener *listener,
	const unsigned char *payload,
	size_t size)
{
	struct kwl_client *client;
	struct kwl_object *object;
	int error;

	/* The listener's client, still connected. */
	for (client = server->clients; client != NULL; client = client->next) {
		/* Another client. */
		if (client->number != listener->client)
			continue;

		/* A client being ended hears nothing more. */
		if (client->fatal)
			return ENOENT;

		/* Its mail object, still alive. */
		object = kwl_find(client, listener->object);
		if (object == NULL ||
		    object->dead ||
		    object->kind != KWL_SYSTEM_MAIL)
			return ENOENT;

		/* The event. */
		error = kwl_emit(client, object->id, KL_SYSTEM_MAIL_EVENT_MAIL, payload, size);
		if (error != 0)
			return ENOENT;

		/* Succeeded: the listener heard it. */
		return 0;
	}

	/* The client went. */
	return ENOENT;
}

/* Reports whether the user lets a reader hear the arrivals (its mail.codes.<app> setting on). */
static int
mail_allowed(
	struct kwl_server *server,
	const char *app)
{
	char key[KL_SETTINGS_KEY_MAX];
	int written;
	int number;
	int error;

	/* The setting's name. */
	written = snprintf(key, sizeof(key), "%s%s", KL_SYSTEM_MAIL_SETTING_PREFIX, app);
	if (written < 0 || (size_t)written >= sizeof(key))
		return 0;

	/* Its value now; no settings or no such setting is not allowed. */
	error = kwl_settings_number(server, key, &number);
	if (error != 0)
		return 0;

	/* Allowed only when it is on. */
	if (number != 1)
		return 0;

	/* Allowed. */
	return 1;
}

/* Answers a request: result(request, applied, saved). */
static void
mail_result(
	struct kwl_object *object,
	uint32_t request,
	uint32_t applied)
{
	uint32_t words[3];

	/* request, applied, saved. */
	words[0] = request;
	words[1] = applied;
	words[2] = applied;
	(void)kwl_emit(object->client, object->id, KL_SYSTEM_MAIL_EVENT_RESULT, words, sizeof(words));
}

/*
 * Reads a string argument in place; *next is the offset after it.
 * Returns 0, or EPROTO for one that does not fit the request or the bound.
 */
static int
mail_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	const char **text,
	size_t *next)
{
	const void *inner;
	uint32_t length;
	size_t padded;

	/* The length, with the NUL, within the request. */
	if (offset + 4U > size)
		return EPROTO;
	length = mail_word(bytes, offset);

	/* Within the bound. */
	if (length == 0U || length > MAIL_WIRE_TEXT_MAX)
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

/* Writes a string argument (its length with the NUL, the bytes, zeros to a word) and reports the offset after it. */
static size_t
mail_put_string(
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
mail_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The word. */
	memcpy(&word, bytes + offset, sizeof(word));
	return word;
}
