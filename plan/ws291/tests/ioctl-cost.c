/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cost of one system call and of one GPU ioctl on zedBSD: getppid(),
 * then GPU_GET_INFO (a call of the GPU core that only copies its answer
 * out, through the same entry and admission as the submission path's
 * calls) on the GPU node given (default /dev/gpu0), each timed over many
 * calls.  Prints one line each, the nanoseconds a
 * call.
 *
 *   ioctl-cost [NODE]
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <uapi/gpu.h>

/* The calls each line times. */
#define COST_CALLS	200000L

static long long cost_now(void);
static void cost_report(const char *name, long long start, long long end, long count);

/* Times the two calls. */
int
main(
	int argc,
	char **argv)
{
	struct gpu_info request;
	const char *node;
	long long start;
	long index;
	int descriptor;
	int status;

	/* The node. */
	node = "/dev/gpu0";
	if (argc > 1)
		node = argv[1];

	/* A system call that does nothing. */
	start = cost_now();
	for (index = 0; index < COST_CALLS; index++)
		(void)getppid();
	cost_report("syscall getppid", start, cost_now(), COST_CALLS);

	/* The GPU core's information call. */
	descriptor = open(node, O_RDWR);
	if (descriptor < 0) {
		perror(node);
		return 1;
	}

	start = cost_now();
	for (index = 0; index < COST_CALLS; index++) {
		memset(&request, 0, sizeof(request));
		request.version = GPU_ABI_VERSION;
		request.size = sizeof(request);
		status = ioctl(descriptor, GPU_GET_INFO, &request);
		if (status != 0) {
			perror("GPU_GET_INFO");
			return 1;
		}
	}
	cost_report("ioctl GPU_GET_INFO", start, cost_now(), COST_CALLS);

	/* Succeeded: both lines are printed. */
	(void)close(descriptor);
	return 0;
}

/* Reads the monotonic clock in nanoseconds. */
static long long
cost_now(
	void)
{
	struct timespec now;

	/* The clock's seconds and nanoseconds. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* Prints a line: the name and the nanoseconds a call. */
static void
cost_report(
	const char *name,
	long long start,
	long long end,
	long count)
{
	/* The average of the calls. */
	printf("%s: %lld ns\n", name, (end - start) / count);
}
