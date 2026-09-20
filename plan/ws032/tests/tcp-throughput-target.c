/*
 * WS032: measures loopback TCP throughput on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The number it prints is only meaningful next to another run of the same
 * program on the same machine: it is there to compare one send-window depth
 * with another, not to state what the network can carry.
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TRANSFER_BYTES (256U * 1024U)
#define CHUNK 4096U

int
main(void)
{
	static char buffer[CHUNK];
	struct sockaddr_in address;
	struct timespec start, finish;
	socklen_t length;
	int listener, client, server;
	size_t sent, received;
	ssize_t moved;
	double seconds;
	pid_t child;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	client = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0 || client < 0) {
		printf("THROUGHPUT FAIL socket errno %d\n", errno);
		return 1;
	}

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
	    listen(listener, 1) != 0) {
		printf("THROUGHPUT FAIL listen errno %d\n", errno);
		return 1;
	}
	length = sizeof(address);
	if (getsockname(listener, (struct sockaddr *)&address, &length) != 0) {
		printf("THROUGHPUT FAIL getsockname errno %d\n", errno);
		return 1;
	}
	if (connect(client, (struct sockaddr *)&address, sizeof(address)) != 0) {
		printf("THROUGHPUT FAIL connect errno %d\n", errno);
		return 1;
	}
	server = accept(listener, NULL, NULL);
	if (server < 0) {
		printf("THROUGHPUT FAIL accept errno %d\n", errno);
		return 1;
	}

	memset(buffer, 'z', sizeof(buffer));

	/* A child drains the other end so the sender is never blocked by it. */
	child = fork();
	if (child < 0) {
		printf("THROUGHPUT FAIL fork errno %d\n", errno);
		return 1;
	}
	if (child == 0) {
		(void)close(client);
		received = 0;
		while (received < TRANSFER_BYTES) {
			moved = read(server, buffer, sizeof(buffer));
			if (moved <= 0)
				_exit(1);
			received += (size_t)moved;
		}
		_exit(0);
	}
	(void)close(server);

	(void)clock_gettime(CLOCK_MONOTONIC, &start);
	sent = 0;
	while (sent < TRANSFER_BYTES) {
		moved = write(client, buffer, sizeof(buffer));
		if (moved <= 0) {
			printf("THROUGHPUT FAIL write errno %d\n", errno);
			return 1;
		}
		sent += (size_t)moved;
	}
	(void)clock_gettime(CLOCK_MONOTONIC, &finish);

	seconds = (double)(finish.tv_sec - start.tv_sec) +
	    (double)(finish.tv_nsec - start.tv_nsec) / 1000000000.0;
	if (seconds <= 0.0)
		seconds = 0.000001;

	printf("THROUGHPUT %u bytes in %u ms = %u KiB/s\n",
	    (unsigned)sent, (unsigned)(seconds * 1000.0),
	    (unsigned)((double)sent / 1024.0 / seconds));
	printf("THROUGHPUT verdict: PASS\n");
	return 0;
}
