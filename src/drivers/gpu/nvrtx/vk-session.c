/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's sessions (ws037-p007 stage D2): one for each open of the
 * node, with its objects and the scratch its commands decode into.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/*
 * Opens an executor session for an owner (the node's session).
 */
int
nvrtx_vk_session_open(
	void *owner,
	struct nvrtx_vk_session **session)
{
	struct nvrtx_vk_session *created;

	*session = NULL;

	/* Allocates the session. */
	created = kern_calloc(1U, sizeof(*created));
	if (created == NULL)
		return ENOMEM;

	/* Allocates its scratch. */
	created->arena.base = kern_calloc(1U, NVRTX_VK_ARENA_BYTES);
	if (created->arena.base == NULL) {
		kern_free(created);
		return ENOMEM;
	}

	/* No object yet; the scratch's size and the owner. */
	created->arena.size = NVRTX_VK_ARENA_BYTES;
	created->owner = owner;

	/* Succeeded: the session is the caller's. */
	*session = created;
	return 0;
}

/*
 * Closes an executor session: removes its objects and frees it.
 *
 * The caller has ended every command of the session first.  When payloads
 * are still held elsewhere the session stays (EBUSY); a destructor's error
 * is reported after the session is freed.
 */
int
nvrtx_vk_session_close(
	struct nvrtx_vk_session **session)
{
	struct nvrtx_vk_session *closing;
	int error;

	/* A session never opened has nothing to close. */
	closing = *session;
	if (closing == NULL)
		return 0;

	/* Removes every object. */
	error = nvrtx_vk_objects_close(closing);
	if (closing->live_objects != 0U) {
		/* Payloads still held keep the session for their destructors. */
		if (error != 0)
			return error;
		return EBUSY;
	}

	/* Frees the scratch and the session. */
	kern_free(closing->arena.base);
	kern_free(closing);
	*session = NULL;

	/* Reports a destructor that failed. */
	if (error != 0)
		return error;

	/* Succeeded: the session is gone. */
	return 0;
}
