/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The application, part of <keiland/keiland.h>.
 */

#ifndef KEILAND_APP_H
#define KEILAND_APP_H

#include <keiland/keiland_window.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * KL_VERSION 26: the application.  One kl_app is one connection to the
 * compositor: its globals are learnt with one roundtrip when it opens, and
 * its windows, menus, titlebars and glass bind what they need from that one
 * registry without searching again.  Its windows' input, the actions chosen
 * in their menus and controls, and the descriptors it watches arrive as one
 * queue of kl_app_event values, in the order they happened: the application
 * waits with kl_app_dispatch and takes them with kl_app_take (a window of an
 * application has no queue of its own; kl_window_take finds nothing).  A held
 * key repeats by itself within kl_app_dispatch, after the events read with it
 * (a release read in the same dispatch stops it first).  The desktop's system
 * (network, sound, power, kl_system) is the application's too.  A window made
 * by kl_window_open is still a connection of its own.
 *
 * Every call is made from the one thread that opened the application, but
 * kl_app_post and kl_window_post (KL_VERSION 97), which the program's other
 * threads call to hand the application's thread an event of their own.
 */
struct kl_app;
struct kl_system;
struct kl_glass_panel;
struct kl_process;
struct kl_instance;

/* The most descriptors an application watches (16 before KL_VERSION 51). */
#define KL_APP_FDS_MAX		64U

/* What a watched descriptor is waited for, and what it became (a hang-up or an error is always told). */
#define KL_APP_FD_READ		1U
#define KL_APP_FD_WRITE	2U
#define KL_APP_FD_HANGUP	4U

/* The kinds of an application's event. */
#define KL_APP_WINDOW		1U
#define KL_APP_FD		2U
#define KL_APP_THEME		3U
#define KL_APP_POST		4U
#define KL_APP_PROCESS		5U
#define KL_APP_INSTANCE	6U

/*
 * What an application is opened with.  Either pointer may be NULL:
 * display (the WAYLAND_DISPLAY one) and application (the app_id its
 * windows get when their options name none).
 */
struct kl_app_options {
	const char *display;
	const char *application;
};

/*
 * One event of an application: its kind; for KL_APP_WINDOW the window and
 * its input (as kl_window_take gave it; KL_WINDOW_ACTION for an action
 * chosen), for KL_APP_FD the descriptor and what it became (KL_APP_FD_*);
 * KL_APP_THEME (KL_VERSION 35) says the desktop's appearance changed and
 * kl_theme_default's colours with it, for the application to draw its
 * windows again (kl_appearance_get(NULL) tells which); KL_APP_POST
 * (KL_VERSION 97) is the program's own, with the code and the data
 * kl_app_post was given (or kl_window_post's, window NULL, when the window
 * closed before it came); KL_APP_PROCESS (KL_VERSION 98) is a process's
 * news (<keiland/keiland_process.h>): the process, and in ready what it
 * tells (KL_PROCESS_READY_*); KL_APP_INSTANCE (KL_VERSION 99) says later
 * starts of the program handed requests to its copy that runs (instance,
 * bound with kl_instance_bind): the program takes them with
 * kl_instance_take until none is left.
 */
struct kl_app_event {
	unsigned kind;
	struct kl_window *window;
	struct kl_window_event input;
	int fd;
	unsigned ready;
	uint32_t code;
	void *data;
	struct kl_process *process;
	struct kl_instance *instance;
};

/*
 * Opens an application: connects, learns the globals and binds what every
 * window shares.  Returns NULL with errno set: a connection's error,
 * EOPNOTSUPP (no compositor or shell), EPROTO, ENOMEM.
 */
struct kl_app *kl_app_open(const struct kl_app_options *options);

/*
 * Closes the windows still open, the system, and the connection.
 */
void kl_app_close(struct kl_app *app);

/*
 * Waits up to a timeout (milliseconds, -1 for ever; no wait while events
 * are queued or a repeat is due) for the compositor or a watched
 * descriptor, and queues what happened.  Returns 0, or -1 when the
 * connection is broken.
 */
int kl_app_dispatch(struct kl_app *app, int timeout_ms);

/*
 * Watches a descriptor for KL_APP_FD_READ and KL_APP_FD_WRITE (0 stops
 * watching it): while it is ready, each dispatch queues a KL_APP_FD event.
 * Returns 0, EINVAL, or ENOSPC past KL_APP_FDS_MAX.
 */
int kl_app_watch_fd(struct kl_app *app, int fd, unsigned events);

/*
 * KL_VERSION 97: posts an event of the program's own (KL_APP_POST with code
 * and data) to the application, from any thread.  The application's own
 * thread queues it at once, after the events queued so far; another
 * thread's post wakes kl_app_dispatch and is queued there, after the
 * compositor's events read with it.  The posts of one thread arrive in the
 * order they were made, and none is dropped: what the queue has no room for
 * waits for the next dispatch.  The data goes to the one who takes the
 * event.  The program ends the threads that post before it closes the
 * application (the posts not taken by then are dropped, their data with
 * them).  Returns 0, EINVAL, or ENOMEM.
 */
int kl_app_post(struct kl_app *app, uint32_t code, void *data);

/*
 * Takes the oldest event.  Returns 1 with it in *event, 0 when none is queued.
 */
int kl_app_take(struct kl_app *app, struct kl_app_event *event);

/*
 * The application's system, opened the first time it is asked for; NULL
 * with errno set when the compositor has none (kl_system_open).
 */
struct kl_system *kl_app_system(struct kl_app *app);

/*
 * KL_VERSION 49: posts a notification of the application (its application ID
 * as the name shown), without waiting for its number.  Returns 0 when asked,
 * ENOTSUP when the compositor takes none, or EINVAL.
 */
int kl_app_notify(struct kl_app *app, const char *title, const char *body);

/* KL_VERSION 79: kl_app_notify with what the lock screen shows of it (kl_system_notify_lock). */
int kl_app_notify_lock(struct kl_app *app, const char *title, const char *body, const char *lock_text);

/* The application's connection, for libkeiland's other objects. */
struct wl_display *kl_app_display(const struct kl_app *app);

/*
 * Makes a window of the application (options->display is not used; a
 * NULL application takes the application's).  It is configured when this
 * returns, as kl_window_open's is.  Returns NULL with errno set as
 * kl_window_open does.
 */
struct kl_window *kl_app_window_create(struct kl_app *app, const struct kl_window_options *options);

#ifdef __cplusplus
}
#endif

#endif
