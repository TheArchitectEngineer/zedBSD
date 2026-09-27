/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The list view of files (spec §12): a header of columns and one
 * row an item.
 *
 * The name column takes what the others leave.  Which columns show is the
 * app's choice (View > List Columns); the places whose items come from
 * many folders (search, recent files, the trash) add where each item is.
 * Clicking a column's title sorts by it, clicking again reverses.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* The header's and a row's heights. */
#define LIST_HEADER		FM_LIST_HEADER
#define LIST_ROW		FM_LIST_ROW

/* The text sizes of the view. */
#define LIST_TEXT		13U
#define LIST_TEXT_HEADER	12U

/* The widest a list may show columns: the name, and the six others at most. */
#define LIST_COLUMNS		8

/*
 * One column as it is laid out for a frame: which one, where and how wide.
 */
struct list_column {
	unsigned column;
	int x;
	int width;
};

static int list_layout(struct fm_app *app, const struct fm_rect *inner, struct list_column *columns);
static void list_header(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *inner, const struct list_column *columns, int count);
static void list_row(struct fm_app *app, struct fm_canvas *canvas, struct fm_entry *entry, int index, const struct fm_rect *row, const struct list_column *columns, int count);
static void list_cell_text(struct fm_app *app, struct fm_entry *entry, unsigned column, char *text, size_t size);
static const char *list_title(unsigned column);
static unsigned list_sort_of(unsigned column);
static int list_tag_count(unsigned tags);
static void list_tag_dots(struct fm_app *app, struct fm_canvas *canvas, unsigned tags, int x, int y);

/*
 * Draws the items as a list in the panel's inner rectangle, and records
 * the header's titles and each row.
 */
void
fm_list_draw(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *inner)
{
	struct list_column columns[LIST_COLUMNS];
	struct fm_rect rows;
	struct fm_rect row;
	struct fm_tab *tab;
	size_t index;
	int count;

	/* The columns and the header. */
	tab = fm_ui_tab(app);
	count = list_layout(app, inner, columns);
	list_header(app, canvas, inner, columns, count);

	/* The rows' part, under the header. */
	rows.x = inner->x;
	rows.y = inner->y + LIST_HEADER;
	rows.width = inner->width;
	rows.height = inner->height - LIST_HEADER;
	app->layout.columns = 1;
	app->layout.cell_width = inner->width;
	app->layout.cell_height = LIST_ROW;
	app->layout.content_height = (int)tab->listing.count * LIST_ROW + LIST_HEADER + 64;

	/* Each row in sight. */
	fm_canvas_clip_push(canvas, &rows);
	for (index = 0; index < tab->listing.count; index++) {
		row.x = rows.x;
		row.y = rows.y + (int)index * LIST_ROW - tab->scroll;
		row.width = rows.width;
		row.height = LIST_ROW;
		if (row.y + LIST_ROW < rows.y || row.y > rows.y + rows.height)
			continue;
		list_row(app, canvas, &tab->listing.entries[index], (int)index, &row, columns, count);
	}

	/* The rows' clip ends. */
	fm_canvas_clip_pop(canvas);
}

/*
 * Writes a time the way the lists show it: "Today 16:20", "Yesterday
 * 20:18", "Sep 21 14:03" in this year, "Sep 21, 2025" before.
 */
void
fm_time_text(
	time_t when,
	time_t now,
	char *text,
	size_t size)
{
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	struct tm moment;
	struct tm today;
	struct tm *converted;
	long days;

	/* The time and today, in local time. */
	converted = localtime(&when);
	if (converted == NULL) {
		snprintf(text, size, "--");
		return;
	}

	/* The time taken, before the next conversion reuses the buffer. */
	moment = *converted;
	converted = localtime(&now);
	if (converted == NULL) {
		snprintf(text, size, "--");
		return;
	}

	/* Today's date. */
	today = *converted;

	/* How many calendar days ago (in this year). */
	days = -1;
	if (moment.tm_year == today.tm_year)
		days = (long)today.tm_yday - (long)moment.tm_yday;

	/* Today and yesterday are named. */
	if (days == 0) {
		snprintf(text, size, "Today %02d:%02d", moment.tm_hour, moment.tm_min);
		return;
	}

	/* Yesterday is named too. */
	if (days == 1) {
		snprintf(text, size, "Yesterday %02d:%02d", moment.tm_hour, moment.tm_min);
		return;
	}

	/* Earlier this year: the day and the time. */
	if (moment.tm_year == today.tm_year) {
		snprintf(text, size, "%s %d %02d:%02d", months[moment.tm_mon], moment.tm_mday, moment.tm_hour, moment.tm_min);
		return;
	}

	/* Another year: the date. */
	snprintf(text, size, "%s %d, %d", months[moment.tm_mon], moment.tm_mday, moment.tm_year + 1900);
}

/*
 * Reports the column whose title is at a point of the header, as a sort
 * (FM_SORT_*); -1 when the point is on no sortable title.  The layout is
 * the last frame's.
 */
int
fm_list_sort_at(
	struct fm_app *app,
	int index)
{
	/* The header's hit carries the column; its sort. */
	(void)app;
	if (index < 0)
		return -1;

	/* Reports the sort of that column. */
	return (int)list_sort_of((unsigned)index);
}

/* Lays out the columns shown, and returns how many there are. */
static int
list_layout(
	struct fm_app *app,
	const struct fm_rect *inner,
	struct list_column *columns)
{
	static const int widths[FM_COLUMN_COUNT] = { 0, 150, 90, 150, 150, 120, 110, 200, 150 };
	const struct fm_location *location;
	struct fm_tab *tab;
	unsigned shown;
	unsigned column;
	struct list_column swap;
	int count;
	int right;
	int first;
	int last;

	/* The columns the app shows, and those the place adds. */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;
	shown = app->columns | (1U << FM_COLUMN_NAME);
	if (location->kind == FM_LOCATION_SEARCH || location->kind == FM_LOCATION_RECENTS || location->kind == FM_LOCATION_TAG)
		shown |= 1U << FM_COLUMN_LOCATION;
	if (location->kind == FM_LOCATION_TRASH) {
		shown |= 1U << FM_COLUMN_LOCATION;
		shown |= 1U << FM_COLUMN_DELETED;
	}

	/* The other columns from the right, while the name keeps 200 pixels. */
	count = 1;
	right = inner->x + inner->width - 8;
	for (column = FM_COLUMN_COUNT - 1U; column > FM_COLUMN_NAME; column--) {
		if ((shown & (1U << column)) == 0U)
			continue;
		if (right - widths[column] < inner->x + 200)
			continue;
		right -= widths[column];
		columns[count].column = column;
		columns[count].x = right;
		columns[count].width = widths[column];
		count++;
	}

	/* They were laid out right to left: put them in order after the name. */
	first = 1;
	last = count - 1;
	while (first < last) {
		swap = columns[first];
		columns[first] = columns[last];
		columns[last] = swap;
		first++;
		last--;
	}

	/* The name takes the rest. */
	columns[0].column = FM_COLUMN_NAME;
	columns[0].x = inner->x + 8;
	columns[0].width = right - columns[0].x;

	/* Reports how many columns there are. */
	return count;
}

/* Draws the header: each column's title, the sorted one with its direction. */
static void
list_header(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *inner,
	const struct list_column *columns,
	int count)
{
	struct fm_rect title;
	fm_color ink;
	unsigned sort;
	int baseline;
	int width;
	int index;

	/* A thin line under the header. */
	title.x = inner->x;
	title.y = inner->y + LIST_HEADER - 1;
	title.width = inner->width;
	title.height = 1;
	fm_canvas_fill(canvas, &title, FM_COLOR_SEPARATOR);

	/* Each column's title. */
	baseline = fm_text_center(LIST_TEXT_HEADER, inner->y, LIST_HEADER);
	for (index = 0; index < count; index++) {
		title.x = columns[index].x;
		title.y = inner->y;
		title.width = columns[index].width;
		title.height = LIST_HEADER;

		/* The title lit under the pointer, darker when it is the sort. */
		sort = list_sort_of(columns[index].column);
		ink = FM_COLOR_TEXT_SECONDARY;
		if (sort == app->sort)
			ink = FM_COLOR_TEXT;
		if (app->hover_kind == FM_HIT_HEADER && app->hover_index == (int)columns[index].column)
			fm_canvas_round(canvas, (float)title.x - 4.0f, (float)title.y + 3.0f, (float)title.width, (float)title.height - 6.0f, 6.0f, FM_COLOR_HOVER);
		width = fm_text_draw_fit(app->text, canvas, title.x + 4, baseline, list_title(columns[index].column), LIST_TEXT_HEADER, 1, title.width - 24, ink);

		/* The sort's direction after its title. */
		if (sort == app->sort) {
			if (app->sort_reverse != 0)
				fm_icon_draw(canvas, FM_ICON_UP, (float)(title.x + width + 8), (float)(baseline - 11), 12.0f, ink);
			else
				fm_icon_draw(canvas, FM_ICON_DOWN, (float)(title.x + width + 8), (float)(baseline - 11), 12.0f, ink);
		}

		/* A sortable title can be clicked. */
		if (sort != FM_SORT_COUNT)
			fm_ui_hit(app, &title, FM_HIT_HEADER, (int)columns[index].column);
	}
}

/* Draws one row: its ground, the icon and the name, and the other cells. */
static void
list_row(
	struct fm_app *app,
	struct fm_canvas *canvas,
	struct fm_entry *entry,
	int index,
	const struct fm_rect *row,
	const struct list_column *columns,
	int count)
{
	struct fm_rect field;
	char text[FM_PATH_MAX];
	fm_color ink;
	fm_color faint;
	int renaming;
	int match;
	int baseline;
	int column;
	int width;
	int drawn;

	/* The ground: the accent when selected, faint under the pointer. */
	ink = FM_COLOR_TEXT;
	faint = FM_COLOR_TEXT_SECONDARY;
	if (entry->selected != 0) {
		if (app->focused != 0) {
			fm_canvas_round(canvas, (float)row->x, (float)row->y + 1.0f, (float)row->width, (float)row->height - 2.0f, 7.0f, FM_COLOR_ACCENT);
			ink = FM_RGB(0xffffff);
			faint = FM_RGBA(0xffffff, 210);
		} else {
			fm_canvas_round(canvas, (float)row->x, (float)row->y + 1.0f, (float)row->width, (float)row->height - 2.0f, 7.0f, FM_COLOR_SELECTION_INACTIVE);
		}
	} else if (app->hover_kind == FM_HIT_ITEM && app->hover_index == index) {
		fm_canvas_round(canvas, (float)row->x, (float)row->y + 1.0f, (float)row->width, (float)row->height - 2.0f, 7.0f, FM_COLOR_HOVER);
	}

	/* The small icon (faded when cut) and the name, or the field while it is being changed. */
	baseline = fm_text_center(LIST_TEXT, row->y, row->height);
	fm_grid_entry_icon(app, canvas, entry, (float)columns[0].x, (float)row->y + 3.0f, 22.0f);
	if (entry->cut != 0)
		fm_canvas_round(canvas, (float)columns[0].x, (float)row->y + 3.0f, 22.0f, 22.0f, 4.0f, FM_RGBA(0xffffff, 150));
	width = columns[0].width - 34;
	renaming = 0;
	if (app->focus == FM_FOCUS_RENAME) {
		match = strcmp(entry->path, app->rename_path);
		if (match == 0)
			renaming = 1;
	}

	/* The field where the name was, or the name. */
	if (renaming != 0) {
		field.x = columns[0].x + 26;
		field.y = row->y + 3;
		field.width = width + 4;
		field.height = row->height - 6;
		fm_canvas_round(canvas, (float)field.x, (float)field.y, (float)field.width, (float)field.height, 5.0f, FM_COLOR_PANEL);
		fm_canvas_round_border(canvas, (float)field.x, (float)field.y, (float)field.width, (float)field.height, 5.0f, 1.5f, FM_COLOR_ACCENT);
		field.x += 4;
		field.width -= 8;
		fm_field_draw(app, canvas, &app->rename, &field, LIST_TEXT, NULL);
	} else {
		drawn = fm_text_draw_fit(app->text, canvas, columns[0].x + 30, baseline, entry->name, LIST_TEXT, 0, width - 8 * list_tag_count(entry->tags), ink);
		list_tag_dots(app, canvas, entry->tags, columns[0].x + 30 + drawn + 10, row->y + row->height / 2);
	}

	/* The other columns' cells. */
	for (column = 1; column < count; column++) {
		list_cell_text(app, entry, columns[column].column, text, sizeof(text));

		/* Sizes are aligned to the right of their column. */
		if (columns[column].column == FM_COLUMN_SIZE) {
			width = fm_text_width(app->text, text, strlen(text), LIST_TEXT, 0);
			(void)fm_text_draw(app->text, canvas, columns[column].x + columns[column].width - 16 - width, baseline, text, strlen(text), LIST_TEXT, 0, faint);
			continue;
		}

		/* The others to the left. */
		(void)fm_text_draw_fit(app->text, canvas, columns[column].x + 4, baseline, text, LIST_TEXT, 0, columns[column].width - 12, faint);
	}

	/* The row can be clicked. */
	fm_ui_hit(app, row, FM_HIT_ITEM, index);
}

/* Writes what an entry shows in a column. */
static void
list_cell_text(
	struct fm_app *app,
	struct fm_entry *entry,
	unsigned column,
	char *text,
	size_t size)
{
	/* Each column's text. */
	text[0] = '\0';
	switch (column) {
	case FM_COLUMN_KIND:
		snprintf(text, size, "%s", entry->mime->kind);
		break;
	case FM_COLUMN_SIZE:
		if (entry->folder != 0)
			snprintf(text, size, "--");
		else
			fm_dir_size_text(entry->size, text, size);
		break;
	case FM_COLUMN_MODIFIED:
		fm_time_text(entry->modified, (time_t)app->wall, text, size);
		break;
	case FM_COLUMN_CHANGED:
		fm_time_text(entry->changed, (time_t)app->wall, text, size);
		break;
	case FM_COLUMN_DELETED:
		fm_time_text(entry->extra_time, (time_t)app->wall, text, size);
		break;
	case FM_COLUMN_OWNER:
		fm_owner_text(entry->uid, entry->gid, text, size);
		break;
	case FM_COLUMN_TAGS:
		fm_tags_text(app, entry->tags, text, size);
		break;
	case FM_COLUMN_LOCATION:
		if (entry->detail != NULL)
			snprintf(text, size, "%s", entry->detail);
		break;
	default:
		break;
	}
}

/* Names a column in the header. */
static const char *
list_title(
	unsigned column)
{
	/* Each column's title. */
	switch (column) {
	case FM_COLUMN_NAME:
		return "Name";
	case FM_COLUMN_KIND:
		return "Kind";
	case FM_COLUMN_SIZE:
		return "Size";
	case FM_COLUMN_MODIFIED:
		return "Date Modified";
	case FM_COLUMN_CHANGED:
		return "Date Changed";
	case FM_COLUMN_TAGS:
		return "Tags";
	case FM_COLUMN_OWNER:
		return "Owner";
	case FM_COLUMN_LOCATION:
		return "Location";
	case FM_COLUMN_DELETED:
		return "Date Deleted";
	default:
		break;
	}

	/* A column this program does not know. */
	return "";
}

/* Reports the sort a column's title chooses (FM_SORT_COUNT for none). */
static unsigned
list_sort_of(
	unsigned column)
{
	/* The columns that sort. */
	switch (column) {
	case FM_COLUMN_NAME:
		return FM_SORT_NAME;
	case FM_COLUMN_KIND:
		return FM_SORT_KIND;
	case FM_COLUMN_SIZE:
		return FM_SORT_SIZE;
	case FM_COLUMN_MODIFIED:
		return FM_SORT_MODIFIED;
	default:
		break;
	}

	/* The others do not. */
	return FM_SORT_COUNT;
}

/* Counts the tags of a mask. */
static int
list_tag_count(
	unsigned tags)
{
	int count;

	/* Each bit set. */
	count = 0;
	while (tags != 0U) {
		count += (int)(tags & 1U);
		tags >>= 1;
	}

	/* Reports the count. */
	return count;
}

/* Draws the dots of an item's tags after its name, overlapping. */
static void
list_tag_dots(
	struct fm_app *app,
	struct fm_canvas *canvas,
	unsigned tags,
	int x,
	int y)
{
	int index;

	/* Each tag's dot in the sidebar's order. */
	for (index = 0; index < app->tags.count; index++) {
		if ((tags & (1U << index)) == 0U)
			continue;
		fm_icon_tag(canvas, (float)x, (float)y, 4.5f, app->tags.items[index].color);
		x += 8;
	}
}
