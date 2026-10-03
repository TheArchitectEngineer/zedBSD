/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-107 probe: the system holds many more sockets than 32.
 *
 *   socket-many [PAIRS] [TCP]
 *
 * Opens PAIRS unix socketpairs (default 200) and TCP sockets connected to
 * a listener on 127.0.0.1 (default 60, each side a socket), checks a byte
 * crosses every pair and every connection, and, as root, opens raw ICMP
 * sockets until one is refused, which must be the 33rd (the broadcast
 * family limit) with ENFILE.  Prints SOCKET-MANY:PASS.
 */

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PAIRS_MAX 400
#define TCP_MAX 100
#define RAW_TRY 40

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	static int pairs[PAIRS_MAX][2];
	static int clients[TCP_MAX], servers[TCP_MAX], raws[RAW_TRY];
	int pair_count, tcp_count, i, listener, failures, raw_count, raw_error;
	struct sockaddr_in address;
	socklen_t length;
	char byte;

	pair_count = argc > 1 ? atoi(argv[1]) : 200;
	tcp_count = argc > 2 ? atoi(argv[2]) : 60;
	if (pair_count > PAIRS_MAX)
		pair_count = PAIRS_MAX;
	if (tcp_count > TCP_MAX)
		tcp_count = TCP_MAX;
	failures = 0;

	/* Opens the unix socketpairs. */
	for (i = 0; i < pair_count; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pairs[i]) != 0) {
			printf("SOCKET-MANY:FAIL socketpair %d: %s\n", i, strerror(errno));
			return 1;
		}
	}

	/* Opens the TCP listener and the connections. */
	listener = socket(AF_INET, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(0x7f000001);
	address.sin_port = 0;
	if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
	    listen(listener, 16) != 0) {
		printf("SOCKET-MANY:FAIL listener: %s\n", strerror(errno));
		return 1;
	}
	length = sizeof(address);
	(void)getsockname(listener, (struct sockaddr *)&address, &length);
	for (i = 0; i < tcp_count; i++) {
		clients[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (clients[i] < 0 || connect(clients[i], (struct sockaddr *)&address, sizeof(address)) != 0) {
			printf("SOCKET-MANY:FAIL tcp %d: %s\n", i, strerror(errno));
			return 1;
		}
		servers[i] = accept(listener, NULL, NULL);
		if (servers[i] < 0) {
			printf("SOCKET-MANY:FAIL accept %d: %s\n", i, strerror(errno));
			return 1;
		}
	}

	/* Sends one byte across every pair and every connection. */
	for (i = 0; i < pair_count; i++) {
		byte = (char)i;
		if (write(pairs[i][0], &byte, 1) != 1 || read(pairs[i][1], &byte, 1) != 1 || byte != (char)i)
			failures++;
	}
	for (i = 0; i < tcp_count; i++) {
		byte = (char)i;
		if (write(clients[i], &byte, 1) != 1 || read(servers[i], &byte, 1) != 1 || byte != (char)i)
			failures++;
	}
	printf("sockets: %d unix pairs, %d tcp connections (+1 listener), %d byte failures\n",
	       pair_count, tcp_count, failures);

	/* As root, opens raw ICMP sockets until one is refused. */
	raw_count = 0;
	raw_error = 0;
	if (geteuid() == 0) {
		for (i = 0; i < RAW_TRY; i++) {
			raws[i] = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
			if (raws[i] < 0) {
				raw_error = errno;
				break;
			}
			raw_count++;
		}
		printf("raw icmp: %d opened, then %s\n", raw_count, raw_error ? strerror(raw_error) : "none refused");
		if (raw_count != 32 || raw_error != ENFILE)
			failures++;
		for (i = 0; i < raw_count; i++)
			close(raws[i]);

		/* After closing them, one opens again. */
		raws[0] = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
		if (raws[0] < 0)
			failures++;
		else
			close(raws[0]);
	}

	printf("%s\n", failures == 0 ? "SOCKET-MANY:PASS" : "SOCKET-MANY:FAIL");
	return failures == 0 ? 0 : 1;
}
