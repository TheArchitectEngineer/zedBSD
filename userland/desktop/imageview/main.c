/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Image Viewer (ws091): PNG, JPEG and GIF images in a Wayland window,
 * decoded on the CPU and drawn with Vulkan.
 *
 *   imageview [--display=NAME] [--font=PATH] [--width=N] [--height=N]
 *             [--fullscreen] [--timeout-s=N] [FILE|FOLDER]
 *
 * The file is opened from the command line (with the other images of its
 * folder to go through), or with File > Open (Ctrl+O).  The outcome is
 * one line on standard error: IMAGEVIEW DONE with the reason, or
 * IMAGEVIEW FAILED naming what failed; IMAGEVIEW READY says the first
 * frame is shown, and IMAGEVIEW SHOW each image shown.
 */

#include "window.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The font used unless told otherwise. */
#define MAIN_FONT		"/usr/share/fonts/keiland.ttf"

/* How many frames in a row may find the swapchain out of date before the program gives up. */
#define MAIN_STALE_LIMIT	8U

/* The longest the loop sleeps when nothing is due, in milliseconds. */
#define MAIN_IDLE_MS		1000

/* The application's identity in the compositor and the recent files. */
#define MAIN_APPLICATION	"imageview"

/* The ground: clear over the glass, black when fullscreen, a light slate when the window is opaque (0xAARRGGBB). */
#define MAIN_GROUND_GLASS	0x00000000U
#define MAIN_GROUND_FULLSCREEN	0xff000000U
#define MAIN_GROUND_OPAQUE	0xffe8ecf1U

/*
 * What the command line asked for.
 */
struct main_options {
	const char *display;
	const char *font;
	const char *file;
	unsigned width;
	unsigned height;
	unsigned timeout;
	int fullscreen;
};

/*
 * The program's parts, for the whole run.  They are file-scope because
 * the window's input queue and the viewer are too large for the stack.
 *
 * The window: the Wayland connection and surface, and the input queue,
 * from the start of the run to its end.
 */
static struct iv_window main_window;

/* The presenter: Vulkan's swapchain over the window, made after it and closed before it. */
static struct iv_present main_present;

/* The viewer: the images and the view, made once the swapchain's size is known. */
static struct iv_app main_app;

/* The font the canvas's words are drawn in, open for the whole run (without it the canvas has no words). */
static struct iv_text main_text;

/*
 * The window's menus in the compositor, opened with the window and closed
 * before it; absent with a compositor without them.
 */
static struct iv_menu main_menu;

/* The window's titlebar controls in the compositor, with the same life as the menus. */
static struct iv_titlebar main_titlebar;

/* The window's glass in the compositor, with the same life (absent without it, or with an opaque swapchain). */
static struct iv_glass main_glass;

/* The touch screen's gestures and scroller, made with the viewer (without them fingers do nothing). */
static struct iv_touch main_touch;

/*
 * The canvas's memory: ordinary memory the size of the swapchain, remade
 * (and the canvas with it) when the window changes size.
 */
static uint32_t *main_pixels;

/* The canvas over main_pixels, which the viewer draws its words and cards into. */
static struct iv_canvas main_canvas;

/* The frame of an animated image last written to the presenter (it writes a new one when the viewer's serial moves on). */
static unsigned main_frame_serial;

static int main_parse(int argc, char **argv, struct main_options *options);
static const char *main_value(const char *argument, const char *name);
static int main_number(const char *text, unsigned maximum, unsigned *value);
static int main_loop(const struct main_options *options);
static int main_frame(void);
static int main_canvas_make(void);
static void main_state(struct iv_state *state);
static void main_opened(void);
static void main_fullscreen(void);
static int main_image(void);

/*
 * Runs Image Viewer.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	struct iv_state state;
	VkResult result;
	int status;
	int error;
	int glass;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: imageview [--display=NAME] [--font=PATH] [--width=N] [--height=N] [--fullscreen] [--timeout-s=N] [FILE|FOLDER]\n");
		return 2;
	}

	/* The font; without it the viewer shows the images and no words of its own. */
	error = iv_text_open(&main_text, options.font);
	if (error != 0)
		iv_log("FONT missing path=%s error=%d", options.font, error);

	/* The window. */
	status = iv_window_open(&main_window, options.display, options.width, options.height, "Image Viewer", MAIN_APPLICATION);
	if (status != 0) {
		fprintf(stderr, "IMAGEVIEW FAILED operation=window error=%d\n", errno);
		iv_text_close(&main_text);
		return 1;
	}

	/* The presenter. */
	result = iv_present_open(&main_present, &main_window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "IMAGEVIEW FAILED operation=%s result=%d\n", main_present.operation, (int)result);
		iv_present_close(&main_present);
		iv_window_close(&main_window);
		iv_text_close(&main_text);
		return 1;
	}

	/* The viewer at the swapchain's size, knowing how large a texture may be and whether the window is glass. */
	iv_app_init(&main_app, &main_text, (int)main_present.extent.width, (int)main_present.extent.height);
	main_app.max_dimension = main_present.max_dimension;
	glass = iv_glass_open(&main_glass, &main_window, &main_present);
	main_app.glass = glass;

	/* The file given on the command line. */
	if (options.file != NULL)
		(void)iv_app_open(&main_app, options.file);

	/* The touch screen; without memory for it the fingers do nothing. */
	error = iv_touch_open(&main_touch);
	if (error != 0)
		iv_log("TOUCH failed errno=%d", error);

	/* The menus and the titlebar; a window without them goes on with its keys. */
	main_state(&state);
	error = iv_menu_open(&main_menu, &main_window, &state);
	if (error != 0) {
		iv_log("MENU failed errno=%d", error);
		iv_menu_close(&main_menu);
	}

	/* The titlebar's controls. */
	error = iv_titlebar_open(&main_titlebar, &main_window, &state);
	if (error != 0) {
		iv_log("TITLEBAR failed errno=%d", error);
		iv_titlebar_close(&main_titlebar);
	}

	/* Fullscreen from the start, when asked. */
	if (options.fullscreen)
		iv_window_fullscreen(&main_window, 1);

	/* The loop, until the window closes. */
	status = main_loop(&options);

	/* Everything goes, the titlebar, the menus, the glass and the viewer before the window they belong to. */
	iv_titlebar_close(&main_titlebar);
	iv_menu_close(&main_menu);
	iv_glass_close(&main_glass);
	iv_touch_close(&main_touch);
	iv_app_release(&main_app);
	free(main_pixels);
	iv_present_close(&main_present);
	iv_window_close(&main_window);
	iv_text_close(&main_text);

	/* Reports how the run ended. */
	if (status != 0)
		return 1;

	/* Succeeded: the window was closed. */
	return 0;
}

/* Reads the command line into the options; returns nonzero for a malformed one. */
static int
main_parse(
	int argc,
	char **argv,
	struct main_options *options)
{
	const char *value;
	int status;
	int index;
	int match;

	/* The defaults. */
	memset(options, 0, sizeof(*options));
	options->font = MAIN_FONT;
	options->width = IV_WIDTH;
	options->height = IV_HEIGHT;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The compositor's display. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->display = value;
			continue;
		}

		/* The font of the viewer's own words. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->font = value;
			continue;
		}

		/* The window's width. */
		value = main_value(argv[index], "--width=");
		if (value != NULL) {
			status = main_number(value, 8192U, &options->width);
			if (status != 0)
				return status;
			continue;
		}

		/* The window's height. */
		value = main_value(argv[index], "--height=");
		if (value != NULL) {
			status = main_number(value, 8192U, &options->height);
			if (status != 0)
				return status;
			continue;
		}

		/* Fullscreen from the start. */
		match = strcmp(argv[index], "--fullscreen");
		if (match == 0) {
			options->fullscreen = 1;
			continue;
		}

		/* How long the program runs at most (0 for ever). */
		value = main_value(argv[index], "--timeout-s=");
		if (value != NULL) {
			status = main_number(value, 86400U, &options->timeout);
			if (status != 0)
				return status;
			continue;
		}

		/* An unknown option refuses the command line. */
		if (argv[index][0] == '-')
			return -1;

		/* The file to open, once. */
		if (options->file != NULL)
			return -1;
		options->file = argv[index];
	}

	/* A window has some size. */
	if (options->width < 320U || options->height < 240U)
		return -1;

	/* Succeeded: the options are read. */
	return 0;
}

/* Returns what follows an option's name in an argument, or NULL when the argument is another option. */
static const char *
main_value(
	const char *argument,
	const char *name)
{
	size_t length;
	int match;

	/* The name must start the argument. */
	length = strlen(name);
	match = strncmp(argument, name, length);
	if (match != 0)
		return NULL;

	/* Reports the value after it. */
	return argument + length;
}

/* Reads a decimal number no larger than a maximum; nonzero for a malformed one. */
static int
main_number(
	const char *text,
	unsigned maximum,
	unsigned *value)
{
	unsigned long number;
	char *end;

	/* The digits, all of them. */
	errno = 0;
	number = strtoul(text, &end, 10);
	if (errno != 0 ||
	    end == text ||
	    *end != '\0' ||
	    number > maximum)
		return -1;

	/* Succeeded: the number. */
	*value = (unsigned)number;
	return 0;
}

/* Runs the window until it closes (or the timeout passes); returns nonzero when something failed. */
static int
main_loop(
	const struct main_options *options)
{
	struct iv_touch_event touch;
	struct iv_event event;
	struct iv_state state;
	uint64_t started;
	uint64_t now;
	int prefetched;
	int taken;
	int status;
	int timeout;
	int due;

	/* The first frame's canvas. */
	status = main_canvas_make();
	if (status != 0) {
		fprintf(stderr, "IMAGEVIEW FAILED operation=canvas\n");
		return -1;
	}

	/* The image given on the command line, and the first frame. */
	main_opened();
	status = main_frame();
	if (status != 0)
		return -1;
	iv_log("READY width=%u height=%u glass=%d images=%lu", main_present.extent.width, main_present.extent.height, main_app.glass,
	    (unsigned long)main_app.folder.count);

	/* Each round: input, time, and a frame when something changed. */
	started = iv_clock();
	for (;;) {
		/* Waits for the compositor, or until something is due. */
		now = iv_clock();
		timeout = MAIN_IDLE_MS;
		due = iv_app_tick(&main_app, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = iv_window_repeat_wait(&main_window, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = iv_touch_tick(&main_touch, &main_app, iv_touch_clock());
		if (due >= 0 && due < timeout)
			timeout = due;
		if (main_app.dirty)
			timeout = 0;

		/* With time to spare, a neighbouring image is decoded ahead, one a round. */
		if (timeout > 0) {
			prefetched = iv_app_prefetch(&main_app);
			if (prefetched)
				timeout = 0;
		}

		/* Waits; a lost connection ends the run. */
		status = iv_window_dispatch(&main_window, timeout);
		if (status != 0) {
			iv_log("DONE reason=disconnected");
			return 0;
		}

		/* Every input queued (the menus' and the titlebar's choices among them). */
		now = iv_clock();
		main_app.now = now;
		(void)iv_window_repeat(&main_window, now);
		for (;;) {
			taken = iv_window_take(&main_window, &event);
			if (taken == 0)
				break;
			iv_app_event(&main_app, &event);
		}

		/* Every touch queued. */
		for (;;) {
			taken = iv_window_take_touch(&main_window, &touch);
			if (taken == 0)
				break;
			iv_touch_event(&main_touch, &main_app, &touch);
		}

		/* An image opened: its title and the recent files; the full screen asked for or given. */
		main_opened();
		main_fullscreen();

		/* Time passes for the viewer and the fingers; the menus and the titlebar show its state. */
		(void)iv_app_tick(&main_app, now);
		(void)iv_touch_tick(&main_touch, &main_app, iv_touch_clock());
		main_state(&state);
		iv_menu_refresh(&main_menu, &state);
		iv_titlebar_refresh(&main_titlebar, &state);

		/* A context menu asked for (a right press or a long press). */
		if (main_app.want_context) {
			main_app.want_context = 0;
			iv_menu_context(&main_menu, &state, main_app.context_x, main_app.context_y);
		}

		/* The close button, Quit, or Close on an empty window end the run. */
		if (main_window.closed != 0 || main_app.want_close != 0) {
			iv_log("DONE reason=close");
			return 0;
		}

		/* So does the timeout, when one was given. */
		if (options->timeout != 0U && now - started >= (uint64_t)options->timeout * 1000U) {
			iv_log("DONE reason=timeout");
			return 0;
		}

		/* A new size: a new swapchain and canvas, and a frame. */
		if (main_window.resized != 0) {
			main_window.resized = 0;
			status = iv_present_resize(&main_present, main_window.width, main_window.height);
			if (status != VK_SUCCESS) {
				fprintf(stderr, "IMAGEVIEW FAILED operation=%s result=%d\n", main_present.operation, status);
				return -1;
			}

			/* A canvas of the new size. */
			status = main_canvas_make();
			if (status != 0)
				return -1;
			iv_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
		}

		/* A frame when something changed. */
		if (main_app.dirty != 0) {
			status = main_frame();
			if (status != 0)
				return -1;
		}
	}
}

/* Draws and shows a frame, remaking the swapchain when it is out of date; nonzero when it cannot be shown. */
static int
main_frame(void)
{
	struct iv_quad quad;
	uint32_t ground;
	VkResult result;
	unsigned stale;
	int canvas_changed;
	int status;

	/* Tries until the frame is shown, remaking a stale swapchain a few times. */
	for (stale = 0; stale < MAIN_STALE_LIMIT; stale++) {
		/* The image's textures, when the image changed. */
		status = main_image();
		if (status != 0)
			return -1;

		/* The canvas, when its words or cards changed; the glass's panels follow it. */
		canvas_changed = 0;
		if (main_app.ui_dirty) {
			iv_draw(&main_app, &main_canvas);
			canvas_changed = 1;
		}

		/* The glass's panels follow the canvas. */
		iv_glass_update(&main_glass, &main_app);

		/* The ground under everything. */
		ground = MAIN_GROUND_OPAQUE;
		if (main_app.glass)
			ground = MAIN_GROUND_GLASS;
		if (main_app.fullscreen)
			ground = MAIN_GROUND_FULLSCREEN;

		/* The frame: the image where the view places it, the canvas over it. */
		iv_app_quad(&main_app, &quad);
		result = iv_present_frame(&main_present, main_pixels, (size_t)main_present.extent.width, canvas_changed, &quad, ground);
		if (result == VK_SUCCESS) {
			main_app.dirty = 0;
			return 0;
		}

		/* Anything but a stale swapchain is a failure. */
		if (result != VK_ERROR_OUT_OF_DATE_KHR) {
			fprintf(stderr, "IMAGEVIEW FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A stale swapchain is remade at the window's size, with a canvas to match. */
		result = iv_present_resize(&main_present, main_window.width, main_window.height);
		if (result != VK_SUCCESS) {
			fprintf(stderr, "IMAGEVIEW FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A canvas of the swapchain's size. */
		status = main_canvas_make();
		if (status != 0)
			return -1;
		iv_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
	}

	/* The swapchain stayed out of date. */
	fprintf(stderr, "IMAGEVIEW FAILED operation=stale-swapchain\n");
	return -1;
}

/*
 * Gives the presenter the image shown (when it changed) and the frame of
 * an animated one (when it moved on); nonzero when the textures could not
 * be made.
 */
static int
main_image(void)
{
	const struct iv_image *image;
	VkResult result;

	/* The image's levels, once for each image shown. */
	image = NULL;
	if (main_app.has_image)
		image = main_app.current;
	result = iv_present_set_image(&main_present, image, main_app.image_serial);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "IMAGEVIEW FAILED operation=%s result=%d\n", main_present.operation, (int)result);
		return -1;
	}

	/* An animated image's frame, when it moved on. */
	if (image != NULL && image->frame_count > 1U && main_frame_serial != main_app.frame_serial) {
		iv_present_set_frame(&main_present, image->frames[main_app.frame]);
		main_frame_serial = main_app.frame_serial;
	}

	/* Succeeded: the presenter has the image. */
	return 0;
}

/* Makes the canvas's memory at the swapchain's size; nonzero when memory runs out. */
static int
main_canvas_make(void)
{
	uint32_t *pixels;
	size_t count;

	/* The canvas's memory. */
	count = (size_t)main_present.extent.width * (size_t)main_present.extent.height;
	pixels = malloc(count * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;
	free(main_pixels);
	main_pixels = pixels;

	/* The canvas over it, to be drawn anew. */
	main_canvas.pixels = main_pixels;
	main_canvas.stride = main_present.extent.width;
	main_canvas.width = (int)main_present.extent.width;
	main_canvas.height = (int)main_present.extent.height;
	main_app.dirty = 1;
	main_app.ui_dirty = 1;

	/* Succeeded: frames can be drawn. */
	return 0;
}

/* Gathers what the menus and the titlebar show. */
static void
main_state(
	struct iv_state *state)
{
	/* A clean state, so that states compare by their bytes. */
	memset(state, 0, sizeof(*state));
	state->has_image = main_app.has_image;
	if (main_app.has_image && main_app.current != NULL && main_app.current->error == 0)
		state->can_show = 1;
	state->index = main_app.folder.index;
	state->count = main_app.folder.count;
	state->fit = main_app.fit;
	if (main_app.has_image && main_app.current != NULL && main_app.current->frame_count > 1U)
		state->animated = 1;
	state->playing = main_app.playing;
	state->fullscreen = main_app.fullscreen;
}

/* After an image opened: the window's title names it, and it joins the recent files. */
static void
main_opened(void)
{
	char resolved[PATH_MAX];
	char title[IV_PATH_MAX + 32];
	const char *name;
	char *absolute;
	int error;

	/* Only once for each image shown. */
	if (!main_app.opened)
		return;
	main_app.opened = 0;

	/* Without an image, the application's name. */
	if (!main_app.has_image || main_app.current == NULL) {
		iv_window_title(&main_window, "Image Viewer");
		return;
	}

	/* The title: the file's name and the application's. */
	name = strrchr(main_app.current->path, '/');
	if (name == NULL)
		name = main_app.current->path;
	else
		name++;
	snprintf(title, sizeof(title), "%s \xe2\x80\x94 Image Viewer", name);
	iv_window_title(&main_window, title);

	/* The recent files, by the absolute path. */
	absolute = realpath(main_app.current->path, resolved);
	if (absolute == NULL)
		return;
	error = keiland_recent_add(resolved, MAIN_APPLICATION);
	if (error != 0)
		iv_log("RECENT failed errno=%d", error);
}

/*
 * Asks the compositor for the full screen (or out of it) when the viewer
 * wants it, and lays the viewer out as the compositor made the window.
 */
static void
main_fullscreen(void)
{
	/* A request: the opposite of how the window is now. */
	if (main_app.want_fullscreen) {
		main_app.want_fullscreen = 0;
		iv_window_fullscreen(&main_window, !main_window.fullscreen);
		iv_log("FULLSCREEN request=%d", !main_window.fullscreen);
	}

	/* The compositor's answer: the layout (and the glass) follow it. */
	if (main_app.fullscreen != main_window.fullscreen) {
		main_app.fullscreen = main_window.fullscreen;
		iv_app_layout(&main_app);
		iv_log("FULLSCREEN state=%d", main_app.fullscreen);
	}
}
