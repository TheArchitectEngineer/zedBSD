/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef LIBC_SYS_SOCKET_H
#define LIBC_SYS_SOCKET_H

#include <uapi/socket.h>

/* The standard macro evaluates its message pointer once through a bounded helper. */
#define CMSG_FIRSTHDR(message) __libc_cmsg_firsthdr(message)

/* Return the first ancillary header only when the supplied control buffer contains it. */
static __inline struct cmsghdr *
__libc_cmsg_firsthdr(
	const struct msghdr *message)
{
	/* A truncated ancillary buffer contains no complete first message header. */
	if (message->msg_controllen < sizeof(struct cmsghdr))
		return (struct cmsghdr *)0;

	/* Succeeded: the caller owns the complete first control header, if any. */
	return (struct cmsghdr *)message->msg_control;
}

#endif
