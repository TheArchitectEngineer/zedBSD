/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The installer's helper, install-admin, as the installer sees it: a probe
 * that answers and ends (while no session runs), and a session that lives
 * from the first write until the installer ends it, its requests one at a
 * time.  The helper's lines come on a pipe the application watches; each
 * line goes to the flow (flow.c) with the request it answers, and the
 * line that ends a request ("end" of a probe, "DONE" or "FAIL" of the
 * others) frees the helper for the next.  The helper's standard error is
 * the installer's, the session's log.
 */

#include "installer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int helper_spawn(const char *verb, int *to_child, int *from_child, pid_t *pid);
static void helper_read(struct inst_app *app, int fd);
static void helper_line(struct inst_app *app, char *line);
static void helper_hangup(struct inst_app *app, int fd);
static int helper_terminal(enum inst_job job, const char *line);
static int helper_write(int fd, const char *text, size_t length);

/*
 * Asks the helper what the disks are: the session's probe while a session
 * runs, else a probe of its own.  Returns 0 when asked, EBUSY while
 * another request is answered, or the error of starting the helper.
 */
int
inst_helper_probe(
	struct inst_app *app)
{
	struct inst_helper *helper;
	int error;

	/* One request at a time. */
	helper = &app->helper;
	if (helper->job != INST_JOB_NONE)
		return EBUSY;

	/* A session answers the probe itself (a second helper would find the lock taken). */
	if (helper->session_pid > 0) {
		error = inst_helper_request(app, helper->session_disk, INST_JOB_PROBE, "probe\n");
		if (error != 0)
			return error;

		/* Succeeded: the session probes. */
		return 0;
	}

	/* A probe of its own, its output watched. */
	error = helper_spawn("probe", NULL, &helper->probe_fd, &helper->probe_pid);
	if (error != 0)
		return error;
	error = inst_helper_watch(app, helper->probe_fd);
	if (error != 0)
		inst_log("HELPER watch errno=%d", error);
	helper->job = INST_JOB_PROBE;
	helper->line_used = 0;
	helper->line_long = 0;
	inst_log("HELPER probe pid=%d", (int)helper->probe_pid);

	/* Succeeded: the probe runs. */
	return 0;
}

/*
 * Sends a request to the session, starting the session (bound to a disk)
 * when none runs.  A cancellation sent while an installation runs is only
 * written: the installation answers it.  Returns 0 when sent, EBUSY while
 * another request is answered or a session is ending, or the error of
 * starting or writing to the helper.
 */
int
inst_helper_request(
	struct inst_app *app,
	uint32_t disk,
	enum inst_job job,
	const char *request)
{
	struct inst_helper *helper;
	size_t length;
	int error;

	/* A cancellation of the installation that runs goes on the same input. */
	helper = &app->helper;
	length = strlen(request);
	if (job == INST_JOB_CANCEL && helper->job == INST_JOB_INSTALL && helper->session_in >= 0) {
		error = helper_write(helper->session_in, request, length);
		if (error != 0)
			return error;

		/* Succeeded: the installation stops at its next point. */
		return 0;
	}

	/* One request at a time; a session that ends is waited out. */
	if (helper->job != INST_JOB_NONE)
		return EBUSY;
	if (helper->session_pid > 0 && helper->session_in < 0)
		return EBUSY;

	/* A session bound to another disk ends first (spec 5.2). */
	if (helper->session_pid > 0 && helper->session_disk != disk) {
		inst_helper_end_session(app);
		return EBUSY;
	}

	/* The session, when none runs. */
	if (helper->session_pid <= 0) {
		error = helper_spawn("session", &helper->session_in, &helper->session_out, &helper->session_pid);
		if (error != 0)
			return error;
		error = inst_helper_watch(app, helper->session_out);
		if (error != 0)
			inst_log("HELPER watch errno=%d", error);
		helper->session_disk = disk;
		helper->line_used = 0;
		helper->line_long = 0;
		inst_log("HELPER session pid=%d disk=%u", (int)helper->session_pid, (unsigned)disk);
	}

	/* The request, whole. */
	helper->job = job;
	error = helper_write(helper->session_in, request, length);
	if (error != 0) {
		helper->job = INST_JOB_NONE;
		return error;
	}

	/* Succeeded: the session answers it. */
	return 0;
}

/*
 * Ends the session: its input closes, which the helper takes as the end
 * (as a cancellation between requests), and its output is read until it
 * closes too.
 */
void
inst_helper_end_session(
	struct inst_app *app)
{
	struct inst_helper *helper;

	/* Nothing runs. */
	helper = &app->helper;
	if (helper->session_pid <= 0 || helper->session_in < 0)
		return;

	/* The end of its input. */
	(void)close(helper->session_in);
	helper->session_in = -1;
	inst_log("HELPER session-end pid=%d", (int)helper->session_pid);
}

/*
 * Takes what a helper's pipe became: its lines while it is readable, its
 * end when it hung up.
 */
void
inst_helper_ready(
	struct inst_app *app,
	int fd,
	unsigned ready)
{
	/* The lines that came. */
	if ((ready & KL_APP_FD_READ) != 0U)
		helper_read(app, fd);

	/* A pipe that closed (read to its end above when it had more). */
	if ((ready & KL_APP_FD_HANGUP) != 0U)
		helper_hangup(app, fd);
}

/* Reports whether the helper is answering a request. */
int
inst_helper_busy(
	const struct inst_app *app)
{
	/* A request is answered. */
	if (app->helper.job != INST_JOB_NONE)
		return 1;

	/* A session that ends is still busy. */
	if (app->helper.session_pid > 0 && app->helper.session_in < 0)
		return 1;

	/* Free. */
	return 0;
}

/* Watches a helper's pipe for its lines; returns kl_app_watch_fd's answer. */
int
inst_helper_watch(
	struct inst_app *app,
	int fd)
{
	int error;

	/* Readable, and its end (always told). */
	error = kl_app_watch_fd(app->app, fd, KL_APP_FD_READ);
	if (error != 0)
		return error;

	/* Succeeded: its lines arrive as the application's events. */
	return 0;
}

/*
 * Starts the helper with a verb, its output on a new pipe (non-blocking)
 * and, when asked, its input on another.  Returns 0 or an errno value.
 */
static int
helper_spawn(
	const char *verb,
	int *to_child,
	int *from_child,
	pid_t *pid)
{
	char *argument[3];
	int output[2];
	int input[2];
	int flags;
	int error;
	pid_t child;

	/* The pipes. */
	input[0] = -1;
	input[1] = -1;
	error = pipe(output);
	if (error != 0)
		return errno;
	if (to_child != NULL) {
		error = pipe(input);
		if (error != 0) {
			error = errno;
			(void)close(output[0]);
			(void)close(output[1]);
			return error;
		}
	}

	/* The child: the pipes as its input and output, nothing else of the installer's. */
	child = fork();
	if (child < 0) {
		error = errno;
		(void)close(output[0]);
		(void)close(output[1]);
		if (input[0] >= 0) {
			(void)close(input[0]);
			(void)close(input[1]);
		}
		return error;
	}
	if (child == 0) {
		if (input[0] >= 0)
			(void)dup2(input[0], 0);
		(void)dup2(output[1], 1);
		closefrom(3);
		argument[0] = (char *)INST_HELPER;
		argument[1] = (char *)verb;
		argument[2] = NULL;
		execv(INST_HELPER, argument);
		_exit(127);
	}

	/* The installer's ends: the output read without waiting, the input written whole. */
	(void)close(output[1]);
	flags = fcntl(output[0], F_GETFL);
	if (flags >= 0)
		(void)fcntl(output[0], F_SETFL, flags | O_NONBLOCK);
	(void)fcntl(output[0], F_SETFD, FD_CLOEXEC);
	*from_child = output[0];
	if (to_child != NULL) {
		(void)close(input[0]);
		(void)fcntl(input[1], F_SETFD, FD_CLOEXEC);
		*to_child = input[1];
	}
	*pid = child;

	/* Succeeded: the helper runs. */
	return 0;
}

/* Reads what a pipe holds and hands each whole line on. */
static void
helper_read(
	struct inst_app *app,
	int fd)
{
	struct inst_helper *helper;
	char buffer[4096];
	ssize_t count;
	ssize_t i;

	/* Until the pipe has nothing more for now. */
	helper = &app->helper;
	for (;;) {
		count = read(fd, buffer, sizeof(buffer));
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return;

		/* Each byte into the line; a newline ends it (a line too long is dropped whole). */
		for (i = 0; i < count; i++) {
			if (buffer[i] == '\n') {
				helper->line[helper->line_used] = '\0';
				if (!helper->line_long)
					helper_line(app, helper->line);
				helper->line_used = 0;
				helper->line_long = 0;
				continue;
			}
			if (helper->line_used + 1U >= sizeof(helper->line)) {
				helper->line_long = 1;
				continue;
			}
			helper->line[helper->line_used] = buffer[i];
			helper->line_used++;
		}
	}
}

/* Gives a line to the flow, and frees the helper when it ends a request. */
static void
helper_line(
	struct inst_app *app,
	char *line)
{
	enum inst_job job;
	int ended;

	/* The line, for the request it answers. */
	job = app->helper.job;
	ended = helper_terminal(job, line);
	inst_flow_line(app, job, line);

	/* The end of a session's request: the next may be sent. */
	if (ended && app->helper.session_pid > 0 && app->helper.job == job) {
		app->helper.job = INST_JOB_NONE;
		inst_flow_ended(app, job, 0);
	}
}

/*
 * Takes the end of a helper's pipe: the probe or the session has ended.
 * A request it had not answered ends with it.
 */
static void
helper_hangup(
	struct inst_app *app,
	int fd)
{
	struct inst_helper *helper;
	enum inst_job job;
	pid_t ended;
	int status;

	/* Whose pipe it was. */
	helper = &app->helper;
	(void)kl_app_watch_fd(app->app, fd, 0U);
	(void)close(fd);
	status = -1;
	job = helper->job;

	/* The probe of its own: it has ended. */
	if (fd == helper->probe_fd) {
		ended = waitpid(helper->probe_pid, &status, 0);
		if (ended != helper->probe_pid)
			status = -1;
		inst_log("HELPER probe-end status=%d", status);
		helper->probe_fd = -1;
		helper->probe_pid = 0;
		helper->job = INST_JOB_NONE;
		inst_flow_ended(app, job, status);
		return;
	}

	/* Another pipe is not the helper's. */
	if (fd != helper->session_out)
		return;

	/* The session: it has ended, with whatever it was answering. */
	ended = waitpid(helper->session_pid, &status, 0);
	if (ended != helper->session_pid)
		status = -1;
	inst_log("HELPER session-gone status=%d job=%d", status, (int)job);
	if (helper->session_in >= 0)
		(void)close(helper->session_in);
	helper->session_in = -1;
	helper->session_out = -1;
	helper->session_pid = 0;
	helper->session_disk = 0;
	helper->job = INST_JOB_NONE;
	if (job != INST_JOB_NONE)
		inst_flow_ended(app, job, status == 0 ? 0 : -1);
}

/* Reports whether a line of the session ends the request it answers. */
static int
helper_terminal(
	enum inst_job job,
	const char *line)
{
	int same;

	/* A failure ends any request. */
	same = strncmp(line, "FAIL ", 5U);
	if (same == 0)
		return 1;

	/* A probe ends with its "end". */
	if (job == INST_JOB_PROBE) {
		same = strcmp(line, "end");
		if (same == 0)
			return 1;
		return 0;
	}

	/* The others end with DONE. */
	same = strcmp(line, "DONE");
	if (same == 0)
		return 1;

	/* A line within the answer. */
	return 0;
}

/* Writes a text whole to a descriptor; returns 0 or an errno value. */
static int
helper_write(
	int fd,
	const char *text,
	size_t length)
{
	size_t done;
	ssize_t count;

	/* Until every byte went. */
	done = 0;
	while (done < length) {
		count = write(fd, text + done, length - done);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
			return errno;
		if (count == 0)
			return EIO;
		done += (size_t)count;
	}

	/* Succeeded: the text was written. */
	return 0;
}
