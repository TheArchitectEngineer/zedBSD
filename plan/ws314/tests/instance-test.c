/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test of kl_instance_bind: a small Wayland server (libwayland-server)
 * announces the two globals kl_app_open needs; the application becomes the
 * one copy of a name, binds it, and later starts (kl_instance_open of the
 * same name, from another thread) hand requests over that arrive as
 * KL_APP_INSTANCE events.  The application then closes before the copy, as
 * Settings does.  Prints one line per case and PASS or FAIL.
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

static void *
starter_run(void *argument)
{
	const char *const *requests;
	struct kl_instance *other;
	unsigned index;
	int error;

	requests = argument;
	for (index = 0; requests[index] != NULL; index++) {
		error = kl_instance_open("ws314-instance-test", requests[index], &other);
		if (error != 0 || other != NULL)
			fprintf(stderr, "start %u: error=%d other=%p\n", index, error, (void *)other);
	}
	return NULL;
}

int
main(void)
{
	static const char *const requests[] = { "page-a", "page-b", "page-c", NULL };
	struct kl_app_options options;
	struct kl_app_event event;
	struct kl_instance *instance;
	pthread_t starter;
	const char *socket_name;
	char request[KL_INSTANCE_REQUEST_MAX];
	char token[KL_ACTIVATION_TOKEN_MAX];
	char seen[256];
	unsigned events;
	uint64_t start;
	int error;

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

	error = kl_instance_open("ws314-instance-test", "first", &instance);
	check(error == 0 && instance != NULL, "open: the one copy");
	if (instance == NULL)
		return 1;
	check(kl_instance_bind(instance, app) == 0, "bind");
	check(kl_instance_bind(instance, app) == EINVAL, "bind twice: EINVAL");

	pthread_create(&starter, NULL, starter_run, (void *)requests);
	seen[0] = '\0';
	events = 0;
	start = now_ms();
	while (now_ms() - start < 5000U && strlen(seen) < strlen("page-a page-b page-c ")) {
		(void)kl_app_dispatch(app, 500);
		while (kl_app_take(app, &event)) {
			if (event.kind != KL_APP_INSTANCE)
				continue;
			events++;
			check(event.instance == instance, "event: the copy");
			while (kl_instance_take(event.instance, request, sizeof(request), token, sizeof(token))) {
				strcat(seen, request);
				strcat(seen, " ");
			}
		}
	}
	pthread_join(starter, NULL);
	check(strcmp(seen, "page-a page-b page-c ") == 0, "three requests in order");
	check(events >= 1U && events <= 3U, "one event per batch");
	check(!kl_instance_take(instance, request, sizeof(request), token, sizeof(token)), "none left");

	/* The application first (its close unbinds the copy), then the copy, as Settings does. */
	kl_app_close(app);
	kl_instance_close(instance);
	check(1, "closed: application, then copy");

	test_server_stop();
	printf("%s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
