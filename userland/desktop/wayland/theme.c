/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * keiland_theme_v1 (ws089-p017): the desktop's appearance, light or dark,
 * told to a client when it binds the global and again whenever the
 * setting appearance.dark changes (settings.c calls zwl_theme_changed),
 * so that libkeiland's applications draw in it and redraw.  The
 * compositor's own drawing follows server->dark (glass.c).
 */

#include "zwl.h"

#include <errno.h>
#include <stdio.h>

/* The request and the event of keiland_theme_v1. */
#define THEME_DESTROY		0U
#define THEME_EVENT_APPEARANCE	0U

static int theme_send(struct zwl_object *theme);

/*
 * Carries out a request of keiland_theme_v1: destroy is the only one.
 */
int
zwl_theme_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	/* destroy, without arguments. */
	(void)bytes;
	if (opcode != THEME_DESTROY || size != 0U)
		return EPROTO;
	zwl_object_destroy(object);

	/* Succeeded. */
	return 0;
}

/*
 * Tells a client that bound the global the appearance now.
 */
int
zwl_theme_bind(
	struct zwl_object *theme)
{
	int error;

	/* The client draws in the appearance from now on (its windows' glass follows it, panels.c). */
	theme->client->theme_bound = 1U;
	theme->client->server->dirty = 1;

	/* The appearance. */
	error = theme_send(theme);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Tells every client that bound the global the appearance, after it
 * changed.
 */
void
zwl_theme_changed(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_object *object;

	/* Each live object of each client. */
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (object = client->objects; object != NULL; object = object->next) {
			if (object->kind == ZWL_THEME && !object->dead)
				(void)theme_send(object);
		}
	}

	/* Logs the appearance told, for the tests. */
	printf("ZWL THEME appearance=%d\n", (int)server->dark);
}

/* Sends one object the appearance: 0 light, 1 dark. */
static int
theme_send(
	struct zwl_object *theme)
{
	uint32_t appearance;
	int error;

	/* The appearance as the protocol's value. */
	appearance = 0U;
	if (theme->client->server->dark != 0)
		appearance = 1U;
	error = zwl_emit(theme->client, theme->id, THEME_EVENT_APPEARANCE, &appearance, sizeof(appearance));
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}
