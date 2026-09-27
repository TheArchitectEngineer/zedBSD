/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * xdg-decoration (zxdg_decoration_manager_v1, ws035-p080): which side draws
 * a window's title bar and frame.
 *
 * zdesktop draws every window's title bar itself (the glass look's floating
 * title bar with the window's menus), so it always answers server_side,
 * whatever mode a client asks for; a toolkit then leaves its own
 * decorations out.  Each answer is a configure of the decoration, followed
 * by a configure of the window once the window has had its first one.
 */

#include "extras.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The requests of the manager and of a toplevel's decoration, and the decoration's configure. */
#define MANAGER_DESTROY			0U
#define MANAGER_GET_TOPLEVEL_DECORATION	1U
#define DECORATION_DESTROY		0U
#define DECORATION_SET_MODE		1U
#define DECORATION_UNSET_MODE		2U
#define DECORATION_CONFIGURE		0U

/* The decoration modes. */
#define MODE_CLIENT_SIDE		1U
#define MODE_SERVER_SIDE		2U

/* The decoration's errors: a toplevel with one already, a mode that is not one. */
#define DECORATION_ERROR_ALREADY_CONSTRUCTED	1U
#define DECORATION_ERROR_INVALID_MODE		3U

static int decoration_create(struct zwl_object *manager, const unsigned char *bytes, size_t size);
static int decoration_answer(struct zwl_object *decoration);
static uint32_t decoration_word(const unsigned char *bytes, size_t offset);

/*
 * Carries out a request of zxdg_decoration_manager_v1 or of a toplevel's
 * zxdg_toplevel_decoration_v1.
 */
int
zwl_decoration_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t mode;
	int error;

	/* The manager: it goes, or it makes a toplevel's decoration. */
	if (object->kind == ZWL_DECORATION_MANAGER) {
		if (opcode == MANAGER_DESTROY && size == 0U) {
			zwl_object_destroy(object);
			return 0;
		}

		/* Only get_toplevel_decoration is left. */
		if (opcode != MANAGER_GET_TOPLEVEL_DECORATION)
			return EPROTO;
		error = decoration_create(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* A decoration that goes leaves its toplevel (zwl_extras_object_gone). */
	if (opcode == DECORATION_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(object);
		return 0;
	}

	/* A mode the client prefers: it must be one of the two. */
	if (opcode == DECORATION_SET_MODE) {
		if (size != 4U)
			return EPROTO;
		mode = decoration_word(bytes, 0U);
		if (mode != MODE_CLIENT_SIDE && mode != MODE_SERVER_SIDE) {
			(void)zwl_error_code(object->client, object->id, DECORATION_ERROR_INVALID_MODE, "not a decoration mode");
			return EPROTO;
		}

		/* The log line the tests read. */
		printf("ZWL DECORATION asked client=%llu mode=%u\n", (unsigned long long)object->client->number, mode);
	} else if (opcode == DECORATION_UNSET_MODE) {
		if (size != 0U)
			return EPROTO;
	} else {
		return EPROTO;
	}

	/* Succeeded: the answer is server_side again. */
	error = decoration_answer(object);
	if (error != 0)
		return error;
	return 0;
}

/*
 * Unties a toplevel or a decoration that is going from the other.
 */
void
zwl_decoration_object_gone(
	struct zwl_object *object)
{
	/* A decoration leaves its toplevel. */
	if (object->kind == ZWL_DECORATION) {
		if (object->decoration_toplevel != NULL)
			object->decoration_toplevel->decoration = NULL;
		object->decoration_toplevel = NULL;
		return;
	}

	/* A toplevel's decoration is orphaned (it answers nothing more). */
	if (object->kind == ZWL_TOPLEVEL && object->decoration != NULL) {
		object->decoration->decoration_toplevel = NULL;
		object->decoration = NULL;
	}
}

/* Makes a toplevel's decoration and answers it at once. */
static int
decoration_create(
	struct zwl_object *manager,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *toplevel;
	struct zwl_object *created;
	uint32_t id;
	uint32_t toplevel_id;
	int error;

	/* The new ID and the toplevel. */
	if (size != 8U)
		return EPROTO;
	id = decoration_word(bytes, 0U);
	toplevel_id = decoration_word(bytes, 4U);
	toplevel = zwl_find(manager->client, toplevel_id);
	if (toplevel == NULL || toplevel->kind != ZWL_TOPLEVEL)
		return EPROTO;

	/* One decoration per toplevel. */
	if (toplevel->decoration != NULL) {
		(void)zwl_error_code(manager->client, manager->id, DECORATION_ERROR_ALREADY_CONSTRUCTED, "the toplevel has a decoration");
		return EPROTO;
	}

	/* The decoration, tied to its toplevel both ways. */
	created = zwl_create(manager->client, id, ZWL_DECORATION, manager->version);
	if (created == NULL)
		return EPROTO;
	created->decoration_toplevel = toplevel;
	toplevel->decoration = created;

	/* Succeeded: the answer. */
	error = decoration_answer(created);
	if (error != 0)
		return error;
	return 0;
}

/*
 * Answers a decoration: configure with server_side, then the window's
 * configure (only once the window has had its first; the first follows its
 * first commit).
 */
static int
decoration_answer(
	struct zwl_object *decoration)
{
	struct zwl_object *toplevel;
	struct zwl_object *surface;
	uint32_t mode;
	int error;

	/* The mode zdesktop draws in. */
	mode = MODE_SERVER_SIDE;
	error = zwl_emit(decoration->client, decoration->id, DECORATION_CONFIGURE, &mode, sizeof(mode));
	if (error != 0)
		return error;
	printf("ZWL DECORATION configure client=%llu mode=%u\n", (unsigned long long)decoration->client->number, mode);

	/* The window it belongs to, when it still has one with a surface. */
	toplevel = decoration->decoration_toplevel;
	if (toplevel == NULL || toplevel->surface == NULL)
		return 0;
	surface = toplevel->surface;

	/* A window configured already hears a new configure that the mode belongs to. */
	if (surface->configured) {
		error = zwl_window_send_configure(surface);
		if (error != 0)
			return error;
	}

	/* Succeeded: the client knows zdesktop draws the decorations. */
	return 0;
}

/* Reads one native-endian protocol word. */
static uint32_t
decoration_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The payload need not be aligned. */
	memcpy(&word, bytes + offset, sizeof(word));

	/* Succeeded: the word. */
	return word;
}
