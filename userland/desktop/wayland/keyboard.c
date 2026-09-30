/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The on-screen keyboard (ws102-p002, plan/ws102/design.md): drawn by the
 * compositor itself, opened by a swipe from a bottom corner.
 *
 * A contact that starts in the bottom-right corner and moves towards the
 * top left opens the flick panel at the right side of the screen; one that
 * starts in the bottom-left corner and moves towards the top right opens
 * the QWERTY panel along the bottom.  The recogniser has corner.c's numbers
 * (the top-right corner's swipe to Notes): a contact that begins in a
 * corner belongs to the keyboard until it ends; it arms once it has moved
 * KEYBOARD_ARM each way within KEYBOARD_ARM_MS; it commits when it ends
 * near the diagonal, far enough along it (KEYBOARD_COMMIT) or quickly
 * enough (KEYBOARD_FLICK_DISTANCE at KEYBOARD_FLICK_SPEED).  The corners are the
 * keyboard's before Wiseview's bottom edge and the desktops' side edges
 * (shell.c asks the keyboard first), so those gestures start outside them.
 *
 * The same swipe again closes the panel it opened; the other corner's
 * swipe changes panels; the panel's close key closes it.  A contact is the
 * pointer's left button or a finger, which touch.c passes through the shell
 * as the pointer (server->shell_source says which).
 *
 * This first step (p002) has the panels empty: their glass, a title and
 * the close key.  The keys, what they type and the work area come in the
 * later phases.
 */

#include "glass.h"
#include "menu.h"

#include <stdio.h>
#include <string.h>

/* The corners a contact starts in: this many pixels from the bottom and from the side (zwl.h). */
#define KEYBOARD_ZONE		ZWL_KEYBOARD_ZONE

/* How far a contact moves each way (inwards and up) before it is the gesture, and how soon. */
#define KEYBOARD_ARM		14
#define KEYBOARD_ARM_MS		1500U

/*
 * The diagonal: the shorter of the two movements must be at least
 * KEYBOARD_CONE_NUMERATOR / KEYBOARD_CONE_DENOMINATOR of the longer, which
 * keeps the contact within 25 degrees of the 45-degree line.
 */
#define KEYBOARD_CONE_NUMERATOR		9
#define KEYBOARD_CONE_DENOMINATOR	25

/* How far along the diagonal a letting go commits, and the distance the hint grows over. */
#define KEYBOARD_COMMIT		108.0f
#define KEYBOARD_DISTANCE	216.0f

/* A flick: at least this far along the diagonal, at this speed (pixels a millisecond) over the last KEYBOARD_FLICK_MS. */
#define KEYBOARD_FLICK_DISTANCE	40.0f
#define KEYBOARD_FLICK_SPEED	0.8f
#define KEYBOARD_FLICK_MS	100U

/* How many recent points of the contact are kept for its speed. */
#define KEYBOARD_SAMPLES	16U

/* The panels' margin from the screen's edges, the corner radius and the title band's height. */
#define KEYBOARD_MARGIN		12
#define KEYBOARD_RADIUS		18.0f
#define KEYBOARD_BAND		36

/* The flick panel's keys: a key's side (a share of the screen's height, within limits) and the gap. */
#define KEYBOARD_KEY_DIVISOR	11
#define KEYBOARD_KEY_MIN	64
#define KEYBOARD_KEY_MAX	96
#define KEYBOARD_KEY_GAP	6
#define KEYBOARD_FLICK_COLUMNS	4
#define KEYBOARD_FLICK_ROWS	4

/* The QWERTY panel's height: a share of the screen's height (in hundredths), within limits. */
#define KEYBOARD_QWERTY_SHARE	38
#define KEYBOARD_QWERTY_MIN	260
#define KEYBOARD_QWERTY_MAX	420

/* The close key's side, at the right of the title band. */
#define KEYBOARD_CLOSE		28

/*
 * Which panel: none, the flick panel at the right side (the bottom-right
 * corner's), or the QWERTY panel along the bottom (the bottom-left
 * corner's).
 */
enum keyboard_kind {
	PANEL_NONE,
	PANEL_FLICK,
	PANEL_QWERTY
};

/*
 * One point a contact passed through, and when (the input event's time in
 * wrapping milliseconds); the recent points give its speed at the end.
 */
struct keyboard_sample {
	int32_t x;
	int32_t y;
	uint32_t time;
};

/*
 * The contact the gesture follows, from its start in a corner to its end:
 * the corner (the panel it would open), its source, where and when it
 * started (the event's time and the compositor's clock), where it is, and
 * whether it armed or ran out of time.  Only one contact is followed.
 */
struct keyboard_contact {
	unsigned active;
	enum keyboard_kind corner;
	enum zwl_contact_source source;
	int32_t start_x;
	int32_t start_y;
	uint32_t start_time;
	uint64_t start_clock_ms;
	int32_t x;
	int32_t y;
	unsigned armed;
	unsigned expired;
	struct keyboard_sample samples[KEYBOARD_SAMPLES];
	unsigned sample_next;
	unsigned sample_count;
};

/*
 * The keyboard's whole state: the contact of the corner's gesture, the
 * panel open (and its rectangle on the output), a press that began on the
 * panel (it is the panel's until its release), and whether the corners'
 * places have been logged (once, for the tests).
 */
struct keyboard_state {
	struct keyboard_contact contact;
	enum keyboard_kind open;
	int32_t panel[4];
	unsigned pressing;
	unsigned zones_logged;
};

/*
 * The keyboard of the one compositor in this process.
 *
 * It is zero (no contact, no panel) at start-up.  The compositor's single
 * thread is the only one that reads or changes it: the input handlers, the
 * clock and the drawing.
 */
static struct keyboard_state keyboard;

static int keyboard_contact_begin(struct zwl_server *server, int32_t x, int32_t y, uint32_t time);
static int keyboard_contact_move(struct zwl_server *server, int32_t x, int32_t y, uint32_t time);
static int keyboard_contact_end(struct zwl_server *server, int32_t x, int32_t y, uint32_t time);
static enum keyboard_kind keyboard_corner_at(struct zwl_server *server, int32_t x, int32_t y);
static void keyboard_travel(int32_t *inwards, int32_t *upwards);
static void keyboard_sample(int32_t x, int32_t y, uint32_t time);
static float keyboard_speed(void);
static int keyboard_on_diagonal(int32_t inwards, int32_t upwards);
static void keyboard_commit(struct zwl_server *server, const char *via, float progress);
static void keyboard_open(struct zwl_server *server, enum keyboard_kind kind);
static void keyboard_place(struct zwl_server *server, enum keyboard_kind kind, int32_t *rect);
static int keyboard_panel_button(struct zwl_server *server, uint32_t button, uint32_t state);
static int keyboard_contains(const int32_t *rect, int32_t x, int32_t y);
static void keyboard_close_rect(int32_t *rect);
static void keyboard_draw_panel(struct zwl_server *server, VkCommandBuffer command, const int32_t *rect, float opacity);
static void keyboard_draw_hint(struct zwl_server *server, VkCommandBuffer command);
static const char *keyboard_kind_name(enum keyboard_kind kind);
static const char *keyboard_source_name(enum zwl_contact_source source);

/*
 * Feeds the left pointer button (the mouse's, or a finger's passed as the
 * pointer) to the keyboard: a press in a bottom corner begins the corner's
 * gesture, a press on the open panel is the panel's, and a release ends
 * whichever began.  Returns 1 when the button is the keyboard's.
 */
int
zwl_keyboard_button(
	struct zwl_server *server,
	uint32_t button,
	uint32_t state)
{
	int taken;

	/* Only the left button. */
	if (button != ZWL_BUTTON_LEFT)
		return 0;

	/* A release ends the corner's contact, when it is the keyboard's. */
	if (state == 0) {
		taken = keyboard_contact_end(server, server->pointer_x, server->pointer_y, server->input_time);
		if (taken)
			return 1;

		/* Or the press on the panel. */
		taken = keyboard_panel_button(server, button, state);
		return taken;
	}

	/* A press on the open panel is the panel's. */
	taken = keyboard_panel_button(server, button, state);
	if (taken)
		return 1;

	/* Succeeded: a press may begin the corner's gesture. */
	taken = keyboard_contact_begin(server, server->pointer_x, server->pointer_y, server->input_time);
	return taken;
}

/*
 * Feeds the pointer's movement to the keyboard while the corner's contact
 * is followed (only the source that began it moves it).  Returns 1 when the
 * movement is the keyboard's.
 */
int
zwl_keyboard_motion(
	struct zwl_server *server)
{
	int taken;

	/* A press held on the panel keeps the pointer's movement. */
	if (keyboard.pressing)
		return 1;

	/* Only the contact of the source moving the pointer now. */
	if (!keyboard.contact.active || keyboard.contact.source != server->shell_source)
		return 0;

	/* Succeeded: the contact moves with the pointer. */
	taken = keyboard_contact_move(server, server->pointer_x, server->pointer_y, server->input_time);
	return taken;
}

/*
 * Keeps the keyboard's time: the corners' places are logged once, a
 * contact whose release never came ends, and one that does not arm in time
 * is let go.  The panel follows a change of the screen's size.
 */
void
zwl_keyboard_tick(
	struct zwl_server *server)
{
	int32_t rect[4];
	uint64_t now;
	int same;

	/* The corners' places, once, for the tests. */
	if (!keyboard.zones_logged && server->width > 0U) {
		keyboard.zones_logged = 1;
		printf("ZWL OSK zone kind=flick x=%d y=%d size=%d\n", (int)server->width - KEYBOARD_ZONE, (int)server->height - KEYBOARD_ZONE, KEYBOARD_ZONE);
		printf("ZWL OSK zone kind=qwerty x=0 y=%d size=%d\n", (int)server->height - KEYBOARD_ZONE, KEYBOARD_ZONE);
	}

	/* A contact whose release never came (its device went away with the button held) ends. */
	if (keyboard.contact.active && (server->buttons_down & 1U) == 0U) {
		keyboard.contact.active = 0;
		server->dirty = 1;
		printf("ZWL OSK cancel reason=lost\n");
	}

	/* So does a press on the panel. */
	if (keyboard.pressing && (server->buttons_down & 1U) == 0U)
		keyboard.pressing = 0;

	/* A contact still waiting to arm times out. */
	now = zwl_milliseconds();
	if (keyboard.contact.active &&
	    !keyboard.contact.armed &&
	    !keyboard.contact.expired &&
	    now - keyboard.contact.start_clock_ms > KEYBOARD_ARM_MS) {
		keyboard.contact.expired = 1;
		printf("ZWL OSK cancel reason=timeout\n");
	}

	/* The open panel's place for the screen's size now. */
	if (keyboard.open == PANEL_NONE)
		return;
	keyboard_place(server, keyboard.open, rect);
	same = memcmp(rect, keyboard.panel, sizeof(rect));
	if (same != 0) {
		memcpy(keyboard.panel, rect, sizeof(rect));
		server->dirty = 1;
		printf("ZWL OSK place kind=%s x=%d y=%d width=%d height=%d\n", keyboard_kind_name(keyboard.open), rect[0], rect[1], rect[2], rect[3]);
	}
}

/*
 * Tells whether the keyboard shows something over the windows: an open
 * panel, or the hint of an armed contact.  While it does, the output is
 * composed, even over a fullscreen window.
 */
int
zwl_keyboard_showing(
	void)
{
	/* An open panel. */
	if (keyboard.open != PANEL_NONE)
		return 1;

	/* An armed contact that has not timed out. */
	if (keyboard.contact.active &&
	    keyboard.contact.armed &&
	    !keyboard.contact.expired)
		return 1;

	/* Nothing shows. */
	return 0;
}

/*
 * Tells whether a point of the output is on the open panel (a press there
 * is the keyboard's, not a window's).
 */
int
zwl_keyboard_at(
	int32_t x,
	int32_t y)
{
	int inside;

	/* No panel. */
	if (keyboard.open == PANEL_NONE)
		return 0;

	/* Succeeded: whether the point is on it. */
	inside = keyboard_contains(keyboard.panel, x, y);
	return inside;
}

/*
 * Closes the open panel, saying why in the log.
 */
void
zwl_keyboard_close(
	struct zwl_server *server,
	const char *reason)
{
	/* No panel. */
	if (keyboard.open == PANEL_NONE)
		return;

	/* The panel goes. */
	printf("ZWL OSK close kind=%s reason=%s\n", keyboard_kind_name(keyboard.open), reason);
	keyboard.open = PANEL_NONE;
	keyboard.pressing = 0;
	server->dirty = 1;
}

/*
 * Draws the keyboard over everything else: the open panel, and the hint of
 * an armed contact growing from its corner.
 */
void
zwl_keyboard_draw(
	struct zwl_server *server,
	VkCommandBuffer command)
{
	/* The open panel. */
	if (keyboard.open != PANEL_NONE)
		keyboard_draw_panel(server, command, keyboard.panel, 1.0f);

	/* The hint of an armed contact. */
	if (keyboard.contact.active &&
	    keyboard.contact.armed &&
	    !keyboard.contact.expired)
		keyboard_draw_hint(server, command);
}

/*
 * Starts following a contact that begins in a bottom corner.  Returns 1
 * when the contact is the keyboard's.
 */
static int
keyboard_contact_begin(
	struct zwl_server *server,
	int32_t x,
	int32_t y,
	uint32_t time)
{
	enum keyboard_kind corner;
	float home;
	int open;

	/* The login and lock screens, App Home and Wiseview have no keyboard. */
	if (server->greeter || server->locked)
		return 0;
	home = zwl_home_progress(server);
	if (home > 0.0f || server->home_to > 0.0f)
		return 0;
	if (server->wiseview > 0.0f || server->wiseview_gesture || server->wiseview_moving)
		return 0;

	/* A contact already followed keeps the gesture. */
	if (keyboard.contact.active)
		return 0;

	/* Only a contact in a bottom corner. */
	corner = keyboard_corner_at(server, x, y);
	if (corner == PANEL_NONE)
		return 0;

	/* An open menu closes on a press anywhere, the corners too, before the gesture could start. */
	open = zwl_network_is_open();
	if (open)
		return 0;
	open = zwl_menu_is_open();
	if (open)
		return 0;
	open = zwl_volume_is_open();
	if (open)
		return 0;

	/* The contact from its start; it is not armed yet. */
	memset(&keyboard.contact, 0, sizeof(keyboard.contact));
	keyboard.contact.active = 1;
	keyboard.contact.corner = corner;
	keyboard.contact.source = server->shell_source;
	keyboard.contact.start_x = x;
	keyboard.contact.start_y = y;
	keyboard.contact.start_time = time;
	keyboard.contact.start_clock_ms = zwl_milliseconds();
	keyboard.contact.x = x;
	keyboard.contact.y = y;
	keyboard_sample(x, y, time);

	/* Succeeded: the contact is the keyboard's. */
	printf("ZWL OSK press corner=%s source=%s x=%d y=%d\n", keyboard_kind_name(corner), keyboard_source_name(server->shell_source), x, y);
	return 1;
}

/*
 * Follows the corner's contact to a new point: it arms once it has moved
 * far enough inwards and up in time.  Returns 1 when it is the keyboard's.
 */
static int
keyboard_contact_move(
	struct zwl_server *server,
	int32_t x,
	int32_t y,
	uint32_t time)
{
	uint32_t elapsed;
	int32_t inwards;
	int32_t upwards;

	/* A contact the keyboard does not have goes on. */
	if (!keyboard.contact.active)
		return 0;

	/* The point, for the hint and for the speed at the end. */
	keyboard.contact.x = x;
	keyboard.contact.y = y;
	keyboard_sample(x, y, time);

	/* A contact that timed out stays the keyboard's until it ends, without effect. */
	if (keyboard.contact.expired)
		return 1;

	/* An armed contact redraws the hint. */
	if (keyboard.contact.armed) {
		server->dirty = 1;
		return 1;
	}

	/* Too late to arm: the contact is let go (still the keyboard's until it ends). */
	elapsed = time - keyboard.contact.start_time;
	if (elapsed > KEYBOARD_ARM_MS) {
		keyboard.contact.expired = 1;
		printf("ZWL OSK cancel reason=timeout\n");
		return 1;
	}

	/* It arms once it has moved far enough inwards and far enough up. */
	keyboard_travel(&inwards, &upwards);
	if (inwards < KEYBOARD_ARM || upwards < KEYBOARD_ARM)
		return 1;

	/* Succeeded: the contact is armed, and the hint shows from now on. */
	keyboard.contact.armed = 1;
	server->dirty = 1;
	printf("ZWL OSK armed corner=%s ms=%u\n", keyboard_kind_name(keyboard.contact.corner), elapsed);
	return 1;
}

/*
 * Ends the corner's contact: it commits when it ended near the diagonal,
 * far enough along it or quickly enough, and is let go otherwise.  Returns
 * 1 when the contact was the keyboard's.
 */
static int
keyboard_contact_end(
	struct zwl_server *server,
	int32_t x,
	int32_t y,
	uint32_t time)
{
	int32_t inwards;
	int32_t upwards;
	int diagonal;
	float progress;
	float speed;

	/* A contact the keyboard does not have goes on. */
	if (!keyboard.contact.active)
		return 0;

	/* The last point, which the speed is measured to. */
	keyboard.contact.x = x;
	keyboard.contact.y = y;
	keyboard_sample(x, y, time);
	server->dirty = 1;

	/* A contact that timed out ends without effect (its cancel was logged then). */
	if (keyboard.contact.expired) {
		keyboard.contact.active = 0;
		return 1;
	}

	/* A contact that never armed ends without effect, like a tap in the corner. */
	if (!keyboard.contact.armed) {
		keyboard.contact.active = 0;
		printf("ZWL OSK cancel reason=unarmed\n");
		return 1;
	}

	/* A contact that ends off the diagonal is let go. */
	keyboard_travel(&inwards, &upwards);
	diagonal = keyboard_on_diagonal(inwards, upwards);
	if (!diagonal) {
		keyboard.contact.active = 0;
		printf("ZWL OSK cancel reason=direction\n");
		return 1;
	}

	/* Far enough along the diagonal commits. */
	progress = ((float)inwards + (float)upwards) * 0.5f;
	if (progress >= KEYBOARD_COMMIT) {
		keyboard_commit(server, "distance", progress);
		return 1;
	}

	/* A quick flick commits after a shorter way. */
	speed = keyboard_speed();
	if (progress >= KEYBOARD_FLICK_DISTANCE && speed >= KEYBOARD_FLICK_SPEED) {
		keyboard_commit(server, "flick", progress);
		return 1;
	}

	/* Succeeded: too short and too slow, the contact is let go. */
	keyboard.contact.active = 0;
	printf("ZWL OSK cancel reason=short progress=%.0f speed=%.2f\n", (double)progress, (double)speed);
	return 1;
}

/* Finds the bottom corner a point is in: the flick panel's (right), the QWERTY panel's (left), or none. */
static enum keyboard_kind
keyboard_corner_at(
	struct zwl_server *server,
	int32_t x,
	int32_t y)
{
	/* Only the bottom strip. */
	if (y < (int32_t)server->height - KEYBOARD_ZONE)
		return PANEL_NONE;

	/* The right corner. */
	if (x >= (int32_t)server->width - KEYBOARD_ZONE)
		return PANEL_FLICK;

	/* The left corner. */
	if (x < KEYBOARD_ZONE)
		return PANEL_QWERTY;

	/* Between them. */
	return PANEL_NONE;
}

/* Works out how far the contact has moved inwards (away from its corner's side) and up. */
static void
keyboard_travel(
	int32_t *inwards,
	int32_t *upwards)
{
	/* Leftwards from the right corner, rightwards from the left one. */
	*inwards = keyboard.contact.x - keyboard.contact.start_x;
	if (keyboard.contact.corner == PANEL_FLICK)
		*inwards = keyboard.contact.start_x - keyboard.contact.x;

	/* Up from either. */
	*upwards = keyboard.contact.start_y - keyboard.contact.y;
}

/* Keeps a point of the contact among the recent ones. */
static void
keyboard_sample(
	int32_t x,
	int32_t y,
	uint32_t time)
{
	struct keyboard_sample *sample;

	/* The next place of the ring. */
	sample = &keyboard.contact.samples[keyboard.contact.sample_next];
	sample->x = x;
	sample->y = y;
	sample->time = time;
	keyboard.contact.sample_next = (keyboard.contact.sample_next + 1U) % KEYBOARD_SAMPLES;
	if (keyboard.contact.sample_count < KEYBOARD_SAMPLES)
		keyboard.contact.sample_count++;
}

/*
 * Measures the contact's speed along the diagonal over the last
 * KEYBOARD_FLICK_MS, in pixels a millisecond (0 without two points).
 */
static float
keyboard_speed(
	void)
{
	const struct keyboard_sample *last;
	const struct keyboard_sample *first;
	const struct keyboard_sample *sample;
	uint32_t elapsed;
	unsigned index;
	unsigned count;
	float inwards;
	float upwards;

	/* The newest point. */
	if (keyboard.contact.sample_count < 2U)
		return 0.0f;
	index = (keyboard.contact.sample_next + KEYBOARD_SAMPLES - 1U) % KEYBOARD_SAMPLES;
	last = &keyboard.contact.samples[index];

	/* The oldest point within the window, walking back. */
	first = last;
	for (count = 1U; count < keyboard.contact.sample_count; count++) {
		index = (index + KEYBOARD_SAMPLES - 1U) % KEYBOARD_SAMPLES;
		sample = &keyboard.contact.samples[index];
		if (last->time - sample->time > KEYBOARD_FLICK_MS)
			break;
		first = sample;
	}

	/* No time between them: no speed. */
	elapsed = last->time - first->time;
	if (elapsed == 0U)
		return 0.0f;

	/* The movement inwards and up, along the diagonal. */
	inwards = (float)(last->x - first->x);
	if (keyboard.contact.corner == PANEL_FLICK)
		inwards = -inwards;
	upwards = (float)(first->y - last->y);

	/* Succeeded: the speed. */
	return (inwards + upwards) * 0.5f / (float)elapsed;
}

/* Tells whether a movement (inwards, up) is within the cone around the diagonal. */
static int
keyboard_on_diagonal(
	int32_t inwards,
	int32_t upwards)
{
	/* Both ways must be positive. */
	if (inwards <= 0 || upwards <= 0)
		return 0;

	/* The shorter way against the longer. */
	if (inwards * KEYBOARD_CONE_DENOMINATOR < upwards * KEYBOARD_CONE_NUMERATOR)
		return 0;
	if (upwards * KEYBOARD_CONE_DENOMINATOR < inwards * KEYBOARD_CONE_NUMERATOR)
		return 0;

	/* On the diagonal. */
	return 1;
}

/*
 * Acts on a committed swipe: the corner's panel opens, or closes when it
 * is the one open (the same swipe again).
 */
static void
keyboard_commit(
	struct zwl_server *server,
	const char *via,
	float progress)
{
	enum keyboard_kind corner;

	/* The contact is over. */
	corner = keyboard.contact.corner;
	keyboard.contact.active = 0;
	printf("ZWL OSK commit corner=%s via=%s progress=%.0f\n", keyboard_kind_name(corner), via, (double)progress);

	/* The same panel again closes it. */
	if (keyboard.open == corner) {
		zwl_keyboard_close(server, "gesture");
		return;
	}

	/* Otherwise the corner's panel opens (in place of the other one). */
	keyboard_open(server, corner);
}

/* Opens a panel at its place for the screen's size. */
static void
keyboard_open(
	struct zwl_server *server,
	enum keyboard_kind kind)
{
	/* The panel and its rectangle. */
	keyboard.open = kind;
	keyboard.pressing = 0;
	keyboard_place(server, kind, keyboard.panel);
	server->dirty = 1;

	/* The log line the tests read. */
	printf("ZWL OSK open kind=%s x=%d y=%d width=%d height=%d\n", keyboard_kind_name(kind), keyboard.panel[0], keyboard.panel[1], keyboard.panel[2], keyboard.panel[3]);
}

/*
 * Works out a panel's rectangle (x, y, width, height) for the screen's
 * size: the flick panel at the bottom of the right side, its keys a share
 * of the height; the QWERTY panel along the bottom, a share of the height.
 */
static void
keyboard_place(
	struct zwl_server *server,
	enum keyboard_kind kind,
	int32_t *rect)
{
	int32_t width;
	int32_t height;
	int32_t key;

	/* The screen. */
	width = (int32_t)server->width;
	height = (int32_t)server->height;

	/* The QWERTY panel: the full width within the margins, a share of the height. */
	if (kind == PANEL_QWERTY) {
		rect[3] = height * KEYBOARD_QWERTY_SHARE / 100;
		if (rect[3] < KEYBOARD_QWERTY_MIN)
			rect[3] = KEYBOARD_QWERTY_MIN;
		if (rect[3] > KEYBOARD_QWERTY_MAX)
			rect[3] = KEYBOARD_QWERTY_MAX;
		rect[2] = width - 2 * KEYBOARD_MARGIN;
		rect[0] = KEYBOARD_MARGIN;
		rect[1] = height - KEYBOARD_MARGIN - rect[3];
		return;
	}

	/* The flick panel's key side. */
	key = height / KEYBOARD_KEY_DIVISOR;
	if (key < KEYBOARD_KEY_MIN)
		key = KEYBOARD_KEY_MIN;
	if (key > KEYBOARD_KEY_MAX)
		key = KEYBOARD_KEY_MAX;

	/* Its keys and gaps, the title band above them, at the bottom right. */
	rect[2] = KEYBOARD_FLICK_COLUMNS * key + (KEYBOARD_FLICK_COLUMNS + 1) * KEYBOARD_KEY_GAP;
	rect[3] = KEYBOARD_BAND + KEYBOARD_FLICK_ROWS * key + (KEYBOARD_FLICK_ROWS + 1) * KEYBOARD_KEY_GAP;
	rect[0] = width - KEYBOARD_MARGIN - rect[2];
	rect[1] = height - KEYBOARD_MARGIN - rect[3];
}

/*
 * Handles the left button on the open panel: a press on it is the
 * panel's (the close key's press closes it at the release); its release
 * ends it.  Returns 1 when the button was the panel's.
 */
static int
keyboard_panel_button(
	struct zwl_server *server,
	uint32_t button,
	uint32_t state)
{
	int32_t close[4];
	int inside;

	/* The release of a press the panel took. */
	(void)button;
	if (state == 0) {
		if (!keyboard.pressing)
			return 0;
		keyboard.pressing = 0;

		/* On the close key, the panel closes. */
		keyboard_close_rect(close);
		inside = keyboard_contains(close, server->pointer_x, server->pointer_y);
		if (inside)
			zwl_keyboard_close(server, "key");
		return 1;
	}

	/* A press off the panel is not the panel's. */
	inside = zwl_keyboard_at(server->pointer_x, server->pointer_y);
	if (!inside)
		return 0;

	/* Succeeded: the press is the panel's until its release. */
	keyboard.pressing = 1;
	printf("ZWL OSK panel-press x=%d y=%d\n", server->pointer_x, server->pointer_y);
	return 1;
}

/* Tells whether a point is in a rectangle (x, y, width, height). */
static int
keyboard_contains(
	const int32_t *rect,
	int32_t x,
	int32_t y)
{
	/* Left or right of it. */
	if (x < rect[0] || x >= rect[0] + rect[2])
		return 0;

	/* Above or below it. */
	if (y < rect[1] || y >= rect[1] + rect[3])
		return 0;

	/* In it. */
	return 1;
}

/* Works out the close key's rectangle, at the right of the open panel's title band. */
static void
keyboard_close_rect(
	int32_t *rect)
{
	/* A square in the band, a margin from the panel's right edge. */
	rect[2] = KEYBOARD_CLOSE;
	rect[3] = KEYBOARD_CLOSE;
	rect[0] = keyboard.panel[0] + keyboard.panel[2] - KEYBOARD_MARGIN - KEYBOARD_CLOSE;
	rect[1] = keyboard.panel[1] + (KEYBOARD_BAND - KEYBOARD_CLOSE) / 2 + 4;
}

/*
 * Draws a panel: its shadow, the white glass (as the volume's popup), the
 * title at the left of the band and the close key at its right.
 */
static void
keyboard_draw_panel(
	struct zwl_server *server,
	VkCommandBuffer command,
	const int32_t *rect,
	float opacity)
{
	static const float dark[4] = { 0.12f, 0.16f, 0.24f, 1.0f };
	static const float key[4] = { 1.0f, 1.0f, 1.0f, 0.55f };
	struct glass_shape shape;
	int32_t close[4];
	int32_t advance;
	const char *title;

	/* The shadow. */
	glass_shape_init(&shape, (float)rect[0], (float)rect[1] + 6.0f, (float)rect[2], (float)rect[3]);
	shape.quad[0] -= 40.0f;
	shape.quad[1] -= 40.0f;
	shape.quad[2] += 80.0f;
	shape.quad[3] += 80.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = KEYBOARD_RADIUS;
	shape.soft = 18.0f;
	shape.color[0] = 0.10f;
	shape.color[1] = 0.18f;
	shape.color[2] = 0.35f;
	shape.color[3] = 0.24f;
	shape.opacity = opacity;
	glass_shape_draw(server, command, &shape);

	/* The glass, as white as the volume's popup. */
	glass_shape_init(&shape, (float)rect[0], (float)rect[1], (float)rect[2], (float)rect[3]);
	shape.mode = MODE_GLASS;
	shape.radius = KEYBOARD_RADIUS;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = 0.86f;
	shape.edge = 0.85f;
	shape.opacity = opacity;
	glass_shape_draw(server, command, &shape);

	/* The hint draws the glass alone. */
	if (opacity < 1.0f)
		return;

	/* The title at the left of the band. */
	title = "Flick";
	if (keyboard.open == PANEL_QWERTY)
		title = "Keyboard";
	glass_draw_text(server, command, SIZE_TITLE, rect[0] + KEYBOARD_MARGIN + 4, rect[1] + KEYBOARD_BAND - 6, title, rect[2] - 3 * KEYBOARD_MARGIN - KEYBOARD_CLOSE, dark);

	/* The close key: a pale round key with the multiplication sign. */
	keyboard_close_rect(close);
	glass_draw_solid(server, command, (float)close[0], (float)close[1], (float)close[2], (float)close[3], (float)close[2] / 2.0f, key);
	advance = glass_glyph_advance(server, SIZE_SIGN, GLASS_CLOSE_GLYPH);
	glass_draw_glyph(server, command, SIZE_SIGN, GLASS_CLOSE_GLYPH, close[0] + (close[2] - advance) / 2, close[1] + close[3] / 2 + 7, dark);
}

/*
 * Draws the hint of an armed contact: the corner's panel, faint, its
 * opacity growing with how far the contact has come along the diagonal.
 */
static void
keyboard_draw_hint(
	struct zwl_server *server,
	VkCommandBuffer command)
{
	int32_t rect[4];
	int32_t inwards;
	int32_t upwards;
	float progress;

	/* How far along the diagonal. */
	keyboard_travel(&inwards, &upwards);
	progress = ((float)inwards + (float)upwards) * 0.5f / KEYBOARD_DISTANCE;
	if (progress < 0.0f)
		progress = 0.0f;
	if (progress > 1.0f)
		progress = 1.0f;

	/* The corner's panel at its place, faint. */
	keyboard_place(server, keyboard.contact.corner, rect);
	keyboard_draw_panel(server, command, rect, 0.2f + 0.6f * progress);
}

/* Names a panel (or a corner) for the log. */
static const char *
keyboard_kind_name(
	enum keyboard_kind kind)
{
	/* Each kind. */
	switch (kind) {
	case PANEL_FLICK:
		return "flick";
	case PANEL_QWERTY:
		return "qwerty";
	default:
		break;
	}

	/* None. */
	return "none";
}

/* Names a contact's source for the log. */
static const char *
keyboard_source_name(
	enum zwl_contact_source source)
{
	/* Each source. */
	switch (source) {
	case ZWL_CONTACT_TOUCH:
		return "touch";
	case ZWL_CONTACT_PEN:
		return "pen";
	default:
		break;
	}

	/* The pointer. */
	return "pointer";
}
