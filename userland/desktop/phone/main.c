/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Phone's window (WS170 p000; phone.h): a libkeiland application with one
 * window that shows the view (view.c), its menu (File: Quit; Conversation:
 * Call, Send Message), and the view's input.  Ctrl+Q quits.  What happens
 * is logged on standard error as "PHONE" lines for the tests.
 *
 *   phone [--width=N] [--height=N] [--timeout-s=N]
 */

#include "phone.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fonts, the window's first size, and the longest wait for input. */
#define PH_FONT			KEILAND_DATADIR "/fonts/keiland.ttf"
#define PH_FALLBACK_FONT	KEILAND_DATADIR "/fonts/keiland-fallback.ttf"
#define PH_WIDTH		980U
#define PH_HEIGHT		660U
#define PH_IDLE_MS		1000
#define PH_MOVING_MS		10

/* The most glass panels of a frame. */
#define PH_PANELS_MAX		4U

/* The key Q, which quits with Ctrl. */
#define PH_KEY_Q		16U

/*
 * The window's state: the application, the window and its input, the
 * frame (its pixels, size and canvas), the text and the style, the view,
 * whether a frame is due, the window changed size, or something moves, and
 * whether the glass was decided.
 */
struct ph_window {
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
	struct ph_view view;
	int dirty;
	int resized;
	int moving;
	int glass_decided;
};

/* The window's menu. */
static const struct kl_menu_entry ph_menu[] = {
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 2U, 1U, KL_MENU_ITEM_NORMAL, "Quit Phone", PH_ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ 3U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Conversation", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 4U, 3U, KL_MENU_ITEM_NORMAL, "Call", PH_ACTION_CALL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 5U, 3U, KL_MENU_ITEM_NORMAL, "Send Message", PH_ACTION_SEND, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 6U, 3U, KL_MENU_ITEM_NORMAL, "Attach File...", PH_ACTION_ATTACH, KL_MENU_ROLE_NONE, 0U, 0U }
};

int main(int argc, char **argv);
static int ph_parse(int argc, char **argv, unsigned *width, unsigned *height, unsigned *timeout);
static int ph_loop(struct ph_window *phone, unsigned timeout);
static void ph_input(struct ph_window *phone, const struct kl_window_event *event);
static int ph_resize(struct ph_window *phone);
static void ph_draw(struct ph_window *phone, uint64_t now_us);
static int ph_wait(const struct ph_window *phone, uint64_t now_us);

/*
 * Runs Phone.
 */
int
main(
	int argc,
	char **argv)
{
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	static struct ph_window phone;
	unsigned timeout;
	unsigned width;
	unsigned height;
	int status;
	int error;

	/* The command line. */
	status = ph_parse(argc, argv, &width, &height, &timeout);
	if (status != 0) {
		fprintf(stderr, "usage: phone [--width=N] [--height=N] [--timeout-s=N]\n");
		return 2;
	}

	/* The fonts; without them the view shows no words. */
	error = kl_text_open(&phone.text, PH_FONT, PH_FALLBACK_FONT);
	if (error != 0)
		ph_log("FONT missing error=%d", error);

	/* The view's state. */
	error = ph_view_init(&phone.view);
	if (error != 0) {
		ph_log("FAILED operation=view error=%d", error);
		return 1;
	}

	/* The application. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "phone";
	phone.app = kl_app_open(&app_options);
	if (phone.app == NULL) {
		ph_log("FAILED operation=app error=%d", errno);
		ph_view_release(&phone.view);
		return 1;
	}

	/* Its window, and the input of its frames. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Phone";
	window_options.width = width;
	window_options.height = height;
	window_options.present = KL_PRESENT_VULKAN;
	phone.window = kl_app_window_create(phone.app, &window_options);
	phone.ui = kl_ui_create();
	if (phone.window == NULL || phone.ui == NULL) {
		ph_log("FAILED operation=window error=%d", errno);
		kl_ui_destroy(phone.ui);
		kl_app_close(phone.app);
		ph_view_release(&phone.view);
		return 1;
	}

	/* The menu and the style (opaque until the first frame finds whether the window can stand on glass). */
	(void)kl_window_set_menu(phone.window, ph_menu, sizeof(ph_menu) / sizeof(ph_menu[0]));
	phone.style.text = &phone.text;
	phone.style.theme = kl_theme_default();
	phone.style.glass = 0;
	phone.view.glass = 0;

	/* The loop until the window closes. */
	status = ph_loop(&phone, timeout);

	/* Everything goes. */
	kl_ui_destroy(phone.ui);
	if (phone.canvas_made)
		kl_canvas_release(&phone.canvas);
	free(phone.pixels);
	kl_app_close(phone.app);
	ph_view_release(&phone.view);
	kl_text_close(&phone.text);

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
ph_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The line. */
	va_start(arguments, format);
	fputs("PHONE ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

/*
 * Reads the command line; nonzero when it cannot be read.
 */
static int
ph_parse(
	int argc,
	char **argv,
	unsigned *width,
	unsigned *height,
	unsigned *timeout)
{
	int index;
	int same;

	/* The defaults. */
	*width = PH_WIDTH;
	*height = PH_HEIGHT;
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
ph_loop(
	struct ph_window *phone,
	unsigned timeout)
{
	struct kl_app_event event;
	uint64_t started;
	uint64_t now;
	int status;
	int taken;
	int wait;

	/* The first frame. */
	status = ph_resize(phone);
	if (status != 0)
		return -1;
	ph_log("READY width=%u height=%u", phone->width, phone->height);

	/* Each round: the input, then a frame when something changed. */
	started = kl_clock_us();
	for (;;) {
		/* Waits for the compositor, or for the time something moves. */
		now = kl_clock_us();
		wait = ph_wait(phone, now);
		status = kl_app_dispatch(phone->app, wait);
		if (status != 0) {
			ph_log("DONE reason=disconnected");
			return 0;
		}

		/* The window's input and the actions of its menu. */
		for (;;) {
			taken = kl_app_take(phone->app, &event);
			if (!taken)
				break;

			/* The desktop's appearance changed: the theme's colours are new, the window is drawn again (ws089-p017). */
			if (event.kind == KL_APP_THEME) {
				phone->dirty = 1;
				continue;
			}

			/* Another window's event is not this one's. */
			if (event.kind != KL_APP_WINDOW || event.window != phone->window)
				continue;

			/* An action of the menu, or input. */
			if (event.input.kind == KL_WINDOW_ACTION) {
				ph_view_action(&phone->view, event.input.code, kl_clock_us());
				phone->dirty = 1;
			} else {
				ph_input(phone, &event.input);
			}
		}

		/* The end: the window closed or Quit. */
		now = kl_clock_us();
		if (phone->view.quit) {
			ph_log("DONE reason=close");
			return 0;
		}

		/* The timeout, when one was given. */
		if (timeout != 0U && now - started >= (uint64_t)timeout * 1000000U) {
			ph_log("DONE reason=timeout");
			return 0;
		}

		/* A new size. */
		if (phone->resized) {
			phone->resized = 0;
			status = ph_resize(phone);
			if (status != 0)
				return -1;
		}

		/* The view's notice gone: drawn without it. */
		if (phone->view.notice != NULL && now >= phone->view.notice_until) {
			phone->view.notice = NULL;
			phone->dirty = 1;
		}

		/* A frame. */
		ph_draw(phone, now);
	}
}

/*
 * Gives one input of the window to the view's widgets, or takes it as a
 * key of the window.
 */
static void
ph_input(
	struct ph_window *phone,
	const struct kl_window_event *event)
{
	/* Any input may change the view. */
	phone->dirty = 1;

	/* Each kind of input. */
	switch (event->kind) {
	case KL_WINDOW_MOTION:
		(void)kl_ui_pointer_motion(phone->ui, event->x, event->y);
		break;
	case KL_WINDOW_LEAVE:
		(void)kl_ui_pointer_leave(phone->ui);
		break;
	case KL_WINDOW_BUTTON:
		/* The left button presses the widgets. */
		(void)kl_ui_pointer_motion(phone->ui, event->x, event->y);
		if (event->code == KL_BUTTON_LEFT)
			(void)kl_ui_pointer_button(phone->ui, event->pressed, event->arrival_us);
		break;
	case KL_WINDOW_AXIS:
	case KL_WINDOW_AXIS_STOP:
		/* The wheel glides; a touch pad's fingers hold the content, and it flies on when they lift (BUG-211). */
		(void)kl_ui_axis(phone->ui, event);
		break;
	case KL_WINDOW_TOUCH_DOWN:
		(void)kl_ui_touch_down(phone->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_MOTION:
		(void)kl_ui_touch_motion(phone->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_UP:
		(void)kl_ui_touch_up(phone->ui, event->id, event->time_us, event->arrival_us);
		break;
	case KL_WINDOW_TOUCH_CANCEL:
		(void)kl_ui_touch_cancel(phone->ui, event->arrival_us);
		break;
	case KL_WINDOW_KEY:
		/* Ctrl+Q quits; the other keys go to the widgets, and those no widget takes to the view. */
		if (event->pressed &&
		    (event->modifiers & KL_MOD_CTRL) != 0U &&
		    event->code == PH_KEY_Q)
			phone->view.quit = 1;
		else
			(void)kl_ui_key(phone->ui, event->code, event->pressed, event->modifiers);
		break;
	case KL_WINDOW_TEXT_COMMIT:
	case KL_WINDOW_TEXT_PREEDIT:
	case KL_WINDOW_TEXT_DELETE:
		/* Text from an input method or the on-screen keyboard, for the field with the keyboard (BUG-203, BUG-204). */
		(void)kl_ui_text(phone->ui, event);
		break;
	case KL_WINDOW_RESIZE:
		phone->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		phone->view.quit = 1;
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
ph_resize(
	struct ph_window *phone)
{
	uint32_t *pixels;
	int see_through;
	int status;

	/* The presenter at the window's size. */
	status = kl_window_present_resize(phone->window, &phone->width, &phone->height);
	if (status != 0) {
		ph_log("FAILED operation=present error=%d", status);
		return -1;
	}

	/* zdesktop's glass, when the frames are blended by their alpha (decided at the first size). */
	if (!phone->glass_decided) {
		phone->glass_decided = 1;
		see_through = kl_window_see_through(phone->window);
		if (see_through) {
			phone->style.glass = 1;
			phone->view.glass = 1;
		}

		/* The log line the tests read. */
		ph_log("GLASS see_through=%d", see_through);
	}

	/* A frame's pixels of its size. */
	pixels = malloc((size_t)phone->width * (size_t)phone->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* The canvas on them, in place of the old one. */
	if (phone->canvas_made)
		kl_canvas_release(&phone->canvas);
	phone->canvas_made = 0;
	free(phone->pixels);
	phone->pixels = pixels;
	status = kl_canvas_init(&phone->canvas, phone->pixels, (size_t)phone->width, (int)phone->width, (int)phone->height);
	if (status != 0)
		return -1;

	/* Succeeded: drawn again at the new size. */
	phone->canvas_made = 1;
	phone->style.canvas = &phone->canvas;
	phone->dirty = 1;
	return 0;
}

/*
 * Draws and shows a frame when something changed or moves, then takes the
 * keys no widget took.
 */
static void
ph_draw(
	struct ph_window *phone,
	uint64_t now_us)
{
	struct kl_glass_panel panels[PH_PANELS_MAX];
	struct kl_event event;
	struct kl_rect caret;
	size_t count;
	int status;
	int error;
	int taken;
	int wanted;

	/* Nothing changed and nothing moves: no frame. */
	if (!phone->dirty && !phone->moving)
		return;

	/* The view. */
	phone->dirty = 0;
	kl_ui_begin(phone->ui, now_us);
	ph_view_draw(&phone->view, phone->ui, &phone->style, (int)phone->width, (int)phone->height, now_us);
	phone->moving = kl_ui_end(phone->ui, now_us);

	/*
	 * The text input is asked for while a field has the keyboard, and told
	 * where its caret is, so that an input method's candidates and the
	 * on-screen keyboard stay out of its way.
	 */
	wanted = kl_ui_text_wanted(phone->ui, &caret);
	kl_window_text_input(phone->window, wanted);
	if (wanted)
		kl_window_text_cursor(phone->window, caret.x, caret.y, caret.width, caret.height);

	/* The glass's panels for the frame; a compositor without glass leaves the window opaque from the next one. */
	if (phone->view.glass) {
		count = ph_view_panels(&phone->view, (int)phone->width, (int)phone->height, panels, PH_PANELS_MAX);
		error = kl_window_set_glass(phone->window, panels, count);
		if (error != 0) {
			ph_log("GLASS failed error=%d", error);
			phone->view.glass = 0;
			phone->style.glass = 0;
			phone->dirty = 1;
		}
	}

	/* The frame shown. */
	status = kl_window_present(phone->window, phone->pixels, (size_t)phone->width);
	if (status == EAGAIN)
		phone->resized = 1;

	/* The keys no widget took, and the input that met no widget. */
	for (;;) {
		taken = kl_ui_take(phone->ui, &event);
		if (!taken)
			break;

		/* A key is the view's; it draws again. */
		if (event.kind == KL_EVENT_KEY) {
			ph_view_key(&phone->view, event.code, event.modifiers, now_us);
			phone->dirty = 1;
		}
	}
}

/*
 * Reports how long the loop may wait for input (ms): no time while a frame
 * is due, a frame's time while something moves, until the view's notice
 * goes, or a second.
 */
static int
ph_wait(
	const struct ph_window *phone,
	uint64_t now_us)
{
	int wait;

	/* A frame due now. */
	if (phone->dirty)
		return 0;

	/* Something moving. */
	if (phone->moving)
		return PH_MOVING_MS;

	/* The view's notice, or a second. */
	wait = ph_view_wait(&phone->view, now_us);
	if (wait < 0 || wait > PH_IDLE_MS)
		wait = PH_IDLE_MS;

	/* The time to wait. */
	return wait;
}
