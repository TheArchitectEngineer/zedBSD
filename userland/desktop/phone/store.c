/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Phone's contacts and timelines on the disk (WS170 p002, plan/ws170/
 * phase001/phase.md section 1), under a root (~/Documents/Phone):
 *
 *   contacts/<id>.vcf          a contact: vCard 3.0's FN and TEL
 *   messages/<id>/<name>.txt   one item of its timeline: "Key: value"
 *                              lines (Kind, Channel, Direction, Date,
 *                              State, Detail), an empty line, the words
 *
 * One file for each thing, so that a folder synchronized with the cloud
 * rarely has two machines write the same file; a change rewrites one
 * file, written beside it and renamed over it.  Everything is read when
 * the store opens: the contacts with the latest item first, each
 * timeline oldest first.  Only the window's thread touches the store.
 */

#include "phone.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The longest path, line and item file read, with its NUL. */
#define STORE_PATH_MAX		2048U
#define STORE_LINE_MAX		1024U
#define STORE_ROOT_MAX		512U
#define STORE_ITEM_MAX		65536U

/* The most contacts. */
#define STORE_CONTACTS_MAX	1024U

/* How many seconds a day has. */
#define STORE_DAY_SECONDS	86400L

/* The words of the kinds, channels and states in the files, in their enums' orders. */
static const char *const store_kinds[] = { "text", "call", "photo", "file" };
static const char *const store_channels[] = { "sms", "mms", "rcs", "line", "voip" };
static const char *const store_channel_words[] = { "SMS", "MMS", "RCS", "Phone", "VoIP" };
static const char *const store_states[PH_STATES] = { "", "unread", "read", "sending", "sent", "delivered", "failed", "answered", "missed", "no-answer" };

/* The days and months as words. */
static const char *const store_days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const store_months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* The contacts' colors, chosen by their names. */
static const kl_color store_colors[8] = {
	KL_RGB(0xf2994a), KL_RGB(0x56ccf2), KL_RGB(0xeb5757), KL_RGB(0x6fcf97),
	KL_RGB(0xbb6bd9), KL_RGB(0x2d9cdb), KL_RGB(0x828282), KL_RGB(0xf2c94c)
};

/* The root folder (empty while the store is closed). */
static char store_root[STORE_ROOT_MAX];

/* The contacts (allocated), how many, and the room. */
static struct ph_contact *store_contacts;
static size_t store_contact_count;
static size_t store_contact_capacity;

/* The number in the next file's name, so that two made in one second differ. */
static unsigned long store_serial;

static int store_load_contacts(void);
static int store_load_contact(const char *id);
static int store_load_items(long contact);
static int store_load_item(long contact, const char *path);
static int store_append_contact(const char *id, const char *name, const char *number);
static int store_append_item(long contact, const struct ph_item *item);
static int store_write_item(const struct ph_item *item, const char *path);
static int store_write_file(const char *path, const char *text, size_t length);
static int store_word(const char *const *words, size_t count, const char *word);
static void store_item_field(struct ph_item *item, const char *key, char *value);
static void store_words(struct ph_item *item, time_t now);
static void store_initials(const char *name, char *initials, size_t size);
static kl_color store_color(const char *name);
static void store_sort_contacts(void);
static time_t store_latest(const struct ph_contact *contact);
static void store_free_item(struct ph_item *item);
static char *store_copy(const char *text, int *failed);
static void store_digits(const char *number, char *digits, size_t size);
static const char *store_direction(int outgoing);
static int store_is_named(const char *name, const char *suffix);

/*
 * Opens the store at a root folder (made when it is not there) and reads
 * every contact and item.  Returns 0 or an errno value.
 */
int
ph_store_open(
	const char *root)
{
	char path[STORE_PATH_MAX];
	int error;

	/* The folders. */
	ph_store_close();
	(void)snprintf(store_root, sizeof(store_root), "%s", root);
	(void)mkdir(store_root, 0700);
	(void)snprintf(path, sizeof(path), "%s/contacts", store_root);
	(void)mkdir(path, 0700);
	(void)snprintf(path, sizeof(path), "%s/messages", store_root);
	(void)mkdir(path, 0700);

	/* Everything in them. */
	error = store_load_contacts();
	if (error != 0)
		return error;

	/* Succeeded: the latest contact first. */
	store_sort_contacts();
	return 0;
}

/*
 * Frees everything the store holds.
 */
void
ph_store_close(void)
{
	size_t contact;
	size_t item;

	/* Each contact and its items. */
	for (contact = 0; contact < store_contact_count; contact++) {
		for (item = 0; item < store_contacts[contact].item_count; item++)
			store_free_item(&store_contacts[contact].items[item]);
		free(store_contacts[contact].items);
		free(store_contacts[contact].id);
		free(store_contacts[contact].name);
		free(store_contacts[contact].number);
	}

	/* The array. */
	free(store_contacts);
	store_contacts = NULL;
	store_contact_count = 0;
	store_contact_capacity = 0;
	store_root[0] = '\0';
}

/*
 * Reports the contacts and how many there are.
 */
const struct ph_contact *
ph_contacts(
	size_t *count)
{
	/* The contacts read and added. */
	*count = store_contact_count;
	return store_contacts;
}

/*
 * Adds a contact, its file written; *index is where it is.  Returns 0,
 * EINVAL for an empty number, or an errno value.
 */
int
ph_store_add_contact(
	const char *name,
	const char *number,
	long *index)
{
	char id[64];
	char path[STORE_PATH_MAX];
	char text[STORE_LINE_MAX * 2U];
	int length;
	int error;

	/* A number is needed; the name may be the number. */
	if (number[0] == '\0')
		return EINVAL;
	if (name[0] == '\0')
		name = number;

	/* Its ID and file. */
	store_serial++;
	(void)snprintf(id, sizeof(id), "c%lld-%lu", (long long)time(NULL), store_serial);
	(void)snprintf(path, sizeof(path), "%s/contacts/%s.vcf", store_root, id);
	length = snprintf(text, sizeof(text), "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:%s\r\nTEL:%s\r\nEND:VCARD\r\n", name, number);
	if (length < 0 || (size_t)length >= sizeof(text))
		return E2BIG;
	error = store_write_file(path, text, (size_t)length);
	if (error != 0)
		return error;

	/* Its folder of items. */
	(void)snprintf(path, sizeof(path), "%s/messages/%s", store_root, id);
	(void)mkdir(path, 0700);

	/* Kept at the end. */
	error = store_append_contact(id, name, number);
	if (error != 0)
		return error;

	/* Succeeded: the contact is the last. */
	*index = (long)store_contact_count - 1L;
	return 0;
}

/*
 * Finds the contact of a number (its digits compared, a leading "+" or
 * spaces aside); its index, or -1.
 */
long
ph_store_find_number(
	const char *number)
{
	char wanted[64];
	char digits[64];
	size_t index;
	int same;

	/* The number's digits. */
	store_digits(number, wanted, sizeof(wanted));
	if (wanted[0] == '\0')
		return -1;

	/* Each contact's. */
	for (index = 0; index < store_contact_count; index++) {
		store_digits(store_contacts[index].number, digits, sizeof(digits));
		same = strcmp(digits, wanted);
		if (same == 0)
			return (long)index;
	}

	/* None. */
	return -1;
}

/*
 * Adds an item to a contact's timeline, its file written; *item is where
 * it is in the timeline.  Returns 0 or an errno value.
 */
int
ph_store_add_item(
	long contact,
	enum ph_kind kind,
	enum ph_channel channel,
	int outgoing,
	time_t date,
	enum ph_state state,
	const char *text,
	const char *detail,
	size_t *item)
{
	struct ph_item made;
	char path[STORE_PATH_MAX];
	int error;

	/* A contact that is there. */
	if (contact < 0 || (size_t)contact >= store_contact_count)
		return EINVAL;

	/* Its file's name: the time, the number of this run, and the direction. */
	store_serial++;
	(void)snprintf(path, sizeof(path), "%s/messages/%s/%lld-%lu-%s.txt", store_root, store_contacts[contact].id, (long long)date, store_serial, store_direction(outgoing));

	/* The item, written. */
	memset(&made, 0, sizeof(made));
	made.kind = kind;
	made.channel = channel;
	made.outgoing = outgoing;
	made.date = date;
	made.state = state;
	made.text = (char *)text;
	made.detail = (char *)detail;
	made.path = path;
	error = store_write_item(&made, path);
	if (error != 0)
		return error;

	/* Kept at the end of the timeline. */
	error = store_append_item(contact, &made);
	if (error != 0)
		return error;

	/* Succeeded: the item is the last. */
	*item = store_contacts[contact].item_count - 1U;
	return 0;
}

/*
 * Changes an item's state (and detail, unless NULL), its file written
 * again.
 */
int
ph_store_set_state(
	long contact,
	size_t item,
	enum ph_state state,
	const char *detail)
{
	struct ph_item *kept;
	char *copied;
	int failed;
	int error;

	/* An item that is there. */
	if (contact < 0 || (size_t)contact >= store_contact_count)
		return EINVAL;
	if (item >= store_contacts[contact].item_count)
		return EINVAL;
	kept = &store_contacts[contact].items[item];

	/* The new state and detail. */
	kept->state = state;
	if (detail != NULL) {
		failed = 0;
		copied = store_copy(detail, &failed);
		if (failed)
			return ENOMEM;
		free(kept->detail);
		kept->detail = copied;
	}

	/* Its file, written again. */
	error = store_write_item(kept, kept->path);
	if (error != 0)
		return error;

	/* The words shown. */
	store_words(kept, time(NULL));
	return 0;
}

/*
 * Marks a contact's unread messages read (their files written again).
 */
int
ph_store_mark_read(
	long contact)
{
	struct ph_contact *kept;
	size_t index;
	int error;

	/* A contact that is there. */
	if (contact < 0 || (size_t)contact >= store_contact_count)
		return EINVAL;
	kept = &store_contacts[contact];

	/* Each unread item. */
	for (index = 0; index < kept->item_count; index++) {
		if (kept->items[index].state != PH_STATE_UNREAD)
			continue;
		error = ph_store_set_state(contact, index, PH_STATE_READ, NULL);
		if (error != 0)
			return error;
	}

	/* Succeeded: nothing unread. */
	kept->unread = 0;
	return 0;
}

/*
 * Reports a channel's name as the view shows it.
 */
const char *
ph_channel_word(
	enum ph_channel channel)
{
	/* A channel not known. */
	if ((unsigned)channel >= sizeof(store_channel_words) / sizeof(store_channel_words[0]))
		return "";

	/* Its name. */
	return store_channel_words[channel];
}

/* Reads every contact's file. */
static int
store_load_contacts(void)
{
	char path[STORE_PATH_MAX];
	char id[256];
	struct dirent *entry;
	size_t length;
	DIR *folder;
	int named;
	int error;

	/* The folder. */
	(void)snprintf(path, sizeof(path), "%s/contacts", store_root);
	folder = opendir(path);
	if (folder == NULL)
		return errno;

	/* Each .vcf file. */
	error = 0;
	for (;;) {
		entry = readdir(folder);
		if (entry == NULL)
			break;

		/* Another file, or a name too long for an ID. */
		named = store_is_named(entry->d_name, ".vcf");
		length = strlen(entry->d_name);
		if (!named || length - 4U >= sizeof(id))
			continue;

		/* The contact of its ID. */
		memcpy(id, entry->d_name, length - 4U);
		id[length - 4U] = '\0';
		error = store_load_contact(id);
		if (error != 0)
			break;
	}

	/* The folder is read. */
	closedir(folder);

	/* A failure to keep one. */
	if (error != 0)
		return error;

	/* Succeeded: every contact is read. */
	return 0;
}

/* Reads one contact's file and its items. */
static int
store_load_contact(
	const char *id)
{
	char path[STORE_PATH_MAX];
	char line[STORE_LINE_MAX];
	char name[STORE_LINE_MAX];
	char number[STORE_LINE_MAX];
	char *colon;
	char *end;
	FILE *file;
	int same;
	int error;

	/* The file. */
	(void)snprintf(path, sizeof(path), "%s/contacts/%s.vcf", store_root, id);
	file = fopen(path, "r");
	if (file == NULL)
		return 0;

	/* Its FN and TEL lines (a parameter after TEL, "TEL;TYPE=cell:", is allowed). */
	name[0] = '\0';
	number[0] = '\0';
	for (;;) {
		end = fgets(line, (int)sizeof(line), file);
		if (end == NULL)
			break;

		/* The line without its end, and its value after the colon. */
		line[strcspn(line, "\r\n")] = '\0';
		colon = strchr(line, ':');
		if (colon == NULL)
			continue;
		*colon = '\0';

		/* The name. */
		same = strcmp(line, "FN");
		if (same == 0) {
			(void)snprintf(name, sizeof(name), "%s", colon + 1);
			continue;
		}

		/* The number, with or without parameters. */
		same = strncmp(line, "TEL", 3U);
		if (same == 0)
			(void)snprintf(number, sizeof(number), "%s", colon + 1);
	}

	/* The file is read. */
	fclose(file);

	/* A contact without a number is not one. */
	if (number[0] == '\0')
		return 0;
	if (name[0] == '\0')
		(void)snprintf(name, sizeof(name), "%s", number);

	/* Kept, then its items. */
	error = store_append_contact(id, name, number);
	if (error != 0)
		return error;
	error = store_load_items((long)store_contact_count - 1L);
	if (error != 0)
		return error;

	/* Succeeded: the contact is read. */
	return 0;
}

/* Reads a contact's items, oldest first. */
static int
store_load_items(
	long contact)
{
	struct ph_contact *kept;
	struct ph_item moved;
	char path[STORE_PATH_MAX];
	char item_path[STORE_PATH_MAX + 256U + 2U];
	struct dirent *entry;
	size_t i;
	size_t at;
	DIR *folder;
	int named;
	int error;

	/* The folder (a contact without one has no items). */
	(void)snprintf(path, sizeof(path), "%s/messages/%s", store_root, store_contacts[contact].id);
	folder = opendir(path);
	if (folder == NULL)
		return 0;

	/* Each .txt file. */
	error = 0;
	for (;;) {
		entry = readdir(folder);
		if (entry == NULL)
			break;
		named = store_is_named(entry->d_name, ".txt");
		if (!named)
			continue;

		/* The item of the file. */
		(void)snprintf(item_path, sizeof(item_path), "%s/%s", path, entry->d_name);
		error = store_load_item(contact, item_path);
		if (error != 0)
			break;
	}

	/* The folder is read; a failure to keep an item ends the reading. */
	closedir(folder);
	if (error != 0)
		return error;

	/* Oldest first: each moved back past the newer ones before it. */
	kept = &store_contacts[contact];
	for (i = 1; i < kept->item_count; i++) {
		moved = kept->items[i];
		at = i;
		while (at > 0U && kept->items[at - 1U].date > moved.date) {
			kept->items[at] = kept->items[at - 1U];
			at--;
		}

		/* Its place. */
		kept->items[at] = moved;
	}

	/* Succeeded: the timeline is read. */
	return 0;
}

/* Reads one item's file. */
static int
store_load_item(
	long contact,
	const char *path)
{
	struct ph_item item;
	char *text;
	char *line;
	char *next;
	char *colon;
	char *body;
	size_t length;
	FILE *file;
	int error;

	/* The file's bytes. */
	file = fopen(path, "r");
	if (file == NULL)
		return 0;
	text = malloc(STORE_ITEM_MAX);
	if (text == NULL) {
		fclose(file);
		return ENOMEM;
	}

	/* Up to the most an item keeps. */
	length = fread(text, 1U, STORE_ITEM_MAX - 1U, file);
	fclose(file);
	text[length] = '\0';

	/* The header's lines up to the empty one. */
	memset(&item, 0, sizeof(item));
	item.path = (char *)path;
	body = NULL;
	for (line = text; line != NULL && *line != '\0'; line = next) {
		next = strchr(line, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}

		/* Without a CR. */
		line[strcspn(line, "\r")] = '\0';

		/* The empty line: the words after it. */
		if (line[0] == '\0') {
			body = next;
			break;
		}

		/* A key and its value. */
		colon = strchr(line, ':');
		if (colon == NULL)
			continue;
		*colon = '\0';
		colon++;
		while (*colon == ' ')
			colon++;
		store_item_field(&item, line, colon);
	}

	/* The words, without the line end at their end. */
	if (body != NULL) {
		length = strlen(body);
		while (length > 0U && (body[length - 1U] == '\n' || body[length - 1U] == '\r'))
			length--;
		body[length] = '\0';
		if (length != 0U)
			item.text = body;
	}

	/* Kept. */
	error = store_append_item(contact, &item);
	free(text);
	if (error != 0)
		return error;

	/* Succeeded: the item is read. */
	return 0;
}

/* Appends a contact (its strings copied, its initials and color made). */
static int
store_append_contact(
	const char *id,
	const char *name,
	const char *number)
{
	struct ph_contact *grown;
	struct ph_contact *kept;
	size_t capacity;
	int failed;

	/* Room for one more. */
	if (store_contact_count == STORE_CONTACTS_MAX)
		return ENOSPC;
	if (store_contact_count == store_contact_capacity) {
		capacity = store_contact_capacity * 2U;
		if (capacity == 0U)
			capacity = 16U;
		grown = realloc(store_contacts, capacity * sizeof(grown[0]));
		if (grown == NULL)
			return ENOMEM;
		store_contacts = grown;
		store_contact_capacity = capacity;
	}

	/* The contact. */
	kept = &store_contacts[store_contact_count];
	memset(kept, 0, sizeof(*kept));
	failed = 0;
	kept->id = store_copy(id, &failed);
	kept->name = store_copy(name, &failed);
	kept->number = store_copy(number, &failed);
	if (failed) {
		free(kept->id);
		free(kept->name);
		free(kept->number);
		return ENOMEM;
	}

	/* The picture standing for the person. */
	store_initials(name, kept->initials, sizeof(kept->initials));
	kept->color = store_color(name);

	/* Succeeded: one more contact. */
	store_contact_count++;
	return 0;
}

/* Appends an item to a contact's timeline (its strings copied, its words made). */
static int
store_append_item(
	long contact,
	const struct ph_item *item)
{
	struct ph_contact *kept;
	struct ph_item *grown;
	struct ph_item *added;
	size_t capacity;
	int failed;

	/* Room for one more. */
	kept = &store_contacts[contact];
	if (kept->item_count == kept->item_capacity) {
		capacity = kept->item_capacity * 2U;
		if (capacity == 0U)
			capacity = 16U;
		grown = realloc(kept->items, capacity * sizeof(grown[0]));
		if (grown == NULL)
			return ENOMEM;
		kept->items = grown;
		kept->item_capacity = capacity;
	}

	/* The item, its strings of its own. */
	added = &kept->items[kept->item_count];
	*added = *item;
	added->day = NULL;
	added->time = NULL;
	failed = 0;
	added->text = store_copy(item->text, &failed);
	added->detail = store_copy(item->detail, &failed);
	added->path = store_copy(item->path, &failed);
	if (failed) {
		store_free_item(added);
		return ENOMEM;
	}

	/* The day and time as words. */
	store_words(added, time(NULL));

	/* Counted; an unread one is counted for the list's dot. */
	kept->item_count++;
	if (added->state == PH_STATE_UNREAD)
		kept->unread++;
	return 0;
}

/* Writes an item's file. */
static int
store_write_item(
	const struct ph_item *item,
	const char *path)
{
	const char *detail;
	const char *words;
	char *text;
	int length;
	int error;

	/* The header and the words. */
	text = malloc(STORE_ITEM_MAX);
	if (text == NULL)
		return ENOMEM;
	detail = "";
	if (item->detail != NULL)
		detail = item->detail;
	words = "";
	if (item->text != NULL)
		words = item->text;
	if (item->detail != NULL) {
		length = snprintf(text, STORE_ITEM_MAX, "Kind: %s\nChannel: %s\nDirection: %s\nDate: %lld\nState: %s\nDetail: %s\n\n%s\n",
		    store_kinds[item->kind], store_channels[item->channel], store_direction(item->outgoing), (long long)item->date,
		    store_states[item->state], detail, words);
	} else {
		length = snprintf(text, STORE_ITEM_MAX, "Kind: %s\nChannel: %s\nDirection: %s\nDate: %lld\nState: %s\n\n%s\n",
		    store_kinds[item->kind], store_channels[item->channel], store_direction(item->outgoing), (long long)item->date,
		    store_states[item->state], words);
	}

	/* An item larger than a file keeps. */
	if (length < 0 || (size_t)length >= STORE_ITEM_MAX) {
		free(text);
		return E2BIG;
	}

	/* Written. */
	error = store_write_file(path, text, (size_t)length);
	free(text);
	if (error != 0)
		return error;

	/* Succeeded: the file holds the item. */
	return 0;
}

/* Writes a file beside its place and renames it over it. */
static int
store_write_file(
	const char *path,
	const char *text,
	size_t length)
{
	char fresh[STORE_PATH_MAX + 8U];
	size_t written;
	FILE *file;
	int status;

	/* The new file. */
	(void)snprintf(fresh, sizeof(fresh), "%s.new", path);
	file = fopen(fresh, "w");
	if (file == NULL)
		return errno;
	written = fwrite(text, 1U, length, file);
	status = fclose(file);
	if (written != length || status != 0)
		return EIO;

	/* In place. */
	status = rename(fresh, path);
	if (status != 0)
		return errno;

	/* Succeeded: the file is written. */
	return 0;
}

/* Sets an item's field from a header line's key and value (an unknown key or value is skipped). */
static void
store_item_field(
	struct ph_item *item,
	const char *key,
	char *value)
{
	int found;
	int same;

	/* Kind. */
	same = strcmp(key, "Kind");
	if (same == 0) {
		found = store_word(store_kinds, 4U, value);
		if (found >= 0)
			item->kind = (enum ph_kind)found;
		return;
	}

	/* Channel. */
	same = strcmp(key, "Channel");
	if (same == 0) {
		found = store_word(store_channels, 5U, value);
		if (found >= 0)
			item->channel = (enum ph_channel)found;
		return;
	}

	/* Direction. */
	same = strcmp(key, "Direction");
	if (same == 0) {
		same = strcmp(value, "out");
		item->outgoing = 0;
		if (same == 0)
			item->outgoing = 1;
		return;
	}

	/* Date. */
	same = strcmp(key, "Date");
	if (same == 0) {
		item->date = (time_t)strtoll(value, NULL, 10);
		return;
	}

	/* State. */
	same = strcmp(key, "State");
	if (same == 0) {
		found = store_word(store_states, PH_STATES, value);
		if (found >= 0)
			item->state = (enum ph_state)found;
		return;
	}

	/* Detail (the line's own text, copied when the item is kept). */
	same = strcmp(key, "Detail");
	if (same == 0)
		item->detail = value;
}

/* Finds a word in a table; its index, or -1. */
static int
store_word(
	const char *const *words,
	size_t count,
	const char *word)
{
	size_t index;
	int same;

	/* Each word. */
	for (index = 0; index < count; index++) {
		same = strcmp(words[index], word);
		if (same == 0)
			return (int)index;
	}

	/* Not known. */
	return -1;
}

/* Makes an item's day and time as words ("Today", "Yesterday", "Sat, 3 Oct"; "09:41") and an outgoing message's detail from its state. */
static void
store_words(
	struct ph_item *item,
	time_t now)
{
	struct tm when;
	struct tm today;
	char day[32];
	char clock[16];
	const char *state;
	char *copied;
	int failed;

	/* The date and today in the local time. */
	(void)localtime_r(&item->date, &when);
	(void)localtime_r(&now, &today);

	/* The day. */
	if (when.tm_year == today.tm_year && when.tm_yday == today.tm_yday)
		(void)snprintf(day, sizeof(day), "Today");
	else if (when.tm_year == today.tm_year && today.tm_yday - when.tm_yday == 1)
		(void)snprintf(day, sizeof(day), "Yesterday");
	else
		(void)snprintf(day, sizeof(day), "%s, %d %s", store_days[when.tm_wday], when.tm_mday, store_months[when.tm_mon]);
	(void)snprintf(clock, sizeof(clock), "%02d:%02d", when.tm_hour, when.tm_min);

	/* Kept (an old pair is freed). */
	failed = 0;
	free(item->day);
	free(item->time);
	item->day = store_copy(day, &failed);
	item->time = store_copy(clock, &failed);

	/* An outgoing message's detail is its state. */
	if (item->kind != PH_TEXT || !item->outgoing)
		return;
	state = NULL;
	if (item->state == PH_STATE_SENDING)
		state = "Sending";
	else if (item->state == PH_STATE_SENT)
		state = "Sent";
	else if (item->state == PH_STATE_DELIVERED)
		state = "Delivered";
	else if (item->state == PH_STATE_FAILED)
		state = "Not delivered";
	if (state == NULL)
		return;
	copied = store_copy(state, &failed);
	if (copied == NULL)
		return;
	free(item->detail);
	item->detail = copied;
}

/* Makes the initials of a name: the first character of its first and last words (one for one word). */
static void
store_initials(
	const char *name,
	char *initials,
	size_t size)
{
	const char *last;
	size_t first_length;
	size_t last_length;

	/* The first character (all of its UTF-8 bytes). */
	first_length = 1;
	while (name[first_length] != '\0' && ((unsigned char)name[first_length] & 0xc0U) == 0x80U)
		first_length++;

	/* The last word's first character, when there is a second word. */
	last = strrchr(name, ' ');
	last_length = 0;
	if (last != NULL && last[1] != '\0') {
		last++;
		last_length = 1;
		while (last[last_length] != '\0' && ((unsigned char)last[last_length] & 0xc0U) == 0x80U)
			last_length++;
	}

	/* Both, when they fit. */
	initials[0] = '\0';
	if (first_length + last_length >= size)
		return;
	memcpy(initials, name, first_length);
	if (last_length != 0U)
		memcpy(initials + first_length, last, last_length);
	initials[first_length + last_length] = '\0';
}

/* Chooses a contact's color by its name. */
static kl_color
store_color(
	const char *name)
{
	unsigned long hash;
	size_t index;

	/* A small hash of the name. */
	hash = 5381UL;
	for (index = 0; name[index] != '\0'; index++)
		hash = hash * 33UL + (unsigned char)name[index];

	/* One of the colors. */
	return store_colors[hash % 8UL];
}

/* Sorts the contacts by their latest item, the latest first (those without items last). */
static void
store_sort_contacts(void)
{
	struct ph_contact moved;
	time_t moved_latest;
	time_t before_latest;
	size_t i;
	size_t at;

	/* Each moved up past the older ones before it. */
	for (i = 1; i < store_contact_count; i++) {
		moved = store_contacts[i];
		at = i;
		moved_latest = store_latest(&moved);
		while (at > 0U) {
			/* A later one before it stays before it. */
			before_latest = store_latest(&store_contacts[at - 1U]);
			if (before_latest >= moved_latest)
				break;
			store_contacts[at] = store_contacts[at - 1U];
			at--;
		}

		/* Its place. */
		store_contacts[at] = moved;
	}
}

/* Reports the date of a contact's latest item (0 for none). */
static time_t
store_latest(
	const struct ph_contact *contact)
{
	/* No item. */
	if (contact->item_count == 0U)
		return 0;

	/* The last of the timeline. */
	return contact->items[contact->item_count - 1U].date;
}

/* Frees an item's strings. */
static void
store_free_item(
	struct ph_item *item)
{
	/* Each string (free takes NULL). */
	free(item->day);
	free(item->time);
	free(item->text);
	free(item->detail);
	free(item->path);
}

/* Copies a string (NULL stays NULL); a failure is noted. */
static char *
store_copy(
	const char *text,
	int *failed)
{
	char *copied;
	size_t length;

	/* Nothing to copy. */
	if (text == NULL)
		return NULL;

	/* The copy. */
	length = strlen(text);
	copied = malloc(length + 1U);
	if (copied == NULL) {
		*failed = 1;
		return NULL;
	}

	/* Its bytes and NUL. */
	memcpy(copied, text, length + 1U);
	return copied;
}

/* Keeps the digits of a number. */
static void
store_digits(
	const char *number,
	char *digits,
	size_t size)
{
	size_t length;
	size_t index;

	/* Each digit. */
	length = 0;
	for (index = 0; number[index] != '\0' && length + 1U < size; index++) {
		if (number[index] >= '0' && number[index] <= '9') {
			digits[length] = number[index];
			length++;
		}
	}

	/* Their end. */
	digits[length] = '\0';
}

/* Reports the word of an item's direction in its file and name. */
static const char *
store_direction(
	int outgoing)
{
	/* Out. */
	if (outgoing)
		return "out";

	/* In. */
	return "in";
}

/* Tells whether a file's name ends with a suffix (and has something before it). */
static int
store_is_named(
	const char *name,
	const char *suffix)
{
	size_t length;
	size_t suffix_length;
	int same;

	/* Longer than the suffix. */
	length = strlen(name);
	suffix_length = strlen(suffix);
	if (length <= suffix_length)
		return 0;

	/* Ending with it. */
	same = strcmp(name + length - suffix_length, suffix);
	if (same != 0)
		return 0;

	/* Named so. */
	return 1;
}
