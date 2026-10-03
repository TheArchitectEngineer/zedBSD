/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cache of libkeiland's settings (settings-private.h; WS135,
 * plan/ws135/design.md section 3.4): the values the compositor sent and
 * the application's file holds, the watches, and the finished requests.
 *
 * The compositor's values are pending until its done, so that a state is
 * never seen half made.  The watches run only in settings_cache_notify,
 * which kl_settings_dispatch calls: a key is told once, with its last
 * value, and not at all when it changed back to what the watches heard.
 */

#include "settings-private.h"

#include <errno.h>
#include <string.h>

static void cache_copy(char *to, size_t size, const char *from);
static struct settings_cache_entry *cache_add(struct settings_cache *cache, const char *key);
static int cache_entry_differs(const struct settings_cache_entry *entry);
static int cache_watch_covers(const struct settings_cache_watch *watch, const char *key);

/*
 * Makes an empty cache: one entry for each key of the table, with no value.
 */
void
settings_cache_init(
	struct settings_cache *cache)
{
	const struct kl_settings_key *key;
	size_t count;
	size_t index;

	memset(cache, 0, sizeof(*cache));

	/* One entry for each key of a row (a prefix row's keys come as they are set), none of them with a value yet. */
	count = kl_settings_key_count();
	for (index = 0; index < count && cache->count < SETTINGS_CACHE_KEYS; index++) {
		key = kl_settings_key_at(index);
		if ((key->flags & KL_SETTINGS_KEY_PREFIX) != 0U)
			continue;
		cache_copy(cache->entries[cache->count].name, sizeof(cache->entries[0].name), key->name);
		cache->entries[cache->count].key = key;
		cache->count++;
	}

	/* Watch numbers start at 1 (0 names none). */
	cache->next_watch = 1;
}

/*
 * Finds a key's entry; NULL for a key the table does not have.
 */
struct settings_cache_entry *
settings_cache_find(
	struct settings_cache *cache,
	const char *key)
{
	unsigned index;
	int differs;

	/* Compares each entry's name with the one asked for. */
	for (index = 0; index < cache->count; index++) {
		differs = strcmp(cache->entries[index].name, key);
		if (differs == 0)
			return &cache->entries[index];
	}

	/* The table has no such key. */
	return NULL;
}

/*
 * Notes a value the compositor sent, in effect at its next done.
 */
void
settings_cache_pending(
	struct settings_cache *cache,
	const char *key,
	const char *value,
	unsigned flags,
	unsigned present)
{
	struct settings_cache_entry *entry;

	/* A key the table does not have (a newer compositor's) is passed over. */
	entry = settings_cache_find(cache, key);
	if (entry == NULL)
		return;

	/* The value waits for the done. */
	cache_copy(entry->pending_value, sizeof(entry->pending_value), value);
	entry->pending_flags = flags;
	entry->pending_present = present;
	entry->touched = 1;
}

/*
 * Puts the compositor's pending values into effect (its done).
 */
void
settings_cache_done(
	struct settings_cache *cache)
{
	struct settings_cache_entry *entry;
	unsigned index;

	/* Each key the compositor sent since its last done. */
	for (index = 0; index < cache->count; index++) {
		entry = &cache->entries[index];
		if (!entry->touched)
			continue;

		/* The pending value is the one in effect, and is compared at the next notify. */
		cache_copy(entry->value, sizeof(entry->value), entry->pending_value);
		entry->flags = entry->pending_flags;
		entry->present = entry->pending_present;
		entry->touched = 0;
		entry->changed = 1;
	}
}

/*
 * Puts a value into effect at once (the application's file).
 */
void
settings_cache_set(
	struct settings_cache *cache,
	const char *key,
	const char *value,
	unsigned flags,
	unsigned present)
{
	struct settings_cache_entry *entry;

	/* The key's entry; a prefix row's key gets one the first time. */
	entry = settings_cache_find(cache, key);
	if (entry == NULL)
		entry = cache_add(cache, key);
	if (entry == NULL)
		return;

	/* The value in effect, compared at the next notify. */
	cache_copy(entry->value, sizeof(entry->value), value);
	entry->flags = flags;
	entry->present = present;
	entry->changed = 1;
}

/*
 * Takes away the values of one resolver (the compositor went): its keys
 * have no value from now on, which the watches hear.
 */
void
settings_cache_lost(
	struct settings_cache *cache,
	unsigned resolver)
{
	struct settings_cache_entry *entry;
	unsigned index;

	/* Each key of the resolver. */
	for (index = 0; index < cache->count; index++) {
		entry = &cache->entries[index];
		if (entry->key->resolver != resolver)
			continue;

		/* No value, and nothing pending. */
		entry->value[0] = '\0';
		entry->flags = 0;
		entry->present = 0;
		entry->touched = 0;
		entry->changed = 1;
	}
}

/*
 * Takes the values in effect as already heard (at the open: a watch hears
 * changes, not the values the settings started with).
 */
void
settings_cache_settle(
	struct settings_cache *cache)
{
	struct settings_cache_entry *entry;
	unsigned index;

	/* Each key: what is in effect is what was told, and nothing is to compare. */
	for (index = 0; index < cache->count; index++) {
		entry = &cache->entries[index];
		cache_copy(entry->told_value, sizeof(entry->told_value), entry->value);
		entry->told_flags = entry->flags;
		entry->told_present = entry->present;
		entry->changed = 0;
	}
}

/*
 * Copies a key's value and flags.  Returns 0, ENOENT for a key the table
 * does not have, EAGAIN for a key with no value, or ERANGE.
 */
int
settings_cache_get(
	const struct settings_cache *cache,
	const char *key,
	char *value,
	size_t size,
	unsigned *flags)
{
	const struct settings_cache_entry *entry;
	const struct kl_settings_key *row;
	size_t length;

	/* The key's entry; a prefix row's key not set yet has no value. */
	entry = settings_cache_find((struct settings_cache *)cache, key);
	if (entry == NULL) {
		row = kl_settings_key_find(key);
		if (row == NULL)
			return ENOENT;
		return EAGAIN;
	}

	/* No value yet. */
	if (!entry->present)
		return EAGAIN;

	/* A value that does not fit is not cut. */
	length = strlen(entry->value);
	if (length + 1U > size)
		return ERANGE;

	/* Succeeded: the value and its flags. */
	memcpy(value, entry->value, length + 1U);
	if (flags != NULL)
		*flags = entry->flags;
	return 0;
}

/*
 * Adds a watch of the keys starting with prefix.  Returns 0, EINVAL for a
 * missing callback or a prefix too long, or ENOMEM when every watch is in
 * use.
 */
int
settings_cache_watch(
	struct settings_cache *cache,
	const char *prefix,
	kl_settings_watch_fn fn,
	void *data,
	unsigned *watch)
{
	struct settings_cache_watch *slot;
	size_t length;
	unsigned index;

	/* A callback and a prefix that fits. */
	if (fn == NULL || prefix == NULL)
		return EINVAL;
	length = strlen(prefix);
	if (length >= KL_SETTINGS_KEY_MAX)
		return EINVAL;

	/* A free slot. */
	slot = NULL;
	for (index = 0; index < SETTINGS_CACHE_WATCHES; index++) {
		if (!cache->watches[index].used) {
			slot = &cache->watches[index];
			break;
		}
	}
	if (slot == NULL)
		return ENOMEM;

	/*
	 * The watch.  added marks one made while the watches run: it hears
	 * the next change, not the one being told.
	 */
	memset(slot, 0, sizeof(*slot));
	memcpy(slot->prefix, prefix, length + 1U);
	slot->fn = fn;
	slot->data = data;
	slot->id = cache->next_watch;
	slot->used = 1;
	slot->added = cache->notifying;
	cache->next_watch++;
	if (cache->next_watch == 0U)
		cache->next_watch = 1;

	/* Succeeded: the watch's number. */
	if (watch != NULL)
		*watch = slot->id;
	return 0;
}

/*
 * Stops a watch; while the watches run it is only marked, and taken out
 * after the run.
 */
void
settings_cache_unwatch(
	struct settings_cache *cache,
	unsigned watch)
{
	unsigned index;

	/* The watch of that number. */
	for (index = 0; index < SETTINGS_CACHE_WATCHES; index++) {
		if (!cache->watches[index].used || cache->watches[index].id != watch)
			continue;

		/* removed keeps the run from calling it; the slot is freed now or after the run. */
		cache->watches[index].removed = 1;
		if (!cache->notifying)
			cache->watches[index].used = 0;
		return;
	}
}

/*
 * Runs the watches of the keys that changed since the last notify: once a
 * key, with its value in effect, and not when it is what the watches last
 * heard.
 */
void
settings_cache_notify(
	struct settings_cache *cache)
{
	struct settings_cache_entry *entry;
	struct settings_cache_watch *watch;
	const char *value;
	unsigned index;
	unsigned slot;
	int differs;
	int covers;

	/* While the watches run, an added watch waits and a stopped one is only marked. */
	cache->notifying = 1;

	/* Each key that changed. */
	for (index = 0; index < cache->count; index++) {
		entry = &cache->entries[index];
		if (!entry->changed)
			continue;
		entry->changed = 0;

		/* The same as the watches heard: nothing to tell. */
		differs = cache_entry_differs(entry);
		if (!differs)
			continue;

		/* What the watches hear now. */
		cache_copy(entry->told_value, sizeof(entry->told_value), entry->value);
		entry->told_flags = entry->flags;
		entry->told_present = entry->present;
		value = NULL;
		if (entry->present)
			value = entry->told_value;

		/* Each watch that covers the key, and was not added or stopped meanwhile. */
		for (slot = 0; slot < SETTINGS_CACHE_WATCHES; slot++) {
			watch = &cache->watches[slot];
			if (!watch->used || watch->removed || watch->added)
				continue;
			covers = cache_watch_covers(watch, entry->name);
			if (!covers)
				continue;
			watch->fn(watch->data, entry->name, value, entry->told_flags);
		}
	}

	/* The run is over: stopped watches go, added ones hear from now on. */
	for (slot = 0; slot < SETTINGS_CACHE_WATCHES; slot++) {
		watch = &cache->watches[slot];
		if (watch->removed)
			watch->used = 0;
		watch->added = 0;
	}
	cache->notifying = 0;
}

/*
 * Keeps a finished request for settings_cache_take_result; the oldest is
 * dropped when the ring is full.
 */
void
settings_cache_result(
	struct settings_cache *cache,
	uint32_t request,
	int error)
{
	unsigned place;

	/* A full ring drops its oldest. */
	if (cache->result_count == SETTINGS_CACHE_RESULTS) {
		cache->result_head = (cache->result_head + 1U) % SETTINGS_CACHE_RESULTS;
		cache->result_count--;
	}

	/* The result after the others. */
	place = (cache->result_head + cache->result_count) % SETTINGS_CACHE_RESULTS;
	cache->results[place].request = request;
	cache->results[place].error = error;
	cache->result_count++;
}

/*
 * Takes the oldest finished request.  Returns 1 with it, or 0 when none.
 */
int
settings_cache_take_result(
	struct settings_cache *cache,
	uint32_t *request,
	int *error)
{
	/* None waits. */
	if (cache->result_count == 0U)
		return 0;

	/* The oldest. */
	*request = cache->results[cache->result_head].request;
	*error = cache->results[cache->result_head].error;
	cache->result_head = (cache->result_head + 1U) % SETTINGS_CACHE_RESULTS;
	cache->result_count--;

	/* Succeeded: one result taken. */
	return 1;
}

/* Copies a string into room of its own, cut to fit. */
static void
cache_copy(
	char *to,
	size_t size,
	const char *from)
{
	size_t length;

	/* The bytes that fit, then the NUL. */
	length = strlen(from);
	if (length >= size)
		length = size - 1U;
	memmove(to, from, length);
	to[length] = '\0';
}

/* Tells whether an entry's value in effect differs from what the watches last heard. */
static int
cache_entry_differs(
	const struct settings_cache_entry *entry)
{
	int differs;

	/* Whether there is a value, and its flags. */
	if (entry->present != entry->told_present)
		return 1;
	if (entry->flags != entry->told_flags)
		return 1;

	/* Neither has a value. */
	if (!entry->present)
		return 0;

	/* Both have one: the same text or not. */
	differs = strcmp(entry->value, entry->told_value);
	if (differs != 0)
		return 1;

	/* The same. */
	return 0;
}

/* Tells whether a watch covers a key (the key starts with its prefix). */
static int
cache_watch_covers(
	const struct settings_cache_watch *watch,
	const char *key)
{
	size_t length;
	int differs;

	/* The prefix's characters at the key's start. */
	length = strlen(watch->prefix);
	differs = strncmp(key, watch->prefix, length);
	if (differs != 0)
		return 0;

	/* Covered. */
	return 1;
}

/* Makes the entry of a prefix row's key (none for another key, or when the cache is full); NULL without one. */
static struct settings_cache_entry *
cache_add(
	struct settings_cache *cache,
	const char *key)
{
	const struct kl_settings_key *row;
	struct settings_cache_entry *entry;

	/* Only a prefix row's key. */
	row = kl_settings_key_find(key);
	if (row == NULL || (row->flags & KL_SETTINGS_KEY_PREFIX) == 0U)
		return NULL;

	/* A full cache takes no more. */
	if (cache->count == SETTINGS_CACHE_KEYS)
		return NULL;

	/* The new entry, with no value yet. */
	entry = &cache->entries[cache->count];
	memset(entry, 0, sizeof(*entry));
	cache_copy(entry->name, sizeof(entry->name), key);
	entry->key = row;
	cache->count++;

	/* Succeeded: the key's entry. */
	return entry;
}
