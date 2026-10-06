/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Help menu's cards: a short guide to the file manager, the keyboard
 * shortcuts (spec §35), and About.  Each is drawn over the window like the
 * information card and closes with Esc, its close button or a click on
 * the dimmed window.
 */

#include "files.h"

#include <string.h>

/* The card's width, its padding, its header and the height of a line. */
#define HELP_WIDTH		600
#define HELP_PADDING		28
#define HELP_HEADER		72
#define HELP_LINE		21

/* The width of the shortcuts' key column. */
#define HELP_KEY_WIDTH		200

/* The text sizes. */
#define HELP_TEXT_TITLE		18U
#define HELP_TEXT_BODY		13U

/*
 * One line of a card: a key (for the shortcuts; NULL otherwise) and its
 * text.  A line with no text is a gap.
 */
struct help_line {
	const char *key;
	const char *text;
};

/* The guide. */
static const struct help_line help_guide[] = {
	{ NULL, "Files shows the folders of this computer and what they hold." },
	{ NULL, "" },
	{ NULL, "Today is a dashboard: your folders, and the files you opened last; Home is your home folder." },
	{ NULL, "The sidebar leads to your favorite folders and the trash." },
	{ NULL, "Double-click a folder to open it, a file to open it with its application." },
	{ NULL, "Space shows a file large (Quick Look); Get Info shows everything about it," },
	{ NULL, "its owner and permissions, its attributes and its checksum." },
	{ NULL, "" },
	{ NULL, "Deleted items go to the Trash, from which they can be put back." },
	{ NULL, "Almost everything can be undone with Undo in the Edit menu." },
	{ NULL, "Type in the search field to find items by name, kind (kind:image)" },
	{ NULL, "or extension (.png)." }
};

/* The keyboard shortcuts. */
static const struct help_line help_shortcuts[] = {
	{ "Ctrl+N", "New window" },
	{ "Ctrl+T  Ctrl+W", "New tab, close tab" },
	{ "Ctrl+Tab  Ctrl+Shift+Tab", "Next tab, previous tab" },
	{ "Ctrl+Shift+N", "New folder" },
	{ "Ctrl+L", "Go to a location" },
	{ "Ctrl+F", "Find" },
	{ "Ctrl+C  X  V", "Copy, cut, paste" },
	{ "Ctrl+Z  Ctrl+Shift+Z", "Undo, redo" },
	{ "Ctrl+D", "Duplicate" },
	{ "Ctrl+I", "Get Info" },
	{ "F2", "Rename" },
	{ "Delete  Shift+Delete", "Move to the Trash, delete for good" },
	{ "Space", "Quick Look" },
	{ "Backspace  Alt+Left", "Back" },
	{ "Alt+Right", "Forward" },
	{ "Ctrl+Up", "Enclosing folder" },
	{ "Ctrl+1  Ctrl+2", "Icons, list" },
	{ "Ctrl+H", "Show hidden files" },
	{ "Ctrl+Alt+P", "Show the preview" }
};

/* About. */
static const struct help_line help_about[] = {
	{ NULL, "The file manager of Kei." },
	{ NULL, "" },
	{ NULL, "Copyright (C) 2026 Awe Morris" },
	{ NULL, "Zlib license" }
};

static void help_lines(unsigned help, const struct help_line **lines, int *count, const char **title);

/*
 * Opens a Help card (FM_HELP_*), in place of any other card.
 */
void
fm_help_open(
	struct fm_app *app,
	unsigned help)
{
	/* The other cards give way. */
	fm_info_close(app);
	fm_look_close(app);

	/* The card. */
	app->help = help;
	app->dirty = 1;
	fm_log("HELP open card=%u", help);
}

/*
 * Closes the Help card.
 */
void
fm_help_close(
	struct fm_app *app)
{
	/* Already closed. */
	if (app->help == FM_HELP_NONE)
		return;

	/* The window shows again. */
	app->help = FM_HELP_NONE;
	app->dirty = 1;
	fm_log("HELP close");
}

/*
 * Draws the Help card over the window when one is open.
 */
void
fm_help_draw(
	struct fm_app *app,
	struct kl_canvas *canvas)
{
	const struct help_line *lines;
	const char *title;
	struct kl_rect whole;
	struct kl_rect card;
	struct kl_rect close;
	int count;
	int index;
	int baseline;
	int text_x;

	/* Closed, nothing is drawn. */
	if (app->help == FM_HELP_NONE)
		return;

	/* The window dimmed; a click on it closes the card. */
	whole.x = 0;
	whole.y = 0;
	whole.width = app->width;
	whole.height = app->height;
	kl_canvas_fill(canvas, &whole, KL_RGBA(0x1b2233, 90));
	fm_ui_hit(app, &whole, FM_HIT_OVERLAY, FM_OVERLAY_HELP_GROUND);

	/* The card's lines, and its size. */
	help_lines(app->help, &lines, &count, &title);
	card.width = HELP_WIDTH;
	if (card.width > app->width - 40)
		card.width = app->width - 40;
	card.height = HELP_HEADER + count * HELP_LINE + HELP_PADDING;
	card.x = (app->width - card.width) / 2;
	card.y = (app->height - card.height) / 2;
	if (card.y < 10)
		card.y = 10;

	/* The card; a click on it stays there. */
	kl_canvas_shadow(canvas, (float)card.x, (float)card.y + 10.0f, (float)card.width, (float)card.height, 18.0f, 30.0f, KL_RGBA(0x0f1a33, 90));
	kl_canvas_round(canvas, (float)card.x, (float)card.y, (float)card.width, (float)card.height, 18.0f, FM_COLOR_PANEL);
	fm_ui_hit(app, &card, FM_HIT_OVERLAY, FM_OVERLAY_CARD);

	/* The title, and the close button at the right. */
	(void)kl_text_draw_fit(app->text, canvas, card.x + HELP_PADDING, card.y + 44, title, HELP_TEXT_TITLE, 1, card.width - 2 * HELP_PADDING - 40, FM_COLOR_TEXT);
	close.x = card.x + card.width - 48;
	close.y = card.y + 20;
	close.width = 28;
	close.height = 28;
	kl_canvas_circle(canvas, (float)close.x + 14.0f, (float)close.y + 14.0f, 14.0f, FM_COLOR_BUTTON);
	if (app->hover_kind == FM_HIT_BUTTON && app->hover_index == FM_BUTTON_HELP_CLOSE)
		kl_canvas_circle(canvas, (float)close.x + 14.0f, (float)close.y + 14.0f, 14.0f, FM_COLOR_HOVER);
	kl_icon_draw(canvas, KL_ICON_CLOSE, (float)close.x + 6.0f, (float)close.y + 6.0f, 16.0f, FM_COLOR_TEXT_SECONDARY);
	fm_ui_hit(app, &close, FM_HIT_BUTTON, FM_BUTTON_HELP_CLOSE);

	/* Each line: a key in its column when it has one, then its text. */
	for (index = 0; index < count; index++) {
		baseline = card.y + HELP_HEADER + index * HELP_LINE + 14;
		text_x = card.x + HELP_PADDING;
		if (lines[index].key != NULL) {
			(void)kl_text_draw(app->text, canvas, text_x, baseline, lines[index].key, strlen(lines[index].key), HELP_TEXT_BODY, 1, FM_COLOR_TEXT);
			text_x += HELP_KEY_WIDTH;
		}

		/* The text, cut at the card's edge. */
		(void)kl_text_draw_fit(app->text, canvas, text_x, baseline, lines[index].text, HELP_TEXT_BODY, 0, card.x + card.width - HELP_PADDING - text_x, FM_COLOR_TEXT_SECONDARY);
	}
}

/* Chooses a card's lines and title. */
static void
help_lines(
	unsigned help,
	const struct help_line **lines,
	int *count,
	const char **title)
{
	/* Each card's own. */
	switch (help) {
	case FM_HELP_SHORTCUTS:
		*lines = help_shortcuts;
		*count = (int)(sizeof(help_shortcuts) / sizeof(help_shortcuts[0]));
		*title = "Keyboard Shortcuts";
		break;
	case FM_HELP_ABOUT:
		*lines = help_about;
		*count = (int)(sizeof(help_about) / sizeof(help_about[0]));
		*title = "About Files";
		break;
	default:
		*lines = help_guide;
		*count = (int)(sizeof(help_guide) / sizeof(help_guide[0]));
		*title = "File Manager Help";
		break;
	}
}
