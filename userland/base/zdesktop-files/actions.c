/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file operations of zdesktop-files as the user asks for them: cut,
 * copy and paste, duplicate, move to the trash, delete, put back, empty the
 * trash, new folder, rename, undo and redo (spec §15, §22〜§24, §31, §33).
 *
 * The keyboard, the menus and the buttons all call these.  The ones that
 * take time start a task (task.c), which the main loop moves on a slice at
 * a time (fm_actions_tick); when it finishes, its outcome is recorded for
 * undo, the folder is read again and what the task made is selected.
 */

#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* How long the tasks may run in one round of the main loop, in milliseconds. */
#define ACTIONS_BUDGET_MS	12U

/* How often a running task's progress is drawn, in milliseconds. */
#define ACTIONS_PROGRESS_MS	150U

/* The name a new folder gets. */
#define ACTIONS_NEW_FOLDER	"untitled folder"

static int actions_start(struct fm_app *app, unsigned kind, char *const *sources, size_t count, const char *destination, int undoing);
static void actions_finish(struct fm_app *app, struct fm_task *task);
static void actions_record(struct fm_app *app, struct fm_task *task);
static void actions_select_after(struct fm_app *app, char *const *paths, size_t count);
static void actions_ask(struct fm_app *app, unsigned dialog, char **paths, size_t count);
static void actions_undo_item(struct fm_app *app, struct fm_undo_item *item, int redo);
static int actions_undo_possible(const struct fm_undo_item *item, int redo);
static void actions_parent(const char *path, char *parent, size_t size);
static void actions_error(struct fm_app *app, const char *what, int error, const char *path);

/*
 * Puts the selected items on the clipboard, to be copied (or, with cut,
 * moved) at the paste.
 */
void
fm_action_copy(
	struct fm_app *app,
	int cut)
{
	const char *verb;
	char message[64];
	char items[32];
	char **paths;
	unsigned mode;
	size_t count;
	int error;

	/* The selection's paths. */
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* On the clipboard, to be copied or moved. */
	mode = FM_CLIP_COPY;
	verb = "Copied";
	if (cut != 0) {
		mode = FM_CLIP_CUT;
		verb = "Cut";
	}

	/* The paths go on the clipboard. */
	error = fm_clip_set(mode, paths, count);
	if (error != 0) {
		actions_error(app, "Couldn't use the clipboard", error, "");
		fm_paths_free(paths, count);
		return;
	}

	/* Cut items are drawn faded until the paste. */
	fm_ui_reload(app, fm_ui_tab(app));

	/* Said in the status pill and the log. */
	fm_dir_items_text((long)count, items, sizeof(items));
	snprintf(message, sizeof(message), "%s %s", verb, items);
	fm_ui_message(app, message);
	fm_log("CLIPBOARD mode=%u items=%lu", mode, (unsigned long)count);
	fm_paths_free(paths, count);
}

/*
 * Pastes the clipboard into the folder shown: a copy, or a move for a cut
 * (the clipboard is then emptied).
 */
void
fm_action_paste(
	struct fm_app *app)
{
	const char *folder;
	char **paths;
	unsigned mode;
	size_t count;
	int error;

	/* Only into a folder. */
	folder = fm_current_folder(app);
	if (folder == NULL)
		return;

	/* The clipboard. */
	error = fm_clip_get(&mode, &paths, &count);
	if (error != 0 || mode == FM_CLIP_NONE) {
		fm_paths_free(paths, count);
		fm_ui_message(app, "Nothing to paste");
		return;
	}

	/* A cut moves (once), a copy copies. */
	if (mode == FM_CLIP_CUT) {
		error = actions_start(app, FM_TASK_MOVE, paths, count, folder, 0);
		if (error == 0)
			fm_clip_clear();
	} else {
		error = actions_start(app, FM_TASK_COPY, paths, count, folder, 0);
	}

	/* The clipboard's paths are not needed any more. */
	fm_paths_free(paths, count);
}

/*
 * Duplicates the selected items beside themselves ("name copy").
 */
void
fm_action_duplicate(
	struct fm_app *app)
{
	const char *folder;
	char **paths;
	size_t count;
	int error;

	/* The selection, in the folder shown. */
	folder = fm_current_folder(app);
	error = fm_selected_paths(app, &paths, &count);
	if (folder == NULL || error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* A duplicate task into the same folder. */
	(void)actions_start(app, FM_TASK_DUPLICATE, paths, count, folder, 0);
	fm_paths_free(paths, count);
}

/*
 * Moves the selected items to the trash (in the trash itself, Delete asks
 * to delete them for good instead).
 */
void
fm_action_trash(
	struct fm_app *app)
{
	struct fm_tab *tab;
	char trash[FM_PATH_MAX];
	char **paths;
	size_t count;
	int error;

	/* In the trash, removing is for good and is asked about. */
	tab = fm_ui_tab(app);
	if (tab->history[tab->history_index].location.kind == FM_LOCATION_TRASH) {
		fm_action_delete(app);
		return;
	}

	/* The selection. */
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* The trash. */
	error = fm_trash_path(trash, sizeof(trash));
	if (error != 0) {
		actions_error(app, "There is no trash", error, "");
		fm_paths_free(paths, count);
		return;
	}

	/* A trash task. */
	(void)actions_start(app, FM_TASK_TRASH, paths, count, trash, 0);
	fm_paths_free(paths, count);
}

/*
 * Asks whether to delete the selected items for good (Shift+Delete).
 */
void
fm_action_delete(
	struct fm_app *app)
{
	char **paths;
	size_t count;
	int error;

	/* The selection, handed to the question. */
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* The question, which takes the paths over. */
	actions_ask(app, FM_DIALOG_DELETE, paths, count);
}

/*
 * Asks whether to empty the trash.
 */
void
fm_action_empty_trash(
	struct fm_app *app)
{
	struct fm_listing listing;
	char trash[FM_PATH_MAX];
	char **paths;
	size_t index;
	int error;

	/* The trash's items. */
	error = fm_trash_path(trash, sizeof(trash));
	if (error != 0)
		return;
	memset(&listing, 0, sizeof(listing));
	(void)fm_dir_read_trash(&listing, trash);
	if (listing.count == 0) {
		fm_dir_free(&listing);
		fm_ui_message(app, "The trash is empty");
		return;
	}

	/* Their paths, handed to the question. */
	paths = calloc(listing.count, sizeof(char *));
	if (paths == NULL) {
		fm_dir_free(&listing);
		return;
	}

	/* The paths are taken from the listing. */
	for (index = 0; index < listing.count; index++) {
		paths[index] = listing.entries[index].path;
		listing.entries[index].path = NULL;
	}

	/* The question, which takes them over. */
	actions_ask(app, FM_DIALOG_EMPTY_TRASH, paths, listing.count);
	fm_dir_free(&listing);
}

/*
 * Answers the question asked: a confirmed delete or empty trash starts its
 * task; otherwise nothing happens.
 */
void
fm_action_confirm(
	struct fm_app *app,
	int confirmed)
{
	char trash[FM_PATH_MAX];
	char files[FM_PATH_MAX + 8];
	char path[2 * FM_PATH_MAX + 32];
	size_t index;
	unsigned dialog;
	int trash_error;
	int error;
	int match;

	/* The question is over. */
	dialog = app->dialog;
	app->dialog = FM_DIALOG_NONE;
	app->dirty = 1;
	fm_log("DIALOG answer=%d", confirmed);

	/* Confirmed: a delete task over the paths. */
	if (confirmed != 0 && dialog != FM_DIALOG_NONE) {
		error = actions_start(app, FM_TASK_DELETE, app->dialog_paths, app->dialog_count, NULL, 0);

		/* Items deleted from the trash take their records with them (the records are small, removed at once). */
		trash_error = fm_trash_path(trash, sizeof(trash));
		snprintf(files, sizeof(files), "%s/files", trash);
		for (index = 0; error == 0 && trash_error == 0 && index < app->dialog_count; index++) {
			actions_parent(app->dialog_paths[index], path, sizeof(path));
			match = strcmp(path, files);
			if (match != 0)
				continue;
			snprintf(path, sizeof(path), "%s/info/%s.trashinfo", trash, app->dialog_paths[index] + strlen(files) + 1U);
			(void)unlink(path);
		}
	}

	/* The paths asked about are let go. */
	fm_paths_free(app->dialog_paths, app->dialog_count);
	app->dialog_paths = NULL;
	app->dialog_count = 0;
}

/*
 * Puts the selected items of the trash back where they were.
 */
void
fm_action_put_back(
	struct fm_app *app)
{
	struct fm_tab *tab;
	char trash[FM_PATH_MAX];
	char **paths;
	size_t count;
	int error;

	/* Only in the trash. */
	tab = fm_ui_tab(app);
	if (tab->history[tab->history_index].location.kind != FM_LOCATION_TRASH)
		return;

	/* The selection and the trash. */
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* The trash they are in. */
	error = fm_trash_path(trash, sizeof(trash));
	if (error != 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* A put-back task. */
	(void)actions_start(app, FM_TASK_RESTORE, paths, count, trash, 0);
	fm_paths_free(paths, count);
}

/*
 * Makes a new folder in the folder shown ("untitled folder", numbered
 * when taken), selects it and starts changing its name.
 */
void
fm_action_new_folder(
	struct fm_app *app)
{
	const char *folder;
	char path[FM_PATH_MAX];
	char *paths[1];
	int error;
	int status;

	/* Only in a folder. */
	folder = fm_current_folder(app);
	if (folder == NULL)
		return;

	/* A free name, and the folder. */
	error = fm_unique_name(folder, ACTIONS_NEW_FOLDER, NULL, path, sizeof(path));
	if (error != 0) {
		actions_error(app, "Couldn't make a folder", error, folder);
		return;
	}

	/* The folder itself. */
	status = mkdir(path, 0755);
	if (status != 0) {
		actions_error(app, "Couldn't make a folder", errno, path);
		return;
	}

	/* Recorded for undo; the folder read again with the new one selected, and its name being changed. */
	paths[0] = path;
	fm_undo_push(&app->undo, FM_UNDO_NEW_FOLDER, 1, paths, paths, NULL, NULL);
	fm_log("NEWFOLDER path=%s", path);
	actions_select_after(app, paths, 1);
	fm_ui_reload(app, fm_ui_tab(app));
	fm_action_rename_begin(app);
}

/*
 * Starts changing the name of the item with the cursor (F2): the name is
 * edited in place, its stem selected (not the extension).
 */
void
fm_action_rename_begin(
	struct fm_app *app)
{
	struct fm_tab *tab;
	struct fm_entry *entry;
	const char *dot;
	int index;

	/* The item: the cursor's, or else the first selected. */
	tab = fm_ui_tab(app);
	index = tab->cursor;
	if (index < 0 || (size_t)index >= tab->listing.count || tab->listing.entries[index].selected == 0)
		index = fm_select_first(tab);
	if (index < 0)
		return;
	entry = &tab->listing.entries[index];

	/* The field over its name, the stem selected. */
	fm_field_set(&app->rename, entry->name);
	dot = strrchr(entry->name, '.');
	if (dot != NULL && dot != entry->name && entry->folder == 0)
		fm_field_select(&app->rename, 0, (size_t)(dot - entry->name));
	snprintf(app->rename_path, sizeof(app->rename_path), "%s", entry->path);
	fm_select_only(tab, index);
	app->focus = FM_FOCUS_RENAME;
	app->dirty = 1;
}

/*
 * Ends changing a name: with commit, the item is renamed to what was typed
 * (a name that is empty, has a slash or is taken is refused and said so).
 */
void
fm_action_rename_end(
	struct fm_app *app,
	int commit)
{
	struct stat status;
	char parent[FM_PATH_MAX];
	char target[2 * FM_PATH_MAX + 2];
	char message[FM_PATH_MAX + 64];
	char *from[1];
	char *to[1];
	const char *old_name;
	const char *slash;
	int error;
	int taken;
	int same;
	int dot;
	int dots;
	int root;

	/* The editing is over either way. */
	app->focus = FM_FOCUS_CONTENT;
	app->dirty = 1;
	if (commit == 0)
		return;

	/* The same name changes nothing. */
	old_name = strrchr(app->rename_path, '/');
	if (old_name == NULL)
		return;
	old_name++;
	same = strcmp(old_name, app->rename.text);
	if (same == 0)
		return;

	/* A name must be something, without a slash, and not . or .. */
	slash = strchr(app->rename.text, '/');
	dot = strcmp(app->rename.text, ".");
	dots = strcmp(app->rename.text, "..");
	if (app->rename.text[0] == '\0' ||
	    slash != NULL ||
	    dot == 0 ||
	    dots == 0) {
		fm_ui_message(app, "That name can't be used");
		return;
	}

	/* The new path, which must be free. */
	actions_parent(app->rename_path, parent, sizeof(parent));
	root = strcmp(parent, "/");
	if (root == 0)
		snprintf(target, sizeof(target), "/%s", app->rename.text);
	else
		snprintf(target, sizeof(target), "%s/%s", parent, app->rename.text);
	taken = lstat(target, &status);
	if (taken == 0) {
		snprintf(message, sizeof(message), "An item named \"%s\" already exists", app->rename.text);
		fm_ui_message(app, message);
		return;
	}

	/* The rename. */
	error = rename(app->rename_path, target);
	if (error != 0) {
		actions_error(app, "Couldn't rename", errno, app->rename_path);
		return;
	}

	/* Recorded for undo, and the folder read again with the item selected under its new name. */
	from[0] = app->rename_path;
	to[0] = target;
	fm_undo_push(&app->undo, FM_UNDO_RENAME, 1, from, to, NULL, NULL);
	fm_log("RENAME from=%s to=%s", app->rename_path, target);
	actions_select_after(app, to, 1);
	fm_ui_reload(app, fm_ui_tab(app));
}

/*
 * Undoes the newest change (or, with redo, does again the newest undone
 * one); a change the files no longer match is dropped and said so.
 */
void
fm_action_undo(
	struct fm_app *app,
	int redo)
{
	struct fm_undo_item item;
	int taken;
	int possible;

	/* The newest change of the history. */
	taken = fm_undo_take(&app->undo, redo, &item);
	if (taken == 0) {
		fm_ui_message(app, "Nothing to undo or redo");
		return;
	}

	/* The files must still be where the change left them. */
	possible = actions_undo_possible(&item, redo);
	if (possible == 0) {
		fm_ui_message(app, "Can't do that: the items were moved or deleted");
		fm_undo_item_free(&item);
		return;
	}

	/* Carried out, and kept on the other history. */
	actions_undo_item(app, &item, redo);
	if (redo != 0)
		fm_undo_push_item(&app->undo, &item);
	else
		fm_undo_push_redo(&app->undo, &item);
	fm_ui_reload(app, fm_ui_tab(app));
}

/*
 * Stops a running task (the tasks' list's cancel button).
 */
void
fm_action_cancel_task(
	struct fm_app *app,
	int index)
{
	/* Only a task that is there. */
	if (index < 0 || index >= app->task_count)
		return;

	/* It stops; the next tick finishes it. */
	fm_task_cancel(app->tasks[index]);
	fm_log("TASK cancel id=%u", app->tasks[index]->id);
	app->dirty = 1;
}

/*
 * Moves the running tasks on for a slice of time and finishes those that
 * are done; returns nonzero while tasks remain.
 */
int
fm_actions_tick(
	struct fm_app *app)
{
	struct fm_task *task;
	int more;
	int index;

	/* No task, nothing to do. */
	if (app->task_count == 0)
		return 0;

	/* The oldest task runs (tasks run one after another, as a queue). */
	task = app->tasks[0];
	more = fm_task_step(task, ACTIONS_BUDGET_MS);

	/* A finished one is finished off and leaves the queue. */
	if (more == 0) {
		actions_finish(app, task);
		fm_task_free(task);
		for (index = 1; index < app->task_count; index++)
			app->tasks[index - 1] = app->tasks[index];
		app->task_count--;
		app->dirty = 1;
	}

	/* The progress is drawn now and then. */
	if (app->now >= app->task_drawn_at + ACTIONS_PROGRESS_MS) {
		app->task_drawn_at = app->now;
		app->dirty = 1;
	}

	/* Reports whether tasks remain. */
	return app->task_count;
}

/*
 * Frees what the actions hold: the tasks (stopped), the histories, the
 * question's paths.
 */
void
fm_actions_release(
	struct fm_app *app)
{
	int index;

	/* The tasks. */
	for (index = 0; index < app->task_count; index++)
		fm_task_free(app->tasks[index]);
	app->task_count = 0;

	/* The histories and the paths kept. */
	fm_undo_free(&app->undo);
	fm_paths_free(app->dialog_paths, app->dialog_count);
	app->dialog_paths = NULL;
	app->dialog_count = 0;
	fm_paths_free(app->select_paths, app->select_count);
	app->select_paths = NULL;
	app->select_count = 0;
}

/*
 * Returns the folder the tab shows, or NULL for a place that is not one
 * folder.
 */
const char *
fm_current_folder(
	struct fm_app *app)
{
	struct fm_tab *tab;
	const struct fm_location *location;

	/* A folder (the home dashboard is not one: its items are cards). */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;
	if (location->kind == FM_LOCATION_FOLDER)
		return location->path;

	/* Another kind of place. */
	return NULL;
}

/*
 * Makes a table of the selected items' paths (allocated; free with
 * fm_paths_free).  Returns 0, or ENOMEM.
 */
int
fm_selected_paths(
	struct fm_app *app,
	char ***paths,
	size_t *count)
{
	struct fm_tab *tab;
	uint64_t bytes;
	size_t index;
	size_t selected;

	/* A table as large as the selection. */
	tab = fm_ui_tab(app);
	*count = 0;
	selected = fm_select_count(tab, &bytes);
	*paths = calloc(selected + 1U, sizeof(char *));
	if (*paths == NULL)
		return ENOMEM;

	/* Each selected entry's path. */
	for (index = 0; index < tab->listing.count; index++) {
		if (tab->listing.entries[index].selected == 0)
			continue;
		(*paths)[*count] = strdup(tab->listing.entries[index].path);
		if ((*paths)[*count] == NULL)
			return ENOMEM;
		(*count)++;
	}

	/* Succeeded: the table is complete. */
	return 0;
}

/* Starts a task over sources into a destination; nonzero when it cannot start. */
static int
actions_start(
	struct fm_app *app,
	unsigned kind,
	char *const *sources,
	size_t count,
	const char *destination,
	int undoing)
{
	struct fm_task *task;

	/* The queue has room for a few tasks. */
	if (app->task_count == FM_TASKS) {
		fm_ui_message(app, "Too many operations at once; wait for one to finish");
		return EBUSY;
	}

	/* The task, queued behind the others. */
	task = fm_task_new(kind, sources, count, destination);
	if (task == NULL) {
		fm_ui_message(app, "Out of memory");
		return ENOMEM;
	}

	/* How it is recorded when it ends. */
	task->undoing = undoing;

	/* Queued behind the others. */
	app->tasks[app->task_count] = task;
	app->task_count++;
	app->dirty = 1;

	/* The log line the tests wait for. */
	if (destination == NULL)
		destination = "-";
	fm_log("TASK start id=%u kind=%s items=%lu destination=%s", task->id, fm_task_verb(kind), (unsigned long)count, destination);

	/* Succeeded: the task runs in the main loop. */
	return 0;
}

/* Finishes a task: its outcome recorded for undo, said when it failed, and the folder read again with what it made selected. */
static void
actions_finish(
	struct fm_app *app,
	struct fm_task *task)
{
	const char *state;
	char message[FM_PATH_MAX + 96];

	/* The log line the tests wait for. */
	state = "done";
	if (task->state == FM_TASK_CANCELLED)
		state = "cancelled";
	fm_log("TASK done id=%u kind=%s state=%s files=%llu bytes=%llu errors=%u", task->id, fm_task_verb(task->kind), state, (unsigned long long)task->files_done, (unsigned long long)task->bytes_done, task->error_count);

	/* A failure is told (the first one). */
	if (task->error != 0) {
		snprintf(message, sizeof(message), "Couldn't %s %s: %s", fm_task_verb(task->kind), task->error_path, strerror(task->error));
		fm_ui_message(app, message);
	}

	/* The outcome for undo, unless this was an undo itself. */
	actions_record(app, task);

	/* What the task made or moved is selected once the folder is read again. */
	if (task->kind == FM_TASK_COPY || task->kind == FM_TASK_MOVE || task->kind == FM_TASK_DUPLICATE || task->kind == FM_TASK_LINK || task->kind == FM_TASK_RESTORE)
		actions_select_after(app, task->results, task->source_count);
	fm_ui_reload(app, fm_ui_tab(app));
}

/* Records a finished task's outcome in the undo history (only what succeeded). */
static void
actions_record(
	struct fm_app *app,
	struct fm_task *task)
{
	char **from;
	char **to;
	size_t count;
	size_t index;
	unsigned kind;

	/* An undo is not recorded again, nor a delete (it cannot be undone). */
	if (task->undoing == 1 || task->kind == FM_TASK_DELETE)
		return;

	/* The kind of change. */
	kind = FM_UNDO_COPY;
	if (task->kind == FM_TASK_MOVE)
		kind = FM_UNDO_MOVE;
	else if (task->kind == FM_TASK_TRASH)
		kind = FM_UNDO_TRASH;
	else if (task->kind == FM_TASK_RESTORE)
		kind = FM_UNDO_RESTORE;

	/* The pairs of the sources that succeeded. */
	from = calloc(task->source_count + 1U, sizeof(char *));
	to = calloc(task->source_count + 1U, sizeof(char *));
	count = 0;
	for (index = 0; from != NULL && to != NULL && index < task->source_count; index++) {
		if (task->failed[index] != 0 || task->results[index] == NULL)
			continue;
		from[count] = task->sources[index];
		to[count] = task->results[index];
		count++;
	}

	/* A change with something in it is recorded. */
	if (count != 0)
		fm_undo_push(&app->undo, kind, count, from, to, NULL, NULL);
	free(from);
	free(to);
}

/* Keeps paths to select once the folder is read again (NULL entries are left out). */
static void
actions_select_after(
	struct fm_app *app,
	char *const *paths,
	size_t count)
{
	size_t index;

	/* The paths kept before are dropped. */
	fm_paths_free(app->select_paths, app->select_count);
	app->select_paths = NULL;
	app->select_count = 0;

	/* A table for the new ones. */
	app->select_paths = calloc(count + 1U, sizeof(char *));
	if (app->select_paths == NULL)
		return;

	/* Each path that is there, copied. */
	for (index = 0; index < count; index++) {
		if (paths[index] == NULL)
			continue;
		app->select_paths[app->select_count] = strdup(paths[index]);
		if (app->select_paths[app->select_count] == NULL)
			break;
		app->select_count++;
	}
}

/* Asks a question about paths (the table is taken over). */
static void
actions_ask(
	struct fm_app *app,
	unsigned dialog,
	char **paths,
	size_t count)
{
	/* A question asked before is dropped. */
	fm_paths_free(app->dialog_paths, app->dialog_count);

	/* The question and its paths, shown over the window. */
	app->dialog = dialog;
	app->dialog_paths = paths;
	app->dialog_count = count;
	app->dirty = 1;
	fm_log("DIALOG ask=%u items=%lu", dialog, (unsigned long)count);
}

/* Carries out the undoing (or redoing) of a change. */
static void
actions_undo_item(
	struct fm_app *app,
	struct fm_undo_item *item,
	int redo)
{
	char trash[FM_PATH_MAX];
	char parent[FM_PATH_MAX];
	char *one[1];
	size_t index;
	int status;
	int error;

	/* The trash, which several kinds need. */
	error = fm_trash_path(trash, sizeof(trash));
	if (error != 0)
		trash[0] = '\0';
	fm_log("UNDO redo=%d kind=%u items=%lu", redo, item->kind, (unsigned long)item->count);

	/* Each kind of change, backward or forward. */
	switch (item->kind) {
	case FM_UNDO_MOVE:
	case FM_UNDO_RENAME:
		for (index = 0; index < item->count; index++) {
			if (redo != 0)
				status = rename(item->from[index], item->to[index]);
			else
				status = rename(item->to[index], item->from[index]);
			if (status == 0 || errno != EXDEV)
				continue;

			/* Across file systems a rename cannot do it: a move task. */
			if (redo != 0) {
				actions_parent(item->to[index], parent, sizeof(parent));
				one[0] = item->from[index];
			} else {
				actions_parent(item->from[index], parent, sizeof(parent));
				one[0] = item->to[index];
			}

			/* The task, not recorded again. */
			(void)actions_start(app, FM_TASK_MOVE, one, 1, parent, 1);
		}

		/* Every item was moved back (or forward). */
		break;
	case FM_UNDO_COPY:
		if (redo != 0) {
			actions_parent(item->to[0], parent, sizeof(parent));
			(void)actions_start(app, FM_TASK_COPY, item->from, item->count, parent, 1);
		} else {
			(void)actions_start(app, FM_TASK_TRASH, item->to, item->count, trash, 1);
		}

		/* The copies are made again, or go to the trash. */
		break;
	case FM_UNDO_TRASH:
		if (redo != 0)
			(void)actions_start(app, FM_TASK_TRASH, item->from, item->count, trash, 1);
		else
			(void)actions_start(app, FM_TASK_RESTORE, item->to, item->count, trash, 1);
		break;
	case FM_UNDO_RESTORE:
		if (redo != 0)
			(void)actions_start(app, FM_TASK_RESTORE, item->from, item->count, trash, 1);
		else
			(void)actions_start(app, FM_TASK_TRASH, item->to, item->count, trash, 1);
		break;
	case FM_UNDO_TAGS:
		for (index = 0; index < item->count; index++) {
			if (redo != 0)
				(void)fm_tags_write(&app->tags, item->to[index], item->after[index]);
			else
				(void)fm_tags_write(&app->tags, item->to[index], item->before[index]);
		}

		/* Every item has its tags back (or again). */
		break;
	case FM_UNDO_NEW_FOLDER:
		if (redo != 0) {
			(void)mkdir(item->to[0], 0755);
			break;
		}

		/* An empty new folder is removed; one with something in it goes to the trash. */
		status = rmdir(item->to[0]);
		if (status != 0)
			(void)actions_start(app, FM_TASK_TRASH, item->to, 1, trash, 1);
		break;
	default:
		break;
	}
}

/* Tells whether the files are still where a change left them (or, to redo, where the undo left them). */
static int
actions_undo_possible(
	const struct fm_undo_item *item,
	int redo)
{
	struct stat status;
	size_t index;
	int missing;

	/* A new folder is redone where nothing is. */
	if (item->kind == FM_UNDO_NEW_FOLDER && redo != 0) {
		missing = lstat(item->to[0], &status);
		if (missing == 0)
			return 0;
		return 1;
	}

	/* Every path the change starts from must be there. */
	for (index = 0; index < item->count; index++) {
		if (redo != 0)
			missing = lstat(item->from[index], &status);
		else
			missing = lstat(item->to[index], &status);
		if (missing != 0)
			return 0;
	}

	/* Succeeded: the change can be carried out. */
	return 1;
}

/* Writes the folder a path is in ("/" for the root's items). */
static void
actions_parent(
	const char *path,
	char *parent,
	size_t size)
{
	const char *slash;
	size_t length;

	/* Up to the last slash. */
	slash = strrchr(path, '/');
	if (slash == NULL || slash == path) {
		snprintf(parent, size, "/");
		return;
	}

	/* The path up to it. */
	length = (size_t)(slash - path);
	if (length >= size)
		length = size - 1U;
	memcpy(parent, path, length);
	parent[length] = '\0';
}

/* Tells a failure in the status pill: what failed, where, and why. */
static void
actions_error(
	struct fm_app *app,
	const char *what,
	int error,
	const char *path)
{
	char message[FM_PATH_MAX + 128];

	/* One line: what, the path when there is one, and the reason. */
	if (path[0] != '\0')
		snprintf(message, sizeof(message), "%s: %s: %s", what, path, strerror(error));
	else
		snprintf(message, sizeof(message), "%s: %s", what, strerror(error));
	fm_ui_message(app, message);
}

/*
 * Turns a tag on for the selected items, or off when they all have it
 * (Alt+1 to Alt+9); recorded for undo.
 */
void
fm_action_toggle_tag(
	struct fm_app *app,
	int tag)
{
	struct fm_tab *tab;
	unsigned *before;
	unsigned *after;
	char **paths;
	char message[96];
	size_t count;
	size_t index;
	int all;
	int error;

	/* A tag the window knows, and a selection. */
	if (tag < 0 || tag >= app->tags.count)
		return;
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* The tags before, and whether every item has this one already. */
	before = calloc(count, sizeof(unsigned));
	after = calloc(count, sizeof(unsigned));
	if (before == NULL || after == NULL) {
		free(before);
		free(after);
		fm_paths_free(paths, count);
		return;
	}

	/* Whether every item has the tag already. */
	all = 1;
	for (index = 0; index < count; index++) {
		before[index] = fm_tags_of(&app->tags, paths[index]);
		if ((before[index] & (1U << tag)) == 0U)
			all = 0;
	}

	/* Each item gets the tag, or loses it when all had it. */
	for (index = 0; index < count; index++) {
		after[index] = before[index] | (1U << tag);
		if (all != 0)
			after[index] = before[index] & ~(1U << tag);
		error = fm_tags_write(&app->tags, paths[index], after[index]);
		if (error != 0) {
			actions_error(app, "Couldn't tag", error, paths[index]);
			after[index] = before[index];
		}
	}

	/* Recorded for undo, said, and the items read again with their tags. */
	fm_undo_push(&app->undo, FM_UNDO_TAGS, count, paths, paths, before, after);
	if (all != 0)
		snprintf(message, sizeof(message), "Removed the tag %s", app->tags.items[tag].name);
	else
		snprintf(message, sizeof(message), "Tagged %s", app->tags.items[tag].name);
	fm_ui_message(app, message);
	fm_log("TAG tag=%s on=%d items=%lu", app->tags.items[tag].name, all == 0, (unsigned long)count);
	free(before);
	free(after);
	fm_paths_free(paths, count);
	tab = fm_ui_tab(app);
	fm_ui_reload(app, tab);
}

/*
 * Adds the folder shown to the sidebar's Favorites (Ctrl+Alt+T).
 */
void
fm_action_add_favorite(
	struct fm_app *app)
{
	struct fm_tab *tab;
	const struct fm_location *location;
	int error;

	/* Only a folder. */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;
	if (location->kind != FM_LOCATION_FOLDER)
		return;

	/* Added to the list, and the sidebar filled again. */
	error = fm_places_add_favorite(&app->places, location->path);
	if (error == EEXIST) {
		fm_ui_message(app, "It is already in the sidebar");
		return;
	}

	/* An error is told. */
	if (error != 0) {
		actions_error(app, "Couldn't change the sidebar", error, "");
		return;
	}

	/* The sidebar with it. */
	fm_places_init(&app->places, app->home, &app->tags);
	fm_log("FAVORITE add path=%s", location->path);
	app->dirty = 1;
}

/*
 * Takes a favorite folder off the sidebar (its row's small button).
 */
void
fm_action_remove_favorite(
	struct fm_app *app,
	int place)
{
	int error;

	/* Taken off the list, and the sidebar filled again. */
	error = fm_places_remove_favorite(&app->places, place);
	if (error != 0)
		return;
	fm_log("FAVORITE remove path=%s", app->places.items[place].location.path);
	fm_places_init(&app->places, app->home, &app->tags);
	app->dirty = 1;
}
