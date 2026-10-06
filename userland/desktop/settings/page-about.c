/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The About page: the Kei mark and name, then two cards of values, this
 * computer (its name, processor, graphics and screen) and its software
 * (Kei, the kernel, the architecture and how long it has run).
 */

#include "settings.h"

#include <stdio.h>
#include <string.h>

/* The hero card's height, the mark's size in it, and the name's and the tagline's text sizes. */
#define ABOUT_HERO_HEIGHT	148
#define ABOUT_MARK		100U
#define ABOUT_TEXT_NAME		44U
#define ABOUT_TEXT_TAGLINE	14U

/* The space between two cards, and a button's height. */
#define ABOUT_CARD_GAP		18
#define ABOUT_BUTTON_HEIGHT	32

/*
 * One row of a card of values: its label and its value.  The rows of a
 * card are worked out for each frame; the texts belong to the caller and
 * outlive the card's drawing.
 */
struct about_row {
	const char *label;
	const char *value;
};

static int about_hero(struct se_app *app, struct kl_canvas *canvas, int x, int top, int width);
static int about_card(struct se_app *app, struct kl_canvas *canvas, int x, int top, int width, const char *title, const struct about_row *rows, int count);
static int about_add(struct about_row *rows, int count, const char *label, const char *value);
static void about_uptime(uint64_t milliseconds, char *text, size_t size);

/*
 * Draws the About page's cards from a top edge; returns the edge below
 * them.
 */
int
se_about_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct about_row rows[6];
	const struct se_about *about;
	const char *system;
	char text[64];
	int count;
	int y;

	/* The mark and the name. */
	about = &app->about;
	y = about_hero(app, canvas, x, top, width);

	/* This computer: the values that could be read. */
	count = 0;
	count = about_add(rows, count, "Computer name", about->host);
	count = about_add(rows, count, "Processor", about->processor);
	text[0] = '\0';
	if (about->cores > 0U)
		(void)snprintf(text, sizeof(text), "%u", about->cores);
	count = about_add(rows, count, "Processors", text);
	count = about_add(rows, count, "Graphics", about->graphics);
	count = about_add(rows, count, "Display", about->display);
	y = about_card(app, canvas, x, y + ABOUT_CARD_GAP, width, "This computer", rows, count);

	/* Software: the system's version (Kei without os-release), the kernel, the architecture and the time since the machine started. */
	count = 0;
	system = "Kei";
	if (about->system[0] != '\0')
		system = about->system;
	count = about_add(rows, count, "Operating system", system);
	count = about_add(rows, count, "Kernel", about->kernel);
	count = about_add(rows, count, "Architecture", about->machine);
	about_uptime(app->now, text, sizeof(text));
	count = about_add(rows, count, "Up for", text);
	y = about_card(app, canvas, x, y + ABOUT_CARD_GAP, width, "Software", rows, count);

	/* The Welcome again (ws164-p002). */
	y += ABOUT_CARD_GAP;
	(void)se_button_draw(app, canvas, x, y, kl_tr("Show Welcome again"), 0, 1, SE_ABOUT_WELCOME);

	/* The edge below the button. */
	return y + ABOUT_BUTTON_HEIGHT;
}

/*
 * Carries out a press of the About page's control: "Show Welcome again"
 * starts the Welcome in the window (ws164-p002).
 */
void
se_about_press(
	struct se_app *app,
	int index)
{
	/* The Welcome. */
	if (index == SE_ABOUT_WELCOME)
		se_welcome_start(app);
}

/* Draws the hero card: the Kei mark, the word Kei and the tagline; returns the edge below it. */
static int
about_hero(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct kl_text_line name;
	struct kl_text_line tagline;
	int left;
	int baseline;

	/* The card, untitled. */
	(void)se_card_begin(app, canvas, x, top, width, ABOUT_HERO_HEIGHT, NULL, NULL);

	/* The mark at the card's left, in the middle of its height. */
	se_mark_draw(canvas, x + 28, top + (ABOUT_HERO_HEIGHT - (int)ABOUT_MARK) / 2, ABOUT_MARK, 1.0f);

	/* The word Kei (three letters, never a lone K), in slate, beside the mark. */
	kl_text_metrics(app->text, ABOUT_TEXT_NAME, &name);
	kl_text_metrics(app->text, ABOUT_TEXT_TAGLINE, &tagline);
	left = x + 28 + (int)ABOUT_MARK + 24;
	baseline = top + (ABOUT_HERO_HEIGHT - name.height - 6 - tagline.height) / 2 + name.ascent;
	(void)kl_text_draw(app->text, canvas, left, baseline, "Kei", 3U, ABOUT_TEXT_NAME, 0, SE_COLOR_TITLE);

	/* The tagline under it: the kernel's name as it is written. */
	baseline += name.descent + 6 + tagline.ascent;
	(void)kl_text_draw_fit(app->text, canvas, left, baseline, "powered by zedBSD", ABOUT_TEXT_TAGLINE, 0, width - (left - x) - 20, SE_COLOR_TEXT_SECONDARY);

	/* The edge below the card. */
	return top + ABOUT_HERO_HEIGHT;
}

/* Draws a titled card of rows of values; returns the edge below it. */
static int
about_card(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width,
	const char *title,
	const struct about_row *rows,
	int count)
{
	int height;
	int y;
	int index;

	/* The card, as tall as its rows. */
	height = se_card_height(count, 1);
	y = se_card_begin(app, canvas, x, top, width, height, title, NULL);

	/* Each row, a line between two. */
	for (index = 0; index < count; index++)
		y = se_row_value(app, canvas, x, y, width, rows[index].label, rows[index].value, index + 1 == count);

	/* The edge below the card. */
	return top + height;
}

/* Adds a row when its value is known (an empty value is not shown); returns the new count. */
static int
about_add(
	struct about_row *rows,
	int count,
	const char *label,
	const char *value)
{
	/* A value that could not be read is left out. */
	if (value == NULL || value[0] == '\0')
		return count;

	/* The row. */
	rows[count].label = label;
	rows[count].value = value;

	/* One row more. */
	return count + 1;
}

/*
 * Writes how long the machine has run, from the monotonic clock (which
 * starts with the machine), in days, hours and minutes.
 */
static void
about_uptime(
	uint64_t milliseconds,
	char *text,
	size_t size)
{
	unsigned long minutes;
	unsigned long hours;
	unsigned long days;

	/* The whole minutes, hours and days. */
	minutes = (unsigned long)(milliseconds / 60000U);
	hours = minutes / 60UL;
	days = hours / 24UL;
	minutes %= 60UL;
	hours %= 24UL;

	/* The largest units that matter. */
	if (days > 0UL) {
		(void)snprintf(text, size, "%lu d %lu h %lu min", days, hours, minutes);
	} else if (hours > 0UL) {
		(void)snprintf(text, size, "%lu h %lu min", hours, minutes);
	} else {
		(void)snprintf(text, size, "%lu min", minutes);
	}
}
