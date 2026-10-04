/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws132-p004: host test of libkeiland-backend's volumes on zedBSD
 * (libkeiland-backend-zedbsd/volume-zedbsd.c) against a fake volumed on a
 * socket of the test's own (VOLUME_SOCKET_PATH): the greeting, a list and
 * its DONE, a change (one VOLUME, one GONE), the escaped label and path,
 * the mount and eject lines with their numbers, a RESULT with a program,
 * and the list emptied when volumed goes.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static unsigned failures;

static void check(int condition, const char *what);
static void say(int peer, const char *text);
static void hear(int peer, char *line, size_t size);
static unsigned update(struct kl_backend_volumes *volumes);

int
main(
	void)
{
	struct kl_backend_volumes *volumes;
	struct kl_backend_volume list[KL_BACKEND_VOLUMES_MAX];
	struct sockaddr_un address;
	char line[256];
	char user[64];
	uint32_t request;
	unsigned changed;
	size_t count;
	int listener;
	int peer;
	int error;

	/* The fake volumed. */
	(void)unlink(VOLUME_SOCKET_PATH);
	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s", VOLUME_SOCKET_PATH);
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0) {
		printf("FAIL: the fake volumed's socket\n");
		return 1;
	}

	/* The backend connects and says HELLO. */
	volumes = kl_backend_volumes_open();
	peer = accept(listener, NULL, NULL);
	hear(peer, line, sizeof(line));
	check(strcmp(line, "HELLO 1") == 0, "the backend says HELLO 1");

	/* A list of two, and its DONE. */
	say(peer, "VOLUME id=sda state=available fs=fat size=16777216 label=USB%20STICK path=- new=1\n"
	    "VOLUME id=sdb1 state=mounted fs=ufs size=1024 label= path=/media/a%3db new=0\nDONE\n");
	changed = update(volumes);
	count = kl_backend_volumes_get(volumes, list, KL_BACKEND_VOLUMES_MAX);
	check((changed & KL_BACKEND_VOLUMES_CHANGED_LIST) != 0U && count == 2U, "the list of two comes with its DONE");
	check(strcmp(list[0].id, "sda") == 0 && strcmp(list[0].fs, "fat") == 0 && strcmp(list[0].label, "USB STICK") == 0 &&
	    list[0].path[0] == '\0' && list[0].bytes == 16777216U && list[0].fresh == 1U, "the first volume, its label unescaped, no path");
	check(strcmp(list[1].path, "/media/a=b") == 0 && list[1].label[0] == '\0' && list[1].fresh == 0U, "the second, its path unescaped");

	/* Lines without their DONE change nothing yet. */
	say(peer, "GONE id=sdb1\n");
	changed = update(volumes);
	count = kl_backend_volumes_get(volumes, list, KL_BACKEND_VOLUMES_MAX);
	check(changed == 0U && count == 2U, "a GONE without its DONE changes nothing yet");
	say(peer, "VOLUME id=sda state=mounted fs=fat size=16777216 label=USB%20STICK path=/media/USB%20STICK new=0\nDONE\n");
	changed = update(volumes);
	count = kl_backend_volumes_get(volumes, list, KL_BACKEND_VOLUMES_MAX);
	check(changed == KL_BACKEND_VOLUMES_CHANGED_LIST && count == 1U && strcmp(list[0].path, "/media/USB STICK") == 0,
	    "the change: one gone, one mounted in its place");

	/* A mount and an eject, numbered. */
	error = kl_backend_volumes_mount(volumes, "sda", &request);
	hear(peer, line, sizeof(line));
	check(error == 0 && request == 1U && strcmp(line, "MOUNT 1 sda") == 0, "a mount is MOUNT 1 sda");
	error = kl_backend_volumes_eject(volumes, "sda", &request);
	hear(peer, line, sizeof(line));
	check(error == 0 && request == 2U && strcmp(line, "EJECT 2 sda") == 0, "an eject is EJECT 2 sda");
	error = kl_backend_volumes_eject(volumes, "bad id", &request);
	check(error == EINVAL, "an ID with a space is refused");

	/* The answers. */
	say(peer, "RESULT 1 0\nRESULT 2 16 user=sh\n");
	changed = update(volumes);
	check(changed == KL_BACKEND_VOLUMES_CHANGED_RESULT, "the answers are a result change");
	error = -1;
	check(kl_backend_volumes_take_result(volumes, &request, &error, user, sizeof(user)) == 1 && request == 1U && error == 0,
	    "the mount's answer: 0");
	check(kl_backend_volumes_take_result(volumes, &request, &error, user, sizeof(user)) == 1 && request == 2U &&
	    error == 16 && strcmp(user, "sh") == 0, "the eject's answer with its program");
	check(kl_backend_volumes_take_result(volumes, &request, &error, user, sizeof(user)) == 0, "no more answers");

	/* volumed goes: the list empties, and a request has no connection. */
	(void)close(peer);
	changed = update(volumes);
	count = kl_backend_volumes_get(volumes, list, KL_BACKEND_VOLUMES_MAX);
	check(changed == KL_BACKEND_VOLUMES_CHANGED_LIST && count == 0U, "without volumed the list is empty");
	error = kl_backend_volumes_mount(volumes, "sda", &request);
	check(error == ENOTCONN, "a mount without volumed is ENOTCONN");

	/* Done. */
	kl_backend_volumes_close(volumes);
	(void)close(listener);
	(void)unlink(VOLUME_SOCKET_PATH);
	if (failures != 0U) {
		printf("host-volumes: FAIL (%u)\n", failures);
		return 1;
	}
	printf("host-volumes: PASS\n");
	return 0;
}

/* Sends the fake volumed's lines. */
static void
say(
	int peer,
	const char *text)
{
	(void)write(peer, text, strlen(text));
}

/* Reads one line the backend sent. */
static void
hear(
	int peer,
	char *line,
	size_t size)
{
	size_t used;
	char character;

	used = 0U;
	while (read(peer, &character, 1U) == 1 && character != '\n') {
		if (used + 1U < size)
			line[used++] = character;
	}
	line[used] = '\0';
}

/* Lets the bytes arrive, then updates. */
static unsigned
update(
	struct kl_backend_volumes *volumes)
{
	unsigned changed;

	usleep(20000);
	changed = 0U;
	(void)kl_backend_volumes_update(volumes, &changed);
	return changed;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		printf("ok: %s\n", what);
	} else {
		printf("FAIL: %s\n", what);
		failures++;
	}
}
