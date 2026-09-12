/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A finite, fullscreen Wayland service using independent shared GPU resources.
 */

#include "zwl.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Signal handlers only request ordinary event-loop cleanup; they own no GPU state. */
static volatile sig_atomic_t stop_requested;

static void stop_service(int signal_number);
static int parse_options(struct zwl_server *server, int count, char **arguments);
static int unsigned_option(const char *text, uint64_t maximum, uint64_t *number);
static int listen_socket(struct zwl_server *server);
static void unlink_socket(struct zwl_server *server);
static int accept_client(struct zwl_server *server);
static int event_loop(struct zwl_server *server);
static void service_cleanup(struct zwl_server *server);

/*
 * Runs one bounded fullscreen compositor instance and cleans up its own resources.
 */
int
main(
	int count,
	char **arguments)
{
	struct zwl_server server;
	void (*previous_handler)(int);
	int error;
	int cleanup_failed;

	/* Every descriptor starts invalid so partial initialization can use ordinary cleanup. */
	memset(&server, 0, sizeof(server));
	server.listener = -1;
	server.gpu = -1;
	server.gpu_path = "/dev/gpu0";
	server.width = 320;
	server.height = 240;
	server.timeout_ms = 150000;
	strcpy(server.socket_path, "/tmp/wayland-0");
	setvbuf(stdout, NULL, _IOLBF, 0);
	error = parse_options(&server, count, arguments);
	if (error != 0) {
		fprintf(stderr, "usage: zwl [--socket=/path] [--gpu=/dev/gpu0] [--width=N] [--height=N] [--timeout=seconds] [--max-frames=N]\n");
		return 2;
	}

	/* Catch normal termination without performing allocation or I/O inside a signal handler. */
	previous_handler = signal(SIGINT, stop_service);
	if (previous_handler == SIG_ERR)
		return 1;

	/* SIGTERM follows the same lease-safe shutdown path. */
	previous_handler = signal(SIGTERM, stop_service);
	if (previous_handler == SIG_ERR)
		return 1;

	/* Open an independent GPU context before publishing a usable Wayland endpoint. */
	error = zwl_gpu_open(&server);
	if (error == 0)
		error = listen_socket(&server);

	/* READY appears only after the hardware contract and socket namespace are both usable. */
	if (error == 0) {
		printf("ZWL READY socket=%s width=%u height=%u timeout_ms=%llu pid=%ld\n", server.socket_path, server.width, server.height, (unsigned long long)server.timeout_ms, (long)getpid());
		error = event_loop(&server);
	}

	/* No exit path leaves a lease, imported image or owned socket generation behind. */
	service_cleanup(&server);
	cleanup_failed = server.failed;
	printf("ZWL EXIT frames=%llu error=%d cleanup_failed=%d pid=%ld\n", (unsigned long long)server.frame, error, cleanup_failed, (long)getpid());
	if (error != 0 || cleanup_failed)
		return 1;

	/* Succeeded: the finite service run completed and every owned resource was retired. */
	return 0;
}

/* Requests cleanup from the main loop without touching non-signal-safe state. */
static void
stop_service(
	int signal_number)
{
	/* Both accepted termination signals mean the same orderly service withdrawal. */
	(void)signal_number;
	stop_requested = 1;

	/* Succeeded: the event loop will withdraw this service generation. */
	return;
}

/* Parses explicit bounded service settings without depending on environment state. */
static int
parse_options(
	struct zwl_server *server,
	int count,
	char **arguments)
{
	const char *argument;
	const char *text;
	uint64_t number;
	size_t length;
	int index;
	int match;
	int error;

	/* Each option is independent and leaves an explicit configured service value. */
	for (index = 1; index < count; index++) {
		/* Endpoint selection is validated before copying it into bounded service storage. */
		argument = arguments[index];
		match = strncmp(argument, "--socket=", 9);
		if (match == 0) {
			/* Relative or overlong endpoints cannot identify the service's owned pathname. */
			text = argument + 9;
			length = strlen(text);
			if (text[0] != '/' || length >= sizeof(server->socket_path))
				return EINVAL;

			/* Absolute paths make socket-generation ownership explicit. */
			strcpy(server->socket_path, text);
			continue;
		}

		/* The renderer node is selected independently of the Wayland endpoint. */
		match = strncmp(argument, "--gpu=", 6);
		if (match == 0) {
			/* GPU selection uses an explicit device path independent of current directory. */
			if (argument[6] != '/')
				return EINVAL;

			/* Keep the immutable argv storage for the duration of this process. */
			server->gpu_path = argument + 6;
			continue;
		}

		/* Width and height remain subject to the native side-effect-free mode validation. */
		match = strncmp(argument, "--width=", 8);
		if (match == 0) {
			/* An out-of-range width cannot reach native mode validation. */
			error = unsigned_option(argument + 8, 16384, &number);
			if (error != 0)
				return error;

			/* Publish a positive bounded width only after parsing succeeds. */
			server->width = (uint32_t)number;
			continue;
		}

		/* The configured image shape is fixed throughout this compositor generation. */
		match = strncmp(argument, "--height=", 9);
		if (match == 0) {
			/* An out-of-range height cannot reach native mode validation. */
			error = unsigned_option(argument + 9, 16384, &number);
			if (error != 0)
				return error;

			/* Publish a positive bounded height only after parsing succeeds. */
			server->height = (uint32_t)number;
			continue;
		}

		/* Service timeout always remains finite, including unattended test invocations. */
		match = strncmp(argument, "--timeout=", 10);
		if (match == 0) {
			/* A positive finite timeout is mandatory for this service invocation. */
			error = unsigned_option(argument + 10, 3600, &number);
			if (error != 0)
				return error;

			/* Store the configured deadline interval in the monotonic clock's units. */
			server->timeout_ms = number * 1000U;
			continue;
		}

		/* An optional frame bound can end a short smoke run before its wall-clock timeout. */
		match = strncmp(argument, "--max-frames=", 13);
		if (match == 0) {
			/* Completed-frame accounting remains finite when this additional limit is selected. */
			error = unsigned_option(argument + 13, 1000000, &number);
			if (error != 0)
				return error;

			/* Only completed GPU presentations count toward this bound. */
			server->max_frames = number;
			continue;
		}

		/* Unknown arguments cannot silently alter the test or service contract. */
		return EINVAL;
	}

	/* Succeeded: every option is explicit, bounded and understood. */
	return 0;
}

/* Parses one positive decimal value without overflow, whitespace or trailing garbage. */
static int
unsigned_option(
	const char *text,
	uint64_t maximum,
	uint64_t *number)
{
	uint64_t parsed;
	unsigned digit;

	/* Empty values are not a request to retain the default setting. */
	if (*text == '\0')
		return EINVAL;

	/* A bounded multiply-add exposes each malformed or overflowing input directly. */
	parsed = 0;
	while (*text != '\0') {
		/* Signs, whitespace and trailing text are outside the numeric option grammar. */
		if (*text < '0' || *text > '9')
			return EINVAL;

		/* Refuse overflow before extending the number by another decimal digit. */
		digit = (unsigned)(*text - '0');
		if (parsed > maximum / 10U || (parsed == maximum / 10U && digit > maximum % 10U))
			return EINVAL;

		/* The validated digit extends the same positive bounded value. */
		parsed = parsed * 10U + digit;
		text++;
	}

	/* Zero would make dimensions invalid or silently remove a finite bound. */
	if (parsed == 0)
		return EINVAL;

	/* Succeeded: the caller receives one positive representable setting. */
	*number = parsed;
	return 0;
}

/* Publishes a new Unix endpoint without deleting or replacing an existing server path. */
static int
listen_socket(
	struct zwl_server *server)
{
	struct sockaddr_un address;
	struct stat status;
	socklen_t length;
	int error;

	/* Binding an existing pathname must fail instead of guessing whether its owner is stale. */
	server->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (server->listener < 0)
		return errno;

	/* The pathname terminator is part of the Unix address length. */
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, server->socket_path);
	length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1U);
	error = bind(server->listener, (const struct sockaddr *)&address, length);
	if (error != 0)
		return errno;

	/* Record the actual created inode so cleanup never intentionally removes a replacement. */
	error = lstat(server->socket_path, &status);
	if (error != 0)
		return errno;

	/* The pathname identity belongs only to this successfully bound generation. */
	server->socket_device = status.st_dev;
	server->socket_inode = status.st_ino;
	server->socket_owned = 1;
	error = listen(server->listener, 16);
	if (error != 0)
		return errno;

	/* Succeeded: incoming clients can reach this exact socket generation. */
	return 0;
}

/* Removes only the pathname identity created by this service instance. */
static void
unlink_socket(
	struct zwl_server *server)
{
	struct stat status;
	int error;

	/* Failed binds never own an existing server's pathname. */
	if (!server->socket_owned)
		return;

	/* A removed or replaced socket path no longer belongs to this cleanup pass. */
	server->socket_owned = 0;
	error = lstat(server->socket_path, &status);
	if (error != 0)
		return;

	/* Inode identity protects ordinary concurrent server-generation replacement. */
	if (status.st_dev != server->socket_device || status.st_ino != server->socket_inode)
		return;

	/* The known pathname is withdrawn before the listener descriptor is closed. */
	error = unlink(server->socket_path);
	if (error != 0 && errno != ENOENT)
		server->failed = 1;

	/* Succeeded: this service generation no longer publishes its pathname. */
	return;
}

/* Accepts one client and creates its required display object before processing requests. */
static int
accept_client(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_object *display;
	int descriptor;

	/* Nonblocking acceptance cannot stall already connected clients. */
	descriptor = accept4(server->listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (descriptor < 0) {
		/* Transient readiness or signal changes leave the listening generation usable. */
		if (errno == EAGAIN ||
		    errno == EWOULDBLOCK ||
		    errno == EINTR)
			return 0;

		/* Per-process descriptor exhaustion is explicit and terminates this finite run. */
		return errno;
	}

	/* Client state owns every subsequently received byte, fd and object. */
	client = calloc(1, sizeof(*client));
	if (client == NULL) {
		close(descriptor);
		return ENOMEM;
	}

	/* The service-local client serial distinguishes descriptor-number reuse in logs. */
	client->fd = descriptor;
	client->server = server;
	client->number = ++server->client_serial;
	client->next = server->clients;
	server->clients = client;
	display = zwl_create(client, 1, ZWL_DISPLAY, 1);
	if (display == NULL) {
		zwl_client_destroy(client);
		return ENOMEM;
	}

	/* READY already guarantees that this independent namespace can import images. */
	printf("ZWL CLIENT client=%llu fd=%d\n", (unsigned long long)client->number, descriptor);

	/* Succeeded: the display object owns the connection's first live identity. */
	return 0;
}

/* Services independent client streams and schedules bounded fullscreen presentations. */
static int
event_loop(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_client **clients;
	struct pollfd *descriptors;
	uint64_t started;
	uint64_t now;
	size_t count;
	size_t index;
	int timeout;
	int ready;
	int error;
	int remove;

	/* A finite monotonic deadline covers both idle service and active clients. */
	started = zwl_milliseconds();
	if (started == UINT64_MAX)
		return EIO;

	/* Each pass rebuilds the descriptor generation snapshot after prior cleanup. */
	while (!stop_requested && !server->failed) {
		/* Clock failure must not turn the configured service deadline into an endless run. */
		now = zwl_milliseconds();
		if (now == UINT64_MAX)
			return EIO;

		/* The optional frame bound counts only completed hardware selections. */
		if (now - started >= server->timeout_ms ||
		    (server->max_frames != 0 && server->frame >= server->max_frames))
			break;

		/* Allocate exactly enough poll storage for the presently live client set. */
		count = 1;
		for (client = server->clients; client != NULL; client = client->next)
			count++;

		/* Allocation failure leaves all live clients owned by service cleanup. */
		descriptors = calloc(count, sizeof(*descriptors));
		if (descriptors == NULL)
			return ENOMEM;

		/* The parallel pointer array resolves readiness to this snapshot's generation. */
		clients = calloc(count, sizeof(*clients));
		if (clients == NULL) {
			free(descriptors);
			return ENOMEM;
		}

		/* A short finite poll interval also bounds cleanup of fatal clients with stalled output. */
		descriptors[0].fd = server->listener;
		descriptors[0].events = POLLIN;
		timeout = 10;
		index = 1;
		for (client = server->clients; client != NULL; client = client->next) {
			/* Retain the snapshot identity while suppressing new input on fatal connections. */
			clients[index] = client;
			descriptors[index].fd = client->fd;
			if (!client->fatal)
				descriptors[index].events = POLLIN;

			/* Queued bytes retain interest in writable readiness without busy-looping send. */
			if (client->output_head != NULL)
				descriptors[index].events |= POLLOUT;

			/* A fully flushed fatal connection can be retired immediately. */
			if (client->fatal && client->output_head == NULL)
				timeout = 0;

			/* Each slot and pointer now describe one live connection generation. */
			index++;
		}

		/* Poll sees sockets only; typed image fds are consumed immediately during import. */
		ready = poll(descriptors, count, timeout);
		if (ready < 0) {
			error = errno;
			free(clients);
			free(descriptors);

			/* A signal wakeup returns to the loop's ordinary shutdown and deadline checks. */
			if (error == EINTR)
				continue;

			/* Other poll failures stop the service through ordinary cleanup. */
			return error;
		}

		/* Accept at most one client per pass so existing streams retain service opportunity. */
		error = 0;
		if ((descriptors[0].revents & POLLIN) != 0)
			error = accept_client(server);

		/* A failed listener cannot serve this generation any further. */
		if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			error = EIO;

		/* Process only the clients captured by this poll snapshot. */
		for (index = 1; index < count; index++) {
			/* Apply readiness only to this iteration's still-owned connection generation. */
			client = clients[index];
			remove = 0;
			if ((descriptors[index].revents & POLLIN) != 0 && !client->fatal) {
				/* Decode all complete requests while retaining partial bytes and ancillary ownership. */
				ready = zwl_read(client);
				if (ready != 0 && !client->fatal) {
					/* Malformed ancillary data gets a terminal protocol event before withdrawal. */
					if (ready == EPROTO)
						(void)zwl_error(client, 1, "malformed ancillary input");
					else
						remove = 1;
				}
			}

			/* A disconnected peer cannot receive queued events or use future GPU imports. */
			if ((descriptors[index].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
				remove = 1;

			/* Flush events produced by requests even when POLLOUT was absent from this snapshot. */
			if (!remove) {
				/* Preserve unsent suffixes, but close a stream that can no longer accept events. */
				ready = zwl_flush(client);
				if (ready != 0)
					remove = 1;
			}

			/* Protocol errors get a bounded final flush before their namespace is destroyed. */
			if (client->fatal &&
			    (client->output_head == NULL ||
			     (now >= client->fatal_time && now - client->fatal_time >= 2000U)))
				remove = 1;

			/* Withdrawal releases unread rights and scanout ownership before fd reuse. */
			if (remove)
				zwl_client_destroy(client);
		}

		/* The snapshot contains no ownership references beyond this iteration. */
		free(clients);
		free(descriptors);
		if (error != 0)
			return error;

		/* The scheduler presents at most one latest per-surface queued image this pass. */
		zwl_schedule(server);
	}

	/* A hardware cleanup failure is distinct from a normal finite timeout or signal exit. */
	if (server->failed)
		return EIO;

	/* Succeeded: the requested finite service interval has ended. */
	return 0;
}

/* Withdraws display ownership, client generations and the service's own pathname in order. */
static void
service_cleanup(
	struct zwl_server *server)
{
	int error;

	/* No new client should observe a service whose cleanup has begun. */
	unlink_socket(server);
	if (server->listener >= 0) {
		close(server->listener);
		server->listener = -1;
	}

	/* Hardware stops borrowing front storage before any client import is destroyed. */
	error = zwl_unscan(server);
	if (error != 0)
		server->failed = 1;

	/* Each client cleanup closes both user-received and still-kernel-queued rights. */
	while (server->clients != NULL)
		zwl_client_destroy(server->clients);

	/* Closing the independent renderer session completes all remaining native cleanup. */
	if (server->gpu >= 0) {
		close(server->gpu);
		server->gpu = -1;
	}

	/* Succeeded: the service retains no listener, client or GPU descriptor. */
	return;
}
