/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail's connection to a server (WS169 p003; mail.h): a TCP socket to a
 * host and port, TLS from the start or after STARTTLS (tls.c), and reads
 * of a whole line or of a counted run of bytes through a buffer, each
 * waiting at most the connection's timeout for the server.
 */

#include "mail.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* How long a read waits for the server by default (ms). */
#define CONN_TIMEOUT_MS		30000

/* The highest port number. */
#define CONN_PORT_MAX		65535UL

static int conn_fill(struct ml_conn *conn);

/*
 * Reads a server as "host" or "host:port": the port (the fallback when
 * none is given) and whether TLS starts at once (the ports 993 and 465;
 * every other port is upgraded with STARTTLS).  Returns 0 or EINVAL.
 */
int
ml_server_parse(
	const char *text,
	unsigned fallback_port,
	struct ml_server *server)
{
	const char *colon;
	unsigned long port;
	size_t length;
	char *end;

	/* Nothing yet. */
	memset(server, 0, sizeof(server[0]));

	/* The host, up to a colon. */
	colon = strchr(text, ':');
	length = strlen(text);
	if (colon != NULL)
		length = (size_t)(colon - text);
	if (length == 0U || length >= sizeof(server->host))
		return EINVAL;
	memcpy(server->host, text, length);
	server->host[length] = '\0';

	/* The port given after it, or the fallback. */
	port = fallback_port;
	if (colon != NULL) {
		errno = 0;
		port = strtoul(colon + 1, &end, 10);
		if (errno != 0 || *end != '\0' || end == colon + 1)
			return EINVAL;
	}

	/* A port in range. */
	if (port == 0UL || port > CONN_PORT_MAX)
		return EINVAL;
	server->port = (unsigned)port;

	/* TLS from the start on the TLS ports. */
	if (port == 993UL || port == 465UL)
		server->secure = 1;

	/* Succeeded: the server is read. */
	return 0;
}

/*
 * Connects to a server, with TLS from the start when the server is secure.
 * Returns 0, an errno value of the network, ENOENT for a host not found,
 * or a TLS failure (ml_tls_error says why).
 */
int
ml_conn_open(
	struct ml_conn *conn,
	const struct ml_server *server)
{
	struct addrinfo hints;
	struct addrinfo *found;
	struct addrinfo *address;
	char port[16];
	int status;
	int error;
	int fd;

	/* Nothing yet. */
	memset(conn, 0, sizeof(conn[0]));
	conn->fd = -1;
	conn->timeout_ms = CONN_TIMEOUT_MS;

	/* The host's addresses for a stream. */
	(void)snprintf(port, sizeof(port), "%u", server->port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	status = getaddrinfo(server->host, port, &hints, &found);
	if (status != 0)
		return ENOENT;

	/* The first address that takes the connection. */
	error = ECONNREFUSED;
	for (address = found; address != NULL; address = address->ai_next) {
		/* A socket of its family. */
		fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (fd < 0) {
			error = errno;
			continue;
		}

		/* Connected, or the next address. */
		status = connect(fd, address->ai_addr, address->ai_addrlen);
		if (status != 0) {
			error = errno;
			(void)close(fd);
			continue;
		}

		/* This one. */
		conn->fd = fd;
		break;
	}

	/* The addresses are not needed after. */
	freeaddrinfo(found);

	/* No address took it. */
	if (conn->fd < 0)
		return error;

	/* A secure server: TLS at once. */
	if (server->secure) {
		error = ml_conn_start_tls(conn, server->host);
		if (error != 0) {
			ml_conn_close(conn);
			return error;
		}
	}

	/* Succeeded: the connection is open. */
	return 0;
}

/*
 * Starts TLS on an open plain connection (after STARTTLS), checking the
 * certificate against a host.  Returns 0 or a TLS failure.
 */
int
ml_conn_start_tls(
	struct ml_conn *conn,
	const char *host)
{
	int error;

	/* Bytes the server sent before the handshake would be read as plain text after it. */
	if (conn->start != conn->end)
		return EPROTO;

	/* The handshake. */
	error = ml_tls_open(conn->fd, host, &conn->tls);
	if (error != 0)
		return error;

	/* Succeeded: the connection is TLS from here. */
	return 0;
}

/*
 * Sends bytes.  Returns 0 or an errno value.
 */
int
ml_conn_write(
	struct ml_conn *conn,
	const char *bytes,
	size_t length)
{
	ssize_t written;
	size_t done;
	int error;

	/* Through TLS. */
	if (conn->tls != NULL) {
		error = ml_tls_write(conn->tls, (const unsigned char *)bytes, length);
		if (error != 0)
			return error;
		return 0;
	}

	/* Plain, until everything is sent. */
	done = 0;
	while (done < length) {
		written = write(conn->fd, bytes + done, length - done);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return EIO;
		done += (size_t)written;
	}

	/* Succeeded: everything is sent. */
	return 0;
}

/*
 * Reads a line without its line end (CR LF or LF) into a room; a longer
 * line is cut to the room and the rest of it is dropped.  Returns 0,
 * EPIPE when the server closed, ETIMEDOUT, or an errno value.
 */
int
ml_conn_line(
	struct ml_conn *conn,
	char *line,
	size_t size)
{
	size_t length;
	unsigned char byte;
	int error;

	/* Byte by byte from the buffer, refilled as it empties. */
	length = 0;
	for (;;) {
		/* An empty buffer is filled. */
		if (conn->start == conn->end) {
			error = conn_fill(conn);
			if (error != 0)
				return error;
		}

		/* The next byte; a line feed ends the line. */
		byte = conn->buffer[conn->start];
		conn->start++;
		if (byte == '\n')
			break;

		/* Kept while there is room (a CR is dropped at the end below). */
		if (length + 1U < size) {
			line[length] = (char)byte;
			length++;
		}
	}

	/* The line without its CR. */
	if (length > 0U && line[length - 1U] == '\r')
		length--;
	line[length] = '\0';

	/* Succeeded: one line read. */
	return 0;
}

/*
 * Reads exactly count bytes (an IMAP literal).  Returns 0, EPIPE when the
 * server closed first, ETIMEDOUT, or an errno value.
 */
int
ml_conn_bytes(
	struct ml_conn *conn,
	char *bytes,
	size_t count)
{
	size_t taken;
	size_t part;
	int error;

	/* From the buffer, refilled as it empties. */
	taken = 0;
	while (taken < count) {
		/* An empty buffer is filled. */
		if (conn->start == conn->end) {
			error = conn_fill(conn);
			if (error != 0)
				return error;
		}

		/* As much of the buffer as is wanted. */
		part = conn->end - conn->start;
		if (part > count - taken)
			part = count - taken;
		memcpy(bytes + taken, conn->buffer + conn->start, part);
		conn->start += part;
		taken += part;
	}

	/* Succeeded: the bytes are read. */
	return 0;
}

/*
 * Tells whether bytes wait without reading the socket: in the buffer, or
 * decrypted by TLS (then a poll of the socket would not see them).
 */
int
ml_conn_ready(
	const struct ml_conn *conn)
{
	int pending;

	/* In the buffer. */
	if (conn->start != conn->end)
		return 1;

	/* Held by TLS. */
	if (conn->tls != NULL) {
		pending = ml_tls_pending(conn->tls);
		if (pending)
			return 1;
	}

	/* Nothing waits. */
	return 0;
}

/*
 * Closes a connection (TLS first, then the socket).
 */
void
ml_conn_close(
	struct ml_conn *conn)
{
	/* TLS's close. */
	if (conn->tls != NULL)
		ml_tls_close(conn->tls);
	conn->tls = NULL;

	/* The socket. */
	if (conn->fd >= 0)
		(void)close(conn->fd);
	conn->fd = -1;
	conn->start = 0;
	conn->end = 0;
}

/* Fills the empty buffer with what the server sends, waiting at most the timeout. */
static int
conn_fill(
	struct ml_conn *conn)
{
	struct pollfd watched;
	ssize_t received;
	size_t decrypted;
	int ready;
	int pending;
	int error;

	/* Bytes TLS already holds need no wait; otherwise the socket must become readable in time. */
	pending = 0;
	if (conn->tls != NULL)
		pending = ml_tls_pending(conn->tls);
	if (!pending) {
		watched.fd = conn->fd;
		watched.events = POLLIN;
		watched.revents = 0;
		ready = poll(&watched, 1, conn->timeout_ms);
		if (ready < 0)
			return errno;
		if (ready == 0)
			return ETIMEDOUT;
	}

	/* Through TLS. */
	conn->start = 0;
	conn->end = 0;
	if (conn->tls != NULL) {
		error = ml_tls_read(conn->tls, conn->buffer, sizeof(conn->buffer), &decrypted);
		if (error != 0)
			return error;
		if (decrypted == 0U)
			return EPIPE;
		conn->end = decrypted;
		return 0;
	}

	/* Plain. */
	received = read(conn->fd, conn->buffer, sizeof(conn->buffer));
	if (received < 0)
		return errno;
	if (received == 0)
		return EPIPE;

	/* Succeeded: the buffer has bytes. */
	conn->end = (size_t)received;
	return 0;
}
