Programming a Keiland application
==================================

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
