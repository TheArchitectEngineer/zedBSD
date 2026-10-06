/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's appearance, light or dark (ws089-p017).
 *
 * The compositor's kl_theme_v1 tells the appearance when it is bound
 * and again whenever the user changes it.  A watch binds it on a queue of
 * the library's own, takes the first appearance with a roundtrip of that
 * queue (no event of the program's runs in it), and then moves to the
 * display's default queue, where the program's dispatch runs its changes.
 *
 * The appearance told last on any watch is the program's: the theme
 * kl_theme_default hands out is made its (theme.c).  Each watch keeps its
 * own too, so that each is called when its own changes.
 */

#include <keiland.h>

#include "ui/internal.h"

#include <wayland-client.h>
#include "userland/desktop/libwayland/zed-theme-v1-client-protocol.h"

#include <errno.h>
#include <stdlib.h>

/* One watch: the compositor's object, the watch's appearance, and whom to call. */
struct kl_appearance {
	struct kl_theme_v1 *theme;
	unsigned appearance;
	kl_appearance_fn changed;
	void *data;
	int opening;
};

/* The program's appearance, told last on any watch. */
static unsigned appearance_program;

static void appearance_told(void *data, struct kl_theme_v1 *theme, uint32_t appearance);

/* The object's callbacks. */
static const struct kl_theme_v1_listener appearance_listener = {
	appearance_told
};

/*
 * Watches the appearance on a display (keiland.h): binds the global on a
 * queue of the library's own, learns the appearance, and moves the object
 * to the default queue.
 */
int
kl_appearance_open(
	struct wl_display *display,
	kl_appearance_fn changed,
	void *data,
	struct kl_appearance **appearance)
{
	struct keiui_global_search search;
	struct kl_appearance *watch;
	struct wl_event_queue *queue;
	struct wl_registry *wrapper;
	int status;
	int error;

	/* Nothing yet. */
	*appearance = NULL;
	if (display == NULL)
		return EINVAL;

	/* The global. */
	error = keiui_global_find(&search, display, "kl_theme_v1");
	if (error != 0) {
		keiui_global_end(&search);
		return error;
	}

	/* A compositor without the global has the light appearance only. */
	if (search.name == 0U || search.registry == NULL) {
		keiui_global_end(&search);
		return ENOTSUP;
	}

	/* The watch. */
	watch = calloc(1, sizeof(*watch));
	if (watch == NULL) {
		keiui_global_end(&search);
		return ENOMEM;
	}

	/* Whom the watch calls. */
	watch->changed = changed;
	watch->data = data;

	/* The library's queue. */
	queue = wl_display_create_queue(display);
	if (queue == NULL) {
		keiui_global_end(&search);
		free(watch);
		return ENOMEM;
	}

	/* The registry as seen from the queue. */
	wrapper = wl_proxy_create_wrapper(search.registry);
	if (wrapper == NULL) {
		keiui_global_end(&search);
		wl_event_queue_destroy(queue);
		free(watch);
		return ENOMEM;
	}

	/* What the wrapper binds lives on the library's queue. */
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

	/* The binding, on the library's queue. */
	watch->theme = wl_registry_bind(wrapper, search.name, &kl_theme_v1_interface, 1U);
	wl_proxy_wrapper_destroy(wrapper);
	keiui_global_end(&search);
	if (watch->theme == NULL) {
		wl_event_queue_destroy(queue);
		free(watch);
		return ENOMEM;
	}

	/* The appearance told on the binding, without calling the program. */
	status = kl_theme_v1_add_listener(watch->theme, &appearance_listener, watch);
	watch->opening = 1;
	if (status == 0)
		status = wl_display_roundtrip_queue(display, queue);
	watch->opening = 0;

	/* From now on the program's dispatch runs the changes. */
	wl_proxy_set_queue((struct wl_proxy *)watch->theme, NULL);
	wl_event_queue_destroy(queue);
	if (status < 0) {
		kl_appearance_close(watch);
		return EPROTO;
	}

	/* Succeeded. */
	*appearance = watch;
	return 0;
}

/*
 * Reports a watch's appearance, or the program's for NULL (keiland.h).
 */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* The program's. */
	if (appearance == NULL)
		return appearance_program;

	/* The watch's. */
	return appearance->appearance;
}

/*
 * Stops watching (keiland.h).
 */
void
kl_appearance_close(
	struct kl_appearance *appearance)
{
	/* No watch, nothing to stop. */
	if (appearance == NULL)
		return;

	/* The compositor's object, then the watch. */
	if (appearance->theme != NULL)
		kl_theme_v1_destroy(appearance->theme);
	free(appearance);
}

/*
 * Takes an appearance the compositor told: the watch's and the program's
 * become it (an unknown value is light), the theme handed out follows,
 * and the program is called when the watch's changed (not while the watch
 * opens).
 */
static void
appearance_told(
	void *data,
	struct kl_theme_v1 *theme,
	uint32_t appearance)
{
	struct kl_appearance *watch;
	unsigned told;

	/* The appearance; one this library does not know is light. */
	(void)theme;
	watch = data;
	told = KL_APPEARANCE_LIGHT;
	if (appearance == KL_THEME_V1_APPEARANCE_DARK)
		told = KL_APPEARANCE_DARK;

	/* The program's, and the theme it is handed. */
	if (told != appearance_program) {
		appearance_program = told;
		keiui_theme_set(told);
	}

	/* The watch's; the program hears of a change. */
	if (told == watch->appearance)
		return;
	watch->appearance = told;
	if (watch->changed != NULL && !watch->opening)
		watch->changed(watch->data, told);
}
