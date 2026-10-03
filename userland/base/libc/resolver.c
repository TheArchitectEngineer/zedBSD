/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements the zedBSD C library resolver support.
 */

#include "userland/base/libc/resolver-internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static uint32_t resolver_counter;
static pthread_mutex_t resolver_counter_lock = PTHREAD_MUTEX_INITIALIZER;

static int resolver_query_server_depth(const char *name, uint16_t type, const struct in_addr *server_address, uint16_t port, struct resolver_result *result, unsigned depth);
static uint16_t query_id(const char *name);
static int tcp_query(const struct sockaddr_in *server, const uint8_t *query, size_t query_length, uint16_t id, const char *name, uint16_t type, struct resolver_result *result);
static int write_all_socket(int descriptor, const uint8_t *buffer, size_t length);
static int read_exact_socket(int descriptor, uint8_t *buffer, size_t length);
static int parse_service(const char *service, const char *protocol, uint16_t *port);
static int make_ptr_name(struct in_addr address, char *output, size_t capacity);

/*
 * Implements the resolver load config operation.
 */
int
resolver_load_config(
	struct resolver_config *config)
{
	char *text, *end;
	FILE *file;
	char line[128];

	/* Handles the config availability. */
	if (config == NULL)
		return EAI_FAIL;
	memset(config, 0, sizeof(*config));
	file = fopen("/etc/resolv.conf", "r");

	/* Handles the file availability. */
	if (file == NULL)
		return EAI_AGAIN;

	/* Process input until it is exhausted. */
	while (config->count < DNS_MAX_NAMESERVERS &&
	       fgets(line, sizeof(line), file) != NULL) {
		/* Continue while the operation condition remains true. */
		text = line;
		while (*text == ' ' || *text == '\t')
			text++;

		/* Validates the current text. */
		if (*text == '#' || *text == '\n' || *text == '\0')
			continue;

		/* Selects the matching prefix. */
		if (strncmp(text, "nameserver", 10U) != 0 ||
		    (text[10] != ' ' && text[10] != '\t'))
			continue;
		text += 10;

		/* Continue while the operation condition remains true. */
		while (*text == ' ' || *text == '\t')
			text++;

		/* Continue while the operation condition remains true. */
		end = text;
		while (*end != '\0' && *end != '\n' && *end != '\r' &&
		       *end != ' ' && *end != '\t' && *end != '#')
			end++;
		*end = '\0';
		/* Handles a failed inet aton operation. */
		if (inet_aton(text, &config->servers[config->count]))
			config->count++;
	}
	fclose(file);

	/* Returns the computed result. */
	return config->count != 0U ? 0 : EAI_AGAIN;
}

/*
 * Implements the resolver query server operation.
 */
int
resolver_query_server(
	const char *name,
	uint16_t type,
	const struct in_addr *server_address,
	uint16_t port,
	struct resolver_result *result)
{
	int function_result;

	/* Obtains the resolver query server depth result. */
	function_result = resolver_query_server_depth(name, type, server_address, port,
					   result, 0);

	/* Returns the computed result. */
	return function_result;
}

/*
 * Implements the resolver query operation.
 */
int
resolver_query(
	const char *name,
	uint16_t type,
	struct resolver_result *result)
{
	struct resolver_config config;
	unsigned index;
	int error;

	error = resolver_load_config(&config);

	/* Handles an operation failure. */
	if (error != 0)
		return error;

	/* Process each remaining element. */
	for (index = 0; index < config.count; index++) {
		error = resolver_query_server(
		    name, type, &config.servers[index], 53U, result);

		/* Handles an operation failure. */
		if (error == 0 || error == EAI_NONAME)
			return error;
	}

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the getaddrinfo operation.
 */
int
getaddrinfo(
	const char *node,
	const char *service,
	const struct addrinfo *hints,
	struct addrinfo **output)
{
	struct addrinfo *item;
	struct sockaddr_in *address;
	struct resolver_result result;
	struct addrinfo *head, **tail;
	struct in_addr numeric;
	uint16_t port;
	unsigned count, index;
	int family, socktype, protocol, flags, error;

	head = NULL;
	tail = &head;
	family = AF_UNSPEC;
	socktype = 0;
	protocol = 0;
	flags = 0;

	/* Handles the output availability. */
	if (output == NULL)
		return EAI_FAIL;
	*output = NULL;
	/* Handles the hints availability. */
	if (hints != NULL) {
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		flags = hints->ai_flags;

		/* Checks the active flags. */
		if ((flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
			       AI_NUMERICSERV)) != 0)

			/* Returns the computed result. */
			return EAI_BADFLAGS;
	}

	/* Handles the family condition. */
	if (family != AF_UNSPEC && family != AF_INET)
		return EAI_FAMILY;

	/* Handles the socktype condition. */
	if (socktype != 0 && socktype != SOCK_DGRAM &&
	    socktype != SOCK_STREAM && socktype != SOCK_RAW)

		/* Returns the computed result. */
		return EAI_SOCKTYPE;
	error = parse_service(service, hints != NULL &&
	    hints->ai_socktype == SOCK_DGRAM ? "udp" : "tcp", &port);

	/* Handles an operation failure. */
	if (error != 0)
		return error;
	memset(&result, 0, sizeof(result));

	/* Handles the node availability. */
	if (node == NULL) {
		numeric.s_addr =
		    htonl((flags & AI_PASSIVE) ? INADDR_ANY : 0x7f000001U);
		result.addresses[0] = numeric;
		result.address_count = 1;
	} else if (inet_aton(node, &numeric)) {
		result.addresses[0] = numeric;
		result.address_count = 1;
		strncpy(result.canonical, node, sizeof(result.canonical) - 1U);
	} else {
		/* Checks the active flags. */
		if ((flags & AI_NUMERICHOST) != 0)
			return EAI_NONAME;
		error = resolver_query(node, DNS_TYPE_A, &result);

		/* Handles an operation failure. */
		if (error != 0)
			return error;

		/* Checks the operation result. */
		if (result.canonical[0] == '\0') {
			strncpy(result.canonical, node,
				sizeof(result.canonical) - 1U);
		}
	}

	/* Process each remaining element. */
	count = result.address_count;
	for (index = 0; index < count; index++) {
		item = calloc(1, sizeof(*item));
		address = calloc(1, sizeof(*address));

		/* Handles the item availability. */
		if (item == NULL || address == NULL) {
			free(item);
			free(address);
			freeaddrinfo(head);

			/* Returns the computed result. */
			return EAI_MEMORY;
		}
		address->sin_family = AF_INET;
		address->sin_port = htons(port);
		address->sin_addr = result.addresses[index];
		item->ai_flags = flags;
		item->ai_family = AF_INET;
		item->ai_socktype = socktype;
		item->ai_protocol = protocol;
		item->ai_addrlen = sizeof(*address);
		item->ai_addr = (struct sockaddr *)address;

		/* Checks the active flags. */
		if ((flags & AI_CANONNAME) != 0 && index == 0)
			item->ai_canonname = strdup(result.canonical);
		*tail = item;
		tail = &item->ai_next;
	}
	*output = head;
	/* Reports successful completion. */
	return 0;
}

/*
 * Implements the freeaddrinfo operation.
 */
void
freeaddrinfo(
	struct addrinfo *info)
{
	struct addrinfo *next;

	/* Continue while the operation condition remains true. */
	while (info != NULL) {
		next = info->ai_next;
		free(info->ai_addr);
		free(info->ai_canonname);
		free(info);
		info = next;
	}
}

/*
 * Implements the gai strerror operation.
 */
const char *
gai_strerror(
	int error)
{
	/* Dispatch the selected operation case. */
	switch (error) {
	case 0:
		/* Returns the computed result. */
		return "success";
	case EAI_AGAIN:
		/* Returns the computed result. */
		return "temporary failure in name resolution";
	case EAI_BADFLAGS:
		/* Returns the computed result. */
		return "invalid resolver flags";
	case EAI_FAIL:
		/* Returns the computed result. */
		return "name server failure";
	case EAI_FAMILY:
		/* Returns the computed result. */
		return "unsupported address family";
	case EAI_MEMORY:
		/* Returns the computed result. */
		return "out of memory";
	case EAI_NONAME:
		/* Returns the computed result. */
		return "name or service not known";
	case EAI_SERVICE:
		/* Returns the computed result. */
		return "unsupported service";
	case EAI_SOCKTYPE:
		/* Returns the computed result. */
		return "unsupported socket type";
	case EAI_OVERFLOW:
		/* Returns the computed result. */
		return "result buffer too small";
	case EAI_SYSTEM:
		/* Returns the computed result. */
		return "system error";
	default:
		/* Returns the computed result. */
		return "resolver error";
	}
}

/*
 * Implements the getnameinfo operation.
 */
int
getnameinfo(
	const struct sockaddr *address,
	socklen_t length,
	char *host,
	socklen_t host_length,
	char *service,
	socklen_t service_length,
	int flags)
{
	int needed;
	const struct sockaddr_in *inet;
	char buffer[254];
	struct resolver_result result;
	int error;

	inet = (const struct sockaddr_in *)address;

	/* Checks the active flags. */
	if ((flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NAMEREQD)) != 0)
		return EAI_BADFLAGS;

	/* Handles the address availability. */
	if (address == NULL || length < sizeof(*inet) ||
	    inet->sin_family != AF_INET)

		/* Returns the computed result. */
		return EAI_FAMILY;

	/* Handles the service availability. */
	if (service != NULL && service_length != 0U) {
		needed = snprintf(service, service_length, "%u",
		      ntohs(inet->sin_port));

		/* Handles the needed condition. */
		if (needed < 0 || (socklen_t)needed >= service_length)
			return EAI_OVERFLOW;
	}

	/* Handles the host availability. */
	if (host == NULL || host_length == 0U)
		return 0;

	/* Checks the active flags. */
	if ((flags & NI_NUMERICHOST) == 0) {
		error = make_ptr_name(inet->sin_addr, buffer, sizeof(buffer));

		/* Handles an operation failure. */
		if (error == 0)
			error = resolver_query(buffer, DNS_TYPE_PTR, &result);

		/* Handles an operation failure. */
		if (error == 0) {
			/* Handles a failed strlen operation. */
			if (strlen(result.ptr_name) + 1U > host_length)
				return EAI_OVERFLOW;
			strcpy(host, result.ptr_name);

			/* Reports successful completion. */
			return 0;
		}

		/* Checks the active flags. */
		if ((flags & NI_NAMEREQD) != 0)
			return error;
	}

	/* Handles a failed inet ntop operation. */
	if (inet_ntop(AF_INET, &inet->sin_addr, buffer, sizeof(buffer)) == NULL)
		return EAI_SYSTEM;

	/* Handles a failed strlen operation. */
	if (strlen(buffer) + 1U > host_length)
		return EAI_OVERFLOW;
	strcpy(host, buffer);

	/* Reports successful completion. */
	return 0;
}

/* Supports the resolver query server depth operation. */
static int
resolver_query_server_depth(
	const char *name,
	uint16_t type,
	const struct in_addr *server_address,
	uint16_t port,
	struct resolver_result *result,
	unsigned depth)
{
	struct resolver_result target;
	char alias[254];
	uint32_t cname_ttl;
	uint8_t query[512], response[512];
	struct sockaddr_in server, source;
	struct timeval timeout;
	socklen_t source_length;
	size_t query_length;
	uint16_t id;
	int attempt, descriptor, error, truncated;
	ssize_t count;

	/* Handles the name availability. */
	if (name == NULL || server_address == NULL || result == NULL)
		return EAI_FAIL;
	memset(result, 0, sizeof(*result));
	id = query_id(name);
	error = resolver_dns_build_query(query, sizeof(query), id, name, type,
					 &query_length);

	/* Handles an operation failure. */
	if (error != 0)
		return error;
	memset(&server, 0, sizeof(server));

	/* Process each element required by the operation. */
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr = *server_address;
	for (attempt = 0; attempt < 2; attempt++) {
		descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		/* Checks the file descriptor. */
		if (descriptor < 0)
			return EAI_SYSTEM;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		(void)setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout,
				 sizeof(timeout));

		/* Handles a failed sendto operation. */
		if (sendto(descriptor, query, query_length, 0,
			   (const struct sockaddr *)&server,
			   sizeof(server)) >= 0) {
			source_length = sizeof(source);
			count = recvfrom(
			    descriptor, response, sizeof(response), 0,
			    (struct sockaddr *)&source, &source_length);

			/* Checks the remaining item count. */
			if (count >= 0 && source.sin_family == AF_INET &&
			    source.sin_addr.s_addr == server.sin_addr.s_addr &&
			    source.sin_port == server.sin_port) {
				error = resolver_dns_parse(
				    response, (size_t)count, id, name, type,
				    result, &truncated);
				close(descriptor);

				/* Handles the truncated condition. */
				if (truncated) {
					error = tcp_query(&server, query,
							  query_length, id,
							  name, type, result);
				}

				/* Handles an operation failure. */
				if (error == EAI_NONAME && type == DNS_TYPE_A &&
				    result->canonical[0] != '\0' &&
				    depth < 8U) {
					cname_ttl = result->ttl;
					strncpy(alias, result->canonical,
						sizeof(alias) - 1U);
					alias[sizeof(alias) - 1U] = '\0';
					error = resolver_query_server_depth(
					    alias, type, server_address, port,
					    &target, depth + 1U);

					/* Handles an operation failure. */
					if (error == 0) {
						*result = target;
						/* Checks the operation result. */
						if (result->cname_count < 8U) {
							memmove(
							    result->cname_chain +
								1,
							    result->cname_chain,
							    result->cname_count *
								sizeof(
								    result->cname_chain
									[0]));
							strncpy(
							    result->cname_chain
								[0],
							    alias, 253U);
							result->cname_count++;
						}

						/* Handles the cname ttl condition. */
						if (cname_ttl != 0 &&
						    (result->ttl == 0 ||
						     cname_ttl < result->ttl))
							result->ttl = cname_ttl;
					}
				}

				/* Handles an operation failure. */
				if (error == 0) {
					result->server = *server_address;
					result->port = port;
				}

				/* Returns the computed result. */
				return error;
			}
		}
		close(descriptor);
	}

	/* Returns the computed result. */
	return EAI_AGAIN;
}

/* Supports the query id operation. */
static uint16_t
query_id(
	const char *name)
{
	struct timespec now;
	uint32_t hash;

	(void)pthread_mutex_lock(&resolver_counter_lock);
	hash = ++resolver_counter;
	(void)pthread_mutex_unlock(&resolver_counter_lock);

	/* Continue while the operation condition remains true. */
	while (*name != '\0')
		hash = hash * 33U ^ (uint8_t)*name++;

	/* Handles a failed clock gettime operation. */
	if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
		hash ^= (uint32_t)now.tv_nsec ^ (uint32_t)now.tv_sec;

	/* Returns the computed result. */
	return (uint16_t)(hash ^ hash >> 16);
}

/* Supports the tcp query operation. */
static int
tcp_query(
	const struct sockaddr_in *server,
	const uint8_t *query,
	size_t query_length,
	uint16_t id,
	const char *name,
	uint16_t type,
	struct resolver_result *result)
{
	uint8_t request[514], response[2048], prefix[2];
	uint16_t length;
	int descriptor, error, truncated;

	request[0] = (uint8_t)(query_length >> 8);
	request[1] = (uint8_t)query_length;
	memcpy(request + 2, query, query_length);
	descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	/* Checks the file descriptor. */
	if (descriptor < 0)
		return EAI_AGAIN;

	/* Handles a failed connect operation. */
	if (connect(descriptor, (const struct sockaddr *)server,
		    sizeof(*server)) != 0 ||
	    write_all_socket(descriptor, request, query_length + 2U) != 0 ||
	    read_exact_socket(descriptor, prefix, 2U) != 0) {
		close(descriptor);

		/* Returns the computed result. */
		return EAI_AGAIN;
	}
	length = (uint16_t)((uint16_t)prefix[0] << 8 | prefix[1]);

	/* Handles a failed read exact socket operation. */
	if (length > sizeof(response) ||
	    read_exact_socket(descriptor, response, length) != 0) {
		close(descriptor);

		/* Returns the computed result. */
		return EAI_FAIL;
	}
	close(descriptor);
	error = resolver_dns_parse(response, length, id, name, type, result,
				   &truncated);

	/* Returns the computed result. */
	return truncated ? EAI_FAIL : error;
}

/* Supports the write all socket operation. */
static int
write_all_socket(
	int descriptor,
	const uint8_t *buffer,
	size_t length)
{
	ssize_t count;

	/* Process each remaining element. */
	while (length != 0U) {
		count = send(descriptor, buffer, length, 0);

		/* Checks the remaining item count. */
		if (count <= 0)
			return -1;
		buffer += count;
		length -= (size_t)count;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the read exact socket operation. */
static int
read_exact_socket(
	int descriptor,
	uint8_t *buffer,
	size_t length)
{
	ssize_t count;

	/* Process each remaining element. */
	while (length != 0U) {
		count = recv(descriptor, buffer, length, 0);

		/* Checks the remaining item count. */
		if (count <= 0)
			return -1;
		buffer += count;
		length -= (size_t)count;
	}

	/* Reports successful completion. */
	return 0;
}

/* Supports the parse service operation. */
static int
parse_service(
	const char *service,
	const char *protocol,
	uint16_t *port)
{
	char *end;
	unsigned long value;

	/* Handles the service availability. */
	if (service == NULL) {
		*port = 0;
		/* Reports successful completion. */
		return 0;
	}
	value = strtoul(service, &end, 10);

	/* A name that is not a number is looked up in the service database. */
	if (*service == '\0' || *end != '\0') {
		const struct servent *entry;

		entry = getservbyname(service, protocol);
		if (entry == NULL)
			return EAI_SERVICE;
		*port = ntohs((uint16_t)entry->s_port);

		/* Reports successful completion. */
		return 0;
	}

	/* Handles the service condition. */
	if (value > 65535U)
		return EAI_SERVICE;
	*port = (uint16_t)value;
	/* Reports successful completion. */
	return 0;
}

/* Supports the make ptr name operation. */
static int
make_ptr_name(
	struct in_addr address,
	char *output,
	size_t capacity)
{
	int function_result;
	uint32_t value;

	value = ntohl(address.s_addr);

	/* Computes the function result. */
	function_result = snprintf(output, capacity, "%u.%u.%u.%u.in-addr.arpa",
			value & 255U, value >> 8 & 255U, value >> 16 & 255U,
			value >> 24 & 255U) >= (int)capacity
		   ? EAI_OVERFLOW
		   : 0;

	/* Returns the computed result. */
	return function_result;
}

/* ------------------------------------------------------------------ *
 * The service database
 *
 * /etc/services is read line by line.  A returned entry points into
 * per-thread storage that the next call on the same thread reuses, which is
 * what the historical interface promises.
 * ------------------------------------------------------------------ */

#define SERVICE_ALIAS_MAX 8
#define SERVICE_LINE_MAX 256

struct service_state {
	FILE *file;
	int keep_open;
	struct servent entry;
	char name[64];
	char proto[16];
	char alias_text[SERVICE_ALIAS_MAX][64];
	char *aliases[SERVICE_ALIAS_MAX + 1];
};

static __thread struct service_state service_state;

/*
 * Supports the field splitting operation.
 *
 * Returns the next blank-separated field and advances the cursor past it.
 * This is strtok_r's job, which the C library does not have yet.
 */
static char *
next_field(
	char **cursor)
{
	char *text, *start;

	text = *cursor;

	/* Continue while the operation condition remains true. */
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;

	/* Reports that no result is available. */
	if (*text == '\0') {
		*cursor = text;
		return NULL;
	}
	start = text;

	/* Continue while the operation condition remains true. */
	while (*text != '\0' && *text != ' ' && *text != '\t' &&
	       *text != '\r' && *text != '\n')
		text++;

	/* Terminates the field unless the line already ended. */
	if (*text != '\0')
		*text++ = '\0';
	*cursor = text;

	/* Returns the computed result. */
	return start;
}

/* Supports the service state operation. */
static struct service_state *
service_context(void)
{
	/* Returns the computed result. */
	return &service_state;
}

/* Supports the service open operation. */
static int
service_open(
	struct service_state *state)
{
	/* Handles the already open condition. */
	if (state->file != NULL)
		return 0;
	state->file = fopen("/etc/services", "r");

	/* Handles the file availability. */
	if (state->file == NULL)
		return -1;

	/* Reports successful completion. */
	return 0;
}

/*
 * Reads the next entry into the thread's storage.
 *
 * A line is "name port/protocol [alias...]"; anything from a '#' is a
 * comment.  A malformed line is skipped rather than ending the walk.
 */
static struct servent *
service_next(
	struct service_state *state)
{
	char line[SERVICE_LINE_MAX];
	char *text, *field, *slash, *end;
	unsigned long port;
	unsigned count;

	/* Process input until it is exhausted. */
	while (fgets(line, sizeof(line), state->file) != NULL) {
		text = strchr(line, '#');
		if (text != NULL)
			*text = '\0';

		/* Takes the service name. */
		text = line;
		field = next_field(&text);
		if (field == NULL)
			continue;
		if (strlen(field) >= sizeof(state->name))
			continue;
		strcpy(state->name, field);

		/* Takes the port and protocol, which share one field. */
		field = next_field(&text);
		if (field == NULL)
			continue;
		slash = strchr(field, '/');
		if (slash == NULL)
			continue;
		*slash++ = '\0';
		port = strtoul(field, &end, 10);
		if (*field == '\0' || *end != '\0' || port > 65535U)
			continue;
		if (strlen(slash) >= sizeof(state->proto))
			continue;
		strcpy(state->proto, slash);

		/* Takes any further names for the same entry. */
		count = 0;
		while (count < SERVICE_ALIAS_MAX) {
			field = next_field(&text);
			if (field == NULL)
				break;
			if (strlen(field) >= sizeof(state->alias_text[0]))
				continue;
			strcpy(state->alias_text[count], field);
			state->aliases[count] = state->alias_text[count];
			count++;
		}
		state->aliases[count] = NULL;

		state->entry.s_name = state->name;
		state->entry.s_aliases = state->aliases;
		state->entry.s_port = (int)htons((uint16_t)port);
		state->entry.s_proto = state->proto;

		/* Returns the computed result. */
		return &state->entry;
	}

	/* Reports that no result is available. */
	return NULL;
}

/*
 * Implements the setservent operation.
 */
void
setservent(int keep_open)
{
	struct service_state *state;

	state = service_context();
	state->keep_open = keep_open != 0;

	/* Restarts the walk when the database is already open. */
	if (state->file != NULL)
		rewind(state->file);
	else
		(void)service_open(state);
}

/*
 * Implements the endservent operation.
 */
void
endservent(void)
{
	struct service_state *state;

	state = service_context();

	/* Handles the file availability. */
	if (state->file != NULL) {
		(void)fclose(state->file);
		state->file = NULL;
	}
	state->keep_open = 0;
}

/*
 * Implements the getservent operation.
 */
struct servent *
getservent(void)
{
	struct service_state *state;

	state = service_context();

	/* Handles a failed open operation. */
	if (service_open(state) != 0)
		return NULL;

	/* Returns the computed result. */
	return service_next(state);
}

/* Supports the service lookup operation. */
static struct servent *
service_find(
	const char *name,
	int port,
	const char *protocol)
{
	struct service_state *state;
	struct servent *entry;
	unsigned index;
	int matched;

	state = service_context();

	/* Handles a failed open operation. */
	if (service_open(state) != 0)
		return NULL;
	rewind(state->file);

	/* Process each remaining element. */
	while ((entry = service_next(state)) != NULL) {
		/* Skips an entry for a different protocol. */
		if (protocol != NULL && strcmp(entry->s_proto, protocol) != 0)
			continue;

		/* Selects by name, including the further names. */
		if (name != NULL) {
			matched = strcmp(entry->s_name, name) == 0;
			for (index = 0; !matched &&
			     entry->s_aliases[index] != NULL; index++)
				matched = strcmp(entry->s_aliases[index],
						 name) == 0;
			if (!matched)
				continue;
		} else if (entry->s_port != port) {
			continue;
		}

		/* Leaves the database open only when asked to. */
		if (!state->keep_open) {
			(void)fclose(state->file);
			state->file = NULL;
		}

		/* Returns the computed result. */
		return entry;
	}

	/* Leaves the database open only when asked to. */
	if (!state->keep_open) {
		(void)fclose(state->file);
		state->file = NULL;
	}

	/* Reports that no result is available. */
	return NULL;
}

/*
 * Implements the getservbyname operation.
 */
struct servent *
getservbyname(const char *name, const char *protocol)
{
	/* Handles the name availability. */
	if (name == NULL)
		return NULL;

	/* Returns the computed result. */
	return service_find(name, 0, protocol);
}

/*
 * Implements the getservbyport operation.
 */
struct servent *
getservbyport(int port, const char *protocol)
{
	/* Returns the computed result. */
	return service_find(NULL, port, protocol);
}

/* ------------------------------------------------------------------ *
 * The host database
 *
 * These are the interfaces POSIX.1-2008 removed.  They are answered from the
 * same resolver getaddrinfo uses, so there is one name service and not two,
 * and the result lives in per-thread storage the next call reuses.
 * ------------------------------------------------------------------ */

#define HOST_ADDRESS_MAX 8

struct host_state {
	int error;
	struct hostent entry;
	char name[256];
	struct in_addr addresses[HOST_ADDRESS_MAX];
	char *address_list[HOST_ADDRESS_MAX + 1];
	char *aliases[1];
};

static __thread struct host_state host_state;

/*
 * Implements the h_errno location operation.
 */
int *
__h_errno_location(void)
{
	/* Returns the computed result. */
	return &host_state.error;
}

/*
 * Implements the hstrerror operation.
 */
const char *
hstrerror(int error)
{
	/* Selects the matching description. */
	switch (error) {
	case 0:
		return "Resolver error 0";
	case HOST_NOT_FOUND:
		return "Unknown host";
	case TRY_AGAIN:
		return "Host name lookup failure";
	case NO_RECOVERY:
		return "Unknown server error";
	case NO_DATA:
		return "No address associated with name";
	default:
		break;
	}

	/* Returns the computed result. */
	return "Unknown resolver error";
}

/*
 * Implements the sethostent operation.
 *
 * There is no host file to hold open, so the request is accepted and the
 * resolver is consulted per call.
 */
void
sethostent(int keep_open)
{
	(void)keep_open;
}

/*
 * Implements the endhostent operation.
 */
void
endhostent(void)
{
}

/* Supports the host error translation operation. */
static int
host_error_of(
	int error)
{
	/* Selects the matching description. */
	switch (error) {
	case EAI_NONAME:
	case EAI_ADDRFAMILY:
		return HOST_NOT_FOUND;
	case EAI_AGAIN:
		return TRY_AGAIN;
	case EAI_MEMORY:
	case EAI_SYSTEM:
	case EAI_FAIL:
		return NO_RECOVERY;
	default:
		break;
	}

	/* Returns the computed result. */
	return NO_RECOVERY;
}

/* Supports the host entry publication operation. */
static struct hostent *
host_publish(
	struct host_state *state,
	const char *name,
	unsigned count)
{
	unsigned index;

	/* Handles the absence of any address. */
	if (count == 0) {
		state->error = HOST_NOT_FOUND;
		return NULL;
	}

	/* Process each remaining element. */
	for (index = 0; index < count; index++)
		state->address_list[index] = (char *)&state->addresses[index];
	state->address_list[count] = NULL;
	state->aliases[0] = NULL;

	/* Keeps the reported name inside the thread's storage. */
	if (name != NULL && name != state->name) {
		if (strlen(name) >= sizeof(state->name)) {
			state->error = NO_RECOVERY;
			return NULL;
		}
		strcpy(state->name, name);
	}

	state->entry.h_name = state->name;
	state->entry.h_aliases = state->aliases;
	state->entry.h_addrtype = AF_INET;
	state->entry.h_length = (int)sizeof(struct in_addr);
	state->entry.h_addr_list = state->address_list;
	state->error = 0;

	/* Returns the computed result. */
	return &state->entry;
}

/*
 * Implements the gethostbyname operation.
 */
struct hostent *
gethostbyname(const char *name)
{
	struct host_state *state;
	struct addrinfo hints;
	struct addrinfo *list;
	struct addrinfo *entry;
	struct in_addr literal;
	unsigned count;
	int error;

	state = &host_state;

	/* Rejects a missing name. */
	if (name == NULL) {
		state->error = HOST_NOT_FOUND;
		return NULL;
	}

	/* An address in text form is its own answer. */
	if (inet_aton(name, &literal) != 0) {
		state->addresses[0] = literal;

		/* Returns the computed result. */
		return host_publish(state, name, 1);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	list = NULL;
	error = getaddrinfo(name, NULL, &hints, &list);

	/* Handles a failed lookup. */
	if (error != 0) {
		state->error = host_error_of(error);
		return NULL;
	}

	/* Collects the addresses the resolver returned. */
	count = 0;
	for (entry = list; entry != NULL && count < HOST_ADDRESS_MAX;
	     entry = entry->ai_next) {
		if (entry->ai_family != AF_INET || entry->ai_addr == NULL)
			continue;
		state->addresses[count++] =
		    ((const struct sockaddr_in *)(const void *)
			entry->ai_addr)->sin_addr;
	}

	/* Prefers the canonical name the resolver reported. */
	if (list != NULL && list->ai_canonname != NULL &&
	    strlen(list->ai_canonname) < sizeof(state->name))
		strcpy(state->name, list->ai_canonname);
	else if (strlen(name) < sizeof(state->name))
		strcpy(state->name, name);
	else
		count = 0;
	freeaddrinfo(list);

	/* Returns the computed result. */
	return host_publish(state, state->name, count);
}

/*
 * Implements the gethostbyaddr operation.
 */
struct hostent *
gethostbyaddr(const void *address, socklen_t length, int family)
{
	struct host_state *state;
	struct sockaddr_in query;
	char name[256];
	int error;

	state = &host_state;

	/* Only IPv4 addresses can be looked up. */
	if (address == NULL || family != AF_INET ||
	    length != sizeof(struct in_addr)) {
		state->error = HOST_NOT_FOUND;
		return NULL;
	}
	memcpy(&state->addresses[0], address, sizeof(struct in_addr));

	memset(&query, 0, sizeof(query));
	query.sin_family = AF_INET;
	query.sin_addr = state->addresses[0];
	error = getnameinfo((const struct sockaddr *)&query, sizeof(query),
	    name, (socklen_t)sizeof(name), NULL, 0, NI_NAMEREQD);

	/* Handles a failed reverse lookup. */
	if (error != 0) {
		state->error = host_error_of(error);
		return NULL;
	}

	/* Returns the computed result. */
	return host_publish(state, name, 1);
}
