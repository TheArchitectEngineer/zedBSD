/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the daemons' connections on threads of their own
 * (libkeiland-backend-zedbsd's daemon-link-zedbsd.c with volume-zedbsd.c
 * and network-zedbsd.c, built with their sockets in the test's folder).
 *
 * It checks: the threads are made through the lent maker under their
 * names and taken off after the close; a publication wakes the lent wake
 * pipe; volumed not there (ENOTCONN), then its list, a mount and its
 * result, and its going; a volumed that never accepts (the backend's
 * connect waits in its thread) while the event loop's update and requests
 * return within 5 ms; networkd's watch, a join and a second request
 * refused (EBUSY), the join's answer through the update (DONE), and a
 * request that cannot be sent answered through the update (ENOENT), not
 * by the call.
 *
 * usage: host-daemon-link FOLDER   (a new folder for the sockets)
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include "userland/base/net/protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* The longest an event loop's call may take while a daemon stalls, in microseconds. */
#define TEST_CALL_MAX_US	5000U

/* What the pretend volumed lists. */
#define TEST_VOLUME_LIST	"VOLUME id=sda fs=fat label=STICK path=- size=1024 new=1\nDONE\n"

/* The checks run and passed. */
static int checks;
static int passed;

/* The wake pipe lent to the backend, and how many threads the lent maker made and took off. */
static int wake[2] = { -1, -1 };
static int made;
static int ended;
static pthread_mutex_t names_lock = PTHREAD_MUTEX_INITIALIZER;

/* The sockets' paths. */
static char volume_path[108];
static char network_path[108];

void kl_backend_threads(struct kl_backend_threads *threads);
static int lent_start(const char *name, void *(*run)(void *), void *argument, pthread_t *thread);
static void lent_ended(const char *name);
static void check(int ok, const char *what);
static uint64_t now_us(void);
static int listen_at(const char *path, int backlog);
static int wake_count(void);
static void pause_ms(unsigned milliseconds);
static void *volumed_main(void *argument);
static void *networkd_main(void *argument);
static void networkd_answer(int descriptor, const struct networkd_protocol_header *asked, const char *output);
static void test_volumes(void);
static void test_stall(void);
static void test_network(void);

/*
 * Lends the test's wake pipe and thread maker to the daemons' links, as
 * the compositor does through kl_backend_open.
 */
void
kl_backend_threads(
	struct kl_backend_threads *threads)
{
	/* The pipe and the maker. */
	memset(threads, 0, sizeof(*threads));
	threads->wake_descriptor = wake[1];
	threads->start = lent_start;
	threads->ended = lent_ended;
}

/* Runs the checks; returns 0 when all passed. */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* A folder for the sockets. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-daemon-link FOLDER\n");
		return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	(void)snprintf(volume_path, sizeof(volume_path), "%s/volumed.sock", argv[1]);
	(void)snprintf(network_path, sizeof(network_path), "%s/networkd.sock", argv[1]);

	/* The wake pipe, which never blocks. */
	error = pipe(wake);
	if (error != 0)
		return 2;
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);

	/* The three parts. */
	test_volumes();
	test_stall();
	test_network();

	/* Every thread made was taken off after its close. */
	check(made == 3 && ended == 3, "each link's thread made by the lent maker and taken off after the close");

	/* The outcome. */
	printf("host-daemon-link: %d/%d passed\n", passed, checks);
	if (passed != checks) {
		printf("host-daemon-link: FAIL\n");
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-daemon-link: PASS\n");
	return 0;
}

/* Makes a thread for a link, counting it. */
static int
lent_start(
	const char *name,
	void *(*run)(void *),
	void *argument,
	pthread_t *thread)
{
	int error;

	/* The thread. */
	error = pthread_create(thread, NULL, run, argument);
	if (error != 0)
		return error;

	/* Counted under its name. */
	pthread_mutex_lock(&names_lock);
	made++;
	pthread_mutex_unlock(&names_lock);
	printf("made %s\n", name);
	return 0;
}

/* Takes a link's thread off, counting it. */
static void
lent_ended(
	const char *name)
{
	/* Counted. */
	pthread_mutex_lock(&names_lock);
	ended++;
	pthread_mutex_unlock(&names_lock);
	printf("ended %s\n", name);
}

/* Counts one check. */
static void
check(
	int ok,
	const char *what)
{
	/* Told either way. */
	checks++;
	if (ok)
		passed++;
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
}

/* Gives the monotonic clock in microseconds. */
static uint64_t
now_us(
	void)
{
	struct timespec now;

	/* The clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Listens on a path (a new socket); returns the listener or -1. */
static int
listen_at(
	const char *path,
	int backlog)
{
	struct sockaddr_un address;
	int listener;
	int error;

	/* The socket at the path. */
	(void)unlink(path);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listener < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	(void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
	error = bind(listener, (struct sockaddr *)&address, sizeof(address));
	if (error == 0)
		error = listen(listener, backlog);
	if (error != 0) {
		close(listener);
		return -1;
	}

	/* Succeeded. */
	return listener;
}

/* Empties the wake pipe and says how many bytes it held. */
static int
wake_count(
	void)
{
	unsigned char bytes[64];
	ssize_t got;
	int count;

	/* Every byte. */
	count = 0;
	for (;;) {
		got = read(wake[0], bytes, sizeof(bytes));
		if (got <= 0)
			break;
		count += (int)got;
	}
	return count;
}

/* Waits some milliseconds. */
static void
pause_ms(
	unsigned milliseconds)
{
	struct timespec time;

	/* The whole wait. */
	time.tv_sec = (time_t)(milliseconds / 1000U);
	time.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&time, NULL);
}

/* A pretend volumed: one client, HELLO answered by a volume and DONE, MOUNT by RESULT 0, then closes when told. */
static void *
volumed_main(
	void *argument)
{
	char input[512];
	char reply[128];
	unsigned request;
	ssize_t got;
	size_t used;
	int listener;
	int client;
	int length;

	/* The listener the test made. */
	listener = *(int *)argument;
	client = accept(listener, NULL, NULL);
	if (client < 0)
		return NULL;

	/* Each line until the client goes. */
	used = 0U;
	for (;;) {
		got = recv(client, input + used, sizeof(input) - 1U - used, 0);
		if (got <= 0)
			break;
		used += (size_t)got;
		input[used] = '\0';
		if (strstr(input, "HELLO 1\n") != NULL) {
			(void)send(client, TEST_VOLUME_LIST, strlen(TEST_VOLUME_LIST), 0);
			used = 0U;
		}
		if (sscanf(input, "MOUNT %u sda", &request) == 1 && strchr(input, '\n') != NULL) {
			length = snprintf(reply, sizeof(reply), "RESULT %u 0\n", request);
			(void)send(client, reply, (size_t)length, 0);
			used = 0U;
		}
	}

	/* The client went. */
	close(client);
	return NULL;
}

/* volumed not there, then its list, a mount and its result, and the wake pipe. */
static void
test_volumes(void)
{
	struct kl_backend_volumes *volumes;
	struct kl_backend_volume list[4];
	pthread_t daemon;
	uint32_t request;
	uint32_t answered;
	unsigned changed;
	unsigned seen;
	char user[64];
	int listener;
	int error;
	int round;
	int got;
	size_t count;

	/* No volumed: followed, nothing listed, a mount refused at once. */
	(void)unlink(volume_path);
	volumes = kl_backend_volumes_open();
	check(volumes != NULL, "volumes: open without volumed");
	pause_ms(50U);
	(void)kl_backend_volumes_update(volumes, &changed);
	error = kl_backend_volumes_mount(volumes, "sda", &request);
	check(error == ENOTCONN, "volumes: a mount without volumed is refused (ENOTCONN)");

	/* volumed comes: the thread connects within its two seconds and the list arrives with a wake. */
	listener = listen_at(volume_path, 4);
	(void)pthread_create(&daemon, NULL, volumed_main, &listener);
	(void)wake_count();
	seen = 0U;
	for (round = 0; round < 400 && (seen & KL_BACKEND_VOLUMES_CHANGED_LIST) == 0U; round++) {
		pause_ms(10U);
		(void)kl_backend_volumes_update(volumes, &changed);
		seen |= changed;
	}
	count = kl_backend_volumes_get(volumes, list, 4U);
	check(count == 1U && strcmp(list[0].id, "sda") == 0 && strcmp(list[0].label, "STICK") == 0, "volumes: the list after volumed came");
	check(wake_count() > 0, "volumes: the publication woke the wake pipe");

	/* A mount, answered through the update. */
	error = kl_backend_volumes_mount(volumes, "sda", &request);
	check(error == 0, "volumes: a mount handed to the thread");
	got = 0;
	for (round = 0; round < 200 && !got; round++) {
		pause_ms(10U);
		(void)kl_backend_volumes_update(volumes, &changed);
		got = kl_backend_volumes_take_result(volumes, &answered, &error, user, sizeof(user));
	}
	check(got && answered == request && error == 0, "volumes: the mount's result");

	/* The close ends the thread; volumed sees its client go. */
	kl_backend_volumes_close(volumes);
	(void)pthread_join(daemon, NULL);
	close(listener);
	(void)unlink(volume_path);
}

/* A volumed that never accepts: the thread waits in connect while the event loop's calls return at once. */
static void
test_stall(void)
{
	struct kl_backend_volumes *volumes;
	struct sockaddr_un address;
	uint32_t request;
	uint64_t longest;
	uint64_t start;
	uint64_t took;
	unsigned changed;
	int fillers[8];
	int filled;
	int listener;
	int status;
	int index;
	int client;

	/* A listener whose backlog is full, so that a blocking connect waits. */
	listener = listen_at(volume_path, 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	(void)snprintf(address.sun_path, sizeof(address.sun_path), "%s", volume_path);
	filled = 0;
	for (index = 0; index < 8; index++) {
		fillers[index] = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
		status = connect(fillers[index], (struct sockaddr *)&address, sizeof(address));
		if (status != 0) {
			close(fillers[index]);
			break;
		}
		filled++;
	}
	check(filled > 0, "stall: the backlog filled");

	/* The event loop's calls while the thread waits in connect, for half a second. */
	volumes = kl_backend_volumes_open();
	longest = 0U;
	for (index = 0; index < 50; index++) {
		start = now_us();
		(void)kl_backend_volumes_update(volumes, &changed);
		(void)kl_backend_volumes_mount(volumes, "sda", &request);
		took = now_us() - start;
		if (took > longest)
			longest = took;
		pause_ms(10U);
	}
	printf("stall: the longest update and mount took %llu us\n", (unsigned long long)longest);
	check(longest < TEST_CALL_MAX_US, "stall: the event loop's calls return within 5 ms while volumed does not accept");

	/* The backlog drains: the thread's connect ends and the close can join it. */
	for (index = 0; index < filled + 1; index++) {
		client = accept(listener, NULL, NULL);
		if (client >= 0)
			close(client);
	}
	for (index = 0; index < filled; index++)
		close(fillers[index]);
	kl_backend_volumes_close(volumes);
	close(listener);
	(void)unlink(volume_path);
	check(1, "stall: the close joined the thread once volumed accepted");
}

/* Writes an answer frame: OK, no errno, and the text. */
static void
networkd_answer(
	int descriptor,
	const struct networkd_protocol_header *asked,
	const char *output)
{
	struct networkd_protocol_header header;
	struct networkd_field_writer writer;
	unsigned char payload[512];

	/* The fields. */
	networkd_field_writer_init(&writer, payload, sizeof(payload));
	(void)networkd_field_write_u32(&writer, NETWORKD_FIELD_STATUS, NETWORKD_RESULT_OK);
	(void)networkd_field_write_u32(&writer, NETWORKD_FIELD_ERROR, 0U);
	(void)networkd_field_write(&writer, NETWORKD_FIELD_OUTPUT, output, strlen(output));

	/* The frame, of the request's id and opcode. */
	header.request_id = asked->request_id;
	header.opcode = asked->opcode;
	header.payload_length = (uint32_t)writer.used;
	(void)networkd_protocol_write_frame(descriptor, &header, payload);
}

/* A pretend networkd: answers SUBSCRIBE with a state (and keeps the watch), a join after 300 ms; stops when the listener is shut. */
static void *
networkd_main(
	void *argument)
{
	struct networkd_protocol_header header;
	unsigned char payload[NETWORKD_REQUEST_MAX];
	int clients[8];
	int count;
	int listener;
	int client;
	int failed;
	int index;

	/* Each client until the listener is shut. */
	listener = *(int *)argument;
	count = 0;
	for (;;) {
		client = accept(listener, NULL, NULL);
		if (client < 0)
			break;
		failed = networkd_protocol_read_frame(client, &header, payload, sizeof(payload), NETWORKD_REQUEST_MAX);
		if (failed != 0) {
			close(client);
			continue;
		}
		if (header.opcode == NETWORKD_OP_WIFI_CONNECT)
			pause_ms(300U);
		networkd_answer(client, &header, "ne0 static online\n");
		if (count < 8)
			clients[count++] = client;
		else
			close(client);
	}

	/* The watches and answers go. */
	for (index = 0; index < count; index++)
		close(clients[index]);
	return NULL;
}

/* networkd's watch, a join, a second request refused, the join's answer, and a request that cannot be sent. */
static void
test_network(void)
{
	struct kl_backend_network_state state;
	struct kl_backend_network *network;
	pthread_t daemon;
	unsigned changed;
	unsigned seen;
	unsigned request;
	int listener;
	int error;
	int round;

	/* networkd there: the watch's state arrives. */
	listener = listen_at(network_path, 8);
	(void)pthread_create(&daemon, NULL, networkd_main, &listener);
	network = kl_backend_network_open();
	seen = 0U;
	for (round = 0; round < 200 && (seen & KL_BACKEND_NETWORK_CHANGED_STATE) == 0U; round++) {
		pause_ms(10U);
		(void)kl_backend_network_update(network, &changed);
		seen |= changed;
	}
	kl_backend_network_get_state(network, &state);
	check(state.reachable == 1U, "network: the watch's state");

	/* A join, and another request while it is outstanding. */
	error = kl_backend_network_request(network, KL_BACKEND_NETWORK_REQUEST_JOIN, "home");
	check(error == 0, "network: a join handed to the thread");
	error = kl_backend_network_request(network, KL_BACKEND_NETWORK_REQUEST_SCAN, NULL);
	check(error == EBUSY, "network: a second request while the join is outstanding (EBUSY)");
	request = kl_backend_network_get_request(network, &error);
	check(request == KL_BACKEND_NETWORK_REQUEST_JOIN, "network: the join outstanding");

	/* The join's answer through the update. */
	seen = 0U;
	for (round = 0; round < 300 && (seen & KL_BACKEND_NETWORK_CHANGED_DONE) == 0U; round++) {
		pause_ms(10U);
		(void)kl_backend_network_update(network, &changed);
		seen |= changed;
	}
	request = kl_backend_network_get_request(network, &error);
	check((seen & KL_BACKEND_NETWORK_CHANGED_DONE) != 0U && request == KL_BACKEND_NETWORK_REQUEST_JOIN && error == 0, "network: the join's answer (DONE, error 0)");

	/* networkd goes: a request that cannot be sent is answered through the update, not by the call. */
	shutdown(listener, SHUT_RDWR);
	close(listener);
	(void)pthread_join(daemon, NULL);
	(void)unlink(network_path);
	error = kl_backend_network_request(network, KL_BACKEND_NETWORK_REQUEST_DISCONNECT, NULL);
	check(error == 0, "network: a request handed over while networkd is gone");
	seen = 0U;
	for (round = 0; round < 200 && (seen & KL_BACKEND_NETWORK_CHANGED_DONE) == 0U; round++) {
		pause_ms(10U);
		(void)kl_backend_network_update(network, &changed);
		seen |= changed;
	}
	request = kl_backend_network_get_request(network, &error);
	check(request == KL_BACKEND_NETWORK_REQUEST_DISCONNECT && error == ENOENT, "network: its failure answered through the update (ENOENT)");

	/* The close ends the thread. */
	kl_backend_network_close(network);
}
