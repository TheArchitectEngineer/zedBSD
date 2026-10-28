/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Calendar's test data for the host test of its view (WS155 p000; moved
 * out of the program by p003, now kept in a store in a folder): events of
 * the four calendars, made up for the mock.  The events are placed by their month counted
 * from today's, so that the months round today always have some.
 */

#include "userland/desktop/calendar/calendar.h"

#include <stdio.h>
#include <string.h>

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

int test_calendar_data_load(const char *folder, const struct cal_date *today);

/* The events. */
static const struct cal_event data_events[] = {
	{ -1, 3, CAL_WORK, "Kick-off", "10:00" },
	{ -1, 11, CAL_FAMILY, "Sports Day", NULL },
	{ -1, 17, CAL_STUDY, "Essay Due", "17:00" },
	{ -1, 24, CAL_PERSONAL, "Dentist", "09:30" },
	{ 0, 0, CAL_WORK, "Design Review", "11:00" },
	{ 0, 0, CAL_PERSONAL, "Gym", "18:30" },
	{ 0, 2, CAL_WORK, "Team Meeting", "10:00" },
	{ 0, 6, CAL_PERSONAL, "Doctor Appointment", "15:30" },
	{ 0, 6, CAL_STUDY, "Study Session", "19:00" },
	{ 0, 9, CAL_FAMILY, "Kids' Event", NULL },
	{ 0, 12, CAL_WORK, "Project Review", "14:00" },
	{ 0, 13, CAL_STUDY, "Library", "16:00" },
	{ 0, 16, CAL_WORK, "1:1 with Ken", "13:00" },
	{ 0, 16, CAL_PERSONAL, "Concert", "19:30" },
	{ 0, 19, CAL_FAMILY, "Grandma's Birthday", NULL },
	{ 0, 21, CAL_WORK, "Sprint Planning", "10:00" },
	{ 0, 21, CAL_STUDY, "Lecture", "18:00" },
	{ 0, 21, CAL_FAMILY, "Parents' Meeting", "19:30" },
	{ 0, 23, CAL_PERSONAL, "Yoga", "07:00" },
	{ 0, 26, CAL_STUDY, "Exam Prep", "20:00" },
	{ 0, 28, CAL_WORK, "Quarterly Report", NULL },
	{ 0, 30, CAL_FAMILY, "Halloween Party", "17:00" },
	{ 1, 2, CAL_WORK, "Team Meeting", "10:00" },
	{ 1, 5, CAL_STUDY, "Exam", "09:00" },
	{ 1, 8, CAL_FAMILY, "Trip to Hakone", NULL },
	{ 1, 9, CAL_FAMILY, "Trip to Hakone", NULL },
	{ 1, 14, CAL_PERSONAL, "Haircut", "11:00" },
	{ 1, 20, CAL_WORK, "Release", NULL },
	{ 1, 27, CAL_PERSONAL, "Dinner with Aiko", "19:00" },
	{ 2, 4, CAL_WORK, "Year Plan", "10:00" },
	{ 2, 12, CAL_STUDY, "Final Exam", "09:00" },
	{ 2, 24, CAL_FAMILY, "Christmas Eve", NULL },
	{ 2, 31, CAL_PERSONAL, "New Year's Eve", NULL }
};

/*
 * Opens Calendar's store in a folder and keeps the test data's events in
 * it, placed from today (a day past its month's end is left out).
 * Returns 0 or an errno value.
 */
int
test_calendar_data_load(
	const char *folder,
	const struct cal_date *today)
{
	const struct cal_event *event;
	struct cal_item item;
	size_t index;
	long kept;
	int length;
	int hour;
	int minute;
	int error;

	/* The store. */
	error = cal_store_open(folder);
	if (error != 0)
		return error;

	/* Each event. */
	for (index = 0; index < sizeof(data_events) / sizeof(data_events[0]); index++) {
		event = &data_events[index];

		/* Its day: today's month moved by its own, on its day (today's when 0). */
		memset(&item, 0, sizeof(item));
		item.date = *today;
		item.date.day = 1;
		cal_add_months(&item.date, event->month_offset);
		item.date.day = event->day;
		if (event->day == 0 && event->month_offset == 0)
			item.date.day = today->day;
		length = cal_days_in_month(item.date.year, item.date.month);
		if (item.date.day > length)
			continue;

		/* Its calendar, title and time (an hour long; all day without one). */
		item.list = event->list;
		(void)snprintf(item.title, sizeof(item.title), "%s", event->title);
		item.all_day = 1;
		if (event->time != NULL && sscanf(event->time, "%d:%d", &hour, &minute) == 2) {
			item.all_day = 0;
			item.start = hour * 60 + minute;
			item.end = item.start + 60;
		}

		/* Kept. */
		error = cal_store_save_event(-1, &item, &kept);
		if (error != 0)
			return error;
	}

	/* The application's memo of the mock. */
	error = cal_store_set_memo("Ideas for the weekend:\n- walk along the river\n- call Grandma\n- film for the camera");
	if (error != 0)
		return error;

	/* Succeeded: the store has the test data. */
	return 0;
}
