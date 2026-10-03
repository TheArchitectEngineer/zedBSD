/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's settings in the compositor (WS135, plan/ws135/design.md
 * section 4): the session's store (settings-store.c), the settings put
 * into effect (the wallpaper, the windows' opacity, the pointer, the
 * keyboards' repeat, the sound through volume.c), and Keiland's system
 * extension that clients reach them through (kl_system_manager_v1 and
 * kl_system_settings_v1, keiland/kl-system-protocol.h).
 *
 * The store reads desktop.conf once, before the look draws the wallpaper,
 * and writes it once, at the session's end: a thread merges it at the Log
 * Out, and the compositor's end merges what changed since.  Nothing here
 * waits on the disk during the session: a wallpaper chosen is read and
 * decoded by glass.c's thread, and its result answers the client later.
 *
 * Each change, whoever made it, comes to every settings object as its
 * value and a done.  Each key put into effect is logged
 * ("ZWL PREFERENCES key=... applied"), which the tests read.
 *
 * Until ws135-p004 moves Settings to the extension, Settings still writes
 * desktop.conf and preferences.c's watcher follows it
 * (zwl_settings_follow): those values are in effect and already in the
 * file, so the session's end does not write them again.
 */

#include "zwl.h"
#include "settings-store.h"
#include "ime.h"

#include "userland/desktop/keiland/kl-system-protocol.h"
#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <keiland.h>

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The longest string a request may carry; a longer key or value is refused as invalid. */
#define SETTINGS_WIRE_TEXT_MAX	4096U

/*
 * A client's request waiting for the wallpaper glass.c reads: the client
 * (by its number: the client may go meanwhile) and its settings object, the
 * request's number, and whether the wallpaper goes back to its default.
 * active is zero when nothing waits.
 */
struct settings_waiting {
	uint64_t client;
	uint32_t object;
	uint32_t request;
	unsigned reset;
	unsigned active;
	char path[KL_SETTINGS_VALUE_MAX];
};

/*
 * The compositor's side of the settings that is not the store: which
 * entries are to be told to the settings objects at the next flush, the
 * serial of the last done, and the request waiting for a wallpaper.  It
 * lives for the process (one desktop runs in it); only the event loop's
 * thread touches it.
 */
struct settings_state {
	unsigned announce[ZWL_SETTINGS_ENTRIES];
	uint32_t serial;
	struct settings_waiting waiting;
};

/* The one state of the process; zero until zwl_settings_open. */
static struct settings_state settings_state;

static int settings_home(char *home, size_t size);
static void settings_apply(struct zwl_server *server, const char *name, int starting);
static void settings_apply_all(struct zwl_server *server, int starting);
static void settings_apply_wallpaper(struct zwl_server *server, int starting);
static void settings_apply_opacity(struct zwl_server *server);
static void settings_apply_number(struct zwl_server *server, const char *name, int32_t *target);
static void settings_mark(struct zwl_server *server, const char *name);
static void settings_flush(struct zwl_server *server);
static int settings_emit_value(struct zwl_client *client, uint32_t id, const struct zwl_settings_entry *entry);
static void settings_emit_done(struct zwl_client *client, uint32_t id);
static void settings_result(struct zwl_client *client, uint32_t id, uint32_t request, uint32_t applied, int stored);
static int settings_set(struct zwl_object *object, const unsigned char *bytes, size_t size, int reset);
static uint32_t settings_change(struct zwl_object *object, uint32_t request, const char *name, const char *value, int reset, int *answered);
static uint32_t settings_change_sound(struct zwl_server *server, const struct kl_settings_key *key, const char *value, int reset);
static uint32_t settings_change_wallpaper(struct zwl_object *object, uint32_t request, const char *value, int reset, int *answered);
static void settings_wallpaper_done(struct zwl_server *server, int error);
static void settings_sound(struct zwl_server *server);
static int settings_snapshot(struct zwl_object *settings);
static uint32_t settings_result_of(int error);
static size_t settings_put_string(unsigned char *payload, size_t offset, const char *text);
static int settings_read_string(const unsigned char *bytes, size_t size, size_t offset, char **text, size_t *next);
static uint32_t settings_word(const unsigned char *bytes, size_t offset);

/*
 * Opens the session's settings, before the look draws the wallpaper: the
 * command line's wallpaper and opacity are the defaults, the file's values
 * come over them, and every setting is put into effect.  The login screen
 * has none.
 */
void
zwl_settings_open(
	struct zwl_server *server)
{
	struct zwl_settings_store *store;
	char home[ZWL_SETTINGS_PATH_MAX];
	char opacity[16];
	int percent;
	int error;

	/* What the command line gave is what a setting back at its default returns to. */
	server->window_opacity_started = server->window_opacity;
	server->wallpaper_started = server->wallpaper_path;
	server->wallpaper_chosen[0] = '\0';
	memset(&settings_state, 0, sizeof(settings_state));

	/* The store. */
	store = calloc(1, sizeof(*store));
	if (store == NULL) {
		printf("ZWL SETTINGS none errno=%d\n", ENOMEM);
		return;
	}

	/* The user's home; without one the settings live for the session only. */
	error = settings_home(home, sizeof(home));
	if (error != 0)
		home[0] = '\0';
	error = zwl_settings_store_open(store, home);
	if (error != 0)
		printf("ZWL SETTINGS home errno=%d\n", error);

	/* The command line's defaults: the wallpaper, and the opacity in whole percent. */
	if (server->wallpaper_path != NULL)
		zwl_settings_store_default(store, "wallpaper", server->wallpaper_path);
	percent = (int)(server->window_opacity * 100.0f + 0.5f);
	(void)snprintf(opacity, sizeof(opacity), "%d", percent);
	zwl_settings_store_default(store, "window.opacity", opacity);

	/* The file, once. */
	error = zwl_settings_store_load(store);
	if (error != 0)
		printf("ZWL SETTINGS read-failed errno=%d\n", error);

	/* Every setting, before anything is drawn. */
	server->settings = store;
	settings_apply_all(server, 1);
	printf("ZWL SETTINGS open present=%d\n", store->present);
}

/*
 * Looks after the settings once a pass: a wallpaper read meanwhile, the
 * sound audiod reported, and the changes to tell the settings objects.
 */
void
zwl_settings_tick(
	struct zwl_server *server)
{
	int finished;
	int error;

	/* No settings (the login screen). */
	if (server->settings == NULL)
		return;

	/* A wallpaper glass.c finished reading. */
	finished = zwl_glass_wallpaper_poll(server, &error);
	if (finished)
		settings_wallpaper_done(server, error);

	/* The sound as audiod has it. */
	settings_sound(server);

	/* What changed, to every settings object. */
	settings_flush(server);
}

/*
 * Starts writing the session's settings at the Log Out, without the event
 * loop waiting (the session goes on showing until its manager ends it).
 */
void
zwl_settings_logout(
	struct zwl_server *server)
{
	int error;

	/* No settings (the login screen). */
	if (server->settings == NULL)
		return;

	/* The merge, on its thread. */
	error = zwl_settings_store_save_later(server->settings);
	printf("ZWL SETTINGS logout-save error=%d\n", error);
}

/*
 * Writes what is left of the session's settings and lets them go, at the
 * compositor's end.
 */
void
zwl_settings_close(
	struct zwl_server *server)
{
	int error;

	/* No settings (the login screen). */
	if (server->settings == NULL)
		return;

	/* The writer, then whatever changed after it (or everything, without it). */
	error = zwl_settings_store_finish(server->settings);
	printf("ZWL SETTINGS saved error=%d\n", error);

	/* The store goes. */
	zwl_settings_store_close(server->settings);
	free(server->settings);
	server->settings = NULL;
}

/*
 * Follows desktop.conf as preferences.c's watcher read it again: Settings
 * wrote it (until ws135-p004).  Each compositor setting but the sound's
 * takes the file's value (or its default, when the key went), and the
 * settings that changed are put into effect and told.
 */
void
zwl_settings_follow(
	struct zwl_server *server)
{
	struct zwl_settings_entry *entry;
	char before[KL_SETTINGS_VALUE_MAX];
	char value[KEILAND_PREFERENCES_VALUE_MAX];
	unsigned index;
	unsigned chosen;
	int differs;
	int error;

	/* Nothing to follow. */
	if (server->settings == NULL || server->preferences == NULL)
		return;

	/* Each compositor setting the file may hold. */
	for (index = 0; index < server->settings->count; index++) {
		entry = &server->settings->entries[index];
		if ((entry->key->flags & KL_SETTINGS_KEY_KEPT) == 0U)
			continue;
		if (strncmp(entry->key->name, "sound.", 6U) == 0)
			continue;

		/* What it is now. */
		(void)snprintf(before, sizeof(before), "%s", entry->value);
		chosen = entry->chosen;

		/* The file's value, or none. */
		error = keiland_preferences_get(server->preferences, entry->key->name, value, sizeof(value));
		if (error != 0) {
			zwl_settings_store_follow(server->settings, entry->key->name, NULL);
		} else {
			zwl_settings_store_follow(server->settings, entry->key->name, value);
		}

		/* A setting that changed is put into effect and told. */
		differs = strcmp(before, entry->value);
		if (differs == 0 && chosen == entry->chosen)
			continue;
		settings_apply(server, entry->key->name, 0);
		settings_mark(server, entry->key->name);
	}

	/* The changes, told now. */
	settings_flush(server);
}

/*
 * Gives the value a setting had in the file at the session's start, as a
 * number (the volume volume.c gives audiod).  Returns 0, or ENOENT when
 * the file did not hold it.
 */
int
zwl_settings_kept(
	struct zwl_server *server,
	const char *name,
	int *number)
{
	struct zwl_settings_entry *entry;
	int error;

	/* No settings, or no such setting. */
	if (server->settings == NULL)
		return ENOENT;
	entry = zwl_settings_store_find(server->settings, name);
	if (entry == NULL || !entry->start_chosen)
		return ENOENT;

	/* The file's value as a number. */
	error = kl_settings_key_number(entry->key, entry->start, number);
	if (error != 0)
		return ENOENT;

	/* Succeeded: the kept number. */
	return 0;
}

/*
 * Tells whether a client sees a global of the system extension: only a
 * session (not the login screen) shows it, and only to a client of the
 * compositor's own user (WS131 D5).  Every other global is everyone's.
 */
int
zwl_settings_global_visible(
	struct zwl_client *client,
	enum zwl_kind kind)
{
	uid_t uid;
	int error;

	/* Only the system manager is limited. */
	if (kind != ZWL_SYSTEM_MANAGER)
		return 1;

	/* The login screen has no settings. */
	if (client->server->settings == NULL)
		return 0;

	/* The peer's user, looked at once. */
	if (!client->peer_checked) {
		client->peer_checked = 1;
		client->peer_same = 0;
		error = kl_backend_peer_uid(client->fd, &uid);
		if (error == 0 && uid == getuid())
			client->peer_same = 1;
		if (error != 0)
			printf("ZWL SETTINGS peer client=%llu errno=%d\n", (unsigned long long)client->number, error);
	}

	/* Only the compositor's user. */
	if (!client->peer_same)
		return 0;

	/* Succeeded: the client sees the extension. */
	return 1;
}

/*
 * Tells a newly bound system manager what it offers.
 */
int
zwl_settings_bind(
	struct zwl_object *manager)
{
	uint32_t bits;
	int error;

	/* The settings are all there is in version 1. */
	bits = KL_SYSTEM_CAPABILITY_SETTINGS;
	error = zwl_emit(manager->client, manager->id, KL_SYSTEM_MANAGER_EVENT_CAPABILITIES, &bits, sizeof(bits));
	if (error != 0)
		return error;

	/* Succeeded: the client knows the capabilities. */
	return 0;
}

/*
 * Carries out a request of kl_system_manager_v1 or of a settings object.
 */
int
zwl_settings_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *created;
	uint32_t id;
	int error;

	/* The manager: it goes, or it makes a settings object. */
	if (object->kind == ZWL_SYSTEM_MANAGER) {
		if (opcode == KL_SYSTEM_MANAGER_DESTROY && size == 0U) {
			zwl_object_destroy(object);
			return 0;
		}

		/* Only get_settings is left, with its new ID. */
		if (opcode != KL_SYSTEM_MANAGER_GET_SETTINGS || size != 4U)
			return EPROTO;
		id = settings_word(bytes, 0U);
		created = zwl_create(object->client, id, ZWL_SYSTEM_SETTINGS, object->version);
		if (created == NULL)
			return EPROTO;

		/* It hears every setting and a done. */
		error = settings_snapshot(created);
		if (error != 0)
			return error;
		return 0;
	}

	/* A settings object: it goes, sets, or resets. */
	switch (opcode) {
	case KL_SYSTEM_SETTINGS_DESTROY:
		if (size != 0U)
			return EPROTO;
		zwl_object_destroy(object);
		return 0;
	case KL_SYSTEM_SETTINGS_SET:
		error = settings_set(object, bytes, size, 0);
		break;
	case KL_SYSTEM_SETTINGS_RESET:
		error = settings_set(object, bytes, size, 1);
		break;
	default:
		error = EPROTO;
		break;
	}

	/* Reports a malformed request. */
	if (error != 0)
		return error;

	/* Succeeded: the request is answered (now, or once the wallpaper is read). */
	return 0;
}

/* Finds the user's home: $HOME, else the password file's; returns 0 or ENOENT. */
static int
settings_home(
	char *home,
	size_t size)
{
	const struct passwd *user;
	const char *given;
	int written;

	/* $HOME when it is an absolute path, else the password file's. */
	given = getenv("HOME");
	if (given == NULL || given[0] != '/') {
		user = getpwuid(getuid());
		if (user == NULL ||
		    user->pw_dir == NULL ||
		    user->pw_dir[0] != '/')
			return ENOENT;
		given = user->pw_dir;
	}

	/* The home must fit. */
	written = snprintf(home, size, "%s", given);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the home is known. */
	return 0;
}

/* Puts every setting into effect (starting: before the look is made). */
static void
settings_apply_all(
	struct zwl_server *server,
	int starting)
{
	unsigned index;

	/* Each compositor setting the store holds. */
	for (index = 0; index < server->settings->count; index++)
		settings_apply(server, server->settings->entries[index].key->name, starting);
}

/* Puts one setting into effect; the sound is audiod's (volume.c) and needs nothing here. */
static void
settings_apply(
	struct zwl_server *server,
	const char *name,
	int starting)
{
	/* The wallpaper and the windows' opacity. */
	if (strcmp(name, "wallpaper") == 0) {
		settings_apply_wallpaper(server, starting);
		return;
	}
	if (strcmp(name, "window.opacity") == 0) {
		settings_apply_opacity(server);
		return;
	}

	/* The pointer's speed and the wheel's direction. */
	if (strcmp(name, "pointer.speed") == 0) {
		settings_apply_number(server, name, &server->pointer_speed);
		return;
	}
	if (strcmp(name, "pointer.natural") == 0) {
		settings_apply_number(server, name, &server->pointer_natural);
		return;
	}

	/* The keyboards' repeat, told again to the keyboards bound already (not before anything is bound). */
	if (strcmp(name, "keyboard.repeat.rate") == 0) {
		settings_apply_number(server, name, &server->repeat_rate);
		if (!starting) {
			zwl_seat_repeat_changed(server);
			zwl_ime_repeat_changed(server);
		}
		return;
	}
	if (strcmp(name, "keyboard.repeat.delay") == 0) {
		settings_apply_number(server, name, &server->repeat_delay_ms);
		if (!starting) {
			zwl_seat_repeat_changed(server);
			zwl_ime_repeat_changed(server);
		}
		return;
	}
}

/*
 * Shows the wallpaper the settings hold, reading it now (the start, and a
 * file Settings wrote until ws135-p004); a client's choice is read by
 * glass.c's thread instead (settings_change_wallpaper).
 */
static void
settings_apply_wallpaper(
	struct zwl_server *server,
	int starting)
{
	struct zwl_settings_entry *entry;
	const char *path;
	const char *chosen;
	int differs;
	int error;

	/* The choice: the setting's path when chosen, else none (the command line's). */
	entry = zwl_settings_store_find(server->settings, "wallpaper");
	chosen = "";
	if (entry != NULL && entry->chosen)
		chosen = entry->value;

	/* The same choice as shown changes nothing. */
	differs = strcmp(chosen, server->wallpaper_chosen);
	if (differs == 0)
		return;

	/* The choice, and the picture it means. */
	(void)snprintf(server->wallpaper_chosen, sizeof(server->wallpaper_chosen), "%s", chosen);
	path = server->wallpaper_started;
	if (server->wallpaper_chosen[0] != '\0')
		path = server->wallpaper_chosen;
	server->wallpaper_path = path;

	/* Before the look is made, it draws the picture itself. */
	if (starting) {
		printf("ZWL PREFERENCES key=wallpaper applied\n");
		return;
	}

	/* Afterwards the look draws it now. */
	error = zwl_glass_wallpaper(server, path);
	if (error != 0) {
		printf("ZWL PREFERENCES key=wallpaper failed errno=%d\n", error);
		return;
	}

	/* The new picture is shown. */
	printf("ZWL PREFERENCES key=wallpaper applied\n");
}

/* Sets the windows' opacity the settings hold, or the command line's exactly while at the default. */
static void
settings_apply_opacity(
	struct zwl_server *server)
{
	struct zwl_settings_entry *entry;
	float opacity;
	int percent;
	int error;

	/* The command line's opacity while the setting is at its default. */
	entry = zwl_settings_store_find(server->settings, "window.opacity");
	opacity = server->window_opacity_started;
	percent = (int)(opacity * 100.0f + 0.5f);
	if (entry != NULL && entry->chosen) {
		error = kl_settings_key_number(entry->key, entry->value, &percent);
		if (error == 0)
			opacity = (float)percent / 100.0f;
	}

	/* The same opacity changes nothing. */
	if (opacity == server->window_opacity)
		return;

	/* Every window is drawn again at the new opacity. */
	server->window_opacity = opacity;
	server->dirty = 1;
	printf("ZWL PREFERENCES key=window.opacity applied value=%d\n", percent);
}

/* Sets a number the desktop uses to the setting's value when it differs, and logs it. */
static void
settings_apply_number(
	struct zwl_server *server,
	const char *name,
	int32_t *target)
{
	struct zwl_settings_entry *entry;
	int number;
	int error;

	/* The setting's value as a number. */
	entry = zwl_settings_store_find(server->settings, name);
	if (entry == NULL)
		return;
	error = kl_settings_key_number(entry->key, entry->value, &number);
	if (error != 0)
		return;

	/* The same value changes nothing. */
	if (*target == (int32_t)number)
		return;

	/* The new value, from the next input on. */
	*target = (int32_t)number;
	printf("ZWL PREFERENCES key=%s applied value=%d\n", name, number);
}

/* Marks a setting to be told to every settings object at the next flush. */
static void
settings_mark(
	struct zwl_server *server,
	const char *name)
{
	unsigned index;
	int differs;

	/* The setting's entry. */
	for (index = 0; index < server->settings->count; index++) {
		differs = strcmp(server->settings->entries[index].key->name, name);
		if (differs == 0) {
			settings_state.announce[index] = 1;
			return;
		}
	}
}

/* Tells every settings object the marked settings' values and a done. */
static void
settings_flush(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_object *object;
	unsigned marked;
	unsigned index;

	/* Nothing marked, nothing told. */
	marked = 0;
	for (index = 0; index < server->settings->count; index++)
		marked |= settings_state.announce[index];
	if (!marked)
		return;

	/* Each live settings object hears each marked value, then one done. */
	settings_state.serial++;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (object = client->objects; object != NULL; object = object->next) {
			if (object->kind != ZWL_SYSTEM_SETTINGS || object->dead)
				continue;

			/* The values, then the done that makes them one state. */
			for (index = 0; index < server->settings->count; index++) {
				if (settings_state.announce[index])
					(void)settings_emit_value(client, object->id, &server->settings->entries[index]);
			}
			settings_emit_done(client, object->id);
		}
	}

	/* Everything marked was told. */
	memset(settings_state.announce, 0, sizeof(settings_state.announce));
}

/* Sends a setting's value event: its key, its value (empty while not known) and its flags. */
static int
settings_emit_value(
	struct zwl_client *client,
	uint32_t id,
	const struct zwl_settings_entry *entry)
{
	unsigned char payload[16U + KL_SETTINGS_KEY_MAX + KL_SETTINGS_VALUE_MAX];
	const char *value;
	uint32_t flags;
	size_t offset;
	int error;

	/* The flags: at its default, or not known yet. */
	flags = 0;
	value = entry->value;
	if (!entry->chosen)
		flags |= KL_SYSTEM_SETTINGS_DEFAULT;
	if (!entry->known) {
		flags |= KL_SYSTEM_SETTINGS_UNKNOWN;
		value = "";
	}

	/* key, value, flags. */
	offset = settings_put_string(payload, 0, entry->key->name);
	offset = settings_put_string(payload, offset, value);
	memcpy(payload + offset, &flags, sizeof(flags));
	offset += sizeof(flags);
	error = zwl_emit(client, id, KL_SYSTEM_SETTINGS_EVENT_VALUE, payload, offset);
	if (error != 0)
		return error;

	/* Succeeded: the value is queued. */
	return 0;
}

/* Sends a done with the serial of the state it closes. */
static void
settings_emit_done(
	struct zwl_client *client,
	uint32_t id)
{
	uint32_t serial;

	/* The serial. */
	serial = settings_state.serial;
	(void)zwl_emit(client, id, KL_SYSTEM_SETTINGS_EVENT_DONE, &serial, sizeof(serial));
}

/* Answers a request: whether it was applied, and whether it will be kept (stored: the session's end writes the file). */
static void
settings_result(
	struct zwl_client *client,
	uint32_t id,
	uint32_t request,
	uint32_t applied,
	int stored)
{
	uint32_t words[3];

	/*
	 * saved: a setting applied is kept when the store has a file to write
	 * at the session's end; without a home it lives for the session only.
	 */
	words[0] = request;
	words[1] = applied;
	words[2] = applied;
	if (applied == KL_SYSTEM_RESULT_OK && !stored)
		words[2] = KL_SYSTEM_RESULT_NOT_SAVED;
	(void)zwl_emit(client, id, KL_SYSTEM_SETTINGS_EVENT_RESULT, words, sizeof(words));
	printf("ZWL SETTINGS result client=%llu request=%u applied=%u saved=%u\n", (unsigned long long)client->number, request, words[1], words[2]);
}

/* Reads a set (request, key, value) or a reset (request, key) and answers it; returns 0 or EPROTO for a malformed request. */
static int
settings_set(
	struct zwl_object *object,
	const unsigned char *bytes,
	size_t size,
	int reset)
{
	const char *shown;
	uint32_t request;
	uint32_t applied;
	char *name;
	char *value;
	size_t next;
	size_t end;
	int answered;
	int valid;
	int error;

	/* The request's number and the key. */
	if (size < 4U)
		return EPROTO;
	request = settings_word(bytes, 0U);
	error = settings_read_string(bytes, size, 4U, &name, &next);
	if (error != 0)
		return EPROTO;

	/* A set's value; nothing may trail. */
	value = NULL;
	end = next;
	if (!reset) {
		error = settings_read_string(bytes, size, next, &value, &end);
		if (error != 0) {
			free(name);
			return EPROTO;
		}
	}
	if (end != size) {
		free(value);
		free(name);
		return EPROTO;
	}

	/* The change, answered now unless the wallpaper is being read. */
	answered = 0;
	applied = settings_change(object, request, name, value, reset, &answered);
	if (!answered)
		settings_result(object->client, object->id, request, applied, object->client->server->settings->present);
	/* The log names a well-formed key only (a client's bytes are not written as they are). */
	shown = "-";
	valid = kl_settings_name_valid(name);
	if (valid)
		shown = name;
	printf("ZWL SETTINGS %s key=%s client=%llu applied=%u\n", reset ? "reset" : "set", shown, (unsigned long long)object->client->number, applied);
	free(value);
	free(name);

	/* Succeeded: the request was read. */
	return 0;
}

/*
 * Makes a client's change of a setting, puts it into effect and marks it to
 * be told.  Returns the result to answer with; *answered is set when the
 * answer comes later (the wallpaper).
 */
static uint32_t
settings_change(
	struct zwl_object *object,
	uint32_t request,
	const char *name,
	const char *value,
	int reset,
	int *answered)
{
	struct zwl_server *server;
	const struct kl_settings_key *key;
	uint32_t applied;
	int valid;
	int error;

	/* A name of the table's, resolved by the compositor. */
	server = object->client->server;
	valid = kl_settings_name_valid(name);
	if (!valid)
		return KL_SYSTEM_RESULT_INVALID;
	key = kl_settings_key_find(name);
	if (key == NULL || key->resolver != KL_SETTINGS_RESOLVER_COMPOSITOR)
		return KL_SYSTEM_RESULT_UNSUPPORTED;
	if ((key->flags & KL_SETTINGS_KEY_READ_ONLY) != 0U)
		return KL_SYSTEM_RESULT_DENIED;

	/* A value of its type and within its range. */
	if (!reset) {
		error = kl_settings_key_check(key, value);
		if (error != 0)
			return KL_SYSTEM_RESULT_INVALID;
	}

	/* The sound is audiod's; the wallpaper is read away from the event loop. */
	if (strncmp(name, "sound.", 6U) == 0) {
		applied = settings_change_sound(server, key, value, reset);
		return applied;
	}
	if (strcmp(name, "wallpaper") == 0) {
		applied = settings_change_wallpaper(object, request, value, reset, answered);
		return applied;
	}

	/* Any other setting: in the store, into effect, and told. */
	if (reset) {
		error = zwl_settings_store_reset(server->settings, name);
	} else {
		error = zwl_settings_store_choose(server->settings, name, value);
	}
	if (error != 0) {
		applied = settings_result_of(error);
		return applied;
	}
	settings_apply(server, name, 0);
	settings_mark(server, name);
	settings_flush(server);

	/* Succeeded: the setting is in effect. */
	return KL_SYSTEM_RESULT_OK;
}

/* Sends a client's volume or mute to audiod (through volume.c); the store takes audiod's report. */
static uint32_t
settings_change_sound(
	struct zwl_server *server,
	const struct kl_settings_key *key,
	const char *value,
	int reset)
{
	unsigned restored;
	unsigned available;
	unsigned volume;
	unsigned muted;
	uint32_t applied;
	int number;
	int error;

	/* The volume and mute as they are, and the one asked for (a reset is the table's default). */
	zwl_volume_report(&restored, &available, &volume, &muted);
	number = key->fallback;
	if (!reset)
		(void)kl_settings_key_number(key, value, &number);
	if (strcmp(key->name, "sound.volume") == 0) {
		volume = (unsigned)number;
	} else {
		muted = (unsigned)number;
	}

	/* audiod. */
	error = zwl_volume_request(server, volume, muted);
	if (error != 0) {
		applied = settings_result_of(error);
		return applied;
	}

	/* Succeeded: audiod has it; its report is told when it comes. */
	return KL_SYSTEM_RESULT_OK;
}

/* Starts reading a client's wallpaper (or the default's) on glass.c's thread; the answer waits for it. */
static uint32_t
settings_change_wallpaper(
	struct zwl_object *object,
	uint32_t request,
	const char *value,
	int reset,
	int *answered)
{
	struct zwl_server *server;
	struct zwl_settings_entry *entry;
	const char *path;
	uint32_t applied;
	int error;

	/* One wallpaper at a time. */
	server = object->client->server;
	if (settings_state.waiting.active)
		return KL_SYSTEM_RESULT_BUSY;

	/* The path: the client's, or the command line's for a reset. */
	path = value;
	if (reset)
		path = server->wallpaper_started;

	/* A reset to the landscape needs no file: in effect now. */
	if (path == NULL) {
		error = zwl_settings_store_reset(server->settings, "wallpaper");
		if (error != 0) {
			applied = settings_result_of(error);
			return applied;
		}
		entry = zwl_settings_store_find(server->settings, "wallpaper");
		if (entry != NULL)
			settings_apply_wallpaper(server, 0);
		settings_mark(server, "wallpaper");
		settings_flush(server);
		return KL_SYSTEM_RESULT_OK;
	}

	/* The thread; a path that is not an ordinary file is refused now. */
	error = zwl_glass_wallpaper_begin(server, path);
	if (error != 0) {
		applied = settings_result_of(error);
		return applied;
	}

	/* The request waits for it. */
	settings_state.waiting.client = object->client->number;
	settings_state.waiting.object = object->id;
	settings_state.waiting.request = request;
	settings_state.waiting.reset = (unsigned)reset;
	settings_state.waiting.active = 1;
	(void)snprintf(settings_state.waiting.path, sizeof(settings_state.waiting.path), "%s", path);
	*answered = 1;

	/* Succeeded: the answer comes once the picture is read. */
	return KL_SYSTEM_RESULT_OK;
}

/* Takes the wallpaper glass.c read: in the store and told when it is shown, and the waiting client answered either way. */
static void
settings_wallpaper_done(
	struct zwl_server *server,
	int error)
{
	struct settings_waiting *waiting;
	struct zwl_client *client;
	struct zwl_object *object;
	uint32_t applied;
	int stored;

	/* Nothing waits (the look went, or the reader was another's). */
	waiting = &settings_state.waiting;
	if (!waiting->active)
		return;
	waiting->active = 0;

	/* Shown: the setting holds it now. */
	applied = KL_SYSTEM_RESULT_FAILED;
	if (error == 0) {
		applied = KL_SYSTEM_RESULT_OK;
		if (waiting->reset) {
			(void)zwl_settings_store_reset(server->settings, "wallpaper");
			server->wallpaper_chosen[0] = '\0';
		} else {
			(void)zwl_settings_store_choose(server->settings, "wallpaper", waiting->path);
			(void)snprintf(server->wallpaper_chosen, sizeof(server->wallpaper_chosen), "%s", waiting->path);
		}

		/* The picture shown is the setting's. */
		server->wallpaper_path = server->wallpaper_started;
		if (server->wallpaper_chosen[0] != '\0')
			server->wallpaper_path = server->wallpaper_chosen;
		printf("ZWL PREFERENCES key=wallpaper applied\n");
		settings_mark(server, "wallpaper");
		settings_flush(server);
	}

	/* The client, if it is still there with its settings object. */
	stored = server->settings->present;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->number != waiting->client || client->fatal)
			continue;
		object = zwl_find(client, waiting->object);
		if (object == NULL ||
		    object->dead ||
		    object->kind != ZWL_SYSTEM_SETTINGS)
			break;
		settings_result(client, object->id, waiting->request, applied, stored);
		break;
	}
}

/* Takes the sound as audiod reports it into the store (once audiod took the kept volume), and marks what changed. */
static void
settings_sound(
	struct zwl_server *server)
{
	struct zwl_settings_entry *entry;
	unsigned restored;
	unsigned available;
	unsigned value;
	unsigned muted;
	char text[16];
	int differs;

	/* What volume.c knows. */
	zwl_volume_report(&restored, &available, &value, &muted);

	/* Whether there is sound, told when it changes. */
	(void)snprintf(text, sizeof(text), "%u", available);
	entry = zwl_settings_store_find(server->settings, "sound.available");
	if (entry != NULL) {
		differs = strcmp(entry->value, text);
		if (differs != 0 || !entry->known) {
			zwl_settings_store_report(server->settings, "sound.available", text);
			settings_mark(server, "sound.available");
		}
	}

	/* The volume is the session's only once audiod took the kept one. */
	if (!restored || !available)
		return;

	/* The volume. */
	(void)snprintf(text, sizeof(text), "%u", value);
	entry = zwl_settings_store_find(server->settings, "sound.volume");
	if (entry != NULL) {
		differs = strcmp(entry->value, text);
		if (differs != 0 || !entry->known) {
			zwl_settings_store_report(server->settings, "sound.volume", text);
			settings_mark(server, "sound.volume");
		}
	}

	/* The mute. */
	(void)snprintf(text, sizeof(text), "%u", muted);
	entry = zwl_settings_store_find(server->settings, "sound.muted");
	if (entry != NULL) {
		differs = strcmp(entry->value, text);
		if (differs != 0 || !entry->known) {
			zwl_settings_store_report(server->settings, "sound.muted", text);
			settings_mark(server, "sound.muted");
		}
	}
}

/* Tells a new settings object every setting and a done. */
static int
settings_snapshot(
	struct zwl_object *settings)
{
	struct zwl_settings_store *store;
	unsigned index;
	int error;

	/* Every compositor setting the store holds. */
	store = settings->client->server->settings;
	for (index = 0; store != NULL && index < store->count; index++) {
		error = settings_emit_value(settings->client, settings->id, &store->entries[index]);
		if (error != 0)
			return error;
	}

	/* The done that makes them one state. */
	settings_emit_done(settings->client, settings->id);
	printf("ZWL SETTINGS snapshot client=%llu object=%u\n", (unsigned long long)settings->client->number, settings->id);

	/* Succeeded: the object knows every setting. */
	return 0;
}

/* Gives the result an errno value of a change means. */
static uint32_t
settings_result_of(
	int error)
{
	/* Each errno value the changes give. */
	switch (error) {
	case 0:
		return KL_SYSTEM_RESULT_OK;
	case EBUSY:
		return KL_SYSTEM_RESULT_BUSY;
	case EINVAL:
	case ENAMETOOLONG:
		return KL_SYSTEM_RESULT_INVALID;
	case EPERM:
		return KL_SYSTEM_RESULT_DENIED;
	case ENOENT:
		return KL_SYSTEM_RESULT_UNSUPPORTED;
	case ENODEV:
		return KL_SYSTEM_RESULT_UNAVAILABLE;
	default:
		break;
	}

	/* Anything else failed. */
	return KL_SYSTEM_RESULT_FAILED;
}

/* Writes a string argument and gives the offset after it. */
static size_t
settings_put_string(
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
settings_read_string(
	const unsigned char *bytes,
	size_t size,
	size_t offset,
	char **text,
	size_t *next)
{
	uint32_t length;
	size_t padded;
	char *copy;

	/* The length, with the NUL. */
	if (offset + 4U > size)
		return EPROTO;
	length = settings_word(bytes, offset);
	padded = ((size_t)length + 3U) & ~(size_t)3U;
	if (length == 0U || length > SETTINGS_WIRE_TEXT_MAX)
		return EPROTO;
	if (offset + 4U + padded > size)
		return EPROTO;

	/* The text must end with its NUL. */
	if (bytes[offset + 4U + length - 1U] != '\0')
		return EPROTO;

	/* The copy. */
	copy = malloc(length);
	if (copy == NULL)
		return ENOMEM;
	memcpy(copy, bytes + offset + 4U, length);

	/* Succeeded: the text and where the next argument starts. */
	*text = copy;
	*next = offset + 4U + padded;
	return 0;
}

/* Reads a 32-bit word of a request in the wire's native byte order. */
static uint32_t
settings_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The word. */
	memcpy(&word, bytes + offset, sizeof(word));
	return word;
}
