/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Calendar (WS155 p000): the mock of Keiland's calendar.  Only its face
 * exists: the months one under another with the events of four
 * calendars (test data in the program, data.c), a sidebar, a panel to add
 * an event by dragging its kind onto a date, and a desk calendar drawn in
 * 3D (render3d.h, scene.h) that breathes slowly and turns its page when
 * the date shown changes.  Nothing is stored; an event dropped on a date
 * lasts while the program runs.
 *
 * The view (view.c) draws a frame with libkeiland's canvas and widgets and
 * knows nothing of the window, so that the host tests draw it into
 * pictures; the window (main.c) feeds it the input and the time.
 */

#ifndef CALENDAR_CALENDAR_H
#define CALENDAR_CALENDAR_H

#include "scene.h"

#include <keiland.h>

#include <stddef.h>
#include <stdint.h>

/* The calendars, in the sidebar's order. */
enum cal_list {
	CAL_WORK,
	CAL_PERSONAL,
	CAL_FAMILY,
	CAL_STUDY,
	CAL_LISTS
};

/* A day: the year, the month (1 to 12) and the day of the month. */
struct cal_date {
	int year;
	int month;
	int day;
};

/*
 * One event of the test data: its month counted from today's (-1 the one
 * before), its day of that month (0 for today itself), its calendar, its
 * title and its time (NULL for all day).
 */
struct cal_event {
	int month_offset;
	int day;
	enum cal_list list;
	const char *title;
	const char *time;
};

/* An event dropped on a date while the program runs. */
struct cal_added {
	struct cal_date date;
	enum cal_list list;
};

/* The actions of the menu, the keys and the buttons. */
#define CAL_ACTION_TODAY	1U
#define CAL_ACTION_PREVIOUS	2U
#define CAL_ACTION_NEXT		3U
#define CAL_ACTION_MOTION	4U
#define CAL_ACTION_QUIT		5U

/* The most events dropped, cells remembered from a frame, and months shown. */
#define CAL_ADDED_MAX		32U
#define CAL_CELLS_MAX		512U
#define CAL_MONTHS		13

/* A cell of the frame drawn: where it is and its day (the drop of a drag finds it). */
struct cal_cell {
	struct kl_rect rect;
	struct cal_date date;
};

/*
 * The view's state.
 *
 * The days: today, the one chosen, the one the desk calendar shows, and
 * the first month of the scroll (six before today's).  The scroll of the
 * months, the month it is to go to at the next frame (0 for none), the
 * search field, and the calendars hidden (a bit each).
 *
 * The motion: whether it is reduced (no breathing, pages and cells change
 * at once), when the view began (the breathing's clock), a page turning
 * (when it began, from which day to which), a cell sinking after a drop
 * (when and which), and a drag of a kind of event (its kind, -1 for
 * none, and whether it has moved off its card).
 *
 * The events dropped, the cells of the last frame, the notice shown at the
 * bottom until a time (empty for none), whether the window stands on glass, and whether
 * the program is to end.
 *
 * The 3D: the desk calendar's target and picture, its mesh, the icons of
 * the kinds of event, and the pictures of the two pages with their days.
 */
struct cal_view {
	struct cal_date today;
	struct cal_date selected;
	struct cal_date shown;
	struct cal_date first_month;
	struct kl_scroll scroll;
	int scroll_to;
	int scroll_glide;
	struct kl_field search;
	unsigned hidden;

	int reduce_motion;
	uint64_t started_us;
	int flipping;
	uint64_t flip_us;
	struct cal_date flip_to;
	int sinking;
	uint64_t sink_us;
	struct cal_date sink_date;
	int dragging;
	int drag_moved;
	double drag_from_x;
	double drag_from_y;

	struct cal_added added[CAL_ADDED_MAX];
	size_t added_count;
	struct cal_cell cells[CAL_CELLS_MAX];
	size_t cell_count;
	char notice[128];
	uint64_t notice_until;
	int glass;
	int quit;

	struct r3_target target;
	struct kl_image picture;
	struct sc_mesh *mesh;
	struct kl_image icons[SC_ICONS];
	struct kl_image pages[SC_TEXTURES];
	struct cal_date page_dates[SC_TEXTURES];
};

/* The days (date.c). */
int cal_days_in_month(int year, int month);
int cal_weekday(const struct cal_date *date);
void cal_add_months(struct cal_date *date, int months);
void cal_add_days(struct cal_date *date, int days);
int cal_same_day(const struct cal_date *a, const struct cal_date *b);
int cal_month_index(const struct cal_date *first, const struct cal_date *date);
const char *cal_month_name(int month);
const char *cal_weekday_name(int weekday);

/* The test data (data.c). */
const struct cal_event *cal_events(size_t *count);
const char *cal_list_name(enum cal_list list);
kl_color cal_list_color(enum cal_list list);
void cal_event_date(const struct cal_event *event, const struct cal_date *today, struct cal_date *date);

/* The view (view.c). */
int cal_view_init(struct cal_view *view, const struct cal_date *today, uint64_t now_us);
void cal_view_release(struct cal_view *view);
void cal_view_action(struct cal_view *view, unsigned action, uint64_t now_us);
void cal_view_key(struct cal_view *view, uint32_t key, unsigned modifiers, uint64_t now_us);
void cal_view_draw(struct cal_view *view, struct kl_ui *ui, const struct kl_style *style, int width, int height, uint64_t now_us);
size_t cal_view_panels(const struct cal_view *view, int width, int height, struct kl_glass_panel *panels, size_t capacity);
int cal_view_wait(const struct cal_view *view, uint64_t now_us);

/* The log for the tests (main.c, and the host tests' own). */
void cal_log(const char *format, ...);

#endif
