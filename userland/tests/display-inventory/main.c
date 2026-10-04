/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU display inventory probe (ws113-p002): prints what a GPU node's
 * display operations report -- the topology sequence and every output of
 * the inventory -- and, with --watch, waits for topology changes and prints
 * the inventory again after each, acknowledging what it printed.  It opens
 * the node on its own (its acknowledgements are its own), claims nothing
 * and presents nothing.  The lines the tests read:
 *
 *   DISPLAY-INVENTORY sequence=S count=N
 *   DISPLAY-INVENTORY output index=I id=ID generation=G connected=0|1 active=0|1 mode=WxH@MHZ size=WxHmm name=KEY
 *   DISPLAY-INVENTORY done
 *
 *   display-inventory [--node=/dev/gpu0] [--watch=SECONDS]
 */

#include <uapi/gpu.h>
#include <uapi/gpu-display.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* The node opened unless one is named. */
#define INVENTORY_NODE		"/dev/gpu0"

static int inventory_print(int fd, uint64_t *sequence);
static int inventory_ack(int fd, uint64_t sequence);
static int inventory_watch(int fd, unsigned seconds, uint64_t sequence);
static uint64_t inventory_now_ms(void);

int
main(
	int argc,
	char **argv)
{
	const char *node;
	uint64_t sequence;
	unsigned watch;
	int index;
	int same;
	int fd;
	int status;

	/* The command line. */
	node = INVENTORY_NODE;
	watch = 0U;
	for (index = 1; index < argc; index++) {
		same = strncmp(argv[index], "--node=", 7U);
		if (same == 0) {
			node = argv[index] + 7;
			continue;
		}

		/* How long to watch. */
		same = strncmp(argv[index], "--watch=", 8U);
		if (same == 0) {
			watch = (unsigned)strtoul(argv[index] + 8, NULL, 10);
			continue;
		}

		/* Anything else. */
		fprintf(stderr, "usage: display-inventory [--node=/dev/gpu0] [--watch=SECONDS]\n");
		return 2;
	}

	/* The node, an open of its own. */
	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("DISPLAY-INVENTORY FAILED open errno=%d\n", errno);
		return 1;
	}

	/* The inventory now, acknowledged. */
	status = inventory_print(fd, &sequence);
	if (status == 0)
		status = inventory_ack(fd, sequence);

	/* The changes for a while, when asked. */
	if (status == 0 && watch != 0U)
		status = inventory_watch(fd, watch, sequence);

	/* The end. */
	close(fd);
	if (status != 0)
		return 1;
	return 0;
}

/* Prints the topology sequence (read before) and every output; 0, or -1 with the failure printed. */
static int
inventory_print(
	int fd,
	uint64_t *sequence)
{
	struct gpu_display_events events;
	struct gpu_display_info info;
	uint32_t count;
	uint32_t index;
	int status;

	/* The sequence the inventory goes with. */
	memset(&events, 0, sizeof(events));
	events.version = GPU_ABI_VERSION;
	events.size = sizeof(events);
	status = ioctl(fd, GPU_DISPLAY_EVENTS, &events);
	if (status != 0) {
		printf("DISPLAY-INVENTORY FAILED events errno=%d\n", errno);
		return -1;
	}

	/* That sequence. */
	*sequence = events.sequence;

	/* How many outputs. */
	memset(&info, 0, sizeof(info));
	info.version = GPU_ABI_VERSION;
	info.size = sizeof(info);
	info.index = GPU_DISPLAY_COUNT_ONLY;
	status = ioctl(fd, GPU_DISPLAY_QUERY, &info);
	if (status != 0) {
		printf("DISPLAY-INVENTORY FAILED count errno=%d\n", errno);
		return -1;
	}

	/* The count, printed with the sequence. */
	count = info.count;
	printf("DISPLAY-INVENTORY sequence=%llu count=%u\n", (unsigned long long)*sequence, count);

	/* Each output. */
	for (index = 0U; index < count; index++) {
		memset(&info, 0, sizeof(info));
		info.version = GPU_ABI_VERSION;
		info.size = sizeof(info);
		info.index = index;
		status = ioctl(fd, GPU_DISPLAY_QUERY, &info);
		if (status != 0) {
			printf("DISPLAY-INVENTORY output index=%u FAILED errno=%d\n", index, errno);
			continue;
		}

		/* Its line. */
		info.name[sizeof(info.name) - 1U] = '\0';
		printf("DISPLAY-INVENTORY output index=%u id=%u generation=%llu connected=%d active=%d mode=%ux%u@%u size=%ux%umm name=%s\n",
		    index,
		    info.display_id,
		    (unsigned long long)info.generation,
		    (info.flags & GPU_DISPLAY_CONNECTED) != 0U,
		    (info.flags & GPU_DISPLAY_ACTIVE) != 0U,
		    info.preferred_width,
		    info.preferred_height,
		    info.refresh_millihz,
		    info.physical_width_mm,
		    info.physical_height_mm,
		    info.name);
	}

	/* The inventory is printed. */
	printf("DISPLAY-INVENTORY done\n");
	fflush(stdout);
	return 0;
}

/* Acknowledges the snapshot of a sequence; 0, or -1 with the failure printed. */
static int
inventory_ack(
	int fd,
	uint64_t sequence)
{
	struct gpu_display_events events;
	int status;

	/* The acknowledgement of that snapshot. */
	memset(&events, 0, sizeof(events));
	events.version = GPU_ABI_VERSION;
	events.size = sizeof(events);
	events.flags = GPU_DISPLAY_EVENT_ACK;
	events.ack_sequence = sequence;
	status = ioctl(fd, GPU_DISPLAY_EVENTS, &events);
	if (status != 0) {
		printf("DISPLAY-INVENTORY FAILED ack errno=%d\n", errno);
		return -1;
	}

	/* Succeeded. */
	return 0;
}

/* Waits for topology changes for a number of seconds, printing the inventory after each. */
static int
inventory_watch(
	int fd,
	unsigned seconds,
	uint64_t sequence)
{
	struct pollfd descriptor;
	uint64_t deadline;
	uint64_t now;
	int status;

	/* Until the deadline. */
	deadline = inventory_now_ms() + (uint64_t)seconds * 1000U;
	for (;;) {
		/* The time left. */
		now = inventory_now_ms();
		if (now >= deadline)
			break;

		/* A topology change makes the node ready with POLLPRI. */
		descriptor.fd = fd;
		descriptor.events = POLLPRI;
		descriptor.revents = 0;
		status = poll(&descriptor, 1, (int)(deadline - now));
		if (status < 0 && errno != EINTR) {
			printf("DISPLAY-INVENTORY FAILED poll errno=%d\n", errno);
			return -1;
		}

		/* Nothing changed yet. */
		if (status <= 0 || (descriptor.revents & POLLPRI) == 0)
			continue;

		/* The new inventory, acknowledged. */
		status = inventory_print(fd, &sequence);
		if (status == 0)
			status = inventory_ack(fd, sequence);
		if (status != 0)
			return -1;
	}

	/* The watch ended. */
	printf("DISPLAY-INVENTORY watch-end sequence=%llu\n", (unsigned long long)sequence);
	return 0;
}

/* The monotonic clock in milliseconds. */
static uint64_t
inventory_now_ms(void)
{
	struct timespec now;

	/* The clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}
