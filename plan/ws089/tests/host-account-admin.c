/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p026: the host test of account-admin's pure part
 * (userland/base/account-admin/edit.c): the request's reading, the rules
 * of names, the free number, and the edits of passwd, group and shadow
 * texts -- with the image's own files (root and wheel GID 0, kei 1000).
 * A group operation never changes a primary group: root's stays 0.
 *
 * Prints "PASS name" or "FAIL name ..." for each check and a last line
 * "host-account-admin: N checks, F failed"; exits with 1 when one failed.
 */

#include "userland/base/account-admin/edit.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The image's files. */
static const char test_passwd[] =
	"root:x:0:0:System Administrator:/root:/bin/sh\n"
	"sshd:x:22:22:sshd privilege separation:/var/empty:/sbin/nologin\n"
	"_greeter:x:78:78:Graphical login greeter:/var/empty:/sbin/nologin\n"
	"kei:x:1000:1000:Kei:/home/kei:/bin/sh\n";
static const char test_group[] =
	"wheel:x:0:root,kei\n"
	"network:x:69:kei\n"
	"sshd:x:22:\n"
	"_greeter:x:78:\n"
	"kei:x:1000:\n";

/* A request with a NUL inside, a group using 1001, and a name that starts like another. */
static const char test_with_nul[] = "secret\nremove\nbob\0x\nkeep-home\n";
static const char test_group_1001[] = "wheel:x:0:root,kei\nkei:x:1000:\nvideo:x:1001:\n";
static const char test_similar[] = "ke:x:1001:1001::/home/ke:/bin/sh\nkei:x:1000:1000::/home/kei:/bin/sh\n";

/* The checks run and failed. */
static int test_count;
static int test_failed;

int main(void);
static void test_check(const char *name, int ok, const char *detail);
static void test_text(const char *name, const char *got, size_t length, const char *expected);
static int test_parse(const char *text, struct admin_request *request);

/*
 * Runs the checks.
 */
int
main(void)
{
	struct admin_request request;
	char with_nul[sizeof(test_with_nul)];
	char output[4096];
	char second[4096];
	size_t written;
	size_t second_written;
	long id;
	int reason;
	int error;

	/* A request of each operation. */
	reason = test_parse("secret\nadd\nbob\nBob Smith\nhunter22\nadmin\n", &request);
	test_check("parse-add", reason == ADMIN_OK && request.operation == ADMIN_ADD && strcmp(request.name, "bob") == 0 &&
		   strcmp(request.display, "Bob Smith") == 0 && strcmp(request.fresh, "hunter22") == 0 && request.administrator == 1, "");
	reason = test_parse("secret\nremove\nbob\nremove-home", &request);
	test_check("parse-remove-no-end", reason == ADMIN_OK && request.operation == ADMIN_REMOVE && request.remove_home == 1, "");
	reason = test_parse("secret\nreset-password\nbob\nnewpass99\n", &request);
	test_check("parse-reset", reason == ADMIN_OK && strcmp(request.fresh, "newpass99") == 0, "");
	reason = test_parse("secret\ngroup-add\nbob\nnetwork\n", &request);
	test_check("parse-group", reason == ADMIN_OK && request.operation == ADMIN_GROUP_ADD && strcmp(request.group, "network") == 0, "");

	/* Requests refused: another group, a word not known, too few or too many lines, no password, a NUL. */
	reason = test_parse("secret\ngroup-add\nbob\noperator\n", &request);
	test_check("parse-other-group", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("secret\nchown\nbob\nx\n", &request);
	test_check("parse-unknown", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("secret\nadd\nbob\nBob\nhunter22\n", &request);
	test_check("parse-few", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("secret\nremove\nbob\nkeep-home\nextra\n", &request);
	test_check("parse-many", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("\nremove\nbob\nkeep-home\n", &request);
	test_check("parse-no-password", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("secret\nadd\nbob\nBob\nhunter22\nroot\n", &request);
	test_check("parse-bad-kind", reason == ADMIN_BAD_REQUEST, "");
	memcpy(with_nul, test_with_nul, sizeof(test_with_nul));
	reason = admin_parse(with_nul, sizeof(test_with_nul) - 1U, &request);
	test_check("parse-nul", reason == ADMIN_BAD_REQUEST, "");

	/* The words. */
	test_check("words", strcmp(admin_reason_word(ADMIN_LAST_ADMINISTRATOR), "last-administrator") == 0 &&
		   strcmp(admin_reason_word(ADMIN_HOME_EXISTS), "home-exists") == 0 && strcmp(admin_reason_word(999), "failed") == 0 &&
		   strcmp(admin_operation_word(ADMIN_RESET_PASSWORD), "reset-password") == 0, "");

	/* Names and display names. */
	test_check("name-ok", admin_name_valid("bob") && admin_name_valid("a-b_c1") && admin_name_valid("abcdefghijklmnopqrstuvwxyz012345"), "");
	test_check("name-bad", !admin_name_valid("Bob") && !admin_name_valid("1ab") && !admin_name_valid("") && !admin_name_valid("a b") &&
		   !admin_name_valid("abcdefghijklmnopqrstuvwxyz0123456") && !admin_name_valid("a:b"), "");
	test_check("display", admin_display_valid("Bob Smith") && admin_display_valid("\xe7\x94\xb0\xe4\xb8\xad") && !admin_display_valid("a:b") &&
		   !admin_display_valid("a\tb"), "");

	/* Fields and the free number (1000 is kei's; 1001 next; a group's 1001 skips it). */
	test_check("field", admin_number_field(test_passwd, strlen(test_passwd), "kei", 2U) == 1000 &&
		   admin_number_field(test_group, strlen(test_group), "wheel", 2U) == 0 &&
		   admin_number_field(test_passwd, strlen(test_passwd), "nobody", 2U) == -1, "");
	id = admin_free_id(test_passwd, strlen(test_passwd), test_group, strlen(test_group));
	test_check("free-id", id == 1001, "");
	id = admin_free_id(test_passwd, strlen(test_passwd), test_group_1001, strlen(test_group_1001));
	test_check("free-id-group", id == 1002, "");

	/* An addition: the line at the end. */
	error = admin_line_append(test_group, strlen(test_group), "bob:x:1001:", output, sizeof(output), &written);
	test_check("append", error == 0, "");
	test_text("append-text", output, written, "wheel:x:0:root,kei\nnetwork:x:69:kei\nsshd:x:22:\n_greeter:x:78:\nkei:x:1000:\nbob:x:1001:\n");
	error = admin_line_append("a:x:1:", 6U, "b:x:2:", output, sizeof(output), &written);
	test_text("append-no-end", output, written, "a:x:1:\nb:x:2:\n");

	/* wheel: bob made a member, kei taken out; root stays, its line and its primary group 0 unchanged. */
	error = admin_member_set(test_group, strlen(test_group), "wheel", "bob", 1, output, sizeof(output), &written);
	test_check("wheel-add", error == 0, "");
	test_text("wheel-add-text", output, written, "wheel:x:0:root,kei,bob\nnetwork:x:69:kei\nsshd:x:22:\n_greeter:x:78:\nkei:x:1000:\n");
	error = admin_member_set(test_group, strlen(test_group), "wheel", "kei", 0, output, sizeof(output), &written);
	test_text("wheel-remove-text", output, written, "wheel:x:0:root\nnetwork:x:69:kei\nsshd:x:22:\n_greeter:x:78:\nkei:x:1000:\n");
	test_check("root-primary-kept", admin_number_field(output, written, "wheel", 2U) == 0 &&
		   admin_in_group(output, written, "wheel", "root", 0) == 1, "");
	error = admin_member_set(test_group, strlen(test_group), "wheel", "kei", 1, output, sizeof(output), &written);
	test_text("wheel-add-again", output, written, test_group);
	error = admin_member_set(test_group, strlen(test_group), "operator", "kei", 1, output, sizeof(output), &written);
	test_check("group-missing", error == ENOENT, "");
	error = admin_member_set(test_group, strlen(test_group), "sshd", "bob", 1, output, sizeof(output), &written);
	test_text("empty-list-add", output, written, "wheel:x:0:root,kei\nnetwork:x:69:kei\nsshd:x:22:bob\n_greeter:x:78:\nkei:x:1000:\n");

	/* A removal: kei's lines out, kei out of every list. */
	error = admin_line_remove(test_passwd, strlen(test_passwd), "kei", output, sizeof(output), &written);
	test_text("remove-passwd", output, written,
		  "root:x:0:0:System Administrator:/root:/bin/sh\n"
		  "sshd:x:22:22:sshd privilege separation:/var/empty:/sbin/nologin\n"
		  "_greeter:x:78:78:Graphical login greeter:/var/empty:/sbin/nologin\n");
	error = admin_line_remove(test_passwd, strlen(test_passwd), "bob", output, sizeof(output), &written);
	test_check("remove-missing", error == ENOENT, "");
	error = admin_line_remove(test_group, strlen(test_group), "kei", second, sizeof(second), &second_written);
	error = admin_member_forget(second, second_written, "kei", output, sizeof(output), &written);
	test_text("forget", output, written, "wheel:x:0:root\nnetwork:x:69:\nsshd:x:22:\n_greeter:x:78:\n");

	/* A name that only starts like another is not it. */
	error = admin_line_remove(test_similar, strlen(test_similar), "ke", output, sizeof(output), &written);
	test_text("remove-exact", output, written, "kei:x:1000:1000::/home/kei:/bin/sh\n");

	/* Administrators among the people's accounts: kei alone (root is not counted), none without kei. */
	test_check("administrators", admin_administrators(test_passwd, strlen(test_passwd), test_group, strlen(test_group), NULL) == 1U &&
		   admin_administrators(test_passwd, strlen(test_passwd), test_group, strlen(test_group), "kei") == 0U, "");

	/* An output too small. */
	error = admin_line_append(test_group, strlen(test_group), "bob:x:1001:", output, 10U, &written);
	test_check("too-small", error == ERANGE, "");

	/* The result. */
	printf("host-account-admin: %d checks, %d failed\n", test_count, test_failed);
	if (test_failed != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Counts and prints one check.
 */
static void
test_check(
	const char *name,
	int ok,
	const char *detail)
{
	/* Passed or failed. */
	test_count++;
	if (ok) {
		printf("PASS %s\n", name);
	} else {
		printf("FAIL %s %s\n", name, detail);
		test_failed++;
	}
}

/*
 * Checks that an edited text is the one expected.
 */
static void
test_text(
	const char *name,
	const char *got,
	size_t length,
	const char *expected)
{
	char shown[4096];
	int same;

	/* The same bytes. */
	same = length == strlen(expected) && memcmp(got, expected, length) == 0;
	(void)snprintf(shown, sizeof(shown), "got \"%.*s\"", (int)length, got);
	test_check(name, same, shown);
}

/*
 * Parses a request from a copy of a string.
 */
static int
test_parse(
	const char *text,
	struct admin_request *request)
{
	static char copy[ADMIN_REQUEST_MAX + 1U];
	size_t length;

	/* A copy, ended, as account-admin reads it. */
	length = strlen(text);
	memcpy(copy, text, length + 1U);
	return admin_parse(copy, length, request);
}
