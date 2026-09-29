/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-108 probe: a unix stream connect to a listener whose backlog is full.
 *
 * With listen(fd, 1) and one connection left unaccepted:
 *   1. a nonblocking connect fails at once with EAGAIN;
 *   2. a blocking connect (in a child) waits, and completes once the
 *      listener accepts;
 *   3. a blocking connect that is waiting fails with ECONNREFUSED when the
 *      listener is closed;
 *   4. a blocking connect that is waiting ends with EINTR on a signal
 *      whose handler does not restart.
 * Also listen(fd, 1000) takes 128 connections without an accept.
 * Prints UNIX-BACKLOG:PASS.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static int listener_open(const char *path, int backlog);
static int client_connect(const char *path, int nonblocking, int *error);
static void quiet(int signo);
static pid_t waiting_child(const char *path, int listener);

/*
 * Runs the backlog checks.
 */
int
main(
	void)
{
	const char *path = "/tmp/unix-backlog.sock";
	int listener, first, second, accepted, error, status, i, count;
	int clients[140];
	pid_t child;
	struct timespec pause;

	pause.tv_sec = 1;
	pause.tv_nsec = 0;

	/* 1: fills a backlog of one, then a nonblocking connect. */
	listener = listener_open(path, 1);
	first = client_connect(path, 0, &error);
	check(first >= 0, "the first connect fills the backlog");
	second = client_connect(path, 1, &error);
	check(second < 0 && error == EAGAIN, "a nonblocking connect to a full backlog is EAGAIN");

	/* 2: a blocking connect waits until an accept makes room. */
	child = waiting_child(path, listener);
	nanosleep(&pause, NULL);
	check(waitpid(child, &status, WNOHANG) == 0, "a blocking connect to a full backlog waits");
	accepted = accept(listener, NULL, NULL);
	check(accepted >= 0, "the accept takes the first connection");
	(void)waitpid(child, &status, 0);
	check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the waiting connect completes after the accept");

	/* 3: closing the listener fails a waiting connect. */
	child = waiting_child(path, listener);
	nanosleep(&pause, NULL);
	check(waitpid(child, &status, WNOHANG) == 0, "the second waiting connect waits");
	close(listener);
	(void)waitpid(child, &status, 0);
	check(WIFEXITED(status) && WEXITSTATUS(status) == ECONNREFUSED % 256,
	      "closing the listener refuses the waiting connect");
	close(first);
	close(accepted);

	/* 4: a signal ends a waiting connect with EINTR. */
	listener = listener_open(path, 1);
	first = client_connect(path, 0, &error);
	child = waiting_child(path, listener);
	nanosleep(&pause, NULL);
	kill(child, SIGUSR1);
	(void)waitpid(child, &status, 0);
	check(WIFEXITED(status) && WEXITSTATUS(status) == EINTR % 256, "a signal ends the waiting connect with EINTR");
	close(first);
	close(listener);

	/* 5: a large backlog takes 128 connections without an accept. */
	listener = listener_open(path, 1000);
	count = 0;
	for (i = 0; i < 128; i++) {
		clients[i] = client_connect(path, 1, &error);
		if (clients[i] >= 0)
			count++;
	}
	check(count == 128, "a backlog of 128 connections is kept");
	clients[128] = client_connect(path, 1, &error);
	check(clients[128] < 0 && error == EAGAIN, "the 129th nonblocking connect is EAGAIN");
	printf("large backlog: %d connections queued\n", count);
	close(listener);
	unlink(path);

	/* Reports the result. */
	printf("%s\n", failures == 0 ? "UNIX-BACKLOG:PASS" : "UNIX-BACKLOG:FAIL");
	return failures == 0 ? 0 : 1;
}

/* Counts and names one failed check. */
static void
check(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	fflush(stdout);
	if (!condition)
		failures++;
}

/* Binds and listens on PATH with BACKLOG. */
static int
listener_open(
	const char *path,
	int backlog)
{
	struct sockaddr_un address;
	int fd;

	unlink(path);
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, backlog) != 0)
		printf("listener: %s\n", strerror(errno));
	return fd;
}

/* Connects to PATH; returns the descriptor or -1 with *error set. */
static int
client_connect(
	const char *path,
	int nonblocking,
	int *error)
{
	struct sockaddr_un address;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (nonblocking)
		(void)fcntl(fd, F_SETFL, O_NONBLOCK);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path);
	*error = 0;
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		*error = errno;
		close(fd);
		return -1;
	}
	return fd;
}

/* A handler that only interrupts. */
static void
quiet(
	int signo)
{
	(void)signo;
}

/*
 * Starts a child that makes one blocking connect and exits with its errno
 * (0 on success).  The child closes its inherited copy of the listener, so
 * the parent's close is the last one.
 */
static pid_t
waiting_child(
	const char *path,
	int listener)
{
	struct sigaction action;
	pid_t child;
	int fd, error;

	child = fork();
	if (child != 0)
		return child;
	close(listener);
	memset(&action, 0, sizeof(action));
	action.sa_handler = quiet;
	(void)sigaction(SIGUSR1, &action, NULL);
	fd = client_connect(path, 0, &error);
	(void)fd;
	_exit(error % 256);
}
