/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the accounts' shared core (ws160-p001):
 * userland/base/common/account.c with the host's C library (its crypt for
 * SHA-512 crypt).  It checks the new password's rules, the hash (a
 * "$6$rounds=65536$" setting with sixteen salt characters, which crypt
 * gives back for the same password and not for another, a new salt each
 * time), the replacement of a user's line in a shadow text (the hash and
 * the day replaced, every other byte kept, the last line without its end,
 * a user whose name is the start of another's, a missing user, a line
 * without its fields, a hash with a colon, a too small output), and the
 * environment of a command run as another user (only TERM, LANG, LC_* and
 * TZ of the caller; HOME, SHELL, USER, LOGNAME, PATH and the SUDO_*).
 *
 *   sh plan/ws160/tests/run-host-account.sh
 */

#include "userland/base/common/account.h"

#include <crypt.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void check(int condition, const char *what);
static int has(char **variables, const char *variable);

/* The checks that failed, and those that ran. */
static int failures;
static int checks;

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

/* Tells whether the environment holds a variable exactly. */
static int
has(
	char **variables,
	const char *variable)
{
	size_t index;
	int same;

	/* Each variable. */
	for (index = 0; variables[index] != NULL; index++) {
		same = strcmp(variables[index], variable);
		if (same == 0)
			return 1;
	}

	/* None. */
	return 0;
}

int
main(void)
{
	static const char shadow[] =
		"root:*:20000:0:99999:7:::\n"
		"kei:$6$old$hash:20000:0:99999:7:::\n"
		"keiko:$6$x$y:20001::::::\n"
		"last:$6$a$b:1:2:3:4:5:6:7";
	static char *caller[] = { "TERM=xterm", "LANG=ja_JP.UTF-8", "LC_ALL=C", "TZ=Asia/Tokyo", "LD_PRELOAD=/tmp/x.so",
				  "IFS=x", "PATH=/tmp:/bin", "HOME=/home/kei", "ENV=/tmp/e", "COLORTERM=truecolor", NULL };
	struct account_environment_input input;
	char output[1024];
	char hash[ACCOUNT_HASH_MAX];
	char second[ACCOUNT_HASH_MAX];
	char storage[2048];
	char *variables[ACCOUNT_ENVIRONMENT_MAX];
	const char *checked;
	size_t written;
	size_t count;
	int error;
	int same;

	/* The rules: a user's choice is at least 8 characters, root's anything not empty. */
	check(account_password_check("", NULL, 1) == ACCOUNT_PASSWORD_SHORT, "an empty password is refused, even for root");
	check(account_password_check("kei", NULL, 0) == ACCOUNT_PASSWORD_SHORT, "a user's 3 characters are too few");
	check(account_password_check("kei", NULL, 1) == ACCOUNT_PASSWORD_OK, "root may set 3 characters");
	check(account_password_check("abcdefgh", NULL, 0) == ACCOUNT_PASSWORD_OK, "8 characters are enough");
	check(account_password_check("abc\tdefgh", NULL, 0) == ACCOUNT_PASSWORD_CHARACTER, "a control character is refused");
	check(account_password_check("パスワードです", NULL, 0) == ACCOUNT_PASSWORD_OK, "UTF-8 is taken");
	check(account_password_check("abcdefgh", "abcdefgh", 0) == ACCOUNT_PASSWORD_SAME, "the old password is refused");

	/* The hash: SHA-512 crypt with 65536 rounds and a 16-character salt; crypt checks it. */
	error = account_password_hash("correct horse", hash, sizeof(hash));
	check(error == 0, "the hash is made");
	check(strncmp(hash, "$6$rounds=65536$", 16) == 0, "SHA-512 crypt, 65536 rounds");
	check(strlen(hash) > 16U + 17U && hash[16 + 16] == '$', "a 16-character salt");
	checked = crypt("correct horse", hash);
	check(checked != NULL && strcmp(checked, hash) == 0, "crypt gives the hash back for the password");
	checked = crypt("wrong horse", hash);
	check(checked != NULL && strcmp(checked, hash) != 0, "and not for another");
	error = account_password_hash("correct horse", second, sizeof(second));
	check(error == 0 && strcmp(hash, second) != 0, "a new salt each time");
	error = account_password_hash("correct horse", second, 20U);
	check(error == ERANGE, "a hash that does not fit is refused");

	/* The replacement of kei's line: the hash and the day, every other byte kept. */
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "kei", "$6$new$h", 20368L, output, sizeof(output), &written);
	output[written] = '\0';
	check(error == 0, "kei's line is replaced");
	same = strcmp(output,
		"root:*:20000:0:99999:7:::\n"
		"kei:$6$new$h:20368:0:99999:7:::\n"
		"keiko:$6$x$y:20001::::::\n"
		"last:$6$a$b:1:2:3:4:5:6:7");
	check(same == 0, "only kei's hash and day changed (keiko untouched)");

	/* The last line, without its end. */
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "last", "$6$z$z", 9L, output, sizeof(output), &written);
	output[written] = '\0';
	check(error == 0 && strstr(output, "last:$6$z$z:9:2:3:4:5:6:7") != NULL && output[written - 1U] == '7', "the last line, without its end");

	/* The refusals. */
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "nobody", "$6$z$z", 9L, output, sizeof(output), &written);
	check(error == ENOENT, "a missing user is ENOENT");
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "ke", "$6$z$z", 9L, output, sizeof(output), &written);
	check(error == ENOENT, "the start of another name is not that user");
	error = account_shadow_replace("kei:$6$x\n", 9U, "kei", "$6$z$z", 9L, output, sizeof(output), &written);
	check(error == EINVAL, "a line without its fields is refused");
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "kei", "$6$a:b", 9L, output, sizeof(output), &written);
	check(error == EINVAL, "a hash with a colon is refused");
	error = account_shadow_replace(shadow, sizeof(shadow) - 1U, "kei", "$6$z$z", 9L, output, 40U, &written);
	check(error == ERANGE, "a too small output is refused");

	/* The environment of sudo: the caller's terminal and locale, the target's, the secure PATH, the SUDO_*. */
	memset(&input, 0, sizeof(input));
	input.caller = caller;
	input.home = "/root";
	input.shell = "/bin/sh";
	input.user = "root";
	input.sudo_user = "kei";
	input.sudo_uid = 1000;
	input.sudo_gid = 1000;
	input.sudo_command = "/bin/id -u";
	count = account_environment(&input, storage, sizeof(storage), variables, ACCOUNT_ENVIRONMENT_MAX);
	check(count == 14U, "fourteen variables");
	check(has(variables, "TERM=xterm") && has(variables, "COLORTERM=truecolor") && has(variables, "LANG=ja_JP.UTF-8") &&
	      has(variables, "LC_ALL=C") && has(variables, "TZ=Asia/Tokyo"), "the caller's terminal, locale and time zone");
	check(!has(variables, "LD_PRELOAD=/tmp/x.so") && !has(variables, "IFS=x") && !has(variables, "ENV=/tmp/e"), "no LD_*, IFS or ENV");
	check(has(variables, "PATH=" ACCOUNT_SECURE_PATH) && !has(variables, "PATH=/tmp:/bin"), "the secure PATH, not the caller's");
	check(has(variables, "HOME=/root") && !has(variables, "HOME=/home/kei"), "the target's HOME");
	check(has(variables, "USER=root") && has(variables, "LOGNAME=root") && has(variables, "SHELL=/bin/sh"), "the target's USER, LOGNAME, SHELL");
	check(has(variables, "SUDO_USER=kei") && has(variables, "SUDO_UID=1000") && has(variables, "SUDO_GID=1000") &&
	      has(variables, "SUDO_COMMAND=/bin/id -u"), "the SUDO_* variables");
	check(variables[count] == NULL, "the list is ended");

	/* Without sudo (su's login shell), no SUDO_*; and a list that does not fit is refused. */
	input.sudo_user = NULL;
	count = account_environment(&input, storage, sizeof(storage), variables, ACCOUNT_ENVIRONMENT_MAX);
	check(count == 10U && !has(variables, "SUDO_USER=kei"), "no SUDO_* without sudo");
	count = account_environment(&input, storage, 40U, variables, ACCOUNT_ENVIRONMENT_MAX);
	check(count == 0U, "a list that does not fit is refused");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-account: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-account: %d checks passed\n", checks);
	return 0;
}
