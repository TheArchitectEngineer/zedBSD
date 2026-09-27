/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-028 regression: a TCP connect to a port nobody listens on fails with
 * ECONNREFUSED at once, blocking and non-blocking, on loopback and on the
 * host's own address.  An alarm ends a connect that never returns.  A
 * connect to a listener and a UDP datagram to the same address arrive,
 * which needs the host to deliver its own address to itself.
 *
 *   connect-refused [ADDRESS...]      (default 127.0.0.1)
 *
 * Prints one line per check and exits with the number of failures.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* The seconds a single connect may take before the check fails. */
#define CONNECT_LIMIT_SECONDS 10

static int closed_port(struct sockaddr_in *address);
static int check_blocking(const struct sockaddr_in *address);
static int check_nonblocking(const struct sockaddr_in *address);
static int check_listener_after_close(const struct sockaddr_in *address);
static int check_listener(const struct sockaddr_in *address);
static int check_datagram(const struct sockaddr_in *address);
static void report(const char *name, const char *address, int passed, int error);
static void on_alarm(int signal_number);

/*
 * Runs the checks against each address.
 */
int
main(
	int argc,
	char **argv)
{
	struct sockaddr_in address;
	struct sigaction action;
	const char *text;
	int index, failures, error, parsed;

	failures = 0;

	/* Lets the alarm interrupt a connect that never returns. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_alarm;
	sigaction(SIGALRM, &action, NULL);

	/* Checks each address given, or loopback. */
	for (index = 1; index < argc || index == 1; index++) {
		text = "127.0.0.1";
		if (index < argc)
			text = argv[index];

		/* Reads the address. */
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		parsed = inet_pton(AF_INET, text, &address.sin_addr);
		if (parsed != 1) {
			fprintf(stderr, "bad address %s\n", text);
			return 100;
		}

		/* Finds a port nobody listens on. */
		error = closed_port(&address);
		if (error != 0) {
			fprintf(stderr, "no closed port on %s: %s\n", text, strerror(error));
			return 100;
		}

		/* Runs the three checks. */
		error = check_blocking(&address);
		report("blocking connect is refused", text, error == ECONNREFUSED, error);
		if (error != ECONNREFUSED)
			failures++;

		error = check_nonblocking(&address);
		report("non-blocking connect reports refused", text, error == ECONNREFUSED, error);
		if (error != ECONNREFUSED)
			failures++;

		error = check_listener_after_close(&address);
		report("port of a closed listener is refused", text, error == ECONNREFUSED, error);
		if (error != ECONNREFUSED)
			failures++;

		error = check_listener(&address);
		report("connect to a listener succeeds", text, error == 0, error);
		if (error != 0)
			failures++;

		error = check_datagram(&address);
		report("datagram arrives", text, error == 0, error);
		if (error != 0)
			failures++;

		/* Stops after the default address. */
		if (argc == 1)
			break;
	}

	/* Reports the number of failed checks. */
	printf("failures %d\n", failures);
	return failures;
}

/* Binds and closes a listener so that its port is known to be free. */
static int
closed_port(
	struct sockaddr_in *address)
{
	struct sockaddr_in bound;
	socklen_t length;
	int fd, status, error;

	/* Opens a socket and lets the kernel pick a port. */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;

	address->sin_port = 0;
	status = bind(fd, (struct sockaddr *)address, sizeof(*address));
	if (status != 0) {
		error = errno;
		close(fd);
		return error;
	}

	/* Reads the port it picked and gives it back. */
	length = sizeof(bound);
	status = getsockname(fd, (struct sockaddr *)&bound, &length);
	error = errno;
	close(fd);
	if (status != 0)
		return error;

	/* Succeeded: the port is free. */
	address->sin_port = bound.sin_port;
	return 0;
}

/* Reports the errno of a blocking connect, or 0 when it connected. */
static int
check_blocking(
	const struct sockaddr_in *address)
{
	int fd, status, error;

	/* Opens the socket. */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;

	/* Connects under the alarm. */
	alarm(CONNECT_LIMIT_SECONDS);
	status = connect(fd, (const struct sockaddr *)address, sizeof(*address));
	error = errno;
	alarm(0);
	close(fd);

	/* Reports what the connect returned. */
	if (status == 0)
		return 0;

	/* Succeeded: the connect failed with this errno. */
	return error;
}

/* Reports SO_ERROR of a non-blocking connect once poll reports it done. */
static int
check_nonblocking(
	const struct sockaddr_in *address)
{
	struct pollfd poller;
	socklen_t length;
	int fd, status, error, flags, ready;

	/* Opens a non-blocking socket. */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;

	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	/* Starts the connect; a refusal may already be known. */
	status = connect(fd, (const struct sockaddr *)address, sizeof(*address));
	error = errno;
	if (status == 0) {
		close(fd);
		return 0;
	}

	if (error != EINPROGRESS) {
		close(fd);
		return error;
	}

	/* Waits for the connect to finish. */
	poller.fd = fd;
	poller.events = POLLOUT;
	poller.revents = 0;
	ready = poll(&poller, 1, CONNECT_LIMIT_SECONDS * 1000);
	if (ready <= 0) {
		close(fd);
		return ETIMEDOUT;
	}

	/* Reads the outcome. */
	length = sizeof(error);
	error = 0;
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
	close(fd);

	/* Succeeded: this is how the connect ended. */
	return error;
}

/* Listens on the port, closes the listener, then connects to it. */
static int
check_listener_after_close(
	const struct sockaddr_in *address)
{
	int fd, status;

	/* Opens and closes a listener on the port. */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;

	status = bind(fd, (const struct sockaddr *)address, sizeof(*address));
	if (status == 0)
		status = listen(fd, 1);
	close(fd);
	if (status != 0)
		return errno;

	/* Succeeded: the connect to the closed port ends like any other. */
	return check_blocking(address);
}

/* Connects to a listener on the address; reports 0 when it connected. */
static int
check_listener(
	const struct sockaddr_in *address)
{
	struct sockaddr_in bound;
	socklen_t length;
	int listener, fd, status, error;

	/* Listens on a port the kernel picks. */
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0)
		return errno;
	bound = *address;
	bound.sin_port = 0;
	status = bind(listener, (struct sockaddr *)&bound, sizeof(bound));
	if (status == 0)
		status = listen(listener, 1);
	length = sizeof(bound);
	if (status == 0)
		status = getsockname(listener, (struct sockaddr *)&bound, &length);
	if (status != 0) {
		error = errno;
		close(listener);
		return error;
	}

	/* Connects to it under the alarm and takes the connection. */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	alarm(CONNECT_LIMIT_SECONDS);
	status = connect(fd, (struct sockaddr *)&bound, sizeof(bound));
	error = errno;
	alarm(0);
	if (status == 0) {
		error = 0;
		status = accept(listener, NULL, NULL);
		if (status < 0)
			error = errno;
		else
			close(status);
	}
	close(fd);
	close(listener);

	/* Succeeded: this is how the connect ended. */
	return error;
}

/* Sends a datagram to the address and waits for it; reports 0 on arrival. */
static int
check_datagram(
	const struct sockaddr_in *address)
{
	struct sockaddr_in bound;
	struct pollfd poller;
	socklen_t length;
	char buffer[8];
	int receiver, sender, ready;
	ssize_t count;

	/* Binds a receiver on the address. */
	receiver = socket(AF_INET, SOCK_DGRAM, 0);
	sender = socket(AF_INET, SOCK_DGRAM, 0);
	if (receiver < 0 || sender < 0)
		return errno;
	bound = *address;
	bound.sin_port = 0;
	length = sizeof(bound);
	if (bind(receiver, (struct sockaddr *)&bound, sizeof(bound)) != 0 ||
	    getsockname(receiver, (struct sockaddr *)&bound, &length) != 0)
		return errno;

	/* Sends one datagram and waits for it. */
	count = sendto(sender, "ping", 4, 0, (struct sockaddr *)&bound, sizeof(bound));
	if (count != 4)
		return errno;
	poller.fd = receiver;
	poller.events = POLLIN;
	poller.revents = 0;
	ready = poll(&poller, 1, CONNECT_LIMIT_SECONDS * 1000);
	if (ready <= 0)
		return ETIMEDOUT;
	count = recv(receiver, buffer, sizeof(buffer), 0);
	close(receiver);
	close(sender);
	if (count != 4)
		return EIO;

	/* Succeeded: the datagram arrived. */
	return 0;
}

/* Prints the outcome of one check. */
static void
report(
	const char *name,
	const char *address,
	int passed,
	int error)
{
	/* Writes PASS or FAIL with the errno seen. */
	printf("%s %s %s (%s)\n", passed ? "PASS" : "FAIL", name, address,
	    error == 0 ? "connected" : strerror(error));
	fflush(stdout);
}

/* Does nothing: the alarm exists to interrupt a stuck connect. */
static void
on_alarm(
	int signal_number)
{
	(void)signal_number;
}
