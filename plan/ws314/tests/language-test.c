/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A test of KL_APP_LANGUAGE and kl_app_language.  On the host (a small
 * Wayland server without Keiland's settings) the application is in English
 * and hears no change.  Under the compositor (zedBSD) the test changes the
 * desktop's language three times at once through its own kl_settings and
 * expects three KL_APP_LANGUAGE events, each with the language it changed
 * to, in order, then puts the language back.  Prints one line per case and
 * PASS or FAIL.
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

#define KEY "ui.language"

static int
take_languages(char seen[][KL_TR_NAME_MAX + 1U], int wanted, unsigned timeout_ms, struct kl_settings *settings)
{
	struct kl_app_event event;
	uint64_t start;
	int count;

	count = 0;
	start = now_ms();
	while (count < wanted && now_ms() - start < timeout_ms) {
		if (kl_app_dispatch(app, 100) != 0)
			break;
		if (settings != NULL)
			(void)kl_settings_dispatch(settings);
		while (kl_app_take(app, &event)) {
			if (event.kind != KL_APP_LANGUAGE)
				continue;
			if (count < 8)
				snprintf(seen[count], sizeof(seen[0]), "%s", event.language);
			count++;
		}
	}
	return count;
}

int
main(void)
{
	struct kl_app_options options;
	struct kl_settings *settings;
	char seen[8][KL_TR_NAME_MAX + 1U];
	const char *socket_name;
	char first[KL_TR_NAME_MAX + 1U];
	const char *other;
	int original;
	int count;
	int error;

	socket_name = test_server_start();
	if (socket_name == NULL)
		return 1;
	socket_name = strdup(socket_name);
	if (socket_name == NULL)
		return 1;
	memset(&options, 0, sizeof(options));
	options.display = socket_name;
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "kl_app_open: %s\n", strerror(errno));
		return 1;
	}
	settings = kl_settings_open(kl_app_display(app), NULL);
	check(settings != NULL, "settings: opened");
	original = kl_settings_get_int(settings, KEY, -1);
	snprintf(first, sizeof(first), "%s", kl_app_language(app));
	printf("     language=%s setting=%d\n", first, original);

	error = kl_settings_set_int(settings, KEY, original == 1 ? 0 : 1, NULL);
	if (error == ENOTSUP) {
		/* No compositor's settings: English, and no change heard. */
		check(strcmp(first, "en") == 0, "host: English without the compositor's settings");
		count = take_languages(seen, 1, 500, settings);
		check(count == 0, "host: no change heard");
	} else {
		other = original == 1 ? "en" : "ja";
		check(error == 0, "guest: the language changed");
		(void)kl_settings_set_int(settings, KEY, original == 1 ? 1 : 0, NULL);
		(void)kl_settings_set_int(settings, KEY, original == 1 ? 0 : 1, NULL);
		count = take_languages(seen, 3, 10000, settings);
		printf("     events=%d %s %s %s now=%s\n", count, count > 0 ? seen[0] : "-", count > 1 ? seen[1] : "-",
		    count > 2 ? seen[2] : "-", kl_app_language(app));
		check(count == 3, "guest: three changes, three events");
		check(count >= 3 && strcmp(seen[0], other) == 0 && strcmp(seen[1], first) == 0 && strcmp(seen[2], other) == 0,
		    "guest: each event names its own language, in order");
		check(strcmp(kl_app_language(app), other) == 0, "guest: kl_app_language is the last");
		(void)kl_settings_set_int(settings, KEY, original < 0 ? 0 : original, NULL);
		count = take_languages(seen, 1, 10000, settings);
		check(count == 1 && strcmp(seen[0], first) == 0, "guest: put back");
	}

	kl_settings_close(settings);
	kl_app_close(app);
	test_server_stop();
	printf("%s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
