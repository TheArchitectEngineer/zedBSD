/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The peer of a connection on Linux: SO_PEERCRED (keiland-backend.h, WS135).
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Gives the user at the other end of a connected local socket.
 */
int
kl_backend_peer_uid(
	int descriptor,
	uid_t *uid)
{
	struct ucred credentials;
	socklen_t length;
	int status;

	/* The credentials the kernel recorded at the connection. */
	length = sizeof(credentials);
	status = getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &length);
	if (status != 0)
		return errno;

	/* Succeeded: the peer's user. */
	*uid = credentials.uid;
	return 0;
}
