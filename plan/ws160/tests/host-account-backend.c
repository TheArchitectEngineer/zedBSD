/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the zedBSD backend's password change (ws160-p002):
 * userland/desktop/libkeiland-backend-zedbsd/account-zedbsd.c with
 * ACCOUNT_PASSWD_PATH set to a stand-in passwd (fake-passwd.sh) that
 * checks it was started as "passwd -s", writes the two lines it read to a
 * file and exits with the status FAKE_PASSWD_STATUS says.  It checks the
 * lines, the mapping of the exit statuses (0, 3 EACCES, 4 and 5 EINVAL,
 * others EIO), and the refusals before passwd runs (an empty password, a
 * line end, a password longer than 256 bytes).
 *
 *   sh plan/ws160/tests/run-host-account-backend.sh
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *what);
static int run(const char *status, const char *current, const char *fresh);

/* The checks that failed, and those that ran; the file the stand-in writes. */
static int failures;
static int checks;
static const char *lines_path;

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Runs a change with the stand-in exiting with status. */
static int
run(
	const char *status,
	const char *current,
	const char *fresh)
{
	int error;

	/* The stand-in's status, and no lines from before. */
	(void)setenv("FAKE_PASSWD_STATUS", status, 1);
	(void)remove(lines_path);

	/* The change. */
	error = kl_backend_account_set_password(current, fresh);
	return error;
}

int
main(
	int argc,
	char **argv)
{
	char long_password[300];
	char text[128];
	FILE *file;
	size_t length;
	int error;

	/* Where the stand-in writes. */
	if (argc < 2)
		return 2;
	lines_path = argv[1];
	(void)setenv("FAKE_PASSWD_LINES", lines_path, 1);

	/* A change: the two lines reach passwd -s, status 0 is 0. */
	error = run("0", "kei", "newpass123");
	check(error == 0, "status 0 is success");
	file = fopen(lines_path, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1, sizeof(text) - 1U, file);
		(void)fclose(file);
	}

	/* What the stand-in read. */
	text[length] = '\0';
	check(strcmp(text, "args=-s\nkei\nnewpass123\n") == 0, "passwd -s read the current and the new password, one a line");

	/* The statuses. */
	check(run("3", "wrong", "newpass123") == EACCES, "status 3 (the current password wrong) is EACCES");
	check(run("4", "kei", "short") == EINVAL, "status 4 (refused) is EINVAL");
	check(run("5", "kei", "x") == EINVAL, "status 5 (mismatch) is EINVAL");
	check(run("1", "kei", "newpass123") == EIO, "status 1 is EIO");
	check(run("9", "kei", "newpass123") == EIO, "another status is EIO");

	/* Refused before passwd runs. */
	check(run("0", "", "newpass123") == EINVAL, "an empty current password is refused");
	check(run("0", "kei", "") == EINVAL, "an empty new password is refused");
	check(run("0", "kei", "new\npass") == EINVAL, "a line end is refused");
	memset(long_password, 'a', sizeof(long_password) - 1U);
	long_password[sizeof(long_password) - 1U] = '\0';
	check(run("0", "kei", long_password) == EINVAL, "a password longer than 256 bytes is refused");
	file = fopen(lines_path, "r");
	check(file == NULL, "and passwd did not run for them");
	if (file != NULL)
		(void)fclose(file);

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-account-backend: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-account-backend: %d checks passed\n", checks);
	return 0;
}
