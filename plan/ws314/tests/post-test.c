/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test of kl_app_post: a small Wayland server (libwayland-server)
 * announces the two globals kl_app_open needs, and the application takes
 * the posts of several threads and of its own thread.  It checks that
 * every post arrives once, that each thread's posts keep their order, that
 * more posts than the queue holds wait instead of being dropped, and that a
 * dispatch waiting for ever wakes for a post.
 */

#include <keiland/keiland.h>

#include <wayland-server.h>

#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define THREADS		4U
#define POSTS		5000U
#define OWN_POSTS	1000U
#define OWN_CODE	99U

struct poster {
	struct kl_app *app;
	unsigned index;
	pthread_t thread;
};

static struct wl_display *server;
static _Atomic int server_stop;

/* An interface by name only: the client binds nothing of it. */
static const struct wl_interface fake_compositor = { "wl_compositor", 4, 0, NULL, 0, NULL };
static const struct wl_interface fake_shell = { "xdg_wm_base", 1, 0, NULL, 0, NULL };

static void
bind_nothing(
	struct wl_client *client,
	void *data,
	uint32_t version,
	uint32_t id)
{
	(void)client;
	(void)data;
	(void)version;
	(void)id;
}

static void *
server_run(
	void *argument)
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

static void *
poster_run(
	void *argument)
{
	struct poster *poster;
	uintptr_t sequence;
	int error;

	poster = argument;
	for (sequence = 0; sequence < POSTS; sequence++) {
		error = kl_app_post(poster->app, poster->index, (void *)sequence);
		if (error != 0) {
			fprintf(stderr, "post failed: %d\n", error);
			exit(1);
		}
		if ((sequence % 997U) == 0U)
			usleep(1000);
	}
	return NULL;
}

static uint64_t
now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

int
main(void)
{
	struct poster posters[THREADS];
	uintptr_t next[THREADS];
	struct kl_app_options options;
	struct kl_app_event event;
	struct kl_app *app;
	pthread_t server_thread;
	const char *socket_name;
	uintptr_t own_next;
	uintptr_t sequence;
	unsigned total;
	unsigned index;
	unsigned in_queue;
	uint64_t start;
	int status;

	/* The server and its two globals. */
	server = wl_display_create();
	socket_name = wl_display_add_socket_auto(server);
	if (socket_name == NULL) {
		fprintf(stderr, "no socket\n");
		return 1;
	}
	wl_global_create(server, &fake_compositor, 4, NULL, bind_nothing);
	wl_global_create(server, &fake_shell, 1, NULL, bind_nothing);
	pthread_create(&server_thread, NULL, server_run, NULL);

	/* The application. */
	memset(&options, 0, sizeof(options));
	options.display = socket_name;
	options.application = "post-test";
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "kl_app_open: %s\n", strerror(errno));
		return 1;
	}

	/* The own thread's posts first: more than the queue holds (512), none taken yet. */
	for (sequence = 0; sequence < OWN_POSTS; sequence++) {
		status = kl_app_post(app, OWN_CODE, (void *)sequence);
		if (status != 0) {
			fprintf(stderr, "own post: %d\n", status);
			return 1;
		}
	}

	/* The threads. */
	for (index = 0; index < THREADS; index++) {
		posters[index].app = app;
		posters[index].index = index;
		next[index] = 0;
		pthread_create(&posters[index].thread, NULL, poster_run, &posters[index]);
	}

	/* Takes until every post came, dispatching for ever between (a post must wake it). */
	total = 0;
	own_next = 0;
	start = now_ms();
	while (total < THREADS * POSTS + OWN_POSTS) {
		if (now_ms() - start > 20000U) {
			fprintf(stderr, "FAIL timeout: %u of %u\n", total, THREADS * POSTS + OWN_POSTS);
			return 1;
		}
		in_queue = 0;
		while (kl_app_take(app, &event)) {
			if (event.kind != KL_APP_POST)
				continue;
			in_queue++;
			if (event.code == OWN_CODE) {
				if ((uintptr_t)event.data != own_next) {
					fprintf(stderr, "FAIL own order: %lu after %lu\n", (unsigned long)(uintptr_t)event.data, (unsigned long)own_next);
					return 1;
				}
				own_next++;
			} else if (event.code < THREADS) {
				if ((uintptr_t)event.data != next[event.code]) {
					fprintf(stderr, "FAIL order thread %u: %lu, expected %lu\n", event.code, (unsigned long)(uintptr_t)event.data, (unsigned long)next[event.code]);
					return 1;
				}
				next[event.code]++;
			} else {
				fprintf(stderr, "FAIL code %u\n", event.code);
				return 1;
			}
			total++;
		}
		if (in_queue > 512U) {
			fprintf(stderr, "FAIL queue held %u\n", in_queue);
			return 1;
		}
		if (total == THREADS * POSTS + OWN_POSTS)
			break;
		status = kl_app_dispatch(app, -1);
		if (status != 0) {
			fprintf(stderr, "FAIL dispatch\n");
			return 1;
		}
	}

	for (index = 0; index < THREADS; index++)
		pthread_join(posters[index].thread, NULL);

	/* Nothing more. */
	status = kl_app_dispatch(app, 0);
	if (status != 0 || kl_app_take(app, &event)) {
		fprintf(stderr, "FAIL extra event\n");
		return 1;
	}

	kl_app_close(app);
	server_stop = 1;
	pthread_join(server_thread, NULL);
	printf("PASS posts=%u threads=%u own=%u ms=%lu\n", total, THREADS, OWN_POSTS, (unsigned long)(now_ms() - start));
	return 0;
}
