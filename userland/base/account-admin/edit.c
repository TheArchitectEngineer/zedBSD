/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * account-admin's pure part (ws089-p026; edit.h).
 *
 * The account files are lines of fields split by ':' (passwd: name,
 * password, user ID, group ID, display name, home, shell; group: name,
 * password, group ID, members split by ','; shadow: name, hash and the
 * ageing fields).  An edit copies the text into an output buffer line by
 * line, changing only the lines it is about; every other byte is kept.
 * The output always ends with a line end when it is not empty.
 */

#include "edit.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The words of the operations, in their order. */
static const char *const edit_operations[] = { "add", "remove", "reset-password", "group-add", "group-remove", "system-language" };

/* The languages the login screen may be in (ws158-p004; the compositor's language.c knows them). */
static const char *const edit_languages[] = { "en", "ja" };

/* The words of the reasons, in their order (ADMIN_OK's is "ok"). */
static const char *const edit_reasons[] = {
	"ok",
	"bad-request",
	"not-administrator",
	"bad-password",
	"no-such-user",
	"name-taken",
	"bad-name",
	"weak-password",
	"last-administrator",
	"self",
	"root",
	"busy",
	"home-exists",
	"failed"
};

/*
 * A line of a text being walked: where it starts, its length without its
 * end, and where the next one starts.
 */
struct edit_line {
	const char *start;
	size_t length;
	size_t next;
};

static int edit_next_line(const char *text, size_t length, size_t at, struct edit_line *line);
static int edit_line_field(const char *line, size_t length, unsigned index, const char **field, size_t *field_length);
static int edit_line_named(const struct edit_line *line, const char *name);
static long edit_number(const char *text, size_t length);
static int edit_put(char *output, size_t capacity, size_t *used, const char *bytes, size_t length);
static int edit_id_used(const char *text, size_t length, long id);
static int edit_has_member(const char *members, size_t length, const char *name);
static int edit_members_rebuilt(const char *members, size_t length, const char *name, int member, char *output, size_t capacity, size_t *used);

/*
 * Reads a request's text (at most ADMIN_REQUEST_MAX bytes, lines ended by
 * line ends, a NUL after its last byte) into a request, its lines cut in
 * place.  Returns ADMIN_OK or ADMIN_BAD_REQUEST.
 */
int
admin_parse(
	char *text,
	size_t length,
	struct admin_request *request)
{
	char *lines[ADMIN_LINES_MAX];
	const char *nul;
	size_t count;
	size_t at;
	size_t i;
	size_t arguments;
	int known;
	int same;

	/* Nothing yet; a text too long, or holding a NUL, is not a request. */
	memset(request, 0, sizeof(request[0]));
	nul = memchr(text, '\0', length);
	if (length > ADMIN_REQUEST_MAX || nul != NULL)
		return ADMIN_BAD_REQUEST;

	/* The lines, each cut at its end (the last may have none; an empty line after the last end is not one). */
	count = 0;
	at = 0;
	while (at < length) {
		/* Too many lines. */
		if (count == ADMIN_LINES_MAX)
			return ADMIN_BAD_REQUEST;

		/* One line. */
		lines[count] = text + at;
		count++;
		while (at < length && text[at] != '\n')
			at++;

		/* Its end becomes a NUL. */
		if (at < length) {
			text[at] = '\0';
			at++;
		}
	}

	/* The password and the operation at least. */
	if (count < 2U || lines[0][0] == '\0')
		return ADMIN_BAD_REQUEST;

	/* The operation's word. */
	request->password = lines[0];
	for (i = 0; i < sizeof(edit_operations) / sizeof(edit_operations[0]); i++) {
		/* This one. */
		same = strcmp(lines[1], edit_operations[i]);
		if (same == 0)
			break;
	}

	/* A word not known. */
	if (i == sizeof(edit_operations) / sizeof(edit_operations[0]))
		return ADMIN_BAD_REQUEST;
	request->operation = (enum admin_operation)i;

	/* Its arguments: four for an addition, one for the system language, two for the rest. */
	arguments = 2U;
	if (request->operation == ADMIN_ADD)
		arguments = 4U;
	if (request->operation == ADMIN_SYSTEM_LANGUAGE)
		arguments = 1U;
	if (count != 2U + arguments)
		return ADMIN_BAD_REQUEST;
	request->name = lines[2];

	/* Each operation's own. */
	switch (request->operation) {
	case ADMIN_ADD:
		/* The display name, the new password, and admin or user. */
		request->display = lines[3];
		request->fresh = lines[4];
		same = strcmp(lines[5], "admin");
		if (same == 0) {
			request->administrator = 1;
			break;
		}

		/* Or a plain user. */
		same = strcmp(lines[5], "user");
		if (same != 0)
			return ADMIN_BAD_REQUEST;
		break;
	case ADMIN_REMOVE:
		/* keep-home or remove-home. */
		same = strcmp(lines[3], "remove-home");
		if (same == 0) {
			request->remove_home = 1;
			break;
		}

		/* Or keep it. */
		same = strcmp(lines[3], "keep-home");
		if (same != 0)
			return ADMIN_BAD_REQUEST;
		break;
	case ADMIN_RESET_PASSWORD:
		request->fresh = lines[3];
		break;
	case ADMIN_SYSTEM_LANGUAGE:
		/* A language the login screen knows. */
		known = admin_language_valid(lines[2]);
		if (!known)
			return ADMIN_BAD_REQUEST;
		break;
	case ADMIN_GROUP_ADD:
	case ADMIN_GROUP_REMOVE:
		/* wheel or network, and no other group. */
		request->group = lines[3];
		same = strcmp(lines[3], "wheel");
		if (same == 0)
			break;
		same = strcmp(lines[3], "network");
		if (same != 0)
			return ADMIN_BAD_REQUEST;
		break;
	default:
		return ADMIN_BAD_REQUEST;
	}

	/* Succeeded: the request is read. */
	return ADMIN_OK;
}

/*
 * Reports a reason's word for the answer line.
 */
const char *
admin_reason_word(
	int reason)
{
	/* A reason not known is a failure. */
	if (reason < 0 || (size_t)reason >= sizeof(edit_reasons) / sizeof(edit_reasons[0]))
		return "failed";

	/* Its word. */
	return edit_reasons[reason];
}

/*
 * Reports an operation's word.
 */
const char *
admin_operation_word(
	enum admin_operation operation)
{
	/* An operation not known. */
	if ((size_t)operation >= sizeof(edit_operations) / sizeof(edit_operations[0]))
		return "unknown";

	/* Its word. */
	return edit_operations[operation];
}

/*
 * Reports whether a language is one the login screen may be in ("en",
 * "ja"): 1 when it is, 0 otherwise.
 */
int
admin_language_valid(
	const char *language)
{
	size_t i;
	int same;

	/* Each language known. */
	for (i = 0; i < sizeof(edit_languages) / sizeof(edit_languages[0]); i++) {
		same = strcmp(language, edit_languages[i]);
		if (same == 0)
			return 1;
	}

	/* Succeeded: not one of them. */
	return 0;
}

/*
 * Reports whether a new user's name is allowed: a lower-case letter, then
 * up to 31 lower-case letters, digits, '-' or '_'.
 */
int
admin_name_valid(
	const char *name)
{
	size_t i;
	char c;

	/* The first character, a lower-case letter. */
	if (name[0] < 'a' || name[0] > 'z')
		return 0;

	/* The rest, and the length. */
	for (i = 1; name[i] != '\0'; i++) {
		/* Too long. */
		if (i >= ADMIN_NAME_MAX)
			return 0;

		/* A character allowed. */
		c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
			continue;
		return 0;
	}

	/* Allowed. */
	return 1;
}

/*
 * Reports whether a display name is allowed: printable, without ':', not
 * longer than 64 bytes (UTF-8 is taken as it is).
 */
int
admin_display_valid(
	const char *display)
{
	size_t length;
	size_t i;

	/* Not too long. */
	length = strlen(display);
	if (length > 64U)
		return 0;

	/* No ':', and no control character. */
	for (i = 0; i < length; i++) {
		/* One byte. */
		if (display[i] == ':' || (unsigned char)display[i] < 0x20U || display[i] == 0x7f)
			return 0;
	}

	/* Allowed. */
	return 1;
}

/*
 * Finds a field (counted from 0) of the line of a text whose first field
 * is a name.  Returns 0, or ENOENT when there is no such line or field.
 */
int
admin_field(
	const char *text,
	size_t length,
	const char *name,
	unsigned index,
	const char **field,
	size_t *field_length)
{
	struct edit_line line;
	size_t at;
	int more;
	int named;
	int found;

	/* Each line, until the name's. */
	at = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(text, length, at, &line);
		if (!more)
			return ENOENT;
		at = line.next;

		/* The name's line: its field. */
		named = edit_line_named(&line, name);
		if (!named)
			continue;
		found = edit_line_field(line.start, line.length, index, field, field_length);
		if (!found)
			return ENOENT;

		/* Succeeded: the field. */
		return 0;
	}
}

/*
 * Reports a number field of the line of a name, or -1 when there is no
 * such line, field, or number.
 */
long
admin_number_field(
	const char *text,
	size_t length,
	const char *name,
	unsigned index)
{
	const char *field;
	size_t field_length;
	int error;

	/* The field. */
	error = admin_field(text, length, name, index, &field, &field_length);
	if (error != 0)
		return -1;

	/* Its number. */
	return edit_number(field, field_length);
}

/*
 * Reports the lowest number from ADMIN_ID_FIRST up that no account uses
 * as its user ID and no group as its group ID, or -1 when none is left.
 */
long
admin_free_id(
	const char *passwd,
	size_t passwd_length,
	const char *group,
	size_t group_length)
{
	long id;
	int used;

	/* Each number in turn. */
	for (id = ADMIN_ID_FIRST; id <= ADMIN_ID_LAST; id++) {
		/* A user's. */
		used = edit_id_used(passwd, passwd_length, id);
		if (used)
			continue;

		/* A group's. */
		used = edit_id_used(group, group_length, id);
		if (used)
			continue;

		/* Free. */
		return id;
	}

	/* None left. */
	return -1;
}

/*
 * Copies a text without the lines whose first field is a name.  Returns 0,
 * ENOENT when there was none, or ERANGE when the output is too small.
 */
int
admin_line_remove(
	const char *text,
	size_t length,
	const char *name,
	char *output,
	size_t capacity,
	size_t *written)
{
	struct edit_line line;
	size_t at;
	size_t used;
	int removed;
	int more;
	int named;
	int error;

	/* Each line, kept unless it is the name's. */
	at = 0;
	used = 0;
	removed = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(text, length, at, &line);
		if (!more)
			break;
		at = line.next;

		/* The name's goes. */
		named = edit_line_named(&line, name);
		if (named) {
			removed = 1;
			continue;
		}

		/* Another, kept with its end. */
		error = edit_put(output, capacity, &used, line.start, line.length);
		if (error == 0)
			error = edit_put(output, capacity, &used, "\n", 1U);
		if (error != 0)
			return error;
	}

	/* None was the name's. */
	*written = used;
	if (!removed)
		return ENOENT;

	/* Succeeded: the text without it. */
	return 0;
}

/*
 * Copies a text with a line (without its end) added at its end.  Returns
 * 0, or ERANGE when the output is too small.
 */
int
admin_line_append(
	const char *text,
	size_t length,
	const char *line,
	char *output,
	size_t capacity,
	size_t *written)
{
	size_t used;
	int error;

	/* The text, ended. */
	used = 0;
	error = edit_put(output, capacity, &used, text, length);
	if (error == 0 && length > 0U && text[length - 1U] != '\n')
		error = edit_put(output, capacity, &used, "\n", 1U);

	/* The line, ended. */
	if (error == 0)
		error = edit_put(output, capacity, &used, line, strlen(line));
	if (error == 0)
		error = edit_put(output, capacity, &used, "\n", 1U);
	if (error != 0)
		return error;

	/* Succeeded: the text with the line. */
	*written = used;
	return 0;
}

/*
 * Copies a group file's text with a name made a member of a group, or
 * not one (member 0), in the group's list of members; the group's other
 * fields are kept, and a primary group is never changed here.  Returns 0,
 * ENOENT when there is no such group, or ERANGE.
 */
int
admin_member_set(
	const char *text,
	size_t length,
	const char *group,
	const char *name,
	int member,
	char *output,
	size_t capacity,
	size_t *written)
{
	struct edit_line line;
	const char *members;
	size_t members_length;
	size_t prefix;
	size_t at;
	size_t used;
	int found;
	int more;
	int named;
	int has;
	int error;

	/* Each line: the group's rebuilt, the others kept. */
	at = 0;
	used = 0;
	found = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(text, length, at, &line);
		if (!more)
			break;
		at = line.next;

		/* Another group's: kept. */
		named = edit_line_named(&line, group);
		has = 0;
		if (named)
			has = edit_line_field(line.start, line.length, 3U, &members, &members_length);
		if (!named || !has) {
			error = edit_put(output, capacity, &used, line.start, line.length);
			if (error == 0)
				error = edit_put(output, capacity, &used, "\n", 1U);
			if (error != 0)
				return error;
			continue;
		}

		/* The group's: its first three fields as they are, then its members with the name or without it. */
		found = 1;
		prefix = (size_t)(members - line.start);
		error = edit_put(output, capacity, &used, line.start, prefix);
		if (error == 0)
			error = edit_members_rebuilt(members, members_length, name, member, output, capacity, &used);
		if (error == 0)
			error = edit_put(output, capacity, &used, members + members_length, line.length - prefix - members_length);
		if (error == 0)
			error = edit_put(output, capacity, &used, "\n", 1U);
		if (error != 0)
			return error;
	}

	/* No such group. */
	*written = used;
	if (!found)
		return ENOENT;

	/* Succeeded: the text with the group changed. */
	return 0;
}

/*
 * Copies a group file's text with a name taken out of every group's list
 * of members.  Returns 0, or ERANGE.
 */
int
admin_member_forget(
	const char *text,
	size_t length,
	const char *name,
	char *output,
	size_t capacity,
	size_t *written)
{
	struct edit_line line;
	const char *members;
	size_t members_length;
	size_t prefix;
	size_t at;
	size_t used;
	int more;
	int has;
	int error;

	/* Each line: its members without the name. */
	at = 0;
	used = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(text, length, at, &line);
		if (!more)
			break;
		at = line.next;

		/* A line without a members field is kept. */
		has = edit_line_field(line.start, line.length, 3U, &members, &members_length);
		if (!has) {
			error = edit_put(output, capacity, &used, line.start, line.length);
			if (error == 0)
				error = edit_put(output, capacity, &used, "\n", 1U);
			if (error != 0)
				return error;
			continue;
		}

		/* The line with its members rebuilt. */
		prefix = (size_t)(members - line.start);
		error = edit_put(output, capacity, &used, line.start, prefix);
		if (error == 0)
			error = edit_members_rebuilt(members, members_length, name, 0, output, capacity, &used);
		if (error == 0)
			error = edit_put(output, capacity, &used, members + members_length, line.length - prefix - members_length);
		if (error == 0)
			error = edit_put(output, capacity, &used, "\n", 1U);
		if (error != 0)
			return error;
	}

	/* Succeeded: the text without the name in any list. */
	*written = used;
	return 0;
}

/*
 * Reports whether a user is in a group: its primary group ID is the
 * group's, or the group names the user.
 */
int
admin_in_group(
	const char *group_text,
	size_t group_length,
	const char *group,
	const char *name,
	long primary)
{
	const char *members;
	size_t members_length;
	long gid;
	int error;
	int has;

	/* The group's ID: the user's primary group. */
	gid = admin_number_field(group_text, group_length, group, 2U);
	if (gid >= 0 && gid == primary)
		return 1;

	/* Its members: the user named. */
	error = admin_field(group_text, group_length, group, 3U, &members, &members_length);
	if (error != 0)
		return 0;
	has = edit_has_member(members, members_length, name);

	/* Named or not. */
	return has;
}

/*
 * Counts the administrators among the people's accounts (user ID
 * ADMIN_ID_FIRST and up, in wheel), leaving one name out (NULL for none).
 */
size_t
admin_administrators(
	const char *passwd,
	size_t passwd_length,
	const char *group,
	size_t group_length,
	const char *except)
{
	struct edit_line line;
	const char *field;
	size_t field_length;
	char name[ADMIN_NAME_MAX + 1U];
	size_t count;
	size_t at;
	long uid;
	long gid;
	int more;
	int found;
	int in;
	int same;

	/* Each account. */
	at = 0;
	count = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(passwd, passwd_length, at, &line);
		if (!more)
			break;
		at = line.next;

		/* Its name, which fits. */
		found = edit_line_field(line.start, line.length, 0U, &field, &field_length);
		if (!found || field_length == 0U || field_length > ADMIN_NAME_MAX)
			continue;
		memcpy(name, field, field_length);
		name[field_length] = '\0';

		/* A person's account, not the one left out. */
		same = 1;
		if (except != NULL)
			same = strcmp(name, except);
		found = edit_line_field(line.start, line.length, 2U, &field, &field_length);
		uid = -1;
		if (found)
			uid = edit_number(field, field_length);
		if (same == 0 || uid < ADMIN_ID_FIRST)
			continue;

		/* In wheel, by its primary group or by name. */
		found = edit_line_field(line.start, line.length, 3U, &field, &field_length);
		gid = -1;
		if (found)
			gid = edit_number(field, field_length);
		in = admin_in_group(group, group_length, "wheel", name, gid);
		if (in)
			count++;
	}

	/* The count. */
	return count;
}

/*
 * Finds the line of a text that starts at an offset.  Returns 1 with it,
 * 0 at the text's end.
 */
static int
edit_next_line(
	const char *text,
	size_t length,
	size_t at,
	struct edit_line *line)
{
	const char *end;

	/* The text's end. */
	if (at >= length)
		return 0;

	/* To the line's end, or the text's. */
	line->start = text + at;
	end = memchr(line->start, '\n', length - at);
	line->length = length - at;
	if (end != NULL)
		line->length = (size_t)(end - line->start);
	line->next = at + line->length + 1U;

	/* Succeeded: the line. */
	return 1;
}

/*
 * Finds a field (counted from 0) of one line.  Returns 1 with it, 0 when
 * the line has fewer fields.
 */
static int
edit_line_field(
	const char *line,
	size_t length,
	unsigned index,
	const char **field,
	size_t *field_length)
{
	const char *colon;
	size_t at;
	unsigned i;

	/* Past the fields before it. */
	at = 0;
	for (i = 0; i < index; i++) {
		/* The next ':'. */
		colon = memchr(line + at, ':', length - at);
		if (colon == NULL)
			return 0;
		at = (size_t)(colon - line) + 1U;
	}

	/* The field, to the next ':' or the line's end. */
	*field = line + at;
	colon = memchr(line + at, ':', length - at);
	*field_length = length - at;
	if (colon != NULL)
		*field_length = (size_t)(colon - (line + at));

	/* Succeeded: the field. */
	return 1;
}

/*
 * Reports whether a line's first field is a name.
 */
static int
edit_line_named(
	const struct edit_line *line,
	const char *name)
{
	const char *field;
	size_t field_length;
	size_t length;
	int found;
	int same;

	/* The first field against the name. */
	found = edit_line_field(line->start, line->length, 0U, &field, &field_length);
	length = strlen(name);
	if (!found || field_length != length)
		return 0;
	same = memcmp(field, name, length);
	if (same != 0)
		return 0;

	/* The name's line. */
	return 1;
}

/*
 * Reports a field's decimal number, or -1 when it is not one.
 */
static long
edit_number(
	const char *text,
	size_t length)
{
	long value;
	size_t i;

	/* Digits only, not too many. */
	if (length == 0U || length > 9U)
		return -1;
	value = 0;
	for (i = 0; i < length; i++) {
		/* One digit. */
		if (text[i] < '0' || text[i] > '9')
			return -1;
		value = value * 10 + (text[i] - '0');
	}

	/* The number. */
	return value;
}

/*
 * Appends bytes to an output; returns 0, or ERANGE when they do not fit.
 */
static int
edit_put(
	char *output,
	size_t capacity,
	size_t *used,
	const char *bytes,
	size_t length)
{
	/* Room for them. */
	if (length > capacity - *used)
		return ERANGE;

	/* The bytes. */
	memcpy(output + *used, bytes, length);
	*used += length;
	return 0;
}

/*
 * Reports whether an account or group file uses a number as an ID (the
 * third field of a line).
 */
static int
edit_id_used(
	const char *text,
	size_t length,
	long id)
{
	struct edit_line line;
	const char *field;
	size_t field_length;
	size_t at;
	long number;
	int more;
	int found;

	/* Each line's third field. */
	at = 0;
	for (;;) {
		/* The next line, or the end. */
		more = edit_next_line(text, length, at, &line);
		if (!more)
			return 0;
		at = line.next;

		/* The number used. */
		found = edit_line_field(line.start, line.length, 2U, &field, &field_length);
		number = -1;
		if (found)
			number = edit_number(field, field_length);
		if (number == id)
			return 1;
	}
}

/*
 * Reports whether a list of members (split by ',') names a user.
 */
static int
edit_has_member(
	const char *members,
	size_t length,
	const char *name)
{
	const char *comma;
	size_t at;
	size_t item;
	size_t name_length;
	int same;

	/* Each member. */
	name_length = strlen(name);
	at = 0;
	while (at < length) {
		/* One member, to the next ',' or the list's end. */
		comma = memchr(members + at, ',', length - at);
		item = length - at;
		if (comma != NULL)
			item = (size_t)(comma - (members + at));

		/* The name. */
		if (item == name_length) {
			same = memcmp(members + at, name, name_length);
			if (same == 0)
				return 1;
		}

		/* The next. */
		at += item + 1U;
	}

	/* Not named. */
	return 0;
}

/*
 * Writes a list of members again: every member but the name, then the name
 * at the end when it is to be one.
 */
static int
edit_members_rebuilt(
	const char *members,
	size_t length,
	const char *name,
	int member,
	char *output,
	size_t capacity,
	size_t *used)
{
	const char *comma;
	size_t name_length;
	size_t at;
	size_t item;
	int first;
	int same;
	int error;

	/* Each member but the name (empty items dropped). */
	name_length = strlen(name);
	first = 1;
	at = 0;
	while (at < length) {
		/* One member. */
		comma = memchr(members + at, ',', length - at);
		item = length - at;
		if (comma != NULL)
			item = (size_t)(comma - (members + at));

		/* The name itself, or nothing: left out. */
		same = 1;
		if (item == name_length)
			same = memcmp(members + at, name, name_length);
		if (item == 0U || same == 0) {
			at += item + 1U;
			continue;
		}

		/* Kept, after a ',' when not the first. */
		error = 0;
		if (!first)
			error = edit_put(output, capacity, used, ",", 1U);
		if (error == 0)
			error = edit_put(output, capacity, used, members + at, item);
		if (error != 0)
			return error;
		first = 0;
		at += item + 1U;
	}

	/* The name at the end when it is to be a member. */
	if (member) {
		error = 0;
		if (!first)
			error = edit_put(output, capacity, used, ",", 1U);
		if (error == 0)
			error = edit_put(output, capacity, used, name, name_length);
		if (error != 0)
			return error;
	}

	/* Succeeded: the list. */
	return 0;
}
