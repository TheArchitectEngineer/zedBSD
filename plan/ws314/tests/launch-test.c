/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A test of kl_launch: a small Wayland server (libwayland-server, on the
 * host) announces the two globals kl_app_open needs, or the compositor's
 * (zedBSD), and the application starts programs apart from itself and
 * checks the command line, the arguments, the environment, the directory,
 * the empty input, the descriptors not passed on, the signals' default
 * actions, the errors, and that no child is left to wait for.  Prints one
 * line per case and PASS or FAIL.
 */

#include <keiland/keiland.h>

#ifdef KL_TEST_OWN_SERVER
#include <wayland-server.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

/* The directory the programs write into. */
static char folder[PATH_MAX];

/* Reads a file the program writes, waiting for it up to timeout_ms (0 when it never came). */
static int
wait_file(const char *name, char *text, size_t size, unsigned timeout_ms)
{
	char path[PATH_MAX + 64];
	uint64_t start;
	ssize_t got;
	int descriptor;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	start = now_ms();
	while (now_ms() - start < timeout_ms) {
		descriptor = open(path, O_RDONLY);
		if (descriptor >= 0) {
			got = read(descriptor, text, size - 1U);
			close(descriptor);
			if (got > 0) {
				text[got] = '\0';
				if (text[got - 1] == '\n') {
					text[got - 1] = '\0';
					return 1;
				}
			}
		}
		(void)kl_app_dispatch(app, 20);
	}
	text[0] = '\0';
	return 0;
}

static int
launch_command(const char *command, const char *const *environment, const char *directory)
{
	struct kl_launch_options options;

	memset(&options, 0, sizeof(options));
	options.command = command;
	options.environment = environment;
	options.directory = directory;
	return kl_launch(app, &options);
}

static int
launch_argv(const char *const *argv, const char *directory)
{
	struct kl_launch_options options;

	memset(&options, 0, sizeof(options));
	options.argv = argv;
	options.directory = directory;
	return kl_launch(app, &options);
}

static void
case_command(void)
{
	const char *environment[] = { "KL_LAUNCH_FOO=bar", "HOME", NULL };
	char text[256];
	int error;

	error = launch_command("echo \"$KL_LAUNCH_FOO ${HOME-unset}\" > command.txt", environment, folder);
	check(error == 0, "command: started");
	check(wait_file("command.txt", text, sizeof(text), 5000) && strcmp(text, "bar unset") == 0, "command: environment set and removed, in the directory");
}

static void
case_arguments(void)
{
	const char *argv[] = { "sh", "-c", "echo \"$PPID $1\" > arguments.txt", "sh", "two words", NULL };
	char expected[64];
	char text[256];
	int error;

	error = launch_argv(argv, folder);
	check(error == 0, "arguments: started (sh found in PATH)");
	snprintf(expected, sizeof(expected), "%ld two words", (long)getpid());
	check(wait_file("arguments.txt", text, sizeof(text), 5000), "arguments: ran");
	check(strstr(text, "two words") != NULL, "arguments: passed whole");
	check(strcmp(text, expected) != 0, "arguments: not the application's child");
}

static void
case_input(void)
{
	char text[64];
	int error;

	error = launch_command("cat > input.txt; wc -c < input.txt | tr -d ' ' > input-count.txt", NULL, folder);
	check(error == 0, "input: started");
	check(wait_file("input-count.txt", text, sizeof(text), 5000) && strcmp(text, "0") == 0, "input: empty (the null device)");
}

static void
case_descriptors(void)
{
	const char *argv[] = { "sleep", "3", NULL };
	uint64_t start;
	ssize_t got;
	char byte;
	int ends[2];
	int error;

	/* A pipe passed on to a child would keep its end open as long as sleep runs. */
	if (pipe(ends) != 0) {
		check(0, "descriptors: pipe");
		return;
	}
	error = launch_argv(argv, NULL);
	close(ends[1]);
	check(error == 0, "descriptors: started");
	start = now_ms();
	got = read(ends[0], &byte, 1);
	close(ends[0]);
	check(got == 0 && now_ms() - start < 2000U, "descriptors: not passed on");
}

static void
case_signals(void)
{
	char text[64];
	int error;

	/* The application ignores SIGINT; the program starts with its default action and dies of it. */
	(void)signal(SIGINT, SIG_IGN);
	error = launch_command("kill -INT $$; echo alive > signal.txt", NULL, folder);
	(void)signal(SIGINT, SIG_DFL);
	check(error == 0, "signals: started");
	error = launch_command("sleep 1; echo after > signal-after.txt", NULL, folder);
	check(wait_file("signal-after.txt", text, sizeof(text), 5000), "signals: a later program ran");
	check(!wait_file("signal.txt", text, sizeof(text), 10), "signals: the default action (SIGINT ended it)");
}

static void
case_errors(void)
{
	const char *missing[] = { "kl-no-such-program-xyz", NULL };
	const char *absolute[] = { "/nonexistent/program", NULL };
	const char *empty[] = { NULL };
	const char *argv[] = { "sh", NULL };
	struct kl_launch_options options;
	int error;

	error = launch_argv(missing, NULL);
	check(error == ENOENT, "errors: ENOENT in PATH");
	error = launch_argv(absolute, NULL);
	check(error == ENOENT, "errors: ENOENT for a path");
	error = launch_argv(argv, "/nonexistent/folder");
	check(error == ENOENT, "errors: ENOENT for the directory (told by the child)");
	error = launch_argv(empty, NULL);
	check(error == EINVAL, "errors: EINVAL without a program");
	error = launch_command("", NULL, NULL);
	check(error == EINVAL, "errors: EINVAL for an empty command");
	memset(&options, 0, sizeof(options));
	error = kl_launch(app, &options);
	check(error == EINVAL, "errors: EINVAL for neither");
	options.argv = argv;
	options.command = "true";
	error = kl_launch(app, &options);
	check(error == EINVAL, "errors: EINVAL for both");
	error = kl_launch(NULL, &options);
	check(error == EINVAL, "errors: EINVAL without an application");
}

static void
case_children(void)
{
	pid_t pid;

	pid = waitpid(-1, NULL, WNOHANG);
	check(pid < 0 && errno == ECHILD, "children: none left to wait for");
}

int
main(void)
{
	struct kl_app_options options;
	const char *socket_name;
	const char *base;

	/* Kept: the C library may reuse getenv's string. */
	socket_name = test_server_start();
	if (socket_name == NULL)
		return 1;
	socket_name = strdup(socket_name);
	if (socket_name == NULL)
		return 1;

	base = getenv("XDG_RUNTIME_DIR");
	if (base == NULL)
		base = "/tmp";
	snprintf(folder, sizeof(folder), "%s/launch.XXXXXX", base);
	if (mkdtemp(folder) == NULL) {
		fprintf(stderr, "mkdtemp: %s\n", strerror(errno));
		return 1;
	}

	memset(&options, 0, sizeof(options));
	options.display = socket_name;
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "kl_app_open: %s (display=%s)\n", strerror(errno), socket_name);
		return 1;
	}

	case_command();
	case_arguments();
	case_input();
	case_descriptors();
	case_signals();
	case_errors();
	case_children();

	kl_app_close(app);
	test_server_stop();
	printf("%s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
