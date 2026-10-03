/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's store of the desktop's settings (WS135, plan/ws135/
 * design.md sections 4.1 and 4.3): every compositor setting held in memory
 * for the session, read from ~/.config/keiland/desktop.conf once at the
 * session's start and merged back into it once at its end.
 *
 * It knows no Wayland and no server, so that the host tests build it
 * alone (plan/tools/settings/host-store.sh).
 */

#ifndef ZWL_SETTINGS_STORE_H
#define ZWL_SETTINGS_STORE_H

#include "userland/desktop/settings-keys/settings-keys.h"

#include <pthread.h>
#include <stdint.h>

/* The most compositor settings the store holds (the table's compositor rows). */
#define ZWL_SETTINGS_ENTRIES	16U

/* The longest path of the file, with its NUL. */
#define ZWL_SETTINGS_PATH_MAX	1024U

/*
 * One compositor setting as the session holds it.
 *
 * value is what is in effect; chosen is zero while it is its resolver's
 * default (fallback holds that default) and known is zero while nothing
 * has reported it yet (the sound before audiod).  start and start_chosen
 * are what the file held when the session began: the end of the session
 * writes the setting only when it differs from them.
 */
struct zwl_settings_entry {
	const struct kl_settings_key *key;
	char value[KL_SETTINGS_VALUE_MAX];
	char fallback[KL_SETTINGS_VALUE_MAX];
	char start[KL_SETTINGS_VALUE_MAX];
	unsigned chosen;
	unsigned known;
	unsigned start_chosen;
};

/*
 * One change the session's end writes: a key and its value, or no value
 * (the key goes back to its default and leaves the file).
 */
struct zwl_settings_change {
	char key[KL_SETTINGS_KEY_MAX];
	char value[KL_SETTINGS_VALUE_MAX];
	unsigned chosen;
};

/*
 * The thread that merges a session's changes into the file without the
 * event loop waiting (the Log Out): the changes taken when it started,
 * how many, its error once it ended, and whether it runs or has not been
 * joined yet.  lock guards done and error; only the event loop starts and
 * joins it.
 */
struct zwl_settings_writer {
	pthread_t thread;
	pthread_mutex_t lock;
	struct zwl_settings_change changes[ZWL_SETTINGS_ENTRIES];
	unsigned count;
	char path[ZWL_SETTINGS_PATH_MAX];
	char folder[ZWL_SETTINGS_PATH_MAX];
	uint64_t generation;
	int started;
	int done;
	int error;
};

/*
 * The store: the compositor's settings, the file they come from, and the
 * writer.
 *
 * present is zero when there is no home (nothing is read or written).
 * read_error is the start's failure other than a missing file.
 * generation moves at each change of a value; saved_generation is the
 * one the file last took.  It lives in the server for the compositor's
 * life (zwl_settings_store_open to zwl_settings_store_close).
 */
struct zwl_settings_store {
	struct zwl_settings_entry entries[ZWL_SETTINGS_ENTRIES];
	unsigned count;
	char folder[ZWL_SETTINGS_PATH_MAX];
	char path[ZWL_SETTINGS_PATH_MAX];
	int present;
	int read_error;
	uint64_t generation;
	uint64_t saved_generation;
	struct zwl_settings_writer writer;
};

int zwl_settings_store_open(struct zwl_settings_store *store, const char *home);
void zwl_settings_store_close(struct zwl_settings_store *store);
struct zwl_settings_entry *zwl_settings_store_find(struct zwl_settings_store *store, const char *name);
void zwl_settings_store_default(struct zwl_settings_store *store, const char *name, const char *value);
int zwl_settings_store_load(struct zwl_settings_store *store);
int zwl_settings_store_choose(struct zwl_settings_store *store, const char *name, const char *value);
int zwl_settings_store_reset(struct zwl_settings_store *store, const char *name);
void zwl_settings_store_report(struct zwl_settings_store *store, const char *name, const char *value);
unsigned zwl_settings_store_changes(const struct zwl_settings_store *store, struct zwl_settings_change *changes);
int zwl_settings_store_save(struct zwl_settings_store *store);
int zwl_settings_store_save_later(struct zwl_settings_store *store);
int zwl_settings_store_finish(struct zwl_settings_store *store);

#endif
