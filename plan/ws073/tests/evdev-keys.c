/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-070 probe: prints the key events every input device delivers.
 *
 *   evdev-keys SECONDS
 *
 * Opens every /dev/input/eventN, prints each one's name, then for SECONDS
 * prints "eventN code value" for every key event (value 1 press, 0
 * release).  The host sends the keys, for example with QMP
 * input-send-event (plan/ws073/tests/keys-host.py).
 */

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <uapi/input.h>

#define DEVICE_MAX 16

/*
 * Runs the probe.
 */
int
main(
	int argc,
	char **argv)
{
	struct pollfd pollers[DEVICE_MAX];
	int numbers[DEVICE_MAX];
	struct input_event event;
	char path[64];
	char name[128];
	time_t end;
	int count, index, fd, ready;
	ssize_t got;

	/* Opens every event device there is. */
	count = 0;
	for (index = 0; index < DEVICE_MAX; index++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", index);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
		printf("event%d: %s\n", index, name);
		pollers[count].fd = fd;
		pollers[count].events = POLLIN;
		numbers[count] = index;
		count++;
	}
	fflush(stdout);

	/* Prints the key events until the time is up. */
	end = time(NULL) + (argc > 1 ? atoi(argv[1]) : 10);
	while (time(NULL) < end) {
		ready = poll(pollers, (nfds_t)count, 200);
		if (ready <= 0)
			continue;
		for (index = 0; index < count; index++) {
			if ((pollers[index].revents & POLLIN) == 0)
				continue;
			for (;;) {
				got = read(pollers[index].fd, &event, sizeof(event));
				if (got != (ssize_t)sizeof(event))
					break;
				if (event.type == EV_KEY) {
					printf("event%d %u %d\n", numbers[index],
					    event.code, event.value);
					fflush(stdout);
				}
			}
		}
	}

	/* Succeeded: the time is up. */
	return 0;
}
