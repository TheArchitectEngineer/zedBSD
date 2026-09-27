/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The undo history of files (spec §31): what the window changed
 * (a move, a copy, a move to the trash, a put back, a rename, a new folder,
 * tags), newest last, with the paths needed to change it back.
 *
 * A new change empties the redo history, as editors do.  What undoing
 * means for each kind is the actions' business (actions.c); here are only
 * the two stacks.
 */

#include "ops.h"

#include <stdlib.h>
#include <string.h>

static int undo_copy_paths(char ***copy, char *const *paths, size_t count);
static void undo_drop_oldest(struct fm_undo_item *stack, int *count);

/*
 * Records a change (its paths and tags are copied); the redo history is
 * emptied.  A change that cannot be recorded (no memory) is not undoable.
 */
void
fm_undo_push(
	struct fm_undo *history,
	unsigned kind,
	size_t count,
	char *const *from,
	char *const *to,
	const unsigned *before,
	const unsigned *after)
{
	struct fm_undo_item item;
	int error;
	int index;

	/* The change, with copies of what it concerned. */
	memset(&item, 0, sizeof(item));
	item.kind = kind;
	item.count = count;
	error = undo_copy_paths(&item.from, from, count);
	if (error == 0)
		error = undo_copy_paths(&item.to, to, count);
	if (error == 0 && before != NULL) {
		item.before = malloc(count * sizeof(unsigned) + 1U);
		item.after = malloc(count * sizeof(unsigned) + 1U);
		if (item.before == NULL || item.after == NULL)
			error = -1;
		else {
			memcpy(item.before, before, count * sizeof(unsigned));
			memcpy(item.after, after, count * sizeof(unsigned));
		}
	}

	/* A change that could not be copied is not recorded. */
	if (error != 0) {
		fm_undo_item_free(&item);
		return;
	}

	/* A new change leaves nothing to redo. */
	for (index = 0; index < history->redo_count; index++)
		fm_undo_item_free(&history->redo[index]);
	history->redo_count = 0;

	/* The oldest change goes when the history is full, and the new one goes last. */
	if (history->undo_count == FM_UNDO_DEPTH)
		undo_drop_oldest(history->undo, &history->undo_count);
	history->undo[history->undo_count] = item;
	history->undo_count++;
}

/*
 * Puts a change back on the undo history without touching the redo
 * history (a change redone); the item is taken over.
 */
void
fm_undo_push_item(
	struct fm_undo *history,
	struct fm_undo_item *item)
{
	/* The oldest goes when the history is full. */
	if (history->undo_count == FM_UNDO_DEPTH)
		undo_drop_oldest(history->undo, &history->undo_count);

	/* The change goes last; the caller no longer owns it. */
	history->undo[history->undo_count] = *item;
	history->undo_count++;
	memset(item, 0, sizeof(*item));
}

/*
 * Keeps a change that was undone, so that it can be done again (the item
 * is taken over).
 */
void
fm_undo_push_redo(
	struct fm_undo *history,
	struct fm_undo_item *item)
{
	/* The oldest goes when the history is full. */
	if (history->redo_count == FM_UNDO_DEPTH)
		undo_drop_oldest(history->redo, &history->redo_count);

	/* The change goes last; the caller no longer owns it. */
	history->redo[history->redo_count] = *item;
	history->redo_count++;
	memset(item, 0, sizeof(*item));
}

/*
 * Takes the newest change to undo (or, with redo set, to redo); the caller
 * owns it afterwards.  Returns zero when there is none.
 */
int
fm_undo_take(
	struct fm_undo *history,
	int redo,
	struct fm_undo_item *item)
{
	/* The redo history. */
	if (redo != 0) {
		if (history->redo_count == 0)
			return 0;
		history->redo_count--;
		*item = history->redo[history->redo_count];
		return 1;
	}

	/* The undo history. */
	if (history->undo_count == 0)
		return 0;
	history->undo_count--;
	*item = history->undo[history->undo_count];

	/* Succeeded: the newest change is the caller's. */
	return 1;
}

/*
 * Frees what a change holds.
 */
void
fm_undo_item_free(
	struct fm_undo_item *item)
{
	size_t index;

	/* Each path. */
	for (index = 0; index < item->count; index++) {
		if (item->from != NULL)
			free(item->from[index]);
		if (item->to != NULL)
			free(item->to[index]);
	}

	/* The tables. */
	free(item->from);
	free(item->to);
	free(item->before);
	free(item->after);
	memset(item, 0, sizeof(*item));
}

/*
 * Frees both histories.
 */
void
fm_undo_free(
	struct fm_undo *history)
{
	int index;

	/* Every change of both. */
	for (index = 0; index < history->undo_count; index++)
		fm_undo_item_free(&history->undo[index]);
	for (index = 0; index < history->redo_count; index++)
		fm_undo_item_free(&history->redo[index]);
	memset(history, 0, sizeof(*history));
}

/* Copies a table of paths (NULL entries stay NULL); nonzero when memory runs out. */
static int
undo_copy_paths(
	char ***copy,
	char *const *paths,
	size_t count)
{
	size_t index;

	/* The table. */
	*copy = calloc(count + 1U, sizeof(char *));
	if (*copy == NULL)
		return -1;

	/* Each path. */
	for (index = 0; index < count; index++) {
		if (paths[index] == NULL)
			continue;
		(*copy)[index] = strdup(paths[index]);
		if ((*copy)[index] == NULL)
			return -1;
	}

	/* Succeeded: the copy is complete. */
	return 0;
}

/* Drops the oldest change of a full stack. */
static void
undo_drop_oldest(
	struct fm_undo_item *stack,
	int *count)
{
	/* The oldest is freed and the rest move down. */
	fm_undo_item_free(&stack[0]);
	memmove(&stack[0], &stack[1], sizeof(stack[0]) * (size_t)(*count - 1));
	(*count)--;
}
