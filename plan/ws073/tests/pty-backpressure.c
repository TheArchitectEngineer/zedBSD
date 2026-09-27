/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pty back-pressure check of POSIX-R2.ELF, repeated: a forked child
 * writes 6000 bytes to a pseudo terminal's slave in one blocking write, and
 * the parent reads them from the master.  A blocking write must write all
 * of it.  An alarm ends a read that would wait forever.
 *
 *   pty-backpressure [ROUNDS [SECONDS]]
 *
 * Prints one line per failed round and a summary; exits with the number of
 * failed rounds.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define STRESS_SIZE 6000U

static char input[STRESS_SIZE];
static char output[STRESS_SIZE];
static unsigned read_limit = 10;

static void on_alarm(int signal_number) { (void)signal_number; }

/* Runs one round; reports 0 when every byte arrived and the child wrote all. */
static int
round_once(
	int round)
{
	char path[64];
	int master, slave, status;
	size_t received;
	ssize_t count, written;
	pid_t child;

	master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
	    ptsname_r(master, path, sizeof(path)) != 0) {
		perror("pty");
		return 1;
	}
	slave = open(path, O_RDWR | O_NOCTTY);
	if (slave < 0) {
		perror("slave");
		return 1;
	}

	child = fork();
	if (child == 0) {
		written = write(slave, input, sizeof(input));
		if (written == (ssize_t)sizeof(input))
			_exit(0);
		fprintf(stderr, "round %d: child write returned %zd errno %d\n",
		    round, written, written < 0 ? errno : 0);
		_exit(42);
	}

	received = 0;
	alarm(read_limit);
	while (received < sizeof(output)) {
		count = read(master, output + received, sizeof(output) - received);
		if (count <= 0) {
			fprintf(stderr, "round %d: read returned %zd errno %d after %zu bytes\n",
			    round, count, count < 0 ? errno : 0, received);
			break;
		}
		received += (size_t)count;
	}
	alarm(0);

	/* A child still blocked in its write is ended rather than waited on. */
	if (received != sizeof(output))
		kill(child, SIGKILL);
	waitpid(child, &status, 0);
	close(slave);
	close(master);
	if (received != sizeof(output) || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("FAIL round %d: received %zu, child status %d\n", round, received,
		    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return 1;
	}
	if (memcmp(input, output, sizeof(input)) != 0) {
		printf("FAIL round %d: data differs\n", round);
		return 1;
	}
	return 0;
}

/*
 * Runs the rounds.
 */
int
main(
	int argc,
	char **argv)
{
	struct sigaction action;
	int rounds, round, failures;

	rounds = 100;
	if (argc > 1)
		rounds = atoi(argv[1]);
	if (argc > 2)
		read_limit = (unsigned)atoi(argv[2]);
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_alarm;
	sigaction(SIGALRM, &action, NULL);
	memset(input, 'q', sizeof(input));
	setvbuf(stdout, NULL, _IONBF, 0);

	failures = 0;
	for (round = 1; round <= rounds; round++)
		failures += round_once(round);
	printf("pty-backpressure: %d of %d rounds failed\n", failures, rounds);
	return failures;
}
