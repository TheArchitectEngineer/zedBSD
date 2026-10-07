/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The connections to the printers (plan/ws145/design.md §5.3): a host's
 * addresses tried in their order, each connection given 10 seconds, then
 * blocking sends and receives that may stand still for a minute; a
 * document is sent from its spool file a piece at a time, a job asked to
 * stop stopping between two pieces.
 */

#include "printd.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* The piece of a document sent at once. */
#define NET_CHUNK		(64U * 1024U)

static int net_connect_one(const struct addrinfo *address, int *fd);

/*
 * Connects to a printer.  Returns 0 with the socket, or an errno value
 * with the word of the failure (unreachable, refused, timeout).
 */
int
pd_connect(
	const char *host,
	unsigned port,
	int *fd,
	const char **detail)
{
	struct addrinfo hints;
	struct addrinfo *found;
	struct addrinfo *address;
	struct timeval wait;
	char service[16];
	int status;
	int error;

	/* The host's addresses for a stream. */
	*fd = -1;
	*detail = "unreachable";
	(void)snprintf(service, sizeof(service), "%u", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	status = getaddrinfo(host, service, &hints, &found);
	if (status != 0)
		return EHOSTUNREACH;

	/* The first that takes the connection. */
	error = EHOSTUNREACH;
	for (address = found; address != NULL; address = address->ai_next) {
		error = net_connect_one(address, fd);
		if (error == 0)
			break;
	}

	/* The addresses are not needed after; a failure's word. */
	freeaddrinfo(found);
	if (error != 0) {
		if (error == ECONNREFUSED)
			*detail = "refused";
		else if (error == ETIMEDOUT)
			*detail = "timeout";
		return error;
	}

	/* Blocking from here, a minute standing still at most. */
	wait.tv_sec = PD_IDLE_SECONDS;
	wait.tv_usec = 0;
	(void)setsockopt(*fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
	(void)setsockopt(*fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
	*detail = "";
	return 0;
}

/*
 * Writes all the bytes of a buffer.  Returns 0, or an errno value.
 */
int
pd_write_all(
	int fd,
	const void *data,
	size_t size)
{
	const unsigned char *cursor;
	ssize_t sent;

	/* Until every byte is out. */
	cursor = data;
	while (size > 0U) {
		sent = send(fd, cursor, size, MSG_NOSIGNAL);
		if (sent < 0 && errno == EINTR)
			continue;
		if (sent < 0)
			return errno;
		if (sent == 0)
			return EIO;
		cursor += sent;
		size -= (size_t)sent;
	}

	/* Every byte is out. */
	return 0;
}

/*
 * Sends a job's spool file.  Returns 0, ECANCELED when the job was asked
 * to stop meanwhile, or an errno value.
 */
int
pd_send_file(
	int fd,
	struct pd_job *job)
{
	unsigned char *chunk;
	uint64_t done;
	ssize_t got;
	int file;
	int error;
	int stop;

	/* The file and a piece's room. */
	file = open(job->file, O_RDONLY | O_CLOEXEC);
	if (file < 0)
		return errno;
	chunk = malloc(NET_CHUNK);
	if (chunk == NULL) {
		(void)close(file);
		return ENOMEM;
	}

	/* Piece by piece, as many bytes as were copied. */
	done = 0;
	error = 0;
	while (done < job->size) {
		/* A job asked to stop stops between two pieces. */
		stop = pd_cancelled(job);
		if (stop) {
			error = ECANCELED;
			break;
		}

		/* The next piece. */
		got = read(file, chunk, NET_CHUNK);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			error = EIO;
			break;
		}

		/* Sent. */
		error = pd_write_all(fd, chunk, (size_t)got);
		if (error != 0)
			break;
		done += (uint64_t)got;
	}

	/* The file and the room go. */
	free(chunk);
	(void)close(file);
	return error;
}

/*
 * Reads what is there (waiting up to the socket's time out).  Returns the
 * count, 0 at the end, or -1.
 */
ssize_t
pd_read_some(
	int fd,
	void *data,
	size_t size)
{
	ssize_t got;

	/* One read, again when interrupted. */
	for (;;) {
		got = recv(fd, data, size, 0);
		if (got < 0 && errno == EINTR)
			continue;
		return got;
	}
}

/* Connects to one address within the connection's time; 0 or an errno value. */
static int
net_connect_one(
	const struct addrinfo *address,
	int *fd)
{
	struct pollfd poller;
	socklen_t length;
	int flags;
	int status;
	int error;
	int asked;
	int made;

	/* A socket that does not block while it connects. */
	made = socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol);
	if (made < 0)
		return errno;
	flags = fcntl(made, F_GETFL);
	(void)fcntl(made, F_SETFL, flags | O_NONBLOCK);

	/* The connection, waited for. */
	status = connect(made, address->ai_addr, address->ai_addrlen);
	error = 0;
	if (status != 0 && errno != EINPROGRESS)
		error = errno;
	if (status != 0 && error == 0) {
		poller.fd = made;
		poller.events = POLLOUT;
		poller.revents = 0;
		status = poll(&poller, 1, PD_CONNECT_SECONDS * 1000);
		if (status == 0)
			error = ETIMEDOUT;
		else if (status < 0)
			error = errno;
		else {
			length = sizeof(error);
			asked = getsockopt(made, SOL_SOCKET, SO_ERROR, &error, &length);
			if (asked != 0)
				error = errno;
		}
	}

	/* Not connected. */
	if (error != 0) {
		(void)close(made);
		return error;
	}

	/* Connected: blocking again. */
	(void)fcntl(made, F_SETFL, flags);
	*fd = made;
	return 0;
}
