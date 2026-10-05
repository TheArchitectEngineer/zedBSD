/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p016: the host test of libkeiland's one copy of a program
 * (kl_instance_*), against the Linux Keiland's libkeiland.so: the first
 * start listens, a second start hands its request and its token over and
 * ends, the first takes them; a socket left by a copy that ended is taken
 * over; a bad name or request, and a runtime directory others may enter,
 * are refused.  No compositor runs (WAYLAND_DISPLAY names none), so a
 * start without XDG_ACTIVATION_TOKEN hands an empty token over.
 *
 *   plan/ws089/tests/run-host-instance.sh
 */

#include <keiland.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static int hand_over(const char *request, const char *token);
static int take_one(struct kl_instance *instance, char *request, char *token);

/* Runs the cases; exit 0 when all pass. */
int
main(
	int argc,
	char **argv)
{
	char request[KL_INSTANCE_REQUEST_MAX];
	char token[KL_ACTIVATION_TOKEN_MAX];
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	struct sockaddr_un address;
	struct kl_instance *instance;
	struct kl_instance *other;
	const char *directory;
	int descriptor;
	int status;
	int error;

	/* The private runtime directory the script made. */
	(void)argc;
	(void)argv;
	directory = getenv("XDG_RUNTIME_DIR");
	check(directory != NULL, "XDG_RUNTIME_DIR is set");
	if (directory == NULL)
		return 1;
	setenv("WAYLAND_DISPLAY", "no-compositor-here", 1);

	/* Refusals: names, requests. */
	error = kl_instance_open("Bad Name", "", &instance);
	check(error == EINVAL && instance == NULL, "a bad name is refused");
	error = kl_instance_open("settings-test", "two\nlines", &instance);
	check(error == EINVAL && instance == NULL, "a request of two lines is refused");

	/* The first start listens. */
	error = kl_instance_open("settings-test", "", &instance);
	check(error == 0 && instance != NULL, "the first start becomes the one copy");
	check(kl_instance_fd(instance) >= 0, "the copy has a descriptor");
	check(take_one(instance, request, token) == 0, "nothing waits at first");

	/* A second start with a token hands its request over and ends. */
	status = hand_over("sharing", "abc123-token");
	check(status == 0, "the second start hands over and ends");
	check(take_one(instance, request, token) == 1, "the copy takes the request");
	check(strcmp(request, "sharing") == 0, "the request is the page");
	check(strcmp(token, "abc123-token") == 0, "the token came with it");

	/* A start without a page and without a token (no compositor to ask). */
	status = hand_over("", NULL);
	check(status == 0, "a start without a page hands over");
	check(take_one(instance, request, token) == 1, "the copy takes it");
	check(request[0] == '\0' && token[0] == '\0', "an empty request and no token");

	/* A token with a space is not a token: handed over as none. */
	status = hand_over("about", "bad token");
	check(status == 0, "a start with a bad token hands over");
	check(take_one(instance, request, token) == 1 && token[0] == '\0', "the bad token is dropped");

	/* The copy stops: its socket file goes. */
	snprintf(path, sizeof(path), "%s/keiland-settings-test.instance", directory);
	kl_instance_close(instance);
	check(access(path, F_OK) != 0, "the socket file goes with the copy");

	/* A socket left by a copy that ended (bound, never listened, closed) is taken over. */
	descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
	(void)bind(descriptor, (struct sockaddr *)&address, sizeof(address));
	(void)close(descriptor);
	check(access(path, F_OK) == 0, "a stale socket is there");
	error = kl_instance_open("settings-test", "", &instance);
	check(error == 0 && instance != NULL, "a start over a stale socket becomes the one copy");

	/* A second copy cannot be made while one runs: it hands over instead. */
	unsetenv("XDG_ACTIVATION_TOKEN");
	error = kl_instance_open("settings-test", "network", &other);
	check(error == 0 && other == NULL, "a start in the same process hands over too");
	check(take_one(instance, request, token) == 1 && strcmp(request, "network") == 0, "and the copy takes it");
	kl_instance_close(instance);

	/* A runtime directory others may enter: no one copy. */
	(void)chmod(directory, 0755);
	error = kl_instance_open("settings-test", "", &instance);
	check(error == ENOTSUP && instance == NULL, "a runtime directory open to others is refused");
	(void)chmod(directory, 0700);

	/* The outcome. */
	if (failures != 0) {
		printf("HOST-INSTANCE FAIL failures=%d\n", failures);
		return 1;
	}
	printf("HOST-INSTANCE PASS\n");
	return 0;
}

/* Reports one case. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed case is counted. */
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
		return;
	}
	printf("ok %s\n", what);
}

/* Runs a second start in a child process; 0 when it handed its request over. */
static int
hand_over(
	const char *request,
	const char *token)
{
	struct kl_instance *instance;
	pid_t child;
	int status;
	int error;

	/* The child: one start with the token in its environment. */
	child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		if (token != NULL) {
			setenv("XDG_ACTIVATION_TOKEN", token, 1);
		} else {
			unsetenv("XDG_ACTIVATION_TOKEN");
		}
		error = kl_instance_open("settings-test", request, &instance);
		if (error != 0 || instance != NULL)
			_exit(1);
		if (getenv("XDG_ACTIVATION_TOKEN") != NULL)
			_exit(2);
		_exit(0);
	}

	/* Its outcome. */
	(void)waitpid(child, &status, 0);
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

/* Waits briefly for a later start, then takes one request: 1 when one came. */
static int
take_one(
	struct kl_instance *instance,
	char *request,
	char *token)
{
	struct pollfd descriptor;

	/* The descriptor readable, or a short wait. */
	descriptor.fd = kl_instance_fd(instance);
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	(void)poll(&descriptor, 1, 200);

	/* The request. */
	return kl_instance_take(instance, request, KL_INSTANCE_REQUEST_MAX, token, KL_ACTIVATION_TOKEN_MAX);
}
