/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p025: the host test of the zedBSD backend's Remote Login
 * (userland/desktop/libkeiland-backend-zedbsd/sharing-zedbsd.c, built with
 * SHARING_KEY_DIR naming the run's folder of host keys):
 *   host-sharing EXPECTED_FINGERPRINT
 *   - sessiond's state line is read (available, enabled, running, port);
 *     DENIED is EPERM, anything else EIO;
 *   - the host key's fingerprint is ssh-keygen -l's (EXPECTED_FINGERPRINT);
 *   - a request is the SERVICE line sessiond reads, and needs a session.
 * Prints one line a check and "host-sharing: PASS" or FAIL.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int managed;
static char sent[64];
static unsigned sent_request;

static void check(const char *what, int passed);

/* The session's stand-ins: managed or not, and the line sent. */
int
kl_backend_session_managed(
	const struct kl_backend *backend)
{
	(void)backend;
	return managed;
}

int
kl_backend_session_send(
	struct kl_backend *backend,
	unsigned request,
	const char *line)
{
	(void)backend;
	sent_request = request;
	(void)snprintf(sent, sizeof(sent), "%s", line);
	return 0;
}

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* A pass. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	static struct kl_backend backend;
	struct kl_backend_sharing state;
	int error;

	/* The argument. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-sharing EXPECTED_FINGERPRINT\n");
		return 2;
	}

	/* sessiond's answers. */
	error = kl_backend_sharing_take(&backend, "SERVICE available=1 enabled=1 running=0 port=2222");
	check("the state line", error == 0 && backend.sharing.available == 1U && backend.sharing.enabled == 1U && backend.sharing.running == 0U &&
	    backend.sharing.port == 2222U && backend.sharing.known == 1U);
	check("DENIED is EPERM", kl_backend_sharing_take(&backend, "DENIED") == EPERM);
	check("ERROR is EIO", kl_backend_sharing_take(&backend, "ERROR") == EIO);

	/* The fingerprint, as ssh-keygen -l says it. */
	kl_backend_sharing_get(&backend, &state);
	printf("fingerprint %s, expected %s\n", state.fingerprint, argv[1]);
	check("the fingerprint", strcmp(state.fingerprint, argv[1]) == 0);
	check("the state kept", state.port == 2222U && state.enabled == 1U);

	/* Requests: only in a session, as SERVICE lines. */
	managed = 0;
	check("no session, no request", kl_backend_sharing_request(&backend, KL_BACKEND_SHARING_ON) == ENOTSUP);
	managed = 1;
	error = kl_backend_sharing_request(&backend, KL_BACKEND_SHARING_ON);
	check("on", error == 0 && sent_request == KL_BACKEND_SESSION_SERVICE && strcmp(sent, "SERVICE sshd on\n") == 0);
	error = kl_backend_sharing_request(&backend, KL_BACKEND_SHARING_OFF);
	check("off", error == 0 && strcmp(sent, "SERVICE sshd off\n") == 0);
	error = kl_backend_sharing_request(&backend, KL_BACKEND_SHARING_STATUS);
	check("status", error == 0 && strcmp(sent, "SERVICE sshd status\n") == 0);
	check("an unknown action", kl_backend_sharing_request(&backend, 9U) == EINVAL);

	/* The verdict. */
	if (failures != 0) {
		printf("host-sharing: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-sharing: PASS\n");
	return 0;
}
