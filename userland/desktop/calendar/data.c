/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Calendar's test data (WS155 p000): four calendars and their events,
 * made up for the mock.  The events are placed by their month counted
 * from today's, so that the months round today always have some.
 */

#include "calendar.h"

/* The calendars' names and colors. */
static const char *const data_names[CAL_LISTS] = { "Work", "Personal", "Family", "Study" };
static const kl_color data_colors[CAL_LISTS] = {
	KL_RGB(0x3b82f6),
	KL_RGB(0xef4444),
	KL_RGB(0x22c55e),
	KL_RGB(0xf5b82e)
};

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
 * Reports the events and how many there are.
 */
const struct cal_event *
cal_events(
	size_t *count)
{
	/* The array of the program. */
	*count = sizeof(data_events) / sizeof(data_events[0]);
	return data_events;
}

/*
 * Reports a calendar's name.
 */
const char *
cal_list_name(
	enum cal_list list)
{
	/* A calendar not known. */
	if ((unsigned)list >= CAL_LISTS)
		return "";

	/* Its name. */
	return data_names[list];
}

/*
 * Reports a calendar's color.
 */
kl_color
cal_list_color(
	enum cal_list list)
{
	/* A calendar not known is grey. */
	if ((unsigned)list >= CAL_LISTS)
		return KL_RGB(0x8a96aa);

	/* Its color. */
	return data_colors[list];
}

/*
 * Finds the day of an event of the test data, from today; a day past its
 * month's end is 0 (the event is not shown).
 */
void
cal_event_date(
	const struct cal_event *event,
	const struct cal_date *today,
	struct cal_date *date)
{
	int length;

	/* Today's month moved by the event's, on its day (today's when 0). */
	*date = *today;
	date->day = 1;
	cal_add_months(date, event->month_offset);
	date->day = event->day;
	if (event->day == 0 && event->month_offset == 0)
		date->day = today->day;

	/* A day the month does not have. */
	length = cal_days_in_month(date->year, date->month);
	if (date->day > length)
		date->day = 0;
}
