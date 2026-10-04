/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The account on zedBSD (ws160-p002): the user's password changed by
 * passwd's batch mode (userland/base/passwd, ws160-p001).
 *
 * passwd -s runs as a child with its standard input a pipe and its output
 * and errors thrown away; the current and the new password go down the
 * pipe one a line, and its exit status says how it went (account.h).
 * passwd is set-user-ID root and changes the account of its real user ID,
 * the compositor's own user: the compositor holds no privilege.  The child
 * runs only async-signal-safe calls before its exec, as the compositor has
 * threads.  The lines are wiped after they are written.  SIGPIPE, when
 * passwd ends before it read them, is held for this thread and taken back
 * before the thread goes on.
 */

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include "userland/base/common/account.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The lines' room: two passwords, their ends and a NUL. */
#define ACCOUNT_LINES		(2U * (ACCOUNT_PASSWORD_MAX + 1U) + 1U)

static int account_write(int descriptor, const char *text, size_t length);
static void account_wipe(char *text, size_t size);

/*
 * Changes the password of the compositor's user from current to fresh.
 */
int
kl_backend_account_set_password(
	const char *current,
	const char *fresh)
{
	char lines[ACCOUNT_LINES];
	char *argv[3];
	struct timespec none;
	sigset_t pipe_signal;
	sigset_t previous;
	size_t current_length;
	size_t fresh_length;
	size_t current_clean;
	size_t fresh_clean;
	size_t length;
	pid_t child;
	pid_t waited;
	int descriptors[2];
	int null;
	int status;
	int exited;
	int error;

	/* Two passwords that are not empty, fit and hold no line end. */
	current_length = strlen(current);
	fresh_length = strlen(fresh);
	if (current_length == 0U || fresh_length == 0U)
		return EINVAL;
	if (current_length > ACCOUNT_PASSWORD_MAX || fresh_length > ACCOUNT_PASSWORD_MAX)
		return EINVAL;
	current_clean = strcspn(current, "\n");
	fresh_clean = strcspn(fresh, "\n");
	if (current_clean != current_length || fresh_clean != fresh_length)
		return EINVAL;

	/* The pipe passwd reads, closed on exec on this side. */
	error = pipe(descriptors);
	if (error != 0)
		return EIO;
	(void)fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);

	/* SIGPIPE held for this thread while the lines go. */
	sigemptyset(&pipe_signal);
	sigaddset(&pipe_signal, SIGPIPE);
	(void)pthread_sigmask(SIG_BLOCK, &pipe_signal, &previous);

	/* passwd -s, its input the pipe, its output and errors thrown away. */
	argv[0] = "passwd";
	argv[1] = "-s";
	argv[2] = NULL;
	null = open("/dev/null", O_WRONLY | O_CLOEXEC);
	child = fork();
	if (child == 0) {
		/* Async-signal-safe calls only, then the exec. */
		(void)dup2(descriptors[0], STDIN_FILENO);
		if (null >= 0) {
			(void)dup2(null, STDOUT_FILENO);
			(void)dup2(null, STDERR_FILENO);
		}

		/* passwd. */
		(void)execv(ACCOUNT_PASSWD_PATH, argv);

		/* passwd could not run. */
		_exit(127);
	}

	/* The child's ends are its own now. */
	(void)close(descriptors[0]);
	if (null >= 0)
		(void)close(null);
	if (child < 0) {
		(void)close(descriptors[1]);
		(void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
		return EIO;
	}

	/* The two lines, wiped once written. */
	length = (size_t)snprintf(lines, sizeof(lines), "%s\n%s\n", current, fresh);
	error = account_write(descriptors[1], lines, length);
	account_wipe(lines, sizeof(lines));
	(void)close(descriptors[1]);

	/* passwd's end. */
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);

	/* A SIGPIPE it raised is taken, and the mask is as it was. */
	none.tv_sec = 0;
	none.tv_nsec = 0;
	(void)sigtimedwait(&pipe_signal, NULL, &none);
	(void)pthread_sigmask(SIG_SETMASK, &previous, NULL);

	/* passwd did not end by itself, or could not run. */
	if (waited < 0)
		return EIO;
	exited = WIFEXITED(status);
	if (!exited)
		return EIO;
	(void)error;

	/* Its exit status. */
	switch (WEXITSTATUS(status)) {
	case ACCOUNT_PASSWD_OK:
		return 0;
	case ACCOUNT_PASSWD_WRONG:
		return EACCES;
	case ACCOUNT_PASSWD_REFUSED:
	case ACCOUNT_PASSWD_MISMATCH:
		return EINVAL;
	default:
		return EIO;
	}
}

/* Writes the whole text to the pipe; returns 0, or an errno value (EPIPE when passwd ended). */
static int
account_write(
	int descriptor,
	const char *text,
	size_t length)
{
	ssize_t written;
	size_t done;

	/* All of it. */
	done = 0;
	while (done < length) {
		written = write(descriptor, text + done, length - done);
		if (written < 0 && errno == EINTR)
			continue;
		if (written < 0)
			return errno;
		done += (size_t)written;
	}

	/* Succeeded: written. */
	return 0;
}

/* Overwrites text that held a password. */
static void
account_wipe(
	char *text,
	size_t size)
{
	volatile char *byte;
	size_t index;

	/* Byte by byte, so that the compiler keeps the writes. */
	byte = text;
	for (index = 0; index < size; index++)
		byte[index] = '\0';
}
