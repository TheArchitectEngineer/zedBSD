/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Ending the machine through sessiond (ws131-p027): the login screen's
 * "POWER poweroff|reboot" (greeter.c), and since ws131-p027 a session's
 * (the console session's Power Off dialog, ws099-p037), decided by
 * power-rules.c.  A session's answer is OK, "FAIL others" (other users are
 * logged in and the session's user is neither root nor in wheel) or ERROR
 * (another word, or the program could not be started).  Every request is
 * logged to the authentication log.
 */

#include "sessiond.h"
#include "power-rules.h"
#include "../../base/common/account.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <utmpx.h>

static unsigned power_others(const char *name);

/*
 * Runs the program that ends the machine (it asks init, which stops
 * sessiond among the rest), and logs who asked.  Returns 0, or the errno of
 * the fork.
 */
int
sessiond_power_run(
	const char *program,
	const char *what,
	const char *from)
{
	pid_t child;

	/* Logged first: the machine may be gone soon after. */
	sessiond_log("SESSIOND POWER %s from=%s", what, from);
	syslog(LOG_NOTICE, "%s from %s", what, from);

	/* The program in a child of its own. */
	child = fork();
	if (child < 0)
		return errno;
	if (child == 0) {
		(void)execl(program, program, (char *)NULL);
		_exit(127);
	}

	/* Succeeded: the machine is ending. */
	return 0;
}

/*
 * Answers a session's "POWER poweroff|reboot": decided by power-rules.c with
 * the users logged in, then carried out.
 */
void
sessiond_power_session(
	const struct sessiond_account *account,
	int control,
	const char *what)
{
	const char *program;
	const char *name;
	unsigned others;
	int in_wheel;
	int decided;
	int error;

	/* Decided: the word, the users logged in besides the session's, the user's right. */
	name = account->passwd.pw_name;
	others = power_others(name);
	in_wheel = account_in_wheel(name, account->passwd.pw_gid);
	decided = sessiond_power_decide(what, account->passwd.pw_uid, in_wheel, others, &program);
	if (decided == EPERM) {
		sessiond_log("SESSIOND POWER %.16s by %s refused others=%u", what, name, others);
		syslog(LOG_WARNING, "%.16s by %s refused (%u other users logged in, not in wheel)", what, name, others);
		(void)write(control, "FAIL others\n", 12U);
		return;
	}

	/* Another word is not one a session may ask. */
	if (decided != 0) {
		sessiond_log("SESSIOND POWER \"%.16s\" by %s refused", what, name);
		(void)write(control, "ERROR\n", 6U);
		return;
	}

	/* Carried out; the session hears that the machine is ending. */
	error = sessiond_power_run(program, what, name);
	if (error != 0) {
		(void)write(control, "ERROR\n", 6U);
		return;
	}

	/* Succeeded: OK. */
	(void)write(control, "OK\n", 3U);
}

/* Counts the users logged in (utmpx) other than one: an SSH login, another console's. */
static unsigned
power_others(
	const char *name)
{
	struct utmpx *entry;
	unsigned others;
	int differs;

	/* Every user process in utmpx. */
	others = 0U;
	setutxent();
	for (;;) {
		/* The next entry; none left ends the count. */
		entry = getutxent();
		if (entry == NULL)
			break;

		/* Only a logged-in user's, and not the session's user's. */
		if (entry->ut_type != USER_PROCESS)
			continue;
		differs = strncmp(entry->ut_user, name, sizeof(entry->ut_user));
		if (differs != 0)
			others++;
	}
	endutxent();

	/* Succeeded: the count. */
	return others;
}
