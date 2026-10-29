Programming a Keiland application
==================================

Status: how-to guide

<!-- WRITE (whole guide, read first):
This file is a structure for the rewrite.  Every "WRITE" comment says what text goes at its place; every "OLD TEXT"
comment holds the text of the earlier version for reuse.  When the rewrite is done, no WRITE and no OLD TEXT comment
is left; the "example:" marker comments stay (a test compares the code block under each marker with its file).

Readers: C programmers who know other UI toolkits (some of them experts in immediate mode or retained mode toolkits)
and know nothing of Keiland yet.  The reviewer of the earlier version could not understand kl_ui from snippets, so:

1. Every chapter is built on one complete program, shown whole, that compiles, links with -lkeiland and runs.  The
   programs live in docs/howto/keiland-examples/ and are shown byte for byte under their "example:" markers.  Do not
   edit the code inside the blocks; if a program must change, change the file, copy it into the block again, run the
   test (it is not linked from here), and fix every line number this guide cites from it.
2. The pattern of every chapter, in this order:
   a. What the chapter's program does and what it looks like (one short paragraph and the screenshot when there is
      one: keiland-examples/NN.png next to the program).
   b. The program, whole.
   c. "Reading the program" (inductive): walk through the program in the order it runs, citing line ranges as links
      of the form [lines 118-127](keiland-examples/04-counter.c#L118-L127), and quoting the two to six lines being
      discussed right under the sentence, so the reader does not count lines.  Explain only what this chapter adds;
      for what an earlier chapter explained, say "as in chapter N" and move on.
   d. "The rules" (deductive): the concepts and rules the program showed, stated generally, each with a link back to
      the lines of the program where it is seen.  A reader who skipped (c) can read (d) alone and come back.
   e. "Pitfalls" and one sentence that leads to the next chapter.
3. Grow the examples visibly: 02 is 01 with a window, 03 is 02 with a titlebar, 04 is 02 with kl_ui and buttons,
   04-counter-parts is 04 with pacing and partial frames, 06 is 04 with menus.  Say what changed from the program
   before (the new struct members, the new calls), not only what the program is.
4. Define each term where the WRITE comment says it is first defined, in bold, once; later chapters use it without
   redefining it.  Keep one meaning per word.  In particular "frame" means two things in libkeiland: the pixels the
   program draws into and shows (chapter 2), and one drawing of the window's widgets between kl_ui_begin and
   kl_ui_end (chapter 4).  Chapter 4 must say that these are the same event seen from two sides: drawing one frame
   of pixels is one pass of the widgets.
5. Do not mention the descriptor API (kl_app_watch_fd, KL_APP_FD, kl_instance_*, kl_system_media_*): it is being
   replaced, and the guide gets the new API later.  Do not link into plan/.  Write in English, in the plain style of
   docs/style.md.  Keep headers as the authority: link the header of each component where it is first used.
6. Keep "Prerequisites" and the build and run commands per chapter (cc ... -lkeiland), with FONT_DIR for a Linux
   build: cc -DFONT_DIR='"/opt/keiland/share/fonts"' -o counter 04-counter.c -lkeiland.
-->

<!-- WRITE (introduction): Keep the earlier introduction's content (OLD TEXT below): what libkeiland is, the order of
the chapters, the headers as the authority, the prerequisites, linking with -lkeiland, one thread.  Add: the guide is
built on eight complete programs in keiland-examples/ (list them in a small table: file, chapter, what it shows), each
one growing out of an earlier one; how to build one (cc -o NAME NAME.c -lkeiland, and -DFONT_DIR for a Linux build
under /opt/keiland), and that each runs in a Keiland session (WAYLAND_DISPLAY set).  Say how to read the guide: run
the chapter's program first, then read it with the guide. -->

<!-- OLD TEXT (introduction; reuse, then delete):
Status: how-to guide

This guide shows how to write a desktop application with libkeiland, the
library every Keiland program draws, reads its input and talks to the desktop
with. It goes in the order a program is built: the application, its window,
the window's titlebar, the user interface and the main loop, the widgets, the
menus, and then the other services of the desktop.

The interface is the C headers under
[userland/desktop/include/keiland/](../../userland/desktop/include/keiland/).
`<keiland/keiland.h>` includes all of them; each `keiland_*.h` holds one
component (`kl_app`, `kl_window`, `kl_ui`, the widgets, ...) and can be
included alone. The header of each call is the authority for its exact
behaviour; this guide explains how the parts fit together.

Prerequisites:

- A C compiler and libkeiland: the zedBSD sysroot, or the Keiland build for
  Linux or FreeBSD (`userland/desktop/keiland-linux.mk`,
  `userland/desktop/keiland-freebsd.mk`).
- A running Keiland compositor to show the window.
- A TrueType font for the text. Keiland installs its own as
  `/usr/share/fonts/keiland.ttf` and `/usr/share/fonts/keiland-fallback.ttf`
  on zedBSD (under the installation's prefix elsewhere).

A program links with `-lkeiland`. Every call of an application is made from
the one thread that opened it.

-->

## 1. The application

<!-- WRITE (chapter 1, summary): The program connects to the desktop, posts a notification so that something is seen
(a notification under org.example.Hello), listens to the desktop for two seconds, and ends.  Screenshot
keiland-examples/01-app.png (the notification) when there is one.  Terms first defined here: application (struct
kl_app, one connection to the desktop's compositor), compositor, application ID, event, event queue, dispatch (wait
and read), take.  -->

<!-- example: keiland-examples/01-app.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The smallest Keiland application: it connects to the desktop, posts a
 * notification, listens to the desktop for two seconds and disconnects.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* How long the program listens to the desktop, in microseconds. */
#define LISTEN_US	2000000U

int
main(void)
{
	struct kl_app_options options;
	struct kl_app_event event;
	struct kl_app *app;
	uint64_t deadline;
	uint64_t now;
	int status;
	int taken;
	int wait;

	/* Connects to the desktop (the compositor WAYLAND_DISPLAY names) as org.example.Hello. */
	memset(&options, 0, sizeof(options));
	options.application = "org.example.Hello";
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "hello: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Shows that the program is there, under its application ID. */
	status = kl_app_notify(app, "Hello", "A Keiland application is running.");
	if (status != 0)
		fprintf(stderr, "hello: no notification (%s)\n", strerror(status));

	/* Listens until the deadline: waits for the desktop, then takes what it queued. */
	deadline = kl_clock_us() + LISTEN_US;
	for (;;) {
		/* Ends at the deadline. */
		now = kl_clock_us();
		if (now >= deadline)
			break;

		/* Waits for the desktop, at most until the deadline. */
		wait = (int)((deadline - now) / 1000U);
		status = kl_app_dispatch(app, wait);
		if (status != 0) {
			fprintf(stderr, "hello: the desktop went away\n");
			break;
		}

		/* Takes every event queued, oldest first. */
		for (;;) {
			taken = kl_app_take(app, &event);
			if (!taken)
				break;

			/* The desktop's appearance changed: light or dark, or the accent. */
			if (event.kind == KL_APP_THEME)
				printf("hello: the appearance changed\n");
		}
	}

	/* Disconnects. */
	kl_app_close(app);

	/* Succeeded: the program connected and listened. */
	printf("hello: done\n");
	return 0;
}
```

<!-- WRITE (chapter 1, reading the program), in this order:
1. Lines 33-40: kl_app_open with struct kl_app_options zeroed then filled: display NULL means WAYLAND_DISPLAY,
   application is the application ID windows are grouped by.  NULL with errno on failure: EOPNOTSUPP (no Keiland
   compositor), a connection error, EPROTO, ENOMEM.  Define "application" and "application ID" here.
2. Lines 42-45: kl_app_notify, the first visible effect; returns an errno value (ENOTSUP without notifications).
   Only one sentence here; chapter 7 has notifications in full.
3. Lines 47-73: the shape every later main loop has: dispatch (wait for the desktop and queue what came), then take
   every queued event, oldest first.  Define "dispatch", "take", "event" and "event queue".  Say what timeout_ms means
   (-1 for ever, 0 not at all) and that dispatch does not wait while events are already queued; -1 means the
   connection is lost (lines 57-61).
4. Lines 69-71: the kinds: KL_APP_THEME here; KL_APP_WINDOW comes with a window in chapter 2.  An application without
   windows usually gets nothing, which is why this program only listens for a moment.
5. Line 76: kl_app_close closes the windows still open and the connection.
-->

<!-- WRITE (chapter 1, the rules): One struct kl_app per program; every libkeiland call of the application from the one
thread that opened it; everything that happens to the program arrives through the one queue, in the order it happened
(so a window's typed text and a menu's Ctrl+S keep their order); a held key repeats inside kl_app_dispatch.  The table
of event kinds (KL_APP_WINDOW, KL_APP_THEME) from the OLD TEXT, without the descriptor kind.  Link
userland/desktop/include/keiland/keiland_app.h. -->

<!-- WRITE (chapter 1, pitfalls and next): Calling libkeiland from another thread; forgetting that dispatch returns at
once while the queue is not empty (take everything before dispatching again).  Next: a window, whose inputs are what
fills the queue. -->

<!-- OLD TEXT (chapter 1; reuse, then delete):
## 1. The application

A `struct kl_app` is the program's connection to the desktop. It learns what
the compositor offers once, when it opens, and everything the program makes
afterwards (windows, menus, titlebar controls, glass) uses that one
connection. Everything that happens to the program arrives as one queue of
`struct kl_app_event` values, in the order it happened.

```c
#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
	struct kl_app_options options;
	struct kl_app *app;

	/* The connection: WAYLAND_DISPLAY's compositor, and the app_id the windows get. */
	memset(&options, 0, sizeof(options));
	options.application = "org.example.Hello";
	app = kl_app_open(&options);
	if (app == NULL) {
		fprintf(stderr, "hello: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* ... windows and the main loop ... */

	kl_app_close(app);
	return 0;
}
```

- `options.display` names the compositor's socket; NULL takes
  `WAYLAND_DISPLAY`. `options.application` is the application ID the desktop
  shows and groups windows by.
- `kl_app_open` returns NULL with `errno` set when there is no compositor
  (`EOPNOTSUPP`), the connection fails, or memory runs out.
- `kl_app_close` closes the windows still open and the connection.

The queue is read with two calls:

- `kl_app_dispatch(app, timeout_ms)` waits up to `timeout_ms` milliseconds
  (-1 waits for ever, 0 does not wait) for the compositor, reads what came,
  and queues it. It does not wait while events are already queued or a held
  key is due to repeat. It returns -1 when the connection is lost.
- `kl_app_take(app, &event)` takes the oldest event: 1 with it, 0 when the
  queue is empty.

An event's `kind` is one of:

| kind | meaning |
| --- | --- |
| `KL_APP_WINDOW` | an input of one of the application's windows: `event.window` and `event.input` |
| `KL_APP_THEME` | the desktop's appearance (light or dark, the accent) changed: draw every window again |

A held key repeats by itself inside `kl_app_dispatch`: the repeats are queued
as key inputs with `repeated` set, after the inputs read with them.

-->

## 2. The window

<!-- WRITE (chapter 2, summary): The program opens a 480x320 window, paints the theme's ground, and draws a dot that
follows the pointer; the close button ends it.  It is chapter 1's program with a window, a frame and a real main loop.
Screenshot keiland-examples/02-window.png.  Terms first defined here: window, configure (the compositor gives the
size), presenter (KL_PRESENT_*), frame (the pixels the program draws and shows: premultiplied 0xAARRGGBB words, stride
counted in words), canvas, input (struct kl_window_event, delivered as KL_APP_WINDOW), main loop, and the program's
"dirty", "resized" and "closed" flags as a pattern every later example keeps. -->

<!-- example: keiland-examples/02-window.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A window that the program draws itself: the theme's ground, and a dot
 * that follows the pointer.  It ends when its window is closed.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The dot's radius, in pixels. */
#define DOT_RADIUS	12.0f

/*
 * Everything the program keeps: its connection and window, the frame it
 * draws into, where the pointer is, and what the main loop has to do next.
 */
struct hello {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	double pointer_x;
	double pointer_y;
	int pointer_in;
	int dirty;
	int resized;
	int closed;
};

static int hello_resize(struct hello *hello);
static void hello_draw(struct hello *hello);
static int hello_frame(struct hello *hello);
static void hello_input(struct hello *hello, const struct kl_window_event *input);

int
main(void)
{
	static struct hello hello;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Window";
	hello.app = kl_app_open(&app_options);
	if (hello.app == NULL) {
		fprintf(stderr, "window: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window; it is configured when the call returns. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Window";
	window_options.width = 480;
	window_options.height = 320;
	window_options.present = KL_PRESENT_VULKAN;
	hello.window = kl_app_window_create(hello.app, &window_options);
	if (hello.window == NULL) {
		fprintf(stderr, "window: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Makes the first frame at the size the compositor gave. */
	status = hello_resize(&hello);
	if (status != 0)
		return 1;

	/* Runs until the window is closed: waits, takes the events, draws when something changed. */
	while (!hello.closed) {
		/* Waits for the desktop; not at all while a frame is due. */
		timeout = -1;
		if (hello.dirty || hello.resized)
			timeout = 0;
		status = kl_app_dispatch(hello.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(hello.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the theme's colours are new. */
			if (event.kind == KL_APP_THEME)
				hello.dirty = 1;

			/* An input of the window. */
			if (event.kind == KL_APP_WINDOW && event.window == hello.window)
				hello_input(&hello, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (hello.resized) {
			hello.resized = 0;
			status = hello_resize(&hello);
			if (status != 0)
				break;
		}

		/* Something changed: draws and shows a frame. */
		if (hello.dirty) {
			status = hello_frame(&hello);
			if (status != 0)
				break;
		}
	}

	/* Closes the window and the connection, and frees the frame. */
	kl_canvas_release(&hello.canvas);
	free(hello.pixels);
	kl_app_close(hello.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
hello_resize(
	struct hello *hello)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(hello->window, &hello->width, &hello->height);
	if (status != 0)
		return -1;

	/* Allocates the frame: one premultiplied 0xAARRGGBB word per pixel. */
	pixels = malloc((size_t)hello->width * hello->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones. */
	if (hello->pixels != NULL)
		kl_canvas_release(&hello->canvas);
	free(hello->pixels);
	hello->pixels = pixels;
	status = kl_canvas_init(&hello->canvas, hello->pixels, hello->width, (int)hello->width, (int)hello->height);
	if (status != 0)
		return -1;

	/* Succeeded: the new frame is drawn next. */
	hello->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the ground, and the dot where the pointer is.
 */
static void
hello_draw(
	struct hello *hello)
{
	const struct kl_theme *theme;
	struct kl_rect whole;

	/* Paints the theme's ground over the whole frame. */
	theme = kl_theme_default();
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)hello->width;
	whole.height = (int)hello->height;
	kl_canvas_gradient(&hello->canvas, &whole, theme->ground_top, theme->ground_bottom);

	/* Puts the dot under the pointer while the pointer is over the window. */
	if (hello->pointer_in)
		kl_canvas_circle(&hello->canvas, (float)hello->pointer_x, (float)hello->pointer_y, DOT_RADIUS, theme->accent);
}

/*
 * Draws and shows a frame; nonzero when it cannot be shown.
 */
static int
hello_frame(
	struct hello *hello)
{
	int status;

	/* Draws the frame and hands it to the compositor. */
	hello->dirty = 0;
	hello_draw(hello);
	status = kl_window_present(hello->window, hello->pixels, hello->width);

	/* The window's size changed under the frame: a new frame of the new size. */
	if (status == EAGAIN) {
		hello->resized = 1;
		return 0;
	}

	/* Refuses to go on when the frame could not be shown. */
	if (status != 0)
		return -1;

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Acts on one input of the window.
 */
static void
hello_input(
	struct hello *hello,
	const struct kl_window_event *input)
{
	/* Sorts the input by its kind. */
	switch (input->kind) {
	case KL_WINDOW_MOTION:
		/* The pointer moved: the dot follows it. */
		hello->pointer_x = input->x;
		hello->pointer_y = input->y;
		hello->pointer_in = 1;
		hello->dirty = 1;
		break;
	case KL_WINDOW_LEAVE:
		/* The pointer left the window: the dot goes. */
		hello->pointer_in = 0;
		hello->dirty = 1;
		break;
	case KL_WINDOW_RESIZE:
		/* The compositor gave a new size. */
		hello->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		/* The close button: the program ends. */
		hello->closed = 1;
		break;
	default:
		/* The keys, the fingers and the rest: not used here. */
		break;
	}
}
```

<!-- WRITE (chapter 2, reading the program), in this order:
1. Lines 22-38: struct hello holds everything the program keeps.  Point out the three kinds of member: the desktop's
   objects (app, window), the frame (canvas, pixels, width, height), the state the picture shows (pointer_*), and what
   the main loop must do next (dirty, resized, closed).  Later chapters keep this shape.
2. Lines 66-76: kl_app_window_create with struct kl_window_options: title, the size asked for until the compositor
   gives one, present.  The window is already configured when the call returns.  KL_PRESENT_VULKAN shows CPU-drawn
   frames through a Vulkan swapchain and is the one that can be see-through for glass (chapter 7); KL_PRESENT_SHM
   through shared memory; KL_PRESENT_NONE leaves the surface to the program's own Vulkan.
3. Lines 133-165 (hello_resize): kl_window_present_resize reports the size; the frame is malloc'ed memory of that many
   words; kl_canvas_init puts a canvas on it (stride in words).  The old canvas is released before the new one is made
   (lines 154-156).  Define "frame" and "canvas".
4. Lines 167-188 (hello_draw): the canvas calls, the theme's colours (kl_theme_default), drawing the whole picture
   from the state every time.
5. Lines 190-216 (hello_frame): kl_window_present, and EAGAIN: the swapchain no longer matches the window; the loop
   makes a new frame and draws again (no error).
6. Lines 218-252 (hello_input): the input kinds used here (MOTION, LEAVE, RESIZE, CLOSE); input changes the state and
   sets a flag; it never draws.
7. Lines 83-122 (the main loop): wait (no wait while a frame is due, lines 86-88), take every event (KL_APP_THEME sets
   dirty; KL_APP_WINDOW of this window goes to hello_input), then act on the flags: resize first, then draw once.  Say
   explicitly why drawing happens once after the queue is empty and not per input: many inputs, one frame.
-->

<!-- WRITE (chapter 2, the rules): The frame is ordinary memory the program owns (premultiplied 0xAARRGGBB, stride in
words); kl_window_present copies or shows it; EAGAIN means resize and draw again; KL_WINDOW_RESIZE means call
kl_window_present_resize and make a new frame; KL_WINDOW_CLOSE is a request (the program decides).  Coordinates are
surface pixels; times are CLOCK_MONOTONIC microseconds (kl_clock_us).  The list of input kinds from the OLD TEXT (the
pointer, keys, fingers, an input method's text, RESIZE, CLOSE, ACTION), each with the chapter that uses it.  Link
keiland_window.h and keiland_canvas.h. -->

<!-- WRITE (chapter 2, pitfalls and next): Drawing inside the input handler; forgetting to release the old canvas;
counting the stride in bytes; treating EAGAIN as an error.  Next: the window's titlebar, which the program does not
draw. -->

<!-- OLD TEXT (chapter 2; reuse, then delete):
## 2. The window

A window is made from the application with a `struct kl_window_options`.

```c
struct kl_window_options window_options;
struct kl_window *window;

memset(&window_options, 0, sizeof(window_options));
window_options.title = "Hello";
window_options.width = 480;
window_options.height = 320;
window_options.present = KL_PRESENT_VULKAN;
window = kl_app_window_create(app, &window_options);
if (window == NULL)
	return 1;
```

- `width` and `height` are the size asked for until the compositor gives
  one. The window is already configured when the call returns.
- `present` says how the frames are shown. `KL_PRESENT_VULKAN` shows the
  frames the program draws on the CPU through a Vulkan swapchain, and is the
  one that can be see-through for the desktop's glass. `KL_PRESENT_SHM`
  shows them through shared memory (a small window, or no Vulkan).
  `KL_PRESENT_NONE` leaves the surface to the program's own Vulkan
  (`kl_window_vulkan_surface`).
- `fullscreen` asks for the full screen before the first configure.

### Frames

A frame is ordinary memory: premultiplied `0xAARRGGBB` words, as many as the
size `kl_window_present_resize` reports. The program draws into it with a
`struct kl_canvas` and shows it with `kl_window_present`.

```c
uint32_t width;
uint32_t height;
uint32_t *pixels;
struct kl_canvas canvas;

/* The presenter at the window's size, and a frame of that size. */
if (kl_window_present_resize(window, &width, &height) != 0)
	return 1;
pixels = malloc((size_t)width * height * sizeof(pixels[0]));
if (pixels == NULL)
	return 1;
kl_canvas_init(&canvas, pixels, width, (int)width, (int)height);

/* Draws and shows it. */
kl_canvas_fill(&canvas, &(struct kl_rect){ 0, 0, (int)width, (int)height }, KL_RGB(0x2a6fdb));
kl_window_present(window, pixels, width);
```

- The stride is counted in words, not bytes.
- `kl_window_present` returns `EAGAIN` when the swapchain no longer matches
  the window: call `kl_window_present_resize` again, make a frame of the new
  size, and draw again.
- `kl_canvas_release` frees what the canvas allocated (the row its shapes are
  filled through); the pixels stay the program's.

### The window's inputs

The window's inputs come as `KL_APP_WINDOW` events. `event.input.kind` is a
`KL_WINDOW_*` value: the pointer (`KL_WINDOW_MOTION`, `_LEAVE`, `_BUTTON`,
`_AXIS`), the keys (`KL_WINDOW_KEY`, `_FOCUS`), the fingers
(`KL_WINDOW_TOUCH_*`), an input method's text (`KL_WINDOW_TEXT_*`), a new
size (`KL_WINDOW_RESIZE`), the close button (`KL_WINDOW_CLOSE`), and an
action chosen in the titlebar or a menu (`KL_WINDOW_ACTION`, see sections 3
and 6). Coordinates are in the surface's pixels, times in `CLOCK_MONOTONIC`
microseconds (`kl_clock_us()` reads that clock).

On `KL_WINDOW_RESIZE` the program calls `kl_window_present_resize`, makes a
frame of the new size and draws. On `KL_WINDOW_CLOSE` it closes the window
(`kl_window_close`) or ends.

-->

## 3. The titlebar

<!-- WRITE (chapter 3, summary): The program shows one colour at a time; Back and Forward in the titlebar step through
five colours, the search field finds a colour by name as it is typed, and the window's title is the colour's name.
It is chapter 2's program with titlebar controls and without the pointer's dot.  Screenshot
keiland-examples/03-titlebar.png.  Terms first defined here: titlebar presentation (menu, controls, tabs), control,
role, priority, action, action state, declarative and retained (the compositor keeps what the program gave until it
gives something else). -->

<!-- example: keiland-examples/03-titlebar.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A window with titlebar controls: it shows one colour at a time; Back and
 * Forward in the titlebar step through the colours, and the search field
 * finds one by its name.  The window's title is the colour's name.
 */

#include <keiland/keiland.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The titlebar's controls, by ID. */
#define ID_BACK		1U
#define ID_FORWARD	2U
#define ID_SEARCH	3U

/* The actions the controls queue when chosen. */
#define ACTION_BACK	1U
#define ACTION_FORWARD	2U
#define ACTION_SEARCH	3U

/* The radius of the dots that tell which colour is shown, and the room between them. */
#define DOT_RADIUS	6.0f
#define DOT_SPACING	24

/*
 * One colour the program shows: its name and its value.
 */
struct colour {
	const char *name;
	kl_color value;
};

/*
 * Everything the program keeps: its connection and window, the frame,
 * which colour is shown, and what the main loop has to do next.
 */
struct colours {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	size_t current;
	int dirty;
	int resized;
	int closed;
};

static int colours_resize(struct colours *colours);
static void colours_draw(struct colours *colours);
static int colours_frame(struct colours *colours);
static void colours_show(struct colours *colours);
static void colours_input(struct colours *colours, const struct kl_window_event *input);
static long colours_find(const char *text);

/*
 * The colours, in the order Back and Forward step through them.
 */
static const struct colour colour_table[] = {
	{ "Sky", KL_RGB(0x5ac8fa) },
	{ "Leaf", KL_RGB(0x34c759) },
	{ "Sun", KL_RGB(0xffcc00) },
	{ "Rose", KL_RGB(0xff2d55) },
	{ "Slate", KL_RGB(0x8e8e93) }
};

#define COLOUR_COUNT	(sizeof(colour_table) / sizeof(colour_table[0]))

/*
 * The titlebar's controls: what each is (its role), which give way first
 * when the titlebar is short of room (its priority), and the action it
 * queues.  The compositor decides how they look.
 */
static const struct kl_control_entry control_table[] = {
	/* id, role, priority, group, label, action */
	{ ID_BACK, KL_CONTROL_BACK, KL_PRIORITY_PRIMARY, 0U, "Back", ACTION_BACK },
	{ ID_FORWARD, KL_CONTROL_FORWARD, KL_PRIORITY_PRIMARY, 0U, "Forward", ACTION_FORWARD },
	{ ID_SEARCH, KL_CONTROL_SEARCH, KL_PRIORITY_NORMAL, 0U, "Find a colour", ACTION_SEARCH }
};

int
main(void)
{
	static struct colours colours;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Colours";
	colours.app = kl_app_open(&app_options);
	if (colours.app == NULL) {
		fprintf(stderr, "colours: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = colour_table[0].name;
	window_options.width = 480;
	window_options.height = 320;
	window_options.present = KL_PRESENT_VULKAN;
	colours.window = kl_app_window_create(colours.app, &window_options);
	if (colours.window == NULL) {
		fprintf(stderr, "colours: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Gives the titlebar its controls; a compositor without them leaves the title alone. */
	status = kl_window_set_controls(colours.window, control_table, sizeof(control_table) / sizeof(control_table[0]));
	if (status != 0)
		fprintf(stderr, "colours: no titlebar controls (%s)\n", strerror(status));

	/* Shows the search field's hint while it is empty. */
	if (status == 0)
		(void)kl_window_set_control_text(colours.window, ID_SEARCH, NULL, "Colour name");

	/* Sets the title and the state of Back and Forward for the first colour. */
	colours_show(&colours);

	/* Makes the first frame. */
	status = colours_resize(&colours);
	if (status != 0)
		return 1;

	/* Runs until the window is closed. */
	while (!colours.closed) {
		/* Waits for the desktop; not at all while a frame is due. */
		timeout = -1;
		if (colours.dirty || colours.resized)
			timeout = 0;
		status = kl_app_dispatch(colours.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(colours.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the dots' colours are the theme's. */
			if (event.kind == KL_APP_THEME)
				colours.dirty = 1;

			/* An input of the window, the titlebar's among them. */
			if (event.kind == KL_APP_WINDOW && event.window == colours.window)
				colours_input(&colours, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (colours.resized) {
			colours.resized = 0;
			status = colours_resize(&colours);
			if (status != 0)
				break;
		}

		/* Something changed: draws and shows a frame. */
		if (colours.dirty) {
			status = colours_frame(&colours);
			if (status != 0)
				break;
		}
	}

	/* Closes the window and the connection, and frees the frame. */
	kl_canvas_release(&colours.canvas);
	free(colours.pixels);
	kl_app_close(colours.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
colours_resize(
	struct colours *colours)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(colours->window, &colours->width, &colours->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)colours->width * colours->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones. */
	if (colours->pixels != NULL)
		kl_canvas_release(&colours->canvas);
	free(colours->pixels);
	colours->pixels = pixels;
	status = kl_canvas_init(&colours->canvas, colours->pixels, colours->width, (int)colours->width, (int)colours->height);
	if (status != 0)
		return -1;

	/* Succeeded: the new frame is drawn next. */
	colours->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the colour shown, and a row of dots that tells which
 * of the colours it is.
 */
static void
colours_draw(
	struct colours *colours)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	size_t index;
	float x;
	float y;

	/* Paints the colour shown over the whole frame. */
	theme = kl_theme_default();
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)colours->width;
	whole.height = (int)colours->height;
	kl_canvas_fill(&colours->canvas, &whole, colour_table[colours->current].value);

	/* Draws one dot per colour along the bottom, the one shown filled. */
	x = (float)colours->width / 2.0f - (float)(DOT_SPACING * (int)(COLOUR_COUNT - 1U)) / 2.0f;
	y = (float)colours->height - 32.0f;
	for (index = 0; index < COLOUR_COUNT; index++) {
		if (index == colours->current) {
			kl_canvas_circle(&colours->canvas, x, y, DOT_RADIUS, theme->panel);
		} else {
			kl_canvas_ring(&colours->canvas, x, y, DOT_RADIUS, 2.0f, 1.0f, theme->panel);
		}
		x += (float)DOT_SPACING;
	}
}

/*
 * Draws and shows a frame; nonzero when it cannot be shown.
 */
static int
colours_frame(
	struct colours *colours)
{
	int status;

	/* Draws the frame and hands it to the compositor. */
	colours->dirty = 0;
	colours_draw(colours);
	status = kl_window_present(colours->window, colours->pixels, colours->width);

	/* The window's size changed under the frame: a new frame of the new size. */
	if (status == EAGAIN) {
		colours->resized = 1;
		return 0;
	}

	/* Refuses to go on when the frame could not be shown. */
	if (status != 0)
		return -1;

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Tells the titlebar about the colour now shown: its name as the title,
 * Back disabled at the first colour, Forward disabled at the last.
 */
static void
colours_show(
	struct colours *colours)
{
	unsigned back;
	unsigned forward;

	/* Names the window after the colour. */
	kl_window_set_title(colours->window, colour_table[colours->current].name);

	/* Disables the steps that lead nowhere. */
	back = 0U;
	forward = 0U;
	if (colours->current == 0U)
		back = KL_ACTION_DISABLED;
	if (colours->current + 1U == COLOUR_COUNT)
		forward = KL_ACTION_DISABLED;
	(void)kl_window_set_action_state(colours->window, ACTION_BACK, back);
	(void)kl_window_set_action_state(colours->window, ACTION_FORWARD, forward);

	/* The frame shows the new colour. */
	colours->dirty = 1;
}

/*
 * Acts on one input of the window.
 */
static void
colours_input(
	struct colours *colours,
	const struct kl_window_event *input)
{
	long found;

	/* Sorts the input by its kind. */
	switch (input->kind) {
	case KL_WINDOW_ACTION:
		/* A control was chosen: its action is in code. */
		if (input->code == ACTION_BACK && colours->current > 0U) {
			colours->current--;
			colours_show(colours);
		} else if (input->code == ACTION_FORWARD && colours->current + 1U < COLOUR_COUNT) {
			colours->current++;
			colours_show(colours);
		}
		break;
	case KL_WINDOW_CONTROL_TEXT:
		/* The search field's text as it is typed: the first colour it names. */
		found = colours_find(input->text);
		if (found >= 0 && (size_t)found != colours->current) {
			colours->current = (size_t)found;
			colours_show(colours);
		}
		break;
	case KL_WINDOW_CONTROL_DONE:
		/* The search ended (Enter, Escape or elsewhere): the field is emptied for the next one. */
		(void)kl_window_set_control_text(colours->window, ID_SEARCH, "", NULL);
		break;
	case KL_WINDOW_RESIZE:
		/* The compositor gave a new size. */
		colours->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		/* The close button: the program ends. */
		colours->closed = 1;
		break;
	default:
		/* The pointer, the keys and the rest: not used here. */
		break;
	}
}

/*
 * Finds the first colour whose name starts with a text, ignoring case;
 * -1 for none (and for an empty text).
 */
static long
colours_find(
	const char *text)
{
	size_t length;
	size_t index;
	size_t at;
	int same;

	/* An empty search names no colour. */
	length = strlen(text);
	if (length == 0U)
		return -1;

	/* Compares the text with the start of each name. */
	for (index = 0; index < COLOUR_COUNT; index++) {
		same = 1;
		for (at = 0; at < length; at++) {
			if (colour_table[index].name[at] == '\0' ||
			    tolower((unsigned char)colour_table[index].name[at]) != tolower((unsigned char)text[at])) {
				same = 0;
				break;
			}
		}
		if (same)
			return (long)index;
	}

	/* No colour has such a name. */
	return -1;
}
```

<!-- WRITE (chapter 3, reading the program), in this order:
1. Lines 21-29: two numberings: a control's ID (which control) and an action (what the program is asked to do).  They
   happen to be equal here; say why they are separate (chapter 6: a menu item and a control can share one action).
2. Lines 80-90: the table of struct kl_control_entry: id, role (KL_CONTROL_*: how the compositor draws it), priority
   (KL_PRIORITY_*: which give way first when the titlebar is short), group (segmented controls, 0 for none), label,
   action.  Define "control", "role", "priority", "action".
3. Lines 124-131: kl_window_set_controls gives the whole table; ENOTSUP from a compositor without the presentation (the
   window still works); kl_window_set_control_text sets the search field's placeholder after the table.
4. Lines 288-314 (colours_show): after every change the program gives the state again: the title and the action
   states (KL_ACTION_DISABLED for Back at the first colour).  Define "action state"; say it applies to every control
   and menu item of that action.  Contrast with chapter 2: here the program does not draw these, it describes them.
5. Lines 316-362 (colours_input): KL_WINDOW_ACTION with the action in input.code (and the control's ID in input.id);
   KL_WINDOW_CONTROL_TEXT with the search text as typed in input.text; KL_WINDOW_CONTROL_DONE when the editing ends
   (how in input.code: KL_TEXT_SUBMITTED and the others).  These come through the same queue as every other input.
-->

<!-- WRITE (chapter 3, the rules): The titlebar is declarative and retained: the program gives the whole table whenever
something changes, the library sends only the difference, and it stays as given.  The three presentations (the menu
by default, controls, tabs) and how to choose: a count of 0 gives the titlebar back to the menu; kl_window_set_tabs
and KL_WINDOW_TAB with KL_WINDOW_TAB_* for tabs (no example; keep the OLD TEXT's paragraph).  The compositor decides
the look.  Link keiland_titlebar.h and the declarative calls in keiland_window.h. -->

<!-- WRITE (chapter 3, pitfalls and next): Expecting the compositor to disable Back by itself (it does not; set the
action state); using the same number for two controls; treating ENOTSUP as fatal.  Next: the window's content made of
widgets, which is the opposite model: immediate, not retained. -->

<!-- OLD TEXT (chapter 3; reuse, then delete):
## 3. The titlebar

The compositor draws every window's titlebar: its mark and title, the
window's buttons, and one presentation the program chooses:

- the **menu**, the default: the window's menu bar (section 6);
- **controls**: back, forward, a breadcrumb, a search field, view switches,
  a progress, a primary action;
- **tabs**.

The program does not draw these. It describes them as a table, and gives the
whole table again whenever something changes; the library compares it with
what is shown and sends only the difference. The titlebar is *declarative and
retained*: it stays as given until the next table.

```c
#define ACTION_BACK	1U
#define ACTION_FORWARD	2U
#define ACTION_SEARCH	3U

static const struct kl_control_entry controls[] = {
	/* id, role, priority, group, label, action */
	{ 1U, KL_CONTROL_BACK, KL_PRIORITY_PRIMARY, 0U, "Back", ACTION_BACK },
	{ 2U, KL_CONTROL_FORWARD, KL_PRIORITY_PRIMARY, 0U, "Forward", ACTION_FORWARD },
	{ 3U, KL_CONTROL_SEARCH, KL_PRIORITY_NORMAL, 0U, "Search", ACTION_SEARCH }
};

kl_window_set_title(window, "Hello");
kl_window_set_controls(window, controls, sizeof(controls) / sizeof(controls[0]));
```

- `id` names the control (not 0, unique in the table). `role`
  (`KL_CONTROL_*`) decides how the compositor draws it, with the label.
  `priority` (`KL_PRIORITY_*`) is
  the order controls give way in when the titlebar is short of room. `group`
  joins segmented controls (0 for none).
- A control chosen comes as a window input of kind `KL_WINDOW_ACTION`: the
  control's `action` in `input.code` and its `id` in `input.id`.
- An action's state is set with `kl_window_set_action_state(window, action,
  state)`: `KL_ACTION_DISABLED`, `KL_ACTION_CHECKED`, `KL_ACTION_HIDDEN`
  (0 is enabled, unchecked and shown). It applies to every control and menu
  item of that action.
- A search field's text comes as it is typed (`KL_WINDOW_CONTROL_TEXT`, the
  text in `input.text`) and when its editing ends (`KL_WINDOW_CONTROL_DONE`).
  `kl_window_set_control_text` sets it.
- A count of 0 gives the titlebar back to the menu.
- `kl_window_set_tabs` shows tabs (`struct kl_tab_entry`: an ID, a title,
  `KL_TAB_*` flags); a tab chosen or closed comes as `KL_WINDOW_TAB` with
  `KL_WINDOW_TAB_*` in `code` and the tab's ID in `id`.

A compositor without the titlebar presentation returns `ENOTSUP`; the window
still works, with only its title.

-->

## 4. The user interface and the main loop

<!-- WRITE (chapter 4, summary): This is the chapter the reviewer could not follow in the earlier version, which
explained kl_ui from one kl_button line.  Order it strictly: the program first, then one frame followed through the
program, then the rules.  The program is a counter: a number and three buttons, Less, Reset (disabled at 0) and More;
Up and Down change the number.  It is chapter 2's program with a struct kl_ui, a font, a struct kl_style and three
kl_button calls.  Screenshot keiland-examples/04-counter.png.  Terms first defined here: kl_ui (the widgets' record of
one window), widget (a function called while a frame is drawn), immediate mode, frame of the widgets (kl_ui_begin to
kl_ui_end; say it is the same frame as chapter 2's pixels), id and index, record (where a widget was drawn), "resolved
against the frame last drawn", click reported in the next frame, unclaimed input (kl_ui_take), style, theme,
"moving". -->

<!-- example: keiland-examples/04-counter.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A counter: a window with a number and three buttons, Less, Reset and
 * More.  The Up and Down keys change the number too.  The widgets are
 * Keiland's immediate mode widgets: each is one call in the frame.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the desktop's fonts are (/opt/keiland/share/fonts for a Linux build under /opt/keiland). */
#ifndef FONT_DIR
#define FONT_DIR	"/usr/share/fonts"
#endif
#define FONT		FONT_DIR "/keiland.ttf"
#define FALLBACK_FONT	FONT_DIR "/keiland-fallback.ttf"

/* The widgets' ids: a number per widget, unique in the window, the same in every frame. */
#define ID_LESS		1U
#define ID_RESET	2U
#define ID_MORE		3U

/* The buttons' width and the room around them, in pixels. */
#define BUTTON_WIDTH	96
#define MARGIN		24

/*
 * Everything the program keeps: its connection and window, the widgets'
 * record (ui), the font, the frame, the state the widgets show (count),
 * and what the main loop has to do next.
 */
struct counter {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int count;
	int dirty;
	int moving;
	int resized;
	int closed;
};

static int counter_resize(struct counter *counter);
static void counter_draw(struct counter *counter, uint64_t now_us);
static int counter_frame(struct counter *counter);
static void counter_input(struct counter *counter, const struct kl_window_event *input);
static void counter_unclaimed(struct counter *counter);

int
main(void)
{
	static struct counter counter;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Counter";
	counter.app = kl_app_open(&app_options);
	if (counter.app == NULL) {
		fprintf(stderr, "counter: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Counter";
	window_options.width = 400;
	window_options.height = 240;
	window_options.present = KL_PRESENT_VULKAN;
	counter.window = kl_app_window_create(counter.app, &window_options);
	if (counter.window == NULL) {
		fprintf(stderr, "counter: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Makes the widgets' record of the window. */
	counter.ui = kl_ui_create();
	if (counter.ui == NULL)
		return 1;

	/* Opens the font the widgets and the number are drawn in. */
	status = kl_text_open(&counter.text, FONT, FALLBACK_FONT);
	if (status != 0) {
		fprintf(stderr, "counter: no font %s (%s)\n", FONT, strerror(status));
		return 1;
	}

	/* Sets what every widget draws with: the frame's canvas (set by counter_resize), the font, no glass. */
	counter.style.text = &counter.text;
	counter.style.glass = 0;

	/* Makes the first frame. */
	status = counter_resize(&counter);
	if (status != 0)
		return 1;

	/* Runs until the window is closed: waits, gives each input to the widgets, draws when due. */
	while (!counter.closed) {
		/* Waits for the desktop: not at all while a frame is due, a frame's time while something moves. */
		timeout = -1;
		if (counter.moving)
			timeout = 16;
		if (counter.dirty || counter.resized)
			timeout = 0;
		status = kl_app_dispatch(counter.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(counter.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the theme's colours are new. */
			if (event.kind == KL_APP_THEME)
				counter.dirty = 1;

			/* An input of the window. */
			if (event.kind == KL_APP_WINDOW && event.window == counter.window)
				counter_input(&counter, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (counter.resized) {
			counter.resized = 0;
			status = counter_resize(&counter);
			if (status != 0)
				break;
		}

		/* Something changed or moves: draws and shows a frame. */
		if (counter.dirty || counter.moving) {
			status = counter_frame(&counter);
			if (status != 0)
				break;
		}
	}

	/* Frees the widgets' record, the frame and the font, and closes the window and the connection. */
	kl_ui_destroy(counter.ui);
	kl_canvas_release(&counter.canvas);
	free(counter.pixels);
	kl_text_close(&counter.text);
	kl_app_close(counter.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
counter_resize(
	struct counter *counter)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(counter->window, &counter->width, &counter->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)counter->width * counter->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones; the widgets draw on it. */
	if (counter->pixels != NULL)
		kl_canvas_release(&counter->canvas);
	free(counter->pixels);
	counter->pixels = pixels;
	status = kl_canvas_init(&counter->canvas, counter->pixels, counter->width, (int)counter->width, (int)counter->height);
	if (status != 0)
		return -1;
	counter->style.canvas = &counter->canvas;

	/* Succeeded: the new frame is drawn next. */
	counter->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the ground, the number and the three buttons.  Each
 * button's call draws it, records where it is for the input to come, and
 * reports a click the input made on it since the last frame.
 */
static void
counter_draw(
	struct counter *counter,
	uint64_t now_us)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	struct kl_rect less;
	struct kl_rect reset;
	struct kl_rect more;
	unsigned reset_flags;
	char number[32];
	int baseline;
	int clicked;

	/* Starts the frame: the records of the last one are what the input was matched against. */
	theme = kl_theme_default();
	counter->style.theme = theme;
	kl_ui_begin(counter->ui, now_us);

	/* Paints the ground. */
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)counter->width;
	whole.height = (int)counter->height;
	kl_canvas_gradient(&counter->canvas, &whole, theme->ground_top, theme->ground_bottom);

	/* Draws the number as it is now. */
	snprintf(number, sizeof(number), "%d", counter->count);
	baseline = kl_text_center(theme->text_title, MARGIN, 64);
	(void)kl_text_draw(&counter->text, &counter->canvas, MARGIN, baseline, number, strlen(number), theme->text_title, 1, theme->text);

	/* Lays out the three buttons in a row under it. */
	less.x = MARGIN;
	less.y = MARGIN + 80;
	less.width = BUTTON_WIDTH;
	less.height = theme->control_height;
	reset = less;
	reset.x = less.x + BUTTON_WIDTH + 12;
	more = reset;
	more.x = reset.x + BUTTON_WIDTH + 12;

	/* Less: one less. */
	clicked = kl_button(counter->ui, &counter->style, ID_LESS, &less, "Less", 0U);
	if (clicked) {
		counter->count--;
		counter->dirty = 1;
	}

	/* Reset: back to 0; disabled while the number is 0 already. */
	reset_flags = 0U;
	if (counter->count == 0)
		reset_flags = KL_BUTTON_DISABLED;
	clicked = kl_button(counter->ui, &counter->style, ID_RESET, &reset, "Reset", reset_flags);
	if (clicked) {
		counter->count = 0;
		counter->dirty = 1;
	}

	/* More: one more, the window's main action. */
	clicked = kl_button(counter->ui, &counter->style, ID_MORE, &more, "More", KL_BUTTON_PRIMARY);
	if (clicked) {
		counter->count++;
		counter->dirty = 1;
	}

	/* Ends the frame: nonzero while it needs more frames by itself (a finger down, keys waiting, a scroll gliding). */
	counter->moving = kl_ui_end(counter->ui, now_us);

	/* Lets the input method and the clipboard follow the focused widget (none takes text here). */
	kl_ui_window_text(counter->ui, counter->window);
}

/*
 * Draws and shows a frame, then takes the input no widget took; nonzero
 * when the frame cannot be shown.
 */
static int
counter_frame(
	struct counter *counter)
{
	int status;

	/* Draws the frame; a click it reports asks for one more (dirty again). */
	counter->dirty = 0;
	counter_draw(counter, kl_clock_us());

	/* Hands it to the compositor. */
	status = kl_window_present(counter->window, counter->pixels, counter->width);
	if (status == EAGAIN) {
		/* The window's size changed under the frame: a new frame of the new size. */
		counter->resized = 1;
	} else if (status != 0) {
		/* Refuses to go on when the frame could not be shown. */
		return -1;
	}

	/* Takes the keys and the presses that met no widget. */
	counter_unclaimed(counter);

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Gives one input of the window to the widgets, or acts on it.
 */
static void
counter_input(
	struct counter *counter,
	const struct kl_window_event *input)
{
	int taken;

	/* The window's own inputs: a new size, the close button. */
	if (input->kind == KL_WINDOW_RESIZE) {
		counter->resized = 1;
		return;
	}
	if (input->kind == KL_WINDOW_CLOSE) {
		counter->closed = 1;
		return;
	}

	/* The pointer, the keys and the fingers: the widgets' input, resolved against the frame last drawn. */
	taken = kl_ui_window_input(counter->ui, input);
	if (taken)
		counter->dirty = 1;
}

/*
 * Acts on the input no widget took: Up and Down change the number.
 */
static void
counter_unclaimed(
	struct counter *counter)
{
	struct kl_event event;
	int taken;

	/* Takes each input the frame left for the program. */
	for (;;) {
		taken = kl_ui_take(counter->ui, &event);
		if (!taken)
			break;

		/* Only keys are used here; presses on the ground are left alone. */
		if (event.kind != KL_EVENT_KEY)
			continue;

		/* Up is More, Down is Less. */
		if (event.code == KL_KEY_UP) {
			counter->count++;
			counter->dirty = 1;
		} else if (event.code == KL_KEY_DOWN) {
			counter->count--;
			counter->dirty = 1;
		}
	}
}
```

<!-- WRITE (chapter 4, reading the program): first the setup, then one click followed frame by frame.
Setup:
1. Lines 27-30: the widgets' ids, chosen by the program; the same id must name the same widget in every frame.
2. Lines 41-56: struct counter adds ui, text and style to chapter 2's struct; count is the state the widgets show.
   There is no button object anywhere in the program: point at that.
3. Lines 96-110: kl_ui_create; kl_text_open (FONT and FALLBACK_FONT, FONT_DIR for Linux); the style (text, theme,
   canvas set by counter_resize at line 199, glass 0).  Define "style".
The frame (lines 206-282, counter_draw), in the order it runs:
4. Lines 226-229: kl_ui_begin starts a frame of the widgets.
5. Lines 231-241: the program's own drawing (ground, number) is ordinary canvas and text drawing between the widgets.
6. Lines 243-251: layout is plain arithmetic on struct kl_rect values; there is no layout object.
7. Lines 253-275: each kl_button call draws the button, records its rectangle under its id, and returns 1 when the
   input since the last frame clicked it.  The program acts at once (count changes) and sets dirty.  Reset's flags are
   computed from the state each frame (KL_BUTTON_DISABLED when count is 0): "showing a different button" is calling it
   with different arguments.  Define "widget" and "immediate mode" here, with these lines as the evidence.
8. Lines 277-281: kl_ui_end: the records of this frame become the ones the next input is resolved against, a click is
   used up (it is reported in exactly one frame), and the return value says the frame needs successors by itself.
   kl_ui_window_text follows the focused widget's text input (none here; chapter 5 needs it).
One click, followed (write this as a numbered timeline, each step with its lines):
9. Frame N is drawn: the More button is recorded at its rectangle (line 271).
10. The press and the release arrive as KL_APP_WINDOW inputs (lines 129-142) and go to counter_input; RESIZE and CLOSE
   are the window's (lines 325-333); the rest goes to kl_ui_window_input (lines 335-338), which matches the press and
   the release with the button frame N recorded and returns 1, so dirty is set.
11. The loop draws frame N+1 (lines 152-157): kl_button for More returns 1 there (line 271), count becomes 1, and
   dirty is set again, because the number was already drawn above the buttons at line 241 with the old value.
12. Frame N+2 shows 1.  Say plainly that this extra frame is normal in immediate mode and cheap, and that it is why
   counter_frame clears dirty before drawing (line 295) rather than after.
13. Lines 341-370 and 308-309: after the frame, kl_ui_take gives what no widget took (here: Up and Down; no widget
   takes keys, so every key comes back); the program acts and sets dirty.
The loop (lines 117-158): waits 0 while a frame is due, 16 ms while moving, else for ever; takes every event; resizes;
draws once.  Same as chapter 2 plus "moving".
-->

<!-- WRITE (chapter 4, the rules): State the immediate mode model as rules, each with a link to the counter's lines:
1. A widget is a call, not an object: it exists in a frame because the program called it in that frame (lines
   253-275).  Nothing to create, show, hide or destroy.
2. What a widget shows is the program's state, passed in each frame (count; chapter 5's field, list, switch).  The
   library keeps no copy that could disagree.
3. A widget is named by an id (chosen by the program, unique in the window) and an index (to tell apart several
   widgets of one id: rows, swatches; chapter 5).  The pair must be the same from frame to frame for the same widget.
   The keyboard focus is such a pair too (kl_ui_set_focus, kl_ui_has_focus).
4. Input is resolved when it arrives, against the records of the frame last drawn (the latest recorded first, so the
   one on top wins): a press and a release on the same widget click it; the wheel goes to the scroll under the
   pointer; a finger's tap to the widget it touched.
5. The result comes back from the widget's call in the next frame, once (kl_ui_end uses it up).  What met no widget is
   taken after the frame with kl_ui_take (KL_EVENT_* kinds, including KL_EVENT_KEY).
6. A frame is drawn when something changed: an input the widgets took, the program's own state, the theme; and while
   kl_ui_end returns nonzero.  Nothing redraws by itself.
7. The window's frame (titlebar, controls, menus, glass panels) is not immediate: it is declarative and retained
   (chapters 3 and 6).  Say why the two models are mixed: the compositor draws the frame, the program the content.
State exactly what the input calls return (this corrects the earlier version):
- kl_ui_window_input returns 1 when the input was of a kind the widgets take (pointer, main button, wheel, keys,
  fingers, input method text), and 0 for another kind (the program's).  It does NOT tell whether the window must be
  drawn again; a program that uses it draws after every input it returned 1 for, as the counter does.
- The lower calls tell more: kl_ui_pointer_motion and kl_ui_pointer_leave return 1 only when the lit widget changed
  (KL_VERSION 90 and later), which the partial frames below use.
- kl_ui_end returns nonzero while the frame needs more frames by itself: a finger down, a drag, keys waiting for the
  next frame, the lit widget changing under a still pointer, a scroll gliding or its bars fading.  (The earlier
  version said "a widget animates or a scroll flies", which is incomplete.)
Link keiland_ui.h and keiland_widgets.h. -->

<!-- WRITE (chapter 4, pitfalls): Expecting the click's effect in the same frame as the click; changing the state
above the widget that shows it and not drawing again; ids that change between frames (a list's row numbered by its
position on screen instead of the item); calling a widget only while it is "visible" by a flag and expecting it to keep
state (the state is the program's anyway); drawing inside the input handler. -->

### The pace of frames and drawing only what changed

<!-- WRITE (section 4.x, summary): A second complete program, 04-counter-parts.c: the same counter at the display's
pace and drawing only the buttons whose light changed when the pointer moves.  Say first when a program needs this
(a fast pointer, a touch pad, a slow machine); the plain counter is correct without it.  Then show the program whole
and walk through only what differs from 04-counter.c (say so: the rest is identical).  Terms first defined here:
pacing (KL_PACING_MOTION, KL_PACING_FRAME), frame wait, lighting (the lit widget), damage, part. -->

<!-- example: keiland-examples/04-counter-parts.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The counter again, drawn at the display's pace and only where it
 * changed: a frame waits until the compositor took the last one, pointer
 * motions queued together count as one, and when the pointer only moves
 * from one button to another, only those two buttons are drawn again.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the desktop's fonts are (/opt/keiland/share/fonts for a Linux build under /opt/keiland). */
#ifndef FONT_DIR
#define FONT_DIR	"/usr/share/fonts"
#endif
#define FONT		FONT_DIR "/keiland.ttf"
#define FALLBACK_FONT	FONT_DIR "/keiland-fallback.ttf"

/* The widgets' ids: a number per widget, unique in the window, the same in every frame. */
#define ID_LESS		1U
#define ID_RESET	2U
#define ID_MORE		3U

/* The buttons' width and the room around them, in pixels. */
#define BUTTON_WIDTH	96
#define MARGIN		24

/*
 * Everything the program keeps: its connection and window, the widgets'
 * record (ui), the font, the frame, the state the widgets show (count),
 * and what the main loop has to do next: the whole frame (dirty), or only
 * the parts whose light the pointer changed (lit).
 */
struct counter {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int count;
	int dirty;
	int lit;
	int moving;
	int resized;
	int closed;
};

static int counter_resize(struct counter *counter);
static void counter_draw(struct counter *counter, uint64_t now_us);
static int counter_wait(struct counter *counter);
static int counter_frame(struct counter *counter);
static void counter_input(struct counter *counter, const struct kl_window_event *input);
static void counter_unclaimed(struct counter *counter);

int
main(void)
{
	static struct counter counter;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Counter";
	counter.app = kl_app_open(&app_options);
	if (counter.app == NULL) {
		fprintf(stderr, "counter: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Counter";
	window_options.width = 400;
	window_options.height = 240;
	window_options.present = KL_PRESENT_VULKAN;
	counter.window = kl_app_window_create(counter.app, &window_options);
	if (counter.window == NULL) {
		fprintf(stderr, "counter: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Draws at the display's pace: one motion for motions queued together, a frame once the last was taken. */
	status = kl_window_set_pacing(counter.window, KL_PACING_MOTION | KL_PACING_FRAME, 0U);
	if (status != 0)
		fprintf(stderr, "counter: no pacing (%s)\n", strerror(status));

	/* Makes the widgets' record of the window. */
	counter.ui = kl_ui_create();
	if (counter.ui == NULL)
		return 1;

	/* Opens the font the widgets and the number are drawn in. */
	status = kl_text_open(&counter.text, FONT, FALLBACK_FONT);
	if (status != 0) {
		fprintf(stderr, "counter: no font %s (%s)\n", FONT, strerror(status));
		return 1;
	}

	/* Sets what every widget draws with: the frame's canvas (set by counter_resize), the font, no glass. */
	counter.style.text = &counter.text;
	counter.style.glass = 0;

	/* Makes the first frame. */
	status = counter_resize(&counter);
	if (status != 0)
		return 1;

	/* Runs until the window is closed: waits, gives each input to the widgets, draws when due. */
	while (!counter.closed) {
		/* Waits for the desktop, or until a frame may be drawn. */
		timeout = counter_wait(&counter);
		status = kl_app_dispatch(counter.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(counter.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the theme's colours are new. */
			if (event.kind == KL_APP_THEME)
				counter.dirty = 1;

			/* An input of the window. */
			if (event.kind == KL_APP_WINDOW && event.window == counter.window)
				counter_input(&counter, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (counter.resized) {
			counter.resized = 0;
			status = counter_resize(&counter);
			if (status != 0)
				break;
		}

		/* Draws and shows a frame when one is due and may be drawn now. */
		status = counter_frame(&counter);
		if (status != 0)
			break;
	}

	/* Frees the widgets' record, the frame and the font, and closes the window and the connection. */
	kl_ui_destroy(counter.ui);
	kl_canvas_release(&counter.canvas);
	free(counter.pixels);
	kl_text_close(&counter.text);
	kl_app_close(counter.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
counter_resize(
	struct counter *counter)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(counter->window, &counter->width, &counter->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)counter->width * counter->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones; the widgets draw on it. */
	if (counter->pixels != NULL)
		kl_canvas_release(&counter->canvas);
	free(counter->pixels);
	counter->pixels = pixels;
	status = kl_canvas_init(&counter->canvas, counter->pixels, counter->width, (int)counter->width, (int)counter->height);
	if (status != 0)
		return -1;
	counter->style.canvas = &counter->canvas;

	/* Succeeded: the new frame is drawn next. */
	counter->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the ground, the number and the three buttons.  The
 * same calls draw the whole frame or, under a clip, a part of it.
 */
static void
counter_draw(
	struct counter *counter,
	uint64_t now_us)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	struct kl_rect less;
	struct kl_rect reset;
	struct kl_rect more;
	unsigned reset_flags;
	char number[32];
	int baseline;
	int clicked;

	/* Starts the frame: the records of the last one are what the input was matched against. */
	theme = kl_theme_default();
	counter->style.theme = theme;
	kl_ui_begin(counter->ui, now_us);

	/* Paints the ground. */
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)counter->width;
	whole.height = (int)counter->height;
	kl_canvas_gradient(&counter->canvas, &whole, theme->ground_top, theme->ground_bottom);

	/* Draws the number as it is now. */
	snprintf(number, sizeof(number), "%d", counter->count);
	baseline = kl_text_center(theme->text_title, MARGIN, 64);
	(void)kl_text_draw(&counter->text, &counter->canvas, MARGIN, baseline, number, strlen(number), theme->text_title, 1, theme->text);

	/* Lays out the three buttons in a row under it. */
	less.x = MARGIN;
	less.y = MARGIN + 80;
	less.width = BUTTON_WIDTH;
	less.height = theme->control_height;
	reset = less;
	reset.x = less.x + BUTTON_WIDTH + 12;
	more = reset;
	more.x = reset.x + BUTTON_WIDTH + 12;

	/* Less: one less. */
	clicked = kl_button(counter->ui, &counter->style, ID_LESS, &less, "Less", 0U);
	if (clicked) {
		counter->count--;
		counter->dirty = 1;
	}

	/* Reset: back to 0; disabled while the number is 0 already. */
	reset_flags = 0U;
	if (counter->count == 0)
		reset_flags = KL_BUTTON_DISABLED;
	clicked = kl_button(counter->ui, &counter->style, ID_RESET, &reset, "Reset", reset_flags);
	if (clicked) {
		counter->count = 0;
		counter->dirty = 1;
	}

	/* More: one more, the window's main action. */
	clicked = kl_button(counter->ui, &counter->style, ID_MORE, &more, "More", KL_BUTTON_PRIMARY);
	if (clicked) {
		counter->count++;
		counter->dirty = 1;
	}

	/* Ends the frame: nonzero while it needs more frames by itself (a finger down, keys waiting, a scroll gliding). */
	counter->moving = kl_ui_end(counter->ui, now_us);

	/* Lets the input method and the clipboard follow the focused widget (none takes text here). */
	kl_ui_window_text(counter->ui, counter->window);
}

/*
 * Tells how long the main loop may wait for the desktop, in milliseconds:
 * 0 when a frame may be drawn now, -1 for as long as nothing comes.
 */
static int
counter_wait(
	struct counter *counter)
{
	int pace;

	/* A new size is made at once. */
	if (counter->resized)
		return 0;

	/* Nothing to draw: waits for the desktop alone. */
	if (!counter->dirty && !counter->lit && !counter->moving)
		return -1;

	/* A frame due: now, at most so long, or when the compositor took the last one (-1). */
	pace = kl_window_frame_wait(counter->window, kl_clock_us());
	return pace;
}

/*
 * Draws and shows a frame when one is due and the compositor took the
 * last one, then takes the input no widget took; nonzero when the frame
 * cannot be shown.
 */
static int
counter_frame(
	struct counter *counter)
{
	struct kl_rect parts[KL_FRAME_PARTS_MAX];
	unsigned count;
	unsigned index;
	uint64_t now_us;
	int need;
	int pace;
	int status;

	/* Nothing changed and nothing moves: no frame. */
	if (!counter->dirty && !counter->lit && !counter->moving)
		return 0;

	/* The compositor has not taken the last frame yet: this one waits, and takes in what comes meanwhile. */
	now_us = kl_clock_us();
	pace = kl_window_frame_wait(counter->window, now_us);
	if (pace != 0)
		return 0;

	/* Only the light changed: asks which parts the widgets need drawn again. */
	count = 0U;
	if (!counter->dirty && !counter->moving) {
		need = kl_ui_take_frame(counter->ui, parts, KL_FRAME_PARTS_MAX, &count);
		if (need == KL_FRAME_NONE) {
			counter->lit = 0;
			return 0;
		}
		if (need == KL_FRAME_WHOLE)
			count = 0U;
	}
	counter->dirty = 0;
	counter->lit = 0;

	/* Draws the whole frame, or the same frame once per part, each clipped to its part. */
	if (count == 0U)
		counter_draw(counter, now_us);
	for (index = 0; index < count; index++) {
		kl_canvas_clip_push(&counter->canvas, &parts[index]);
		counter_draw(counter, now_us);
		kl_canvas_clip_pop(&counter->canvas);
	}

	/* Hands it to the compositor: the whole frame (count 0) or its parts. */
	status = kl_window_present_parts(counter->window, counter->pixels, counter->width, parts, count);
	if (status == EAGAIN) {
		/* The window's size changed under the frame: a new frame of the new size. */
		counter->resized = 1;
	} else if (status != 0) {
		/* Refuses to go on when the frame could not be shown. */
		return -1;
	}

	/* Takes the keys and the presses that met no widget. */
	counter_unclaimed(counter);

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Gives one input of the window to the widgets, or acts on it.
 */
static void
counter_input(
	struct counter *counter,
	const struct kl_window_event *input)
{
	int changed;
	int taken;

	/* Sorts the input by its kind. */
	switch (input->kind) {
	case KL_WINDOW_RESIZE:
		/* The compositor gave a new size. */
		counter->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		/* The close button: the program ends. */
		counter->closed = 1;
		break;
	case KL_WINDOW_MOTION:
		/* The pointer moved: only the light of the buttons under it may change. */
		changed = kl_ui_pointer_motion(counter->ui, input->x, input->y);
		if (changed)
			counter->lit = 1;
		break;
	case KL_WINDOW_LEAVE:
		/* The pointer left: the button it lit goes dark. */
		changed = kl_ui_pointer_leave(counter->ui);
		if (changed)
			counter->lit = 1;
		break;
	default:
		/* A press, a key, a finger: the widgets' input, and the whole frame after it. */
		taken = kl_ui_window_input(counter->ui, input);
		if (taken)
			counter->dirty = 1;
		break;
	}
}

/*
 * Acts on the input no widget took: Up and Down change the number.
 */
static void
counter_unclaimed(
	struct counter *counter)
{
	struct kl_event event;
	int taken;

	/* Takes each input the frame left for the program. */
	for (;;) {
		taken = kl_ui_take(counter->ui, &event);
		if (!taken)
			break;

		/* Only keys are used here; presses on the ground are left alone. */
		if (event.kind != KL_EVENT_KEY)
			continue;

		/* Up is More, Down is Less. */
		if (event.code == KL_KEY_UP) {
			counter->count++;
			counter->dirty = 1;
		} else if (event.code == KL_KEY_DOWN) {
			counter->count--;
			counter->dirty = 1;
		}
	}
}
```

<!-- WRITE (section 4.x, reading the program), in this order:
1. Line 55: a second flag, lit: only the light changed (dirty means the whole frame).
2. Lines 100-103: kl_window_set_pacing with KL_PACING_MOTION (a motion queued right after another replaces it) and
   KL_PACING_FRAME (a frame only once the compositor took the last).  ENOTSUP only for KL_PRESENT_NONE.
3. Lines 286-307 (counter_wait): the dispatch timeout now comes from kl_window_frame_wait: 0 draw now, a positive
   number of milliseconds to wait at most, -1 wait for the compositor (its answer wakes the dispatch).
4. Lines 376-416 (counter_input; the motion and the leave at lines 397-408): motion and leave go to kl_ui_pointer_motion and kl_ui_pointer_leave, whose 1 means
   "the lit widget changed"; everything else goes to kl_ui_window_input and makes the whole frame dirty.  This is the
   place to state the correction: kl_ui_window_input would return 1 for every motion, so it cannot be used to learn
   that only the light changed.
5. Lines 309-374 (counter_frame): wait for the pace (lines 330-334); when only the light changed, kl_ui_take_frame
   gives KL_FRAME_NONE, KL_FRAME_PARTS with up to KL_FRAME_PARTS_MAX rectangles, or KL_FRAME_WHOLE (lines 336-346);
   each part is drawn by running the whole frame's calls under a clip (lines 350-357), so that every widget is still
   recorded; kl_window_present_parts shows the parts, or the whole frame for a count of 0 (line 360).
6. Why a press or a key still draws the whole frame here: the light's parts are all kl_ui_take_frame knows; a change of
   the program's own state is added with kl_ui_damage or kl_ui_damage_whole (not needed here because such changes set
   dirty and draw everything).
-->

<!-- WRITE (section 4.x, the rules): The pacing rules (what each flag does; what frame_wait returns; input other than
motions and the wheel is drawn at once, once).  The damage rules: kl_ui_take_frame takes and forgets the parts;
kl_ui_begin also forgets them, so ask before drawing; a partial frame runs every widget call between kl_ui_begin and
kl_ui_end; only pixels outside the clip are kept.  Pitfall: calling kl_ui_take_frame after drawing (always NONE). -->

<!-- OLD TEXT (chapter 4; reuse the five rules' wording where it is right, then delete.  Two statements in it are wrong
and must not be carried over: (a) "Drawing only what changed" says kl_ui_window_input returns 1 for a motion only when
the lit widget changed, and rule 3 says the input calls including kl_ui_window_input "return 1 when the window must be
drawn again"; kl_ui_window_input returns 1 for every input of the widgets' kinds.  (b) rule 5 says kl_ui_end returns
nonzero "while a widget animates or a scroll flies"; see the list above.  Its snippets (the lone kl_button line, the
frame skeleton, the partial-frame fragment) are replaced by the two programs):
## 4. The user interface and the main loop

### The immediate mode model

`struct kl_ui` is the input and layout record of one window's widgets.
Keiland's widgets are *immediate mode*: a widget is not an object the
program creates and keeps. It is a function the program calls while it draws
a frame, every frame the widget is shown:

```c
if (kl_button(ui, &style, ID_SAVE, &save_rect, "Save", KL_BUTTON_PRIMARY))
	save_document(app_state);
```

That one call draws the button in the frame and records where it is. Its
return value reports what the input did to it since the last frame. There is
no button to create, show, hide or destroy: a button the program does not
call this frame does not exist this frame.

The model rests on five rules.

1. **What a widget shows is the program's state.** The text of a field, a
   switch's position, a slider's value, a list's selection and scroll live
   in the program's own structures (`struct kl_field`, an `int`, a `double`,
   `struct kl_list`), which the program passes to the widget each frame. The
   widget changes them when the input says so. The library keeps no copy that
   could disagree with the program.

2. **A widget is named by an id and an index.** The id is a number the program
   chooses (unique in the window); the index tells apart the widgets of one id
   (the rows of a list, the buttons of a toolbar, the items of a sidebar).
   The library matches a frame's widget with the input by this pair, so the
   pair must stay the same from one frame to the next for the same widget.
   The keyboard's focus is also a pair: `kl_ui_set_focus`, `kl_ui_has_focus`.

3. **Input is resolved against the frame last drawn.** While a frame is
   drawn, each widget records its rectangle (`kl_ui_hit` underneath, which
   the widgets call). Input that arrives before the next frame is matched with
   those records, the last recorded first (it is on top): a press and a
   release on the same widget click it, the wheel goes to the scroll under the
   pointer, a finger's tap to the widget it touched. The input calls
   (`kl_ui_window_input`, or `kl_ui_pointer_motion`, `kl_ui_pointer_button`,
   `kl_ui_key`, `kl_ui_touch_down` ...) return 1 when the window must be drawn
   again.

4. **The result comes back in the next frame.** A click recorded between two
   frames is reported by the widget's call in the next frame (a button
   returns 1, a field returns `KL_FIELD_SUBMITTED`, a list
   `KL_LIST_ACTIVATED`). So after input that returned 1, the program draws a
   frame, and that frame's widget calls are where it learns what happened.
   Input that met no widget (a key no widget took, a press on empty space, a
   long press) is kept for the program; it takes it after the frame with
   `kl_ui_take`.

5. **A frame is drawn when something changed.** Nothing redraws by itself.
   The program draws a frame when the input said so, when its own state
   changed, and while something moves: `kl_ui_end` returns nonzero while a
   widget animates or a scroll flies, and the program then keeps drawing at
   the display's pace.

The window's frame itself (the titlebar, its controls, the menus, the glass
panels) is *not* immediate: it is declarative and retained (section 3), set
when it changes.

### A frame

A frame is drawn between `kl_ui_begin` and `kl_ui_end` with a
`struct kl_style`: the canvas of the frame, the text, the theme and whether the
window stands on glass.

```c
kl_ui_begin(ui, now_us);
/* the ground, then every widget of the frame, in drawing order */
moving = kl_ui_end(ui, now_us);

/* The focused field's input method and the clipboard follow the window. */
kl_ui_window_text(ui, window);

/* What no widget took. */
while (kl_ui_take(ui, &event))
	handle_unclaimed(&event);
```

`kl_ui_window_text` asks the compositor for an input method while the focused
widget takes text and tells it where the caret is; it also ties the text
widgets' Cut, Copy and Paste to the window's clipboard.

### The main loop

The loop waits for input, gives each input to the widgets, and draws a frame
when one is due. Here is a complete program: a window with a counter and two
buttons.

```c
/* counter.c: a window with a number and two buttons. */
#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT		"/usr/share/fonts/keiland.ttf"
#define FALLBACK_FONT	"/usr/share/fonts/keiland-fallback.ttf"

#define ID_LESS		1U
#define ID_MORE		2U

/* What the program keeps: its window, its frame, and the state its widgets show. */
struct counter {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int count;
	int dirty;
	int moving;
	int resized;
	int closed;
};

/* Makes the frame at the window's size; nonzero when it cannot. */
static int
counter_resize(struct counter *c)
{
	uint32_t *pixels;

	if (kl_window_present_resize(c->window, &c->width, &c->height) != 0)
		return -1;
	pixels = malloc((size_t)c->width * c->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;
	if (c->pixels != NULL)
		kl_canvas_release(&c->canvas);
	free(c->pixels);
	c->pixels = pixels;
	if (kl_canvas_init(&c->canvas, c->pixels, c->width, (int)c->width, (int)c->height) != 0)
		return -1;
	c->style.canvas = &c->canvas;
	c->dirty = 1;
	return 0;
}

/* Draws one frame: the ground, the number and the buttons. */
static void
counter_draw(struct counter *c, uint64_t now_us)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	struct kl_rect less;
	struct kl_rect more;
	char number[32];
	int baseline;

	theme = kl_theme_default();
	c->style.theme = theme;
	kl_ui_begin(c->ui, now_us);

	whole.x = 0;
	whole.y = 0;
	whole.width = (int)c->width;
	whole.height = (int)c->height;
	kl_canvas_gradient(&c->canvas, &whole, theme->ground_top, theme->ground_bottom);

	snprintf(number, sizeof(number), "%d", c->count);
	baseline = kl_text_center(theme->text_title, 40, 60);
	kl_text_draw(&c->text, &c->canvas, 40, baseline, number, strlen(number), theme->text_title, 1, theme->text);

	less.x = 40;
	less.y = 120;
	less.width = 100;
	less.height = theme->control_height;
	more = less;
	more.x = 160;

	/* The buttons report the clicks recorded since the last frame. */
	if (kl_button(c->ui, &c->style, ID_LESS, &less, "Less", 0U)) {
		c->count--;
		c->dirty = 1;
	}
	if (kl_button(c->ui, &c->style, ID_MORE, &more, "More", KL_BUTTON_PRIMARY)) {
		c->count++;
		c->dirty = 1;
	}

	c->moving = kl_ui_end(c->ui, now_us);
	kl_ui_window_text(c->ui, c->window);
}

/* Draws and shows a frame; nonzero when it cannot be shown. */
static int
counter_frame(struct counter *c)
{
	int status;

	c->dirty = 0;
	counter_draw(c, kl_clock_us());
	status = kl_window_present(c->window, c->pixels, c->width);
	if (status == EAGAIN)
		c->resized = 1;
	else if (status != 0)
		return -1;
	return 0;
}

/* Gives one input of the window to the widgets, or to the program. */
static void
counter_input(struct counter *c, const struct kl_window_event *input)
{
	if (input->kind == KL_WINDOW_RESIZE) {
		c->resized = 1;
		return;
	}
	if (input->kind == KL_WINDOW_CLOSE) {
		c->closed = 1;
		return;
	}
	if (kl_ui_window_input(c->ui, input))
		c->dirty = 1;
}

int
main(void)
{
	static struct counter c;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;

	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Counter";
	c.app = kl_app_open(&app_options);
	if (c.app == NULL)
		return 1;

	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Counter";
	window_options.width = 320;
	window_options.height = 200;
	window_options.present = KL_PRESENT_VULKAN;
	c.window = kl_app_window_create(c.app, &window_options);
	c.ui = kl_ui_create();
	if (c.window == NULL || c.ui == NULL || kl_text_open(&c.text, FONT, FALLBACK_FONT) != 0)
		return 1;
	c.style.text = &c.text;
	c.style.glass = 0;
	if (counter_resize(&c) != 0)
		return 1;

	while (!c.closed) {
		/* No wait while a frame is due; a frame's time while something moves. */
		timeout = -1;
		if (c.moving)
			timeout = 16;
		if (c.dirty || c.resized)
			timeout = 0;
		if (kl_app_dispatch(c.app, timeout) != 0)
			break;

		/* Every event queued, in order. */
		while (kl_app_take(c.app, &event)) {
			if (event.kind == KL_APP_THEME)
				c.dirty = 1;
			if (event.kind == KL_APP_WINDOW && event.window == c.window)
				counter_input(&c, &event.input);
		}

		if (c.resized) {
			c.resized = 0;
			if (counter_resize(&c) != 0)
				break;
		}
		if ((c.dirty || c.moving) && counter_frame(&c) != 0)
			break;
	}

	kl_ui_destroy(c.ui);
	kl_canvas_release(&c.canvas);
	free(c.pixels);
	kl_text_close(&c.text);
	kl_app_close(c.app);
	return 0;
}
```

Note how a click works here. The press and the release arrive as
`KL_WINDOW_BUTTON` inputs; `kl_ui_window_input` matches them with the button
the last frame recorded and returns 1, so the loop draws a frame; in that
frame `kl_button` returns 1, and the program changes `count` and asks for
one more frame to show the new number.

To build and run it on a Keiland desktop:

```sh
cc -o counter counter.c -lkeiland
./counter
```

A window titled "Counter" opens with the number 0; More and Less change it,
and the close button ends the program. If `kl_app_open` fails with
`EOPNOTSUPP`, the program found no Keiland compositor: check that it runs in
the desktop's session (`WAYLAND_DISPLAY` names the compositor's socket). If the
program ends with status 1 at once, the window, its input or the font could
not be made: check first that the paths in `FONT` and `FALLBACK_FONT` exist.

### The pace of frames

A program that draws on every input can fall behind a fast pointer or a touch
pad. Two settings of the window keep it at the display's pace:

```c
kl_window_set_pacing(window, KL_PACING_MOTION | KL_PACING_FRAME, 0U);
```

- `KL_PACING_MOTION`: a pointer motion queued right after another replaces it,
  so the program sees the latest place, not every step.
- `KL_PACING_FRAME`: a frame is drawn only once the compositor took the last
  one. The program asks `kl_window_frame_wait(window, now_us)` before it draws:
  0 means draw now; a positive number is the most milliseconds to wait (use it
  as the dispatch timeout); -1 means wait for the compositor alone (its answer
  wakes `kl_app_dispatch`). Input that came meanwhile is drawn as one frame in
  its latest state.

### Drawing only what changed

When the pointer only moves from one widget to another, the frame needs only
the two widgets' places drawn again. `kl_ui_pointer_motion` (and
`kl_ui_window_input` for a motion) returns 1 only when the lit widget changed;
the program then asks `kl_ui_take_frame`:

```c
struct kl_rect parts[KL_FRAME_PARTS_MAX];
unsigned count;
unsigned index;
int need;

need = kl_ui_take_frame(ui, parts, KL_FRAME_PARTS_MAX, &count);
if (need == KL_FRAME_WHOLE)
	count = 0;
if (need != KL_FRAME_NONE) {
	/* Each part is drawn with the whole frame's calls, clipped to the part. */
	for (index = 0; index < count; index++) {
		kl_canvas_clip_push(&canvas, &parts[index]);
		draw_frame(app_state);
		kl_canvas_clip_pop(&canvas);
	}
	if (count == 0)
		draw_frame(app_state);
	kl_window_present_parts(window, pixels, width, parts, count);
}
```

`KL_FRAME_NONE` means nothing needs drawing, `KL_FRAME_PARTS` gives up to
`KL_FRAME_PARTS_MAX` rectangles, `KL_FRAME_WHOLE` the whole window. A change
the widgets cannot know of (the program's own content) is added with
`kl_ui_damage(ui, &rect)` or `kl_ui_damage_whole(ui)`. Each partial draw still
runs every widget call of the frame (between `kl_ui_begin` and `kl_ui_end`)
so that the records stay whole; only the pixels outside the clip are kept.

-->

## 5. The widgets

<!-- WRITE (chapter 5, summary): A greeter: a text field for a name, a list of greetings, a Shout switch, a size
slider, four colour swatches the program draws itself, and a Greet button; Greet, Enter in the field, or a double click
or Enter in the list writes the greeting at the bottom; Escape clears it.  It follows chapter 4's structure exactly
(say so; the loop, resize and frame functions are the counter's).  Screenshot keiland-examples/05-widgets.png.  Terms
first defined here: a widget's state struct (struct kl_field, struct kl_list), focus and its order (the order of
drawing), row, ink, hit bits (KL_HIT_*), the program's own widget (kl_ui_hit). -->

<!-- example: keiland-examples/05-widgets.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A greeter: a text field for a name, a list of greetings, a switch, a
 * slider, colour swatches the program draws itself, and a button.  Greet
 * (or Enter in the field, or a double click in the list) writes the
 * greeting at the bottom of the window; Escape clears it.
 */

#include <keiland/keiland.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the desktop's fonts are (/opt/keiland/share/fonts for a Linux build under /opt/keiland). */
#ifndef FONT_DIR
#define FONT_DIR	"/usr/share/fonts"
#endif
#define FONT		FONT_DIR "/keiland.ttf"
#define FALLBACK_FONT	FONT_DIR "/keiland-fallback.ttf"

/* The widgets' ids; the swatches and the list's rows share one id each and differ by their index. */
#define ID_NAME		1U
#define ID_GREETINGS	2U
#define ID_SHOUT	3U
#define ID_SIZE		4U
#define ID_SWATCH	5U
#define ID_GREET	6U

/* The layout, in pixels. */
#define MARGIN		24
#define COLUMN_WIDTH	240
#define SWATCH_SIZE	32
#define GREETING_MAX	600

/*
 * Everything the program keeps.  The widgets' state is here, in the
 * program's own variables: the field's text (name), the list's selection
 * and scroll (greetings), the switch (shout), the slider (size) and the
 * swatch chosen (swatch).  The widgets read and change them in each frame.
 */
struct greeter {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	struct kl_field name;
	struct kl_list greetings;
	int shout;
	double size;
	size_t swatch;
	char greeting[GREETING_MAX];
	int dirty;
	int moving;
	int resized;
	int closed;
};

static int greeter_resize(struct greeter *greeter);
static void greeter_draw(struct greeter *greeter, uint64_t now_us);
static void greeter_controls(struct greeter *greeter, const struct kl_theme *theme);
static void greeter_list(struct greeter *greeter, const struct kl_theme *theme);
static void greeter_swatches(struct greeter *greeter, const struct kl_theme *theme, int y);
static void greeter_greet(struct greeter *greeter);
static int greeter_frame(struct greeter *greeter);
static void greeter_input(struct greeter *greeter, const struct kl_window_event *input);
static void greeter_unclaimed(struct greeter *greeter);

/* The greetings the list offers. */
static const char *const greeting_table[] = {
	"Hello", "Good morning", "Good afternoon", "Good evening", "Welcome", "Hi", "Greetings", "Howdy", "Cheers", "Good night"
};

#define GREETING_COUNT	(sizeof(greeting_table) / sizeof(greeting_table[0]))

/* The colours the swatches offer for the greeting. */
static const kl_color swatch_table[] = {
	KL_RGB(0x1c1c1e), KL_RGB(0x0a84ff), KL_RGB(0x30b94d), KL_RGB(0xff375f)
};

#define SWATCH_COUNT	(sizeof(swatch_table) / sizeof(swatch_table[0]))

int
main(void)
{
	static struct greeter greeter;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.Greeter";
	greeter.app = kl_app_open(&app_options);
	if (greeter.app == NULL) {
		fprintf(stderr, "greeter: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Greeter";
	window_options.width = 560;
	window_options.height = 400;
	window_options.present = KL_PRESENT_VULKAN;
	greeter.window = kl_app_window_create(greeter.app, &window_options);
	if (greeter.window == NULL) {
		fprintf(stderr, "greeter: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Makes the widgets' record and opens the font. */
	greeter.ui = kl_ui_create();
	if (greeter.ui == NULL)
		return 1;
	status = kl_text_open(&greeter.text, FONT, FALLBACK_FONT);
	if (status != 0) {
		fprintf(stderr, "greeter: no font %s (%s)\n", FONT, strerror(status));
		return 1;
	}
	greeter.style.text = &greeter.text;
	greeter.style.glass = 0;

	/* Gives the widgets' state its first values: a name, the first greeting, a size. */
	kl_field_set(&greeter.name, "World");
	status = kl_list_init(&greeter.greetings);
	if (status != 0)
		return 1;
	greeter.greetings.selected = 0;
	greeter.size = 24.0;

	/* Makes the first frame. */
	status = greeter_resize(&greeter);
	if (status != 0)
		return 1;

	/* Runs until the window is closed. */
	while (!greeter.closed) {
		/* Waits for the desktop: not at all while a frame is due, a frame's time while something moves. */
		timeout = -1;
		if (greeter.moving)
			timeout = 16;
		if (greeter.dirty || greeter.resized)
			timeout = 0;
		status = kl_app_dispatch(greeter.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(greeter.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the theme's colours are new. */
			if (event.kind == KL_APP_THEME)
				greeter.dirty = 1;

			/* An input of the window. */
			if (event.kind == KL_APP_WINDOW && event.window == greeter.window)
				greeter_input(&greeter, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (greeter.resized) {
			greeter.resized = 0;
			status = greeter_resize(&greeter);
			if (status != 0)
				break;
		}

		/* Something changed or moves: draws and shows a frame. */
		if (greeter.dirty || greeter.moving) {
			status = greeter_frame(&greeter);
			if (status != 0)
				break;
		}
	}

	/* Frees the list's scroll, the widgets' record, the frame and the font, and closes the rest. */
	kl_list_release(&greeter.greetings);
	kl_ui_destroy(greeter.ui);
	kl_canvas_release(&greeter.canvas);
	free(greeter.pixels);
	kl_text_close(&greeter.text);
	kl_app_close(greeter.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
greeter_resize(
	struct greeter *greeter)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(greeter->window, &greeter->width, &greeter->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)greeter->width * greeter->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones; the widgets draw on it. */
	if (greeter->pixels != NULL)
		kl_canvas_release(&greeter->canvas);
	free(greeter->pixels);
	greeter->pixels = pixels;
	status = kl_canvas_init(&greeter->canvas, greeter->pixels, greeter->width, (int)greeter->width, (int)greeter->height);
	if (status != 0)
		return -1;
	greeter->style.canvas = &greeter->canvas;

	/* Succeeded: the new frame is drawn next. */
	greeter->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the ground, the controls on the left, the list on the
 * right, and the greeting at the bottom.
 */
static void
greeter_draw(
	struct greeter *greeter,
	uint64_t now_us)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	int baseline;

	/* Starts the frame. */
	theme = kl_theme_default();
	greeter->style.theme = theme;
	kl_ui_begin(greeter->ui, now_us);

	/* Paints the ground. */
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)greeter->width;
	whole.height = (int)greeter->height;
	kl_canvas_gradient(&greeter->canvas, &whole, theme->ground_top, theme->ground_bottom);

	/* Draws the widgets, in the order Tab moves the keyboard's focus through them. */
	greeter_controls(greeter, theme);
	greeter_list(greeter, theme);

	/* Writes the greeting in the colour and the size chosen. */
	baseline = (int)greeter->height - MARGIN;
	(void)kl_text_draw(&greeter->text, &greeter->canvas, MARGIN, baseline, greeter->greeting, strlen(greeter->greeting), (unsigned)greeter->size, 1, swatch_table[greeter->swatch]);

	/* Ends the frame: nonzero while the list's scroll glides or a finger is down. */
	greeter->moving = kl_ui_end(greeter->ui, now_us);

	/* Asks for the input method while the name field has the keyboard, and ties the field to the clipboard. */
	kl_ui_window_text(greeter->ui, greeter->window);
}

/*
 * Draws the left column: the name field, the switch, the slider, the
 * swatches and the Greet button, and acts on what each reports.
 */
static void
greeter_controls(
	struct greeter *greeter,
	const struct kl_theme *theme)
{
	struct kl_rect rect;
	unsigned field;
	int changed;
	int clicked;
	int baseline;

	/* The name: Enter in the field greets at once. */
	rect.x = MARGIN;
	rect.y = MARGIN;
	rect.width = COLUMN_WIDTH;
	rect.height = theme->control_height;
	field = kl_field(greeter->ui, &greeter->style, ID_NAME, &rect, &greeter->name, "Your name");
	if ((field & KL_FIELD_SUBMITTED) != 0U)
		greeter_greet(greeter);

	/* Shout: the switch changes the variable it is given. */
	rect.y += rect.height + 16;
	changed = kl_switch(greeter->ui, &greeter->style, ID_SHOUT, MARGIN, rect.y, &greeter->shout, 0U);
	if (changed)
		greeter_greet(greeter);
	baseline = kl_text_center(theme->text_body, rect.y, theme->switch_height);
	(void)kl_text_draw(&greeter->text, &greeter->canvas, MARGIN + theme->switch_width + 12, baseline, "Shout", 5U, theme->text_body, 0, theme->text);

	/* The greeting's size: the slider changes the variable it is given. */
	rect.y += theme->switch_height + 16;
	(void)kl_slider(greeter->ui, &greeter->style, ID_SIZE, &rect, 16.0, 48.0, 1.0, &greeter->size);

	/* The colour, from the program's own swatches. */
	rect.y += rect.height + 16;
	greeter_swatches(greeter, theme, rect.y);

	/* Greet. */
	rect.y += SWATCH_SIZE + 16;
	rect.width = kl_button_width(&greeter->style, "Greet");
	clicked = kl_button(greeter->ui, &greeter->style, ID_GREET, &rect, "Greet", KL_BUTTON_PRIMARY);
	if (clicked)
		greeter_greet(greeter);
}

/*
 * Draws the list of greetings on the right: the list draws each row's
 * ground and takes its clicks; the program draws the row's text.
 */
static void
greeter_list(
	struct greeter *greeter,
	const struct kl_theme *theme)
{
	struct kl_rect rect;
	struct kl_rect row;
	unsigned changes;
	size_t first;
	size_t last;
	size_t index;
	kl_color ink;
	int baseline;

	/* Starts the list: the keys it took, and which rows show. */
	rect.x = MARGIN + COLUMN_WIDTH + 32;
	rect.y = MARGIN;
	rect.width = COLUMN_WIDTH;
	rect.height = 6 * theme->row_height;
	changes = kl_list_begin(greeter->ui, &greeter->style, ID_GREETINGS, &rect, &greeter->greetings, GREETING_COUNT, &first, &last);

	/* Draws the rows that show: the ground by the list, the text by the program in the ink it gives. */
	for (index = first; index < last; index++) {
		changes |= kl_list_row(greeter->ui, &greeter->style, ID_GREETINGS, &rect, &greeter->greetings, index, &row, &ink);
		baseline = kl_text_center(theme->text_body, row.y, row.height);
		(void)kl_text_draw(&greeter->text, &greeter->canvas, row.x + 12, baseline, greeting_table[index], strlen(greeting_table[index]), theme->text_body, 0, ink);
	}

	/* Ends the list. */
	kl_list_end(greeter->ui, &greeter->style, &rect, &greeter->greetings);

	/* A double click or Enter on a greeting greets with it. */
	if ((changes & KL_LIST_ACTIVATED) != 0U)
		greeter_greet(greeter);
}

/*
 * Draws the colour swatches, which are not a widget of the library: the
 * program draws each and records it with kl_ui_hit (one id, an index per
 * swatch) to learn what the input did to it.
 */
static void
greeter_swatches(
	struct greeter *greeter,
	const struct kl_theme *theme,
	int y)
{
	struct kl_rect rect;
	unsigned hit;
	size_t index;

	/* One swatch after another along the row at y. */
	rect.x = MARGIN;
	rect.y = y;
	rect.width = SWATCH_SIZE;
	rect.height = SWATCH_SIZE;
	for (index = 0; index < SWATCH_COUNT; index++) {
		/* Records it, and learns whether it was clicked since the last frame or is under the pointer. */
		hit = kl_ui_hit(greeter->ui, ID_SWATCH, (uint32_t)index, &rect);
		if ((hit & KL_HIT_CLICKED) != 0U) {
			greeter->swatch = index;
			greeter->dirty = 1;
		}

		/* Draws it, ringed when chosen, lit under the pointer. */
		kl_canvas_round(&greeter->canvas, (float)rect.x, (float)rect.y, (float)rect.width, (float)rect.height, 8.0f, swatch_table[index]);
		if (index == greeter->swatch) {
			kl_canvas_round_border(&greeter->canvas, (float)rect.x - 3.0f, (float)rect.y - 3.0f, (float)rect.width + 6.0f, (float)rect.height + 6.0f, 10.0f, 2.0f, theme->accent);
		} else if ((hit & KL_HIT_HOT) != 0U) {
			kl_canvas_round_border(&greeter->canvas, (float)rect.x - 3.0f, (float)rect.y - 3.0f, (float)rect.width + 6.0f, (float)rect.height + 6.0f, 10.0f, 1.0f, theme->separator);
		}
		rect.x += SWATCH_SIZE + 12;
	}
}

/*
 * Writes the greeting from the widgets' state: the greeting chosen in the
 * list, the name in the field, in capitals when the switch is on.
 */
static void
greeter_greet(
	struct greeter *greeter)
{
	const char *word;
	size_t index;

	/* The greeting chosen, or the first. */
	word = greeting_table[0];
	if (greeter->greetings.selected >= 0)
		word = greeting_table[greeter->greetings.selected];

	/* The greeting and the name. */
	snprintf(greeter->greeting, sizeof(greeter->greeting), "%s, %s!", word, greeter->name.text);

	/* In capitals when shouting. */
	if (greeter->shout) {
		for (index = 0; greeter->greeting[index] != '\0'; index++)
			greeter->greeting[index] = (char)toupper((unsigned char)greeter->greeting[index]);
	}

	/* The frame shows it. */
	greeter->dirty = 1;
}

/*
 * Draws and shows a frame, then takes the input no widget took; nonzero
 * when the frame cannot be shown.
 */
static int
greeter_frame(
	struct greeter *greeter)
{
	int status;

	/* Draws the frame; what the widgets report there may ask for one more. */
	greeter->dirty = 0;
	greeter_draw(greeter, kl_clock_us());

	/* Hands it to the compositor. */
	status = kl_window_present(greeter->window, greeter->pixels, greeter->width);
	if (status == EAGAIN) {
		/* The window's size changed under the frame: a new frame of the new size. */
		greeter->resized = 1;
	} else if (status != 0) {
		/* Refuses to go on when the frame could not be shown. */
		return -1;
	}

	/* Takes the keys and the presses that met no widget. */
	greeter_unclaimed(greeter);

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Gives one input of the window to the widgets, or acts on it.
 */
static void
greeter_input(
	struct greeter *greeter,
	const struct kl_window_event *input)
{
	int taken;

	/* The window's own inputs: a new size, the close button. */
	if (input->kind == KL_WINDOW_RESIZE) {
		greeter->resized = 1;
		return;
	}
	if (input->kind == KL_WINDOW_CLOSE) {
		greeter->closed = 1;
		return;
	}

	/* The pointer, the keys, the fingers and an input method's text: the widgets'. */
	taken = kl_ui_window_input(greeter->ui, input);
	if (taken)
		greeter->dirty = 1;
}

/*
 * Acts on the input no widget took: Escape clears the greeting.  The
 * keys the focused field or list takes never come here.
 */
static void
greeter_unclaimed(
	struct greeter *greeter)
{
	struct kl_event event;
	int taken;

	/* Takes each input the frame left for the program. */
	for (;;) {
		taken = kl_ui_take(greeter->ui, &event);
		if (!taken)
			break;

		/* Escape clears the greeting. */
		if (event.kind == KL_EVENT_KEY && event.code == KL_KEY_ESC) {
			greeter->greeting[0] = '\0';
			greeter->dirty = 1;
		}
	}
}
```

<!-- WRITE (chapter 5, reading the program), in this order:
1. Lines 43-69 (struct greeter): every widget's state is a member: name (struct kl_field: text, caret, selection),
   greetings (struct kl_list: count, selected, scroll), shout (int), size (double), swatch (index).  This is rule 2 of
   chapter 4 made concrete.
2. Lines 139-144: the state is given its first values before the first frame (kl_field_set, kl_list_init and
   selected); lines 195-196: kl_list_release frees the list's scroll.
3. Lines 267-269: the order of the calls is the order of drawing and the order Tab moves the focus.
4. Lines 297-304: kl_field with KL_FIELD_CHANGED, _SUBMITTED, _CANCELLED bits; the field takes the keys and the input
   method's text while it has the focus; kl_ui_window_text (line 279) is what asks the compositor for the input method
   and ties the field's Cut, Copy and Paste to the window's clipboard.
5. Lines 306-316: kl_switch and kl_slider change the variable they are given and return 1 when they did.
6. Lines 330-368 (greeter_list): kl_list_begin says which rows show (first, last) and takes the keys; kl_list_row draws
   a row's ground and gives the rectangle and the ink for the program's text; kl_list_end ends the clip and draws the
   scroll bar.  OR the results of kl_list_begin and every kl_list_row (lines 353 and 357): a key's activation comes
   from kl_list_begin, a double click's from kl_list_row.  (The earlier version's snippet looked at kl_list_begin's
   result alone and so missed double clicks; do not carry it over.)
7. Lines 370-407 (greeter_swatches): a widget of the program's own: it draws with the canvas and records itself with
   kl_ui_hit(ui, id, index, rect), one id and an index per swatch, and reads KL_HIT_CLICKED and KL_HIT_HOT.  Define
   "index" in use here, pointing back to chapter 4's rule 3.
8. Lines 409-436 (greeter_greet): a widget's result is acted on where it is reported; the program reads the other
   widgets' state from its own struct, not from the widgets.
9. Lines 495-518 (greeter_unclaimed): only keys no widget took come here: Escape in the field cancels the field (it
   never arrives here), Escape elsewhere clears the greeting.
-->

<!-- WRITE (chapter 5, the rules): The table of widgets from the OLD TEXT (call, what it shows, what it returns), with a
column "in the example" pointing to the lines that use it, or "-" for the ones the example does not use (icon button,
text area, sidebar, panel, card, header, row, dialog, chip, progress).  Then: focus (a click or tap gives it to a
widget that takes keys; Tab and Shift+Tab in drawing order; keys no focused widget took become KL_EVENT_KEY); the
program's own widgets (kl_ui_hit, kl_ui_hit_still, the KL_HIT_* bits, KL_HIT_DOUBLE, KL_HIT_TOUCHED); scrolling content
of the program's own (struct kl_scroll, kl_ui_scroll_region; no example, keep the OLD TEXT's paragraph); the canvas,
text and icon calls for the program's own drawing.  Link keiland_widgets.h, keiland_scroll.h, keiland_canvas.h,
keiland_text.h, keiland_icon.h. -->

<!-- WRITE (chapter 5, pitfalls and next): Keeping a widget's state in a local variable (it is lost every frame);
reading a list's double click from kl_list_begin alone; giving rows ids by their position on screen; forgetting
kl_ui_window_text (no input method, no clipboard in fields).  Next: the menus, which go back to the declarative model of
chapter 3. -->

<!-- OLD TEXT (chapter 5; reuse the table and the paragraphs on focus, kl_ui_hit and scrolling, then delete; its two
snippets are replaced by the program, and its list snippet is wrong, see above):
## 5. The widgets

Every widget takes the `struct kl_ui` and the frame's `struct kl_style`, an id
(and an index where a widget has several), its rectangle, and the program's
state it shows. They draw in the Kei look of the theme (`kl_theme_default()`),
light or dark as the desktop is, on glass when `style.glass` is set.

| call | what it shows | returns |
| --- | --- | --- |
| `kl_button(ui, style, id, rect, label, flags)` | a push button; `KL_BUTTON_PRIMARY`, `_DANGER`, `_DISABLED` | 1 when pressed since the last frame |
| `kl_icon_button(ui, style, id, index, rect, icon, pixels, flags)` | a button that is a picture (`KL_ICON_*`); `KL_BUTTON_ROUND`, `_QUIET` | 1 when pressed |
| `kl_switch(ui, style, id, x, y, &on, flags)` | an on/off switch over an `int` | 1 when `on` changed |
| `kl_slider(ui, style, id, rect, min, max, step, &value)` | a slider over a `double` | 1 when `value` changed |
| `kl_field(ui, style, id, rect, &field, placeholder)` | a one-line text field over a `struct kl_field` | `KL_FIELD_CHANGED`, `_SUBMITTED`, `_CANCELLED` bits |
| `kl_text_area(ui, style, id, rect, &area, placeholder)` | text of several lines over a `struct kl_text_area` | `KL_FIELD_*` bits |
| `kl_list_begin` / `kl_list_row` / `kl_list_end` | a scrolling list over a `struct kl_list` | `KL_LIST_SELECTED`, `_ACTIVATED` bits |
| `kl_sidebar_section`, `kl_sidebar_item(ui, style, id, index, rect, icon, label, current)` | a sidebar's title and places | 1 when an item was chosen |
| `kl_panel`, `kl_card`, `kl_header`, `kl_row` | the sidebar's and content's panels, cards with titles, a page's header, a card's rows of a label and a value | the edge below what they drew |
| `kl_dialog(ui, style, id, area, title, body, labels, count)` | a question over the window with its buttons | the index of the button chosen, or -1 |
| `kl_chip(style, x, bottom, message)` | a short notice | — |
| `kl_progress(style, rect, fraction, now_us)` | a progress bar (a negative fraction moves without an end) | — |

A text field keeps its text in the program's structure:

```c
static struct kl_field name;	/* zeroed: empty, caret at 0 */
unsigned what;

kl_field_set(&name, "Guest");	/* once, before the first frame */

/* in each frame */
what = kl_field(ui, &style, ID_NAME, &name_rect, &name, "Your name");
if (what & KL_FIELD_SUBMITTED)
	greet(name.text);
```

A list draws only the rows in sight. The program says how many rows there are;
the list tells which ones to draw, and the program draws each row's content in
the rectangle and ink the list gives:

```c
size_t first;
size_t last;
size_t index;
struct kl_rect row;
kl_color ink;
unsigned what;

what = kl_list_begin(ui, &style, ID_FILES, &list_rect, &list, file_count, &first, &last);
for (index = first; index < last; index++) {
	kl_list_row(ui, &style, ID_FILES, &list_rect, &list, index, &row, &ink);
	kl_text_draw(style.text, style.canvas, row.x + 12, kl_text_center(style.theme->text_body, row.y, row.height),
		     names[index], strlen(names[index]), style.theme->text_body, 0, ink);
}
kl_list_end(ui, &style, &list_rect, &list);
if (what & KL_LIST_ACTIVATED)
	open_file(names[list.selected]);
```

`struct kl_list` holds the selection and the scroll; `kl_list_init` and
`kl_list_release` make and free it.

The keyboard's focus moves among the widgets that take keys (fields, text
areas, lists) by a click or a tap, and by Tab and Shift+Tab in the order they
were drawn. The keys the focused widget does not take, and every key while no
widget has the focus, come back from `kl_ui_take` as `KL_EVENT_KEY`.

A program's own drawing between the widgets uses the canvas
(`kl_canvas_fill`, `kl_canvas_round`, `kl_canvas_line`, `kl_canvas_image`
...), the text (`kl_text_draw`, `kl_text_width`, `kl_text_metrics`) and the
icons (`kl_icon_draw`). A part of the program's own that takes clicks records
itself with `kl_ui_hit(ui, id, index, rect)`, which returns the same
`KL_HIT_*` bits the widgets read (`KL_HIT_HOT` under the pointer,
`KL_HIT_ACTIVE` held, `KL_HIT_CLICKED` clicked since the last frame,
`KL_HIT_DOUBLE`). Content larger than its place scrolls with a
`struct kl_scroll` (`kl_scroll_init`, `kl_scroll_set_size`) recorded each
frame with `kl_ui_scroll_region`; the wheel, a touch pad and a finger then
move it, and the program draws its content at the scroll's `x` and `y`.

-->

## 6. Menus

<!-- WRITE (chapter 6, summary): The counter of chapter 4 with the window's menu (File: Quit; Edit: Copy, Paste, Less,
More, Reset; View: Small Number, Large Number) with shortcuts, and a context menu on a right click or a long press.
The buttons, the keys and the menus all ask for the same actions.  Screenshot keiland-examples/06-menu.png (the Edit
menu open, when it can be taken).  Terms first defined here: menu table, item, submenu, menu role, shortcut,
context menu.  "Action" and "action state" are chapter 3's. -->

<!-- example: keiland-examples/06-menu.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The counter with menus: the window's menu (File, Edit, View) with
 * shortcuts, and a context menu on a right click or a long press.  Copy
 * and Paste carry the number through the clipboard; View chooses the
 * number's size.  The menus, the keys and the buttons share one set of
 * actions.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the desktop's fonts are (/opt/keiland/share/fonts for a Linux build under /opt/keiland). */
#ifndef FONT_DIR
#define FONT_DIR	"/usr/share/fonts"
#endif
#define FONT		FONT_DIR "/keiland.ttf"
#define FALLBACK_FONT	FONT_DIR "/keiland-fallback.ttf"

/* The widgets' ids. */
#define ID_LESS		1U
#define ID_RESET	2U
#define ID_MORE		3U

/* The actions: what a menu item, a key or a button asks the program to do. */
#define ACTION_QUIT	1U
#define ACTION_COPY	2U
#define ACTION_PASTE	3U
#define ACTION_LESS	4U
#define ACTION_MORE	5U
#define ACTION_RESET	6U
#define ACTION_SMALL	7U
#define ACTION_LARGE	8U

/* The buttons' width and the room around them, in pixels. */
#define BUTTON_WIDTH	96
#define MARGIN		24

/* The longest number the clipboard gives. */
#define NUMBER_MAX	32U

/*
 * Everything the program keeps: its connection and window, the widgets'
 * record, the font, the frame, the state the window shows (the count and
 * whether the number is large), and what the main loop has to do next.
 */
struct counter {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int count;
	int large;
	int dirty;
	int moving;
	int resized;
	int closed;
};

static int counter_resize(struct counter *counter);
static void counter_draw(struct counter *counter, uint64_t now_us);
static int counter_frame(struct counter *counter);
static void counter_input(struct counter *counter, const struct kl_window_event *input);
static void counter_unclaimed(struct counter *counter);
static void counter_act(struct counter *counter, uint32_t action);
static void counter_states(struct counter *counter);

/*
 * The window's menu.  Each row is an item: its ID, its parent (KL_MENU_ROOT
 * for the menu bar's), its type, label and action, the role that tells the
 * desktop what it means, and its shortcut (modifiers and an XKB keysym).
 * A submenu's items follow it.
 */
static const struct kl_menu_entry menu_table[] = {
	/* id, parent, type, label, action, role, modifiers, keysym */
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 2U, 1U, KL_MENU_ITEM_NORMAL, "Quit", ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ 3U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Edit", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 4U, 3U, KL_MENU_ITEM_NORMAL, "Copy", ACTION_COPY, KL_MENU_ROLE_COPY, KL_MENU_CTRL, 'c' },
	{ 5U, 3U, KL_MENU_ITEM_NORMAL, "Paste", ACTION_PASTE, KL_MENU_ROLE_PASTE, KL_MENU_CTRL, 'v' },
	{ 6U, 3U, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 7U, 3U, KL_MENU_ITEM_NORMAL, "Less", ACTION_LESS, KL_MENU_ROLE_NONE, KL_MENU_CTRL, '-' },
	{ 8U, 3U, KL_MENU_ITEM_NORMAL, "More", ACTION_MORE, KL_MENU_ROLE_NONE, KL_MENU_CTRL, '=' },
	{ 9U, 3U, KL_MENU_ITEM_NORMAL, "Reset", ACTION_RESET, KL_MENU_ROLE_NONE, KL_MENU_CTRL, '0' },
	{ 10U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "View", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 11U, 10U, KL_MENU_ITEM_RADIO, "Small Number", ACTION_SMALL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 12U, 10U, KL_MENU_ITEM_RADIO, "Large Number", ACTION_LARGE, KL_MENU_ROLE_NONE, 0U, 0U }
};

/* The context menu: top-level items only, shown once where the press was. */
static const struct kl_menu_entry context_table[] = {
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Copy", ACTION_COPY, KL_MENU_ROLE_COPY, 0U, 0U },
	{ 2U, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Paste", ACTION_PASTE, KL_MENU_ROLE_PASTE, 0U, 0U },
	{ 3U, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Reset", ACTION_RESET, KL_MENU_ROLE_NONE, 0U, 0U }
};

int
main(void)
{
	static struct counter counter;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = "org.example.MenuCounter";
	counter.app = kl_app_open(&app_options);
	if (counter.app == NULL) {
		fprintf(stderr, "counter: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Opens the window. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = "Counter";
	window_options.width = 400;
	window_options.height = 240;
	window_options.present = KL_PRESENT_VULKAN;
	counter.window = kl_app_window_create(counter.app, &window_options);
	if (counter.window == NULL) {
		fprintf(stderr, "counter: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Gives the window its menu; without the desktop's menus the keys and buttons still work. */
	status = kl_window_set_menu(counter.window, menu_table, sizeof(menu_table) / sizeof(menu_table[0]));
	if (status != 0)
		fprintf(stderr, "counter: no menu (%s)\n", strerror(status));

	/* Ticks the size shown and disables what cannot be done now. */
	counter_states(&counter);

	/* Makes the widgets' record and opens the font. */
	counter.ui = kl_ui_create();
	if (counter.ui == NULL)
		return 1;
	status = kl_text_open(&counter.text, FONT, FALLBACK_FONT);
	if (status != 0) {
		fprintf(stderr, "counter: no font %s (%s)\n", FONT, strerror(status));
		return 1;
	}
	counter.style.text = &counter.text;
	counter.style.glass = 0;

	/* Makes the first frame. */
	status = counter_resize(&counter);
	if (status != 0)
		return 1;

	/* Runs until the window is closed or Quit is chosen. */
	while (!counter.closed) {
		/* Waits for the desktop: not at all while a frame is due, a frame's time while something moves. */
		timeout = -1;
		if (counter.moving)
			timeout = 16;
		if (counter.dirty || counter.resized)
			timeout = 0;
		status = kl_app_dispatch(counter.app, timeout);
		if (status != 0)
			break;

		/* Takes every event queued, in order: the menus' choices come among the keys and the clicks. */
		for (;;) {
			taken = kl_app_take(counter.app, &event);
			if (!taken)
				break;

			/* The appearance changed: the theme's colours are new. */
			if (event.kind == KL_APP_THEME)
				counter.dirty = 1;

			/* An input of the window. */
			if (event.kind == KL_APP_WINDOW && event.window == counter.window)
				counter_input(&counter, &event.input);
		}

		/* A new size: a new frame of that size. */
		if (counter.resized) {
			counter.resized = 0;
			status = counter_resize(&counter);
			if (status != 0)
				break;
		}

		/* Something changed or moves: draws and shows a frame. */
		if (counter.dirty || counter.moving) {
			status = counter_frame(&counter);
			if (status != 0)
				break;
		}
	}

	/* Frees the widgets' record, the frame and the font, and closes the window and the connection. */
	kl_ui_destroy(counter.ui);
	kl_canvas_release(&counter.canvas);
	free(counter.pixels);
	kl_text_close(&counter.text);
	kl_app_close(counter.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
counter_resize(
	struct counter *counter)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(counter->window, &counter->width, &counter->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)counter->width * counter->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones; the widgets draw on it. */
	if (counter->pixels != NULL)
		kl_canvas_release(&counter->canvas);
	free(counter->pixels);
	counter->pixels = pixels;
	status = kl_canvas_init(&counter->canvas, counter->pixels, counter->width, (int)counter->width, (int)counter->height);
	if (status != 0)
		return -1;
	counter->style.canvas = &counter->canvas;

	/* Succeeded: the new frame is drawn next. */
	counter->dirty = 1;
	return 0;
}

/*
 * Draws one frame: the ground, the number at the size chosen, and the
 * three buttons, which ask for the same actions as the menu.
 */
static void
counter_draw(
	struct counter *counter,
	uint64_t now_us)
{
	const struct kl_theme *theme;
	struct kl_rect whole;
	struct kl_rect less;
	struct kl_rect reset;
	struct kl_rect more;
	unsigned reset_flags;
	unsigned pixels;
	char number[32];
	int baseline;
	int clicked;

	/* Starts the frame. */
	theme = kl_theme_default();
	counter->style.theme = theme;
	kl_ui_begin(counter->ui, now_us);

	/* Paints the ground. */
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)counter->width;
	whole.height = (int)counter->height;
	kl_canvas_gradient(&counter->canvas, &whole, theme->ground_top, theme->ground_bottom);

	/* Draws the number at the size View chose. */
	pixels = theme->text_title;
	if (counter->large)
		pixels = theme->text_title * 2U;
	snprintf(number, sizeof(number), "%d", counter->count);
	baseline = kl_text_center(pixels, MARGIN, 64);
	(void)kl_text_draw(&counter->text, &counter->canvas, MARGIN, baseline, number, strlen(number), pixels, 1, theme->text);

	/* Lays out the three buttons in a row under it. */
	less.x = MARGIN;
	less.y = MARGIN + 80;
	less.width = BUTTON_WIDTH;
	less.height = theme->control_height;
	reset = less;
	reset.x = less.x + BUTTON_WIDTH + 12;
	more = reset;
	more.x = reset.x + BUTTON_WIDTH + 12;

	/* The buttons ask for the menu's actions. */
	clicked = kl_button(counter->ui, &counter->style, ID_LESS, &less, "Less", 0U);
	if (clicked)
		counter_act(counter, ACTION_LESS);
	reset_flags = 0U;
	if (counter->count == 0)
		reset_flags = KL_BUTTON_DISABLED;
	clicked = kl_button(counter->ui, &counter->style, ID_RESET, &reset, "Reset", reset_flags);
	if (clicked)
		counter_act(counter, ACTION_RESET);
	clicked = kl_button(counter->ui, &counter->style, ID_MORE, &more, "More", KL_BUTTON_PRIMARY);
	if (clicked)
		counter_act(counter, ACTION_MORE);

	/* Ends the frame. */
	counter->moving = kl_ui_end(counter->ui, now_us);
	kl_ui_window_text(counter->ui, counter->window);
}

/*
 * Draws and shows a frame, then takes the input no widget took; nonzero
 * when the frame cannot be shown.
 */
static int
counter_frame(
	struct counter *counter)
{
	int status;

	/* Draws the frame. */
	counter->dirty = 0;
	counter_draw(counter, kl_clock_us());

	/* Hands it to the compositor. */
	status = kl_window_present(counter->window, counter->pixels, counter->width);
	if (status == EAGAIN) {
		/* The window's size changed under the frame: a new frame of the new size. */
		counter->resized = 1;
	} else if (status != 0) {
		/* Refuses to go on when the frame could not be shown. */
		return -1;
	}

	/* Takes the long presses and the keys that met no widget. */
	counter_unclaimed(counter);

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Acts on one input of the window: a choice of a menu, a right click, or
 * the widgets' input.
 */
static void
counter_input(
	struct counter *counter,
	const struct kl_window_event *input)
{
	int status;
	int taken;

	/* Sorts the input by its kind. */
	switch (input->kind) {
	case KL_WINDOW_RESIZE:
		/* The compositor gave a new size. */
		counter->resized = 1;
		break;
	case KL_WINDOW_CLOSE:
		/* The close button: the program ends. */
		counter->closed = 1;
		break;
	case KL_WINDOW_ACTION:
		/* An item of the window's menu or the context menu, chosen by the pointer or its shortcut. */
		counter_act(counter, input->code);
		break;
	case KL_WINDOW_BUTTON:
		/* A right press opens the context menu there; the widgets take the main button. */
		if (input->code == KL_BUTTON_RIGHT && input->pressed) {
			status = kl_window_popup_menu(counter->window, context_table, sizeof(context_table) / sizeof(context_table[0]), (int)input->x, (int)input->y);
			if (status != 0)
				fprintf(stderr, "counter: no context menu (%s)\n", strerror(status));
			break;
		}
		taken = kl_ui_window_input(counter->ui, input);
		if (taken)
			counter->dirty = 1;
		break;
	default:
		/* The pointer, the keys and the fingers: the widgets'. */
		taken = kl_ui_window_input(counter->ui, input);
		if (taken)
			counter->dirty = 1;
		break;
	}
}

/*
 * Acts on the input no widget took: a finger's long press opens the
 * context menu, Up and Down change the number.
 */
static void
counter_unclaimed(
	struct counter *counter)
{
	struct kl_event event;
	int taken;

	/* Takes each input the frame left for the program. */
	for (;;) {
		taken = kl_ui_take(counter->ui, &event);
		if (!taken)
			break;

		/* A long press is the fingers' right click. */
		if (event.kind == KL_EVENT_LONG_PRESS) {
			(void)kl_window_popup_menu(counter->window, context_table, sizeof(context_table) / sizeof(context_table[0]), (int)event.x, (int)event.y);
			continue;
		}

		/* Up is More, Down is Less. */
		if (event.kind == KL_EVENT_KEY && event.code == KL_KEY_UP)
			counter_act(counter, ACTION_MORE);
		else if (event.kind == KL_EVENT_KEY && event.code == KL_KEY_DOWN)
			counter_act(counter, ACTION_LESS);
	}
}

/*
 * Does what an action asks, whichever menu, key or button asked it.
 */
static void
counter_act(
	struct counter *counter,
	uint32_t action)
{
	char number[NUMBER_MAX];
	size_t length;
	size_t digits;

	/* Sorts the action. */
	switch (action) {
	case ACTION_QUIT:
		/* Ends the program as the close button does. */
		counter->closed = 1;
		break;
	case ACTION_COPY:
		/* Puts the number on the clipboard as text. */
		snprintf(number, sizeof(number), "%d", counter->count);
		kl_window_copy(counter->window, number, strlen(number));
		break;
	case ACTION_PASTE:
		/* Takes a number from the clipboard's text; other text is left alone. */
		length = kl_window_paste(counter->window, number, sizeof(number) - 1U);
		number[length] = '\0';
		digits = strspn(number, "-0123456789");
		if (length > 0U && digits == length)
			counter->count = atoi(number);
		break;
	case ACTION_LESS:
		/* The buttons' and the keys' steps. */
		counter->count--;
		break;
	case ACTION_MORE:
		counter->count++;
		break;
	case ACTION_RESET:
		counter->count = 0;
		break;
	case ACTION_SMALL:
		/* The number's size, from the View menu. */
		counter->large = 0;
		break;
	case ACTION_LARGE:
		counter->large = 1;
		break;
	default:
		/* An action this program does not have. */
		return;
	}

	/* The menus follow the new state, and the frame shows it. */
	counter_states(counter);
	counter->dirty = 1;
}

/*
 * Gives the menus the state of the actions: the size shown ticked, Reset
 * disabled at 0.  The compositor never ticks an item by itself; the state
 * applies to the window's menu and the context menu alike.
 */
static void
counter_states(
	struct counter *counter)
{
	unsigned small;
	unsigned large;
	unsigned reset;

	/* The radio items: the size shown is the one ticked. */
	small = KL_ACTION_CHECKED;
	large = 0U;
	if (counter->large) {
		small = 0U;
		large = KL_ACTION_CHECKED;
	}
	(void)kl_window_set_action_state(counter->window, ACTION_SMALL, small);
	(void)kl_window_set_action_state(counter->window, ACTION_LARGE, large);

	/* Reset is disabled while there is nothing to reset. */
	reset = 0U;
	if (counter->count == 0)
		reset = KL_ACTION_DISABLED;
	(void)kl_window_set_action_state(counter->window, ACTION_RESET, reset);
}
```

<!-- WRITE (chapter 6, reading the program), in this order:
1. Lines 34-42: one list of actions for the whole program; what changed from the counter: the buttons now call
   counter_act (lines 307-319) instead of changing count themselves.
2. Lines 82-102: the menu table: id (not 0, unique), parent (KL_MENU_ROOT for the menu bar's items; a submenu's items
   follow it), type (KL_MENU_ITEM_*), label, action, role (KL_MENU_ROLE_*: tells the desktop what the item means, so it
   can place it), shortcut (KL_MENU_* modifiers and an XKB keysym).  Define "item", "submenu", "role", "shortcut".
3. Lines 143-149: kl_window_set_menu with the whole table (ENOTSUP without the compositor's System Menu: the keys and
   buttons still work), then counter_states.
4. Lines 493-521 (counter_states): the program ticks the radio items and disables Reset with action states; the
   compositor never ticks an item by itself; the state applies to the menu and the context menu alike.
5. Lines 357-402 (counter_input; the action at lines 379-382, the right press at 383-394): KL_WINDOW_ACTION, chosen by the pointer or the shortcut, comes in the queue among the
   other inputs, so typed text and a shortcut keep their order; a right press opens the context menu with
   kl_window_popup_menu at the press's place (it must be the last press: call it when the press arrives), and must be
   checked before kl_ui_window_input, which takes every KL_WINDOW_BUTTON.
6. Lines 104-109 and 421-425: the context menu's table (top-level items only), and the finger's long press arriving as
   KL_EVENT_LONG_PRESS from kl_ui_take.
7. Lines 435-491 (counter_act): one place for every action; Copy and Paste through kl_window_copy and
   kl_window_paste (chapter 7 says more about the clipboard).
-->

<!-- WRITE (chapter 6, the rules): The menu is declarative and retained like the titlebar; give the whole table
whenever it changes; ids are the program's; actions are shared by menu items, controls and context menus; the menu
shows in the titlebar and in the system bar while the window is maximized; a context menu is shown once and goes when
an item is chosen or the user clicks elsewhere.  Link keiland_menu.h and the declarative calls in keiland_window.h. -->

<!-- WRITE (chapter 6, pitfalls and next): Expecting a radio item to tick itself; numbering two items alike; giving a
right press to kl_ui_window_input first; opening a context menu later than the press it belongs to.  Next: the
desktop's other services. -->

<!-- OLD TEXT (chapter 6; reuse the paragraphs on types, roles, shortcuts and the context menu, then delete; its two
snippets are replaced by the program):
## 6. Menus

The window's menu is shown by the compositor in the titlebar (and in the
system bar while the window is maximized). Like the titlebar's controls, it is
a table the program gives whenever it changes:

```c
#define ACTION_OPEN	10U
#define ACTION_QUIT	11U
#define ACTION_GRID	12U
#define ACTION_LIST	13U

static const struct kl_menu_entry menu[] = {
	/* id, parent, type, label, action, role, modifiers, keysym */
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 2U, 1U, KL_MENU_ITEM_NORMAL, "Open...", ACTION_OPEN, KL_MENU_ROLE_OPEN, KL_MENU_CTRL, 'o' },
	{ 3U, 1U, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 4U, 1U, KL_MENU_ITEM_NORMAL, "Quit", ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ 5U, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "View", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 6U, 5U, KL_MENU_ITEM_RADIO, "Grid", ACTION_GRID, KL_MENU_ROLE_NONE, 0U, 0U },
	{ 7U, 5U, KL_MENU_ITEM_RADIO, "List", ACTION_LIST, KL_MENU_ROLE_NONE, 0U, 0U }
};

kl_window_set_menu(window, menu, sizeof(menu) / sizeof(menu[0]));
kl_window_set_action_state(window, ACTION_GRID, KL_ACTION_CHECKED);
```

- Items are numbered by the program (not 0, unique in the table).
  `KL_MENU_ROOT` is the parent of the top-level items; a submenu item is the
  parent of the items under it, which follow it in the table.
- The types are `KL_MENU_ITEM_NORMAL`, `_SEPARATOR`, `_CHECKBOX`, `_RADIO`
  and `_SUBMENU`. A role (`KL_MENU_ROLE_*`) tells the desktop what an item
  means (Quit, Undo, Copy ...) so that it can give it its usual place.
- A shortcut is `KL_MENU_*` modifiers and an XKB keysym (`'q'`, or a key's
  keysym such as `0xffc8` for F11); the compositor shows it and chooses the
  item when the keys are pressed in the focused window.
- A choice comes as `KL_WINDOW_ACTION` with the item's action in
  `input.code` and its ID in `input.id`, in the queue among the window's
  other inputs (so that typed text and then Ctrl+S keep their order).
- The compositor never ticks a checkbox or a radio item by itself: the
  program sets the state with `kl_window_set_action_state`
  (`KL_ACTION_CHECKED`, `KL_ACTION_DISABLED`, `KL_ACTION_HIDDEN`), which
  applies to every item and control of that action.

A context menu is a table of top-level items shown once at a point of the
window, for the last press:

```c
static const struct kl_menu_entry context[] = {
	{ 1U, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Copy", ACTION_COPY, KL_MENU_ROLE_COPY, 0U, 0U },
	{ 2U, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Paste", ACTION_PASTE, KL_MENU_ROLE_PASTE, 0U, 0U }
};

/* On a right press, or a long press from kl_ui_take. */
kl_window_popup_menu(window, context, 2U, (int)input->x, (int)input->y);
```

Its choice comes as a `KL_WINDOW_ACTION` like the window menu's.

Without the compositor's System Menu, `kl_window_set_menu` returns `ENOTSUP`
and nothing is shown; the program's keys still work.

-->

## 7. Other services

<!-- WRITE (chapter 7, summary): One program, 07-services.c, uses several services at once: glass, the appearance,
translations, the file chooser and the recent list, the clipboard and notifications.  Say that this chapter's
sections can be read in any order and each points at the lines of the program it explains.  Screenshot
keiland-examples/07-services.png (on glass when the compositor offers it).  The settings, the system and sound have no
example here; keep them short with links to their headers.  Terms first defined here: see-through, glass panel,
appearance, accent, catalog, domain, settings dispatch, file chooser, recent list. -->

<!-- example: keiland-examples/07-services.c -->
```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A window that uses the desktop's services: it stands on the desktop's
 * glass when the compositor offers it, follows the light or dark
 * appearance, speaks the language chosen in Settings, opens a file
 * through the desktop's file chooser and puts it on the recent list,
 * copies the path to the clipboard, and posts a notification.
 */

#include <keiland/keiland.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the desktop's fonts are (/opt/keiland/share/fonts for a Linux build under /opt/keiland). */
#ifndef FONT_DIR
#define FONT_DIR	"/usr/share/fonts"
#endif
#define FONT		FONT_DIR "/keiland.ttf"
#define FALLBACK_FONT	FONT_DIR "/keiland-fallback.ttf"

/* The application's ID, and its translations' domain (its catalogs are services.tr). */
#define APPLICATION	"org.example.Services"
#define DOMAIN		"services"

/* The widgets' ids. */
#define ID_OPEN		1U
#define ID_COPY		2U
#define ID_NOTIFY	3U

/* The layout, in pixels: the card's margin, the room inside it, and its corners (the glass panel's too). */
#define MARGIN		16
#define PADDING		24
#define CARD_RADIUS	16

/* The longest path kept. */
#define PATH_MAX_BYTES	1024

/*
 * Everything the program keeps: its connection, window and widgets, the
 * desktop's settings it follows the language through, the file chooser
 * while it is open, the path chosen, how many files were opened, and
 * what the main loop has to do next.
 */
struct services {
	struct kl_app *app;
	struct kl_window *window;
	struct kl_ui *ui;
	struct kl_text text;
	struct kl_style style;
	struct kl_canvas canvas;
	struct kl_settings *settings;
	struct kl_file_chooser *chooser;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	char path[PATH_MAX_BYTES];
	unsigned long opened;
	int answered;
	int dirty;
	int moving;
	int resized;
	int closed;
};

static int services_resize(struct services *services);
static void services_draw(struct services *services, uint64_t now_us, struct kl_rect *card);
static int services_frame(struct services *services);
static void services_input(struct services *services, const struct kl_window_event *input);
static void services_open(struct services *services);
static void services_chosen(void *data, struct kl_file_chooser *chooser, unsigned result, const char *path, size_t filter);
static void services_language(void *data, const char *language);

/* What the file chooser calls when it has its answer. */
static const struct kl_file_chooser_listener services_chooser_listener = {
	services_chosen
};

int
main(void)
{
	static struct services services;
	struct kl_window_options window_options;
	struct kl_app_options app_options;
	struct kl_app_event event;
	int see_through;
	int timeout;
	int status;
	int taken;

	/* Connects to the desktop. */
	memset(&app_options, 0, sizeof(app_options));
	app_options.application = APPLICATION;
	services.app = kl_app_open(&app_options);
	if (services.app == NULL) {
		fprintf(stderr, "services: no desktop (%s)\n", strerror(errno));
		return 1;
	}

	/* Follows the language chosen in Settings: reads the catalogs now and whenever it changes. */
	services.settings = kl_settings_open(kl_app_display(services.app), NULL);
	if (services.settings == NULL)
		return 1;
	status = kl_tr_follow(services.settings, DOMAIN, services_language, &services);
	if (status != 0)
		fprintf(stderr, "services: the texts stay in English (%s)\n", strerror(status));

	/* Opens the window; the Vulkan presenter is the one that can stand on glass. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = kl_tr("Services");
	window_options.width = 480;
	window_options.height = 300;
	window_options.present = KL_PRESENT_VULKAN;
	services.window = kl_app_window_create(services.app, &window_options);
	if (services.window == NULL) {
		fprintf(stderr, "services: no window (%s)\n", strerror(errno));
		return 1;
	}

	/* Makes the widgets' record and opens the font. */
	services.ui = kl_ui_create();
	if (services.ui == NULL)
		return 1;
	status = kl_text_open(&services.text, FONT, FALLBACK_FONT);
	if (status != 0) {
		fprintf(stderr, "services: no font %s (%s)\n", FONT, strerror(status));
		return 1;
	}
	services.style.text = &services.text;

	/* Makes the first frame, and learns whether the window is see-through: then it stands on glass. */
	status = services_resize(&services);
	if (status != 0)
		return 1;
	see_through = kl_window_see_through(services.window);
	services.style.glass = see_through;

	/* Runs until the window is closed. */
	while (!services.closed) {
		/* Waits for the desktop: not at all while a frame is due, a frame's time while something moves. */
		timeout = -1;
		if (services.moving)
			timeout = 16;
		if (services.dirty || services.resized || services.answered)
			timeout = 0;
		status = kl_app_dispatch(services.app, timeout);
		if (status != 0)
			break;

		/* Runs the settings' watches on what the dispatch read (a new language calls services_language). */
		(void)kl_settings_dispatch(services.settings);

		/* Takes every event queued, in order. */
		for (;;) {
			taken = kl_app_take(services.app, &event);
			if (!taken)
				break;

			/* The appearance changed: kl_theme_default's colours are new, the frame is drawn with them. */
			if (event.kind == KL_APP_THEME)
				services.dirty = 1;

			/* An input of the window; none of the chooser's window comes here. */
			if (event.kind == KL_APP_WINDOW && event.window == services.window)
				services_input(&services, &event.input);
		}

		/* The chooser answered during the dispatch: it is closed now. */
		if (services.answered) {
			services.answered = 0;
			kl_file_chooser_destroy(services.chooser);
			services.chooser = NULL;
			services.dirty = 1;
		}

		/* A new size: a new frame of that size. */
		if (services.resized) {
			services.resized = 0;
			status = services_resize(&services);
			if (status != 0)
				break;
		}

		/* Something changed or moves: draws and shows a frame. */
		if (services.dirty || services.moving) {
			status = services_frame(&services);
			if (status != 0)
				break;
		}
	}

	/* Closes the chooser, the settings, the widgets' record, the frame, the font and the connection. */
	if (services.chooser != NULL)
		kl_file_chooser_destroy(services.chooser);
	kl_settings_close(services.settings);
	kl_ui_destroy(services.ui);
	kl_canvas_release(&services.canvas);
	free(services.pixels);
	kl_text_close(&services.text);
	kl_app_close(services.app);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes the frame at the window's size; nonzero when it cannot.
 */
static int
services_resize(
	struct services *services)
{
	uint32_t *pixels;
	int status;

	/* Sets the presenter to the window's size, which it reports. */
	status = kl_window_present_resize(services->window, &services->width, &services->height);
	if (status != 0)
		return -1;

	/* Allocates the frame. */
	pixels = malloc((size_t)services->width * services->height * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* Puts the canvas on the new pixels in place of the old ones; the widgets draw on it. */
	if (services->pixels != NULL)
		kl_canvas_release(&services->canvas);
	free(services->pixels);
	services->pixels = pixels;
	status = kl_canvas_init(&services->canvas, services->pixels, services->width, (int)services->width, (int)services->height);
	if (status != 0)
		return -1;
	services->style.canvas = &services->canvas;

	/* Succeeded: the new frame is drawn next. */
	services->dirty = 1;
	return 0;
}

/*
 * Draws one frame: one card (on glass, or on the theme's ground), what
 * the desktop told the program, and the three buttons.  Reports the card's
 * rectangle, which is the glass panel.
 */
static void
services_draw(
	struct services *services,
	uint64_t now_us,
	struct kl_rect *card)
{
	const struct kl_theme *theme;
	const char *appearance;
	unsigned shown;
	struct kl_rect whole;
	struct kl_rect button;
	unsigned open_flags;
	unsigned copy_flags;
	char line[PATH_MAX_BYTES + 64];
	char count[32];
	int top;
	int clicked;

	/* Starts the frame with the theme of the appearance shown now. */
	theme = kl_theme_default();
	services->style.theme = theme;
	kl_ui_begin(services->ui, now_us);

	/* The ground: cleared to nothing on glass (the compositor draws the glass), the theme's otherwise. */
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)services->width;
	whole.height = (int)services->height;
	if (services->style.glass) {
		kl_canvas_clear(&services->canvas);
	} else {
		kl_canvas_gradient(&services->canvas, &whole, theme->ground_top, theme->ground_bottom);
	}

	/* The card: its veil on glass, a white or dark panel otherwise. */
	card->x = MARGIN;
	card->y = MARGIN;
	card->width = (int)services->width - 2 * MARGIN;
	card->height = (int)services->height - 2 * MARGIN;
	kl_panel(&services->style, card, 0);

	/* What the desktop shows now: light or dark. */
	shown = kl_appearance_get(NULL);
	appearance = kl_tr("Light");
	if (shown == KL_APPEARANCE_DARK)
		appearance = kl_tr("Dark");
	(void)kl_tr_format(line, sizeof(line), kl_tr("Appearance: {1}"), appearance, (const char *)NULL);
	top = card->y + PADDING;
	(void)kl_text_draw(&services->text, &services->canvas, card->x + PADDING, top + 16, line, strlen(line), theme->text_body, 0, theme->text);

	/* The file chosen last, and how many were opened (the plural the language needs). */
	snprintf(count, sizeof(count), "%lu", services->opened);
	(void)kl_tr_format(line, sizeof(line), kl_trn("{1} file opened", "{1} files opened", services->opened), count, (const char *)NULL);
	(void)kl_text_draw(&services->text, &services->canvas, card->x + PADDING, top + 48, line, strlen(line), theme->text_body, 0, theme->text_secondary);
	if (services->path[0] != '\0')
		(void)kl_text_draw_fit(&services->text, &services->canvas, card->x + PADDING, top + 76, services->path, theme->text_small, 0, card->width - 2 * PADDING, theme->text_secondary);

	/* Open: the desktop's file chooser; disabled while it is open. */
	button.x = card->x + PADDING;
	button.y = card->y + card->height - PADDING - theme->control_height;
	button.height = theme->control_height;
	button.width = kl_button_width(&services->style, kl_tr("Open..."));
	open_flags = KL_BUTTON_PRIMARY;
	if (services->chooser != NULL)
		open_flags |= KL_BUTTON_DISABLED;
	clicked = kl_button(services->ui, &services->style, ID_OPEN, &button, kl_tr("Open..."), open_flags);
	if (clicked)
		services_open(services);

	/* Copy Path: the path as text on the clipboard; disabled before a file was chosen. */
	button.x += button.width + 12;
	button.width = kl_button_width(&services->style, kl_tr("Copy Path"));
	copy_flags = 0U;
	if (services->path[0] == '\0')
		copy_flags = KL_BUTTON_DISABLED;
	clicked = kl_button(services->ui, &services->style, ID_COPY, &button, kl_tr("Copy Path"), copy_flags);
	if (clicked)
		kl_window_copy(services->window, services->path, strlen(services->path));

	/* Notify: a notification under the application's name, saying how many files were opened. */
	button.x += button.width + 12;
	button.width = kl_button_width(&services->style, kl_tr("Notify"));
	clicked = kl_button(services->ui, &services->style, ID_NOTIFY, &button, kl_tr("Notify"), 0U);
	if (clicked)
		(void)kl_app_notify(services->app, kl_tr("Services"), line);

	/* Ends the frame. */
	services->moving = kl_ui_end(services->ui, now_us);
	kl_ui_window_text(services->ui, services->window);
}

/*
 * Draws and shows a frame, with its glass panel; nonzero when the frame
 * cannot be shown.
 */
static int
services_frame(
	struct services *services)
{
	struct kl_glass_panel panel;
	struct kl_rect card;
	int status;

	/* Draws the frame. */
	services->dirty = 0;
	services_draw(services, kl_clock_us(), &card);

	/* On glass: the card is the panel the compositor puts the glass under, from this frame on. */
	if (services->style.glass) {
		panel.x = card.x;
		panel.y = card.y;
		panel.width = card.width;
		panel.height = card.height;
		panel.radius = CARD_RADIUS;
		panel.kind = KL_GLASS_CARD;
		status = kl_window_set_glass(services->window, &panel, 1U);
		if (status != 0) {
			/* No glass after all: the next frame is opaque. */
			services->style.glass = 0;
			services->dirty = 1;
		}
	}

	/* Hands it to the compositor. */
	status = kl_window_present(services->window, services->pixels, services->width);
	if (status == EAGAIN) {
		/* The window's size changed under the frame: a new frame of the new size. */
		services->resized = 1;
	} else if (status != 0) {
		/* Refuses to go on when the frame could not be shown. */
		return -1;
	}

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Gives one input of the window to the widgets, or acts on it.
 */
static void
services_input(
	struct services *services,
	const struct kl_window_event *input)
{
	int taken;

	/* The window's own inputs: a new size, the close button. */
	if (input->kind == KL_WINDOW_RESIZE) {
		services->resized = 1;
		return;
	}
	if (input->kind == KL_WINDOW_CLOSE) {
		services->closed = 1;
		return;
	}

	/* The rest is the widgets'. */
	taken = kl_ui_window_input(services->ui, input);
	if (taken)
		services->dirty = 1;
}

/*
 * Opens the desktop's file chooser over the window.  The program keeps
 * running; the answer comes once, through services_chosen, during a
 * dispatch.
 */
static void
services_open(
	struct services *services)
{
	struct kl_file_chooser_options options;

	/* One chooser at a time. */
	if (services->chooser != NULL)
		return;

	/* An Open window in the program's name, in its fonts. */
	memset(&options, 0, sizeof(options));
	options.mode = KL_FILE_CHOOSER_OPEN;
	options.title = kl_tr("Open");
	options.application = APPLICATION;
	options.font = FONT;
	options.fallback_font = FALLBACK_FONT;
	services->chooser = kl_file_chooser_open(kl_app_display(services->app), kl_window_toplevel(services->window), &options, &services_chooser_listener, services);
	if (services->chooser == NULL)
		fprintf(stderr, "services: no file chooser (%s)\n", strerror(errno));
}

/*
 * Takes the file chooser's answer: the path chosen, which goes on the
 * desktop's recent list.  The chooser is destroyed by the main loop.
 */
static void
services_chosen(
	void *data,
	struct kl_file_chooser *chooser,
	unsigned result,
	const char *path,
	size_t filter)
{
	struct services *services;

	(void)chooser;
	(void)filter;

	/* Keeps the path, and tells the desktop the file was opened. */
	services = data;
	if (result == KL_FILE_CHOOSER_CHOSEN) {
		snprintf(services->path, sizeof(services->path), "%s", path);
		services->opened++;
		(void)kl_recent_add(path, APPLICATION);
	}

	/* The main loop closes the chooser. */
	services->answered = 1;
}

/*
 * Draws the window again in the language just chosen: kl_tr gives the new
 * language's texts from now on.
 */
static void
services_language(
	void *data,
	const char *language)
{
	struct services *services;

	(void)language;

	/* The title and every text of the next frame in the new language. */
	services = data;
	if (services->window != NULL)
		kl_window_set_title(services->window, kl_tr("Services"));
	services->dirty = 1;
}
```

<!-- WRITE (chapter 7), one subsection each, in this order, each with: what the service is, the lines of the program,
the rules, pitfalls.
### Appearance, theme and accent: KL_APP_THEME (line 167) makes the frame draw again; kl_theme_default gives the
current colours each frame (lines 271-273); kl_appearance_get(NULL) (lines 294-298); kl_theme_choose for the program's own
two colours (OLD TEXT).
### Glass: lines 138-143 (kl_window_see_through after the first resize; only KL_PRESENT_VULKAN can be see-through),
lines 276-292 (clear the ground instead of painting it; kl_panel draws the veil), lines 360-374 (the panel's rectangle
and radius after each frame that moved it; ENOTSUP or another error: draw opaque from the next frame).
### Text in the user's language: lines 107-113 (kl_settings_open and kl_tr_follow), line 158 (kl_settings_dispatch
after every kl_app_dispatch: the watch that rereads the catalogs runs there; the earlier version left this call out,
and without it the language never changes while the program runs), lines 472-490 (the callback draws again and sets
the title), lines 294-305 (kl_tr, kl_trn, kl_tr_format with {1}).  Catalog files and the domain naming rule from the OLD
TEXT.
### Opening files: lines 416-441 (kl_file_chooser_open over the window, one at a time, the Open button disabled while
it is open), lines 443-470 (the answer comes once through the listener during a dispatch; kl_recent_add), lines
175-181 (the chooser destroyed by the loop after the answer; it may also be destroyed in the callback).
### Clipboard: line 330 (kl_window_copy); chapter 6's Paste; fields use the clipboard by themselves once
kl_ui_window_text tied them (chapter 5).
### Notifications: line 337 (kl_app_notify; kl_app_notify_lock for what the lock screen shows).
### The desktop's settings and system, and sound: no example; keep the OLD TEXT's two paragraphs and links.
-->

<!-- OLD TEXT (chapter 7; reuse, then delete.  Its translation snippet lacks kl_settings_dispatch; its snippets are
replaced by the program):
## 7. Other services

### Appearance, theme and accent

`kl_theme_default()` gives the colours and sizes every widget draws with. They
follow the desktop's appearance (light or dark) and the accent the user chose;
when either changes, a `KL_APP_THEME` event comes and the program draws its
windows again. `kl_appearance_get(NULL)` tells which appearance is shown
(`KL_APPEARANCE_LIGHT`, `KL_APPEARANCE_DARK`), and `kl_theme_choose(light,
dark)` picks one of two colours of the program's own.

### Glass

On a see-through window (`kl_window_see_through(window)` is 1, with
`KL_PRESENT_VULKAN`), the compositor blurs what is under the panels the
program names. The program sets `style.glass`, clears the frame's ground
(`kl_canvas_clear`) instead of painting it, and gives the panels' rectangles
after each frame that moved them:

```c
struct kl_glass_panel panels[2];

panels[0] = (struct kl_glass_panel){ sidebar.x, sidebar.y, sidebar.width, sidebar.height, 12, KL_GLASS_CARD };
panels[1] = (struct kl_glass_panel){ content.x, content.y, content.width, content.height, 12, KL_GLASS_CARD };
kl_window_set_glass(window, panels, 2U);
```

`kl_panel` draws the panels' veil in the same places.

### Text in the user's language

The program writes its texts in English and looks each up in its catalogs
(`domain`, a name of lower-case letters, digits, `-` and `_`, and the
desktop's shared `KL_TR_SHARED_DOMAIN`). `kl_tr_follow` reads them in the
language the user chose in Settings, and again whenever it changes, calling
the program back so that it draws its windows again:

```c
static void
language_changed(void *data, const char *language)
{
	struct counter *c = data;

	c->dirty = 1;
}

settings = kl_settings_open(kl_app_display(app), "counter");
kl_tr_follow(settings, "counter", language_changed, &c);

/* in a frame */
label = kl_tr("More");
kl_tr_format(line, sizeof(line), kl_trn("{1} file", "{1} files", count), number, (const char *)NULL);
```

`kl_trn` chooses the singular or a plural for a count; `kl_tr_format` fills
`{1}` to `{9}` with the strings that follow, in any order the language needs.
A language other than the desktop's is read with `kl_tr_open(domain,
language)`.

### Clipboard

`kl_window_copy(window, text, length)` puts text on the clipboard;
`kl_window_paste(window, buffer, size)` reads it, and
`kl_window_can_paste(window)` tells whether there is text. The text widgets
use the window's clipboard by themselves once `kl_ui_window_text` tied them
to it.

### Opening and saving files

The file chooser is the desktop's shared Open and Save window:

```c
static void
chosen(void *data, struct kl_file_chooser *chooser, unsigned result, const char *path, size_t filter)
{
	if (result == KL_FILE_CHOOSER_CHOSEN)
		open_file(data, path);
	kl_file_chooser_destroy(chooser);
}

static const struct kl_file_chooser_listener listener = { chosen };
struct kl_file_chooser_options options;

memset(&options, 0, sizeof(options));
options.mode = KL_FILE_CHOOSER_OPEN;
options.title = "Open";
kl_file_chooser_open(kl_app_display(app), kl_window_toplevel(window), &options, &listener, state);
```

The answer comes once, through the listener, while the program dispatches its
application. `kl_recent_add(path, application)` puts an opened file on the
desktop's recent list.

### Notifications

`kl_app_notify(app, title, body)` shows a notification under the program's
name.

### The desktop's settings and system

`kl_settings_open(kl_app_display(app), name)` reads, watches and asks to
change the desktop's settings (`kl_settings_get`, `kl_settings_watch`,
`kl_settings_set`). `kl_app_system(app)` gives
the desktop's system (`kl_system_*`): the network, the sound, the power, the
removable devices, the displays, the printers. Each of these is asked of the
compositor; the program reads no system file and talks to no daemon itself.

### Sound

A program plays sound through a stream of the desktop
(`kl_audio_stream_open`, `kl_audio_stream_write`, `kl_audio_stream_dispatch`):
see `<keiland/keiland_audio.h>`.
-->
