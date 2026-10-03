/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Keiland's system extension in the compositor (WS131 p010, plan/ws131/
 * design.md section 4; keiland/kl-system-protocol.h): the manager, and the
 * network, the sound, the power and the devices it gives clients.  The
 * settings are settings.c's.
 *
 * The compositor holds the system: the network through network.c's watch
 * (one request at a time, the system bar's included), the sound through
 * volume.c's link to the sound service, the power through
 * libkeiland-backend.  A client asks; the compositor carries it out, tells
 * every object of the kind the change, and answers the asking object with
 * one result.
 *
 * Nothing here waits on the disk, the network daemon or the system bus in
 * the event loop (WS131 review 7): a key saved and the network's details
 * (the interfaces, the DNS servers, the saved networks, read from the
 * kernel and files) are done by a thread of the network's, and the power's
 * state (logind's answers on Linux) by a thread of the power's, each one
 * job at a time.  The system bar saves its keys and learns the saved
 * networks through the same thread (WS131 p011).  A key is never written
 * to the log.
 *
 * A key saved is joined in three steps within the one request (design.md
 * section 4.1 item 4): the key is saved, the network daemon is told the
 * saved networks changed, and the network is joined; the client hears one
 * result, at the end.  A step the system bar's request holds up is sent
 * again on the next pass.
 *
 * The details are no request of the daemon's: they are read whenever the
 * thread is free (a key waiting to be saved goes first), and every object
 * that asked meanwhile hears the same reading.
 */

#include "zwl.h"

#include "userland/desktop/keiland/kl-system-protocol.h"
#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A parameter a function does not use. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* The longest string a request may carry, with its NUL; a longer one is malformed. */
#define SYSTEM_WIRE_TEXT_MAX	4096U

/* The largest event payload this file sends. */
#define SYSTEM_EVENT_MAX	512U

/* The most objects waiting for the details at once; one more is answered busy. */
#define SYSTEM_DETAILS_WAITING	8U

/* The network work waiting (the stage of struct system_network_wait). */
#define SYSTEM_NETWORK_IDLE	0U
#define SYSTEM_NETWORK_REQUEST	1U	/* a request of the daemon's, waiting for its answer */
#define SYSTEM_NETWORK_QUEUED	2U	/* a key waits for the network's thread to be free */
#define SYSTEM_NETWORK_SAVING	3U	/* the network's thread saves the key */
#define SYSTEM_NETWORK_PROFILES	4U	/* the daemon is told the saved networks changed */
#define SYSTEM_NETWORK_JOINING	5U	/* the network of the saved key is joined */
#define SYSTEM_NETWORK_RETRY	6U	/* a step the system bar's request held up, sent again next pass */

/* The jobs of the threads. */
#define SYSTEM_JOB_SAVE_KEY	1U
#define SYSTEM_JOB_DETAILS	2U
#define SYSTEM_JOB_POWER	3U

/*
 * One job of a thread: what it is, its inputs (the network and its key,
 * wiped after the job; the backend for the power), and its outputs.  lock
 * guards done, which the thread sets last; the event loop reads the
 * outputs only after it saw done and joined the thread.  started is the
 * event loop's alone.
 */
struct system_job {
	pthread_t thread;
	pthread_mutex_t lock;
	unsigned lock_ready;
	unsigned kind;
	struct kl_backend *backend;
	char ssid[KL_BACKEND_NETWORK_SSID_MAX];
	char key[KL_BACKEND_NETWORK_KEY_MAX + 1U];
	int error;
	struct kl_backend_network_link links[KL_BACKEND_NETWORK_LINKS_MAX];
	size_t link_count;
	char dns[KL_BACKEND_NETWORK_DNS_MAX][KL_BACKEND_NETWORK_ADDRESS_MAX];
	size_t dns_count;
	char saved[KL_BACKEND_NETWORK_SCAN_MAX][KL_BACKEND_NETWORK_SSID_MAX];
	size_t saved_count;
	struct kl_backend_power_state power;
	unsigned done;
	unsigned started;
};

/*
 * The network work waiting: the stage, the daemon's request it waits on
 * (retry: the one to send again), who asked -- the system bar (bar), or a
 * client by its number, its object (0 once the object went) and the
 * request's number -- the network, and while queued its key (wiped when
 * the thread takes it).
 */
struct system_network_wait {
	unsigned stage;
	unsigned request;
	unsigned bar;
	uint64_t client;
	uint32_t object;
	uint32_t number;
	char ssid[KL_BACKEND_NETWORK_SSID_MAX];
	char key[KL_BACKEND_NETWORK_KEY_MAX + 1U];
};

/* An object waiting for the details: its client by number, its ID and the request's number. */
struct system_details_wait {
	uint64_t client;
	uint32_t object;
	uint32_t number;
};

/*
 * The extension's state that is no object's:
 *
 *   - the network work waiting, and what of the network is being told
 *     (network.c's changed bits, during zwl_system_network_changed only);
 *   - the objects waiting for the details (waiting, counted by
 *     details_count, bar for the system bar's saved networks) and those
 *     the reading under way answers (serving, serving_count, serving_bar);
 *   - the two threads' jobs;
 *   - what the objects were last told of the sound and the power (so that
 *     only a change is told): power_started once the first read of the
 *     power began, power_read once one ended (a power object made before
 *     then hears its first state at that end);
 *   - the serial of the last done.
 *
 * One per process; only the event loop's thread touches it, but the jobs'
 * fields their comment names.
 */
struct system_state {
	struct system_network_wait wait;
	unsigned network_changed;
	struct system_details_wait details[SYSTEM_DETAILS_WAITING];
	unsigned details_count;
	unsigned details_bar;
	struct system_details_wait serving[SYSTEM_DETAILS_WAITING];
	unsigned serving_count;
	unsigned serving_bar;
	struct system_job network_job;
	struct system_job power_job;
	struct kl_backend_audio_state audio;
	unsigned audio_told;
	struct kl_backend_power_state power;
	unsigned power_started;
	unsigned power_read;
	uint32_t serial;
};

/* The one state of the process, zero until the first use; the event loop's thread's (see struct system_state). */
static struct system_state system_state;

static int system_manager_request(struct zwl_object *manager, uint32_t opcode, const unsigned char *bytes, size_t size);
static int system_network_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
static int system_audio_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
static int system_power_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
static int system_devices_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
static uint32_t system_network_send(struct zwl_object *object, uint32_t number, uint32_t what, const char *ssid);
static int system_network_save_key(struct zwl_server *server, uint64_t client, uint32_t object, uint32_t number, unsigned bar, const char *ssid, const char *key);
static uint32_t system_network_details(struct zwl_object *object, uint32_t number);
static void system_network_step(struct zwl_server *server, unsigned request);
static void system_network_snapshot(struct zwl_object *object);
static void system_network_change(struct zwl_object *object);
static void system_network_state(struct zwl_object *object);
static void system_network_scan(struct zwl_object *object);
static void system_network_details_send(struct zwl_object *object);
static void system_network_finish(struct zwl_server *server, int error);
static void system_network_job_take(struct zwl_server *server);
static void system_network_job_next(struct zwl_server *server);
static void system_details_take(struct zwl_server *server);
static void system_power_job_take(struct zwl_server *server);
static void system_power_read(struct zwl_server *server);
static int system_job_finished(struct system_job *job);
static int system_job_start(struct system_job *job, unsigned kind, const char *ssid, const char *key);
static void system_job_wait(struct system_job *job);
static void *system_job_run(void *argument);
static void system_audio_state(struct zwl_object *object);
static void system_power_state(struct zwl_object *object);
static void system_tell(struct zwl_server *server, enum zwl_kind kind, void (*tell)(struct zwl_object *object), uint32_t done_opcode);
static struct zwl_object *system_network_object(struct zwl_server *server, uint64_t number, uint32_t id);
static void system_result(struct zwl_object *object, uint32_t opcode, uint32_t number, uint32_t applied);
static void system_done(struct zwl_object *object, uint32_t opcode);
static uint32_t system_result_of(int error);
static uint32_t system_network_result_of(int error);
static unsigned system_network_what(uint32_t what);
static void system_wipe(char *text, size_t size);
static size_t system_put_word(unsigned char *payload, size_t offset, uint32_t word);
static size_t system_put_string(unsigned char *payload, size_t offset, const char *text);
static int system_read_string(const unsigned char *bytes, size_t size, size_t offset, char **text, size_t *next);
static uint32_t system_word(const unsigned char *bytes, size_t offset);

/*
 * Tells a newly bound system manager what it offers: the settings, the
 * network, the sound, the power and the devices' frame.
 */
int
zwl_system_bind(
	struct zwl_object *manager)
{
	uint32_t bits;
	int error;

	/* Every part version 1 has. */
	bits = KL_SYSTEM_CAPABILITY_SETTINGS |
	    KL_SYSTEM_CAPABILITY_NETWORK |
	    KL_SYSTEM_CAPABILITY_AUDIO |
	    KL_SYSTEM_CAPABILITY_POWER |
	    KL_SYSTEM_CAPABILITY_DEVICES;

	/* The monitor, to a manager bound at version 2 (WS134 p012). */
	if (manager->version >= 2U)
		bits |= KL_SYSTEM_CAPABILITY_MONITOR;
	error = zwl_emit(manager->client, manager->id, KL_SYSTEM_MANAGER_EVENT_CAPABILITIES, &bits, sizeof(bits));
	if (error != 0)
		return error;

	/* Succeeded: the client knows the capabilities. */
	return 0;
}

/*
 * Carries out a request of the manager, or of a network, sound, power or
 * devices object (a settings object's are settings.c's).
 */
int
zwl_system_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* Each kind of the extension's objects. */
	switch (object->kind) {
	case ZWL_SYSTEM_MANAGER:
		error = system_manager_request(object, opcode, bytes, size);
		break;
	case ZWL_SYSTEM_NETWORK:
		error = system_network_request(object, opcode, bytes, size);
		break;
	case ZWL_SYSTEM_AUDIO:
		error = system_audio_request(object, opcode, bytes, size);
		break;
	case ZWL_SYSTEM_POWER:
		error = system_power_request(object, opcode, bytes, size);
		break;
	case ZWL_SYSTEM_DEVICES:
		error = system_devices_request(object, opcode, bytes, size);
		break;
	default:
		error = EPROTO;
		break;
	}

	/* A malformed request ends the client. */
	if (error != 0)
		return error;

	/* Succeeded: the request is carried out, or answered. */
	return 0;
}

/*
 * Looks after the extension once a pass: the power read at the start, the
 * threads' finished jobs and the next ones, a network step held up, and
 * the sound told when it changed.
 */
void
zwl_system_tick(
	struct zwl_server *server)
{
	struct kl_backend_audio_state audio;
	int differs;

	/* The power is read once at the start, so that the clients find its state known. */
	if (!system_state.power_started) {
		system_state.power_started = 1U;
		system_power_read(server);
	}

	/* The threads' jobs, once they are done, and the network thread's next job. */
	system_network_job_take(server);
	system_power_job_take(server);
	system_network_job_next(server);

	/* A step of a saved key the system bar's request held up. */
	if (system_state.wait.stage == SYSTEM_NETWORK_RETRY)
		system_network_step(server, system_state.wait.request);

	/* The monitor's samples, to the monitor objects (sysmon.c, WS134 p012). */
	zwl_sysmon_tick(server);

	/* The sound as volume.c has it, told to every sound object when it changed. */
	zwl_volume_audio_state(&audio);
	differs = memcmp(&audio, &system_state.audio, sizeof(audio));
	if (differs != 0 || !system_state.audio_told) {
		system_state.audio = audio;
		system_state.audio_told = 1U;
		system_tell(server, ZWL_SYSTEM_AUDIO, system_audio_state, KL_SYSTEM_AUDIO_EVENT_DONE);
	}
}

/*
 * Tells every network object what network.c's watch found changed (the
 * state, the scan); network.c calls it after it read them.
 */
void
zwl_system_network_changed(
	struct zwl_server *server,
	unsigned changed)
{
	/* Nothing a network object shows. */
	if ((changed & (KL_BACKEND_NETWORK_CHANGED_STATE | KL_BACKEND_NETWORK_CHANGED_SCAN)) == 0U)
		return;

	/* The new state and the new scan, with one done: one change. */
	system_state.network_changed = changed;
	system_tell(server, ZWL_SYSTEM_NETWORK, system_network_change, KL_SYSTEM_NETWORK_EVENT_DONE);
	system_state.network_changed = 0U;
}

/*
 * Takes the network daemon's answer when the request was the extension's:
 * a client's result, or the next step of a saved key.  Returns 1 when it
 * was the extension's (network.c then only sends what waits in its slot),
 * 0 when it was the system bar's -- its own requests, and the join of a
 * key the bar saved, which network.c follows as its own join.
 */
int
zwl_system_network_done(
	struct zwl_server *server,
	unsigned request,
	int error)
{
	struct system_network_wait *wait;

	/* Only a stage that sent the daemon a request waits for an answer. */
	wait = &system_state.wait;
	if (wait->stage != SYSTEM_NETWORK_REQUEST &&
	    wait->stage != SYSTEM_NETWORK_PROFILES &&
	    wait->stage != SYSTEM_NETWORK_JOINING)
		return 0;
	if (request != wait->request)
		return 0;

	/* The daemon has the saved networks: the network is joined next. */
	if (wait->stage == SYSTEM_NETWORK_PROFILES && error == 0) {
		system_network_step(server, KL_BACKEND_NETWORK_REQUEST_JOIN);
		return 1;
	}

	/* The join of a key the system bar saved is the bar's to follow (its failure text, its key field). */
	if (wait->stage == SYSTEM_NETWORK_JOINING && wait->bar) {
		memset(&system_state.wait, 0, sizeof(system_state.wait));
		return 0;
	}

	/* The request a client asked for, or the last step of its key, answered. */
	system_network_finish(server, error);

	/* Succeeded: the answer was the extension's. */
	return 1;
}

/*
 * Saves a key the system bar's key field took, on the network's thread,
 * then tells the daemon and joins the network as a client's save_key
 * does; the join's answer comes to network.c as its own, a failure before
 * it as zwl_network_key_failed.  Returns 0, EBUSY while other network work
 * of the extension waits, EINVAL, or ENODEV without the daemon's watch.
 */
int
zwl_system_bar_save_key(
	struct zwl_server *server,
	const char *ssid,
	const char *key)
{
	size_t ssid_length;
	size_t key_length;
	int error;

	/* A network and a key within their bounds. */
	ssid_length = strlen(ssid);
	key_length = strlen(key);
	if (ssid_length == 0U || ssid_length >= KL_BACKEND_NETWORK_SSID_MAX)
		return EINVAL;
	if (key_length < KL_BACKEND_NETWORK_KEY_MIN || key_length > KL_BACKEND_NETWORK_KEY_MAX)
		return EINVAL;

	/* The steps, as the bar's. */
	error = system_network_save_key(server, 0U, 0U, 0U, 1U, ssid, key);
	if (error != 0)
		return error;

	/* Succeeded: the key is saved and the network joined after it. */
	return 0;
}

/*
 * Asks the network's thread for the saved networks, which come to network.c
 * as zwl_network_saved (with the next reading of the details).
 */
void
zwl_system_bar_saved(
	struct zwl_server *server)
{
	/* The bar waits for the next reading. */
	system_state.details_bar = 1U;
	system_network_job_next(server);
}

/*
 * Waits for the threads' jobs at the compositor's end, before the backend
 * closes (the power's thread uses it).
 */
void
zwl_system_close(
	struct zwl_server *server)
{
	UNUSED_PARAMETER(server);

	/* The monitor's sampling. */
	zwl_sysmon_close(server);

	/* A job under way ends on its own (a file read or written to its end, a bus call answered). */
	system_job_wait(&system_state.network_job);
	system_job_wait(&system_state.power_job);

	/* Nothing waits any more, and no key stays in memory. */
	system_wipe(system_state.wait.key, sizeof(system_state.wait.key));
	memset(&system_state.wait, 0, sizeof(system_state.wait));
	system_state.details_count = 0U;
	system_state.details_bar = 0U;
}

/* Carries out a request of the manager: it goes, or it makes one of its objects. */
static int
system_manager_request(
	struct zwl_object *manager,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *created;
	enum zwl_kind kind;
	uint32_t id;
	int error;

	/* The manager goes. */
	if (opcode == KL_SYSTEM_MANAGER_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(manager);
		return 0;
	}

	/* The monitor is sysmon.c's, since version 2 (WS134 p012). */
	if (opcode == KL_SYSTEM_MANAGER_GET_MONITOR) {
		if (manager->version < 2U)
			return EPROTO;
		error = zwl_sysmon_create(manager, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* The settings are settings.c's. */
	if (opcode == KL_SYSTEM_MANAGER_GET_SETTINGS) {
		error = zwl_settings_request(manager, opcode, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* Which object the request makes. */
	switch (opcode) {
	case KL_SYSTEM_MANAGER_GET_NETWORK:
		kind = ZWL_SYSTEM_NETWORK;
		break;
	case KL_SYSTEM_MANAGER_GET_AUDIO:
		kind = ZWL_SYSTEM_AUDIO;
		break;
	case KL_SYSTEM_MANAGER_GET_POWER:
		kind = ZWL_SYSTEM_POWER;
		break;
	case KL_SYSTEM_MANAGER_GET_DEVICES:
		kind = ZWL_SYSTEM_DEVICES;
		break;
	default:
		return EPROTO;
	}

	/* The object, under the ID the client chose. */
	if (size != 4U)
		return EPROTO;
	id = system_word(bytes, 0U);
	created = zwl_create(manager->client, id, kind, manager->version);
	if (created == NULL)
		return EPROTO;
	printf("ZWL SYSTEM object client=%llu get=%u id=%u\n", (unsigned long long)manager->client->number, (unsigned)opcode, id);

	/* Its first state and a done. */
	switch (kind) {
	case ZWL_SYSTEM_NETWORK:
		system_network_snapshot(created);
		break;
	case ZWL_SYSTEM_AUDIO:
		system_audio_state(created);
		system_done(created, KL_SYSTEM_AUDIO_EVENT_DONE);
		break;
	case ZWL_SYSTEM_POWER:
		/* Before the first read ends there is no state yet: the read's end tells it, with its done. */
		if (!system_state.power_read)
			break;
		system_power_state(created);
		system_done(created, KL_SYSTEM_POWER_EVENT_DONE);
		break;
	default:
		system_done(created, KL_SYSTEM_DEVICES_EVENT_DONE);
		break;
	}

	/* The power is read again, by its thread, for the new object (a change comes as its state and a done). */
	if (kind == ZWL_SYSTEM_POWER && system_state.power_read)
		system_power_read(manager->client->server);

	/* Succeeded: the object is the client's. */
	return 0;
}

/* Carries out a request of a network object. */
static int
system_network_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct system_network_wait *wait;
	uint32_t number;
	uint32_t applied;
	uint32_t what;
	char *ssid;
	char *key;
	size_t next;
	size_t end;
	int error;

	/* The object goes; work it waits for goes on without it. */
	if (opcode == KL_SYSTEM_NETWORK_DESTROY) {
		if (size != 0U)
			return EPROTO;
		wait = &system_state.wait;
		if (wait->stage != SYSTEM_NETWORK_IDLE && !wait->bar && wait->client == object->client->number && wait->object == object->id)
			wait->object = 0U;
		zwl_object_destroy(object);
		return 0;
	}

	/* The details: the request's number alone. */
	if (opcode == KL_SYSTEM_NETWORK_QUERY_DETAILS) {
		if (size != 4U)
			return EPROTO;
		number = system_word(bytes, 0U);
		applied = system_network_details(object, number);
		if (applied != KL_SYSTEM_RESULT_OK)
			system_result(object, KL_SYSTEM_NETWORK_EVENT_RESULT, number, applied);
		return 0;
	}

	/* A request of the daemon's: its number, what, and the network. */
	if (opcode == KL_SYSTEM_NETWORK_REQUEST) {
		if (size < 8U)
			return EPROTO;
		number = system_word(bytes, 0U);
		what = system_word(bytes, 4U);
		error = system_read_string(bytes, size, 8U, &ssid, &end);
		if (error != 0)
			return EPROTO;
		if (end != size) {
			free(ssid);
			return EPROTO;
		}

		/* Sent, or answered at once when it cannot be. */
		applied = system_network_send(object, number, what, ssid);
		free(ssid);
		if (applied != KL_SYSTEM_RESULT_OK)
			system_result(object, KL_SYSTEM_NETWORK_EVENT_RESULT, number, applied);
		return 0;
	}

	/* A key saved: its number, the network and the key. */
	if (opcode != KL_SYSTEM_NETWORK_SAVE_KEY || size < 4U)
		return EPROTO;
	number = system_word(bytes, 0U);
	error = system_read_string(bytes, size, 4U, &ssid, &next);
	if (error != 0)
		return EPROTO;
	error = system_read_string(bytes, size, next, &key, &end);
	if (error != 0) {
		free(ssid);
		return EPROTO;
	}

	/* The key is wiped from the copy as soon as it is handed on. */
	if (end != size) {
		system_wipe(key, strlen(key));
		free(key);
		free(ssid);
		return EPROTO;
	}

	/* Started, or answered at once when it cannot be. */
	error = system_network_save_key(object->client->server, object->client->number, object->id, number, 0U, ssid, key);
	system_wipe(key, strlen(key));
	free(key);
	free(ssid);
	if (error != 0)
		system_result(object, KL_SYSTEM_NETWORK_EVENT_RESULT, number, system_network_result_of(error));

	/* Succeeded: the request is answered now, or when it finishes. */
	return 0;
}

/* Carries out a request of a sound object. */
static int
system_audio_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t number;
	uint32_t left;
	uint32_t right;
	uint32_t muted;
	int error;

	/* The object goes. */
	if (opcode == KL_SYSTEM_AUDIO_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(object);
		return 0;
	}

	/* The feedback sound, at the device volume. */
	if (opcode == KL_SYSTEM_AUDIO_FEEDBACK) {
		if (size != 4U)
			return EPROTO;
		number = system_word(bytes, 0U);
		error = zwl_volume_feedback();
		system_result(object, KL_SYSTEM_AUDIO_EVENT_RESULT, number, system_result_of(error));
		return 0;
	}

	/* A volume: its number, both channels and the mute. */
	if (opcode != KL_SYSTEM_AUDIO_SET_VOLUME || size != 16U)
		return EPROTO;
	number = system_word(bytes, 0U);
	left = system_word(bytes, 4U);
	right = system_word(bytes, 8U);
	muted = system_word(bytes, 12U);
	if (left > 100U || right > 100U || muted > 1U) {
		system_result(object, KL_SYSTEM_AUDIO_EVENT_RESULT, number, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/* Shown in the system bar and sent to the sound service (its report comes back as the state). */
	error = zwl_volume_request_channels(object->client->server, left, right, muted);
	system_result(object, KL_SYSTEM_AUDIO_EVENT_RESULT, number, system_result_of(error));

	/* Succeeded: the request is answered. */
	return 0;
}

/* Carries out a request of a power object. */
static int
system_power_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t number;
	uint32_t action;
	int error;

	/* The object goes. */
	if (opcode == KL_SYSTEM_POWER_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(object);
		return 0;
	}

	/* An action: its number and which. */
	if (opcode != KL_SYSTEM_POWER_ACTION || size != 8U)
		return EPROTO;
	number = system_word(bytes, 0U);
	action = system_word(bytes, 4U);
	if (action < KL_SYSTEM_POWER_POWEROFF || action > KL_SYSTEM_POWER_SUSPEND) {
		system_result(object, KL_SYSTEM_POWER_EVENT_RESULT, number, KL_SYSTEM_RESULT_INVALID);
		return 0;
	}

	/*
	 * Asked of the session manager on this thread: the backend's record of
	 * the action asked is the event loop's (a zedBSD session offers none:
	 * unsupported at once; logind answers its one call).
	 */
	error = kl_backend_power_action(object->client->server->backend, action);
	printf("ZWL SYSTEM power client=%llu action=%u error=%d\n", (unsigned long long)object->client->number, action, error);
	system_result(object, KL_SYSTEM_POWER_EVENT_RESULT, number, system_result_of(error));

	/* Succeeded: the request is answered. */
	return 0;
}

/* Carries out a request of a devices object (no device is known until WS132). */
static int
system_devices_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t number;
	char *id;
	size_t end;
	int error;

	/* The object goes. */
	if (opcode == KL_SYSTEM_DEVICES_DESTROY) {
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(object);
		return 0;
	}

	/* An eject: its number and the device. */
	if (opcode != KL_SYSTEM_DEVICES_EJECT || size < 4U)
		return EPROTO;
	number = system_word(bytes, 0U);
	error = system_read_string(bytes, size, 4U, &id, &end);
	if (error != 0)
		return EPROTO;
	free(id);
	if (end != size)
		return EPROTO;

	/* No device can be ejected yet. */
	system_result(object, KL_SYSTEM_DEVICES_EVENT_RESULT, number, KL_SYSTEM_RESULT_UNSUPPORTED);

	/* Succeeded: the request is answered. */
	return 0;
}

/* Sends a client's request to the network daemon; the result comes with the daemon's answer. */
static uint32_t
system_network_send(
	struct zwl_object *object,
	uint32_t number,
	uint32_t what,
	const char *ssid)
{
	struct system_network_wait *wait;
	struct kl_backend_network *watch;
	const char *named;
	unsigned request;
	size_t length;
	int error;

	/* One of the requests a client may make; a join names a network that fits. */
	request = system_network_what(what);
	if (request == KL_BACKEND_NETWORK_REQUEST_NONE)
		return KL_SYSTEM_RESULT_INVALID;
	length = strlen(ssid);
	if (length >= KL_BACKEND_NETWORK_SSID_MAX)
		return KL_SYSTEM_RESULT_INVALID;
	if (request == KL_BACKEND_NETWORK_REQUEST_JOIN && length == 0U)
		return KL_SYSTEM_RESULT_INVALID;

	/* One network request at a time, and the daemon's watch. */
	wait = &system_state.wait;
	if (wait->stage != SYSTEM_NETWORK_IDLE)
		return KL_SYSTEM_RESULT_BUSY;
	watch = zwl_network_watch();
	if (watch == NULL)
		return KL_SYSTEM_RESULT_UNAVAILABLE;

	/* The request (the system bar's may be outstanding: busy). */
	named = NULL;
	if (request == KL_BACKEND_NETWORK_REQUEST_JOIN)
		named = ssid;
	error = kl_backend_network_request(watch, request, named);
	printf("ZWL SYSTEM network client=%llu request=%u error=%d\n", (unsigned long long)object->client->number, request, error);
	if (error != 0)
		return system_network_result_of(error);

	/* The answer is waited for. */
	memset(wait, 0, sizeof(*wait));
	wait->stage = SYSTEM_NETWORK_REQUEST;
	wait->request = request;
	wait->client = object->client->number;
	wait->object = object->id;
	wait->number = number;
	(void)snprintf(wait->ssid, sizeof(wait->ssid), "%s", ssid);

	/* Succeeded: the result comes with the answer. */
	return KL_SYSTEM_RESULT_OK;
}

/*
 * Starts saving a key, of a client's object or the system bar's (bar):
 * on the network's thread now, or as soon as it is free; the daemon is
 * told and the network joined after it.  Returns 0, EINVAL, EBUSY while
 * other network work waits, or ENODEV without the daemon's watch.
 */
static int
system_network_save_key(
	struct zwl_server *server,
	uint64_t client,
	uint32_t object,
	uint32_t number,
	unsigned bar,
	const char *ssid,
	const char *key)
{
	struct system_network_wait *wait;
	struct kl_backend_network *watch;
	size_t ssid_length;
	size_t key_length;

	/* A network and a key within their bounds (a WPA key is 8 to 63 characters). */
	ssid_length = strlen(ssid);
	key_length = strlen(key);
	if (ssid_length == 0U || ssid_length >= KL_BACKEND_NETWORK_SSID_MAX)
		return EINVAL;
	if (key_length < KL_BACKEND_NETWORK_KEY_MIN || key_length > KL_BACKEND_NETWORK_KEY_MAX)
		return EINVAL;

	/* One network request at a time, and the daemon's watch to tell. */
	wait = &system_state.wait;
	if (wait->stage != SYSTEM_NETWORK_IDLE)
		return EBUSY;
	watch = zwl_network_watch();
	if (watch == NULL)
		return ENODEV;

	/* The key waits for the thread, which takes it at once when it is free (the network, never the key, is logged). */
	memset(wait, 0, sizeof(*wait));
	wait->stage = SYSTEM_NETWORK_QUEUED;
	wait->request = KL_BACKEND_NETWORK_REQUEST_NONE;
	wait->bar = bar;
	wait->client = client;
	wait->object = object;
	wait->number = number;
	(void)snprintf(wait->ssid, sizeof(wait->ssid), "%s", ssid);
	(void)snprintf(wait->key, sizeof(wait->key), "%s", key);
	printf("ZWL SYSTEM network client=%llu bar=%u save-key ssid=%s\n", (unsigned long long)client, bar, ssid);
	system_network_job_next(server);

	/* Succeeded: the result comes once the network is joined. */
	return 0;
}

/* Asks the network's thread for the details for an object; it hears them before its result. */
static uint32_t
system_network_details(
	struct zwl_object *object,
	uint32_t number)
{
	struct system_details_wait *waiting;

	/* Room for one more. */
	if (system_state.details_count >= SYSTEM_DETAILS_WAITING)
		return KL_SYSTEM_RESULT_BUSY;

	/* The object waits for the next reading. */
	waiting = &system_state.details[system_state.details_count];
	waiting->client = object->client->number;
	waiting->object = object->id;
	waiting->number = number;
	system_state.details_count++;
	system_network_job_next(object->client->server);

	/* Succeeded: the details and the result come later. */
	return KL_SYSTEM_RESULT_OK;
}

/*
 * Sends a step of a saved key (the saved networks told, or the join); one
 * the system bar's request holds up is sent again next pass.
 */
static void
system_network_step(
	struct zwl_server *server,
	unsigned request)
{
	struct system_network_wait *wait;
	struct kl_backend_network *watch;
	const char *named;
	int error;

	/* The daemon's watch; it does not go while the compositor runs. */
	wait = &system_state.wait;
	watch = zwl_network_watch();
	if (watch == NULL) {
		system_network_finish(server, ENODEV);
		return;
	}

	/* The step; a join names the network. */
	named = NULL;
	if (request == KL_BACKEND_NETWORK_REQUEST_JOIN)
		named = wait->ssid;
	error = kl_backend_network_request(watch, request, named);

	/* Held up by the system bar's request: sent again next pass. */
	if (error == EBUSY) {
		wait->stage = SYSTEM_NETWORK_RETRY;
		wait->request = request;
		return;
	}

	/* A step that could not be sent ends the work. */
	printf("ZWL SYSTEM network step request=%u ssid=%s error=%d\n", request, wait->ssid, error);
	if (error != 0) {
		system_network_finish(server, error);
		return;
	}

	/* Its answer is waited for. */
	wait->request = request;
	wait->stage = SYSTEM_NETWORK_JOINING;
	if (request == KL_BACKEND_NETWORK_REQUEST_PROFILES)
		wait->stage = SYSTEM_NETWORK_PROFILES;
}

/* Tells a new network object the state, the scan and a done. */
static void
system_network_snapshot(
	struct zwl_object *object)
{
	/* The state and the scan, then the done that makes them one state. */
	system_network_state(object);
	system_network_scan(object);
	system_done(object, KL_SYSTEM_NETWORK_EVENT_DONE);
}

/* Sends what of the network changed: the state, the scan, or both. */
static void
system_network_change(
	struct zwl_object *object)
{
	/* The state. */
	if ((system_state.network_changed & KL_BACKEND_NETWORK_CHANGED_STATE) != 0U)
		system_network_state(object);

	/* The scan. */
	if ((system_state.network_changed & KL_BACKEND_NETWORK_CHANGED_SCAN) != 0U)
		system_network_scan(object);
}

/* Sends the network's state. */
static void
system_network_state(
	struct zwl_object *object)
{
	struct kl_backend_network_state state;
	unsigned char payload[SYSTEM_EVENT_MAX];
	size_t offset;

	/* The state as network.c's watch last reported it. */
	zwl_network_state(&state);

	/* reachable, connected, kind, interface, wired, wifi, wifi_interface, ssid. */
	offset = system_put_word(payload, 0U, state.reachable);
	offset = system_put_word(payload, offset, state.connected);
	offset = system_put_word(payload, offset, state.kind);
	offset = system_put_string(payload, offset, state.interface);
	offset = system_put_string(payload, offset, state.wired);
	offset = system_put_word(payload, offset, state.wifi);
	offset = system_put_string(payload, offset, state.wifi_interface);
	offset = system_put_string(payload, offset, state.ssid);
	(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_STATE, payload, offset);
}

/* Sends the networks of the last scan and the end of the list. */
static void
system_network_scan(
	struct zwl_object *object)
{
	struct kl_backend_network_ap aps[KL_BACKEND_NETWORK_SCAN_MAX];
	unsigned char payload[SYSTEM_EVENT_MAX];
	size_t offset;
	size_t count;
	size_t index;

	/* The scan as network.c's watch last reported it. */
	count = zwl_network_scan(aps, KL_BACKEND_NETWORK_SCAN_MAX);

	/* Each network: ssid, rssi, secured. */
	for (index = 0; index < count; index++) {
		offset = system_put_string(payload, 0U, aps[index].ssid);
		offset = system_put_word(payload, offset, (uint32_t)aps[index].rssi);
		offset = system_put_word(payload, offset, aps[index].secured);
		(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_ACCESS_POINT, payload, offset);
	}

	/* The end of the list. */
	(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_SCAN_DONE, NULL, 0U);
}

/* Sends the details the thread read: the interfaces, the DNS servers, the saved networks and the end. */
static void
system_network_details_send(
	struct zwl_object *object)
{
	const struct kl_backend_network_link *link;
	const struct system_job *job;
	unsigned char payload[SYSTEM_EVENT_MAX];
	char hardware[18];
	uint32_t flags;
	size_t offset;
	size_t index;

	/* Each interface. */
	job = &system_state.network_job;
	for (index = 0; index < job->link_count && index < KL_BACKEND_NETWORK_LINKS_MAX; index++) {
		/* Its flags and its hardware address as text. */
		link = &job->links[index];
		flags = 0U;
		if (link->up)
			flags |= KL_SYSTEM_LINK_UP;
		if (link->running)
			flags |= KL_SYSTEM_LINK_RUNNING;
		if (link->loopback)
			flags |= KL_SYSTEM_LINK_LOOPBACK;
		(void)snprintf(hardware, sizeof(hardware), "%02x:%02x:%02x:%02x:%02x:%02x", link->hardware[0], link->hardware[1], link->hardware[2], link->hardware[3], link->hardware[4], link->hardware[5]);

		/* name, flags, address, netmask, hardware, mtu, and the bytes received and sent in halves. */
		offset = system_put_string(payload, 0U, link->name);
		offset = system_put_word(payload, offset, flags);
		offset = system_put_string(payload, offset, link->address);
		offset = system_put_string(payload, offset, link->netmask);
		offset = system_put_string(payload, offset, hardware);
		offset = system_put_word(payload, offset, link->mtu);
		offset = system_put_word(payload, offset, (uint32_t)(link->received_bytes >> 32));
		offset = system_put_word(payload, offset, (uint32_t)link->received_bytes);
		offset = system_put_word(payload, offset, (uint32_t)(link->sent_bytes >> 32));
		offset = system_put_word(payload, offset, (uint32_t)link->sent_bytes);
		(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_LINK, payload, offset);
	}

	/* Each DNS server. */
	for (index = 0; index < job->dns_count && index < KL_BACKEND_NETWORK_DNS_MAX; index++) {
		offset = system_put_string(payload, 0U, job->dns[index]);
		(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_DNS, payload, offset);
	}

	/* Each saved network. */
	for (index = 0; index < job->saved_count && index < KL_BACKEND_NETWORK_SCAN_MAX; index++) {
		offset = system_put_string(payload, 0U, job->saved[index]);
		(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_SAVED, payload, offset);
	}

	/* The end of the details. */
	(void)zwl_emit(object->client, object->id, KL_SYSTEM_NETWORK_EVENT_DETAILS_DONE, NULL, 0U);
}

/*
 * Ends the network work waiting with an errno value: the asking object's
 * result if it is still there, or the system bar told of a failure; then
 * makes room for the next.
 */
static void
system_network_finish(
	struct zwl_server *server,
	int error)
{
	struct system_network_wait *wait;
	struct zwl_object *object;

	/* The bar hears only a failure (its join's answer is its own, zwl_system_network_done). */
	wait = &system_state.wait;
	if (wait->bar) {
		if (error != 0)
			zwl_network_key_failed(server, wait->ssid, error);
	} else {
		/* The asking object's result (nothing of the network is kept by the compositor: saved follows applied). */
		object = system_network_object(server, wait->client, wait->object);
		if (object != NULL)
			system_result(object, KL_SYSTEM_NETWORK_EVENT_RESULT, wait->number, system_network_result_of(error));
	}

	/* Nothing waits any more, and no key stays. */
	system_wipe(wait->key, sizeof(wait->key));
	memset(wait, 0, sizeof(*wait));
}

/* Takes the network thread's finished job: the details sent, or the key saved and the daemon told. */
static void
system_network_job_take(
	struct zwl_server *server)
{
	struct system_job *job;
	int finished;

	/* Nothing finished yet. */
	job = &system_state.network_job;
	finished = system_job_finished(job);
	if (!finished)
		return;

	/* The details go to everyone the reading answers. */
	if (job->kind == SYSTEM_JOB_DETAILS) {
		system_details_take(server);
		return;
	}

	/* A key that could not be saved ends the work. */
	printf("ZWL SYSTEM network key saved ssid=%s error=%d\n", system_state.wait.ssid, job->error);
	if (job->error != 0) {
		system_network_finish(server, job->error);
		return;
	}

	/* The daemon is told the saved networks changed; its answer sends the join. */
	system_network_step(server, KL_BACKEND_NETWORK_REQUEST_PROFILES);
}

/*
 * Starts the network thread's next job when it is free: a key waiting
 * first, else the details when anyone waits for them.
 */
static void
system_network_job_next(
	struct zwl_server *server)
{
	struct system_network_wait *wait;
	int error;

	/* The thread is busy. */
	if (system_state.network_job.started)
		return;

	/* A key waiting is saved (the copy here is wiped once the thread has its own); a thread that cannot be made ends the work. */
	wait = &system_state.wait;
	if (wait->stage == SYSTEM_NETWORK_QUEUED) {
		error = system_job_start(&system_state.network_job, SYSTEM_JOB_SAVE_KEY, wait->ssid, wait->key);
		system_wipe(wait->key, sizeof(wait->key));
		if (error != 0) {
			system_network_finish(server, error);
			return;
		}

		/* The thread saves it. */
		wait->stage = SYSTEM_NETWORK_SAVING;
		return;
	}

	/* Nobody waits for the details. */
	if (system_state.details_count == 0U && !system_state.details_bar)
		return;

	/* Those waiting now are the ones this reading answers. */
	error = system_job_start(&system_state.network_job, SYSTEM_JOB_DETAILS, "", "");
	if (error != 0)
		return;
	memcpy(system_state.serving, system_state.details, system_state.details_count * sizeof(system_state.details[0]));
	system_state.serving_count = system_state.details_count;
	system_state.serving_bar = system_state.details_bar;
	system_state.details_count = 0U;
	system_state.details_bar = 0U;
}

/* Sends a finished reading of the details to each object it answers, with its result, and the saved networks to the bar. */
static void
system_details_take(
	struct zwl_server *server)
{
	const struct system_details_wait *waiting;
	struct system_job *job;
	struct zwl_object *object;
	size_t count;
	unsigned index;

	/* Each object still there: the details, then its result. */
	job = &system_state.network_job;
	for (index = 0; index < system_state.serving_count; index++) {
		waiting = &system_state.serving[index];
		object = system_network_object(server, waiting->client, waiting->object);
		if (object == NULL)
			continue;
		system_network_details_send(object);
		system_result(object, KL_SYSTEM_NETWORK_EVENT_RESULT, waiting->number, KL_SYSTEM_RESULT_OK);
	}

	/* The reading answered them all. */
	system_state.serving_count = 0U;

	/* The bar's saved networks. */
	if (system_state.serving_bar) {
		system_state.serving_bar = 0U;
		count = job->saved_count;
		if (count > KL_BACKEND_NETWORK_SCAN_MAX)
			count = KL_BACKEND_NETWORK_SCAN_MAX;
		zwl_network_saved(server, job->saved, count);
	}
}

/* Takes the power thread's finished read, and tells every power object the first state and each change. */
static void
system_power_job_take(
	struct zwl_server *server)
{
	struct system_job *job;
	int finished;
	int differs;
	int first;

	/* Nothing finished yet. */
	job = &system_state.power_job;
	finished = system_job_finished(job);
	if (!finished)
		return;

	/* The first read is the first state, which every power object waits for. */
	first = 0;
	if (!system_state.power_read)
		first = 1;

	/* A state that could not be read keeps the last one; a first one that could not says nothing is known. */
	if (job->error != 0 && !first)
		return;
	if (job->error != 0) {
		memset(&job->power, 0, sizeof(job->power));
		job->power.source = KL_BACKEND_POWER_SOURCE_UNKNOWN;
		job->power.percent = -1;
	}

	/* Told the first time, then only when it changed. */
	differs = memcmp(&job->power, &system_state.power, sizeof(job->power));
	system_state.power = job->power;
	system_state.power_read = 1U;
	if (differs != 0 || first)
		system_tell(server, ZWL_SYSTEM_POWER, system_power_state, KL_SYSTEM_POWER_EVENT_DONE);
}

/* Starts reading the power's state on its thread, unless a read is under way. */
static void
system_power_read(
	struct zwl_server *server)
{
	struct system_job *job;
	int error;

	/* One read at a time; the one under way answers the new object too. */
	job = &system_state.power_job;
	if (job->started)
		return;

	/* The read. */
	job->backend = server->backend;
	error = system_job_start(job, SYSTEM_JOB_POWER, "", "");
	if (error != 0)
		printf("ZWL SYSTEM power read error=%d\n", error);
}

/* Tells whether a thread's job is finished, and joins the thread then (its outputs are the event loop's from here). */
static int
system_job_finished(
	struct system_job *job)
{
	unsigned done;

	/* Nothing under way. */
	if (!job->started)
		return 0;

	/* Samples whether the thread is done. */
	(void)pthread_mutex_lock(&job->lock);

	done = job->done;

	(void)pthread_mutex_unlock(&job->lock);

	/* Still working. */
	if (!done)
		return 0;

	/* The thread's end; no key stays in memory. */
	(void)pthread_join(job->thread, NULL);
	job->started = 0U;
	system_wipe(job->key, sizeof(job->key));

	/* Succeeded: the job's outputs are ready. */
	return 1;
}

/* Starts a thread on a job; returns 0, EBUSY while one is under way, or the errno value of the thread. */
static int
system_job_start(
	struct system_job *job,
	unsigned kind,
	const char *ssid,
	const char *key)
{
	int error;

	/* One job at a time. */
	if (job->started)
		return EBUSY;

	/* The lock, once. */
	if (!job->lock_ready) {
		error = pthread_mutex_init(&job->lock, NULL);
		if (error != 0)
			return error;
		job->lock_ready = 1U;
	}

	/* The job's inputs, and no outputs yet. */
	job->kind = kind;
	(void)snprintf(job->ssid, sizeof(job->ssid), "%s", ssid);
	(void)snprintf(job->key, sizeof(job->key), "%s", key);
	job->error = 0;
	job->link_count = 0U;
	job->dns_count = 0U;
	job->saved_count = 0U;
	job->done = 0U;

	/* The thread. */
	error = pthread_create(&job->thread, NULL, system_job_run, job);
	if (error != 0) {
		system_wipe(job->key, sizeof(job->key));
		return error;
	}

	/* Succeeded: the job is under way. */
	job->started = 1U;
	return 0;
}

/* Waits for a thread's job under way, at the end, and wipes its key. */
static void
system_job_wait(
	struct system_job *job)
{
	/* Nothing under way. */
	if (!job->started)
		return;

	/* The thread's end. */
	(void)pthread_join(job->thread, NULL);
	job->started = 0U;
	system_wipe(job->key, sizeof(job->key));
}

/* A thread: saves a key, reads the network's details, or reads the power's state, away from the event loop. */
static void *
system_job_run(
	void *argument)
{
	struct system_job *job;
	size_t count;
	int error;

	/* The job the thread was started on. */
	job = argument;

	/* The job's work. */
	error = 0;
	switch (job->kind) {
	case SYSTEM_JOB_SAVE_KEY:
		error = kl_backend_network_save_key(job->ssid, job->key);
		break;
	case SYSTEM_JOB_DETAILS:
		count = kl_backend_network_get_links(job->links, KL_BACKEND_NETWORK_LINKS_MAX);
		job->link_count = count;
		count = kl_backend_network_get_dns(job->dns, KL_BACKEND_NETWORK_DNS_MAX);
		job->dns_count = count;
		count = kl_backend_network_get_saved(job->saved, KL_BACKEND_NETWORK_SCAN_MAX);
		job->saved_count = count;
		break;
	default:
		memset(&job->power, 0, sizeof(job->power));
		error = kl_backend_power_get_state(job->backend, &job->power);
		break;
	}

	/* How it went, for the event loop after the join. */
	(void)pthread_mutex_lock(&job->lock);

	job->error = error;
	job->done = 1U;

	(void)pthread_mutex_unlock(&job->lock);

	/* Succeeded: the thread ends. */
	return NULL;
}

/* Sends the sound's state. */
static void
system_audio_state(
	struct zwl_object *object)
{
	struct kl_backend_audio_state state;
	uint32_t words[7];

	/* The state as volume.c has it. */
	zwl_volume_audio_state(&state);

	/* reachable, device, rate, channels, left, right, muted. */
	words[0] = state.reachable;
	words[1] = state.device;
	words[2] = state.rate;
	words[3] = state.channels;
	words[4] = state.left;
	words[5] = state.right;
	words[6] = state.muted;
	(void)zwl_emit(object->client, object->id, KL_SYSTEM_AUDIO_EVENT_STATE, words, sizeof(words));
}

/* Sends the power's state as last read (sent only after the first read). */
static void
system_power_state(
	struct zwl_object *object)
{
	const struct kl_backend_power_state *state;
	uint32_t words[4];

	/* source, percent, charging, actions. */
	state = &system_state.power;
	words[0] = state->source;
	words[1] = (uint32_t)state->percent;
	words[2] = state->charging;
	words[3] = state->actions;
	(void)zwl_emit(object->client, object->id, KL_SYSTEM_POWER_EVENT_STATE, words, sizeof(words));
}

/* Tells every live object of a kind its state and a done. */
static void
system_tell(
	struct zwl_server *server,
	enum zwl_kind kind,
	void (*tell)(struct zwl_object *object),
	uint32_t done_opcode)
{
	struct zwl_client *client;
	struct zwl_object *object;

	/* Each client that is not ending. */
	for (client = server->clients;
	     client != NULL;
	     client = client->next) {
		if (client->fatal)
			continue;

		/* Each live object of the kind: the state, then the done that makes it one state. */
		for (object = client->objects;
		     object != NULL;
		     object = object->next) {
			if (object->kind != kind || object->dead)
				continue;
			tell(object);
			system_done(object, done_opcode);
		}
	}
}

/* Finds a client's network object by the client's number and the object's ID, if it is still there. */
static struct zwl_object *
system_network_object(
	struct zwl_server *server,
	uint64_t number,
	uint32_t id)
{
	struct zwl_client *client;
	struct zwl_object *object;

	/* The object went. */
	if (id == 0U)
		return NULL;

	/* The client by its number, and its network object of that ID. */
	for (client = server->clients;
	     client != NULL;
	     client = client->next) {
		if (client->number != number || client->fatal)
			continue;
		object = zwl_find(client, id);
		if (object == NULL || object->dead || object->kind != ZWL_SYSTEM_NETWORK)
			return NULL;
		return object;
	}

	/* The client went. */
	return NULL;
}

/* Answers a request; nothing of the system's is kept in a file by the compositor, so saved follows applied. */
static void
system_result(
	struct zwl_object *object,
	uint32_t opcode,
	uint32_t number,
	uint32_t applied)
{
	uint32_t words[3];

	/* request, applied, saved. */
	words[0] = number;
	words[1] = applied;
	words[2] = applied;
	(void)zwl_emit(object->client, object->id, opcode, words, sizeof(words));
	printf("ZWL SYSTEM result client=%llu object=%u request=%u applied=%u\n", (unsigned long long)object->client->number, object->id, number, applied);
}

/* Sends a done with the serial of the state it closes. */
static void
system_done(
	struct zwl_object *object,
	uint32_t opcode)
{
	uint32_t serial;

	/* The next serial: each done of the extension has its own. */
	system_state.serial++;
	serial = system_state.serial;
	(void)zwl_emit(object->client, object->id, opcode, &serial, sizeof(serial));
}

/* Gives the protocol's result for an errno value. */
static uint32_t
system_result_of(
	int error)
{
	/* Each errno value the system gives. */
	switch (error) {
	case 0:
		return KL_SYSTEM_RESULT_OK;
	case EBUSY:
		return KL_SYSTEM_RESULT_BUSY;
	case EINVAL:
		return KL_SYSTEM_RESULT_INVALID;
	case EPERM:
	case EACCES:
		return KL_SYSTEM_RESULT_DENIED;
	case ENOTSUP:
		return KL_SYSTEM_RESULT_UNSUPPORTED;
	case ENOENT:
	case ENODEV:
	case ENOTCONN:
		return KL_SYSTEM_RESULT_UNAVAILABLE;
	default:
		break;
	}

	/* Anything else failed. */
	return KL_SYSTEM_RESULT_FAILED;
}

/*
 * Gives the protocol's result for an errno value of the network daemon,
 * which tells a join's failures apart (WS131 p011: Settings says why).
 */
static uint32_t
system_network_result_of(
	int error)
{
	uint32_t applied;

	/* The join's own failures: no key saved, the key refused, the network out of reach. */
	switch (error) {
	case ENOENT:
		return KL_SYSTEM_RESULT_NO_KEY;
	case EACCES:
		return KL_SYSTEM_RESULT_REFUSED;
	case ENETUNREACH:
		return KL_SYSTEM_RESULT_UNREACHABLE;
	default:
		break;
	}

	/* Every other one as for the rest of the system. */
	applied = system_result_of(error);
	return applied;
}

/* Gives the daemon's request for a client's what, or none for a what the protocol does not have. */
static unsigned
system_network_what(
	uint32_t what)
{
	/* Each request a client may make. */
	switch (what) {
	case KL_SYSTEM_NETWORK_SCAN:
		return KL_BACKEND_NETWORK_REQUEST_SCAN;
	case KL_SYSTEM_NETWORK_JOIN:
		return KL_BACKEND_NETWORK_REQUEST_JOIN;
	case KL_SYSTEM_NETWORK_DISCONNECT:
		return KL_BACKEND_NETWORK_REQUEST_DISCONNECT;
	case KL_SYSTEM_NETWORK_WIFI_ON:
		return KL_BACKEND_NETWORK_REQUEST_WIFI_ON;
	case KL_SYSTEM_NETWORK_WIFI_OFF:
		return KL_BACKEND_NETWORK_REQUEST_WIFI_OFF;
	default:
		break;
	}

	/* Not one of them (the saved networks are told only within save_key). */
	return KL_BACKEND_NETWORK_REQUEST_NONE;
}

/* Wipes a key's bytes through a volatile pointer, so the stores are not left out as dead. */
static void
system_wipe(
	char *text,
	size_t size)
{
	volatile char *byte;
	size_t index;

	/* Every byte. */
	byte = text;
	for (index = 0; index < size; index++)
		byte[index] = '\0';
}

/* Writes a word argument and gives the offset after it. */
static size_t
system_put_word(
	unsigned char *payload,
	size_t offset,
	uint32_t word)
{
	/* The word in the wire's native byte order. */
	memcpy(payload + offset, &word, sizeof(word));
	return offset + sizeof(word);
}

/* Writes a string argument and gives the offset after it. */
static size_t
system_put_string(
	unsigned char *payload,
	size_t offset,
	const char *text)
{
	uint32_t length;
	size_t padded;

	/* The length with the NUL, the bytes, and zeros to a four-byte boundary. */
	length = (uint32_t)strlen(text) + 1U;
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	memcpy(payload + offset, &length, sizeof(length));
	memset(payload + offset + 4U, 0, padded);
	memcpy(payload + offset + 4U, text, length - 1U);

	/* The offset after the string. */
	return offset + 4U + padded;
}

/* Reads a string argument into an allocated copy; *next is the offset after it.  Returns 0, EPROTO or ENOMEM. */
static int
system_read_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	char **text,
	size_t *next)
{
	const void *inner;
	uint32_t length;
	size_t padded;
	char *copy;

	/* The length, with the NUL, within the request and the bound. */
	if (offset + 4U > size)
		return EPROTO;
	length = system_word(bytes, offset);
	if (length == 0U || length > SYSTEM_WIRE_TEXT_MAX)
		return EPROTO;
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	if (offset + 4U + padded > size)
		return EPROTO;

	/* The text ends with its NUL and has no other. */
	if (bytes[offset + 4U + length - 1U] != '\0')
		return EPROTO;
	inner = memchr(bytes + offset + 4U, '\0', length - 1U);
	if (inner != NULL)
		return EPROTO;

	/* The copy. */
	copy = malloc(length);
	if (copy == NULL)
		return ENOMEM;
	memcpy(copy, bytes + offset + 4U, length);

	/* Succeeded: the text, and where the next argument starts. */
	*text = copy;
	*next = offset + 4U + padded;
	return 0;
}

/* Reads a 32-bit word of a request in the wire's native byte order. */
static uint32_t
system_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The word. */
	memcpy(&word, bytes + offset, sizeof(word));
	return word;
}
