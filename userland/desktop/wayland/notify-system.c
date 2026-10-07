/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's own notifications and what a click on one does
 * (ws156-p003, plan/ws156/phase001/phase.md section 6): a notification
 * posted with an action keeps it here by its number, and a click of its
 * body runs it (a command started, as the bar's buttons start one).
 */

#include "kwl.h"
#include "notify.h"

#include <stdio.h>
#include <string.h>

/* How many of the compositor's notifications with an action are kept, and the longest command. */
#define NOTIFY_SYSTEM_ACTIONS	16U
#define NOTIFY_SYSTEM_COMMAND	256U

/* One notification's action: its number (0: a free entry) and the command its click starts. */
struct notify_system_action {
	uint32_t id;
	char command[NOTIFY_SYSTEM_COMMAND];
};

/*
 * The actions of the compositor's notifications, the oldest replaced when
 * all are taken.  Only the main loop touches them; an entry stays after
 * its notification went (its number is not given again).
 */
static struct notify_system_action notify_system_actions[NOTIFY_SYSTEM_ACTIONS];
static unsigned notify_system_next;

/*
 * Posts a notification of the compositor's own whose body's click starts
 * a command (NULL: no action).  Returns its number, or 0 when it could not
 * be kept.
 */
uint32_t
kwl_notify_system_post(
	struct kwl_server *server,
	const char *title,
	const char *body,
	unsigned flags,
	const char *command)
{
	struct notify_system_action *action;
	uint32_t id;

	/* A notification with an action, when it has one. */
	if (command != NULL)
		flags |= KWL_NOTIFY_ACTION;
	id = kwl_notify_post_system(server, title, body, flags);
	if (id == 0U || command == NULL)
		return id;

	/* Its action, in the next entry. */
	action = &notify_system_actions[notify_system_next];
	notify_system_next = (notify_system_next + 1U) % NOTIFY_SYSTEM_ACTIONS;
	action->id = id;
	(void)snprintf(action->command, sizeof(action->command), "%s", command);

	/* Succeeded: its number. */
	return id;
}

/*
 * Runs the action of a compositor's notification whose body was clicked
 * (kwl_notify_activate): its command is started.
 */
void
kwl_notify_system_activated(
	struct kwl_server *server,
	uint32_t id)
{
	unsigned index;
	pid_t child;

	/* The notification's action, if it has one. */
	for (index = 0U; index < NOTIFY_SYSTEM_ACTIONS; index++) {
		if (notify_system_actions[index].id != id || id == 0U)
			continue;

		/* Its command, started once. */
		child = kwl_spawn(server, notify_system_actions[index].command);
		printf("KWL NOTIFY system-action id=%u command=\"%s\" pid=%ld\n", id, notify_system_actions[index].command, (long)child);
		notify_system_actions[index].id = 0U;
		return;
	}
}
