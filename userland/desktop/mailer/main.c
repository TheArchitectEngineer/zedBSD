/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Mail's window (WS169 p000; mailer.h): a libkeiland application with one
 * window that shows the view (view.c), its menu (File: New Message, Get
 * Mail, Quit; Message: Send, Reply, Reply All, Forward, Archive, Delete),
 * and the view's input.  Ctrl+N writes a new message, Ctrl+Q quits.  What
 * happens is logged on standard error as "MAIL" lines for the tests.
 *
 *   mailer [--width=N] [--height=N] [--timeout-s=N]
 */

#include "mailer.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fonts, the window's first size, and the longest wait for input. */
#define ML_FONT			KEILAND_DATADIR "/fonts/keiland.ttf"
#define ML_FALLBACK_FONT	KEILAND_DATADIR "/fonts/keiland-fallback.ttf"
#define ML_WIDTH		1180U
#define ML_HEIGHT		740U
#define ML_IDLE_MS		1000
#define ML_MOVING_MS		10

/* The most glass panels of a frame. */
#define ML_PANELS_MAX		4U

/* The key Q, which quits with Ctrl. */
#define ML_KEY_Q		16U

/*
 * The window's state: the application, the window and its input, the
 * frame (its pixels, size and canvas), the text and the style, the view,
 * whether a frame is due, the window changed size, or something moves, and
 * whether the glass was decided.
 */
struct ml_window {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	struct kl_canvas canvas;
	int canvas_made;
	struct kl_text text;
	struct kl_style style;
	struct ml_view view;
	int dirty;
	int resized;
	int moving;
	int glass_decided;
};

/* The window's menu. */
static const struct kl_menu_entry ml_menu[] = {
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 2U, 1U, KL_MENU_ITEM_NORMAL, "New Message", ML_ACTION_NEW, KL_MENU_ROLE_NONE, KL_MENU_CTRL, 'n' },
	{ 3U, 1U, KL_MENU_ITEM_NORMAL, "Get Mail", ML_ACTION_GET, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 4U, 1U, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 5U, 1U, KL_MENU_ITEM_NORMAL, "Quit Mail", ML_ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ 6U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Message", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 7U, 6U, KL_MENU_ITEM_NORMAL, "Send", ML_ACTION_SEND, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 8U, 6U, KL_MENU_ITEM_NORMAL, "Reply", ML_ACTION_REPLY, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 9U, 6U, KL_MENU_ITEM_NORMAL, "Reply All", ML_ACTION_REPLY_ALL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 10U, 6U, KL_MENU_ITEM_NORMAL, "Forward", ML_ACTION_FORWARD, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 11U, 6U, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 12U, 6U, KL_MENU_ITEM_NORMAL, "Archive", ML_ACTION_ARCHIVE, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 13U, 6U, KL_MENU_ITEM_NORMAL, "Delete", ML_ACTION_DELETE, KL_MENU_ROLE_NONE, 0U, 0U }
};

int main(int argc, char **argv);
static int ml_parse(int argc, char **argv, unsigned *width, unsigned *height, unsigned *timeout);
static int ml_loop(struct ml_window *mailer, unsigned timeout);
static void ml_input(struct ml_window *mailer, const struct kl_window_event *event);
static int ml_resize(struct ml_window *mailer);
static void ml_draw(struct ml_window *mailer, uint64_t now_us);
static int ml_wait(const struct ml_window *mailer, uint64_t now_us);

/*
 * Runs Mail.
 */
int
main(
	int argc,
	char **argv)
{
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	static struct ml_window mailer;
	unsigned timeout;
	unsigned width;
	unsigned height;
	int status;
	int error;

	/* The command line. */
	status = ml_parse(argc, argv, &width, &height, &timeout);
	if (status != 0) {
		fprintf(stderr, "usage: mailer [--width=N] [--height=N] [--timeout-s=N]\n");
		return 2;
	}

	/* The fonts; without them the view shows no words. */
	error = kl_text_open(&mailer.text, ML_FONT, ML_FALLBACK_FONT);
	if (error != 0)
		ml_log("FONT missing error=%d", error);

	/* The view's state. */
	error = ml_view_init(&mailer.view);
	if (error != 0) {
		ml_log("FAILED operation=view error=%d", error);
		return 1;
	}

	/* The application. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "mailer";
	mailer.app = kl_app_open(&app_options);
	if (mailer.app == NULL) {
		ml_log("FAILED operation=app error=%d", errno);
		ml_view_release(&mailer.view);
		return 1;
	}

	/* Its window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Mail";
	window_options.width = width;
	window_options.height = height;
	window_options.present = KL_PRESENT_VULKAN;
	mailer.window = kl_app_window_create(mailer.app, &window_options);
	if (mailer.window == NULL) {
		ml_log("FAILED operation=window error=%d", errno);
		kl_app_close(mailer.app);
		ml_view_release(&mailer.view);
		return 1;
	}

	/* The input of its frames. */
	mailer.ui = kl_ui_create();
	if (mailer.ui == NULL) {
		ml_log("FAILED operation=ui error=%d", errno);
		kl_app_close(mailer.app);
		ml_view_release(&mailer.view);
		return 1;
	}

	/* The menu and the style (opaque until the first frame finds whether the window can stand on glass). */
	(void)kl_window_set_menu(mailer.window, ml_menu, sizeof(ml_menu) / sizeof(ml_menu[0]));
	mailer.style.text = &mailer.text;
	mailer.style.theme = kl_theme_default();
	mailer.style.glass = 0;
	mailer.view.glass = 0;

	/* The loop until the window closes. */
	status = ml_loop(&mailer, timeout);

	/* Everything goes. */
	kl_ui_destroy(mailer.ui);
	if (mailer.canvas_made)
		kl_canvas_release(&mailer.canvas);
	free(mailer.pixels);
	kl_app_close(mailer.app);
	ml_view_release(&mailer.view);
	kl_text_close(&mailer.text);

	/* Reports how the loop ended. */
	if (status != 0)
		return 1;

	/* Succeeded: the window closed. */
	return 0;
}

/*
 * Writes a log line for the tests on standard error.
 */
void
ml_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The line. */
	va_start(arguments, format);
	fputs("MAIL ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

/*
 * Reads the command line; nonzero when it cannot be read.
 */
static int
ml_parse(
	int argc,
	char **argv,
	unsigned *width,
	unsigned *height,
	unsigned *timeout)
{
	int index;
	int same;

	/* The defaults. */
	*width = ML_WIDTH;
	*height = ML_HEIGHT;
	*timeout = 0U;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The width. */
		same = strncmp(argv[index], "--width=", 8U);
		if (same == 0) {
			*width = (unsigned)strtoul(argv[index] + 8, NULL, 10);
			continue;
		}

		/* The height. */
		same = strncmp(argv[index], "--height=", 9U);
		if (same == 0) {
			*height = (unsigned)strtoul(argv[index] + 9, NULL, 10);
			continue;
		}

		/* The timeout. */
		same = strncmp(argv[index], "--timeout-s=", 12U);
		if (same == 0) {
			*timeout = (unsigned)strtoul(argv[index] + 12, NULL, 10);
			continue;
		}

		/* An argument not known. */
		return -1;
	}

	/* A window needs a size. */
	if (*width == 0U || *height == 0U)
		return -1;

	/* Succeeded: the command line is read. */
	return 0;
}

/*
 * Runs the window until it closes, Quit or the timeout; nonzero when
 * something failed.
 */
static int
ml_loop(
	struct ml_window *mailer,
	unsigned timeout)
{
	struct kl_app_event event;
	uint64_t started;
	uint64_t now;
	int status;
	int taken;
	int wait;

	/* The first frame. */
	status = ml_resize(mailer);
	if (status != 0)
		return -1;
	ml_log("READY width=%u height=%u", mailer->width, mailer->height);

	/* Each round: the input, then a frame when something changed. */
	started = kl_clock_us();
	for (;;) {
		/* Waits for the compositor, or for the time something moves. */
		now = kl_clock_us();
		wait = ml_wait(mailer, now);
		status = kl_app_dispatch(mailer->app, wait);
		if (status != 0) {
			ml_log("DONE reason=disconnected");
			return 0;
		}

		/* The window's input and the actions of its menu. */
		for (;;) {
			taken = kl_app_take(mailer->app, &event);
			if (!taken)
				break;

			/* The desktop's appearance changed: the theme's colours are new, the window is drawn again (ws089-p017). */
			if (event.kind == KL_APP_THEME) {
				mailer->dirty = 1;
				continue;
			}

			/* Another window's event is not this one's. */
			if (event.kind != KL_APP_WINDOW || event.window != mailer->window)
				continue;

			/* An action of the menu, or input. */
			if (event.input.kind == KL_WINDOW_ACTION) {
				ml_view_action(&mailer->view, event.input.code, kl_clock_us());
				mailer->dirty = 1;
			} else {
				ml_input(mailer, &event.input);
			}
		}

		/* The end: the window closed or Quit. */
		now = kl_clock_us();
		if (mailer->view.quit) {
			ml_log("DONE reason=close");
			return 0;
		}

		/* The timeout, when one was given. */
		if (timeout != 0U && now - started >= (uint64_t)timeout * 1000000U) {
			ml_log("DONE reason=timeout");
			return 0;
		}

		/* A new size. */
		if (mailer->resized) {
			mailer->resized = 0;
			status = ml_resize(mailer);
			if (status != 0)
				return -1;
		}

		/* The view's notice gone: drawn without it. */
		if (mailer->view.notice[0] != '\0' && now >= mailer->view.notice_until) {
			mailer->view.notice[0] = '\0';
			mailer->dirty = 1;
		}

		/* A frame. */
		ml_draw(mailer, now);
	}
}

/*
 * Gives one input of the window to the view's widgets, or takes it as a
 * key of the window.
 */
static void
ml_input(
	struct ml_window *mailer,
	const struct kl_window_event *event)
{
	/* Any input may change the view. */
	mailer->dirty = 1;

	/* Each kind of input. */
	switch (event->kind) {
	case KL_WINDOW_MOTION:
		(void)kl_ui_pointer_motion(mailer->ui, event->x, event->y);
		break;
	case KL_WINDOW_LEAVE:
		(void)kl_ui_pointer_leave(mailer->ui);
		break;
	case KL_WINDOW_BUTTON:
		/* The left button presses the widgets. */
		(void)kl_ui_pointer_motion(mailer->ui, event->x, event->y);
		if (event->code == KL_BUTTON_LEFT)
			(void)kl_ui_pointer_button(mailer->ui, event->pressed, event->arrival_us);
		break;
	case KL_WINDOW_AXIS:
		(void)kl_ui_wheel(mailer->ui, event->dx, event->dy, event->arrival_us);
		break;
	case KL_WINDOW_TOUCH_DOWN:
		(void)kl_ui_touch_down(mailer->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_MOTION:
		(void)kl_ui_touch_motion(mailer->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_UP:
		(void)kl_ui_touch_up(mailer->ui, event->id, event->time_us, event->arrival_us);
		break;
	case KL_WINDOW_TOUCH_CANCEL:
		(void)kl_ui_touch_cancel(mailer->ui, event->arrival_us);
		break;
	case KL_WINDOW_KEY:
		/* Ctrl+Q quits; the other keys go to the widgets, and those no widget takes to the view. */
		if (event->pressed &&
		    (event->modifiers & KL_MOD_CTRL) != 0U &&
		    event->code == ML_KEY_Q)
			mailer->view.quit = 1;
		else
			(void)kl_ui_key(mailer->ui, event->code, event->pressed, event->modifiers);
		break;
	case KL_WINDOW_RESIZE:
		mailer->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		mailer->view.quit = 1;
		break;
	default:
		break;
	}
}

/*
 * Remakes the presenter and the canvas at the window's size; nonzero when
 * it cannot.
 */
static int
ml_resize(
	struct ml_window *mailer)
{
	uint32_t *pixels;
	int see_through;
	int status;

	/* The presenter at the window's size. */
	status = kl_window_present_resize(mailer->window, &mailer->width, &mailer->height);
	if (status != 0) {
		ml_log("FAILED operation=present error=%d", status);
		return -1;
	}

	/* zdesktop's glass, when the frames are blended by their alpha (decided at the first size). */
	if (!mailer->glass_decided) {
		mailer->glass_decided = 1;
		see_through = kl_window_see_through(mailer->window);
		if (see_through) {
			mailer->style.glass = 1;
			mailer->view.glass = 1;
		}

		/* The log line the tests read. */
		ml_log("GLASS see_through=%d", see_through);
	}

	/* A frame's pixels of its size. */
	pixels = malloc((size_t)mailer->width * (size_t)mailer->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* The canvas on them, in place of the old one. */
	if (mailer->canvas_made)
		kl_canvas_release(&mailer->canvas);
	mailer->canvas_made = 0;
	free(mailer->pixels);
	mailer->pixels = pixels;
	status = kl_canvas_init(&mailer->canvas, mailer->pixels, (size_t)mailer->width, (int)mailer->width, (int)mailer->height);
	if (status != 0)
		return -1;

	/* Succeeded: drawn again at the new size. */
	mailer->canvas_made = 1;
	mailer->style.canvas = &mailer->canvas;
	mailer->dirty = 1;
	return 0;
}

/*
 * Draws and shows a frame when something changed or moves, then takes the
 * keys no widget took.
 */
static void
ml_draw(
	struct ml_window *mailer,
	uint64_t now_us)
{
	struct kl_glass_panel panels[ML_PANELS_MAX];
	struct kl_event event;
	size_t count;
	int status;
	int error;
	int taken;

	/* Nothing changed and nothing moves: no frame. */
	if (!mailer->dirty && !mailer->moving)
		return;

	/* The view. */
	mailer->dirty = 0;
	kl_ui_begin(mailer->ui, now_us);
	ml_view_draw(&mailer->view, mailer->ui, &mailer->style, (int)mailer->width, (int)mailer->height, now_us);
	mailer->moving = kl_ui_end(mailer->ui, now_us);

	/* The glass's panels for the frame; a compositor without glass leaves the window opaque from the next one. */
	if (mailer->view.glass) {
		count = ml_view_panels(&mailer->view, (int)mailer->width, (int)mailer->height, panels, ML_PANELS_MAX);
		error = kl_window_set_glass(mailer->window, panels, count);
		if (error != 0) {
			ml_log("GLASS failed error=%d", error);
			mailer->view.glass = 0;
			mailer->style.glass = 0;
			mailer->dirty = 1;
		}
	}

	/* The frame shown. */
	status = kl_window_present(mailer->window, mailer->pixels, (size_t)mailer->width);
	if (status == EAGAIN)
		mailer->resized = 1;

	/* The keys no widget took, and the input that met no widget. */
	for (;;) {
		taken = kl_ui_take(mailer->ui, &event);
		if (!taken)
			break;

		/* A key is the view's; it draws again. */
		if (event.kind == KL_EVENT_KEY) {
			ml_view_key(&mailer->view, event.code, event.modifiers, now_us);
			mailer->dirty = 1;
		}
	}
}

/*
 * Reports how long the loop may wait for input (ms): no time while a frame
 * is due, a frame's time while something moves, until the view's notice
 * goes, or a second.
 */
static int
ml_wait(
	const struct ml_window *mailer,
	uint64_t now_us)
{
	int wait;

	/* A frame due now. */
	if (mailer->dirty)
		return 0;

	/* Something moving. */
	if (mailer->moving)
		return ML_MOVING_MS;

	/* The view's notice, or a second. */
	wait = ml_view_wait(&mailer->view, now_us);
	if (wait < 0 || wait > ML_IDLE_MS)
		wait = ML_IDLE_MS;

	/* The time to wait. */
	return wait;
}
