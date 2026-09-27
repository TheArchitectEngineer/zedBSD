/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * HTTP/1.1 (RFC 9112), the first pass: a GET over a new TCP connection
 * per request (Connection: close), the response read to the end of the
 * connection, its body cut by Content-Length or decoded from chunks,
 * redirects followed (twenty at most), and the cookies of each response
 * kept and sent back (cookie.c).  An https URL's connection is TLS
 * (tls.c) under the same requests.
 *
 * The fetch blocks the caller while it runs: the name lookup, the
 * connection and each read (a read waits NET_HTTP_TIMEOUT at most).  The
 * asynchronous loader with its resolver thread, persistent connections
 * and the cache come later (plan/ws074/design.md §9).
 */

#include "net/net.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* How long a connection may stay silent, in milliseconds. */
#define NET_HTTP_TIMEOUT	30000

/* The most redirects a fetch follows. */
#define NET_HTTP_REDIRECTS	20

/* The largest response a fetch reads, in bytes. */
#define NET_HTTP_MAX_RESPONSE	((size_t)256U * 1024U * 1024U)

/* The User-Agent the browser sends. */
#define NET_HTTP_AGENT		"zdesktop-browser/0.1 (zedBSD)"

/*
 * The other headers of every request: any type (the star, the slash and the star
 * written apart so that they do not read as a comment), no compression, and a
 * new connection each time.
 */
#define NET_HTTP_HEADERS	"Accept: *" "/" "*\r\nAccept-Encoding: identity\r\nConnection: close\r\n"

/*
 * What the headers of a response say that the fetch uses (besides the
 * Content-Type and the cookies, kept at once).
 */
struct http_headers {
	size_t content_length;
	int has_length;
	int chunked;
};

/*
 * A request's connection: the socket, and its TLS for https (NULL for
 * http).
 */
struct http_connection {
	int descriptor;
	struct net_tls *tls;
};

static int http_request(const struct net_url *url, struct net_response *response, struct wb_buffer *location);
static int http_connect(const struct net_url *url, int *descriptor);
static int http_send_all(const struct http_connection *connection, const unsigned char *bytes, size_t length);
static int http_receive_all(const struct http_connection *connection, struct wb_buffer *raw);
static int http_receive(const struct http_connection *connection, unsigned char *bytes, size_t length, size_t *received);
static int http_is_web(const char *scheme);
static int http_parse(const struct net_url *url, const struct wb_buffer *raw, struct net_response *response, struct wb_buffer *location);
static int http_header(const struct net_url *url, const char *line, size_t length, struct net_response *response, struct wb_buffer *location, struct http_headers *headers);
static int http_header_value(const char *line, size_t length, const char *name, const char **value, size_t *value_length);
static int http_dechunk(const unsigned char *body, size_t length, struct wb_buffer *out);
static int http_is_redirect(int status);

/*
 * Fetches a URL with GET, following redirects: fills the response (its
 * final URL, status, Content-Type and body) or returns an errno value
 * (EPROTONOSUPPORT for a scheme other than http and https, or https
 * without the OpenSSL package; EPROTO for a TLS failure, whose reason
 * net_tls_error gives; EINVAL for a response
 * that is not HTTP, ELOOP for too many redirects).
 */
int
net_http_fetch(
	const char *url_text,
	struct net_response *response)
{
	struct net_url url;
	struct net_url next;
	struct wb_buffer location;
	int redirects;
	int is_web;
	int redirected;
	int error;

	/* The URL (and no TLS failure yet). */
	net_tls_clear_error();
	memset(response, 0, sizeof(*response));
	wb_buffer_init(&response->url);
	wb_buffer_init(&response->content_type);
	wb_buffer_init(&response->body);
	error = net_url_parse(url_text, strlen(url_text), NULL, &url);
	if (error != 0)
		return error;

	/* Each request, then the next one a redirect names. */
	wb_buffer_init(&location);
	for (redirects = 0;; redirects++) {
		/* Only http and https. */
		is_web = http_is_web(url.scheme);
		if (!is_web) {
			error = EPROTONOSUPPORT;
			break;
		}

		/* The request and its response. */
		wb_buffer_clear(&location);
		error = http_request(&url, response, &location);
		if (error != 0)
			break;

		/* A redirect with a place to go is followed; anything else is the response. */
		redirected = http_is_redirect(response->status);
		if (!redirected || location.length == 0)
			break;
		if (redirects == NET_HTTP_REDIRECTS) {
			error = ELOOP;
			break;
		}

		/* The next URL, resolved against this one. */
		error = net_url_parse(wb_buffer_string(&location), location.length, &url, &next);
		if (error != 0)
			break;
		net_url_release(&url);
		url = next;
	}

	/* The final URL, which relative URLs in the page resolve against. */
	if (error == 0)
		error = net_url_serialize(&url, 0, &response->url);
	net_url_release(&url);
	wb_buffer_release(&location);
	if (error != 0) {
		net_response_release(response);
		return error;
	}

	/* Succeeded: the response is there. */
	return 0;
}

/*
 * Frees a response's buffers.
 */
void
net_response_release(
	struct net_response *response)
{
	/* The three buffers. */
	wb_buffer_release(&response->url);
	wb_buffer_release(&response->content_type);
	wb_buffer_release(&response->body);
}

/* Sends one GET and reads its response; a redirect's Location goes to location. */
static int
http_request(
	const struct net_url *url,
	struct net_response *response,
	struct wb_buffer *location)
{
	struct http_connection connection;
	struct wb_buffer request;
	struct wb_buffer raw;
	const char *target;
	int is_https;
	int error;

	/* The request line: the path (at least "/") and the query. */
	wb_buffer_init(&request);
	wb_buffer_init(&raw);
	target = url->path;
	if (target[0] == '\0')
		target = "/";
	error = wb_buffer_printf(&request, "GET %s", target);
	if (error == 0 && url->query != NULL)
		error = wb_buffer_printf(&request, "?%s", url->query);
	if (error == 0)
		error = wb_buffer_printf(&request, " HTTP/1.1\r\nHost: %s", url->host);
	if (error == 0 && url->port >= 0)
		error = wb_buffer_printf(&request, ":%d", url->port);
	if (error == 0)
		error = wb_buffer_printf(&request, "\r\nUser-Agent: %s\r\n%s", NET_HTTP_AGENT, NET_HTTP_HEADERS);

	/* The cookies for the URL, and the end of the headers. */
	if (error == 0)
		error = net_cookie_header(url, &request);
	if (error == 0)
		error = wb_buffer_append_string(&request, "\r\n");
	if (error != 0) {
		wb_buffer_release(&request);
		return error;
	}

	/* The connection, and TLS over it for https. */
	connection.tls = NULL;
	connection.descriptor = -1;
	error = http_connect(url, &connection.descriptor);
	is_https = !strcmp(url->scheme, "https");
	if (error == 0 && is_https)
		error = net_tls_open(connection.descriptor, url->host, &connection.tls);

	/* The request and everything the server sends until it closes. */
	if (error == 0)
		error = http_send_all(&connection, request.data, request.length);
	if (error == 0)
		error = http_receive_all(&connection, &raw);
	net_tls_close(connection.tls);
	if (connection.descriptor >= 0)
		close(connection.descriptor);

	/* The request was sent. */
	wb_buffer_release(&request);

	/* The response. */
	if (error == 0)
		error = http_parse(url, &raw, response, location);
	wb_buffer_release(&raw);
	if (error != 0)
		return error;

	/* Succeeded: the response is read. */
	return 0;
}

/* Looks up a URL's host and connects to its port (the first address that answers). */
static int
http_connect(
	const struct net_url *url,
	int *descriptor)
{
	struct addrinfo hints;
	struct addrinfo *found;
	struct addrinfo *address;
	char host[256];
	char port[16];
	size_t length;
	int number;
	int status;
	int error;

	/* The host without an IPv6 address's brackets. */
	length = strlen(url->host);
	if (length >= sizeof(host))
		return ENAMETOOLONG;
	memcpy(host, url->host, length + 1U);
	if (length >= 2U && host[0] == '[') {
		memmove(host, host + 1, length - 2U);
		host[length - 2U] = '\0';
	}

	/* The port, or the scheme's. */
	number = net_url_default_port(url->scheme);
	if (url->port >= 0)
		number = url->port;
	snprintf(port, sizeof(port), "%d", number);

	/* The addresses of the host. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	status = getaddrinfo(host, port, &hints, &found);
	if (status != 0)
		return EHOSTUNREACH;

	/* The first that accepts a connection. */
	error = ECONNREFUSED;
	*descriptor = -1;
	for (address = found; address != NULL; address = address->ai_next) {
		*descriptor = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (*descriptor < 0) {
			error = errno;
			continue;
		}

		/* A connection that works ends the search. */
		status = connect(*descriptor, address->ai_addr, address->ai_addrlen);
		if (status == 0) {
			error = 0;
			break;
		}

		/* Otherwise the next address. */
		error = errno;
		close(*descriptor);
		*descriptor = -1;
	}

	/* The addresses are no longer needed. */
	freeaddrinfo(found);
	if (error != 0)
		return error;

	/* Succeeded: the connection is open. */
	return 0;
}

/* Sends all of a buffer, over TLS or the socket (a closed connection does not raise SIGPIPE). */
static int
http_send_all(
	const struct http_connection *connection,
	const unsigned char *bytes,
	size_t length)
{
	ssize_t sent;
	size_t done;
	int error;

	/* TLS sends it all itself. */
	if (connection->tls != NULL) {
		error = net_tls_write(connection->tls, bytes, length);
		return error;
	}

	/* Until everything is sent. */
	for (done = 0; done < length; done += (size_t)sent) {
		sent = send(connection->descriptor, bytes + done, length - done, MSG_NOSIGNAL);
		if (sent < 0 && errno == EINTR) {
			sent = 0;
			continue;
		}

		/* Any other failure ends the request. */
		if (sent < 0)
			return errno;
	}

	/* Succeeded: the request is sent. */
	return 0;
}

/* Reads until the server closes the connection (or the response grows too large, or the server falls silent). */
static int
http_receive_all(
	const struct http_connection *connection,
	struct wb_buffer *raw)
{
	struct pollfd waiting;
	unsigned char chunk[16384];
	size_t received;
	int pending;
	int ready;
	int error;

	/* Each read, after waiting for the connection to be readable (unless TLS already holds bytes). */
	for (;;) {
		pending = 0;
		if (connection->tls != NULL)
			pending = net_tls_pending(connection->tls);
		if (!pending) {
			waiting.fd = connection->descriptor;
			waiting.events = POLLIN;
			waiting.revents = 0;
			ready = poll(&waiting, 1, NET_HTTP_TIMEOUT);
			if (ready < 0 && errno == EINTR)
				continue;
			if (ready < 0)
				return errno;
			if (ready == 0)
				return ETIMEDOUT;
		}

		/* The bytes, or the end of the response. */
		error = http_receive(connection, chunk, sizeof(chunk), &received);
		if (error == EINTR)
			continue;
		if (error != 0)
			return error;
		if (received == 0)
			break;
		if (raw->length + received > NET_HTTP_MAX_RESPONSE)
			return EFBIG;
		error = wb_buffer_append(raw, chunk, received);
		if (error != 0)
			return error;
	}

	/* Succeeded: the whole response is read. */
	return 0;
}

/* Reads what is there, over TLS or the socket; *received is 0 at the end of the connection. */
static int
http_receive(
	const struct http_connection *connection,
	unsigned char *bytes,
	size_t length,
	size_t *received)
{
	ssize_t count;
	int error;

	/* TLS decrypts. */
	if (connection->tls != NULL) {
		error = net_tls_read(connection->tls, bytes, length, received);
		return error;
	}

	/* The socket's bytes. */
	*received = 0;
	count = recv(connection->descriptor, bytes, length, 0);
	if (count < 0)
		return errno;
	*received = (size_t)count;
	return 0;
}

/* Tells whether a scheme is one the fetch speaks: http or https. */
static int
http_is_web(
	const char *scheme)
{
	int differs;

	/* The two schemes. */
	differs = strcmp(scheme, "http");
	if (differs == 0)
		return 1;
	differs = strcmp(scheme, "https");
	return differs == 0;
}

/* Parses a response: the status line, the headers (cookies kept, a redirect's Location), and the body. */
static int
http_parse(
	const struct net_url *url,
	const struct wb_buffer *raw,
	struct net_response *response,
	struct wb_buffer *location)
{
	struct http_headers headers;
	const char *text;
	const char *line;
	const char *end;
	const char *newline;
	const unsigned char *body;
	size_t line_length;
	size_t body_length;
	int differs;
	int error;

	/* The status line: HTTP/1.x, a space, the three digits. */
	text = (const char *)raw->data;
	if (raw->length < 12U)
		return EINVAL;
	differs = strncmp(text, "HTTP/1.", 7);
	if (differs != 0 || text[8] != ' ')
		return EINVAL;
	response->status = (text[9] - '0') * 100 + (text[10] - '0') * 10 + (text[11] - '0');

	/* The headers, a line each, up to the empty line. */
	end = text + raw->length;
	newline = memchr(text, '\n', raw->length);
	if (newline == NULL)
		return EINVAL;
	line = newline + 1;
	memset(&headers, 0, sizeof(headers));
	error = 0;
	while (error == 0 && line < end) {
		/* The line without its CR LF; the empty one ends the headers. */
		newline = memchr(line, '\n', (size_t)(end - line));
		if (newline == NULL)
			newline = end;
		line_length = (size_t)(newline - line);
		if (line_length > 0 && line[line_length - 1U] == '\r')
			line_length--;
		if (line_length == 0) {
			line = newline + 1;
			break;
		}

		/* The header, then the next line. */
		error = http_header(url, line, line_length, response, location, &headers);
		line = newline + 1;
	}

	/* A buffer that could not grow, or a cookie that could not be kept. */
	if (error != 0)
		return error;

	/* The body: chunks, a length, or the rest of the connection. */
	body = (const unsigned char *)line;
	body_length = 0;
	if (line < end)
		body_length = (size_t)(end - line);
	if (headers.chunked) {
		error = http_dechunk(body, body_length, &response->body);
	} else {
		if (headers.has_length && headers.content_length < body_length)
			body_length = headers.content_length;
		error = wb_buffer_append(&response->body, body, body_length);
	}

	/* A buffer that could not grow. */
	if (error != 0)
		return error;

	/* Succeeded: the response is parsed. */
	return 0;
}

/* Reads one header line: Content-Type, Content-Length, Transfer-Encoding, Location and Set-Cookie. */
static int
http_header(
	const struct net_url *url,
	const char *line,
	size_t length,
	struct net_response *response,
	struct wb_buffer *location,
	struct http_headers *headers)
{
	const char *value;
	size_t value_length;
	int found;
	int differs;
	int error;

	/* The type of the body. */
	error = 0;
	found = http_header_value(line, length, "content-type", &value, &value_length);
	if (found) {
		wb_buffer_clear(&response->content_type);
		error = wb_buffer_append(&response->content_type, value, value_length);
	}

	/* The length of the body. */
	found = http_header_value(line, length, "content-length", &value, &value_length);
	if (found) {
		headers->has_length = 1;
		headers->content_length = (size_t)strtoul(value, NULL, 10);
	}

	/* Chunks (the last coding named is chunked). */
	found = http_header_value(line, length, "transfer-encoding", &value, &value_length);
	if (found && value_length >= 7U) {
		differs = strncmp(value + value_length - 7U, "chunked", 7);
		if (differs == 0)
			headers->chunked = 1;
	}

	/* Where a redirect goes. */
	found = http_header_value(line, length, "location", &value, &value_length);
	if (found && error == 0) {
		wb_buffer_clear(location);
		error = wb_buffer_append(location, value, value_length);
	}

	/* A cookie to keep. */
	found = http_header_value(line, length, "set-cookie", &value, &value_length);
	if (found && error == 0)
		error = net_cookie_store(url, value, value_length);
	if (error != 0)
		return error;

	/* Succeeded: the header is read. */
	return 0;
}

/* Finds a header's value on a line when the line is that header (the name in any case); nonzero when found. */
static int
http_header_value(
	const char *line,
	size_t length,
	const char *name,
	const char **value,
	size_t *value_length)
{
	size_t name_length;
	size_t index;
	size_t start;
	size_t end;
	int c;

	/* The name and a colon. */
	name_length = strlen(name);
	if (length <= name_length || line[name_length] != ':')
		return 0;
	for (index = 0; index < name_length; index++) {
		c = (unsigned char)line[index];
		if (c >= 'A' && c <= 'Z')
			c += 0x20;
		if (c != (unsigned char)name[index])
			return 0;
	}

	/* The value without the whitespace around it. */
	start = name_length + 1U;
	end = length;
	while (start < end && (line[start] == ' ' || line[start] == '\t'))
		start++;
	while (end > start && (line[end - 1U] == ' ' || line[end - 1U] == '\t'))
		end--;

	/* Succeeded: the value is found. */
	*value = line + start;
	*value_length = end - start;
	return 1;
}

/* Decodes a chunked body: each hexadecimal size line and that many bytes, up to the zero chunk. */
static int
http_dechunk(
	const unsigned char *body,
	size_t length,
	struct wb_buffer *out)
{
	size_t position;
	size_t size;
	int digit;
	int c;
	int error;

	/* Each chunk. */
	position = 0;
	while (position < length) {
		/* The size in hexadecimal (an extension after ; is ignored), then the line's end. */
		size = 0;
		while (position < length) {
			c = body[position];
			digit = -1;
			if (c >= '0' && c <= '9')
				digit = c - '0';
			if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
				digit = (c | 0x20) - 'a' + 10;
			if (digit < 0)
				break;
			size = size * 16U + (size_t)digit;
			position++;
		}
		while (position < length && body[position] != '\n')
			position++;
		position++;

		/* The last chunk ends the body (trailers are ignored). */
		if (size == 0)
			break;

		/* The chunk's bytes (a short chunk at the end gives what there is). */
		if (position > length)
			break;
		if (size > length - position)
			size = length - position;
		error = wb_buffer_append(out, body + position, size);
		if (error != 0)
			return error;
		position += size;

		/* The CR LF after it. */
		while (position < length && body[position] != '\n')
			position++;
		position++;
	}

	/* Succeeded: the body is decoded. */
	return 0;
}

/* Tells whether a status is a redirect the fetch follows. */
static int
http_is_redirect(
	int status)
{
	/* 301, 302, 303, 307 and 308. */
	if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
		return 1;
	return 0;
}
