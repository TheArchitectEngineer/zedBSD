/*
 * WS032: exercises the TCP idle probe over the loopback interface.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * Meant for an image built with a short keepalive idle time, so the probe
 * happens while the test is watching:
 *
 *   ZEDBSD_TEST_CPPFLAGS='-DCONFIG_TCP_KEEPALIVE_IDLE_MS=2000 \
 *       -DCONFIG_TCP_KEEPALIVE_INTERVAL_MS=1000 -DCONFIG_TCP_KEEPALIVE_COUNT=3'
 *
 * With both ends local, the peer answers each probe, so the connection has to
 * survive an idle period several times longer than the probe schedule and
 * still carry data afterwards.
 */
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void
verdict(int ok, const char *detail)
{
	printf("KEEPALIVE verdict: %s (%s)\n", ok ? "PASS" : "FAIL", detail);
}

int
main(void)
{
	struct sockaddr_in address;
	socklen_t length;
	int listener;
	int client;
	int server;
	int value;
	char detail[80];
	char byte;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	client = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0 || client < 0) {
		snprintf(detail, sizeof(detail), "socket errno %d", errno);
		verdict(0, detail);
		return 1;
	}

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
	    listen(listener, 1) != 0) {
		snprintf(detail, sizeof(detail), "listen errno %d", errno);
		verdict(0, detail);
		return 1;
	}

	length = sizeof(address);
	if (getsockname(listener, (struct sockaddr *)&address, &length) != 0) {
		snprintf(detail, sizeof(detail), "getsockname errno %d", errno);
		verdict(0, detail);
		return 1;
	}

	if (connect(client, (struct sockaddr *)&address, sizeof(address)) != 0) {
		snprintf(detail, sizeof(detail), "connect errno %d", errno);
		verdict(0, detail);
		return 1;
	}
	server = accept(listener, NULL, NULL);
	if (server < 0) {
		snprintf(detail, sizeof(detail), "accept errno %d", errno);
		verdict(0, detail);
		return 1;
	}

	/* Both ends probe, so both directions of the idle path are used. */
	value = 1;
	if (setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &value,
		       sizeof(value)) != 0 ||
	    setsockopt(server, SOL_SOCKET, SO_KEEPALIVE, &value,
		       sizeof(value)) != 0) {
		snprintf(detail, sizeof(detail), "keepalive errno %d", errno);
		verdict(0, detail);
		return 1;
	}

	printf("KEEPALIVE connected, idling\n");
	fflush(stdout);
	sleep(12);

	/* The connection has to have survived the probes it just exchanged. */
	byte = 'k';
	if (write(client, &byte, 1) != 1) {
		snprintf(detail, sizeof(detail), "write after idle errno %d",
		    errno);
		verdict(0, detail);
		return 1;
	}
	byte = 0;
	if (read(server, &byte, 1) != 1 || byte != 'k') {
		snprintf(detail, sizeof(detail), "read after idle errno %d",
		    errno);
		verdict(0, detail);
		return 1;
	}

	(void)close(client);
	(void)close(server);
	(void)close(listener);
	verdict(1, "connection carried data after an idle period");
	return 0;
}
