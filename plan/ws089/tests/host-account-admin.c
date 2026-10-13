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
 * ws196-p002 adds root's form of the request (user-add, user-modify,
 * user-remove), the count of the administrators who can log in, the next
 * user ID never given, a field's replacement, the lock of a password, and
 * which group named after a removed user goes with it.
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

/*
 * ws196-p002: administrators who can or cannot log in -- kei (a hash), lin
 * (locked), mo (no password), noa (nologin), oto (expired on day 20000),
 * pia (expires later), quin (no shadow line), rei (wheel as its primary
 * group); and hal, whose group is ivy's primary group.
 */
static const char test_admins_passwd[] =
	"root:x:0:0::/root:/bin/sh\n"
	"kei:x:1000:1000::/home/kei:/bin/sh\n"
	"lin:x:1001:1001::/home/lin:/bin/sh\n"
	"mo:x:1002:1002::/home/mo:/bin/sh\n"
	"noa:x:1003:1003::/home/noa:/sbin/nologin\n"
	"oto:x:1004:1004::/home/oto:/bin/sh\n"
	"pia:x:1005:1005::/home/pia:/bin/sh\n"
	"quin:x:1006:1006::/home/quin:/bin/sh\n"
	"rei:x:1007:0::/home/rei:/bin/sh\n"
	"hal:x:1008:1008::/home/hal:/bin/sh\n"
	"ivy:x:1009:1008::/home/ivy:/bin/sh\n";
static const char test_admins_group[] =
	"wheel:x:0:root,kei,lin,mo,noa,oto,pia,quin\n"
	"network:x:69:\n"
	"kei:x:1000:\n"
	"hal:x:1008:\n"
	"ivy:x:1009:\n"
	"pair:x:1010:kei,lin\n";
static const char test_admins_shadow[] =
	"root:$6$r$x:20000:0:99999:7:::\n"
	"kei:$6$k$x:20000:0:99999:7:::\n"
	"lin:!$6$l$x:20000:0:99999:7:::\n"
	"mo:!:20000:0:99999:7:::\n"
	"noa:$6$n$x:20000:0:99999:7:::\n"
	"oto:$6$o$x:20000:0:99999:7::20000:\n"
	"pia:$6$p$x:20000:0:99999:7::20001:\n"
	"rei:$6$e$x:20000:0:99999:7:::\n";

/* The checks run and failed. */
static int test_count;
static int test_failed;

int main(void);
static void test_check(const char *name, int ok, const char *detail);
static void test_text(const char *name, const char *got, size_t length, const char *expected);
static int test_parse(const char *text, struct admin_request *request);
static int test_parse_root(const char *text, struct admin_request *request);
static void test_root(void);
static void test_counting(void);
static void test_edits(void);

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
	reason = admin_parse(with_nul, sizeof(test_with_nul) - 1U, 0, &request);
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

	/* Fields and the next number (1000 is kei's; 1001 next; a group's 1001 skips it). */
	test_check("field", admin_number_field(test_passwd, strlen(test_passwd), "kei", 2U) == 1000 &&
		   admin_number_field(test_group, strlen(test_group), "wheel", 2U) == 0 &&
		   admin_number_field(test_passwd, strlen(test_passwd), "nobody", 2U) == -1, "");
	id = admin_next_id(test_passwd, strlen(test_passwd), test_group, strlen(test_group), 0);
	test_check("next-id", id == 1001, "");
	id = admin_next_id(test_passwd, strlen(test_passwd), test_group_1001, strlen(test_group_1001), 0);
	test_check("next-id-group", id == 1002, "");

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
	error = admin_line_remove(test_passwd, strlen(test_passwd), "kei", output, sizeof(output), &written);
	test_check("administrators", admin_administrators(test_passwd, strlen(test_passwd), test_group, strlen(test_group),
							  "kei:$6$k$x:20000:0:99999:7:::\n", 31U, 20000L) == 1U &&
		   admin_administrators(output, written, test_group, strlen(test_group), "kei:$6$k$x:20000:0:99999:7:::\n", 31U, 20000L) == 0U, "");

	/* An output too small. */
	error = admin_line_append(test_group, strlen(test_group), "bob:x:1001:", output, 10U, &written);
	test_check("too-small", error == ERANGE, "");

	/* ws196-p002. */
	test_root();
	test_counting();
	test_edits();

	/* The result. */
	printf("host-account-admin: %d checks, %d failed\n", test_count, test_failed);
	if (test_failed != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Checks root's form of the request (ws196-p002): each operation's keys,
 * their values' rules, and the rules across the keys.
 */
static void
test_root(void)
{
	struct admin_request request;
	int reason;

	/* useradd with every key. */
	reason = test_parse_root("user-add\nname bob\ncomment Bob Smith\nshell /bin/sh\nuid 1500\ngroup wheel\ngroups wheel,network\nhome none\n", &request);
	test_check("root-add", reason == ADMIN_OK && request.operation == ADMIN_USER_ADD && request.by_root == 1 &&
		   strcmp(request.name, "bob") == 0 && strcmp(request.display, "Bob Smith") == 0 && strcmp(request.shell, "/bin/sh") == 0 &&
		   request.uid == 1500 && strcmp(request.primary, "wheel") == 0 && strcmp(request.groups, "wheel,network") == 0 &&
		   request.groups_append == 0 && request.make_home == 0 && request.password == NULL, "");

	/* useradd's defaults: no ID chosen, a home made, no display name or shell. */
	reason = test_parse_root("user-add\nname bob", &request);
	test_check("root-add-defaults", reason == ADMIN_OK && request.uid == -1 && request.make_home == 1 && request.display == NULL &&
		   request.shell == NULL && request.primary == NULL && request.groups == NULL, "");

	/* An empty comment, and the user's own group as the primary one. */
	reason = test_parse_root("user-add\ncomment \nname bob\ngroup bob\n", &request);
	test_check("root-add-own-group", reason == ADMIN_OK && strcmp(request.display, "") == 0 && strcmp(request.primary, "bob") == 0, "");

	/* usermod: the groups added to, the lock, then the unlock. */
	reason = test_parse_root("user-modify\nname bob\ngroups-add wheel\npassword lock\n", &request);
	test_check("root-modify", reason == ADMIN_OK && request.operation == ADMIN_USER_MODIFY && request.groups_append == 1 &&
		   strcmp(request.groups, "wheel") == 0 && request.lock == ADMIN_LOCK_SET, "");
	reason = test_parse_root("user-modify\nname bob\npassword unlock\n", &request);
	test_check("root-modify-unlock", reason == ADMIN_OK && request.lock == ADMIN_LOCK_CLEAR, "");
	reason = test_parse_root("user-modify\nname bob\ngroups \n", &request);
	test_check("root-modify-no-groups", reason == ADMIN_OK && strcmp(request.groups, "") == 0 && request.groups_append == 0, "");

	/* userdel: the home removed, or a busy user forced. */
	reason = test_parse_root("user-remove\nname bob\nhome remove\n", &request);
	test_check("root-remove", reason == ADMIN_OK && request.operation == ADMIN_USER_REMOVE && request.remove_home == 1 && request.force == 0, "");
	reason = test_parse_root("user-remove\nname bob\nforce yes\n", &request);
	test_check("root-remove-force", reason == ADMIN_OK && request.force == 1 && request.remove_home == 0, "");

	/* Refused forms: Settings' operation, root's from Settings, an unknown key, a key twice, a key not taken, no space. */
	reason = test_parse_root("add\nname bob\n", &request);
	test_check("root-settings-word", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse("secret\nuser-add\nname bob\n", &request);
	test_check("settings-root-word", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-add\nname bob\nhome-dir /x\n", &request);
	test_check("root-unknown-key", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-add\nname bob\nname carol\n", &request);
	test_check("root-twice", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-add\nname bob\npassword lock\n", &request);
	test_check("root-not-taken", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-remove\nname bob\nforce\n", &request);
	test_check("root-no-space", reason == ADMIN_BAD_REQUEST, "");

	/* Refused across the keys: no name, both groups, a modification of nothing, a home removed by force. */
	reason = test_parse_root("user-add\ncomment x\n", &request);
	test_check("root-no-name", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-modify\nname bob\ngroups wheel\ngroups-add network\n", &request);
	test_check("root-both-groups", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-modify\nname bob\n", &request);
	test_check("root-modify-nothing", reason == ADMIN_BAD_REQUEST, "");
	reason = test_parse_root("user-remove\nname bob\nhome remove\nforce yes\n", &request);
	test_check("root-remove-home-force", reason == ADMIN_BAD_REQUEST, "");

	/* Values outside the rules: user IDs, shells, groups, a primary group, the lock's word, the home's. */
	reason = test_parse_root("user-add\nname bob\nuid 0\n", &request);
	test_check("root-uid-0", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\nuid 60000\n", &request);
	test_check("root-uid-high", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\nuid 15x\n", &request);
	test_check("root-uid-text", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\nshell sh\n", &request);
	test_check("root-shell-relative", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\nshell /bin/s:h\n", &request);
	test_check("root-shell-colon", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\ngroups wheel,_passkey\n", &request);
	test_check("root-groups-system", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\ngroups wheel,\n", &request);
	test_check("root-groups-empty-item", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-modify\nname bob\ngroups-add \n", &request);
	test_check("root-groups-add-empty", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\ngroup _greeter\n", &request);
	test_check("root-group-system", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\ngroup kei\n", &request);
	test_check("root-group-other", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-modify\nname bob\npassword open\n", &request);
	test_check("root-lock-word", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\nhome remove\n", &request);
	test_check("root-home-word", reason == ADMIN_BAD_VALUE, "");
	reason = test_parse_root("user-add\nname bob\ncomment a:b\n", &request);
	test_check("root-comment-colon", reason == ADMIN_BAD_VALUE, "");

	/* The words of the new reasons, after the old ones. */
	test_check("words-root", strcmp(admin_reason_word(ADMIN_FAILED), "failed") == 0 && strcmp(admin_reason_word(ADMIN_BAD_VALUE), "bad-value") == 0 &&
		   strcmp(admin_reason_word(ADMIN_ID_TAKEN), "id-taken") == 0 && strcmp(admin_reason_word(ADMIN_NO_PASSWORD), "no-password") == 0 &&
		   strcmp(admin_reason_word(ADMIN_HOME_FAILED), "home-failed") == 0 &&
		   strcmp(admin_operation_word(ADMIN_USER_REMOVE), "user-remove") == 0, "");
}

/*
 * Checks the count of the administrators who can log in, and the next user
 * ID never given (ws196-p002).
 */
static void
test_counting(void)
{
	char output[4096];
	size_t written;
	size_t count;
	long id;
	int error;

	/* kei, pia (not expired yet) and rei (wheel as its primary group) on day 20000; lin, mo, noa, oto and quin not. */
	count = admin_administrators(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group),
				     test_admins_shadow, strlen(test_admins_shadow), 20000L);
	test_check("count-usable", count == 3U, "");

	/* A day later pia's account has expired too. */
	count = admin_administrators(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group),
				     test_admins_shadow, strlen(test_admins_shadow), 20001L);
	test_check("count-expired", count == 2U, "");

	/* kei locked: one fewer. */
	error = admin_lock_hash(test_admins_shadow, strlen(test_admins_shadow), "kei", ADMIN_LOCK_SET, output, sizeof(output), &written);
	count = admin_administrators(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group),
				     output, written, 20000L);
	test_check("count-locked", error == 0 && count == 2U, "");

	/* rei's primary group no longer wheel: one fewer. */
	error = admin_field_set(test_admins_passwd, strlen(test_admins_passwd), "rei", 3U, "69", output, sizeof(output), &written);
	count = admin_administrators(output, written, test_admins_group, strlen(test_admins_group),
				     test_admins_shadow, strlen(test_admins_shadow), 20000L);
	test_check("count-primary", error == 0 && count == 2U, "");

	/* kei's shell a nologin: one fewer. */
	error = admin_field_set(test_admins_passwd, strlen(test_admins_passwd), "kei", 6U, "/usr/sbin/nologin", output, sizeof(output), &written);
	count = admin_administrators(output, written, test_admins_group, strlen(test_admins_group),
				     test_admins_shadow, strlen(test_admins_shadow), 20000L);
	test_check("count-nologin", error == 0 && count == 2U, "");

	/* Nobody in wheel's list: rei alone. */
	error = admin_member_forget(test_admins_group, strlen(test_admins_group), "kei", output, sizeof(output), &written);
	count = admin_administrators(test_admins_passwd, strlen(test_admins_passwd), output, written,
				     test_admins_shadow, strlen(test_admins_shadow), 20001L);
	test_check("count-wheel-list", error == 0 && count == 1U, "");

	/* The next ID: above the highest used (a gap below is not filled), and above the floor. */
	id = admin_next_id(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group), 0);
	test_check("next-id-highest", id == 1011, "");
	id = admin_next_id(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group), 1500);
	test_check("next-id-floor", id == 1501, "");
	id = admin_next_id("a:x:1000:1000::/:/bin/sh\nb:x:1005:1005::/:/bin/sh\nnobody:x:65534:65534::/:/sbin/nologin\n", 77U, "", 0U, 0);
	test_check("next-id-gap-nobody", id == 1006, "");
	id = admin_next_id(test_admins_passwd, strlen(test_admins_passwd), test_admins_group, strlen(test_admins_group), ADMIN_ID_LAST);
	test_check("next-id-used-up", id == -1, "");

	/* A number used in a field. */
	test_check("number-used", admin_number_used(test_admins_passwd, strlen(test_admins_passwd), 3U, 1008) == 1 &&
		   admin_number_used(test_admins_passwd, strlen(test_admins_passwd), 2U, 1010) == 0 &&
		   admin_number_used(test_admins_group, strlen(test_admins_group), 2U, 1010) == 1, "");
}

/*
 * Checks the field's replacement, the lock of a password, and which group
 * named after a removed user goes with it (ws196-p002).
 */
static void
test_edits(void)
{
	char output[4096];
	char second[4096];
	size_t written;
	size_t second_written;
	int error;

	/* A field replaced; a value that would break the line; a name not there. */
	error = admin_field_set(test_passwd, strlen(test_passwd), "kei", 4U, "Kei K", output, sizeof(output), &written);
	test_check("field-set", error == 0 && strstr(output, "kei:x:1000:1000:Kei K:/home/kei:/bin/sh\n") != NULL && written == strlen(test_passwd) + 2U, "");
	error = admin_field_set(test_passwd, strlen(test_passwd), "kei", 4U, "a:b", output, sizeof(output), &written);
	test_check("field-set-colon", error == EINVAL, "");
	error = admin_field_set(test_passwd, strlen(test_passwd), "bob", 4U, "x", output, sizeof(output), &written);
	test_check("field-set-missing", error == ENOENT, "");

	/* A lock, a lock again, an unlock. */
	error = admin_lock_hash("kei:$6$k$x:1:0:99999:7:::\n", 26U, "kei", ADMIN_LOCK_SET, output, sizeof(output), &written);
	test_text("lock", output, written, "kei:!$6$k$x:1:0:99999:7:::\n");
	error = admin_lock_hash("kei:!$6$k$x:1:0:99999:7:::\n", 27U, "kei", ADMIN_LOCK_SET, output, sizeof(output), &written);
	test_text("lock-again", output, written, "kei:!$6$k$x:1:0:99999:7:::\n");
	error = admin_lock_hash("kei:!$6$k$x:1:0:99999:7:::\n", 27U, "kei", ADMIN_LOCK_CLEAR, output, sizeof(output), &written);
	test_text("unlock", output, written, "kei:$6$k$x:1:0:99999:7:::\n");

	/* An unlock that would leave no password (an account made without one, "!*", "!!"): refused. */
	error = admin_lock_hash("bob:!:1:0:99999:7:::\n", 21U, "bob", ADMIN_LOCK_CLEAR, output, sizeof(output), &written);
	test_check("unlock-no-password", error == EACCES, "");
	error = admin_lock_hash("bob:!*:1:0:99999:7:::\n", 22U, "bob", ADMIN_LOCK_CLEAR, output, sizeof(output), &written);
	test_check("unlock-star", error == EACCES, "");
	error = admin_lock_hash("bob:!!$6$x:1:0:99999:7:::\n", 26U, "bob", ADMIN_LOCK_CLEAR, output, sizeof(output), &written);
	test_check("unlock-twice-locked", error == EACCES, "");
	error = admin_lock_hash("bob:!:1:0:99999:7:::\n", 21U, "kei", ADMIN_LOCK_SET, output, sizeof(output), &written);
	test_check("lock-no-line", error == ENOENT, "");

	/* hal removed: its group is ivy's primary group, so it stays (M7); with ivy gone too, it goes. */
	error = admin_line_remove(test_admins_passwd, strlen(test_admins_passwd), "hal", output, sizeof(output), &written);
	test_check("group-other-primary", admin_group_removable(test_admins_group, strlen(test_admins_group), output, written, "hal", 1008, 1008) == 0, "");
	error = admin_line_remove(output, written, "ivy", second, sizeof(second), &second_written);
	test_check("group-own", admin_group_removable(test_admins_group, strlen(test_admins_group), second, second_written, "hal", 1008, 1008) == 1, "");

	/* A group with other members, a group not the user's number, a system group: they stay. */
	test_check("group-members", admin_group_removable(test_admins_group, strlen(test_admins_group), "", 0U, "pair", 1010, 1010) == 0, "");
	test_check("group-not-own", admin_group_removable(test_admins_group, strlen(test_admins_group), "", 0U, "ivy", 1500, 1500) == 0, "");
	test_check("group-system", admin_group_removable(test_admins_group, strlen(test_admins_group), "", 0U, "network", -1, -1) == 0, "");

	/* What is left of a user without its passwd line: its group goes. */
	test_check("group-rest", admin_group_removable(test_admins_group, strlen(test_admins_group), "", 0U, "ivy", -1, -1) == 1, "");

	/* The shell's and the groups' rules. */
	test_check("shell-valid", admin_shell_valid("/bin/sh") == 1 && admin_shell_valid("/") == 1 && admin_shell_valid("bin/sh") == 0 &&
		   admin_shell_valid("/bin/\tsh") == 0 && admin_shell_valid("") == 0, "");
	test_check("groups-valid", admin_groups_valid("wheel", 0) == 1 && admin_groups_valid("network,wheel", 0) == 1 &&
		   admin_groups_valid("", 1) == 1 && admin_groups_valid("", 0) == 0 && admin_groups_valid("wheels", 0) == 0 &&
		   admin_groups_valid(",wheel", 0) == 0, "");
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
	return admin_parse(copy, length, 0, request);
}

/*
 * Parses root's form of a request from a copy of a string.
 */
static int
test_parse_root(
	const char *text,
	struct admin_request *request)
{
	static char copy[ADMIN_REQUEST_MAX + 1U];
	size_t length;

	/* A copy, ended, as account-admin reads it from root. */
	length = strlen(text);
	memcpy(copy, text, length + 1U);
	return admin_parse(copy, length, 1, request);
}
