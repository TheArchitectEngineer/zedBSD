/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Phone (WS170 p000): the mock of the application that puts a person's
 * messages (SMS, MMS, RCS) and calls (the line's and VoIP) in one
 * timeline.  Only its face exists: the contacts and their timelines are
 * test data kept in the program (data.c), nothing is stored, and there is
 * no backend -- sending a message or calling shows that there is none.
 *
 * The view (view.c) draws a frame with libkeiland's canvas and widgets and
 * knows nothing of the window, so that the host tests draw it into a
 * picture; the window (main.c) feeds it the input.
 */

#ifndef PHONE_PHONE_H
#define PHONE_PHONE_H

#include <keiland.h>

#include <stddef.h>
#include <stdint.h>

/* What a timeline item is: a message, a call, a picture or another file. */
enum ph_kind {
	PH_TEXT,
	PH_CALL,
	PH_PHOTO,
	PH_FILE
};

/* The way an item went: the carrier's messages, RCS, the line's call, or a VoIP call. */
enum ph_channel {
	PH_SMS,
	PH_MMS,
	PH_RCS,
	PH_LINE,
	PH_VOIP
};

/*
 * One item of a timeline: its kind and channel, whether it went out, the
 * day and time it happened, and its text -- a message's words, a file's
 * name, a picture's caption (NULL for none) -- with a detail: a message's
 * state ("Read 10:02"), a call's length (NULL for a missed call), a file's
 * type and size.
 */
struct ph_item {
	enum ph_kind kind;
	enum ph_channel channel;
	int outgoing;
	const char *day;
	const char *time;
	const char *text;
	const char *detail;
};

/*
 * One contact: the name, the number, the initials and color of the
 * picture standing for the person, the messages not read, and the
 * timeline (oldest first).
 */
struct ph_contact {
	const char *name;
	const char *number;
	const char *initials;
	kl_color color;
	unsigned unread;
	const struct ph_item *items;
	size_t item_count;
};

/* The actions of the menu, the keys and the buttons. */
#define PH_ACTION_SEND		1U
#define PH_ACTION_CALL		2U
#define PH_ACTION_ATTACH	3U
#define PH_ACTION_QUIT		4U

/* The longest the contacts' filter keeps, with its NUL. */
#define PH_FILTER_MAX		64U

/*
 * The view's state: the search field and its filter, the message being
 * written, the scrolls of the contacts and of the timeline, the contact
 * shown (-1 for none), whether a narrow window shows the timeline instead
 * of the contacts (and whether the last frame was narrow), whether the
 * view stands on zdesktop's glass (cards with the desktop between), whether the
 * timeline goes to its end at the next frame, the contacts whose messages
 * were read (a bit each of the first 32), the notice shown at the bottom
 * until a time, and whether the program is to end.
 */
struct ph_view {
	struct kl_field search;
	struct kl_field message;
	struct kl_scroll contacts_scroll;
	struct kl_scroll timeline_scroll;
	long selected;
	int opened;
	int narrow;
	int glass;
	int to_end;
	unsigned long seen;
	const char *notice;
	uint64_t notice_until;
	int quit;
};

/* The test data (data.c). */
const struct ph_contact *ph_contacts(size_t *count);

/* The view (view.c). */
int ph_view_init(struct ph_view *view);
void ph_view_release(struct ph_view *view);
void ph_view_select(struct ph_view *view, long index);
void ph_view_action(struct ph_view *view, unsigned action, uint64_t now_us);
void ph_view_key(struct ph_view *view, uint32_t key, unsigned modifiers, uint64_t now_us);
void ph_view_draw(struct ph_view *view, struct kl_ui *ui, const struct kl_style *style, int width, int height, uint64_t now_us);
size_t ph_view_panels(struct ph_view *view, int width, int height, struct kl_glass_panel *panels, size_t capacity);
int ph_view_wait(const struct ph_view *view, uint64_t now_us);

/* The log for the tests (main.c, and the host tests' own). */
void ph_log(const char *format, ...);

#endif
