/*
 * ws084: reads an input device and prints, for every event, the kernel's
 * stamp and the moments it was read (CLOCK_MONOTONIC and CLOCK_REALTIME),
 * so a delay before the event reaches user space can be told from a delay
 * after it.
 *
 *   evlat DEVICE SECONDS      e.g. evlat /dev/input/event1 10
 *
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <uapi/input.h>

static int64_t
now_ms(
	clockid_t clock)
{
	struct timespec ts;

	/* Reads the clock in milliseconds. */
	clock_gettime(clock, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int
main(
	int argc,
	char **argv)
{
	struct input_event events[64];
	struct pollfd pfd;
	int64_t start;
	int64_t stamp;
	ssize_t got;
	int seconds;
	int fd;
	int i;

	/* The device and the time to watch it. */
	if (argc != 3) {
		fprintf(stderr, "usage: evlat DEVICE SECONDS\n");
		return 2;
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	seconds = atoi(argv[2]);

	/* One line per event: its stamp, the monotonic and real read times, and the event. */
	printf("# stamp_ms mono_ms real_ms type code value\n");
	start = now_ms(CLOCK_MONOTONIC);
	while (now_ms(CLOCK_MONOTONIC) - start < (int64_t)seconds * 1000) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 100) <= 0)
			continue;
		got = read(fd, events, sizeof(events));
		if (got <= 0)
			break;
		for (i = 0; i < (int)(got / (ssize_t)sizeof(events[0])); i++) {
			stamp = (int64_t)events[i].time.tv_sec * 1000 + events[i].time.tv_usec / 1000;
			printf("%lld %lld %lld %u %u %d\n", (long long)stamp, (long long)now_ms(CLOCK_MONOTONIC),
			    (long long)now_ms(CLOCK_REALTIME), events[i].type, events[i].code, events[i].value);
		}
		fflush(stdout);
	}
	close(fd);
	return 0;
}
