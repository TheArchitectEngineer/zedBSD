/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws131-p010: the host test of Keiland's system extension, both ends.
 *
 * 1. libkeiland's view (system/system-view.c) alone: a state is seen only
 *    after its done, a scan and the details are whole lists, the results
 *    ring, the errno values of the results.
 * 2. The compositor's system.c alone, with a capture client: malformed
 *    requests end the client, out-of-range ones are answered invalid.
 * 3. Both ends over a socket pair: libkeiland's kl_system_* (with the
 *    host's libwayland-client) against the compositor's system.c, served
 *    by a small server thread that stands in for the compositor's event
 *    loop (wl_display's sync and get_registry, the registry's bind, then
 *    zwl_system_request and zwl_system_tick each pass).  The network
 *    daemon, the sound service, the power and the key store are fakes
 *    the test controls.
 *
 * Exit 0 when every check passes; each failure is printed.
 */

#include "userland/desktop/wayland/zwl.h"
#include "userland/desktop/keiland/kl-system-protocol.h"
#include "userland/desktop/libkeiland-backend/keiland-backend.h"
#include "userland/desktop/libkeiland/system/system-private.h"
#include "userland/desktop/libkeiland/system/system-protocol.h"

#include <keiland.h>
#include <wayland-client.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, ...) do { \
	if (!(condition)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ---------------------------------------------------------------- fakes */

/* What the fakes hold; lock guards all of it (the server thread, the key store's worker and the test touch it). */
static struct {
	pthread_mutex_t lock;
	struct kl_backend_network_state network;
	struct kl_backend_network_ap aps[4];
	size_t ap_count;
	unsigned outstanding;		/* the daemon's request out, NONE when none */
	unsigned bar_holds;		/* the outstanding one is the system bar's, held until released */
	unsigned auto_answer;		/* the server answers the extension's requests each pass */
	int answer_error;
	unsigned log[32];		/* the requests sent, in order */
	char log_ssid[32][KL_BACKEND_NETWORK_SSID_MAX];
	unsigned log_count;
	char saved_ssid[KL_BACKEND_NETWORK_SSID_MAX];
	char saved_key[KL_BACKEND_NETWORK_KEY_MAX + 1U];
	unsigned save_count;
	unsigned bar_done_count;
	int save_error;
	unsigned command_bar_key;
	unsigned command_bar_saved;
	int bar_key_error;
	unsigned bar_key_called;
	unsigned key_failed_count;
	int key_failed_error;
	size_t bar_saved_count;
	char bar_saved_first[KL_BACKEND_NETWORK_SSID_MAX];
	unsigned bar_saved_calls;
	unsigned scan_holders;		/* network.c's holders of the scans (ws089-p021) */
	struct kl_backend_audio_state audio;
	unsigned set_left;
	unsigned set_right;
	unsigned set_muted;
	unsigned set_count;
	unsigned feedback_count;
	struct kl_backend_power_state power;
	unsigned power_reads;
	unsigned action_count;
	/* The test's commands for the server thread's next pass. */
	unsigned command_hold_bar;
	unsigned command_release_bar;
	unsigned command_state;
	unsigned stop;
} world;

static int fake_watch;

struct kl_backend_network *
zwl_network_watch(void)
{
	return (struct kl_backend_network *)&fake_watch;
}

void
zwl_network_state(struct kl_backend_network_state *state)
{
	pthread_mutex_lock(&world.lock);
	*state = world.network;
	pthread_mutex_unlock(&world.lock);
}

size_t
zwl_network_scan(struct kl_backend_network_ap *aps, size_t capacity)
{
	size_t count;

	pthread_mutex_lock(&world.lock);
	count = world.ap_count < capacity ? world.ap_count : capacity;
	memcpy(aps, world.aps, count * sizeof(aps[0]));
	pthread_mutex_unlock(&world.lock);
	return count;
}

/* A wired configuration (ws089-p022): kept as a request of its own, the interface in place of the network, its fields remembered. */
static struct kl_backend_wired_config wired_seen;

int
kl_backend_network_configure_wired(struct kl_backend_network *network, const struct kl_backend_wired_config *config)
{
	int error;

	pthread_mutex_lock(&world.lock);
	wired_seen = *config;
	pthread_mutex_unlock(&world.lock);
	error = kl_backend_network_request(network, KL_BACKEND_NETWORK_REQUEST_WIRED, config->interface);
	return error;
}

int
kl_backend_network_request(struct kl_backend_network *network, unsigned request, const char *ssid)
{
	int error;

	(void)network;
	pthread_mutex_lock(&world.lock);
	error = 0;
	if (world.outstanding != KL_BACKEND_NETWORK_REQUEST_NONE) {
		error = EBUSY;
	} else {
		world.outstanding = request;
		if (world.log_count < 32U) {
			world.log[world.log_count] = request;
			snprintf(world.log_ssid[world.log_count], KL_BACKEND_NETWORK_SSID_MAX, "%s", ssid != NULL ? ssid : "");
			world.log_count++;
		}
	}
	pthread_mutex_unlock(&world.lock);
	return error;
}

int
kl_backend_network_save_key(const char *ssid, const char *key)
{
	int error;

	pthread_mutex_lock(&world.lock);
	snprintf(world.saved_ssid, sizeof(world.saved_ssid), "%s", ssid);
	snprintf(world.saved_key, sizeof(world.saved_key), "%s", key);
	world.save_count++;
	error = world.save_error;
	pthread_mutex_unlock(&world.lock);
	return error;
}

void
zwl_network_key_failed(struct zwl_server *server, const char *ssid, int error)
{
	(void)server;
	(void)ssid;
	pthread_mutex_lock(&world.lock);
	world.key_failed_count++;
	world.key_failed_error = error;
	pthread_mutex_unlock(&world.lock);
}

void
zwl_network_saved(struct zwl_server *server, char (*ssids)[KL_BACKEND_NETWORK_SSID_MAX], size_t count)
{
	(void)server;
	pthread_mutex_lock(&world.lock);
	world.bar_saved_calls++;
	world.bar_saved_count = count;
	if (count > 0U)
		snprintf(world.bar_saved_first, sizeof(world.bar_saved_first), "%s", ssids[0]);
	pthread_mutex_unlock(&world.lock);
}

/* The system bar's network details (ws099-p032): nothing to show here. */
void
zwl_network_details(struct zwl_server *server, const struct kl_backend_network_link *links, size_t link_count, const char (*dns)[KL_BACKEND_NETWORK_ADDRESS_MAX], size_t dns_count)
{
	(void)server;
	(void)links;
	(void)link_count;
	(void)dns;
	(void)dns_count;
}

/* The account (ws160-p002): "kei" is the current password, a new one shorter than 8 characters is refused. */
int
kl_backend_account_set_password(const char *current, const char *fresh)
{
	if (strcmp(current, "kei") != 0)
		return EACCES;
	if (strlen(fresh) < 8U)
		return EINVAL;
	return 0;
}

void
zwl_network_scan_hold(unsigned on)
{
	pthread_mutex_lock(&world.lock);
	if (on != 0U)
		world.scan_holders++;
	else if (world.scan_holders > 0U)
		world.scan_holders--;
	pthread_mutex_unlock(&world.lock);
}

static unsigned
scan_holders_now(void)
{
	unsigned holders;

	pthread_mutex_lock(&world.lock);
	holders = world.scan_holders;
	pthread_mutex_unlock(&world.lock);
	return holders;
}

size_t
kl_backend_network_get_links(struct kl_backend_network_link *links, size_t capacity)
{
	(void)capacity;
	memset(links, 0, 2U * sizeof(links[0]));
	snprintf(links[0].name, sizeof(links[0].name), "lo0");
	links[0].up = 1U;
	links[0].running = 1U;
	links[0].loopback = 1U;
	snprintf(links[0].address, sizeof(links[0].address), "127.0.0.1");
	snprintf(links[0].netmask, sizeof(links[0].netmask), "255.0.0.0");
	links[0].mtu = 16384U;
	snprintf(links[1].name, sizeof(links[1].name), "wlan0");
	links[1].up = 1U;
	snprintf(links[1].address, sizeof(links[1].address), "192.168.1.20");
	snprintf(links[1].netmask, sizeof(links[1].netmask), "255.255.255.0");
	links[1].hardware[0] = 0x02;
	links[1].hardware[5] = 0xab;
	links[1].mtu = 1500U;
	links[1].received_bytes = 0x123456789ULL;
	links[1].sent_bytes = 42U;
	return 2U;
}

size_t
kl_backend_network_get_dns(char (*servers)[KL_BACKEND_NETWORK_ADDRESS_MAX], size_t capacity)
{
	(void)capacity;
	snprintf(servers[0], KL_BACKEND_NETWORK_ADDRESS_MAX, "192.168.1.1");
	return 1U;
}

size_t
kl_backend_network_get_saved(char (*ssids)[KL_BACKEND_NETWORK_SSID_MAX], size_t capacity)
{
	(void)capacity;
	snprintf(ssids[0], KL_BACKEND_NETWORK_SSID_MAX, "Home");
	return 1U;
}

int
kl_backend_power_get_state(const struct kl_backend *backend, struct kl_backend_power_state *state)
{
	(void)backend;
	pthread_mutex_lock(&world.lock);
	*state = world.power;
	world.power_reads++;
	pthread_mutex_unlock(&world.lock);
	return 0;
}

int
kl_backend_power_action(struct kl_backend *backend, unsigned action)
{
	int error;

	(void)backend;
	pthread_mutex_lock(&world.lock);
	world.action_count++;
	error = ENOTSUP;
	if ((world.power.actions & KL_BACKEND_POWER_ACTION_BIT(action)) != 0U)
		error = 0;
	pthread_mutex_unlock(&world.lock);
	return error;
}

/*
 * The system monitor's backend (WS134, compositor's sysmon.c): none on this host, so the monitor's requests are
 * refused; this test does not exercise it, it only links the compositor's system.c that reaches it.
 */
struct kl_backend_monitor *
kl_backend_monitor_open(void)
{
	return NULL;
}

int
kl_backend_monitor_info(struct kl_backend_monitor *monitor, struct kl_backend_monitor_info *info)
{
	(void)monitor;
	(void)info;
	return ENOTSUP;
}

int
kl_backend_monitor_sample(struct kl_backend_monitor *monitor, struct kl_backend_monitor_sample *sample)
{
	(void)monitor;
	(void)sample;
	return ENOTSUP;
}

void
kl_backend_monitor_close(struct kl_backend_monitor *monitor)
{
	(void)monitor;
}

/* How far the test moved the compositor's clock ahead (ws089-p021: a minute passes at once). */
static uint64_t clock_ahead_ms;

/* The compositor's clock (main.c), for the monitor's tick and the scans' minute. */
uint64_t
zwl_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U + __atomic_load_n(&clock_ahead_ms, __ATOMIC_SEQ_CST);
}

void
zwl_volume_audio_state(struct kl_backend_audio_state *state)
{
	pthread_mutex_lock(&world.lock);
	*state = world.audio;
	pthread_mutex_unlock(&world.lock);
}

/* The removable media (media.c, ws132-p004): none here, and a request is not offered (as on a system without volumed). */
unsigned
zwl_media_tick(struct zwl_server *server)
{
	(void)server;
	return 0U;
}

size_t
zwl_media_volumes(struct kl_backend_volume *list, size_t capacity)
{
	(void)list;
	(void)capacity;
	return 0U;
}

int
zwl_media_ask(int mount, const char *id, uint32_t *request)
{
	(void)mount;
	(void)id;
	(void)request;
	return ENOTSUP;
}

int
zwl_media_take_result(uint32_t *request, int *error, char *user, size_t size)
{
	(void)request;
	(void)error;
	(void)user;
	(void)size;
	return 0;
}

int
zwl_volume_feedback(void)
{
	pthread_mutex_lock(&world.lock);
	world.feedback_count++;
	pthread_mutex_unlock(&world.lock);
	return 0;
}

int
zwl_volume_request_channels(struct zwl_server *server, unsigned left, unsigned right, unsigned muted)
{
	(void)server;
	pthread_mutex_lock(&world.lock);
	world.set_left = left;
	world.set_right = right;
	world.set_muted = muted;
	world.set_count++;
	pthread_mutex_unlock(&world.lock);
	return 0;
}

int
zwl_settings_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size)
{
	(void)object;
	(void)opcode;
	(void)bytes;
	(void)size;
	return EPROTO;
}

/* ------------------------------------------------- the compositor's objects */

static struct zwl_server server;
static struct zwl_client capture_client;
static struct zwl_client socket_client;

/* What a capture client was sent: the events, each its object, opcode and payload. */
static struct {
	uint32_t object;
	uint32_t opcode;
	unsigned char payload[512];
	size_t size;
} captured[64];
static unsigned captured_count;

static void emit_raw(int fd, uint32_t object, uint32_t opcode, const void *payload, size_t size);

int
zwl_emit(struct zwl_client *client, uint32_t object, uint32_t opcode, const void *payload, size_t size)
{
	if (client->fd < 0) {
		if (captured_count < 64U && size <= sizeof(captured[0].payload)) {
			captured[captured_count].object = object;
			captured[captured_count].opcode = opcode;
			if (size > 0U)
				memcpy(captured[captured_count].payload, payload, size);
			captured[captured_count].size = size;
			captured_count++;
		}
		return 0;
	}
	emit_raw(client->fd, object, opcode, payload, size);
	return 0;
}

struct zwl_object *
zwl_find(struct zwl_client *client, uint32_t id)
{
	struct zwl_object *object;

	for (object = client->objects; object != NULL; object = object->next) {
		if (object->id == id && !object->dead)
			return object;
	}
	return NULL;
}

struct zwl_object *
zwl_create(struct zwl_client *client, uint32_t id, enum zwl_kind kind, uint32_t version)
{
	struct zwl_object *object;

	if (id == 0U || zwl_find(client, id) != NULL)
		return NULL;
	object = calloc(1, sizeof(*object));
	if (object == NULL)
		return NULL;
	object->client = client;
	object->id = id;
	object->kind = kind;
	object->version = version;
	object->next = client->objects;
	client->objects = object;
	return object;
}

void
zwl_object_destroy(struct zwl_object *object)
{
	uint32_t id;

	if (object->kind == ZWL_SYSTEM_NETWORK)
		zwl_system_network_gone(object);
	object->dead = 1U;
	id = object->id;
	if (object->client->fd >= 0)
		emit_raw(object->client->fd, 1U, 1U, &id, sizeof(id));
}

static void
free_objects(struct zwl_client *client)
{
	struct zwl_object *object;

	while (client->objects != NULL) {
		object = client->objects;
		client->objects = object->next;
		free(object);
	}
}

/* ------------------------------------------------------------------ wire */

static void
emit_raw(int fd, uint32_t object, uint32_t opcode, const void *payload, size_t size)
{
	unsigned char message[600];
	uint32_t header[2];
	ssize_t written;

	header[0] = object;
	header[1] = (uint32_t)((size + 8U) << 16) | opcode;
	memcpy(message, header, sizeof(header));
	if (size > 0U)
		memcpy(message + 8, payload, size);
	written = write(fd, message, size + 8U);
	if (written != (ssize_t)(size + 8U))
		printf("FAIL emit_raw: write %zd\n", written);
}

static size_t
put_word(unsigned char *payload, size_t offset, uint32_t word)
{
	memcpy(payload + offset, &word, 4);
	return offset + 4U;
}

static size_t
put_string(unsigned char *payload, size_t offset, const char *text)
{
	uint32_t length;
	size_t padded;

	length = (uint32_t)strlen(text) + 1U;
	padded = (length + 3U) & ~3U;
	memcpy(payload + offset, &length, 4);
	memset(payload + offset + 4U, 0, padded);
	memcpy(payload + offset + 4U, text, length - 1U);
	return offset + 4U + padded;
}

static uint32_t
word_at(const unsigned char *bytes, size_t offset)
{
	uint32_t word;

	memcpy(&word, bytes + offset, 4);
	return word;
}

/* ---------------------------------------------------------- server thread */

static uint32_t registry_id;
static unsigned protocol_errors;

/* Carries out one request from the client, as the compositor's dispatch would. */
static void
serve_message(uint32_t id, uint32_t opcode, const unsigned char *bytes, size_t size)
{
	unsigned char payload[128];
	struct zwl_object *object;
	uint32_t serial;
	uint32_t length;
	size_t offset;
	int error;

	/* wl_display: sync, get_registry. */
	if (id == 1U) {
		if (opcode == 0U) {
			serial = 7U;
			emit_raw(socket_client.fd, word_at(bytes, 0), 0U, &serial, 4);
			serial = word_at(bytes, 0);
			emit_raw(socket_client.fd, 1U, 1U, &serial, 4);
		} else {
			registry_id = word_at(bytes, 0);
			offset = put_word(payload, 0U, 25U);
			offset = put_string(payload, offset, KL_SYSTEM_MANAGER_NAME);
			offset = put_word(payload, offset, KL_SYSTEM_MANAGER_VERSION);
			emit_raw(socket_client.fd, registry_id, 0U, payload, offset);
		}
		return;
	}

	/* wl_registry.bind(name, interface, version, new_id). */
	if (id == registry_id) {
		length = word_at(bytes, 4);
		offset = 8U + ((length + 3U) & ~3U);
		object = zwl_create(&socket_client, word_at(bytes, offset + 4U), ZWL_SYSTEM_MANAGER, word_at(bytes, offset));
		if (object == NULL || zwl_system_bind(object) != 0)
			protocol_errors++;
		return;
	}

	/* The extension's objects. */
	object = zwl_find(&socket_client, id);
	if (object == NULL) {
		printf("FAIL serve: no object %u (opcode %u)\n", id, opcode);
		protocol_errors++;
		return;
	}
	error = zwl_system_request(object, opcode, bytes, size);
	if (error != 0) {
		printf("FAIL serve: object %u opcode %u error %d\n", id, opcode, error);
		protocol_errors++;
	}
}

/* One pass of the stand-in event loop: the test's commands, the fake daemon's answers, the extension's tick. */
static void
serve_pass(void)
{
	struct kl_backend_network_state state;
	unsigned bar_key;
	unsigned bar_saved;
	unsigned answer;
	unsigned release;
	unsigned changed;
	int error;
	int owned;

	/* The test's commands to the system bar's side (called outside the lock: the fakes take it). */
	pthread_mutex_lock(&world.lock);
	bar_key = world.command_bar_key;
	world.command_bar_key = 0U;
	bar_saved = world.command_bar_saved;
	world.command_bar_saved = 0U;
	pthread_mutex_unlock(&world.lock);
	if (bar_key) {
		error = zwl_system_bar_save_key(&server, "Bar", "barpass1");
		pthread_mutex_lock(&world.lock);
		world.bar_key_error = error;
		world.bar_key_called = 1U;
		pthread_mutex_unlock(&world.lock);
	}
	if (bar_saved)
		zwl_system_bar_saved(&server);

	/* The test's commands. */
	pthread_mutex_lock(&world.lock);
	if (world.command_hold_bar) {
		world.command_hold_bar = 0U;
		if (world.outstanding == KL_BACKEND_NETWORK_REQUEST_NONE) {
			world.outstanding = KL_BACKEND_NETWORK_REQUEST_WIFI_ON;
			world.bar_holds = 1U;
		}
	}
	release = world.command_release_bar;
	world.command_release_bar = 0U;
	changed = world.command_state;
	world.command_state = 0U;
	if (changed != 0U) {
		snprintf(world.network.ssid, sizeof(world.network.ssid), "Cafe");
		world.ap_count = 1U;
	}
	state = world.network;
	(void)state;

	/* The daemon answers the extension's request, or the released bar's. */
	answer = KL_BACKEND_NETWORK_REQUEST_NONE;
	error = 0;
	if (world.outstanding != KL_BACKEND_NETWORK_REQUEST_NONE) {
		if (world.bar_holds && release) {
			answer = world.outstanding;
			world.bar_holds = 0U;
		} else if (!world.bar_holds && world.auto_answer) {
			answer = world.outstanding;
			error = world.answer_error;
		}
		if (answer != KL_BACKEND_NETWORK_REQUEST_NONE)
			world.outstanding = KL_BACKEND_NETWORK_REQUEST_NONE;
	}
	pthread_mutex_unlock(&world.lock);

	/* As network.c's tick does: the state and scan told, then the answer offered to the extension. */
	if (changed != 0U)
		zwl_system_network_changed(&server, KL_BACKEND_NETWORK_CHANGED_STATE | KL_BACKEND_NETWORK_CHANGED_SCAN);
	if (answer != KL_BACKEND_NETWORK_REQUEST_NONE) {
		owned = zwl_system_network_done(&server, answer, error);
		if (!owned) {
			pthread_mutex_lock(&world.lock);
			world.bar_done_count++;
			pthread_mutex_unlock(&world.lock);
		}
	}

	/* The extension's own pass. */
	zwl_system_tick(&server);
}

static void *
serve(void *argument)
{
	unsigned char buffer[65536];
	struct pollfd descriptor;
	size_t filled;
	size_t at;
	ssize_t got;
	uint32_t size;
	unsigned stop;

	(void)argument;
	filled = 0U;
	for (;;) {
		pthread_mutex_lock(&world.lock);
		stop = world.stop;
		pthread_mutex_unlock(&world.lock);
		if (stop)
			break;

		descriptor.fd = socket_client.fd;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		if (poll(&descriptor, 1, 5) > 0 && (descriptor.revents & POLLIN) != 0) {
			got = read(socket_client.fd, buffer + filled, sizeof(buffer) - filled);
			if (got <= 0)
				break;
			filled += (size_t)got;
			at = 0U;
			while (filled - at >= 8U) {
				size = word_at(buffer, at + 4U) >> 16;
				if (size < 8U || filled - at < size)
					break;
				serve_message(word_at(buffer, at), word_at(buffer, at + 4U) & 0xffffU, buffer + at + 8U, size - 8U);
				at += size;
			}
			memmove(buffer, buffer + at, filled - at);
			filled -= at;
		}
		serve_pass();
	}
	return NULL;
}

/* ----------------------------------------------------------- client side */

/* Reads and dispatches until the predicate holds, or for rounds rounds (5 ms apart); returns the bits seen. */
static unsigned
pump(struct wl_display *display, struct kl_system *system, int (*until)(struct kl_system *system, unsigned seen), int rounds)
{
	struct timespec pause;
	unsigned seen;
	unsigned changed;
	int round;

	seen = 0U;
	pause.tv_sec = 0;
	pause.tv_nsec = 5000000;
	for (round = 0; round < rounds; round++) {
		if (wl_display_roundtrip(display) < 0)
			break;
		if (kl_system_dispatch(system, &changed) != 0)
			break;
		seen |= changed;
		if (until != NULL && until(system, seen))
			break;
		nanosleep(&pause, NULL);
	}
	return seen;
}

static int
until_result(struct kl_system *system, unsigned seen)
{
	(void)system;
	return (seen & KL_SYSTEM_CHANGED_RESULT) != 0U;
}

static int
until_power(struct kl_system *system, unsigned seen)
{
	(void)system;
	return (seen & KL_SYSTEM_CHANGED_POWER) != 0U;
}

static int
until_audio(struct kl_system *system, unsigned seen)
{
	(void)system;
	return (seen & KL_SYSTEM_CHANGED_AUDIO) != 0U;
}

static int
until_network(struct kl_system *system, unsigned seen)
{
	(void)system;
	return (seen & KL_SYSTEM_CHANGED_NETWORK) != 0U;
}

static unsigned
outstanding_now(void)
{
	unsigned outstanding;

	pthread_mutex_lock(&world.lock);
	outstanding = world.outstanding;
	pthread_mutex_unlock(&world.lock);
	return outstanding;
}

/* Waits for a result and checks its number and error. */
static void
expect_result(struct wl_display *display, struct kl_system *system, uint32_t request, int error, const char *what)
{
	uint32_t got;
	int got_error;
	int taken;

	taken = kl_system_take_result(system, &got, &got_error);
	if (!taken) {
		(void)pump(display, system, until_result, 400);
		taken = kl_system_take_result(system, &got, &got_error);
	}
	CHECK(taken == 1, "%s: no result", what);
	if (taken)
		CHECK(got == request && got_error == error, "%s: result request=%u error=%d, wanted %u %d", what, got, got_error, request, error);
}

/* ------------------------------------------------------------- the tests */

static void
test_view(void)
{
	struct system_view view;
	struct kl_network_state state;
	struct kl_network_ap ap;
	struct kl_network_link link;
	uint32_t request;
	unsigned index;
	int error;

	system_view_init(&view);
	CHECK(view.power.percent == -1, "view: power unknown");

	/* A state is seen only after its done. */
	memset(&state, 0, sizeof(state));
	state.reachable = 1U;
	snprintf(state.ssid, sizeof(state.ssid), "Home");
	system_view_network_state(&view, &state);
	CHECK(view.network.reachable == 0U, "view: state seen before its done");
	system_view_network_done(&view);
	CHECK(view.network.reachable == 1U && strcmp(view.network.ssid, "Home") == 0, "view: state after done");
	CHECK(system_view_take_changed(&view) == KL_SYSTEM_CHANGED_NETWORK, "view: changed network");

	/* The same state again is no change. */
	system_view_network_state(&view, &state);
	system_view_network_done(&view);
	CHECK(system_view_take_changed(&view) == 0U, "view: same state told as a change");

	/* A scan is a whole list, replaced by the next; an empty scan empties it. */
	memset(&ap, 0, sizeof(ap));
	snprintf(ap.ssid, sizeof(ap.ssid), "A");
	system_view_access_point(&view, &ap);
	snprintf(ap.ssid, sizeof(ap.ssid), "B");
	system_view_access_point(&view, &ap);
	system_view_scan_done(&view);
	CHECK(view.scan_count == 0U, "view: scan seen before done");
	system_view_network_done(&view);
	CHECK(view.scan_count == 2U, "view: scan of two");
	snprintf(ap.ssid, sizeof(ap.ssid), "C");
	system_view_access_point(&view, &ap);
	system_view_scan_done(&view);
	system_view_network_done(&view);
	CHECK(view.scan_count == 1U && strcmp(view.scan[0].ssid, "C") == 0, "view: second scan replaces");
	system_view_scan_done(&view);
	system_view_network_done(&view);
	CHECK(view.scan_count == 0U, "view: empty scan");
	(void)system_view_take_changed(&view);

	/* The details: whole, replaced by the next. */
	memset(&link, 0, sizeof(link));
	snprintf(link.name, sizeof(link.name), "lo0");
	system_view_link(&view, &link);
	system_view_dns(&view, "1.1.1.1");
	system_view_saved(&view, "Home");
	CHECK(view.link_count == 0U, "view: details before their end");
	system_view_details_done(&view);
	CHECK(view.link_count == 1U && view.dns_count == 1U && view.saved_count == 1U, "view: details");
	system_view_dns(&view, "9.9.9.9");
	system_view_details_done(&view);
	CHECK(view.link_count == 0U && view.dns_count == 1U && strcmp(view.dns[0], "9.9.9.9") == 0, "view: details replaced");
	CHECK(system_view_take_changed(&view) == KL_SYSTEM_CHANGED_DETAILS, "view: changed details");

	/* The results: in order, the oldest dropped when full, errno values. */
	for (index = 0; index < SYSTEM_VIEW_RESULTS + 2U; index++)
		system_view_result(&view, index + 1U, KL_SYSTEM_RESULT_BUSY);
	CHECK(system_view_take_result(&view, &request, &error) == 1 && request == 3U && error == EBUSY, "view: ring drops the oldest");
	while (system_view_take_result(&view, &request, &error) == 1)
		;
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_OK) == 0, "view: ok");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_DENIED) == EPERM, "view: denied");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_UNSUPPORTED) == ENOTSUP, "view: unsupported");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_INVALID) == EINVAL, "view: invalid");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_UNAVAILABLE) == ENODEV, "view: unavailable");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_NOT_SAVED) == EIO, "view: not saved");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_NO_KEY) == ENOENT, "view: no key");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_REFUSED) == EACCES, "view: refused");
	CHECK(system_view_error_of(KL_SYSTEM_RESULT_UNREACHABLE) == ENETUNREACH, "view: unreachable");
}

/* The last captured result's applied value, or UINT32_MAX without one. */
static uint32_t
captured_applied(uint32_t opcode)
{
	unsigned index;

	for (index = captured_count; index > 0U; index--) {
		if (captured[index - 1U].opcode == opcode && captured[index - 1U].size == 12U)
			return word_at(captured[index - 1U].payload, 4U);
	}
	return UINT32_MAX;
}

static void
test_server_alone(void)
{
	unsigned char bytes[64];
	struct zwl_object *manager;
	struct zwl_object *network;
	struct zwl_object *audio;
	struct zwl_object *power;
	size_t size;
	uint32_t word;

	capture_client.fd = -1;
	capture_client.server = &server;
	capture_client.number = 1U;
	manager = zwl_create(&capture_client, 2U, ZWL_SYSTEM_MANAGER, 1U);
	CHECK(zwl_system_bind(manager) == 0 && captured_count == 1U && word_at(captured[0].payload, 0) == 0x1fU, "alone: capabilities");

	/* A get without its new ID, and an unknown opcode, are malformed. */
	CHECK(zwl_system_request(manager, KL_SYSTEM_MANAGER_GET_NETWORK, bytes, 0U) == EPROTO, "alone: get without an ID");
	CHECK(zwl_system_request(manager, 9U, bytes, 0U) == EPROTO, "alone: unknown opcode");

	/* The objects. */
	word = 3U;
	CHECK(zwl_system_request(manager, KL_SYSTEM_MANAGER_GET_NETWORK, (unsigned char *)&word, 4U) == 0, "alone: network");
	CHECK(zwl_system_request(manager, KL_SYSTEM_MANAGER_GET_NETWORK, (unsigned char *)&word, 4U) == EPROTO, "alone: ID taken");
	word = 4U;
	CHECK(zwl_system_request(manager, KL_SYSTEM_MANAGER_GET_AUDIO, (unsigned char *)&word, 4U) == 0, "alone: audio");
	word = 5U;
	CHECK(zwl_system_request(manager, KL_SYSTEM_MANAGER_GET_POWER, (unsigned char *)&word, 4U) == 0, "alone: power");
	network = zwl_find(&capture_client, 3U);
	audio = zwl_find(&capture_client, 4U);
	power = zwl_find(&capture_client, 5U);
	CHECK(network != NULL && audio != NULL && power != NULL, "alone: objects made");
	if (network == NULL || audio == NULL || power == NULL)
		return;

	/* A request with an unknown what is answered invalid; one with a string without its NUL is malformed. */
	size = put_word(bytes, 0U, 11U);
	size = put_word(bytes, size, 9U);
	size = put_string(bytes, size, "");
	CHECK(zwl_system_request(network, KL_SYSTEM_NETWORK_REQUEST, bytes, size) == 0, "alone: unknown what accepted");
	CHECK(captured_applied(KL_SYSTEM_NETWORK_EVENT_RESULT) == KL_SYSTEM_RESULT_INVALID, "alone: unknown what invalid");
	size = put_word(bytes, 0U, 12U);
	size = put_word(bytes, size, KL_SYSTEM_NETWORK_SCAN);
	size = put_word(bytes, size, 4U);
	memcpy(bytes + size, "abcd", 4);
	size += 4U;
	CHECK(zwl_system_request(network, KL_SYSTEM_NETWORK_REQUEST, bytes, size) == EPROTO, "alone: string without NUL");

	/* A short key is invalid; a join without a network is invalid. */
	size = put_word(bytes, 0U, 13U);
	size = put_string(bytes, size, "Cafe");
	size = put_string(bytes, size, "short");
	CHECK(zwl_system_request(network, KL_SYSTEM_NETWORK_SAVE_KEY, bytes, size) == 0, "alone: short key accepted");
	CHECK(captured_applied(KL_SYSTEM_NETWORK_EVENT_RESULT) == KL_SYSTEM_RESULT_INVALID, "alone: short key invalid");
	size = put_word(bytes, 0U, 14U);
	size = put_word(bytes, size, KL_SYSTEM_NETWORK_JOIN);
	size = put_string(bytes, size, "");
	CHECK(zwl_system_request(network, KL_SYSTEM_NETWORK_REQUEST, bytes, size) == 0, "alone: join accepted");
	CHECK(captured_applied(KL_SYSTEM_NETWORK_EVENT_RESULT) == KL_SYSTEM_RESULT_INVALID, "alone: join without a network invalid");

	/* A volume over 100 and an unknown action are invalid; a short set is malformed. */
	size = put_word(bytes, 0U, 15U);
	size = put_word(bytes, size, 101U);
	size = put_word(bytes, size, 0U);
	size = put_word(bytes, size, 0U);
	CHECK(zwl_system_request(audio, KL_SYSTEM_AUDIO_SET_VOLUME, bytes, size) == 0, "alone: loud accepted");
	CHECK(captured_applied(KL_SYSTEM_AUDIO_EVENT_RESULT) == KL_SYSTEM_RESULT_INVALID, "alone: volume 101 invalid");
	CHECK(zwl_system_request(audio, KL_SYSTEM_AUDIO_SET_VOLUME, bytes, 12U) == EPROTO, "alone: short set");
	size = put_word(bytes, 0U, 16U);
	size = put_word(bytes, size, 7U);
	CHECK(zwl_system_request(power, KL_SYSTEM_POWER_ACTION, bytes, size) == 0, "alone: action accepted");
	CHECK(captured_applied(KL_SYSTEM_POWER_EVENT_RESULT) == KL_SYSTEM_RESULT_INVALID, "alone: action 7 invalid");

	/* No network work was left waiting. */
	CHECK(outstanding_now() == KL_BACKEND_NETWORK_REQUEST_NONE, "alone: nothing sent to the daemon");
	zwl_system_close(&server);
	free_objects(&capture_client);
	server.clients = NULL;
}

static void
test_both_ends(void)
{
	struct wl_display *display;
	struct kl_system *system;
	struct kl_network_state state;
	struct kl_network_ap aps[8];
	struct kl_network_link links[4];
	char dns[4][KL_NETWORK_ADDRESS_MAX];
	char saved[4][KL_NETWORK_SSID_MAX];
	struct kl_audio_state audio;
	struct kl_power_state power;
	struct kl_device devices[2];
	pthread_t thread;
	uint32_t first;
	uint32_t second;
	struct kl_network_wired_config wired;
	uint32_t request;
	unsigned seen;
	int taken_error;
	int sockets[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0, "socketpair");
	socket_client.fd = sockets[0];
	socket_client.server = &server;
	socket_client.number = 2U;
	server.clients = &socket_client;
	pthread_mutex_lock(&world.lock);
	world.auto_answer = 1U;
	pthread_mutex_unlock(&world.lock);
	CHECK(pthread_create(&thread, NULL, serve, NULL) == 0, "server thread");
	display = wl_display_connect_to_fd(sockets[1]);
	CHECK(display != NULL, "connect");
	if (display == NULL)
		return;

	/* Open: the capabilities and the first state of each object. */
	system = kl_system_open(display);
	CHECK(system != NULL, "open: errno %d", errno);
	if (system == NULL)
		return;
	/* The library's table describes the version it binds (zedBSD's libwayland refuses more than the table; T1-144). */
	CHECK(kl_system_manager_v1_interface.version == (int)KL_SYSTEM_MANAGER_VERSION, "manager table version %d", kl_system_manager_v1_interface.version);
	CHECK(kl_system_capabilities(system) == (KL_SYSTEM_HAS_NETWORK | KL_SYSTEM_HAS_AUDIO | KL_SYSTEM_HAS_POWER | KL_SYSTEM_HAS_DEVICES | KL_SYSTEM_HAS_MONITOR | KL_SYSTEM_HAS_ACCOUNT), "capabilities");
	kl_system_network_get_state(system, &state);
	CHECK(state.reachable == 1U && state.connected == 1U && state.kind == KL_NETWORK_WIFI && state.wifi == KL_WIFI_CONNECTED, "first network state");
	CHECK(strcmp(state.interface, "wlan0") == 0 && strcmp(state.ssid, "Home") == 0 && state.wired[0] == '\0', "first network names");
	CHECK(kl_system_network_get_scan(system, aps, 8U) == 2U && strcmp(aps[1].ssid, "Cafe") == 0 && aps[1].rssi == -70 && aps[1].secured == 1U, "first scan");
	kl_system_audio_get_state(system, &audio);
	CHECK(audio.reachable == 1U && audio.left == 70U && audio.right == 60U && audio.channels == 2U && audio.rate == 48000U, "first sound");
	CHECK(kl_system_devices_get(system, devices, 2U) == 0U, "no device");

	/* The power comes from its thread, after the first state. */
	kl_system_power_get_state(system, &power);
	if (power.percent != 80)
		(void)pump(display, system, until_power, 400);
	kl_system_power_get_state(system, &power);
	CHECK(power.source == KL_POWER_SOURCE_AC && power.percent == 80 && power.actions == KL_POWER_ACTION_BIT(KL_POWER_SUSPEND), "power read by its thread");

	/* A scan, answered by the daemon. */
	CHECK(kl_system_network_request(system, KL_NETWORK_SCAN, NULL, &first) == 0, "scan asked");
	expect_result(display, system, first, 0, "scan");

	/* Scans asked for and no longer (ws089-p021): the object is one holder however often it asks. */
	CHECK(kl_system_network_set_scanning(system, 1U) == 0, "scanning asked");
	CHECK(kl_system_network_set_scanning(system, 1U) == 0, "scanning asked again");
	(void)pump(display, system, NULL, 3);
	CHECK(scan_holders_now() == 1U, "one holder (%u)", scan_holders_now());
	CHECK(kl_system_network_set_scanning(system, 0U) == 0, "scanning no longer");
	(void)pump(display, system, NULL, 3);
	CHECK(scan_holders_now() == 0U, "no holder (%u)", scan_holders_now());
	CHECK(kl_system_network_set_scanning(system, 1U) == 0, "scanning asked, then not again");
	(void)pump(display, system, NULL, 3);
	CHECK(scan_holders_now() == 1U, "holder before its minute (%u)", scan_holders_now());
	__atomic_add_fetch(&clock_ahead_ms, 61000U, __ATOMIC_SEQ_CST);
	(void)pump(display, system, NULL, 5);
	CHECK(scan_holders_now() == 0U, "an asking not renewed for a minute ends (%u)", scan_holders_now());
	CHECK(kl_system_network_set_scanning(system, 1U) == 0, "scanning asked until the close");
	(void)pump(display, system, NULL, 3);
	CHECK(scan_holders_now() == 1U, "holder until the close (%u)", scan_holders_now());

	/* One network request at a time: the second is busy while the first is out. */
	pthread_mutex_lock(&world.lock);
	world.auto_answer = 0U;
	pthread_mutex_unlock(&world.lock);
	CHECK(kl_system_network_request(system, KL_NETWORK_JOIN, "Home", &first) == 0, "join asked");
	(void)pump(display, system, NULL, 3);
	CHECK(outstanding_now() == KL_BACKEND_NETWORK_REQUEST_JOIN, "join out");
	CHECK(kl_system_network_request(system, KL_NETWORK_DISCONNECT, NULL, &second) == 0, "disconnect asked");
	expect_result(display, system, second, EBUSY, "busy while a join is out");
	pthread_mutex_lock(&world.lock);
	world.auto_answer = 1U;
	world.answer_error = ENOENT;
	pthread_mutex_unlock(&world.lock);
	expect_result(display, system, first, ENOENT, "join without a key");
	pthread_mutex_lock(&world.lock);
	world.answer_error = 0;
	pthread_mutex_unlock(&world.lock);

	/* A wired configuration (ws089-p022): every field reaches the backend, and the daemon's answer is the result. */
	memset(&wired, 0, sizeof(wired));
	snprintf(wired.interface, sizeof(wired.interface), "%s", "em0");
	wired.mode = KL_WIRED_STATIC;
	snprintf(wired.address, sizeof(wired.address), "%s", "192.168.7.20");
	snprintf(wired.netmask, sizeof(wired.netmask), "%s", "255.255.255.0");
	snprintf(wired.router, sizeof(wired.router), "%s", "192.168.7.1");
	snprintf(wired.dns[0], sizeof(wired.dns[0]), "%s", "192.168.7.53");
	CHECK(kl_system_network_configure_wired(system, &wired, &first) == 0, "wired asked");
	expect_result(display, system, first, 0, "wired applied");

	/* What the backend saw, then a refusal as the daemon gives a non-member. */
	pthread_mutex_lock(&world.lock);
	CHECK(wired_seen.mode == KL_BACKEND_WIRED_STATIC && strcmp(wired_seen.interface, "em0") == 0 && strcmp(wired_seen.address, "192.168.7.20") == 0 &&
	    strcmp(wired_seen.netmask, "255.255.255.0") == 0 && strcmp(wired_seen.router, "192.168.7.1") == 0 && strcmp(wired_seen.dns[0], "192.168.7.53") == 0 &&
	    wired_seen.dns[1][0] == '\0', "wired fields reach the backend");
	world.answer_error = EACCES;
	pthread_mutex_unlock(&world.lock);
	wired.mode = KL_WIRED_DHCP;
	CHECK(kl_system_network_configure_wired(system, &wired, &first) == 0, "wired DHCP asked");
	expect_result(display, system, first, EPERM, "wired refused to a non-member");
	pthread_mutex_lock(&world.lock);
	world.answer_error = 0;
	pthread_mutex_unlock(&world.lock);
	wired.mode = 7U;
	CHECK(kl_system_network_configure_wired(system, &wired, NULL) == EINVAL, "wired mode unknown");

	/* The client checks what it can. */
	CHECK(kl_system_network_request(system, KL_NETWORK_JOIN, "", NULL) == EINVAL, "join without a network");
	CHECK(kl_system_network_save_key(system, "Cafe", "short", NULL) == EINVAL, "short key");
	CHECK(kl_system_audio_set_volume(system, 101U, 0U, 0U, NULL) == EINVAL, "loud");
	CHECK(kl_system_power_action(system, 9U, NULL) == EINVAL, "unknown action");

	/* A key saved, the daemon told, the network joined: one result at the end. */
	pthread_mutex_lock(&world.lock);
	world.log_count = 0U;
	pthread_mutex_unlock(&world.lock);
	CHECK(kl_system_network_save_key(system, "Cafe", "password1", &first) == 0, "save key asked");
	expect_result(display, system, first, 0, "save key");
	pthread_mutex_lock(&world.lock);
	CHECK(world.save_count == 1U && strcmp(world.saved_ssid, "Cafe") == 0 && strcmp(world.saved_key, "password1") == 0, "key saved by the store");
	CHECK(world.log_count == 2U && world.log[0] == KL_BACKEND_NETWORK_REQUEST_PROFILES && world.log[1] == KL_BACKEND_NETWORK_REQUEST_JOIN && strcmp(world.log_ssid[1], "Cafe") == 0, "profiles then join (%u requests)", world.log_count);
	world.log_count = 0U;
	pthread_mutex_unlock(&world.lock);

	/* The same with the system bar's request out: the step waits for it and goes after it. */
	pthread_mutex_lock(&world.lock);
	world.command_hold_bar = 1U;
	pthread_mutex_unlock(&world.lock);
	(void)pump(display, system, NULL, 10);
	CHECK(outstanding_now() == KL_BACKEND_NETWORK_REQUEST_WIFI_ON, "bar's request out");
	CHECK(kl_system_network_save_key(system, "Cafe", "password2", &first) == 0, "save key behind the bar asked");
	(void)pump(display, system, NULL, 10);
	CHECK(kl_system_take_result(system, &request, &taken_error) == 0, "no result while the bar holds the daemon");
	pthread_mutex_lock(&world.lock);
	CHECK(world.save_count == 2U && world.log_count == 0U, "saved, nothing sent behind the bar");
	world.command_release_bar = 1U;
	pthread_mutex_unlock(&world.lock);
	expect_result(display, system, first, 0, "save key after the bar");
	pthread_mutex_lock(&world.lock);
	CHECK(world.bar_done_count == 1U, "the bar's answer left to the bar");
	CHECK(world.log_count == 2U && world.log[0] == KL_BACKEND_NETWORK_REQUEST_PROFILES && world.log[1] == KL_BACKEND_NETWORK_REQUEST_JOIN, "profiles then join after the bar");
	pthread_mutex_unlock(&world.lock);

	/* The details, from the network's thread, before their result. */
	CHECK(kl_system_network_query_details(system, &first) == 0, "details asked");
	seen = pump(display, system, until_result, 400);
	CHECK((seen & KL_SYSTEM_CHANGED_DETAILS) != 0U, "details changed");
	expect_result(display, system, first, 0, "details");
	CHECK(kl_system_network_get_links(system, links, 4U) == 2U, "two links");
	CHECK(strcmp(links[0].name, "lo0") == 0 && links[0].up == 1U && links[0].loopback == 1U && links[0].mtu == 16384U, "loopback link");
	CHECK(strcmp(links[1].address, "192.168.1.20") == 0 && strcmp(links[1].hardware, "02:00:00:00:00:ab") == 0, "wlan link");
	CHECK(links[1].received_bytes == 0x123456789ULL && links[1].sent_bytes == 42U && links[1].running == 0U, "link counters");
	CHECK(kl_system_network_get_dns(system, dns, 4U) == 1U && strcmp(dns[0], "192.168.1.1") == 0, "dns");
	CHECK(kl_system_network_get_saved(system, saved, 4U) == 1U && strcmp(saved[0], "Home") == 0, "saved");

	/* The details alongside a request of the daemon's: answered while the scan is still out. */
	pthread_mutex_lock(&world.lock);
	world.auto_answer = 0U;
	pthread_mutex_unlock(&world.lock);
	CHECK(kl_system_network_request(system, KL_NETWORK_SCAN, NULL, &second) == 0, "scan held asked");
	(void)pump(display, system, NULL, 3);
	CHECK(outstanding_now() == KL_BACKEND_NETWORK_REQUEST_SCAN, "scan out");
	CHECK(kl_system_network_query_details(system, &first) == 0, "details beside the scan asked");
	expect_result(display, system, first, 0, "details beside the scan");
	pthread_mutex_lock(&world.lock);
	world.auto_answer = 1U;
	pthread_mutex_unlock(&world.lock);
	expect_result(display, system, second, 0, "scan after the details");

	/* The system bar's key: saved by the thread, told, joined; the join's answer is the bar's. */
	pthread_mutex_lock(&world.lock);
	world.log_count = 0U;
	world.bar_done_count = 0U;
	world.command_bar_key = 1U;
	pthread_mutex_unlock(&world.lock);
	(void)pump(display, system, NULL, 20);
	pthread_mutex_lock(&world.lock);
	CHECK(world.bar_key_called == 1U && world.bar_key_error == 0, "bar key handed (error %d)", world.bar_key_error);
	CHECK(world.save_count == 3U && strcmp(world.saved_ssid, "Bar") == 0, "bar key saved by the store");
	CHECK(world.log_count == 2U && world.log[0] == KL_BACKEND_NETWORK_REQUEST_PROFILES && world.log[1] == KL_BACKEND_NETWORK_REQUEST_JOIN && strcmp(world.log_ssid[1], "Bar") == 0, "bar: profiles then join");
	CHECK(world.bar_done_count == 1U, "bar: the join's answer is the bar's");
	world.save_error = EIO;
	world.command_bar_key = 1U;
	pthread_mutex_unlock(&world.lock);
	(void)pump(display, system, NULL, 20);
	pthread_mutex_lock(&world.lock);
	CHECK(world.key_failed_count == 1U && world.key_failed_error == EIO, "bar: a key not saved is told");
	world.save_error = 0;

	/* The system bar's saved networks. */
	world.command_bar_saved = 1U;
	pthread_mutex_unlock(&world.lock);
	(void)pump(display, system, NULL, 20);
	pthread_mutex_lock(&world.lock);
	CHECK(world.bar_saved_calls == 1U && world.bar_saved_count == 1U && strcmp(world.bar_saved_first, "Home") == 0, "bar: saved networks");
	pthread_mutex_unlock(&world.lock);

	/* A new state from the daemon. */
	pthread_mutex_lock(&world.lock);
	world.command_state = 1U;
	pthread_mutex_unlock(&world.lock);
	seen = pump(display, system, until_network, 400);
	kl_system_network_get_state(system, &state);
	CHECK((seen & KL_SYSTEM_CHANGED_NETWORK) != 0U && strcmp(state.ssid, "Cafe") == 0, "new network state");
	CHECK(kl_system_network_get_scan(system, aps, 8U) == 1U, "new scan");

	/* The sound: a set, the feedback, and a change from the service. */
	CHECK(kl_system_audio_set_volume(system, 30U, 40U, 1U, &first) == 0, "volume asked");
	expect_result(display, system, first, 0, "volume");
	pthread_mutex_lock(&world.lock);
	CHECK(world.set_left == 30U && world.set_right == 40U && world.set_muted == 1U, "volume set");
	world.audio.left = 55U;
	pthread_mutex_unlock(&world.lock);
	(void)pump(display, system, until_audio, 400);
	kl_system_audio_get_state(system, &audio);
	CHECK(audio.left == 55U, "sound changed");
	CHECK(kl_system_audio_feedback(system, &first) == 0, "feedback asked");
	expect_result(display, system, first, 0, "feedback");

	/* The power: an action not offered, and one offered. */
	CHECK(kl_system_power_action(system, KL_POWER_REBOOT, &first) == 0, "reboot asked");
	expect_result(display, system, first, ENOTSUP, "reboot not offered");
	CHECK(kl_system_power_action(system, KL_POWER_SUSPEND, &first) == 0, "suspend asked");
	expect_result(display, system, first, 0, "suspend");

	/* The devices: none can be ejected yet. */
	CHECK(kl_system_devices_eject(system, "usb0", &first) == 0, "eject asked");
	expect_result(display, system, first, ENOTSUP, "eject");
	CHECK(kl_system_devices_mount(system, "usb0", &first) == 0, "mount asked (version 5, ws132-p004)");
	expect_result(display, system, first, ENOTSUP, "mount");

	/* The account (ws160-p002): changed, the current password wrong, the new one refused, two at once busy. */
	CHECK(kl_system_account_set_password(system, "kei", "newpass123", &first) == 0, "password change asked");
	expect_result(display, system, first, 0, "password changed");
	CHECK(kl_system_account_set_password(system, "wrong", "newpass123", &first) == 0, "wrong current asked");
	expect_result(display, system, first, EPERM, "the current password wrong");
	CHECK(kl_system_account_set_password(system, "kei", "short", &first) == 0, "short new asked");
	expect_result(display, system, first, EINVAL, "the new password refused");
	CHECK(kl_system_account_set_password(system, "", "newpass123", &first) == EINVAL, "an empty password is refused by the library");

	/* Close, and no protocol error on the way; the closed system's asking for scans went with it. */
	kl_system_close(system);
	(void)wl_display_roundtrip(display);
	CHECK(protocol_errors == 0U, "%u protocol errors", protocol_errors);
	CHECK(scan_holders_now() == 0U, "no holder after the close (%u)", scan_holders_now());
	pthread_mutex_lock(&world.lock);
	world.stop = 1U;
	pthread_mutex_unlock(&world.lock);
	pthread_join(thread, NULL);
	wl_display_disconnect(display);
	zwl_system_close(&server);
	close(sockets[0]);
	free_objects(&socket_client);
}

int
main(void)
{
	pthread_mutex_init(&world.lock, NULL);
	world.network.reachable = 1U;
	world.network.connected = 1U;
	world.network.kind = KL_BACKEND_NETWORK_WIFI;
	snprintf(world.network.interface, sizeof(world.network.interface), "wlan0");
	world.network.wifi = KL_BACKEND_WIFI_CONNECTED;
	snprintf(world.network.wifi_interface, sizeof(world.network.wifi_interface), "wlan0");
	snprintf(world.network.ssid, sizeof(world.network.ssid), "Home");
	snprintf(world.aps[0].ssid, sizeof(world.aps[0].ssid), "Home");
	world.aps[0].rssi = -40;
	snprintf(world.aps[1].ssid, sizeof(world.aps[1].ssid), "Cafe");
	world.aps[1].rssi = -70;
	world.aps[1].secured = 1U;
	world.ap_count = 2U;
	world.audio.reachable = 1U;
	world.audio.device = 1U;
	world.audio.rate = 48000U;
	world.audio.channels = 2U;
	world.audio.left = 70U;
	world.audio.right = 60U;
	world.power.source = KL_BACKEND_POWER_SOURCE_AC;
	world.power.percent = 80;
	world.power.actions = KL_BACKEND_POWER_ACTION_BIT(KL_BACKEND_POWER_SUSPEND);

	test_view();
	test_server_alone();
	test_both_ends();

	if (failures != 0) {
		printf("host-system: %d FAILED\n", failures);
		return 1;
	}
	printf("host-system: PASS\n");
	return 0;
}
