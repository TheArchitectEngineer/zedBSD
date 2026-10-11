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
 *   audiod-route-test SOCKET [denied | flow]
 *
 * "denied": the audiod was started with uids that are not this process's;
 * only the refusals are checked.  "flow" (ws209-p003): the sound itself,
 * this process playing the daemon's side of the rings in real time: media
 * to a headset's sink at 44.1 kHz (the tone, its level, the fill near its
 * target, played_position behind by the output's delay, SUSPENDED writes
 * nothing and the stream waits), and a call between a phone's 8 kHz pair
 * and a headset's 16 kHz voice pair (the far voice and the media at
 * -12 dB in the headset, the user's voice at the far side).  Prints a line for each failed check and
 * "audiod-route: all PASS" at the end.
 */

#include "userland/base/audiod/protocol.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

/* The most samples a flow check keeps. */
#define FLOW_KEEP	(48000U * 4U)

/*
 * One ring as the far side of it uses it (flow): the header and frames,
 * the frames, whether this side writes it, when its pace started and the
 * frames done since, the tone's phase, the samples kept (channel 0), and
 * the fill seen and the times a reader found it short after it first had
 * sound.
 */
struct ring {
	struct audiod_shm_header *shm;
	int16_t *frames;
	size_t bytes;
	uint32_t channels;
	uint32_t rate;
	uint32_t capacity;
	unsigned writer;
	double start;
	uint64_t done;
	double phase;
	float *kept;
	size_t kept_count;
	unsigned keeping;
	unsigned sounded;
	unsigned short_reads;
	uint64_t fill_min;
	uint64_t fill_max;
};

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
static double now_seconds(void);
static void link_pump(struct link *link);
static uint32_t add_ring(struct link *link, uint32_t kind, uint32_t endpoint_class, uint32_t group, uint32_t channels, uint32_t rate, uint32_t target, uint32_t flags, uint32_t latency_us, const char *key, struct ring *ring);
static int ring_map(struct ring *ring, struct link *link, size_t bytes, unsigned writer);
static int stream_ring(struct link *link, uint32_t stream, uint32_t direction, uint32_t role, struct ring *ring);
static void ring_produce(struct ring *ring, double now, double frequency, double amplitude, unsigned paced);
static void ring_consume(struct ring *ring, double now);
static void ring_keep(struct ring *ring, unsigned keeping);
static double tone_level(const struct ring *ring, double frequency);
static void flow_run(struct ring **producers, const double *tones, unsigned producer_count, struct ring **consumers, unsigned consumer_count, struct link **links, unsigned link_count, double seconds);
static int test_flow(void);

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
	int flow;
	int got;

	/* The socket, and the refusals only. */
	if (argc < 2)
		return 2;
	socket_path = argv[1];
	denied = argc >= 3;
	flow = 0;
	if (denied)
		flow = strcmp(argv[2], "flow") == 0;
	if (flow)
		return test_flow();
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

/* The monotonic clock in seconds. */
static double
now_seconds(
	void)
{
	struct timespec now;

	/* Read. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Takes what came on a connection and drops the events a flow does not wait for (all but WANTED and MEDIA_HELD). */
static void
link_pump(
	struct link *link)
{
	struct audiod_header header;
	struct pollfd wait;
	size_t offset;
	ssize_t got;
	int ready;

	/* What is there now. */
	for (;;) {
		wait.fd = link->fd;
		wait.events = POLLIN;
		ready = poll(&wait, 1, 0);
		if (ready <= 0 || link->used == sizeof(link->input))
			break;
		got = recv(link->fd, link->input + link->used, sizeof(link->input) - link->used, MSG_DONTWAIT);
		if (got <= 0)
			break;
		link->used += (size_t)got;
	}

	/* Whole messages not waited for, dropped. */
	offset = 0U;
	while (link->used - offset >= sizeof(header)) {
		memcpy(&header, link->input + offset, sizeof(header));
		if (header.length < sizeof(header) || link->used - offset < header.length)
			break;
		if (header.type == AUDIOD_ENDPOINT_WANTED || header.type == AUDIOD_MEDIA_HELD) {
			offset += header.length;
			continue;
		}

		/* Dropped. */
		memmove(link->input + offset, link->input + offset + header.length, link->used - offset - header.length);
		link->used -= header.length;
	}
}

/* Registers an endpoint with its ring mapped for this side; returns its id, 0 on a refusal. */
static uint32_t
add_ring(
	struct link *link,
	uint32_t kind,
	uint32_t endpoint_class,
	uint32_t group,
	uint32_t channels,
	uint32_t rate,
	uint32_t target,
	uint32_t flags,
	uint32_t latency_us,
	const char *key,
	struct ring *ring)
{
	struct audiod_endpoint_add add;
	struct audiod_endpoint_added added;
	int got;
	int mapped;

	/* A ring of 200 ms. */
	memset(&add, 0, sizeof(add));
	add.kind = kind;
	add.endpoint_class = endpoint_class;
	add.group = group;
	add.format = AUDIOD_FORMAT_S16_LE;
	add.channels = channels;
	add.rate = rate;
	add.buffer_frames = rate / 5U;
	add.target_frames = target;
	add.state = AUDIOD_ENDPOINT_ACTIVE;
	add.flags = flags;
	add.latency_us = latency_us;
	snprintf(add.key, sizeof(add.key), "%s", key);
	snprintf(add.name, sizeof(add.name), "flow %s", key);
	(void)link_request(link, &add, sizeof(add), AUDIOD_ENDPOINT_ADD);
	got = link_take(link, AUDIOD_ENDPOINT_ADDED, &added, sizeof(added), 1000);
	if (!got)
		return 0U;

	/* Its ring: this side writes a source and reads a sink. */
	mapped = ring_map(ring, link, added.shm_bytes, kind == AUDIOD_KIND_SOURCE);
	if (!mapped)
		return 0U;

	/* Succeeded: its id. */
	return added.id;
}

/* Maps the ring a connection was last handed. */
static int
ring_map(
	struct ring *ring,
	struct link *link,
	size_t bytes,
	unsigned writer)
{
	void *memory;

	/* The memory. */
	memset(ring, 0, sizeof(*ring));
	if (link->received_fd < 0)
		return 0;
	memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, link->received_fd, 0);
	(void)close(link->received_fd);
	link->received_fd = -1;
	if (memory == MAP_FAILED)
		return 0;

	/* The ring. */
	ring->shm = memory;
	ring->frames = (int16_t *)(void *)((uint8_t *)memory + AUDIOD_SHM_HEADER);
	ring->bytes = bytes;
	ring->channels = ring->shm->channels;
	ring->rate = ring->shm->rate;
	ring->capacity = ring->shm->capacity_frames;
	ring->writer = writer;
	ring->kept = calloc(FLOW_KEEP, sizeof(float));
	ring->fill_min = UINT64_MAX;

	/* Succeeded. */
	return ring->kept != NULL;
}

/* Makes and starts a version 2 stream (48 kHz stereo) of a direction and a role with its ring mapped. */
static int
stream_ring(
	struct link *link,
	uint32_t stream,
	uint32_t direction,
	uint32_t role,
	struct ring *ring)
{
	struct audiod_stream_create2 create;
	struct audiod_stream_created created;
	struct audiod_header header;
	uint32_t serial;
	int got;
	int result;

	/* Made. */
	memset(&create, 0, sizeof(create));
	create.header.stream = stream;
	create.direction = direction;
	create.format = AUDIOD_FORMAT_S16_LE;
	create.channels = 2U;
	create.rate = 48000U;
	create.buffer_frames = 9600U;
	create.role = role;
	(void)link_request(link, &create, sizeof(create), AUDIOD_STREAM_CREATE);
	got = link_take(link, AUDIOD_STREAM_CREATED, &created, sizeof(created), 1000);
	if (!got)
		return 0;
	got = ring_map(ring, link, created.shm_bytes, direction == AUDIOD_PLAYBACK);
	if (!got)
		return 0;

	/* Started. */
	memset(&header, 0, sizeof(header));
	header.stream = stream;
	serial = link_request(link, &header, sizeof(header), AUDIOD_STREAM_START);
	result = link_result(link, serial);

	/* Succeeded or not. */
	return result == 0;
}

/* Writes a tone into a ring: paced (a daemon's source, in real time) or as far as it has room (a player). */
static void
ring_produce(
	struct ring *ring,
	double now,
	double frequency,
	double amplitude,
	unsigned paced)
{
	uint64_t write_position;
	uint64_t read_position;
	uint64_t due;
	uint64_t room;
	uint64_t index;
	uint32_t channel;
	int16_t sample;

	/* The frames due and the room. */
	if (ring->start == 0.0)
		ring->start = now;
	write_position = ring->shm->write_position;
	read_position = audiod_position_load(&ring->shm->read_position, &ring->shm->read_sequence);
	room = ring->capacity - (write_position - read_position);
	due = room;
	if (paced) {
		due = (uint64_t)((now - ring->start) * (double)ring->rate) - ring->done;
		if (due > room)
			due = room;
	}

	/* The tone. */
	for (index = 0U; index < due; index++) {
		sample = (int16_t)lrint(amplitude * 32767.0 * sin(ring->phase));
		ring->phase += 2.0 * M_PI * frequency / (double)ring->rate;
		for (channel = 0U; channel < ring->channels; channel++)
			ring->frames[((write_position + index) % ring->capacity) * ring->channels + channel] = sample;
	}

	/* Published. */
	ring->done += due;
	audiod_position_store(&ring->shm->write_position, &ring->shm->write_sequence, write_position + due);
}

/* Reads a sink's ring in real time, keeping channel 0 while keeping, and noting its fill and short reads once it sounded. */
static void
ring_consume(
	struct ring *ring,
	double now)
{
	uint64_t write_position;
	uint64_t read_position;
	uint64_t available;
	uint64_t due;
	uint64_t index;
	int16_t sample;

	/* The frames due and those there. */
	if (ring->start == 0.0)
		ring->start = now;
	write_position = audiod_position_load(&ring->shm->write_position, &ring->shm->write_sequence);
	read_position = ring->shm->read_position;
	available = write_position - read_position;
	due = (uint64_t)((now - ring->start) * (double)ring->rate) - ring->done;
	if (available != 0U)
		ring->sounded = 1U;
	if (ring->keeping) {
		if (available < ring->fill_min)
			ring->fill_min = available;
		if (available > ring->fill_max)
			ring->fill_max = available;
		if (ring->sounded && available < due)
			ring->short_reads++;
	}

	/* Before it sounds the pace waits; then what is due and there. */
	if (!ring->sounded) {
		ring->done += due;
		return;
	}

	/* No more than is there. */
	if (due > available)
		due = available;
	for (index = 0U; index < due; index++) {
		sample = ring->frames[((read_position + index) % ring->capacity) * ring->channels];
		if (ring->keeping && ring->kept_count < FLOW_KEEP) {
			ring->kept[ring->kept_count] = (float)sample / 32768.0f;
			ring->kept_count++;
		}
	}

	/* Given back. */
	ring->done += due;
	audiod_position_store(&ring->shm->read_position, &ring->shm->read_sequence, read_position + due);
}

/* Starts or stops keeping a ring's samples and figures (starting clears them). */
static void
ring_keep(
	struct ring *ring,
	unsigned keeping)
{
	/* Cleared when starting. */
	if (keeping) {
		ring->kept_count = 0U;
		ring->short_reads = 0U;
		ring->fill_min = UINT64_MAX;
		ring->fill_max = 0U;
	}

	/* Kept or not. */
	ring->keeping = keeping;
}

/*
 * The amplitude of a tone in the kept samples: Goertzel over blocks of
 * 50 ms under a Hann window, averaged (the converter's correction moves the
 * pitch by up to 0.2%, which a single bin over seconds would miss).
 */
static double
tone_level(
	const struct ring *ring,
	double frequency)
{
	double coefficient;
	double window;
	double weights;
	double total;
	double s0;
	double s1;
	double s2;
	double power;
	size_t block;
	size_t blocks;
	size_t first;
	size_t index;

	/* None kept. */
	block = ring->rate / 20U;
	if (ring->kept_count < block)
		return 0.0;

	/* Each block. */
	coefficient = 2.0 * cos(2.0 * M_PI * frequency / (double)ring->rate);
	total = 0.0;
	blocks = ring->kept_count / block;
	for (first = 0U; first < blocks * block; first += block) {
		s1 = 0.0;
		s2 = 0.0;
		weights = 0.0;
		for (index = 0U; index < block; index++) {
			window = 0.5 - 0.5 * cos(2.0 * M_PI * (double)index / (double)block);
			weights += window;
			s0 = window * (double)ring->kept[first + index] + coefficient * s1 - s2;
			s2 = s1;
			s1 = s0;
		}

		/* The block's amplitude. */
		power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
		total += 2.0 * sqrt(power) / weights;
	}

	/* Succeeded: the average. */
	return total / (double)blocks;
}

/* Runs the rings for a while: producers paced (daemons' sources) or kept full (players, tone 0 > frequency < 0), consumers in real time. */
static void
flow_run(
	struct ring **producers,
	const double *tones,
	unsigned producer_count,
	struct ring **consumers,
	unsigned consumer_count,
	struct link **links,
	unsigned link_count,
	double seconds)
{
	double end;
	double now;
	unsigned index;

	/* Every 2 ms. */
	end = now_seconds() + seconds;
	for (now = now_seconds(); now < end; now = now_seconds()) {
		for (index = 0U; index < producer_count; index++)
			ring_produce(producers[index], now, fabs(tones[index * 2U]), tones[index * 2U + 1U], tones[index * 2U] > 0.0);
		for (index = 0U; index < consumer_count; index++)
			ring_consume(consumers[index], now);
		for (index = 0U; index < link_count; index++)
			link_pump(links[index]);
		usleep(2000);
	}
}

/*
 * The flow (ws209-p003): media to a headset's sink, then SUSPENDED, then
 * a call with its two legs and the media at -12 dB.
 */
static int
test_flow(
	void)
{
	struct link seat;
	struct link daemon;
	struct link player;
	struct ring sink;
	struct ring media;
	struct ring far_sink;
	struct ring far_source;
	struct ring voice_sink;
	struct ring voice_source;
	struct ring mic;
	struct ring recording;
	uint32_t mic_id;
	struct ring *producers[3];
	struct ring *consumers[3];
	struct link *links[3];
	double tones[6];
	struct audiod_subscribe subscribe;
	struct audiod_endpoint_state state;
	struct audiod_endpoint_wanted wanted;
	struct audiod_call call;
	uint64_t write_before;
	uint64_t read_before;
	uint64_t lag;
	uint64_t played;
	uint32_t sink_id;
	uint32_t serial;
	uint32_t again;
	double level;
	double other;
	int result;
	int got;

	/* The seat, the daemon with a headset's media sink (44.1 kHz, target 40 ms, its own volume, 50 ms late), the route. */
	(void)link_open(&seat, 2U);
	memset(&subscribe, 0, sizeof(subscribe));
	subscribe.mask = AUDIOD_SUBSCRIBE_ROUTES;
	serial = link_request(&seat, &subscribe, sizeof(subscribe), AUDIOD_SUBSCRIBE);
	(void)link_result(&seat, serial);
	(void)link_open(&daemon, 2U);
	sink_id = add_ring(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_HEADSET_MEDIA, 1U, 2U, 44100U, 1764U, AUDIOD_ENDPOINT_FLAG_VOLUME, 50000U,
	    "flow:a2dp", &sink);
	check(sink_id != 0U, "flow: the headset's sink");
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, sink_id, 0U);
	check(result == 0, "flow: media to the headset");

	/* A media stream of 1 kHz at 0.5 for 3 s, kept for the last 2. */
	(void)link_open(&player, 2U);
	got = stream_ring(&player, 1U, AUDIOD_PLAYBACK, AUDIOD_ROLE_MEDIA, &media);
	check(got, "flow: the media stream");
	if (sink_id == 0U || !got)
		return 1;
	producers[0] = &media;
	tones[0] = -1000.0;
	tones[1] = 0.5;
	consumers[0] = &sink;
	links[0] = &seat;
	links[1] = &daemon;
	links[2] = &player;
	flow_run(producers, tones, 1U, consumers, 1U, links, 3U, 1.0);
	ring_keep(&sink, 1U);
	flow_run(producers, tones, 1U, consumers, 1U, links, 3U, 2.0);
	level = tone_level(&sink, 1000.0);
	other = tone_level(&sink, 1500.0);
	printf("flow: media in the sink %.4f (1.5 kHz %.4f), fill %llu..%llu frames, short reads %u\n", level, other,
	    (unsigned long long)sink.fill_min, (unsigned long long)sink.fill_max, sink.short_reads);
	check(level > 0.47 && level < 0.53 && other < 0.005, "flow: the tone at its level in the headset");
	check(sink.short_reads == 0U, "flow: the headset never ran short");
	check(sink.fill_min + 441U >= 1764U && sink.fill_max <= 1764U + 941U + 441U, "flow: the fill near its target (M9)");

	/* played_position: behind what was read by the sink's fill and its 50 ms (N4). */
	for (;;) {
		serial = __atomic_load_n(&media.shm->played_sequence, __ATOMIC_ACQUIRE);
		played = media.shm->played_position;
		again = __atomic_load_n(&media.shm->played_sequence, __ATOMIC_ACQUIRE);
		if ((serial & 1U) == 0U && serial == again)
			break;
	}

	/* Behind what was read. */
	lag = audiod_position_load(&media.shm->read_position, &media.shm->read_sequence) - played;
	printf("flow: played_position %llu frames behind\n", (unsigned long long)lag);
	check(lag >= 2400U + 1500U && lag <= 2400U + 9600U + 1024U, "flow: played_position behind by the output's delay");

	/* SUSPENDED: nothing written, the stream waits (N20); WANTED again, its ticket's ACTIVE opens it. */
	memset(&state, 0, sizeof(state));
	state.id = sink_id;
	state.state = AUDIOD_ENDPOINT_SUSPENDED;
	serial = link_request(&daemon, &state, sizeof(state), AUDIOD_ENDPOINT_STATE);
	(void)link_result(&daemon, serial);
	write_before = sink.shm->write_position;
	read_before = media.shm->read_position;
	ring_keep(&sink, 0U);
	sink.sounded = 0U;
	flow_run(producers, tones, 1U, NULL, 0U, links, 2U, 0.3);
	check(sink.shm->write_position == write_before, "flow: nothing written while SUSPENDED");
	check(media.shm->read_position == read_before, "flow: the stream waits while SUSPENDED");
	got = wait_wanted(&daemon, sink_id, 1U, &wanted, 2000);
	check(got, "flow: the headset wanted again");
	state.state = AUDIOD_ENDPOINT_ACTIVE;
	state.ticket = wanted.ticket;
	serial = link_request(&daemon, &state, sizeof(state), AUDIOD_ENDPOINT_STATE);
	(void)link_result(&daemon, serial);
	audiod_position_store(&sink.shm->read_position, &sink.shm->read_sequence, sink.shm->write_position);
	sink.start = 0.0;
	sink.done = 0U;
	flow_run(producers, tones, 1U, consumers, 1U, links, 3U, 0.5);
	ring_keep(&sink, 1U);
	flow_run(producers, tones, 1U, consumers, 1U, links, 3U, 1.0);
	level = tone_level(&sink, 1000.0);
	check(level > 0.47 && level < 0.53 && sink.short_reads == 0U, "flow: the tone again after ACTIVE");

	/* The media input a headset's 16 kHz voice source: a recording stream gets its 500 Hz at 0.3. */
	mic_id = add_ring(&daemon, AUDIOD_KIND_SOURCE, AUDIOD_CLASS_HEADSET_VOICE, 3U, 1U, 16000U, 320U, 0U, 0U, "flow:mic", &mic);
	result = route_set(&seat, AUDIOD_ROUTE_MEDIA, sink_id, mic_id);
	check(mic_id != 0U && result == 0, "flow: media's input a headset's voice");
	got = stream_ring(&player, 2U, AUDIOD_CAPTURE, AUDIOD_ROLE_MEDIA, &recording);
	check(got, "flow: the recording stream");
	if (mic_id == 0U || !got)
		return 1;
	producers[1] = &mic;
	tones[2] = 500.0;
	tones[3] = 0.3;
	consumers[1] = &recording;
	flow_run(producers, tones, 2U, consumers, 2U, links, 3U, 0.5);
	ring_keep(&recording, 1U);
	flow_run(producers, tones, 2U, consumers, 2U, links, 3U, 1.0);
	level = tone_level(&recording, 500.0);
	printf("flow: recorded 500 Hz %.4f, short reads %u\n", level, recording.short_reads);
	check(level > 0.28 && level < 0.32, "flow: the recording from the headset's voice");

	/*
	 * A call: a phone's 8 kHz pair (target 20 ms, M8) and a headset's
	 * 16 kHz voice pair.  The far voice 400 Hz at 0.4, the user's 700 Hz
	 * at 0.3, the media 1 kHz at 0.5 under the call (-12 dB: 0.125).
	 */
	(void)add_ring(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_PHONE_CALL, 7U, 1U, 8000U, 160U, 0U, 0U, "flow:call", &far_sink);
	(void)add_ring(&daemon, AUDIOD_KIND_SOURCE, AUDIOD_CLASS_PHONE_CALL, 7U, 1U, 8000U, 160U, 0U, 0U, "flow:call", &far_source);
	(void)add_ring(&daemon, AUDIOD_KIND_SINK, AUDIOD_CLASS_HEADSET_VOICE, 1U, 1U, 16000U, 320U, AUDIOD_ENDPOINT_FLAG_VOLUME, 0U,
	    "flow:voice", &voice_sink);
	(void)add_ring(&daemon, AUDIOD_KIND_SOURCE, AUDIOD_CLASS_HEADSET_VOICE, 1U, 1U, 16000U, 320U, 0U, 0U, "flow:voice", &voice_source);
	check(far_sink.shm != NULL && far_source.shm != NULL && voice_sink.shm != NULL && voice_source.shm != NULL, "flow: the call's endpoints");
	if (far_sink.shm == NULL || far_source.shm == NULL || voice_sink.shm == NULL || voice_source.shm == NULL)
		return 1;
	memset(&call, 0, sizeof(call));
	call.far_group = 7U;
	serial = link_request(&daemon, &call, sizeof(call), AUDIOD_CALL_SET);
	result = link_result(&daemon, serial);
	check(result == 0, "flow: call set on the headset's voice");
	producers[1] = &far_source;
	tones[2] = 400.0;
	tones[3] = 0.4;
	producers[2] = &voice_source;
	tones[4] = 700.0;
	tones[5] = 0.3;
	consumers[0] = &voice_sink;
	consumers[1] = &far_sink;
	consumers[2] = &sink;
	flow_run(producers, tones, 3U, consumers, 3U, links, 3U, 1.0);
	ring_keep(&voice_sink, 1U);
	ring_keep(&far_sink, 1U);
	flow_run(producers, tones, 3U, consumers, 3U, links, 3U, 2.0);
	level = tone_level(&voice_sink, 400.0);
	other = tone_level(&voice_sink, 1000.0);
	printf("flow: headset voice 400 Hz %.4f, media 1 kHz %.4f, short reads %u\n", level, other, voice_sink.short_reads);
	check(level > 0.37 && level < 0.43, "flow: the far voice in the headset");
	check(other > 0.11 && other < 0.14, "flow: the media at -12 dB in the headset (Q13)");
	level = tone_level(&far_sink, 700.0);
	printf("flow: far side 700 Hz %.4f, fill %llu..%llu frames, short reads %u, written %llu\n", level,
	    (unsigned long long)far_sink.fill_min, (unsigned long long)far_sink.fill_max, far_sink.short_reads,
	    (unsigned long long)far_sink.shm->write_position);
	check(level > 0.27 && level < 0.33, "flow: the user's voice at the far side");
	check(voice_sink.short_reads == 0U && far_sink.short_reads == 0U, "flow: the call's sinks never ran short");

	/* The summary. */
	if (failures != 0U) {
		printf("audiod-route: flow %u FAIL\n", failures);
		return 1;
	}

	/* Passed. */
	printf("audiod-route: flow all PASS\n");
	return 0;
}
