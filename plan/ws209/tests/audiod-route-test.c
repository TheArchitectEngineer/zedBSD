/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws209-p002: audiod's endpoints and routes on the host (T1 of
 * plan/ws209/phase001/phase.md).  Talks to an audiod built for the host
 * (-DAUDIOD_HOST_TEST: its socket, the Bluetooth daemon's and the seat's
 * uids from the environment) through several connections: a version 1
 * client, a daemon that registers endpoints, the seat's compositor, and a
 * player.  The host has no /dev/dsp0, so there are no device endpoints.
 *
 *   audiod-route-test SOCKET [denied]
 *
 * "denied": the audiod was started with uids that are not this process's;
 * only the refusals are checked.  Prints a line for each failed check and
 * "audiod-route: all PASS" at the end.
 */

#include "userland/base/audiod/protocol.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* A connection: its socket, its bytes not yet taken, and the last descriptor received. */
struct link {
	int fd;
	uint8_t input[65536];
	size_t used;
	int received_fd;
	uint32_t serial;
};

/* Any value of a field. */
#define ANY	0xffffffffU

static unsigned failures;
static const char *socket_path;

static void check(int good, const char *what);
static int link_open(struct link *link, uint32_t version);
static void link_send(struct link *link, void *message, uint32_t length);
static int link_find(struct link *link, uint32_t type, uint32_t serial, void *message, size_t size, int timeout_ms);
static int link_take(struct link *link, uint32_t type, void *message, size_t size, int timeout_ms);
static uint32_t link_request(struct link *link, void *message, uint32_t length, uint32_t type);
static int link_result(struct link *link, uint32_t serial);
static uint32_t add_endpoint(struct link *link, uint32_t kind, uint32_t endpoint_class, uint32_t group, uint32_t state, uint32_t flags, const char *key);
static int route_set(struct link *link, uint32_t route, uint32_t output, uint32_t input);
static int wait_route(struct link *link, uint32_t route, uint32_t output, uint32_t reason, struct audiod_route_info *info, int timeout_ms);
static int wait_wanted(struct link *link, uint32_t id, uint32_t wanted, struct audiod_endpoint_wanted *message, int timeout_ms);
static void test_denied(void);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	/* A failure is printed and counted. */
	if (!good) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Connects and says HELLO with a version; returns WELCOME's device (or -1). */
static int
link_open(
	struct link *link,
	uint32_t version)
{
	struct sockaddr_un address;
	struct audiod_hello hello;
	struct audiod_welcome welcome;
	uint32_t agreed;
	int error;
	int got;

	/* The socket. */
	memset(link, 0, sizeof(*link));
	link->received_fd = -1;
	link->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
	error = connect(link->fd, (struct sockaddr *)&address, sizeof(address));
	if (error != 0)
		return -1;

	/* HELLO and WELCOME. */
	memset(&hello, 0, sizeof(hello));
	hello.header.type = AUDIOD_HELLO;
	hello.header.length = sizeof(hello);
	hello.version = version;
	link_send(link, &hello, sizeof(hello));
	got = link_take(link, AUDIOD_WELCOME, &welcome, sizeof(welcome), 2000);
	agreed = 1U;
	if (version >= 2U)
		agreed = 2U;
	if (!got || welcome.version != agreed)
		return -1;

	/* Succeeded: whether there is an output. */
	return (int)welcome.device;
}

/* Sends a message with the next serial number. */
static void
link_send(
	struct link *link,
	void *message,
	uint32_t length)
{
	struct audiod_header *header;
	ssize_t sent;

	/* Numbered and sent whole. */
	header = message;
	link->serial++;
	header->serial = link->serial;
	sent = send(link->fd, message, length, 0);
	if (sent != (ssize_t)length)
		printf("FAIL send\n");
}

/*
 * Waits for a message of a type (and, when serial is not 0, that serial),
 * taking it out of what came and leaving the others for later checks;
 * keeps a descriptor that came with it.  Returns 1 with the message, 0
 * when none came in time.
 */
static int
link_find(
	struct link *link,
	uint32_t type,
	uint32_t serial,
	void *message,
	size_t size,
	int timeout_ms)
{
	union {
		struct cmsghdr header;
		char space[CMSG_SPACE(sizeof(int))];
	} control;
	struct audiod_header header;
	struct msghdr received;
	struct iovec vector;
	struct cmsghdr *cmsg;
	struct pollfd wait;
	size_t offset;
	size_t copied;
	ssize_t got;
	int ready;

	/* Until one matches or the time is up. */
	for (;;) {
		/* A whole matching message anywhere in what came. */
		offset = 0U;
		while (link->used - offset >= sizeof(header)) {
			memcpy(&header, link->input + offset, sizeof(header));
			if (header.length < sizeof(header) || link->used - offset < header.length)
				break;
			if (header.type == type && (serial == 0U || header.serial == serial)) {
				copied = header.length;
				if (copied > size)
					copied = size;
				memset(message, 0, size);
				memcpy(message, link->input + offset, copied);
				memmove(link->input + offset, link->input + offset + header.length, link->used - offset - header.length);
				link->used -= header.length;
				return 1;
			}

			/* The next message. */
			offset += header.length;
		}

		/* More bytes. */
		wait.fd = link->fd;
		wait.events = POLLIN;
		ready = poll(&wait, 1, timeout_ms);
		if (ready <= 0)
			return 0;
		memset(&received, 0, sizeof(received));
		vector.iov_base = link->input + link->used;
		vector.iov_len = sizeof(link->input) - link->used;
		received.msg_iov = &vector;
		received.msg_iovlen = 1;
		received.msg_control = control.space;
		received.msg_controllen = sizeof(control.space);
		got = recvmsg(link->fd, &received, 0);
		if (got <= 0)
			return 0;
		link->used += (size_t)got;
		cmsg = CMSG_FIRSTHDR(&received);
		if (cmsg != NULL && cmsg->cmsg_type == SCM_RIGHTS)
			memcpy(&link->received_fd, CMSG_DATA(cmsg), sizeof(int));
	}
}

/* Waits for the next message of a type. */
static int
link_take(
	struct link *link,
	uint32_t type,
	void *message,
	size_t size,
	int timeout_ms)
{
	int got;

	/* Any serial. */
	got = link_find(link, type, 0U, message, size, timeout_ms);
	return got;
}

/* Waits for a ROUTE_INFO of a route with an output and a reason (ANY for either). */
static int
wait_route(
	struct link *link,
	uint32_t route,
	uint32_t output,
	uint32_t reason,
	struct audiod_route_info *info,
	int timeout_ms)
{
	int waited;
	int got;

	/* Each ROUTE_INFO until one fits. */
	for (waited = 0; waited < timeout_ms; waited += 50) {
		got = link_take(link, AUDIOD_ROUTE_INFO, info, sizeof(*info), 50);
		if (!got)
			continue;
		if (info->route != route)
			continue;
		if (output != ANY && info->output != output)
			continue;
		if (reason != ANY && info->reason != reason)
			continue;
		return 1;
	}

	/* None. */
	return 0;
}

/* Waits for an ENDPOINT_WANTED of an endpoint, open or close. */
static int
wait_wanted(
	struct link *link,
	uint32_t id,
	uint32_t wanted,
	struct audiod_endpoint_wanted *message,
	int timeout_ms)
{
	int waited;
	int got;

	/* Each WANTED until one fits. */
	for (waited = 0; waited < timeout_ms; waited += 50) {
		got = link_take(link, AUDIOD_ENDPOINT_WANTED, message, sizeof(*message), 50);
		if (got && message->id == id && message->wanted == wanted)
			return 1;
	}

	/* None. */
	return 0;
}

/* Sends a request and returns its serial. */
static uint32_t
link_request(
	struct link *link,
	void *message,
	uint32_t length,
	uint32_t type)
{
	struct audiod_header *header;

	/* Typed, sized, sent. */
	header = message;
	header->type = type;
	header->length = length;
	link_send(link, message, length);
	return link->serial;
}

/* Waits for a request's DONE (0) or ERROR (its error); -1 when none came. */
static int
link_result(
	struct link *link,
	uint32_t serial)
{
	struct audiod_result result;
	int waited;
	int got;

	/* DONE or ERROR for the serial, looked for in turn. */
	for (waited = 0; waited < 3000; waited += 10) {
		got = link_find(link, AUDIOD_DONE, serial, &result, sizeof(result), 0);
		if (got)
			return 0;
		got = link_find(link, AUDIOD_ERROR, serial, &result, sizeof(result), 10);
		if (got)
			return (int)result.error;
	}

	/* No answer. */
	return -1;
}

/* Registers an endpoint (S16 stereo 48 kHz); returns its id, 0 on a refusal. */
static uint32_t
add_endpoint(
	struct link *link,
	uint32_t kind,
	uint32_t endpoint_class,
	uint32_t group,
	uint32_t state,
	uint32_t flags,
	const char *key)
{
	struct audiod_endpoint_add add;
	struct audiod_endpoint_added added;
	int got;

	/* The request. */
	memset(&add, 0, sizeof(add));
	add.kind = kind;
	add.endpoint_class = endpoint_class;
	add.group = group;
	add.format = AUDIOD_FORMAT_S16_LE;
	add.channels = 2U;
	add.rate = 48000U;
	add.buffer_frames = 4800U;
	add.target_frames = 1920U;
	add.state = state;
	add.flags = flags;
	snprintf(add.key, sizeof(add.key), "%s", key);
	snprintf(add.name, sizeof(add.name), "test %s", key);
	(void)link_request(link, &add, sizeof(add), AUDIOD_ENDPOINT_ADD);

	/* ENDPOINT_ADDED with the ring, or a refusal. */
	got = link_take(link, AUDIOD_ENDPOINT_ADDED, &added, sizeof(added), 1000);
	if (!got)
		return 0U;
	if (link->received_fd >= 0)
		(void)close(link->received_fd);
	link->received_fd = -1;

	/* Succeeded: its id. */
	return added.id;
}

/* Sets a route; returns the result. */
static int
route_set(
	struct link *link,
	uint32_t route,
	uint32_t output,
	uint32_t input)
{
	struct audiod_route_set request;
	uint32_t serial;

	/* ROUTE_SET and its answer. */
	memset(&request, 0, sizeof(request));
	request.route = route;
	request.output = output;
	request.input = input;
	serial = link_request(link, &request, sizeof(request), AUDIOD_ROUTE_SET);
	return link_result(link, serial);
}

/* With audiod's uids not this process's: registering and choosing are refused. */
static void
test_denied(
	void)
{
	struct link daemon;
	struct audiod_endpoint_add add;
	uint32_t serial;
	int result;

	/* A version 2 client of another uid. */
	(void)link_open(&daemon, 2U);
	memset(&add, 0, sizeof(add));
	add.kind = AUDIOD_KIND_SINK;
	add.endpoint_class = AUDIOD_CLASS_HEADSET_MEDIA;
	add.format = AUDIOD_FORMAT_S16_LE;
	add.channels = 2U;
	add.rate = 48000U;
	add.buffer_frames = 4800U;
	add.state = AUDIOD_ENDPOINT_DORMANT;
	serial = link_request(&daemon, &add, sizeof(add), AUDIOD_ENDPOINT_ADD);
	result = link_result(&daemon, serial);
	check(result == EACCES, "ENDPOINT_ADD of another uid is EACCES");
	result = route_set(&daemon, AUDIOD_ROUTE_MEDIA, 0U, 0U);
	check(result == EACCES, "ROUTE_SET of another uid is EACCES");
}

/* Runs the scenario. */
int
main(
	int argc,
	char **argv)
{
	struct link old;
	struct link daemon;
	struct link seat;
	struct link player;
	struct audiod_header header;
	struct audiod_endpoint_info info;
	struct audiod_route_info route;
	struct audiod_endpoint_wanted wanted;
	struct audiod_endpoint_state state;
	struct audiod_media_held held;
	struct audiod_subscribe subscribe;
	struct audiod_stream_create create;
	struct audiod_volume volume;
	struct audiod_endpoint_volume endpoint_volume;
	struct audiod_call call;
	uint32_t a2dp;
	uint32_t slow;
	uint32_t phone_out;
	uint32_t phone_in;
	uint32_t voice_out;
	uint32_t voice_in;
	uint32_t serial;
	int device;
	int result;
	int denied;
	int got;

	/* The socket, and the refusals only. */
	if (argc < 2)
		return 2;
	socket_path = argv[1];
	denied = argc >= 3;
	if (denied) {
		test_denied();
		if (failures != 0U) {
			printf("audiod-route: %u FAIL\n", failures);
			return 1;
		}

		/* The refusals held. */
		printf("audiod-route: all PASS\n");
		return 0;
	}

	/* A version 1 client: no output yet, version 2's requests refused. */
	device = link_open(&old, 1U);
	check(device == 0, "version 1 WELCOME without an output");
	memset(&header, 0, sizeof(header));
	serial = link_request(&old, &header, sizeof(header), AUDIOD_ENDPOINT_LIST);
	result = link_result(&old, serial);
	check(result == EINVAL, "version 1 may not list endpoints");

	/* The seat subscribes to endpoints, routes and the volume. */
	device = link_open(&seat, 2U);
	check(device == 0, "version 2 WELCOME");
	memset(&subscribe, 0, sizeof(subscribe));
	subscribe.mask = AUDIOD_SUBSCRIBE_VOLUME | AUDIOD_SUBSCRIBE_ENDPOINTS | AUDIOD_SUBSCRIBE_ROUTES;
	serial = link_request(&seat, &subscribe, sizeof(subscribe), AUDIOD_SUBSCRIBE);
	result = link_result(&seat, serial);
	check(result == 0, "subscribed");

	/* The daemon's endpoints: a headset's media (dormant), a slow one, a phone's call pair, a headset's voice pair. */
	(void)link_open(&daemon, 2U);
	a2dp = add_endpoint(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_HEADSET_MEDIA, 1U, AUDIOD_ENDPOINT_DORMANT, 0U, "bt:a:a2dp");
	slow = add_endpoint(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_HEADSET_MEDIA, 2U, AUDIOD_ENDPOINT_DORMANT, 0U, "bt:b:a2dp");
	phone_out = add_endpoint(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_PHONE_CALL, 7U, AUDIOD_ENDPOINT_ACTIVE, 0U, "bt:p:call");
	phone_in = add_endpoint(&daemon, AUDIOD_KIND_SOURCE, AUDIOD_CLASS_PHONE_CALL, 7U, AUDIOD_ENDPOINT_ACTIVE, 0U, "bt:p:call");
	voice_out = add_endpoint(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_HEADSET_VOICE, 1U, AUDIOD_ENDPOINT_DORMANT, AUDIOD_ENDPOINT_FLAG_VOLUME,
	    "bt:a:voice");
	voice_in = add_endpoint(&daemon, AUDIOD_KIND_SOURCE, AUDIOD_CLASS_HEADSET_VOICE, 1U, AUDIOD_ENDPOINT_DORMANT, 0U, "bt:a:voice");
	check(a2dp >= AUDIOD_ENDPOINT_FIRST && slow > a2dp && phone_in > phone_out && voice_in > voice_out, "endpoints registered");
	got = link_take(&seat, AUDIOD_ENDPOINT_INFO, &info, sizeof(info), 1000);
	check(got && info.id == a2dp && info.endpoint_class == AUDIOD_CLASS_HEADSET_MEDIA, "the seat told of the endpoint");

	/* Now there is an output. */
	device = link_open(&player, 2U);
	check(device == 1, "WELCOME with an output");

	/* Classes: the phone's call fits no route; the headset's media is media's output. */
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, phone_out, 0U);
	check(result == EINVAL, "the phone's call is not media's output");
	result = route_set(&seat, AUDIOD_ROUTE_CALL, 0U, phone_in);
	check(result == EINVAL, "the phone's call is not the user's input");
	result = route_set(&seat, AUDIOD_ROUTE_CALL, a2dp, 0U);
	check(result == EINVAL, "a headset's media is not a call's output");
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, a2dp, 0U);
	check(result == 0, "media to the headset");
	got = wait_route(&seat, AUDIOD_ROUTE_MEDIA, a2dp, ANY, &route, 1000);
	check(got, "the seat told of the route");

	/* A media stream starts: the headset is wanted with ticket 1; a stale ACTIVE does not open it, the ticket's does. */
	memset(&create, 0, sizeof(create));
	create.direction = AUDIOD_PLAYBACK;
	create.format = AUDIOD_FORMAT_S16_LE;
	create.channels = 2U;
	create.rate = 48000U;
	create.header.stream = 1U;
	(void)link_request(&player, &create, sizeof(create), AUDIOD_STREAM_CREATE);
	got = link_take(&player, AUDIOD_STREAM_CREATED, &header, sizeof(header), 1000);
	check(got, "stream created");
	memset(&header, 0, sizeof(header));
	header.stream = 1U;
	serial = link_request(&player, &header, sizeof(header), AUDIOD_STREAM_START);
	result = link_result(&player, serial);
	check(result == 0, "stream started");
	got = wait_wanted(&daemon, a2dp, 1U, &wanted, 2000);
	check(got && wanted.ticket == 1U, "the headset wanted, ticket 1");
	memset(&state, 0, sizeof(state));
	state.id = a2dp;
	state.state = AUDIOD_ENDPOINT_ACTIVE;
	state.ticket = 1U;
	serial = link_request(&daemon, &state, sizeof(state), AUDIOD_ENDPOINT_STATE);
	result = link_result(&daemon, serial);
	check(result == 0, "the headset active for ticket 1");

	/* Opened in time: no TIMEOUT in 2.5 s. */
	got = wait_route(&seat, AUDIOD_ROUTE_MEDIA, ANY, AUDIOD_REASON_TIMEOUT, &route, 2500);
	check(!got, "no timeout once open");

	/* The slow headset never answers: TIMEOUT after 2 s, the route left on it (N1), closed with ticket 2. */
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, slow, 0U);
	check(result == 0, "media to the slow headset");
	got = wait_wanted(&daemon, slow, 1U, &wanted, 2000);
	check(got, "the slow headset wanted");
	got = wait_route(&seat, AUDIOD_ROUTE_MEDIA, slow, AUDIOD_REASON_TIMEOUT, &route, 3500);
	check(got, "TIMEOUT, the route kept");
	got = wait_wanted(&daemon, slow, 0U, &wanted, 1000);
	check(got, "the slow headset closed");

	/* Back to the open headset, then it goes away: media held (G5), the output none. */
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, a2dp, 0U);
	check(result == 0, "media back to the headset");
	memset(&state, 0, sizeof(state));
	state.id = a2dp;
	serial = link_request(&daemon, &state, sizeof(state), AUDIOD_ENDPOINT_REMOVE);
	result = link_result(&daemon, serial);
	check(result == 0, "the headset removed");
	got = link_take(&seat, AUDIOD_MEDIA_HELD, &held, sizeof(held), 1000);
	check(got && held.held == 1U && held.reason == AUDIOD_REASON_OUTPUT_LOST, "media held, output lost");
	serial = link_request(&seat, &header, sizeof(header), AUDIOD_ROUTE_LIST);
	got = link_find(&seat, AUDIOD_ROUTE_INFO, serial, &route, sizeof(route), 1000);
	check(got && route.route == AUDIOD_ROUTE_MEDIA && route.output == 0U && route.held == 1U, "the media route has no output");
	got = link_find(&seat, AUDIOD_ROUTE_INFO, serial, &route, sizeof(route), 1000);
	memset(&header, 0, sizeof(header));
	serial = link_request(&seat, &header, sizeof(header), AUDIOD_MEDIA_RELEASE);
	got = link_take(&seat, AUDIOD_MEDIA_HELD, &held, sizeof(held), 1000);
	check(got && held.held == 0U, "released");

	/* A call on the phone's pair, the user's side the headset's voice by default (G1); a second is EBUSY. */
	memset(&call, 0, sizeof(call));
	call.far_group = 7U;
	serial = link_request(&daemon, &call, sizeof(call), AUDIOD_CALL_SET);
	result = link_result(&daemon, serial);
	check(result == 0, "call set");
	serial = link_request(&daemon, &call, sizeof(call), AUDIOD_CALL_SET);
	result = link_result(&daemon, serial);
	check(result == EBUSY, "a second call is EBUSY");
	serial = link_request(&seat, &header, sizeof(header), AUDIOD_ROUTE_LIST);
	got = link_find(&seat, AUDIOD_ROUTE_INFO, serial, &route, sizeof(route), 1000);
	got = link_find(&seat, AUDIOD_ROUTE_INFO, serial, &route, sizeof(route), 1000);
	check(got && route.route == AUDIOD_ROUTE_CALL && route.active == 1U && route.output == voice_out && route.input == voice_in,
	    "the call on the headset's voice");

	/* The volume keys act on the call's output (N5): its daemon told, the seat told. */
	memset(&volume, 0, sizeof(volume));
	volume.left = 30U;
	volume.right = 30U;
	serial = link_request(&seat, &volume, sizeof(volume), AUDIOD_DEVICE_VOLUME);
	got = link_take(&daemon, AUDIOD_ENDPOINT_VOLUME, &endpoint_volume, sizeof(endpoint_volume), 1000);
	check(got && endpoint_volume.id == voice_out && endpoint_volume.left == 30U, "the headset's volume to its daemon");
	memset(&endpoint_volume, 0, sizeof(endpoint_volume));
	endpoint_volume.id = voice_out;
	endpoint_volume.left = 70U;
	endpoint_volume.right = 70U;
	serial = link_request(&daemon, &endpoint_volume, sizeof(endpoint_volume), AUDIOD_ENDPOINT_VOLUME_REPORT);
	result = link_result(&daemon, serial);
	check(result == 0, "the headset's buttons reported");
	got = 0;
	for (result = 0; result < 20 && !got; result++) {
		got = link_take(&seat, AUDIOD_VOLUME_CHANGED, &volume, sizeof(volume), 100);
		if (got && volume.left != 70U)
			got = 0;
	}

	/* The last one is the report's. */
	check(got, "the seat told the headset's volume");

	/* The daemon goes: its endpoints and its call with it. */
	(void)close(daemon.fd);
	got = 0;
	for (result = 0; result < 20 && !got; result++) {
		got = wait_route(&seat, AUDIOD_ROUTE_CALL, ANY, ANY, &route, 100);
		if (got && route.active != 0U)
			got = 0;
	}

	/* The call is no more. */
	check(got, "the call ended with its daemon");

	/* The summary. */
	if (failures != 0U) {
		printf("audiod-route: %u FAIL\n", failures);
		return 1;
	}

	/* Passed. */
	printf("audiod-route: all PASS\n");
	return 0;
}
