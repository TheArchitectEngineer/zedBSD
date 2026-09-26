/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What zdesktop-files draws over its window: the question asked before
 * an action that cannot be undone (spec §23: delete for good, empty the
 * trash), and the list of running operations opened from the toolbar's
 * progress ring (spec §33).
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* The question's card. */
#define OVERLAY_DIALOG_WIDTH	400
#define OVERLAY_DIALOG_HEIGHT	164
#define OVERLAY_BUTTON_WIDTH	104
#define OVERLAY_BUTTON_HEIGHT	32

/* A task that copies at least this many bytes counts bytes rather than items. */
#define OVERLAY_BYTES_SHOWN	(16ULL * 1000ULL * 1000ULL)

/* The tasks' list: its width and the height of a task's row. */
#define OVERLAY_TASKS_WIDTH	320
#define OVERLAY_TASK_ROW	58

static void overlay_button(struct fm_app *app, struct fm_canvas *canvas, int x, int y, const char *label, int index, int primary);
static void overlay_task_text(const struct fm_task *task, char *text, size_t size);

/*
 * Draws what is over the window now: the question, when one is asked.
 */
void
fm_overlay_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	struct fm_rect whole;
	char title[128];
	char detail[256];
	char items[32];
	const char *action;
	const char *name;
	int x;
	int y;

	/* No question, nothing over the window. */
	if (app->dialog == FM_DIALOG_NONE)
		return;

	/* The window dimmed; a click on it goes nowhere. */
	whole.x = 0;
	whole.y = 0;
	whole.width = app->width;
	whole.height = app->height;
	fm_canvas_fill(canvas, &whole, FM_RGBA(0x1b2233, 60));
	fm_ui_hit(app, &whole, FM_HIT_OVERLAY, 0);

	/* The question's words: what is deleted, and that it cannot be undone. */
	fm_dir_items_text((long)app->dialog_count, items, sizeof(items));
	if (app->dialog == FM_DIALOG_EMPTY_TRASH) {
		snprintf(title, sizeof(title), "Empty the Trash?");
		snprintf(detail, sizeof(detail), "The %s in the Trash will be deleted for good.", items);
	} else if (app->dialog_count == 1U) {
		name = strrchr(app->dialog_paths[0], '/');
		if (name == NULL)
			name = app->dialog_paths[0];
		else
			name++;
		snprintf(title, sizeof(title), "Delete \"%s\" for good?", name);
		snprintf(detail, sizeof(detail), "It will not go to the Trash and can't be brought back.");
	} else {
		snprintf(title, sizeof(title), "Delete %s for good?", items);
		snprintf(detail, sizeof(detail), "They will not go to the Trash and can't be brought back.");
	}

	/* The card in the middle of the window. */
	x = (app->width - OVERLAY_DIALOG_WIDTH) / 2;
	y = (app->height - OVERLAY_DIALOG_HEIGHT) / 2;
	fm_canvas_shadow(canvas, (float)x, (float)y + 8.0f, OVERLAY_DIALOG_WIDTH, OVERLAY_DIALOG_HEIGHT, 18.0f, 24.0f, FM_RGBA(0x1f3a66, 70));
	fm_canvas_round(canvas, (float)x, (float)y, OVERLAY_DIALOG_WIDTH, OVERLAY_DIALOG_HEIGHT, 18.0f, FM_COLOR_PANEL);

	/* The title and the detail. */
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 40, title, 16U, 1, OVERLAY_DIALOG_WIDTH - 48, FM_COLOR_TEXT);
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 70, detail, 13U, 0, OVERLAY_DIALOG_WIDTH - 48, FM_COLOR_TEXT_SECONDARY);

	/* Cancel, and the action in red. */
	action = "Delete";
	if (app->dialog == FM_DIALOG_EMPTY_TRASH)
		action = "Empty";
	overlay_button(app, canvas, x + OVERLAY_DIALOG_WIDTH - 24 - 2 * OVERLAY_BUTTON_WIDTH - 10, y + OVERLAY_DIALOG_HEIGHT - 24 - OVERLAY_BUTTON_HEIGHT, "Cancel", FM_BUTTON_CANCEL, 0);
	overlay_button(app, canvas, x + OVERLAY_DIALOG_WIDTH - 24 - OVERLAY_BUTTON_WIDTH, y + OVERLAY_DIALOG_HEIGHT - 24 - OVERLAY_BUTTON_HEIGHT, action, FM_BUTTON_CONFIRM, 1);
}

/*
 * Draws the list of running operations under the toolbar's progress
 * ring, its top right corner at (x, y): each with what it does, how far it
 * is, a bar and a cancel button.
 */
void
fm_tasks_draw(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int y)
{
	const struct fm_task *task;
	struct fm_rect cancel;
	char text[160];
	float fraction;
	int height;
	int left;
	int row;
	int index;

	/* The card, as tall as the tasks. */
	height = 16 + app->task_count * OVERLAY_TASK_ROW;
	left = x - OVERLAY_TASKS_WIDTH;
	fm_canvas_shadow(canvas, (float)left, (float)y + 6.0f, OVERLAY_TASKS_WIDTH, (float)height, 16.0f, 18.0f, FM_RGBA(0x1f3a66, 60));
	fm_canvas_round(canvas, (float)left, (float)y, OVERLAY_TASKS_WIDTH, (float)height, 16.0f, FM_COLOR_PANEL);

	/* Each task: its words, its bar and its cancel button. */
	for (index = 0; index < app->task_count; index++) {
		task = app->tasks[index];
		row = y + 8 + index * OVERLAY_TASK_ROW;
		overlay_task_text(task, text, sizeof(text));
		(void)fm_text_draw_fit(app->text, canvas, left + 16, row + 22, text, 13U, 0, OVERLAY_TASKS_WIDTH - 64, FM_COLOR_TEXT);

		/* The bar: the bytes and the items done over all of them. */
		fraction = 0.0f;
		if (task->bytes_total + task->files_total != 0U)
			fraction = (float)(task->bytes_done + task->files_done * 4096U) / (float)(task->bytes_total + task->files_total * 4096U);
		if (fraction > 1.0f)
			fraction = 1.0f;
		fm_canvas_round(canvas, (float)left + 16.0f, (float)row + 34.0f, OVERLAY_TASKS_WIDTH - 64.0f, 6.0f, 3.0f, FM_RGB(0xe6ebf2));
		fm_canvas_round(canvas, (float)left + 16.0f, (float)row + 34.0f, (OVERLAY_TASKS_WIDTH - 64.0f) * fraction, 6.0f, 3.0f, FM_COLOR_ACCENT);

		/* The cancel button at the row's right. */
		cancel.x = left + OVERLAY_TASKS_WIDTH - 40;
		cancel.y = row + 12;
		cancel.width = 26;
		cancel.height = 26;
		if (app->hover_kind == FM_HIT_BUTTON && app->hover_index == FM_BUTTON_TASK_CANCEL + index)
			fm_canvas_circle(canvas, (float)cancel.x + 13.0f, (float)cancel.y + 13.0f, 13.0f, FM_COLOR_HOVER);
		fm_icon_draw(canvas, FM_ICON_CLOSE, (float)cancel.x + 5.0f, (float)cancel.y + 5.0f, 16.0f, FM_COLOR_TEXT_SECONDARY);
		fm_ui_hit(app, &cancel, FM_HIT_BUTTON, FM_BUTTON_TASK_CANCEL + index);
	}
}

/*
 * Writes what a running task is doing: "Copying 120 of 450 items".
 */
void
fm_task_text(
	const struct fm_task *task,
	char *text,
	size_t size)
{
	/* The task's words. */
	overlay_task_text(task, text, size);
}

/* Draws a button of the question: a light one, or the primary one in red. */
static void
overlay_button(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int y,
	const char *label,
	int index,
	int primary)
{
	struct fm_rect rect;
	fm_color ground;
	fm_color ink;
	int width;

	/* The button's colors, lit under the pointer. */
	ground = FM_RGB(0xeef1f6);
	ink = FM_COLOR_TEXT;
	if (primary != 0) {
		ground = FM_RGB(0xe5484d);
		ink = FM_RGB(0xffffff);
	}

	/* Darker under the pointer. */
	if (app->hover_kind == FM_HIT_BUTTON && app->hover_index == index)
		ground = fm_color_mix(ground, FM_RGB(0x000000), 0.08f);

	/* The pill and its label, centred. */
	rect.x = x;
	rect.y = y;
	rect.width = OVERLAY_BUTTON_WIDTH;
	rect.height = OVERLAY_BUTTON_HEIGHT;
	fm_canvas_round(canvas, (float)x, (float)y, OVERLAY_BUTTON_WIDTH, OVERLAY_BUTTON_HEIGHT, OVERLAY_BUTTON_HEIGHT * 0.5f, ground);
	width = fm_text_width(app->text, label, strlen(label), 13U, 1);
	(void)fm_text_draw(app->text, canvas, x + (OVERLAY_BUTTON_WIDTH - width) / 2, fm_text_center(13U, y, OVERLAY_BUTTON_HEIGHT), label, strlen(label), 13U, 1, ink);

	/* It can be clicked. */
	fm_ui_hit(app, &rect, FM_HIT_BUTTON, index);
}

/* Writes a task's words: its verb, and how many of its items are done. */
static void
overlay_task_text(
	const struct fm_task *task,
	char *text,
	size_t size)
{
	const char *verb;
	char done[32];
	char total[32];

	/* The verb of each kind, as the status says it. */
	switch (task->kind) {
	case FM_TASK_COPY:
		verb = "Copying";
		break;
	case FM_TASK_MOVE:
		verb = "Moving";
		break;
	case FM_TASK_TRASH:
		verb = "Moving to the Trash";
		break;
	case FM_TASK_RESTORE:
		verb = "Putting back";
		break;
	case FM_TASK_DELETE:
		verb = "Deleting";
		break;
	case FM_TASK_DUPLICATE:
		verb = "Duplicating";
		break;
	default:
		verb = "Linking";
		break;
	}

	/* While planning, only the verb; then how many items are done of how many. */
	if (task->state == FM_TASK_PLANNING) {
		snprintf(text, size, "%s...", verb);
		return;
	}

	/* A large copy counts its bytes. */
	if (task->bytes_total >= OVERLAY_BYTES_SHOWN) {
		fm_dir_size_text(task->bytes_done, done, sizeof(done));
		fm_dir_size_text(task->bytes_total, total, sizeof(total));
		snprintf(text, size, "%s %s of %s", verb, done, total);
		return;
	}

	/* Otherwise its items. */
	fm_dir_items_text((long)task->files_total, total, sizeof(total));
	snprintf(text, size, "%s %llu of %s", verb, (unsigned long long)task->files_done, total);
}
