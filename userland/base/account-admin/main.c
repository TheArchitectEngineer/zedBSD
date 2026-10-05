/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * account-admin: the administrator's changes to the people's accounts
 * (ws089-p026; docs/architecture/security.md, "Account administration").
 *
 *   /usr/libexec/account-admin < request
 *
 * Set-user-ID root, run by the compositor's backend for Settings (never by
 * hand: it is outside PATH).  The request is text on standard input, one
 * field a line: the caller's password, the operation (add, remove,
 * reset-password, group-add, group-remove) and its arguments (edit.h).
 * Nothing comes from the command line or the environment.  The answer is
 * one line on standard output, "ok" or "error REASON", and the exit status
 * is 0 or 1.
 *
 * The checks, in order: the caller (the real user ID, not root); the
 * caller in wheel (otherwise the password is not checked, so that the tool
 * cannot test a non-administrator's password); the caller's password (a
 * wrong one costs two seconds); the target (no root or system account,
 * not oneself for a removal or a wheel removal, not the last administrator
 * among the people's accounts, not a user with a running process for a
 * removal); the values (a new name, display name and password by the
 * rules).  Only then are the files changed: all three under the lock of
 * the account files, each replaced atomically (group, passwd, shadow for
 * an addition, the reverse for a removal), the signals that would stop
 * the tool held meanwhile.  Every request's result goes to the system log
 * (auth), never a password or a hash.
 */

#include "edit.h"

#include "userland/base/common/account.h"
#include "userland/base/login/verify.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

#include <uapi/system.h>

/* Where the people's homes are, the skeleton copied into a new one, and the shell they get. */
#ifndef ADMIN_HOME_BASE
#define ADMIN_HOME_BASE		"/home"
#endif
#ifndef ADMIN_SKELETON
#define ADMIN_SKELETON		"/etc/skel"
#endif
#define ADMIN_SHELL		"/bin/sh"

/* The largest account file read, and the room of an edited one. */
#define ADMIN_FILE_MAX		(256U * 1024U)
#define ADMIN_OUTPUT_MAX	(ADMIN_FILE_MAX + 1024U)

/* The pause after a wrong password (seconds), the deepest home removed, and the most processes looked at. */
#define ADMIN_WRONG_PAUSE	2U
#define ADMIN_TREE_DEPTH	64
#define ADMIN_PROCESSES_MAX	4096U

/* The longest path built, and a line of an account file. */
#define ADMIN_PATH_MAX		512U
#define ADMIN_LINE_MAX		(ACCOUNT_HASH_MAX + 256U)

/*
 * The account files read under the lock and their edited copies: each
 * file's text and length, and two output buffers to edit through.
 */
struct admin_files {
	char *passwd;
	size_t passwd_length;
	char *group;
	size_t group_length;
	char *shadow;
	size_t shadow_length;
	char *first;
	char *second;
};

/*
 * A user found in passwd: its user ID, its primary group, and its home.
 */
struct admin_user {
	long uid;
	long gid;
	char home[ADMIN_PATH_MAX];
};

int main(int argc, char **argv);
static int admin_read_request(char *text, size_t capacity, size_t *length);
static int admin_apply(const struct admin_request *request, const char *caller);
static int admin_files_open(struct admin_files *files);
static void admin_files_close(struct admin_files *files);
static int admin_find_user(const struct admin_files *files, const char *name, struct admin_user *user);
static int admin_add(struct admin_files *files, const struct admin_request *request);
static int admin_remove(struct admin_files *files, const struct admin_request *request, const char *caller);
static int admin_reset(struct admin_files *files, const struct admin_request *request);
static int admin_group(struct admin_files *files, const struct admin_request *request, const char *caller);
static int admin_write(const char *path, mode_t mode, const char *text, size_t length);
static int admin_busy(const char *name, long uid);
static int admin_make_home(const char *name, long id);
static void admin_copy_skeleton(int home, long id);
static int admin_remove_tree(int parent, const char *name, int depth);
static int admin_dot(const char *name);
static void admin_answer(int reason);
static void admin_standard_descriptors(void);
static void admin_wipe(char *buffer, size_t size);

/*
 * Reads the request, checks the caller and its password, and carries the
 * request out.
 */
int
main(
	int argc,
	char **argv)
{
	char text[ADMIN_REQUEST_MAX + 2U];
	char check[ACCOUNT_PASSWORD_MAX + 2U];
	char buffer[LOGIN_VERIFY_BUFFER];
	char caller[ADMIN_NAME_MAX + 1U];
	struct admin_request request;
	struct passwd account;
	struct passwd *found;
	sigset_t held;
	sigset_t previous;
	size_t length;
	size_t name_length;
	uid_t real;
	uid_t effective;
	gid_t primary;
	int reason;
	int wheel;
	int error;

	/* Nothing from the command line or the environment; descriptors 0 to 2 open; the log. */
	(void)argc;
	(void)argv;
	admin_standard_descriptors();
	(void)clearenv();
	(void)umask(022);
	openlog("account-admin", LOG_PID, LOG_AUTH);

	/* Installed set-user-ID root, or it can do nothing. */
	effective = geteuid();
	if (effective != 0) {
		admin_answer(ADMIN_FAILED);
		return 1;
	}

	/* The request, bounded: a longer one is refused before any check. */
	error = admin_read_request(text, sizeof(text), &length);
	if (error != 0) {
		syslog(LOG_NOTICE, "refused: a request too long or unreadable");
		admin_answer(ADMIN_BAD_REQUEST);
		return 1;
	}

	/* Its lines. */
	reason = admin_parse(text, length, &request);
	if (reason != ADMIN_OK) {
		admin_wipe(text, sizeof(text));
		syslog(LOG_NOTICE, "refused: a request not understood");
		admin_answer(reason);
		return 1;
	}

	/* The caller: the real user ID, never root (root has the base tools). */
	real = getuid();
	if (real == 0) {
		admin_wipe(text, sizeof(text));
		syslog(LOG_NOTICE, "refused: run by root");
		admin_answer(ADMIN_ROOT);
		return 1;
	}

	/* The caller's account, its name not longer than a name may be. */
	found = getpwuid(real);
	name_length = ADMIN_NAME_MAX + 1U;
	if (found != NULL)
		name_length = strlen(found->pw_name);
	if (found == NULL || name_length > ADMIN_NAME_MAX) {
		admin_wipe(text, sizeof(text));
		admin_answer(ADMIN_FAILED);
		return 1;
	}

	/* Its name and primary group, kept. */
	(void)snprintf(caller, sizeof(caller), "%s", found->pw_name);
	primary = found->pw_gid;

	/* An administrator, or the password is not even checked. */
	wheel = account_in_wheel(caller, primary);
	if (!wheel) {
		admin_wipe(text, sizeof(text));
		syslog(LOG_NOTICE, "refused: %s is not an administrator", caller);
		admin_answer(ADMIN_NOT_ADMINISTRATOR);
		return 1;
	}

	/* The caller's password (login_verify erases its copy); a wrong one costs two seconds. */
	(void)snprintf(check, sizeof(check), "%s", request.password);
	admin_wipe(request.password, strlen(request.password));
	error = login_verify(caller, check, &account, buffer, sizeof(buffer));
	admin_wipe(check, sizeof(check));
	admin_wipe(buffer, sizeof(buffer));
	if (error != 0) {
		admin_wipe(text, sizeof(text));
		syslog(LOG_NOTICE, "refused: a wrong password for %s", caller);
		(void)sleep(ADMIN_WRONG_PAUSE);
		admin_answer(ADMIN_BAD_PASSWORD);
		return 1;
	}

	/* The work, with the signals that would stop it midway held. */
	sigemptyset(&held);
	sigaddset(&held, SIGINT);
	sigaddset(&held, SIGTERM);
	sigaddset(&held, SIGHUP);
	sigaddset(&held, SIGQUIT);
	sigaddset(&held, SIGTSTP);
	sigaddset(&held, SIGPIPE);
	(void)sigprocmask(SIG_BLOCK, &held, &previous);
	reason = admin_apply(&request, caller);
	(void)sigprocmask(SIG_SETMASK, &previous, NULL);

	/* The request (with its new password) goes; the result is logged and answered. */
	syslog(LOG_NOTICE, "%s by %s for %s: %s", admin_operation_word(request.operation), caller, request.name, admin_reason_word(reason));
	admin_wipe(text, sizeof(text));
	admin_answer(reason);

	/* Reports a refusal or a failure. */
	if (reason != ADMIN_OK)
		return 1;

	/* Succeeded: the change is made. */
	return 0;
}

/*
 * Reads standard input to its end into a buffer, a NUL after it.  Returns
 * 0, or EFBIG when it holds more than ADMIN_REQUEST_MAX bytes, or an errno
 * value.
 */
static int
admin_read_request(
	char *text,
	size_t capacity,
	size_t *length)
{
	ssize_t got;
	size_t used;

	/* All of it, one byte more than taken at most. */
	used = 0;
	for (;;) {
		got = read(STDIN_FILENO, text + used, capacity - 1U - used);

		/* Interrupted: again. */
		if (got < 0 && errno == EINTR)
			continue;

		/* An error, or the end. */
		if (got < 0)
			return errno;
		if (got == 0)
			break;

		/* More than a request may be. */
		used += (size_t)got;
		if (used > ADMIN_REQUEST_MAX)
			return EFBIG;
	}

	/* Succeeded: the text, ended. */
	text[used] = '\0';
	*length = used;
	return 0;
}

/*
 * Carries out a checked caller's request under the lock of the account
 * files.  Returns ADMIN_OK or a reason.
 */
static int
admin_apply(
	const struct admin_request *request,
	const char *caller)
{
	struct admin_files files;
	int reason;
	int error;

	/* The lock. */
	error = account_files_lock();
	if (error != 0)
		return ADMIN_FAILED;

	/* The three files. */
	error = admin_files_open(&files);
	if (error != 0) {
		admin_files_close(&files);
		account_files_unlock();
		return ADMIN_FAILED;
	}

	/* The operation. */
	switch (request->operation) {
	case ADMIN_ADD:
		reason = admin_add(&files, request);
		break;
	case ADMIN_REMOVE:
		reason = admin_remove(&files, request, caller);
		break;
	case ADMIN_RESET_PASSWORD:
		reason = admin_reset(&files, request);
		break;
	case ADMIN_GROUP_ADD:
	case ADMIN_GROUP_REMOVE:
		reason = admin_group(&files, request, caller);
		break;
	default:
		reason = ADMIN_BAD_REQUEST;
		break;
	}

	/* The copies go (the shadow's hold every hash), then the lock. */
	admin_files_close(&files);
	account_files_unlock();

	/* What happened. */
	return reason;
}

/*
 * Reads the three account files and makes the edit buffers.  Returns 0 or
 * an errno value (the caller closes in either case).
 */
static int
admin_files_open(
	struct admin_files *files)
{
	int error;

	/* The buffers. */
	memset(files, 0, sizeof(files[0]));
	files->passwd = malloc(ADMIN_FILE_MAX);
	files->group = malloc(ADMIN_FILE_MAX);
	files->shadow = malloc(ADMIN_FILE_MAX);
	files->first = malloc(ADMIN_OUTPUT_MAX);
	files->second = malloc(ADMIN_OUTPUT_MAX);
	if (files->passwd == NULL || files->group == NULL || files->shadow == NULL)
		return ENOMEM;
	if (files->first == NULL || files->second == NULL)
		return ENOMEM;

	/* passwd. */
	error = account_file_read(ACCOUNT_PASSWD_FILE, files->passwd, ADMIN_FILE_MAX, &files->passwd_length);
	if (error != 0)
		return error;

	/* group. */
	error = account_file_read(ACCOUNT_GROUP_FILE, files->group, ADMIN_FILE_MAX, &files->group_length);
	if (error != 0)
		return error;

	/* shadow. */
	error = account_file_read(ACCOUNT_SHADOW_PATH, files->shadow, ADMIN_FILE_MAX, &files->shadow_length);
	if (error != 0)
		return error;

	/* Succeeded: the files are read. */
	return 0;
}

/*
 * Frees the copies of the account files, wiped (the shadow's and the
 * edits hold hashes).
 */
static void
admin_files_close(
	struct admin_files *files)
{
	char **buffers[5];
	size_t sizes[5];
	size_t i;

	/* Each buffer, wiped and freed. */
	buffers[0] = &files->passwd;
	buffers[1] = &files->group;
	buffers[2] = &files->shadow;
	buffers[3] = &files->first;
	buffers[4] = &files->second;
	sizes[0] = ADMIN_FILE_MAX;
	sizes[1] = ADMIN_FILE_MAX;
	sizes[2] = ADMIN_FILE_MAX;
	sizes[3] = ADMIN_OUTPUT_MAX;
	sizes[4] = ADMIN_OUTPUT_MAX;
	for (i = 0; i < 5U; i++) {
		/* One buffer. */
		if (*buffers[i] == NULL)
			continue;
		admin_wipe(*buffers[i], sizes[i]);
		free(*buffers[i]);
		*buffers[i] = NULL;
	}
}

/*
 * Finds a user in the passwd read: its IDs and home.  Returns 1 with it,
 * 0 when there is no such user.
 */
static int
admin_find_user(
	const struct admin_files *files,
	const char *name,
	struct admin_user *user)
{
	const char *home;
	size_t home_length;
	int error;

	/* The IDs. */
	user->uid = admin_number_field(files->passwd, files->passwd_length, name, 2U);
	user->gid = admin_number_field(files->passwd, files->passwd_length, name, 3U);
	if (user->uid < 0 || user->gid < 0)
		return 0;

	/* The home. */
	user->home[0] = '\0';
	error = admin_field(files->passwd, files->passwd_length, name, 5U, &home, &home_length);
	if (error == 0 && home_length < sizeof(user->home)) {
		memcpy(user->home, home, home_length);
		user->home[home_length] = '\0';
	}

	/* Found. */
	return 1;
}

/*
 * Adds a user: the values checked, the lowest number free as a user ID and
 * a group ID, the group, passwd and shadow lines written in that order
 * (and wheel when asked), then the home made with the skeleton.
 */
static int
admin_add(
	struct admin_files *files,
	const struct admin_request *request)
{
	char line[ADMIN_LINE_MAX];
	char hash[ACCOUNT_HASH_MAX];
	char home[ADMIN_PATH_MAX];
	struct stat status;
	size_t written;
	size_t middle;
	long id;
	long day;
	int valid;
	int rule;
	int error;

	/* The name, the display name and the password by the rules. */
	valid = admin_name_valid(request->name);
	if (!valid)
		return ADMIN_BAD_NAME;
	valid = admin_display_valid(request->display);
	if (!valid)
		return ADMIN_BAD_NAME;
	rule = account_password_check(request->fresh, NULL, 0);
	if (rule != ACCOUNT_PASSWORD_OK)
		return ADMIN_WEAK_PASSWORD;

	/* Not a user's or a group's name already. */
	id = admin_number_field(files->passwd, files->passwd_length, request->name, 2U);
	if (id >= 0)
		return ADMIN_NAME_TAKEN;
	id = admin_number_field(files->group, files->group_length, request->name, 2U);
	if (id >= 0)
		return ADMIN_NAME_TAKEN;

	/* A home already there (one kept by a removal) is not given to the new user. */
	(void)snprintf(home, sizeof(home), "%s/%s", ADMIN_HOME_BASE, request->name);
	error = lstat(home, &status);
	if (error == 0 || errno != ENOENT)
		return ADMIN_HOME_EXISTS;

	/* The number. */
	id = admin_free_id(files->passwd, files->passwd_length, files->group, files->group_length);
	if (id < 0)
		return ADMIN_FAILED;

	/* The group: the private one, and wheel when asked. */
	(void)snprintf(line, sizeof(line), "%s:x:%ld:", request->name, id);
	error = admin_line_append(files->group, files->group_length, line, files->first, ADMIN_OUTPUT_MAX, &written);
	middle = written;
	if (error == 0 && request->administrator) {
		error = admin_member_set(files->first, middle, "wheel", request->name, 1, files->second, ADMIN_OUTPUT_MAX, &written);
		if (error == 0)
			memcpy(files->first, files->second, written);
	}

	/* The group file written. */
	if (error == 0)
		error = admin_write(ACCOUNT_GROUP_FILE, 0644, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* passwd. */
	(void)snprintf(line, sizeof(line), "%s:x:%ld:%ld:%s:%s:%s", request->name, id, id, request->display, home, ADMIN_SHELL);
	error = admin_line_append(files->passwd, files->passwd_length, line, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == 0)
		error = admin_write(ACCOUNT_PASSWD_FILE, 0644, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* shadow: the hash, today, and the usual ageing. */
	error = account_password_hash(request->fresh, hash, sizeof(hash));
	day = (long)(time(NULL) / 86400);
	(void)snprintf(line, sizeof(line), "%s:%s:%ld:0:99999:7:::", request->name, hash, day);
	admin_wipe(hash, sizeof(hash));
	if (error == 0)
		error = admin_line_append(files->shadow, files->shadow_length, line, files->first, ADMIN_OUTPUT_MAX, &written);
	admin_wipe(line, sizeof(line));
	if (error == 0)
		error = admin_write(ACCOUNT_SHADOW_PATH, 0400, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* The home, with the skeleton. */
	error = admin_make_home(request->name, id);
	if (error != 0) {
		syslog(LOG_ERR, "the home of %s could not be made: %s", request->name, strerror(error));
		return ADMIN_FAILED;
	}

	/* Succeeded: the user is added. */
	return ADMIN_OK;
}

/*
 * Removes a user: the target checked, its lines taken out of shadow,
 * passwd and group in that order, then its home removed when asked.
 */
static int
admin_remove(
	struct admin_files *files,
	const struct admin_request *request,
	const char *caller)
{
	struct admin_user user;
	char home[ADMIN_PATH_MAX];
	size_t written;
	size_t remaining;
	long group_id;
	int found;
	int same;
	int busy;
	int administrator;
	int base;
	int error;

	/* A person's account, not oneself. */
	found = admin_find_user(files, request->name, &user);
	if (!found)
		return ADMIN_NO_SUCH_USER;
	if (user.uid < ADMIN_ID_FIRST)
		return ADMIN_ROOT;
	same = strcmp(request->name, caller);
	if (same == 0)
		return ADMIN_SELF;

	/* Not the last administrator among the people's accounts. */
	administrator = admin_in_group(files->group, files->group_length, "wheel", request->name, user.gid);
	remaining = admin_administrators(files->passwd, files->passwd_length, files->group, files->group_length, request->name);
	if (administrator && remaining == 0U)
		return ADMIN_LAST_ADMINISTRATOR;

	/* Nobody logged in or running as it. */
	busy = admin_busy(request->name, user.uid);
	if (busy)
		return ADMIN_BUSY;

	/* shadow (an account without a line there is removed all the same). */
	error = admin_line_remove(files->shadow, files->shadow_length, request->name, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == 0)
		error = admin_write(ACCOUNT_SHADOW_PATH, 0400, files->first, written);
	if (error != 0 && error != ENOENT)
		return ADMIN_FAILED;

	/* passwd. */
	error = admin_line_remove(files->passwd, files->passwd_length, request->name, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == 0)
		error = admin_write(ACCOUNT_PASSWD_FILE, 0644, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* group: its private group (of its name and its primary group's ID), and its name out of every list. */
	group_id = admin_number_field(files->group, files->group_length, request->name, 2U);
	written = files->group_length;
	memcpy(files->second, files->group, files->group_length);
	if (group_id >= 0 && group_id == user.gid) {
		error = admin_line_remove(files->group, files->group_length, request->name, files->second, ADMIN_OUTPUT_MAX, &written);
		if (error != 0)
			return ADMIN_FAILED;
	}

	/* Its name out of every group's list, and the group file written. */
	error = admin_member_forget(files->second, written, request->name, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == 0)
		error = admin_write(ACCOUNT_GROUP_FILE, 0644, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* The home, only when asked, and only the one under the homes' directory by its name; no link is followed. */
	(void)snprintf(home, sizeof(home), "%s/%s", ADMIN_HOME_BASE, request->name);
	same = strcmp(home, user.home);
	if (request->remove_home && same == 0) {
		base = open(ADMIN_HOME_BASE, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		error = errno;
		if (base >= 0) {
			error = admin_remove_tree(base, request->name, 0);
			(void)close(base);
		}

		/* A home that could not be removed is said in the log; the account is removed all the same. */
		if (error != 0 && error != ENOENT)
			syslog(LOG_ERR, "the home of %s could not be removed: %s", request->name, strerror(error));
	}

	/* Succeeded: the user is removed. */
	return ADMIN_OK;
}

/*
 * Sets another person's password: the target checked, the password by the
 * rules, its hash into shadow (a line added when it has none).
 */
static int
admin_reset(
	struct admin_files *files,
	const struct admin_request *request)
{
	struct admin_user user;
	char hash[ACCOUNT_HASH_MAX];
	char line[ADMIN_LINE_MAX];
	size_t written;
	long day;
	int found;
	int rule;
	int error;

	/* A person's account. */
	found = admin_find_user(files, request->name, &user);
	if (!found)
		return ADMIN_NO_SUCH_USER;
	if (user.uid < ADMIN_ID_FIRST)
		return ADMIN_ROOT;

	/* The password by the rules. */
	rule = account_password_check(request->fresh, NULL, 0);
	if (rule != ACCOUNT_PASSWORD_OK)
		return ADMIN_WEAK_PASSWORD;

	/* The hash in the user's line, or a new line. */
	error = account_password_hash(request->fresh, hash, sizeof(hash));
	if (error != 0)
		return ADMIN_FAILED;
	day = (long)(time(NULL) / 86400);
	error = account_shadow_replace(files->shadow, files->shadow_length, request->name, hash, day, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == ENOENT) {
		(void)snprintf(line, sizeof(line), "%s:%s:%ld:0:99999:7:::", request->name, hash, day);
		error = admin_line_append(files->shadow, files->shadow_length, line, files->first, ADMIN_OUTPUT_MAX, &written);
		admin_wipe(line, sizeof(line));
	}

	/* Written. */
	admin_wipe(hash, sizeof(hash));
	if (error == 0)
		error = admin_write(ACCOUNT_SHADOW_PATH, 0400, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* Succeeded: the password is set. */
	return ADMIN_OK;
}

/*
 * Adds a person to wheel or network, or takes it out: the target checked
 * (not oneself or the last administrator out of wheel), the group's list
 * of members changed; no primary group is ever changed.
 */
static int
admin_group(
	struct admin_files *files,
	const struct admin_request *request,
	const char *caller)
{
	struct admin_user user;
	size_t written;
	size_t remaining;
	int member;
	int found;
	int wheel;
	int same;
	int error;

	/* A person's account. */
	found = admin_find_user(files, request->name, &user);
	if (!found)
		return ADMIN_NO_SUCH_USER;
	if (user.uid < ADMIN_ID_FIRST)
		return ADMIN_ROOT;

	/* Out of wheel: not oneself, and not the last administrator. */
	member = 0;
	if (request->operation == ADMIN_GROUP_ADD)
		member = 1;
	wheel = strcmp(request->group, "wheel");
	if (!member && wheel == 0) {
		/* Oneself. */
		same = strcmp(request->name, caller);
		if (same == 0)
			return ADMIN_SELF;

		/* The last one. */
		remaining = admin_administrators(files->passwd, files->passwd_length, files->group, files->group_length, request->name);
		if (remaining == 0U)
			return ADMIN_LAST_ADMINISTRATOR;
	}

	/* The group's list of members. */
	error = admin_member_set(files->group, files->group_length, request->group, request->name, member, files->first, ADMIN_OUTPUT_MAX, &written);
	if (error == 0)
		error = admin_write(ACCOUNT_GROUP_FILE, 0644, files->first, written);
	if (error != 0)
		return ADMIN_FAILED;

	/* Succeeded: the membership is changed. */
	return ADMIN_OK;
}

/*
 * Replaces an account file, saying a failure in the log.  Returns 0 or an
 * errno value.
 */
static int
admin_write(
	const char *path,
	mode_t mode,
	const char *text,
	size_t length)
{
	int error;

	/* The atomic replacement. */
	error = account_file_write(path, mode, text, length);
	if (error != 0) {
		syslog(LOG_ERR, "%s could not be replaced: %s", path, strerror(error));
		return error;
	}

	/* Succeeded: the file is the new one. */
	return 0;
}

/*
 * Reports whether a user is busy: a process runs with its user ID, or the
 * accounting records it logged in.
 */
static int
admin_busy(
	const char *name,
	long uid)
{
	struct process_info process;
	struct utmpx *record;
	int32_t cursor;
	unsigned count;
	int descriptor;
	int error;
	int same;
	int busy;

	/* The kernel's processes, one after another. */
	busy = 0;
	cursor = -1;
	descriptor = open("/dev/system", O_RDONLY | O_CLOEXEC);
	for (count = 0; descriptor >= 0 && count < ADMIN_PROCESSES_MAX; count++) {
		/* The next process after the last one (as ps asks for it). */
		memset(&process, 0, sizeof(process));
		process.pid = cursor;
		error = ioctl(descriptor, KERN_SYSTEM_GET_PROCESS, &process);
		if (error != 0)
			break;
		cursor = process.pid;

		/* One running as the user. */
		if ((long)process.uid == uid) {
			busy = 1;
			break;
		}
	}

	/* The list is no longer needed. */
	if (descriptor >= 0)
		(void)close(descriptor);

	/* A process of the user's. */
	if (busy)
		return 1;

	/* A login recorded for it. */
	setutxent();
	for (;;) {
		/* The next record, or the end. */
		record = getutxent();
		if (record == NULL)
			break;

		/* A login of the user's. */
		same = strncmp(record->ut_user, name, sizeof(record->ut_user));
		if (record->ut_type == USER_PROCESS && same == 0) {
			busy = 1;
			break;
		}
	}

	/* The records are closed. */
	endutxent();

	/* Busy or not. */
	return busy;
}

/*
 * Makes a new user's home under the homes' directory: mode 0700, owned by
 * the user, with the skeleton's files.  Returns 0 or an errno value.
 */
static int
admin_make_home(
	const char *name,
	long id)
{
	int base;
	int home;
	int error;

	/* The homes' directory, not followed if it is a link. */
	base = open(ADMIN_HOME_BASE, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (base < 0)
		return errno;

	/* The home, made anew (never an existing one). */
	error = mkdirat(base, name, 0700);
	if (error != 0) {
		error = errno;
		(void)close(base);
		return error;
	}

	/* Opened without following a link, and given to the user. */
	home = openat(base, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	(void)close(base);
	if (home < 0)
		return errno;
	error = fchown(home, (uid_t)id, (gid_t)id);
	if (error != 0) {
		error = errno;
		(void)close(home);
		return error;
	}

	/* The skeleton's files. */
	admin_copy_skeleton(home, id);
	(void)close(home);

	/* Succeeded: the home is made. */
	return 0;
}

/*
 * Copies the regular files directly in the skeleton into a new home,
 * owned by the user, without the group's and the others' permissions (no
 * link is followed; the skeleton may be missing).
 */
static void
admin_copy_skeleton(
	int home,
	long id)
{
	struct dirent *entry;
	struct stat status;
	char block[4096];
	ssize_t got;
	ssize_t put;
	DIR *skeleton;
	int regular;
	int source;
	int target;
	int error;
	int dot;

	/* The skeleton, when there is one. */
	skeleton = opendir(ADMIN_SKELETON);
	if (skeleton == NULL)
		return;

	/* Each entry that is a regular file. */
	for (;;) {
		/* The next entry, or the end. */
		entry = readdir(skeleton);
		if (entry == NULL)
			break;

		/* Not "." or "..". */
		dot = admin_dot(entry->d_name);
		if (dot)
			continue;

		/* A regular file (not followed). */
		error = fstatat(dirfd(skeleton), entry->d_name, &status, AT_SYMLINK_NOFOLLOW);
		regular = 0;
		if (error == 0 && (status.st_mode & S_IFMT) == S_IFREG)
			regular = 1;
		if (!regular)
			continue;

		/* The file and its copy, made anew in the home. */
		source = openat(dirfd(skeleton), entry->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
		if (source < 0)
			continue;
		target = openat(home, entry->d_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, status.st_mode & 0700);
		if (target < 0) {
			(void)close(source);
			continue;
		}

		/* Its bytes. */
		for (;;) {
			/* A block, or the end. */
			got = read(source, block, sizeof(block));
			if (got <= 0)
				break;

			/* Written whole, or the copy stops. */
			put = write(target, block, (size_t)got);
			if (put != got)
				break;
		}

		/* Given to the user. */
		(void)fchown(target, (uid_t)id, (gid_t)id);
		(void)close(target);
		(void)close(source);
	}

	/* The skeleton is no longer needed. */
	(void)closedir(skeleton);
}

/*
 * Removes an entry of a directory and, when it is a directory, everything
 * under it, never following a link.  Returns 0 or an errno value.
 */
static int
admin_remove_tree(
	int parent,
	const char *name,
	int depth)
{
	struct dirent *entry;
	struct stat status;
	DIR *directory;
	int descriptor;
	int error;
	int first;
	int dot;

	/* Too deep. */
	if (depth > ADMIN_TREE_DEPTH)
		return ELOOP;

	/* Not a directory (or a link to one): unlinked. */
	error = fstatat(parent, name, &status, AT_SYMLINK_NOFOLLOW);
	if (error != 0)
		return errno;
	if ((status.st_mode & S_IFMT) != S_IFDIR) {
		error = unlinkat(parent, name, 0);
		if (error != 0)
			return errno;
		return 0;
	}

	/* A directory: opened without following a link. */
	descriptor = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (descriptor < 0)
		return errno;
	directory = fdopendir(descriptor);
	if (directory == NULL) {
		error = errno;
		(void)close(descriptor);
		return error;
	}

	/* Everything in it, the first error kept. */
	first = 0;
	for (;;) {
		/* The next entry, or the end. */
		entry = readdir(directory);
		if (entry == NULL)
			break;

		/* Not "." or "..". */
		dot = admin_dot(entry->d_name);
		if (dot)
			continue;

		/* Removed, and under it. */
		error = admin_remove_tree(dirfd(directory), entry->d_name, depth + 1);
		if (error != 0 && first == 0)
			first = error;
	}

	/* The directory itself, once empty. */
	(void)closedir(directory);
	if (first != 0)
		return first;
	error = unlinkat(parent, name, AT_REMOVEDIR);
	if (error != 0)
		return errno;

	/* Succeeded: removed. */
	return 0;
}

/*
 * Reports whether a directory entry is "." or "..".
 */
static int
admin_dot(
	const char *name)
{
	/* ".". */
	if (name[0] == '.' && name[1] == '\0')
		return 1;

	/* "..". */
	if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
		return 1;

	/* Another name. */
	return 0;
}

/*
 * Writes the answer line on standard output.
 */
static void
admin_answer(
	int reason)
{
	/* "ok", or "error" and the reason's word. */
	if (reason == ADMIN_OK)
		(void)printf("ok\n");
	else
		(void)printf("error %s\n", admin_reason_word(reason));
	(void)fflush(stdout);
}

/*
 * Opens /dev/null on any of descriptors 0 to 2 that is closed, so that
 * nothing written later lands in a file opened there.
 */
static void
admin_standard_descriptors(void)
{
	int descriptor;
	int flags;
	int index;

	/* Each of the three. */
	for (index = 0; index < 3; index++) {
		/* Open already. */
		flags = fcntl(index, F_GETFD);
		if (flags != -1)
			continue;

		/* /dev/null in its place, or nothing can be done safely. */
		descriptor = open("/dev/null", O_RDWR);
		if (descriptor < 0)
			_exit(1);
	}
}

/*
 * Overwrites a buffer that held a secret.
 */
static void
admin_wipe(
	char *buffer,
	size_t size)
{
	volatile char *byte;
	size_t index;

	/* Byte by byte, so that the compiler keeps the writes. */
	byte = buffer;
	for (index = 0; index < size; index++)
		byte[index] = '\0';
}
