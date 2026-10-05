/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What files draws over its window: the question asked before
 * an action that cannot be undone (spec §23: delete for good, empty the
 * trash) or before a removable medium is mounted (ws132-p009), and the list of running operations opened from the titlebar's
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

/* The question about a taken name's card, wider for a folder's four answers (ws035-p115), and the "apply to all" box. */
#define OVERLAY_COLLISION_WIDTH		460
#define OVERLAY_MERGE_WIDTH		520
#define OVERLAY_COLLISION_HEIGHT	200
#define OVERLAY_CHECK_SIZE		16

/* A task that copies at least this many bytes counts bytes rather than items. */
#define OVERLAY_BYTES_SHOWN	(16ULL * 1000ULL * 1000ULL)

/* The tasks' list: its width and the height of a task's row. */
#define OVERLAY_TASKS_WIDTH	320
#define OVERLAY_TASK_ROW	58

static void overlay_button(struct fm_app *app, struct fm_canvas *canvas, int x, int y, const char *label, int index, int primary);
static void overlay_collision(struct fm_app *app, struct fm_canvas *canvas);
static void overlay_mount(struct fm_app *app, struct fm_canvas *canvas);
static void overlay_check(struct fm_app *app, struct fm_canvas *canvas, int x, int y, const char *label, int checked);
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

	/* A taken name has a question of its own. */
	if (app->dialog == FM_DIALOG_COLLISION) {
		overlay_collision(app, canvas);
		return;
	}

	/* So has a medium to be mounted. */
	if (app->dialog == FM_DIALOG_MOUNT) {
		overlay_mount(app, canvas);
		return;
	}

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
 * Draws the list of running operations (opened by the titlebar's progress
 * ring) with its top right corner at (x, y): each with what it does, how
 * far it is, a bar and a cancel button.
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

	/* The button's colors (1: red, the action that loses something; 2: blue, the default), lit under the pointer. */
	ground = FM_RGB(0xeef1f6);
	ink = FM_COLOR_TEXT;
	if (primary == 1) {
		ground = FM_RGB(0xe5484d);
		ink = FM_RGB(0xffffff);
	} else if (primary == 2) {
		ground = FM_COLOR_ACCENT;
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

/*
 * Draws the question about a taken name (F-041): which name, where, and
 * the answers Skip, Keep Both (the default, Enter) and Replace (red), with
 * "apply to all" when more names of the operation are taken.  When a
 * folder meets a folder, Merge is offered too and is the default
 * (ws035-p115, F-050).
 */
static void
overlay_collision(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_task *task;
	const char *name;
	const char *folder;
	const char *verb;
	char title[FM_PATH_MAX + 32];
	char where[FM_PATH_MAX + 64];
	char question[128];
	char all[64];
	size_t left;
	int merge;
	int width;
	int x;
	int y;
	int buttons_y;
	int right;

	/* The name asked about. */
	task = app->collision_task;
	if (task == NULL)
		return;
	name = strrchr(task->sources[app->collision_index], '/');
	if (name == NULL)
		name = task->sources[app->collision_index];
	else
		name++;

	/* The folder it goes into (a merged folder's, for its contents), by its own name. */
	folder = strrchr(fm_task_folder(task, app->collision_index), '/');
	if (folder == NULL || folder[1] == '\0')
		folder = fm_task_folder(task, app->collision_index);
	else
		folder++;

	/* The words: the name, the folder, and what the operation does; two folders can be merged. */
	verb = "copying";
	if (task->kind == FM_TASK_MOVE)
		verb = "moving";
	merge = fm_task_can_merge(task, app->collision_index);
	if (merge != 0) {
		snprintf(title, sizeof(title), "A folder named \"%s\" already exists", name);
		snprintf(where, sizeof(where), "There is already a folder with this name in \"%s\".", folder);
		snprintf(question, sizeof(question), "Merge the folders, or replace it with the one you're %s?", verb);
	} else {
		snprintf(title, sizeof(title), "\"%s\" already exists", name);
		snprintf(where, sizeof(where), "An item with this name is already in \"%s\".", folder);
		snprintf(question, sizeof(question), "Replace it with the one you're %s, or keep both?", verb);
	}

	/* The card in the middle of the window, wider when there are four answers. */
	width = OVERLAY_COLLISION_WIDTH;
	if (merge != 0)
		width = OVERLAY_MERGE_WIDTH;
	x = (app->width - width) / 2;
	y = (app->height - OVERLAY_COLLISION_HEIGHT) / 2;
	fm_canvas_shadow(canvas, (float)x, (float)y + 8.0f, (float)width, OVERLAY_COLLISION_HEIGHT, 18.0f, 24.0f, FM_RGBA(0x1f3a66, 70));
	fm_canvas_round(canvas, (float)x, (float)y, (float)width, OVERLAY_COLLISION_HEIGHT, 18.0f, FM_COLOR_PANEL);

	/* The title and the two lines under it. */
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 40, title, 16U, 1, width - 48, FM_COLOR_TEXT);
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 68, where, 13U, 0, width - 48, FM_COLOR_TEXT_SECONDARY);
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 88, question, 13U, 0, width - 48, FM_COLOR_TEXT_SECONDARY);

	/* "Apply to all" when more names than this one are taken. */
	left = fm_action_collision_left(app);
	if (left > 1U) {
		snprintf(all, sizeof(all), "Apply to all %lu conflicts", (unsigned long)left);
		overlay_check(app, canvas, x + 24, y + 110, all, app->collision_all);
	}

	/*
	 * From the right: for two folders Merge (the default), Replace (red),
	 * Keep Both and Skip; otherwise Replace, Keep Both (the default) and
	 * Skip.
	 */
	buttons_y = y + OVERLAY_COLLISION_HEIGHT - 24 - OVERLAY_BUTTON_HEIGHT;
	right = x + width - 24;
	if (merge != 0) {
		overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Merge", FM_BUTTON_MERGE, 2);
		right -= OVERLAY_BUTTON_WIDTH + 10;
		overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Replace", FM_BUTTON_REPLACE, 1);
		right -= OVERLAY_BUTTON_WIDTH + 10;
		overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Keep Both", FM_BUTTON_KEEP_BOTH, 0);
	} else {
		overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Replace", FM_BUTTON_REPLACE, 1);
		right -= OVERLAY_BUTTON_WIDTH + 10;
		overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Keep Both", FM_BUTTON_KEEP_BOTH, 2);
	}

	/* Skip, leftmost of the answers. */
	right -= OVERLAY_BUTTON_WIDTH + 10;
	overlay_button(app, canvas, right - OVERLAY_BUTTON_WIDTH, buttons_y, "Skip", FM_BUTTON_SKIP, 0);
}

/* Draws a check box with its label at (x, y), clickable as FM_BUTTON_APPLY_ALL. */
static void
overlay_check(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int y,
	const char *label,
	int checked)
{
	struct fm_rect rect;
	fm_color edge;
	int width;

	/* The outline, darker under the pointer. */
	edge = FM_RGB(0xb8c2d2);
	if (app->hover_kind == FM_HIT_BUTTON && app->hover_index == FM_BUTTON_APPLY_ALL)
		edge = FM_RGB(0x8a97ab);

	/* The box: blue and ticked when on, an outline when off. */
	if (checked != 0) {
		fm_canvas_round(canvas, (float)x, (float)y, OVERLAY_CHECK_SIZE, OVERLAY_CHECK_SIZE, 4.0f, FM_COLOR_ACCENT);
		fm_canvas_line(canvas, (float)x + 4.0f, (float)y + 8.5f, (float)x + 7.0f, (float)y + 11.5f, 2.0f, FM_RGB(0xffffff));
		fm_canvas_line(canvas, (float)x + 7.0f, (float)y + 11.5f, (float)x + 12.5f, (float)y + 5.0f, 2.0f, FM_RGB(0xffffff));
	} else {
		fm_canvas_round(canvas, (float)x, (float)y, OVERLAY_CHECK_SIZE, OVERLAY_CHECK_SIZE, 4.0f, FM_COLOR_PANEL);
		fm_canvas_round_border(canvas, (float)x, (float)y, OVERLAY_CHECK_SIZE, OVERLAY_CHECK_SIZE, 4.0f, 1.5f, edge);
	}

	/* The label beside it. */
	(void)fm_text_draw(app->text, canvas, x + OVERLAY_CHECK_SIZE + 8, fm_text_center(13U, y, OVERLAY_CHECK_SIZE), label, strlen(label), 13U, 0, FM_COLOR_TEXT);

	/* The box and the label can be clicked. */
	width = fm_text_width(app->text, label, strlen(label), 13U, 0);
	rect.x = x;
	rect.y = y - 4;
	rect.width = OVERLAY_CHECK_SIZE + 8 + width;
	rect.height = OVERLAY_CHECK_SIZE + 8;
	fm_ui_hit(app, &rect, FM_HIT_BUTTON, FM_BUTTON_APPLY_ALL);
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

/*
 * Draws the question whether to mount a removable medium: its name, size
 * and file system, that its programs cannot be run, Cancel and Mount.
 */
static void
overlay_mount(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_device *device;
	char title[128];
	char detail[192];
	char size[32];
	const char *system;
	int same;
	int x;
	int y;

	/* The medium asked about (the question stays empty if it went; Cancel or Esc closes it). */
	device = fm_devices_find(app, app->device_confirm);
	title[0] = '\0';
	detail[0] = '\0';
	if (device != NULL) {
		snprintf(title, sizeof(title), "Mount \"%s\"?", device->name);

		/* Its size and file system, as far as the desktop told them. */
		size[0] = '\0';
		if (device->bytes != 0U)
			fm_dir_size_text(device->bytes, size, sizeof(size));
		system = "";
		same = strcmp(device->fs, "fat");
		if (same == 0)
			system = "FAT";
		same = strcmp(device->fs, "ufs");
		if (same == 0)
			system = "UFS";
		if (size[0] != '\0' && system[0] != '\0') {
			snprintf(detail, sizeof(detail), "%s, %s. Programs on it can't be run.", size, system);
		} else if (size[0] != '\0') {
			snprintf(detail, sizeof(detail), "%s. Programs on it can't be run.", size);
		} else {
			snprintf(detail, sizeof(detail), "Programs on it can't be run.");
		}
	}

	/* The card in the middle of the window. */
	x = (app->width - OVERLAY_DIALOG_WIDTH) / 2;
	y = (app->height - OVERLAY_DIALOG_HEIGHT) / 2;
	fm_canvas_shadow(canvas, (float)x, (float)y + 8.0f, OVERLAY_DIALOG_WIDTH, OVERLAY_DIALOG_HEIGHT, 18.0f, 24.0f, FM_RGBA(0x1f3a66, 70));
	fm_canvas_round(canvas, (float)x, (float)y, OVERLAY_DIALOG_WIDTH, OVERLAY_DIALOG_HEIGHT, 18.0f, FM_COLOR_PANEL);

	/* The title and the detail. */
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 40, title, 16U, 1, OVERLAY_DIALOG_WIDTH - 48, FM_COLOR_TEXT);
	(void)fm_text_draw_fit(app->text, canvas, x + 24, y + 70, detail, 13U, 0, OVERLAY_DIALOG_WIDTH - 48, FM_COLOR_TEXT_SECONDARY);

	/* Cancel, and Mount in blue, the default (not red: mounting loses nothing). */
	overlay_button(app, canvas, x + OVERLAY_DIALOG_WIDTH - 24 - 2 * OVERLAY_BUTTON_WIDTH - 10, y + OVERLAY_DIALOG_HEIGHT - 24 - OVERLAY_BUTTON_HEIGHT, "Cancel", FM_BUTTON_CANCEL, 0);
	overlay_button(app, canvas, x + OVERLAY_DIALOG_WIDTH - 24 - OVERLAY_BUTTON_WIDTH, y + OVERLAY_DIALOG_HEIGHT - 24 - OVERLAY_BUTTON_HEIGHT, "Mount", FM_BUTTON_CONFIRM, 2);
}
