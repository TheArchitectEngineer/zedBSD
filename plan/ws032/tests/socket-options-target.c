/*
 * WS032: checks the socket options the kernel gained, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * Prints one line per check and a final verdict, so the console harness can
 * read the result without interpreting anything else on the screen.
 */
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int checks;
static int failures;

static void
report(const char *name, int ok, const char *detail)
{
	checks++;
	if (!ok)
		failures++;
	printf("SOCKOPT %s %s%s%s\n", ok ? "ok  " : "FAIL", name,
	    detail != NULL ? ": " : "", detail != NULL ? detail : "");
}

static void
check_flag(int fd, int level, int option, const char *name)
{
	int value;
	socklen_t length;
	char detail[64];

	/* The option reads as off before anything sets it. */
	value = -1;
	length = sizeof(value);
	if (getsockopt(fd, level, option, &value, &length) != 0) {
		snprintf(detail, sizeof(detail), "initial get errno %d", errno);
		report(name, 0, detail);
		return;
	}
	if (value != 0) {
		snprintf(detail, sizeof(detail), "initial value %d", value);
		report(name, 0, detail);
		return;
	}

	/* Turning it on is accepted and reported back. */
	value = 1;
	if (setsockopt(fd, level, option, &value, sizeof(value)) != 0) {
		snprintf(detail, sizeof(detail), "set errno %d", errno);
		report(name, 0, detail);
		return;
	}
	value = -1;
	length = sizeof(value);
	if (getsockopt(fd, level, option, &value, &length) != 0) {
		snprintf(detail, sizeof(detail), "get errno %d", errno);
		report(name, 0, detail);
		return;
	}
	if (value == 0 || length != sizeof(value)) {
		snprintf(detail, sizeof(detail), "read back %d len %u",
		    value, (unsigned)length);
		report(name, 0, detail);
		return;
	}

	/* Turning it off again is also carried. */
	value = 0;
	if (setsockopt(fd, level, option, &value, sizeof(value)) != 0) {
		snprintf(detail, sizeof(detail), "clear errno %d", errno);
		report(name, 0, detail);
		return;
	}
	value = -1;
	length = sizeof(value);
	if (getsockopt(fd, level, option, &value, &length) != 0 || value != 0) {
		snprintf(detail, sizeof(detail), "cleared value %d", value);
		report(name, 0, detail);
		return;
	}
	report(name, 1, NULL);
}

int
main(void)
{
	int fd;
	int value;
	char detail[64];

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		printf("SOCKOPT FAIL socket: errno %d\n", errno);
		printf("SOCKOPT verdict: FAIL (0/1)\n");
		return 1;
	}

	check_flag(fd, IPPROTO_TCP, TCP_NODELAY, "tcp-nodelay");
	check_flag(fd, SOL_SOCKET, SO_KEEPALIVE, "so-keepalive");

	/* An option this level does not have is still refused. */
	value = 1;
	errno = 0;
	if (setsockopt(fd, IPPROTO_TCP, 0x7fff, &value, sizeof(value)) == 0) {
		report("unknown-option-refused", 0, "accepted");
	} else {
		snprintf(detail, sizeof(detail), "errno %d", errno);
		report("unknown-option-refused",
		    errno == ENOPROTOOPT || errno == EOPNOTSUPP, detail);
	}

	(void)close(fd);
	printf("SOCKOPT verdict: %s (%d/%d)\n",
	    failures == 0 ? "PASS" : "FAIL", checks - failures, checks);
	return failures == 0 ? 0 : 1;
}
