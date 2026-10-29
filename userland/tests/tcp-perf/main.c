/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A TCP throughput probe: connects to a peer that sinks or sources a
 * stream on the host and moves bytes one
 * way for a number of seconds.
 *
 *   tcp-perf send|recv ADDRESS PORT SECONDS [CHUNK]
 *
 * The first byte on the connection tells the peer the direction: 'S' when
 * this program sends (the peer reads to the end), 'R' when it receives
 * (the peer writes until this program closes).  ADDRESS is an IPv4
 * address in dotted form; CHUNK is the bytes of one read or write (default
 * 131072).  Every second a line "t=SECONDS bytes=N mbps=RATE" gives the
 * second's rate; the last line is "TCP-PERF DIRECTION seconds=S bytes=N
 * mbps=RATE" over the whole transfer.  It exits 0, 1 when the transfer
 * failed, or 2 for a bad command line.  The source also builds on Linux.
 *
 * The time is kept by an interval timer that raises SIGALRM every second,
 * not by reading the clock around each read or write: on a system whose
 * clock is read through a system call that would add two calls to each
 * one the transfer makes.  A read or write the signal interrupts is
 * simply made again.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* The default bytes of one read or write. */
#define PERF_CHUNK_DEFAULT 131072UL

/* The largest chunk the command line may ask for. */
#define PERF_CHUNK_MAX 4194304UL

/* The longest transfer the command line may ask for, in seconds. */
#define PERF_SECONDS_MAX 3600UL

/*
 * The seconds the interval timer has counted since the transfer started.
 *
 * The SIGALRM handler adds one; the main thread reads it.
 */
static volatile sig_atomic_t perf_ticks;

/*
 * The rate of each second and of the whole transfer.
 *
 * Only the main thread touches it.
 */
struct perf_progress {
	double start;
	double second_start;
	unsigned long long total;
	unsigned long long second_bytes;
	unsigned second;
};

static int perf_parse(int argc, char **argv, int *sending, unsigned long *seconds, unsigned long *chunk);
static int perf_connect(const char *address_text, const char *port_text, int *descriptor);
static double perf_now(void);
static void perf_tick(int signal_number);
static int perf_start_timer(void);
static void perf_count(struct perf_progress *progress, size_t bytes);
static int perf_send(int descriptor, unsigned char *buffer, unsigned long chunk, unsigned long seconds, struct perf_progress *progress);
static int perf_receive(int descriptor, unsigned char *buffer, unsigned long chunk, unsigned long seconds, struct perf_progress *progress);

/*
 * Moves bytes the way the command line asks and prints the rates.
 */
int
main(
	int argc,
	char **argv)
{
	struct perf_progress progress;
	unsigned char *buffer;
	unsigned long seconds;
	unsigned long chunk;
	unsigned long index;
	double elapsed;
	char direction;
	const char *name;
	ssize_t sent;
	int descriptor;
	int sending;
	int error;

	/* The direction, the duration and the chunk. */
	error = perf_parse(argc, argv, &sending, &seconds, &chunk);
	if (error != 0)
		return 2;

	/* The buffer of one read or write, filled with a changing byte. */
	buffer = malloc(chunk);
	if (buffer == NULL) {
		fprintf(stderr, "tcp-perf: no memory for a chunk of %lu bytes\n", chunk);
		return 1;
	}

	/* The bytes of the chunk differ from one to the next. */
	for (index = 0; index < chunk; index++)
		buffer[index] = (unsigned char)((index * 131UL + 7UL) & 0xffUL);

	/* The connection to the peer. */
	error = perf_connect(argv[2], argv[3], &descriptor);
	if (error != 0) {
		free(buffer);
		return 1;
	}

	/* The direction byte, which the peer reads first. */
	direction = 'R';
	name = "recv";
	if (sending) {
		direction = 'S';
		name = "send";
	}

	/* Sends the byte. */
	sent = write(descriptor, &direction, 1);
	if (sent != 1) {
		fprintf(stderr, "tcp-perf: the direction byte was not sent\n");
		close(descriptor);
		free(buffer);
		return 1;
	}

	/* The transfer, timed from its first byte. */
	memset(&progress, 0, sizeof(progress));
	progress.start = perf_now();
	progress.second_start = progress.start;
	error = perf_start_timer();
	if (error != 0) {
		fprintf(stderr, "tcp-perf: no interval timer: %s\n", strerror(error));
		close(descriptor);
		free(buffer);
		return 1;
	}

	/* Moves the bytes in the direction asked. */
	if (sending)
		error = perf_send(descriptor, buffer, chunk, seconds, &progress);
	else
		error = perf_receive(descriptor, buffer, chunk, seconds, &progress);
	elapsed = perf_now() - progress.start;
	close(descriptor);
	free(buffer);
	if (error != 0) {
		fprintf(stderr, "tcp-perf: %s failed after %llu bytes: %s\n", name, progress.total, strerror(error));
		return 1;
	}

	/* Succeeded: the rate over the whole transfer. */
	if (elapsed <= 0.0)
		elapsed = 1e-9;
	printf("TCP-PERF %s seconds=%.3f bytes=%llu mbps=%.1f\n", name, elapsed, progress.total,
	    (double)progress.total * 8.0 / elapsed / 1e6);
	return 0;
}

/* Reads the direction, the duration and the chunk from the command line. */
static int
perf_parse(
	int argc,
	char **argv,
	int *sending,
	unsigned long *seconds,
	unsigned long *chunk)
{
	char *end;
	int compare;
	int compare_recv;

	/* Refuses a command line without the direction, the address, the port and the duration. */
	if (argc != 5 && argc != 6) {
		fprintf(stderr, "usage: tcp-perf send|recv ADDRESS PORT SECONDS [CHUNK]\n");
		return EINVAL;
	}

	/* The direction. */
	compare = strcmp(argv[1], "send");
	compare_recv = strcmp(argv[1], "recv");
	if (compare == 0) {
		*sending = 1;
	} else if (compare_recv == 0) {
		*sending = 0;
	} else {
		fprintf(stderr, "tcp-perf: the direction is send or recv, not %s\n", argv[1]);
		return EINVAL;
	}

	/* The duration in seconds. */
	errno = 0;
	*seconds = strtoul(argv[4], &end, 10);
	if (errno != 0 || *end != '\0' || *seconds == 0 || *seconds > PERF_SECONDS_MAX) {
		fprintf(stderr, "tcp-perf: bad duration %s\n", argv[4]);
		return EINVAL;
	}

	/* The chunk, the default when none is given. */
	*chunk = PERF_CHUNK_DEFAULT;
	if (argc == 6) {
		errno = 0;
		*chunk = strtoul(argv[5], &end, 0);
		if (errno != 0 || *end != '\0' || *chunk == 0 || *chunk > PERF_CHUNK_MAX) {
			fprintf(stderr, "tcp-perf: bad chunk %s\n", argv[5]);
			return EINVAL;
		}
	}

	/* Succeeded: every value is in range. */
	return 0;
}

/* Connects a blocking stream socket to the address and port given. */
static int
perf_connect(
	const char *address_text,
	const char *port_text,
	int *descriptor)
{
	struct sockaddr_in address;
	unsigned long port;
	char *end;
	int parsed;
	int error;

	/* The port, which must be a number of 16 bits. */
	errno = 0;
	port = strtoul(port_text, &end, 10);
	if (errno != 0 || *end != '\0' || port == 0 || port > 65535UL) {
		fprintf(stderr, "tcp-perf: bad port %s\n", port_text);
		return EINVAL;
	}

	/* The peer's address. */
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t)port);
	parsed = inet_pton(AF_INET, address_text, &address.sin_addr);
	if (parsed != 1) {
		fprintf(stderr, "tcp-perf: bad address %s\n", address_text);
		return EINVAL;
	}

	/* The socket. */
	*descriptor = socket(AF_INET, SOCK_STREAM, 0);
	if (*descriptor < 0) {
		error = errno;
		fprintf(stderr, "tcp-perf: socket: %s\n", strerror(error));
		return error;
	}

	/* The connection, waited for. */
	error = connect(*descriptor, (struct sockaddr *)&address, sizeof(address));
	if (error != 0) {
		error = errno;
		fprintf(stderr, "tcp-perf: connect %s:%s: %s\n", address_text, port_text, strerror(error));
		close(*descriptor);
		return error;
	}

	/* Succeeded: the socket is connected. */
	return 0;
}

/* Reads the monotonic clock in seconds. */
static double
perf_now(void)
{
	struct timespec now;

	/* The clock; a failure leaves the zeroed value, which only skews the rate. */
	memset(&now, 0, sizeof(now));
	(void)clock_gettime(CLOCK_MONOTONIC, &now);

	/* Succeeded: the time in seconds. */
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Counts one more second of the transfer. */
static void
perf_tick(
	int signal_number)
{
	(void)signal_number;
	perf_ticks++;
}

/*
 * Starts the interval timer that counts the transfer's seconds, without
 * SA_RESTART so that a blocked read or write returns to look at them.
 */
static int
perf_start_timer(void)
{
	struct sigaction action;
	struct itimerval interval;
	int error;

	/* The handler of SIGALRM. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = perf_tick;
	sigemptyset(&action.sa_mask);
	error = sigaction(SIGALRM, &action, NULL);
	if (error != 0)
		return errno;

	/* A signal every second from now. */
	memset(&interval, 0, sizeof(interval));
	interval.it_interval.tv_sec = 1;
	interval.it_value.tv_sec = 1;
	error = setitimer(ITIMER_REAL, &interval, NULL);
	if (error != 0)
		return errno;

	/* Succeeded: the seconds are counted. */
	return 0;
}

/*
 * Counts bytes moved and prints the rate of each second that has ended.
 */
static void
perf_count(
	struct perf_progress *progress,
	size_t bytes)
{
	double span;
	double now;

	/* The bytes count toward the total and the running second. */
	progress->total += (unsigned long long)bytes;
	progress->second_bytes += (unsigned long long)bytes;

	/* A second still running prints nothing. */
	if ((unsigned)perf_ticks <= progress->second)
		return;

	/* The second that ended, its rate over its own length. */
	now = perf_now();
	span = now - progress->second_start;
	if (span <= 0.0)
		span = 1e-9;
	progress->second++;
	printf("t=%u bytes=%llu mbps=%.1f\n", progress->second, progress->second_bytes,
	    (double)progress->second_bytes * 8.0 / span / 1e6);
	fflush(stdout);
	progress->second_bytes = 0;
	progress->second_start = now;
}

/*
 * Writes chunks for the duration, then shuts the sending side down and
 * reads until the peer closes, so the peer has taken every byte.
 */
static int
perf_send(
	int descriptor,
	unsigned char *buffer,
	unsigned long chunk,
	unsigned long seconds,
	struct perf_progress *progress)
{
	ssize_t moved;
	int error;

	/* Each write until the duration has passed. */
	for (;;) {
		if ((unsigned long)perf_ticks >= seconds)
			break;

		/* The write, which blocks until the kernel takes some of it. */
		moved = write(descriptor, buffer, chunk);
		if (moved < 0) {
			error = errno;
			if (error == EINTR)
				continue;
			return error;
		}

		/* The bytes count toward the second's rate. */
		perf_count(progress, (size_t)moved);
	}

	/* The end of the stream for the peer. */
	error = shutdown(descriptor, SHUT_WR);
	if (error != 0)
		return errno;

	/* Whatever the peer sends, until it closes. */
	for (;;) {
		moved = read(descriptor, buffer, chunk);
		if (moved == 0)
			break;
		if (moved < 0) {
			error = errno;
			if (error == EINTR)
				continue;
			return error;
		}
	}

	/* Succeeded: the peer closed after taking the stream. */
	return 0;
}

/*
 * Reads chunks for the duration or until the peer closes.
 */
static int
perf_receive(
	int descriptor,
	unsigned char *buffer,
	unsigned long chunk,
	unsigned long seconds,
	struct perf_progress *progress)
{
	ssize_t moved;
	int error;

	/* Each read until the duration has passed or the stream ends. */
	for (;;) {
		if ((unsigned long)perf_ticks >= seconds)
			break;

		/* The read, which blocks until some bytes arrive. */
		moved = read(descriptor, buffer, chunk);
		if (moved == 0)
			break;
		if (moved < 0) {
			error = errno;
			if (error == EINTR)
				continue;
			return error;
		}

		/* The bytes count toward the second's rate. */
		perf_count(progress, (size_t)moved);
	}

	/* Succeeded: the duration passed or the peer ended the stream. */
	return 0;
}
