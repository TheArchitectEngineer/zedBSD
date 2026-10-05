/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Languages page (WS154 p002):
 *
 *   Input method      which input method the keyboard types through:
 *                     Japanese (Kei's), SKK, or none (English); one is
 *                     chosen at a time, and the desktop starts the input
 *                     method again with it at once (ime.method).
 *   Display language  the language of the desktop's words (WS158); English
 *                     alone until the translations come.
 */

#include "settings.h"

#include <stdio.h>

/* The three choices' switches (hit indices), in ime.method's order plus one. */
#define LANGUAGES_NONE		1
#define LANGUAGES_JAPANESE	2
#define LANGUAGES_SKK		3

/* The card's margin, the space between cards, a choice's height and the text sizes. */
#define LANGUAGES_PAD		18
#define LANGUAGES_GAP		16
#define LANGUAGES_ROW		60
#define LANGUAGES_TEXT_TITLE	15U
#define LANGUAGES_TEXT_SMALL	13U

/* One choice: its switch, the method it chooses, its name and what it does. */
struct languages_choice {
	int index;
	int method;
	const char *name;
	const char *line;
};

/* The choices, in the order shown. */
static const struct languages_choice languages_choices[] = {
	{ LANGUAGES_JAPANESE, 1, "Japanese", "Kei's input method: romaji to kana, converted a phrase at a time." },
	{ LANGUAGES_SKK, 2, "SKK", "Emacs's SKK: an upper-case letter starts a word, Space converts it." },
	{ LANGUAGES_NONE, 0, "None (English)", "The keys type as they are; no input method." },
};

/*
 * Draws the Languages page's cards from a top edge; returns the edge below
 * them.
 */
int
se_languages_draw(
	struct se_app *app,
	struct fm_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct fm_text_line line;
	const struct languages_choice *choice;
	int height;
	int y;
	size_t i;

	/* The input method's card: one row a choice, a switch at each. */
	height = se_card_height(0, 1) + 3 * LANGUAGES_ROW + 4;
	y = se_card_begin(app, canvas, x, top, width, height, "Input method", "What the keyboard types through. The change applies at once.");
	fm_text_metrics(app->text, LANGUAGES_TEXT_TITLE, &line);
	for (i = 0; i < sizeof(languages_choices) / sizeof(languages_choices[0]); i++) {
		/* The name, what it does, and its switch (on for the method chosen). */
		choice = &languages_choices[i];
		(void)fm_text_draw_fit(app->text, canvas, x + LANGUAGES_PAD + 2, y + 10 + line.ascent, choice->name, LANGUAGES_TEXT_TITLE, 1, width / 2, SE_COLOR_TEXT);
		(void)fm_text_draw_fit(app->text, canvas, x + LANGUAGES_PAD + 2, y + 32 + line.ascent, choice->line, LANGUAGES_TEXT_SMALL, 0, width - 120, SE_COLOR_TEXT_SECONDARY);
		se_toggle_draw(app, canvas, x + width - LANGUAGES_PAD - 44, y + 16, app->look.ime_method == choice->method, app->look.writable, choice->index);
		y += LANGUAGES_ROW;
	}

	/* The display language's card, English alone for now. */
	top += height + LANGUAGES_GAP;
	height = se_card_height(1, 1) + 24;
	y = se_card_begin(app, canvas, x, top, width, height, "Display language", NULL);
	y = se_row_value(app, canvas, x, y, width, "Language", "English", 1);
	(void)fm_text_draw_fit(app->text, canvas, x + LANGUAGES_PAD + 2, y + 14, "Other languages come with the translations.", LANGUAGES_TEXT_SMALL, 0, width - 2 * LANGUAGES_PAD, SE_COLOR_TEXT_SECONDARY);

	/* The edge below the cards. */
	return top + height;
}

/*
 * Carries out a click on a choice's switch: that method is chosen (a
 * click on the one chosen changes nothing).
 */
void
se_languages_press(
	struct se_app *app,
	int index)
{
	const struct languages_choice *choice;
	size_t i;

	/* The choice of the switch. */
	choice = NULL;
	for (i = 0; i < sizeof(languages_choices) / sizeof(languages_choices[0]); i++) {
		/* The switch clicked. */
		if (languages_choices[i].index == index)
			choice = &languages_choices[i];
	}

	/* Not a choice, a settings file that cannot be written, or the method chosen already. */
	if (choice == NULL || !app->look.writable || app->look.ime_method == choice->method)
		return;

	/* Chosen, and told to the desktop through the settings. */
	app->look.ime_method = choice->method;
	se_look_set_number(app, "ime.method", choice->method, 1);
	se_log("LANGUAGES ime method=%d", choice->method);

	/* The page shows it. */
	app->dirty = 1;
}
