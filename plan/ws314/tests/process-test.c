/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test of kl_process_*: a small Wayland server (libwayland-server)
 * announces the two globals kl_app_open needs, and the application starts
 * processes and checks their lines, bytes, input, end, signals, environment,
 * directory and terminal.  Prints one line per case and PASS or FAIL.
 */

#include <keiland/keiland.h>

#ifdef KL_TEST_OWN_SERVER
#include <wayland-server.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef KL_TEST_OWN_SERVER
static struct wl_display *server;
#endif
#ifdef KL_TEST_OWN_SERVER
static _Atomic int server_stop;
#endif
static struct kl_app *app;
static int failures;

#ifdef KL_TEST_OWN_SERVER
static const struct wl_interface fake_compositor = { "wl_compositor", 4, 0, NULL, 0, NULL };
static const struct wl_interface fake_shell = { "xdg_wm_base", 1, 0, NULL, 0, NULL };

static void
bind_nothing(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	(void)client;
	(void)data;
	(void)version;
	(void)id;
}

static void *
server_run(void *argument)
{
	struct wl_event_loop *loop;

	(void)argument;
	loop = wl_display_get_event_loop(server);
	while (!server_stop) {
		wl_event_loop_dispatch(loop, 50);
		wl_display_flush_clients(server);
	}
	return NULL;
}
#endif

/* The display: a server of the test's own (the host), or the compositor's (WAYLAND_DISPLAY, a zedBSD guest). */
#ifdef KL_TEST_OWN_SERVER
static pthread_t server_thread;

static const char *
test_server_start(void)
{
	const char *socket_name;

	server = wl_display_create();
	socket_name = wl_display_add_socket_auto(server);
	if (socket_name == NULL)
		return NULL;
	setenv("WAYLAND_DISPLAY", socket_name, 1);
	wl_global_create(server, &fake_compositor, 4, NULL, bind_nothing);
	wl_global_create(server, &fake_shell, 1, NULL, bind_nothing);
	pthread_create(&server_thread, NULL, server_run, NULL);
	return socket_name;
}

static void
test_server_stop(void)
{
	server_stop = 1;
	pthread_join(server_thread, NULL);
}
#else
static const char *
test_server_start(void)
{
	return getenv("WAYLAND_DISPLAY") != NULL ? getenv("WAYLAND_DISPLAY") : "wayland-0";
}

static void
test_server_stop(void)
{
}
#endif

static uint64_t
now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void
check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

/* What one process told: its lines or bytes, and its end. */
struct seen {
	char output[8192];
	size_t output_length;
	char error[1024];
	size_t error_length;
	char lines[16][256];
	int kinds[16];
	unsigned line_count;
	int exited;
	int output_after_exit;
	size_t bytes;
	unsigned events;
	size_t read_size;
	int by_line;
	size_t line_size;
};

/* Runs the application's loop until the process's end was told (or a timeout); the seen record's options say how to read. */
static int
run(struct kl_process *process, struct seen *seen, unsigned timeout_ms)
{
	struct kl_app_event event;
	char buffer[4096];
	char line[256];
	uint64_t start;
	size_t got;
	int kind;

	start = now_ms();
	while (!seen->exited) {
		if (now_ms() - start > timeout_ms) {
			printf("     (timeout: events=%u exit=%u)\n", seen->events, kl_process_exit(process, NULL));
			return 0;
		}
		if (kl_app_dispatch(app, 200) != 0) {
			printf("     (dispatch failed)\n");
			return 0;
		}
		while (kl_app_take(app, &event)) {
			if (event.kind != KL_APP_PROCESS || event.process != process)
				continue;
			seen->events++;
			if ((event.ready & KL_PROCESS_READY_OUTPUT) != 0U) {
				if (seen->exited)
					seen->output_after_exit = 1;
				if (seen->by_line) {
					for (;;) {
						kind = kl_process_read_line(process, KL_PROCESS_OUTPUT, line, seen->line_size);
						if (kind == 0)
							break;
						if (seen->line_count < 16U) {
							snprintf(seen->lines[seen->line_count], sizeof(seen->lines[0]), "%s", line);
							seen->kinds[seen->line_count] = kind;
							seen->line_count++;
						}
					}
				} else {
					got = kl_process_read(process, KL_PROCESS_OUTPUT, buffer, seen->read_size != 0U ? seen->read_size : sizeof(buffer));
					seen->bytes += got;
					if (seen->output_length + got < sizeof(seen->output)) {
						memcpy(seen->output + seen->output_length, buffer, got);
						seen->output_length += got;
						seen->output[seen->output_length] = '\0';
					}
				}
			}
			if ((event.ready & KL_PROCESS_READY_ERROR) != 0U) {
				got = kl_process_read(process, KL_PROCESS_ERROR, buffer, sizeof(buffer));
				if (seen->error_length + got < sizeof(seen->error)) {
					memcpy(seen->error + seen->error_length, buffer, got);
					seen->error_length += got;
					seen->error[seen->error_length] = '\0';
				}
			}
			if ((event.ready & KL_PROCESS_READY_EXIT) != 0U)
				seen->exited = 1;
		}
	}
	return 1;
}

static struct kl_process *
start(const char *const *argv, const char *const *environment, const char *directory, unsigned streams, int *error)
{
	struct kl_process_options options;
	struct kl_process *process;

	memset(&options, 0, sizeof(options));
	options.argv = argv;
	options.environment = environment;
	options.directory = directory;
	options.streams = streams;
	options.columns = 80;
	options.rows = 24;
	*error = kl_process_start(app, &options, &process);
	return process;
}

static void
case_lines(void)
{
	const char *argv[] = { "sh", "-c", "echo out1; echo err1 >&2; printf 'a\\r\\nb\\nlast'; exit 3", NULL };
	struct kl_process *process;
	struct seen seen;
	unsigned how;
	int code;
	int error;

	memset(&seen, 0, sizeof(seen));
	seen.by_line = 1;
	seen.line_size = 128;
	process = start(argv, NULL, NULL, KL_PROCESS_OUTPUT | KL_PROCESS_ERROR, &error);
	check(error == 0, "lines: start");
	if (error != 0)
		return;
	check(run(process, &seen, 5000), "lines: ended");
	how = kl_process_exit(process, &code);
	check(seen.line_count == 4 && strcmp(seen.lines[0], "out1") == 0 && strcmp(seen.lines[1], "a") == 0 &&
	    strcmp(seen.lines[2], "b") == 0 && strcmp(seen.lines[3], "last") == 0, "lines: out1 a b last");
	check(strcmp(seen.error, "err1\n") == 0, "lines: error stream");
	check(how == KL_PROCESS_EXITED && code == 3, "lines: exit code 3");
	kl_process_close(process);
}

static void
case_input(void)
{
	const char *argv[] = { "cat", NULL };
	struct kl_process *process;
	struct seen seen;
	size_t written;
	int error;

	memset(&seen, 0, sizeof(seen));
	seen.by_line = 1;
	seen.line_size = 128;
	process = start(argv, NULL, NULL, KL_PROCESS_INPUT | KL_PROCESS_OUTPUT, &error);
	check(error == 0, "input: start");
	if (error != 0)
		return;
	error = kl_process_write(process, "hello\nworld\n", 12, &written);
	check(error == 0 && written == 12, "input: written");
	check(kl_process_close_input(process) == 0, "input: close");
	check(run(process, &seen, 5000), "input: ended");
	check(seen.line_count == 2 && strcmp(seen.lines[0], "hello") == 0 && strcmp(seen.lines[1], "world") == 0, "input: echoed");
	error = kl_process_write(process, "x", 1, &written);
	check(error == EPIPE, "input: EPIPE after the end");
	kl_process_close(process);
}

static void
case_flood(void)
{
	const char *argv[] = { "sh", "-c", "head -c 1000000 /dev/zero", NULL };
	struct kl_process *process;
	struct seen seen;
	char buffer[4096];
	size_t got;
	int error;

	memset(&seen, 0, sizeof(seen));
	seen.read_size = 1000;
	process = start(argv, NULL, NULL, KL_PROCESS_OUTPUT, &error);
	check(error == 0, "flood: start");
	if (error != 0)
		return;
	check(run(process, &seen, 20000), "flood: ended");
	for (;;) {
		got = kl_process_read(process, KL_PROCESS_OUTPUT, buffer, sizeof(buffer));
		if (got == 0U)
			break;
		seen.bytes += got;
	}
	check(seen.bytes == 1000000U, "flood: 1000000 bytes read 1000 at a time (level events)");
	check(!seen.output_after_exit, "flood: no output told after the end");
	kl_process_close(process);
}

static void
case_cut(void)
{
	const char *argv[] = { "sh", "-c", "printf '%0300d\\nshort\\n' 0", NULL };
	struct kl_process *process;
	struct seen seen;
	int error;

	memset(&seen, 0, sizeof(seen));
	seen.by_line = 1;
	seen.line_size = 100;
	process = start(argv, NULL, NULL, KL_PROCESS_OUTPUT, &error);
	check(error == 0, "cut: start");
	if (error != 0)
		return;
	check(run(process, &seen, 5000), "cut: ended");
	check(seen.line_count == 5 && seen.kinds[0] == KL_PROCESS_LINE_CUT && seen.kinds[1] == KL_PROCESS_LINE_CUT &&
	    seen.kinds[2] == KL_PROCESS_LINE_CUT && seen.kinds[3] == KL_PROCESS_LINE && strlen(seen.lines[0]) == 99 &&
	    strlen(seen.lines[3]) == 3 && strcmp(seen.lines[4], "short") == 0, "cut: 99+99+99 cut, 3 whole, short");
	kl_process_close(process);
}

static void
case_kill(void)
{
	const char *argv[] = { "sleep", "100", NULL };
	struct kl_process *process;
	struct seen seen;
	unsigned how;
	int code;
	int error;

	memset(&seen, 0, sizeof(seen));
	process = start(argv, NULL, NULL, 0, &error);
	check(error == 0, "kill: start without streams");
	if (error != 0)
		return;
	check(kl_process_exit(process, NULL) == KL_PROCESS_RUNNING, "kill: running");
	check(kl_process_kill(process, KL_PROCESS_TERMINATE) == 0, "kill: asked");
	check(run(process, &seen, 5000), "kill: ended");
	how = kl_process_exit(process, &code);
	check(how == KL_PROCESS_SIGNALED && code == 15, "kill: SIGTERM");
	check(kl_process_kill(process, KL_PROCESS_KILL) == 0, "kill: after the end is 0");
	kl_process_close(process);
}

static void
case_environment(void)
{
	const char *argv[] = { "sh", "-c", "echo \"$KL_TEST_FOO ${HOME-unset}\"; pwd", NULL };
	const char *environment[] = { "KL_TEST_FOO=bar", "HOME", NULL };
	struct kl_process *process;
	struct seen seen;
	int error;

	memset(&seen, 0, sizeof(seen));
	seen.by_line = 1;
	seen.line_size = 128;
	process = start(argv, environment, "/tmp", KL_PROCESS_OUTPUT, &error);
	check(error == 0, "environment: start");
	if (error != 0)
		return;
	check(run(process, &seen, 5000), "environment: ended");
	check(seen.line_count == 2 && strcmp(seen.lines[0], "bar unset") == 0 && strcmp(seen.lines[1], "/tmp") == 0, "environment: set, removed, directory");
	kl_process_close(process);
}

static void
case_missing(void)
{
	const char *argv[] = { "kl-no-such-program-xyz", NULL };
	const char *absolute[] = { "/nonexistent/program", NULL };
	int error;

	(void)start(argv, NULL, NULL, 0, &error);
	check(error == ENOENT, "missing: ENOENT in PATH");
	(void)start(absolute, NULL, NULL, 0, &error);
	check(error == ENOENT, "missing: ENOENT for a path");
}

/* Types a line into a terminal's shell and reads until text appears. */
static int
terminal_wait(struct kl_process *process, const char *text, char *screen, size_t size, size_t *length)
{
	struct kl_app_event event;
	uint64_t start;
	size_t got;

	start = now_ms();
	while (now_ms() - start < 5000U) {
		if (strstr(screen, text) != NULL)
			return 1;
		if (kl_app_dispatch(app, 200) != 0)
			return 0;
		while (kl_app_take(app, &event)) {
			if (event.kind != KL_APP_PROCESS || event.process != process)
				continue;
			if ((event.ready & KL_PROCESS_READY_OUTPUT) != 0U) {
				got = kl_process_read(process, KL_PROCESS_OUTPUT, screen + *length, size - *length - 1U);
				*length += got;
				screen[*length] = '\0';
			}
		}
	}
	return strstr(screen, text) != NULL;
}

/* Waits up to a second for the terminal's modes to become the ones expected (the shell sets them as it runs). */
static int
terminal_mode_wait(struct kl_process *process, unsigned expected)
{
	uint64_t start;
	unsigned mode;

	start = now_ms();
	do {
		if (kl_process_terminal_mode(process, &mode) == 0 &&
		    (mode & (KL_PROCESS_TERMINAL_ECHO | KL_PROCESS_TERMINAL_LINES)) == expected)
			return 1;
		(void)kl_app_dispatch(app, 50);
	} while (now_ms() - start < 1000U);
	return 0;
}

static void
case_terminal(void)
{
	const char *argv[] = { "sh", NULL };
	const char *environment[] = { "PS1=$ ", "TERM=dumb", NULL };
	struct kl_process *process;
	struct seen seen;
	char screen[8192];
	size_t length;
	size_t written;
	int error;

	process = start(argv, environment, NULL, KL_PROCESS_TERMINAL, &error);
	check(error == 0, "terminal: start");
	if (error != 0)
		return;
	length = 0;
	screen[0] = '\0';
	error = kl_process_write(process, "stty size\n", 10, &written);
	check(error == 0 && written == 10, "terminal: typed");
	check(terminal_wait(process, "24 80", screen, sizeof(screen), &length), "terminal: 24 80");
	check(kl_process_resize(process, 100, 30) == 0, "terminal: resized");
	(void)kl_process_write(process, "stty size; echo mark$((0+1)); read x\n", 37, &written);
	check(terminal_wait(process, "30 100", screen, sizeof(screen), &length), "terminal: 30 100");
	check(terminal_wait(process, "mark1\r\n", screen, sizeof(screen), &length), "terminal: mark1");
	check(terminal_mode_wait(process, KL_PROCESS_TERMINAL_ECHO | KL_PROCESS_TERMINAL_LINES), "terminal: a read echoes, by the line");
	(void)kl_process_write(process, "a\nstty -echo; echo mark$((1+1)); read y\n", 40, &written);
	check(terminal_wait(process, "mark2\r\n", screen, sizeof(screen), &length), "terminal: mark2");
	check(terminal_mode_wait(process, KL_PROCESS_TERMINAL_LINES), "terminal: a secret read: no echo, by the line");
	(void)kl_process_write(process, "b\nstty echo; echo bye; exit 4\n", 30, &written);
	memset(&seen, 0, sizeof(seen));
	seen.read_size = 4096;
	check(run(process, &seen, 5000), "terminal: ended");
	check(strstr(seen.output, "bye") != NULL || strstr(screen, "bye") != NULL, "terminal: last output read");
	check(kl_process_exit(process, &error) == KL_PROCESS_EXITED && error == 4, "terminal: exit 4");
	kl_process_close(process);
}

static void
case_hangup(void)
{
	const char *argv[] = { "sh", NULL };
	struct kl_process *process;
	struct seen seen;
	unsigned how;
	int error;

	process = start(argv, NULL, NULL, KL_PROCESS_TERMINAL, &error);
	check(error == 0, "hangup: start");
	if (error != 0)
		return;
	check(kl_process_kill(process, KL_PROCESS_HANGUP) == 0, "hangup: asked");
	memset(&seen, 0, sizeof(seen));
	check(run(process, &seen, 5000), "hangup: ended");
	how = kl_process_exit(process, &error);
	check(how == KL_PROCESS_SIGNALED && error == 1, "hangup: SIGHUP");
	kl_process_close(process);
}

static void
case_close(void)
{
	const char *argv[] = { "sh", "-c", "while :; do echo spam; done", NULL };
	struct kl_app_event event;
	struct kl_process *process;
	uint64_t start_ms;
	int found;
	int error;

	process = start(argv, NULL, NULL, KL_PROCESS_OUTPUT, &error);
	check(error == 0, "close: start");
	if (error != 0)
		return;
	(void)kl_app_dispatch(app, 200);
	kl_process_close(process);
	found = 0;
	start_ms = now_ms();
	while (now_ms() - start_ms < 500U) {
		(void)kl_app_dispatch(app, 50);
		while (kl_app_take(app, &event)) {
			if (event.kind == KL_APP_PROCESS)
				found = 1;
		}
	}
	check(!found, "close: no event after the close (the writer ends on its broken pipe)");
}

int
main(void)
{
	struct kl_app_options options;
	const char *socket_name;

	socket_name = test_server_start();
	if (socket_name == NULL)
		return 1;

	memset(&options, 0, sizeof(options));
	options.display = socket_name;
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "kl_app_open: %s\n", strerror(errno));
		return 1;
	}

	case_lines();
	case_input();
	case_flood();
	case_cut();
	case_kill();
	case_environment();
	case_missing();
	case_terminal();
	case_hangup();
	case_close();

	kl_app_close(app);
	test_server_stop();
	printf("%s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
