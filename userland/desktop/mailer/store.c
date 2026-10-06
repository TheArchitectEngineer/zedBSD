/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail's store of the run (WS169 p004; mailer.h): the accounts, and the
 * messages the servers gave, each with its own copies of its strings.
 * Messages are only appended (a moved one is hidden by its folder
 * ML_FOLDERS), so that an index the view keeps names the same message for
 * the run; the list sorts them by date as it shows them.  Only the
 * window's thread touches the store.
 */

#include "mailer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* How many seconds a day has, for the short date's "Yesterday" and the days of the week. */
#define STORE_DAY_SECONDS	86400L

/* The folders' names. */
static const char *const store_folders[ML_FOLDERS] = { "Inbox", "Sent", "Drafts", "Archive", "Trash" };

/* The days and months of the dates as words. */
static const char *const store_days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const store_months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

/* The senders' colors, chosen by their address. */
static const kl_color store_colors[8] = {
	KL_RGB(0x2d9cdb), KL_RGB(0xf2994a), KL_RGB(0x56ccf2), KL_RGB(0x6fcf97),
	KL_RGB(0xbb6bd9), KL_RGB(0xeb5757), KL_RGB(0x828282), KL_RGB(0x2f7cf6)
};

/* The accounts, in the order they were added (the thread's too); store_account_count of them are set. */
static struct ml_account_config store_accounts[ML_ACCOUNTS_MAX];
static size_t store_account_count;

/* The messages (allocated, grown as they come), how many there are, and the room. */
static struct ml_message *store_messages;
static size_t store_message_count;
static size_t store_message_capacity;

static char *store_copy(const char *text, int *failed);
static void store_free_message(struct ml_message *message);
static void store_dates(time_t date, time_t now, char *short_text, size_t short_size, char *long_text, size_t long_size);
static kl_color store_color(const char *address);

/*
 * Reports the accounts and how many there are.
 */
const struct ml_account_config *
ml_accounts(
	size_t *count)
{
	/* The accounts added. */
	*count = store_account_count;
	return store_accounts;
}

/*
 * Reports the messages and how many there are.
 */
const struct ml_message *
ml_messages(
	size_t *count)
{
	/* The messages kept. */
	*count = store_message_count;
	return store_messages;
}

/*
 * Reports a folder's name.
 */
const char *
ml_folder_name(
	enum ml_folder folder)
{
	/* A folder not known. */
	if ((unsigned)folder >= ML_FOLDERS)
		return "";

	/* Its name. */
	return store_folders[folder];
}

/*
 * Adds an account; returns its index, or -1 when there are already
 * ML_ACCOUNTS_MAX.
 */
int
ml_store_add_account(
	const struct ml_account_config *config)
{
	int index;

	/* No room. */
	if (store_account_count == ML_ACCOUNTS_MAX)
		return -1;

	/* The next. */
	index = (int)store_account_count;
	store_accounts[index] = *config;
	store_account_count++;
	return index;
}

/*
 * Keeps a message, its strings copied.  Returns 0 or ENOMEM.
 */
int
ml_store_insert(
	const struct ml_message *message)
{
	struct ml_message *grown;
	struct ml_message *kept;
	size_t capacity;
	int failed;

	/* Room for one more. */
	if (store_message_count == store_message_capacity) {
		capacity = store_message_capacity * 2U;
		if (capacity == 0U)
			capacity = 64U;
		grown = realloc(store_messages, capacity * sizeof(grown[0]));
		if (grown == NULL)
			return ENOMEM;
		store_messages = grown;
		store_message_capacity = capacity;
	}

	/* The message, then its strings of its own (NULL stays NULL). */
	kept = &store_messages[store_message_count];
	*kept = *message;
	failed = 0;
	kept->from_name = store_copy(message->from_name, &failed);
	kept->from_address = store_copy(message->from_address, &failed);
	kept->to = store_copy(message->to, &failed);
	kept->cc = store_copy(message->cc, &failed);
	kept->subject = store_copy(message->subject, &failed);
	kept->date_short = store_copy(message->date_short, &failed);
	kept->date_long = store_copy(message->date_long, &failed);
	kept->body = store_copy(message->body, &failed);
	kept->file_name = store_copy(message->file_name, &failed);
	kept->file_detail = store_copy(message->file_detail, &failed);
	kept->code = store_copy(message->code, &failed);
	kept->message_id = store_copy(message->message_id, &failed);
	if (failed) {
		store_free_message(kept);
		return ENOMEM;
	}

	/* Succeeded: one more message. */
	store_message_count++;
	return 0;
}

/*
 * Keeps a message a server gave (read by mime.c): its dates as words from
 * now, its sender's color, its file's detail.  Returns 0 or ENOMEM.
 */
int
ml_store_add_parsed(
	int account,
	enum ml_folder folder,
	uint32_t uid,
	unsigned flags,
	const struct ml_parsed *parsed,
	time_t now)
{
	struct ml_message message;
	char date_short[32];
	char date_long[96];
	char detail[64];
	int error;

	/* What the message is. */
	memset(&message, 0, sizeof(message));
	message.account = account;
	message.folder = folder;
	message.uid = uid;
	message.flags = flags;
	message.date = parsed->date;
	message.from_name = (char *)parsed->from_name;
	message.from_address = (char *)parsed->from_address;
	message.color = store_color(parsed->from_address);
	message.to = (char *)parsed->to;
	message.cc = (char *)parsed->cc;
	message.subject = (char *)parsed->subject;
	message.body = parsed->body;
	message.message_id = (char *)parsed->message_id;

	/* Its dates as words. */
	store_dates(parsed->date, now, date_short, sizeof(date_short), date_long, sizeof(date_long));
	message.date_short = date_short;
	message.date_long = date_long;

	/* The file it carries, with its size in KB or MB. */
	if (parsed->file_name[0] != '\0') {
		message.flags |= ML_ATTACHMENT;
		message.file_name = (char *)parsed->file_name;
		if (parsed->file_size >= 1048576U)
			(void)snprintf(detail, sizeof(detail), "File, %.1f MB", (double)parsed->file_size / 1048576.0);
		else
			(void)snprintf(detail, sizeof(detail), "File, %lu KB", (unsigned long)((parsed->file_size + 1023U) / 1024U));
		message.file_detail = detail;
	}

	/* The sign-in code. */
	if (parsed->code[0] != '\0')
		message.code = (char *)parsed->code;

	/* Kept. */
	error = ml_store_insert(&message);
	if (error != 0)
		return error;

	/* Succeeded: the message is shown from the next frame. */
	return 0;
}

/*
 * Finds a message by its account, folder and UID; its index, or -1.
 */
long
ml_store_find(
	int account,
	enum ml_folder folder,
	uint32_t uid)
{
	size_t index;

	/* Each message. */
	for (index = 0; index < store_message_count; index++) {
		if (store_messages[index].account != account)
			continue;
		if (store_messages[index].folder != folder)
			continue;
		if (store_messages[index].uid == uid)
			return (long)index;
	}

	/* None. */
	return -1;
}

/*
 * Reports the highest UID kept of an account's folder (0 for none), after
 * which a fetch asks for new messages.
 */
uint32_t
ml_store_last_uid(
	int account,
	enum ml_folder folder)
{
	uint32_t highest;
	size_t index;

	/* Each message of the folder. */
	highest = 0;
	for (index = 0; index < store_message_count; index++) {
		if (store_messages[index].account != account)
			continue;
		if (store_messages[index].folder != folder)
			continue;
		if (store_messages[index].uid > highest)
			highest = store_messages[index].uid;
	}

	/* The highest. */
	return highest;
}

/*
 * Gives a message to change (its flags, its folder); NULL for an index
 * not there.
 */
struct ml_message *
ml_store_at(
	long index)
{
	/* Not there. */
	if (index < 0 || (size_t)index >= store_message_count)
		return NULL;

	/* The message. */
	return &store_messages[index];
}

/*
 * Frees every message.
 */
void
ml_store_release(void)
{
	size_t index;

	/* Each message's strings, then the array. */
	for (index = 0; index < store_message_count; index++)
		store_free_message(&store_messages[index]);
	free(store_messages);
	store_messages = NULL;
	store_message_count = 0;
	store_message_capacity = 0;
	store_account_count = 0;
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

	/* The bytes and the NUL. */
	memcpy(copied, text, length + 1U);
	return copied;
}

/* Frees a message's strings. */
static void
store_free_message(
	struct ml_message *message)
{
	/* Each string (free takes NULL). */
	free(message->from_name);
	free(message->from_address);
	free(message->to);
	free(message->cc);
	free(message->subject);
	free(message->date_short);
	free(message->date_long);
	free(message->body);
	free(message->file_name);
	free(message->file_detail);
	free(message->code);
	free(message->message_id);
}

/*
 * Writes a date as words in the local time: short ("09:41" today,
 * "Yesterday", "Sat" this week, "Mon 28 Sep" this year, "28 Sep 2025"
 * before) and long ("Monday, 5 October 2026 at 09:41").
 */
static void
store_dates(
	time_t date,
	time_t now,
	char *short_text,
	size_t short_size,
	char *long_text,
	size_t long_size)
{
	struct tm when;
	struct tm today;
	long days;

	/* No date. */
	if (date == 0) {
		(void)snprintf(short_text, short_size, "%s", "");
		(void)snprintf(long_text, long_size, "%s", "");
		return;
	}

	/* The date and today in the local time. */
	(void)localtime_r(&date, &when);
	(void)localtime_r(&now, &today);

	/* The long form. */
	(void)snprintf(long_text, long_size, "%s, %d %s %d at %02d:%02d",
	    store_days[when.tm_wday], when.tm_mday, store_months[when.tm_mon], when.tm_year + 1900, when.tm_hour, when.tm_min);

	/* Today: the time. */
	if (when.tm_year == today.tm_year && when.tm_yday == today.tm_yday) {
		(void)snprintf(short_text, short_size, "%02d:%02d", when.tm_hour, when.tm_min);
		return;
	}

	/* Yesterday, and this week by its day. */
	days = (long)(now - date) / STORE_DAY_SECONDS;
	if (when.tm_year == today.tm_year && today.tm_yday - when.tm_yday == 1) {
		(void)snprintf(short_text, short_size, "Yesterday");
		return;
	}

	/* Within the week: the day's name. */
	if (days >= 0L && days < 6L) {
		(void)snprintf(short_text, short_size, "%.3s", store_days[when.tm_wday]);
		return;
	}

	/* This year, by its day and month; else with its year. */
	if (when.tm_year == today.tm_year)
		(void)snprintf(short_text, short_size, "%.3s %d %.3s", store_days[when.tm_wday], when.tm_mday, store_months[when.tm_mon]);
	else
		(void)snprintf(short_text, short_size, "%d %.3s %d", when.tm_mday, store_months[when.tm_mon], when.tm_year + 1900);
}

/* Chooses a sender's color by its address. */
static kl_color
store_color(
	const char *address)
{
	unsigned long hash;
	size_t index;

	/* A small hash of the address. */
	hash = 5381UL;
	for (index = 0; address[index] != '\0'; index++)
		hash = hash * 33UL + (unsigned char)address[index];

	/* One of the colors. */
	return store_colors[hash % 8UL];
}
