/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the compositor's PIN store (ws163-p002,
 * userland/desktop/wayland/pin-store.c): the file under a scratch home,
 * the six-digit rule, right and wrong PINs, the count of wrong ones kept
 * in the file, the PIN turned off after five and back after a password,
 * a broken file, the file's mode, and the removal.
 *
 * usage: pin-store-host-test SCRATCH   (SCRATCH an empty folder)
 */

#include "userland/desktop/wayland/pin-store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The checks that failed. */
static unsigned test_failures;

static void test_expect(int condition, const char *what);
static int test_write_text(const char *path, const char *text);

/* Runs the checks; the exit status is 0 only when all of them hold. */
int
main(
	int argc,
	char **argv)
{
	struct zwl_pin_record record;
	struct stat status;
	char path[ZWL_PIN_PATH_MAX];
	char wrong[8];
	unsigned index;
	int error;

	/* The scratch home. */
	if (argc != 2) {
		fprintf(stderr, "usage: %s SCRATCH\n", argv[0]);
		return 2;
	}

	/* The file's path under the home; a relative home has none. */
	error = zwl_pin_store_path(argv[1], path, sizeof(path));
	test_expect(error == 0, "path under the home");
	error = zwl_pin_store_path("relative", wrong, sizeof(wrong));
	test_expect(error == ENOENT, "relative home has no file");
	error = zwl_pin_store_path("/a/very/long/home", wrong, sizeof(wrong));
	test_expect(error == ENAMETOOLONG, "a path that does not fit");

	/* Six digits are a PIN, nothing else is. */
	test_expect(zwl_pin_is_pin("012345"), "six digits");
	test_expect(!zwl_pin_is_pin("01234"), "five digits");
	test_expect(!zwl_pin_is_pin("0123456"), "seven digits");
	test_expect(!zwl_pin_is_pin("01234a"), "a letter");
	test_expect(!zwl_pin_is_pin(""), "empty");
	test_expect(!zwl_pin_is_pin(NULL), "none");

	/* No PIN yet. */
	error = zwl_pin_store_read(path, &record);
	test_expect(error == ENOENT, "no file before a PIN");
	test_expect(!zwl_pin_store_usable(path), "no PIN is not usable");
	error = zwl_pin_store_check(path, "123456");
	test_expect(error == ENOENT, "a check without a PIN");

	/* Setting a PIN that is not six digits is refused. */
	error = zwl_pin_store_set(path, "12345");
	test_expect(error == EINVAL, "set refuses five digits");

	/* Setting a PIN makes the folders and the file, the user's alone, without the PIN in it. */
	error = zwl_pin_store_set(path, "246810");
	test_expect(error == 0, "set");
	error = stat(path, &status);
	test_expect(error == 0 && (status.st_mode & 0777) == 0600, "file mode 0600");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == 0 && record.failures == 0U, "read back, no failures");
	test_expect(strncmp(record.hash, "$6$rounds=20000$", 16U) == 0, "SHA-512 crypt hash");
	test_expect(strstr(record.hash, "246810") == NULL, "the PIN is not in the file");
	test_expect(zwl_pin_store_usable(path), "usable");

	/* The right PIN, then a wrong one counted, then the right one forgets it. */
	error = zwl_pin_store_check(path, "246810");
	test_expect(error == 0, "right PIN");
	error = zwl_pin_store_check(path, "246811");
	test_expect(error == EACCES, "wrong PIN");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == 0 && record.failures == 1U, "one failure kept");
	error = zwl_pin_store_check(path, "246810");
	test_expect(error == 0, "right PIN after a wrong one");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == 0 && record.failures == 0U, "failures forgotten");
	error = zwl_pin_store_check(path, "24681");
	test_expect(error == EINVAL, "a check of five digits");

	/* Five wrong in a row turn the PIN off; even the right one is then refused. */
	for (index = 0; index < ZWL_PIN_TRIES; index++) {
		error = zwl_pin_store_check(path, "000000");
		test_expect(error == EACCES, "wrong PIN in a row");
	}

	/* Now off. */
	test_expect(!zwl_pin_store_usable(path), "off after five");
	error = zwl_pin_store_check(path, "246810");
	test_expect(error == EPERM, "right PIN refused while off");

	/* A password unlock forgives them: the PIN works again. */
	error = zwl_pin_store_forgive(path);
	test_expect(error == 0, "forgive");
	test_expect(zwl_pin_store_usable(path), "usable after forgive");
	error = zwl_pin_store_check(path, "246810");
	test_expect(error == 0, "right PIN after forgive");

	/* A new PIN replaces the old one. */
	error = zwl_pin_store_set(path, "135790");
	test_expect(error == 0, "change");
	error = zwl_pin_store_check(path, "246810");
	test_expect(error == EACCES, "old PIN no longer right");
	error = zwl_pin_store_check(path, "135790");
	test_expect(error == 0, "new PIN right");

	/* A file that is not a PIN file is no PIN. */
	error = test_write_text(path, "hash plain\nfailures 0\n");
	test_expect(error == 0, "write a broken file");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == EINVAL, "not a crypt hash");
	test_expect(!zwl_pin_store_usable(path), "broken file is not usable");
	error = test_write_text(path, "failures 0\n");
	test_expect(error == 0, "write a file without a hash");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == EINVAL, "no hash");
	error = test_write_text(path, "hash $6$x$y\nfailures many\n");
	test_expect(error == 0, "write a bad count");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == EINVAL, "bad count");
	error = test_write_text(path, "hash $6$x$y\nfailures 99\n");
	test_expect(error == 0, "write a large count");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == 0 && record.failures == ZWL_PIN_TRIES, "large count is off");

	/* Removing it leaves no PIN, and removing nothing is no error. */
	error = zwl_pin_store_remove(path);
	test_expect(error == 0, "remove");
	error = zwl_pin_store_read(path, &record);
	test_expect(error == ENOENT, "no file after remove");
	error = zwl_pin_store_remove(path);
	test_expect(error == 0, "remove again");

	/* The verdict. */
	if (test_failures != 0U) {
		printf("pin-store-host-test: FAIL (%u)\n", test_failures);
		return 1;
	}

	/* Every check held. */
	printf("pin-store-host-test: PASS\n");
	return 0;
}

/* Counts and reports a check that does not hold. */
static void
test_expect(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* One that does not. */
	test_failures++;
	printf("FAIL: %s\n", what);
}

/* Writes a file's text by hand (a broken file); returns 0 or an errno value. */
static int
test_write_text(
	const char *path,
	const char *text)
{
	FILE *file;
	int closed;

	/* The file, replaced. */
	file = fopen(path, "w");
	if (file == NULL)
		return errno;
	(void)fputs(text, file);

	/* Closed and flushed. */
	closed = fclose(file);
	if (closed != 0)
		return EIO;

	/* Succeeded: the file holds the text. */
	return 0;
}
