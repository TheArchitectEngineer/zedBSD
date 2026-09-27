/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file operations of files: background tasks (copy, move,
 * trash, restore, delete), the trash (freedesktop.org Trash specification),
 * the undo history and the clipboard of files.
 *
 * Nothing here draws or knows about windows; the host tests run it in
 * temporary folders.
 */

#ifndef ZDESKTOP_FILES_OPS_H
#define ZDESKTOP_FILES_OPS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* The longest path the operations handle, with the terminating NUL (FM_PATH_MAX of files.h). */
#define FM_OPS_PATH_MAX		1024

/* How many tasks run or wait at once, and how many steps of undo are kept. */
#define FM_TASKS		8
#define FM_UNDO_DEPTH		64

/* How many bytes of a file one step of a copy moves. */
#define FM_TASK_CHUNK		(256U * 1024U)

/*
 * The kinds of task.
 */
enum fm_task_kind {
	FM_TASK_COPY,
	FM_TASK_MOVE,
	FM_TASK_TRASH,
	FM_TASK_RESTORE,
	FM_TASK_DELETE,
	FM_TASK_DUPLICATE,
	FM_TASK_LINK
};

/*
 * Where a task is: planning its steps (walking the folders), carrying
 * them out, or finished (done, or cancelled part way).
 */
enum fm_task_state {
	FM_TASK_PLANNING,
	FM_TASK_RUNNING,
	FM_TASK_DONE,
	FM_TASK_CANCELLED
};

/*
 * The kinds of step a task is made of, carried out in order.
 */
enum fm_step_kind {
	FM_STEP_MKDIR,
	FM_STEP_COPY,
	FM_STEP_SYMLINK,
	FM_STEP_RENAME,
	FM_STEP_UNLINK,
	FM_STEP_RMDIR,
	FM_STEP_TRASHINFO,
	FM_STEP_UNTRASHINFO
};

/*
 * One step: what to do from where to where.  mode and times are what a
 * made folder or a copied file gets; size is what a copy moves; owner is
 * the index of the source the step belongs to.
 */
struct fm_step {
	unsigned kind;
	size_t owner;
	char *source;
	char *target;
	uint64_t size;
	mode_t mode;
	time_t modified;
	time_t accessed;
};

/*
 * A folder being walked while a task plans its steps: the folder's open
 * directory, its path and where its copy goes.
 */
struct fm_walk {
	void *directory;
	char *source;
	char *target;
	int post_order;
	size_t owner;
};

/*
 * One background task.
 *
 * The sources are copied in at the start; the steps are planned a slice at
 * a time (a deep folder is walked over several rounds of the main loop) and
 * then carried out a slice at a time, a large file in chunks.  results
 * pairs each source with where it went (for undo and for selecting the
 * outcome).
 */
struct fm_task {
	unsigned id;
	unsigned kind;
	unsigned state;

	/* What it works on, and the folder it works into. */
	char **sources;
	size_t source_count;
	char destination[FM_OPS_PATH_MAX];

	/* The steps, planned and carried out. */
	struct fm_step *steps;
	size_t step_count;
	size_t step_capacity;
	size_t step_index;

	/* The walk of the source being planned: the next source, the source whose steps are being planned, and the folders open. */
	size_t source_index;
	size_t owner;
	struct fm_walk *walks;
	size_t walk_count;
	size_t walk_capacity;

	/* The file being copied: its descriptors (-1 when none) and how far it is. */
	int copy_in;
	int copy_out;
	uint64_t copy_done;

	/* How far the task is. */
	uint64_t files_total;
	uint64_t files_done;
	uint64_t bytes_total;
	uint64_t bytes_done;

	/* Where each source went (NULL for one that failed), in the sources' order, and which sources failed. */
	char **results;
	unsigned char *failed;

	/* The first failure: its errno value and the path it was about. */
	int error;
	char error_path[FM_OPS_PATH_MAX];
	unsigned error_count;

	/* How its outcome is recorded: 0 as a new change, 1 not at all (an undo), 2 as a change redone. */
	int undoing;
};

/*
 * The kinds of change the undo history keeps.
 */
enum fm_undo_kind {
	FM_UNDO_MOVE,
	FM_UNDO_COPY,
	FM_UNDO_TRASH,
	FM_UNDO_RESTORE,
	FM_UNDO_RENAME,
	FM_UNDO_NEW_FOLDER,
	FM_UNDO_TAGS
};

/*
 * One change of the history: what it was and the paths it concerned, as
 * pairs (where from, where to).  For a rename, the old and the new path;
 * for tags, the path and the tags before and after in extra.
 */
struct fm_undo_item {
	unsigned kind;
	size_t count;
	char **from;
	char **to;
	unsigned *before;
	unsigned *after;
};

/*
 * The undo and redo histories of a window (the newest last).
 */
struct fm_undo {
	struct fm_undo_item undo[FM_UNDO_DEPTH];
	int undo_count;
	struct fm_undo_item redo[FM_UNDO_DEPTH];
	int redo_count;
};

/*
 * The clipboard's modes.
 */
enum fm_clip_mode {
	FM_CLIP_NONE,
	FM_CLIP_COPY,
	FM_CLIP_CUT
};

/* The tasks (task.c). */
struct fm_task *fm_task_new(unsigned kind, char *const *sources, size_t count, const char *destination);
int fm_task_step(struct fm_task *task, uint64_t budget_ms);
void fm_task_cancel(struct fm_task *task);
void fm_task_free(struct fm_task *task);
const char *fm_task_verb(unsigned kind);
int fm_unique_name(const char *folder, const char *name, const char *suffix, char *path, size_t size);
uint64_t fm_ops_clock(void);

/* The trash (trash.c). */
int fm_trash_path(char *path, size_t size);
int fm_trash_info_read(const char *trash, const char *name, char *original, size_t size, time_t *deleted);
int fm_trash_info_write(const char *path, const char *original, time_t deleted);
int fm_trash_name(const char *trash, const char *base, char *name, size_t size);

/* The undo history (undo.c). */
void fm_undo_push(struct fm_undo *history, unsigned kind, size_t count, char *const *from, char *const *to, const unsigned *before, const unsigned *after);
void fm_undo_push_item(struct fm_undo *history, struct fm_undo_item *item);
void fm_undo_push_redo(struct fm_undo *history, struct fm_undo_item *item);
int fm_undo_take(struct fm_undo *history, int redo, struct fm_undo_item *item);
void fm_undo_item_free(struct fm_undo_item *item);
void fm_undo_free(struct fm_undo *history);

/* The clipboard (clip.c). */
int fm_clip_set(unsigned mode, char *const *paths, size_t count);
int fm_clip_get(unsigned *mode, char ***paths, size_t *count);
void fm_clip_clear(void);
void fm_paths_free(char **paths, size_t count);

#endif
