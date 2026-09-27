/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The clipboard between clients (ws035-p079): wl_data_device_manager
 * (version 3), wl_data_source, wl_data_device and wl_data_offer.
 *
 * A client offers data by making a wl_data_source with the MIME types it
 * has and setting it as the selection.  The client with the keyboard is
 * told the selection: a new wl_data_offer (made by the compositor, from
 * the server's ID range) with the same types, then the selection event; it
 * is told again whenever the selection changes while it has the keyboard,
 * and a client that gets the keyboard is told first.  A client that wants
 * the data asks the offer to receive a type into a descriptor; the source's
 * client is asked to send that type into it.  A new selection cancels the
 * source it replaces; a source that goes empties the clipboard.
 *
 * Drag and drop is not offered yet: start_drag cancels its source at once.
 */

#include "data.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The requests of wl_data_device_manager. */
#define MANAGER_CREATE_DATA_SOURCE	0U
#define MANAGER_GET_DATA_DEVICE		1U

/* The requests and events of wl_data_source. */
#define SOURCE_OFFER			0U
#define SOURCE_DESTROY			1U
#define SOURCE_SET_ACTIONS		2U
#define SOURCE_SEND			1U
#define SOURCE_CANCELLED		2U

/* The requests and events of wl_data_device. */
#define DEVICE_START_DRAG		0U
#define DEVICE_SET_SELECTION		1U
#define DEVICE_RELEASE			2U
#define DEVICE_DATA_OFFER		0U
#define DEVICE_SELECTION		5U

/* The requests and events of wl_data_offer. */
#define OFFER_ACCEPT			0U
#define OFFER_RECEIVE			1U
#define OFFER_DESTROY			2U
#define OFFER_FINISH			3U
#define OFFER_SET_ACTIONS		4U
#define OFFER_OFFER			0U

/* The most MIME types one source may offer, and the longest type (with its NUL). */
#define DATA_MIME_MAX			32U
#define DATA_MIME_LENGTH		256U

static int manager_request(struct zwl_object *manager, uint32_t opcode, const unsigned char *bytes, size_t size);
static int source_request(struct zwl_object *source, uint32_t opcode, const unsigned char *bytes, size_t size);
static int device_request(struct zwl_object *device, uint32_t opcode, const unsigned char *bytes, size_t size);
static int offer_request(struct zwl_object *offer, uint32_t opcode, const unsigned char *bytes, size_t size);
static int set_selection(struct zwl_object *device, uint32_t source_id);
static void selection_changed(struct zwl_server *server);
static void send_selection(struct zwl_server *server, struct zwl_client *client);
static void send_device_selection(struct zwl_server *server, struct zwl_object *device);
static int emit_string(struct zwl_client *client, uint32_t id, uint32_t opcode, const char *text, int descriptor);
static int read_string(const unsigned char *bytes, size_t size, size_t offset, const char **text, size_t *next);
static uint32_t data_word(const unsigned char *bytes, size_t offset);

/*
 * Carries out a request of one of the data-sharing interfaces.
 */
int
zwl_data_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* Each interface has its own requests. */
	switch (object->kind) {
	case ZWL_DATA_MANAGER:
		error = manager_request(object, opcode, bytes, size);
		break;
	case ZWL_DATA_SOURCE:
		error = source_request(object, opcode, bytes, size);
		break;
	case ZWL_DATA_DEVICE:
		error = device_request(object, opcode, bytes, size);
		break;
	case ZWL_DATA_OFFER:
		error = offer_request(object, opcode, bytes, size);
		break;
	default:
		error = EPROTO;
		break;
	}

	/* Reports a request that was refused (EAGAIN: its descriptor has not come yet). */
	if (error != 0)
		return error;

	/* Succeeded: the request was carried out. */
	return 0;
}

/*
 * Tells the client that is getting the keyboard the selection, before its
 * keyboard hears enter (a client that was told it last is not told again).
 */
void
zwl_data_focus(
	struct zwl_server *server,
	struct zwl_object *focus)
{
	/* No new focus, or the client told last. */
	if (focus == NULL || focus->client->number == server->selection_client)
		return;

	/* The new focus's client hears the selection. */
	send_selection(server, focus->client);
}

/*
 * Unties an object that is going from the clipboard: a source's types are
 * freed, its offers lose it, and when it was the selection the clipboard
 * is empty (the keyboard's client is told).
 */
void
zwl_data_object_gone(
	struct zwl_object *object)
{
	struct zwl_server *server;
	struct zwl_client *client;
	struct zwl_object *other;
	unsigned index;

	/* Only a source has anything to untie. */
	if (object->kind != ZWL_DATA_SOURCE)
		return;

	/* Its types. */
	for (index = 0; index < object->mime_count; index++)
		free(object->mime_types[index]);
	free(object->mime_types);
	object->mime_types = NULL;
	object->mime_count = 0;

	/* The offers made from it have no source any more. */
	server = object->client->server;
	for (client = server->clients; client != NULL; client = client->next) {
		/* Each client's offers. */
		for (other = client->objects; other != NULL; other = other->next) {
			/* An offer of this source. */
			if (other->kind == ZWL_DATA_OFFER && other->data_source == object)
				other->data_source = NULL;
		}
	}

	/* The selection it was: the clipboard is empty now. */
	if (server->selection == object) {
		server->selection = NULL;
		printf("ZWL DATA selection none (source gone)\n");
		selection_changed(server);
	}
}

/* Carries out a request of wl_data_device_manager: a new source, or a seat's data device. */
static int
manager_request(
	struct zwl_object *manager,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *created;
	struct zwl_object *seat;
	struct zwl_server *server;
	uint32_t id;
	uint32_t seat_id;

	/* A source: its new ID. */
	if (opcode == MANAGER_CREATE_DATA_SOURCE) {
		if (size != 4U)
			return EPROTO;
		id = data_word(bytes, 0U);
		created = zwl_create(manager->client, id, ZWL_DATA_SOURCE, manager->version);
		if (created == NULL)
			return EPROTO;
		return 0;
	}

	/* Only get_data_device is left: its new ID and the client's seat. */
	if (opcode != MANAGER_GET_DATA_DEVICE || size != 8U)
		return EPROTO;
	id = data_word(bytes, 0U);
	seat_id = data_word(bytes, 4U);
	seat = zwl_find(manager->client, seat_id);
	if (seat == NULL || seat->kind != ZWL_SEAT)
		return EPROTO;
	created = zwl_create(manager->client, id, ZWL_DATA_DEVICE, manager->version);
	if (created == NULL)
		return EPROTO;

	/* A device of the client with the keyboard hears the selection at once. */
	server = manager->client->server;
	if (server->focus != NULL && server->focus->client == manager->client)
		send_device_selection(server, created);

	/* Succeeded: the client has a data device. */
	return 0;
}

/* Carries out a request of wl_data_source: a type offered, destroy, or the drag actions. */
static int
source_request(
	struct zwl_object *source,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	const char *text;
	char **types;
	size_t next;
	size_t length;
	int error;

	/* The source goes (zwl_data_object_gone empties the clipboard when it held it). */
	if (opcode == SOURCE_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(source);
		return 0;
	}

	/* The drag actions, which are not used yet. */
	if (opcode == SOURCE_SET_ACTIONS) {
		if (size != 4U)
			return EPROTO;
		return 0;
	}

	/* Only offer is left: one MIME type. */
	if (opcode != SOURCE_OFFER)
		return EPROTO;
	error = read_string(bytes, size, 0U, &text, &next);
	if (error != 0 || next != size)
		return EPROTO;

	/* A type too long, or one too many, is left out. */
	length = strlen(text);
	if (length >= DATA_MIME_LENGTH || source->mime_count == DATA_MIME_MAX)
		return 0;

	/* One more slot for it. */
	types = realloc(source->mime_types, (source->mime_count + 1U) * sizeof(*types));
	if (types == NULL)
		return 0;
	source->mime_types = types;

	/* Its copy. */
	types[source->mime_count] = strdup(text);
	if (types[source->mime_count] == NULL)
		return 0;
	source->mime_count++;

	/* Succeeded: the source has the type. */
	return 0;
}

/* Carries out a request of wl_data_device: a drag, the selection, or release. */
static int
device_request(
	struct zwl_object *device,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *source;
	uint32_t source_id;
	int error;

	/* Each request by its opcode. */
	switch (opcode) {
	case DEVICE_START_DRAG:
		/* A drag (source, origin, icon, serial) is not offered: its source is cancelled at once. */
		if (size != 16U)
			return EPROTO;
		source_id = data_word(bytes, 0U);
		source = NULL;
		if (source_id != 0U)
			source = zwl_find(device->client, source_id);
		if (source != NULL && source->kind == ZWL_DATA_SOURCE)
			(void)zwl_emit(source->client, source->id, SOURCE_CANCELLED, NULL, 0U);
		printf("ZWL DATA drag refused client=%llu\n", (unsigned long long)device->client->number);
		return 0;
	case DEVICE_SET_SELECTION:
		/* The selection (a source or none) and the serial of the input that asked. */
		if (size != 8U)
			return EPROTO;
		source_id = data_word(bytes, 0U);
		error = set_selection(device, source_id);
		if (error != 0)
			return error;
		return 0;
	case DEVICE_RELEASE:
		/* The device goes (version 2). */
		if (size != 0U || device->version < 2U)
			return EPROTO;
		zwl_object_destroy(device);
		return 0;
	default:
		break;
	}

	/* No other request exists. */
	return EPROTO;
}

/* Carries out a request of wl_data_offer: receive a type into a descriptor, destroy, or the drag requests. */
static int
offer_request(
	struct zwl_object *offer,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *source;
	const char *text;
	size_t next;
	int descriptor;
	int error;

	/* Each request by its opcode. */
	switch (opcode) {
	case OFFER_ACCEPT:
	case OFFER_FINISH:
	case OFFER_SET_ACTIONS:
		/* The drag requests have nothing to do without drag and drop. */
		return 0;
	case OFFER_DESTROY:
		/* The offer goes. */
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(offer);
		return 0;
	case OFFER_RECEIVE:
		break;
	default:
		return EPROTO;
	}

	/* receive: the type, and the descriptor beside the message. */
	error = read_string(bytes, size, 0U, &text, &next);
	if (error != 0 || next != size)
		return EPROTO;
	descriptor = zwl_take_fd(offer->client);
	if (descriptor < 0)
		return EAGAIN;

	/* An offer whose source has gone has nothing to send; the descriptor is closed (the reader sees its end). */
	source = offer->data_source;
	if (source == NULL || source->dead || source->client->fatal) {
		close(descriptor);
		printf("ZWL DATA receive client=%llu mime=%s source=none\n", (unsigned long long)offer->client->number, text);
		return 0;
	}

	/* Succeeded: the source's client writes the type into the descriptor (the event carries it away). */
	printf("ZWL DATA receive client=%llu mime=%s source=%llu\n", (unsigned long long)offer->client->number, text, (unsigned long long)source->client->number);
	(void)emit_string(source->client, source->id, SOURCE_SEND, text, descriptor);
	return 0;
}

/* Sets the selection from a client's device: a source of the client, or none; the replaced source is cancelled. */
static int
set_selection(
	struct zwl_object *device,
	uint32_t source_id)
{
	struct zwl_server *server;
	struct zwl_object *source;
	struct zwl_object *previous;
	unsigned types;

	/* The source must be one of the client's. */
	source = NULL;
	if (source_id != 0U) {
		source = zwl_find(device->client, source_id);
		if (source == NULL || source->kind != ZWL_DATA_SOURCE)
			return EPROTO;
	}

	/* An unchanged selection tells nobody. */
	server = device->client->server;
	previous = server->selection;
	if (previous == source)
		return 0;

	/* The source replaced hears that it is not the selection any more. */
	if (previous != NULL && !previous->dead)
		(void)zwl_emit(previous->client, previous->id, SOURCE_CANCELLED, NULL, 0U);

	/* The new selection (with how many types it has), and the client with the keyboard hears it. */
	server->selection = source;
	types = 0;
	if (source != NULL)
		types = source->mime_count;
	printf("ZWL DATA selection client=%llu source=%u types=%u\n", (unsigned long long)device->client->number, source_id, types);
	selection_changed(server);

	/* Succeeded: the clipboard holds the source. */
	return 0;
}

/* Tells the client with the keyboard that the selection changed. */
static void
selection_changed(
	struct zwl_server *server)
{
	/* Without a focus nobody is told now (the next focus is). */
	server->selection_client = 0;
	if (server->focus == NULL || server->focus->dead)
		return;

	/* The focused client hears it. */
	send_selection(server, server->focus->client);
}

/* Tells every data device of a client the selection. */
static void
send_selection(
	struct zwl_server *server,
	struct zwl_client *client)
{
	struct zwl_object *device;

	/* A failed client hears nothing. */
	if (client->fatal)
		return;

	/* Each live data device. */
	for (device = client->objects; device != NULL; device = device->next) {
		/* Only live devices. */
		if (device->kind == ZWL_DATA_DEVICE && !device->dead)
			send_device_selection(server, device);
	}

	/* The client has been told. */
	server->selection_client = client->number;
}

/*
 * Tells one data device the selection: a new offer with the source's types
 * and the selection event naming it, or the selection event naming none.
 */
static void
send_device_selection(
	struct zwl_server *server,
	struct zwl_object *device)
{
	struct zwl_object *source;
	struct zwl_object *offer;
	uint32_t word;
	unsigned index;

	/* An empty clipboard: the selection names no offer. */
	source = server->selection;
	word = 0;
	if (source == NULL || source->dead) {
		(void)zwl_emit(device->client, device->id, DEVICE_SELECTION, &word, sizeof(word));
		return;
	}

	/* The offer, made by the compositor. */
	offer = zwl_create_server(device->client, ZWL_DATA_OFFER, device->version);
	if (offer == NULL)
		return;
	offer->data_source = source;

	/* It is introduced, with each of the source's types. */
	word = offer->id;
	(void)zwl_emit(device->client, device->id, DEVICE_DATA_OFFER, &word, sizeof(word));
	for (index = 0; index < source->mime_count; index++)
		(void)emit_string(device->client, offer->id, OFFER_OFFER, source->mime_types[index], -1);

	/* Succeeded: the selection names it. */
	(void)zwl_emit(device->client, device->id, DEVICE_SELECTION, &word, sizeof(word));
	printf("ZWL DATA offer client=%llu offer=%u types=%u\n", (unsigned long long)device->client->number, offer->id, source->mime_count);
}

/* Sends an event whose only argument is a string, with a descriptor beside it when there is one (-1 for none). */
static int
emit_string(
	struct zwl_client *client,
	uint32_t id,
	uint32_t opcode,
	const char *text,
	int descriptor)
{
	unsigned char payload[4U + DATA_MIME_LENGTH + 4U];
	uint32_t length;
	size_t padded;
	int error;

	/* The length with its NUL, the text, the padding. */
	length = (uint32_t)strlen(text) + 1U;
	if (length > DATA_MIME_LENGTH) {
		if (descriptor >= 0)
			close(descriptor);
		return EINVAL;
	}

	/* The payload. */
	memset(payload, 0, sizeof(payload));
	memcpy(payload, &length, sizeof(length));
	memcpy(payload + 4, text, length);
	padded = 4U + (((size_t)length + 3U) & ~(size_t)3U);

	/* The event (the descriptor goes with it). */
	if (descriptor >= 0) {
		error = zwl_emit_fd(client, id, opcode, payload, padded, descriptor);
	} else {
		error = zwl_emit(client, id, opcode, payload, padded);
	}

	/* Reports an event that could not be queued. */
	if (error != 0)
		return error;

	/* Succeeded: the event is queued. */
	return 0;
}

/* Reads a string argument at an offset: its length word, its bytes with the NUL, the padding. */
static int
read_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	const char **text,
	size_t *next)
{
	uint32_t length;
	size_t padded;

	/* The length word. */
	if (offset + 4U > size)
		return EPROTO;
	length = data_word(bytes, offset);
	if (length == 0U)
		return EPROTO;

	/* The bytes and their padding must fit, and end with the NUL. */
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	if (offset + 4U + padded > size)
		return EPROTO;
	if (bytes[offset + 4U + length - 1U] != '\0')
		return EPROTO;

	/* Succeeded: the text and where the next argument starts. */
	*text = (const char *)(bytes + offset + 4U);
	*next = offset + 4U + padded;
	return 0;
}

/* Reads one native-endian protocol word. */
static uint32_t
data_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The payload need not be aligned. */
	memcpy(&word, bytes + offset, sizeof(word));

	/* Succeeded: the word. */
	return word;
}
