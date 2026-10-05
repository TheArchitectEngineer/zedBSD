/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * account-admin's pure part (ws089-p026; docs/architecture/security.md,
 * "Account administration"): the request read from standard input, the
 * rules of names, and the edits of the texts of /etc/passwd, /etc/group
 * and /etc/shadow.  Nothing here touches a file, so the host tests run it
 * alone; main.c reads the files under the lock, checks, edits and writes.
 */

#ifndef ACCOUNT_ADMIN_EDIT_H
#define ACCOUNT_ADMIN_EDIT_H

#include <stddef.h>

/* The longest request taken, without its end. */
#define ADMIN_REQUEST_MAX	4096U

/* The most lines a request has: the password, the operation and four arguments. */
#define ADMIN_LINES_MAX		6U

/* The longest name of a user, and the first and the last user ID given to a person. */
#define ADMIN_NAME_MAX		32U
#define ADMIN_ID_FIRST		1000L
#define ADMIN_ID_LAST		59999L

/* The operations. */
enum admin_operation {
	ADMIN_ADD,
	ADMIN_REMOVE,
	ADMIN_RESET_PASSWORD,
	ADMIN_GROUP_ADD,
	ADMIN_GROUP_REMOVE
};

/*
 * Why a request is refused: each has its fixed word on the answer line
 * (admin_reason_word), which Settings shows in the user's language.
 */
enum admin_reason {
	ADMIN_OK,
	ADMIN_BAD_REQUEST,
	ADMIN_NOT_ADMINISTRATOR,
	ADMIN_BAD_PASSWORD,
	ADMIN_NO_SUCH_USER,
	ADMIN_NAME_TAKEN,
	ADMIN_BAD_NAME,
	ADMIN_WEAK_PASSWORD,
	ADMIN_LAST_ADMINISTRATOR,
	ADMIN_SELF,
	ADMIN_ROOT,
	ADMIN_BUSY,
	ADMIN_HOME_EXISTS,
	ADMIN_FAILED
};

/*
 * A request: the caller's password, the operation, and its arguments (the
 * name; the display name or keep-home/remove-home, the new password, or
 * the group; the new password of an addition; admin or user), each a line
 * of the request's text with its end replaced by a NUL.
 */
struct admin_request {
	char *password;
	enum admin_operation operation;
	const char *name;
	const char *display;
	const char *fresh;
	const char *group;
	int administrator;
	int remove_home;
};

int admin_parse(char *text, size_t length, struct admin_request *request);
const char *admin_reason_word(int reason);
const char *admin_operation_word(enum admin_operation operation);
int admin_name_valid(const char *name);
int admin_display_valid(const char *display);
int admin_field(const char *text, size_t length, const char *name, unsigned index, const char **field, size_t *field_length);
long admin_number_field(const char *text, size_t length, const char *name, unsigned index);
long admin_free_id(const char *passwd, size_t passwd_length, const char *group, size_t group_length);
int admin_line_remove(const char *text, size_t length, const char *name, char *output, size_t capacity, size_t *written);
int admin_line_append(const char *text, size_t length, const char *line, char *output, size_t capacity, size_t *written);
int admin_member_set(const char *text, size_t length, const char *group, const char *name, int member, char *output, size_t capacity, size_t *written);
int admin_member_forget(const char *text, size_t length, const char *name, char *output, size_t capacity, size_t *written);
int admin_in_group(const char *group_text, size_t group_length, const char *group, const char *name, long primary);
size_t admin_administrators(const char *passwd, size_t passwd_length, const char *group, size_t group_length, const char *except);

#endif
