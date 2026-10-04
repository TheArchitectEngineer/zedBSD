/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * sleepctl: the test of the devices' suspend and resume (ws052-p004).
 *
 *   sleepctl devices
 *                 asks /dev/system for KERN_SYSTEM_SLEEP in its devices
 *                 mode (every device suspended and resumed at once) and
 *                 prints
 *                   sleep result=R resume=S device=NAME
 *                 with R the suspend's error (0: every device suspended),
 *                 S the resume's, and NAME the device that refused ("-"
 *                 when none did); the exit status is 0 only when R and S
 *                 are 0
 *   sleepctl -x
 *                 checks the refusals: an unknown mode and a reserved word
 *                 that is not zero (EINVAL); prints "refusals ok"
 *
 * Every way out says why on standard error.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/system.h>

/* The node of the system device. */
#define SYSTEM_NODE "/dev/system"

static int sleep_devices(int fd);
static int check_refusals(int fd);
static int refused(int fd, const struct system_sleep_request *request, const char *what);

/*
 * Runs the command the arguments name.
 */
int
main(
	int argc,
	char **argv)
{
	int compared;
	int status;
	int fd;

	/* Refuses a command line without exactly one word. */
	if (argc != 2) {
		fprintf(stderr, "usage: sleepctl devices | -x\n");
		return 2;
	}

	/* Opens the system device. */
	fd = open(SYSTEM_NODE, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "sleepctl: %s: %s\n", SYSTEM_NODE, strerror(errno));
		return 1;
	}

	/* Runs the command. */
	compared = strcmp(argv[1], "devices");
	if (compared == 0) {
		status = sleep_devices(fd);
	} else {
		/* Only -x is left. */
		compared = strcmp(argv[1], "-x");
		status = 2;
		if (compared == 0)
			status = check_refusals(fd);
		else
			fprintf(stderr, "usage: sleepctl devices | -x\n");
	}

	/* Closes the device and reports the outcome. */
	close(fd);
	return status;
}

/* Suspends and resumes the devices, and prints the outcome. */
static int
sleep_devices(
	int fd)
{
	struct system_sleep_request request;
	const char *device;
	int error;

	/* Asks for the devices mode. */
	memset(&request, 0, sizeof(request));
	request.mode = KERN_SYSTEM_SLEEP_DEVICES;
	error = ioctl(fd, KERN_SYSTEM_SLEEP, &request);
	if (error != 0) {
		fprintf(stderr, "sleepctl: KERN_SYSTEM_SLEEP: %s\n", strerror(errno));
		printf("sleep refused errno=%d\n", errno);
		return 1;
	}

	/* Prints the outcome. */
	request.device[sizeof(request.device) - 1U] = '\0';
	device = request.device;
	if (device[0] == '\0')
		device = "-";

	/* Writes the line the tests read. */
	printf("sleep result=%d resume=%d device=%s\n", (int)request.result, (int)request.resume_result, device);

	/* A suspend that did not go through, or a resume that failed. */
	if (request.result != 0 || request.resume_result != 0)
		return 1;

	/* Succeeded: every device was suspended and resumed. */
	return 0;
}

/* Checks the refusals of KERN_SYSTEM_SLEEP. */
static int
check_refusals(
	int fd)
{
	struct system_sleep_request request;
	int error;

	/* An unknown mode. */
	memset(&request, 0, sizeof(request));
	request.mode = 99U;
	error = refused(fd, &request, "an unknown mode");
	if (error != 0)
		return 1;

	/* A reserved word that is not zero. */
	memset(&request, 0, sizeof(request));
	request.mode = KERN_SYSTEM_SLEEP_DEVICES;
	request.reserved = 1U;
	error = refused(fd, &request, "a reserved word");
	if (error != 0)
		return 1;

	/* Succeeded: every refusal held. */
	printf("refusals ok\n");
	return 0;
}

/* Reports zero when the request is refused with EINVAL. */
static int
refused(
	int fd,
	const struct system_sleep_request *request,
	const char *what)
{
	struct system_sleep_request copy;
	int error;

	/* Asks. */
	copy = *request;
	error = ioctl(fd, KERN_SYSTEM_SLEEP, &copy);
	if (error == 0 || errno != EINVAL) {
		fprintf(stderr, "sleepctl: %s was not refused with EINVAL\n", what);
		return 1;
	}

	/* Succeeded: the request was refused. */
	return 0;
}
