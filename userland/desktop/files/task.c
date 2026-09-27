/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The background tasks of files (spec §33): copy, move, move to
 * the trash, put back, delete, duplicate and link.
 *
 * A task runs inside the main loop a slice of time at a time, so the
 * window keeps answering while a large folder is copied.  It first plans
 * its steps -- making a folder, copying a file, renaming, removing --
 * walking the folders a few entries at a time, and then carries the steps
 * out in order, a large file in chunks.  A move within one file system is
 * one rename; across file systems it is a copy followed by the removal of
 * the source, which is skipped for any source whose copy failed, so a
 * failure never loses the only copy.
 *
 * Names that are taken are never overwritten: the copy gets the next free
 * name ("Report 2.pdf", or "Report copy.pdf" for a duplicate).
 */

#include "ops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

/* How many steps a task's table grows by, and how many walks. */
#define TASK_GROWTH		256U
#define TASK_WALK_GROWTH	16U

/* The largest list of extended attribute names, and the largest value, a copy carries. */
#define TASK_XATTR_NAMES	4096U
#define TASK_XATTR_VALUE	65536U

/*
 * The number the next task is given.
 *
 * It only increases, so a task is known by its number in the log for the
 * whole run; it starts at 1 so that 0 means no task.
 */
static unsigned task_next_id = 1U;

static int task_plan(struct fm_task *task, uint64_t deadline);
static int task_plan_source(struct fm_task *task);
static int task_plan_walk(struct fm_task *task);
static int task_plan_copy(struct fm_task *task, const char *source, const char *target, const struct stat *status);
static int task_plan_transfer(struct fm_task *task, const char *source, const char *target, const struct stat *status);
static int task_walk_finish(struct fm_task *task, int error);
static int task_plan_delete(struct fm_task *task, const char *source, const struct stat *status);
static int task_push_walk(struct fm_task *task, const char *source, const char *target, int post_order);
static struct fm_step *task_add(struct fm_task *task, unsigned kind, const char *source, const char *target);
static int task_run(struct fm_task *task, uint64_t deadline);
static int task_run_step(struct fm_task *task, struct fm_step *step, uint64_t deadline);
static int task_copy_chunk(struct fm_task *task, struct fm_step *step, uint64_t deadline);
static void task_copy_xattrs(int in, int out);
static void task_copy_close(struct fm_task *task, int keep);
static void task_fail(struct fm_task *task, size_t owner, int error, const char *path);
static char *task_join(const char *folder, const char *name);
static const char *task_base(const char *path);
static void task_parent(const char *path, char *parent, size_t size);
static int task_inside(const char *path, const char *folder);
static int task_mkdir_parents(const char *path);

/*
 * Makes a task of a kind over sources (absolute paths, copied) into a
 * destination folder (the trash for a move to the trash; unused for a put
 * back or a delete); NULL when memory runs out.
 */
struct fm_task *
fm_task_new(
	unsigned kind,
	char *const *sources,
	size_t count,
	const char *destination)
{
	struct fm_task *task;
	size_t index;

	/* The task, planning. */
	task = calloc(1, sizeof(*task));
	if (task == NULL)
		return NULL;
	task->id = task_next_id;
	task_next_id++;
	task->kind = kind;
	task->state = FM_TASK_PLANNING;
	task->copy_in = -1;
	task->copy_out = -1;
	if (destination != NULL)
		snprintf(task->destination, sizeof(task->destination), "%s", destination);

	/* The tables of sources, results and failures. */
	task->sources = calloc(count + 1U, sizeof(task->sources[0]));
	task->results = calloc(count + 1U, sizeof(task->results[0]));
	task->failed = calloc(count + 1U, 1U);
	if (task->sources == NULL || task->results == NULL || task->failed == NULL) {
		fm_task_free(task);
		return NULL;
	}

	/* The sources, copied. */
	for (index = 0; index < count; index++) {
		task->sources[index] = strdup(sources[index]);
		if (task->sources[index] == NULL) {
			fm_task_free(task);
			return NULL;
		}

		/* One more source is kept. */
		task->source_count++;
	}

	/* Succeeded: the task plans at its first step. */
	return task;
}

/*
 * Moves a task on for about a budget of milliseconds.
 *
 * Returns 1 while there is more to do, 0 once the task is finished (done
 * or cancelled).
 */
int
fm_task_step(
	struct fm_task *task,
	uint64_t budget_ms)
{
	uint64_t deadline;
	int more;

	/* A finished task has nothing to do. */
	if (task->state == FM_TASK_DONE || task->state == FM_TASK_CANCELLED)
		return 0;

	/* The time the slice ends. */
	deadline = fm_ops_clock() + budget_ms;

	/* Planning, until the plan is whole or the time is up. */
	if (task->state == FM_TASK_PLANNING) {
		more = task_plan(task, deadline);
		if (more != 0)
			return 1;
		task->state = FM_TASK_RUNNING;
	}

	/* Carrying out the steps. */
	more = task_run(task, deadline);
	if (more != 0)
		return 1;

	/* Succeeded: every step was carried out. */
	task->state = FM_TASK_DONE;
	return 0;
}

/*
 * Stops a task: a file being copied is removed, the steps not yet carried
 * out are not.
 */
void
fm_task_cancel(
	struct fm_task *task)
{
	/* A finished task stays as it is. */
	if (task->state == FM_TASK_DONE || task->state == FM_TASK_CANCELLED)
		return;

	/* The half-copied file goes, and the task ends. */
	task_copy_close(task, 0);
	task->state = FM_TASK_CANCELLED;
}

/*
 * Frees a task and everything it holds (its open folders and files too).
 */
void
fm_task_free(
	struct fm_task *task)
{
	size_t index;

	/* Nothing to free. */
	if (task == NULL)
		return;

	/* A file being copied is closed (and its half copy removed). */
	task_copy_close(task, 0);

	/* The folders still open, with their paths. */
	for (index = 0; index < task->walk_count; index++) {
		if (task->walks[index].directory != NULL)
			closedir(task->walks[index].directory);
		free(task->walks[index].source);
		free(task->walks[index].target);
	}

	/* The table of walks. */
	free(task->walks);

	/* The steps' paths and the table. */
	for (index = 0; index < task->step_count; index++) {
		free(task->steps[index].source);
		free(task->steps[index].target);
	}

	/* The table of steps. */
	free(task->steps);

	/* The sources and the results. */
	for (index = 0; index < task->source_count; index++) {
		free(task->sources[index]);
		if (task->results != NULL)
			free(task->results[index]);
	}

	/* The tables themselves. */
	free(task->sources);
	free(task->results);
	free(task->failed);
	free(task);
}

/*
 * Names what a kind of task does, for the log and the status pill.
 */
const char *
fm_task_verb(
	unsigned kind)
{
	/* Each kind's verb. */
	switch (kind) {
	case FM_TASK_COPY:
		return "copy";
	case FM_TASK_MOVE:
		return "move";
	case FM_TASK_TRASH:
		return "trash";
	case FM_TASK_RESTORE:
		return "restore";
	case FM_TASK_DELETE:
		return "delete";
	case FM_TASK_DUPLICATE:
		return "duplicate";
	case FM_TASK_LINK:
		return "link";
	default:
		break;
	}

	/* A kind this program does not know. */
	return "task";
}

/*
 * Finds a free path for a name in a folder: the name itself, or with the
 * suffix (" copy" for a duplicate), then with a number ("name 2.ext",
 * "name copy 2.ext").  A folder's name keeps its dots whole.
 *
 * Returns 0, or ENAMETOOLONG when no such path fits.
 */
int
fm_unique_name(
	const char *folder,
	const char *name,
	const char *suffix,
	char *path,
	size_t size)
{
	struct stat status;
	const char *dot;
	char base[FM_OPS_PATH_MAX];
	char extension[FM_OPS_PATH_MAX];
	size_t length;
	int written;
	int taken;
	int folder_name;
	unsigned number;

	/* A folder's name is not split (its dots are part of it). */
	written = snprintf(path, size, "%s/%s", folder, name);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;
	folder_name = 0;
	taken = lstat(path, &status);
	if (taken == 0)
		folder_name = S_ISDIR(status.st_mode);

	/* The name's stem and extension (a leading dot is not an extension). */
	dot = strrchr(name, '.');
	if (dot == NULL || dot == name || folder_name != 0)
		dot = name + strlen(name);
	length = (size_t)(dot - name);
	if (length >= sizeof(base))
		return ENAMETOOLONG;
	memcpy(base, name, length);
	base[length] = '\0';
	snprintf(extension, sizeof(extension), "%s", dot);
	if (suffix == NULL)
		suffix = "";

	/* The name with its suffix, then with 2, 3, ... until one is free. */
	for (number = 1; number < 10000U; number++) {
		if (number == 1U)
			written = snprintf(path, size, "%s/%s%s%s", folder, base, suffix, extension);
		else
			written = snprintf(path, size, "%s/%s%s %u%s", folder, base, suffix, number, extension);
		if (written < 0 || (size_t)written >= size)
			return ENAMETOOLONG;

		/* A path nothing is at is free. */
		taken = lstat(path, &status);
		if (taken != 0)
			return 0;
	}

	/* Ten thousand copies of one name: give up. */
	return EEXIST;
}

/*
 * Returns a monotonic time in milliseconds.
 */
uint64_t
fm_ops_clock(void)
{
	struct timespec now;
	int status;

	/* The monotonic clock. */
	status = clock_gettime(CLOCK_MONOTONIC, &now);
	if (status != 0)
		return 0U;

	/* Reports it in milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Plans until the plan is whole (0) or the deadline passes (1). */
static int
task_plan(
	struct fm_task *task,
	uint64_t deadline)
{
	uint64_t now;

	/* Walks and sources, one at a time (a failure is recorded with its source and the plan goes on). */
	for (;;) {
		/* A folder being walked goes on first, then the next source, until none is left. */
		if (task->walk_count != 0U) {
			(void)task_plan_walk(task);
		} else if (task->source_index < task->source_count) {
			(void)task_plan_source(task);
			task->source_index++;
		} else {
			return 0;
		}

		/* The time is up: the rest in the next slice. */
		now = fm_ops_clock();
		if (now >= deadline)
			return 1;
	}
}

/* Plans the steps of the next source (a folder's contents follow in the walk); nonzero on failure. */
static int
task_plan_source(
	struct fm_task *task)
{
	struct stat status;
	struct stat target_status;
	struct fm_step *step;
	char target[2 * FM_OPS_PATH_MAX + 32];
	char parent[FM_OPS_PATH_MAX];
	char original[FM_OPS_PATH_MAX];
	char name[FM_OPS_PATH_MAX];
	const char *source;
	const char *suffix;
	size_t owner;
	time_t deleted;
	int same_device;
	int same_folder;
	int error;

	/* The source and what it is; its steps are its own. */
	owner = task->source_index;
	task->owner = owner;
	source = task->sources[owner];
	error = lstat(source, &status);
	if (error != 0) {
		task_fail(task, owner, errno, source);
		return -1;
	}

	/* Each kind plans differently. */
	switch (task->kind) {
	case FM_TASK_COPY:
	case FM_TASK_DUPLICATE:
		/* A folder cannot be copied into itself. */
		error = task_inside(task->destination, source);
		if (error != 0) {
			task_fail(task, owner, EINVAL, source);
			return -1;
		}

		/* A free name in the destination, then the copy of the tree. */
		suffix = NULL;
		if (task->kind == FM_TASK_DUPLICATE)
			suffix = " copy";
		error = fm_unique_name(task->destination, task_base(source), suffix, target, sizeof(target));
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* The copy goes there; the tree is planned. */
		task->results[owner] = strdup(target);
		error = task_plan_copy(task, source, target, &status);
		break;
	case FM_TASK_MOVE:
		/* An item already in the destination stays where it is. */
		task_parent(source, parent, sizeof(parent));
		same_folder = strcmp(parent, task->destination);
		if (same_folder == 0) {
			task->results[owner] = strdup(source);
			return 0;
		}

		/* A folder cannot be moved into itself. */
		error = task_inside(task->destination, source);
		if (error != 0) {
			task_fail(task, owner, EINVAL, source);
			return -1;
		}

		/* A free name in the destination. */
		error = fm_unique_name(task->destination, task_base(source), NULL, target, sizeof(target));
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* The item goes there. */
		task->results[owner] = strdup(target);

		/* Within one file system a rename; across, a copy and the source's removal. */
		same_device = 0;
		error = stat(task->destination, &target_status);
		if (error == 0 && target_status.st_dev == status.st_dev)
			same_device = 1;
		if (same_device != 0) {
			step = task_add(task, FM_STEP_RENAME, source, target);
			error = 0;
			if (step == NULL)
				error = ENOMEM;
		} else {
			error = task_plan_transfer(task, source, target, &status);
		}

		/* The move is planned. */
		break;
	case FM_TASK_TRASH:
		/* A name in the trash and the record of where the item was. */
		error = fm_trash_name(task->destination, task_base(source), name, sizeof(name));
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* The record of where the item was. */
		snprintf(target, sizeof(target), "%s/info/%s.trashinfo", task->destination, name);
		step = task_add(task, FM_STEP_TRASHINFO, source, target);
		if (step == NULL)
			return ENOMEM;

		/* The item into the trash's files: a rename, or a copy and a removal across file systems. */
		snprintf(target, sizeof(target), "%s/files/%s", task->destination, name);
		task->results[owner] = strdup(target);
		same_device = 0;
		task_parent(target, parent, sizeof(parent));
		error = stat(parent, &target_status);
		if (error == 0 && target_status.st_dev == status.st_dev)
			same_device = 1;
		if (same_device != 0) {
			step = task_add(task, FM_STEP_RENAME, source, target);
			error = 0;
			if (step == NULL)
				error = ENOMEM;
		} else {
			error = task_plan_transfer(task, source, target, &status);
		}

		/* The move into the trash is planned. */
		break;
	case FM_TASK_RESTORE:
		/* Where the item was, from its record in the trash. */
		error = fm_trash_info_read(task->destination, task_base(source), original, sizeof(original), &deleted);
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* A free name in the folder it was in (made again if it is gone). */
		task_parent(original, parent, sizeof(parent));
		(void)task_mkdir_parents(parent);
		error = fm_unique_name(parent, task_base(original), NULL, target, sizeof(target));
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* The item goes there. */
		task->results[owner] = strdup(target);

		/* The item back: a rename, or a copy and a removal across file systems. */
		same_device = 0;
		error = stat(parent, &target_status);
		if (error == 0 && target_status.st_dev == status.st_dev)
			same_device = 1;
		if (same_device != 0) {
			step = task_add(task, FM_STEP_RENAME, source, target);
			error = 0;
			if (step == NULL)
				error = ENOMEM;
		} else {
			error = task_plan_transfer(task, source, target, &status);
		}

		/* The record goes once the item is back. */
		snprintf(target, sizeof(target), "%s/info/%s.trashinfo", task->destination, task_base(source));
		step = task_add(task, FM_STEP_UNTRASHINFO, target, NULL);
		if (step == NULL)
			error = ENOMEM;
		break;
	case FM_TASK_DELETE:
		/* The tree, children before their folders. */
		task->results[owner] = strdup(source);
		error = task_plan_delete(task, source, &status);
		break;
	case FM_TASK_LINK:
		/* A symbolic link to the source under a free name. */
		error = fm_unique_name(task->destination, task_base(source), NULL, target, sizeof(target));
		if (error != 0) {
			task_fail(task, owner, error, source);
			return -1;
		}

		/* The link goes there. */
		task->results[owner] = strdup(target);
		step = task_add(task, FM_STEP_SYMLINK, source, target);
		error = 0;
		if (step == NULL)
			error = ENOMEM;
		break;
	default:
		error = EINVAL;
		break;
	}

	/* Reports a source that could not be planned. */
	if (error != 0) {
		task_fail(task, owner, error, source);
		return -1;
	}

	/* Succeeded: the source's first steps are planned. */
	return 0;
}

/* Plans the next entry of the folder being walked, or finishes the folder; nonzero on failure. */
static int
task_plan_walk(
	struct fm_task *task)
{
	struct fm_walk *walk;
	struct dirent *item;
	struct stat status;
	char *source;
	char *target;
	int error;
	int dot;

	/* The folder's entries are the folder's source's steps. */
	walk = &task->walks[task->walk_count - 1U];
	task->owner = walk->owner;

	/* The folder, opened the first time it is walked. */
	if (walk->directory == NULL) {
		walk->directory = opendir(walk->source);
		if (walk->directory == NULL) {
			task_fail(task, walk->owner, errno, walk->source);
			error = task_walk_finish(task, -1);
			return error;
		}
	}

	/* The next entry but . and .., or the end of the folder. */
	for (;;) {
		item = readdir(walk->directory);
		if (item == NULL) {
			error = task_walk_finish(task, 0);
			return error;
		}

		/* The folder itself and its parent are not entries. */
		dot = 0;
		if (item->d_name[0] == '.' && item->d_name[1] == '\0')
			dot = 1;
		if (item->d_name[0] == '.' && item->d_name[1] == '.' && item->d_name[2] == '\0')
			dot = 1;
		if (dot == 0)
			break;
	}

	/* The entry's paths. */
	source = task_join(walk->source, item->d_name);
	if (source == NULL)
		return ENOMEM;
	target = NULL;
	if (walk->target != NULL) {
		target = task_join(walk->target, item->d_name);
		if (target == NULL) {
			free(source);
			return ENOMEM;
		}
	}

	/* What the entry is, and its steps (a folder's contents are walked next). */
	error = lstat(source, &status);
	if (error != 0) {
		task_fail(task, walk->owner, errno, source);
	} else if (walk->post_order != 0) {
		error = task_plan_delete(task, source, &status);
	} else {
		error = task_plan_copy(task, source, target, &status);
	}

	/* The paths are not needed any more. */
	free(source);
	free(target);

	/* Reports a failed entry. */
	if (error != 0)
		return error;

	/* Succeeded: the entry is planned. */
	return 0;
}

/* Closes the folder at the top of the walk stack; a folder being removed is removed after its contents. */
static int
task_walk_finish(
	struct fm_task *task,
	int error)
{
	struct fm_walk *walk;
	struct fm_step *step;

	/* The folder is closed. */
	walk = &task->walks[task->walk_count - 1U];
	if (walk->directory != NULL)
		closedir(walk->directory);
	walk->directory = NULL;

	/* A folder whose contents were removed is removed itself. */
	if (walk->post_order != 0 && error == 0) {
		step = task_add(task, FM_STEP_RMDIR, walk->source, NULL);
		if (step == NULL)
			error = ENOMEM;
	}

	/* The walk goes off the stack. */
	free(walk->source);
	free(walk->target);
	task->walk_count--;

	/* Reports how the folder ended. */
	if (error != 0)
		return error;

	/* Succeeded: the folder is planned. */
	return 0;
}

/*
 * Plans an item's move by copying it and removing the source: for a file
 * the copy and then its removal; for a folder its removal first on the walk
 * stack, under its copy, so that the copy is walked and planned first.
 */
static int
task_plan_transfer(
	struct fm_task *task,
	const char *source,
	const char *target,
	const struct stat *status)
{
	int error;
	int folder;

	/* A folder: the removal's walk under the copy's. */
	folder = S_ISDIR(status->st_mode);
	if (folder != 0) {
		error = task_plan_delete(task, source, status);
		if (error != 0)
			return error;
		error = task_plan_copy(task, source, target, status);
		if (error != 0)
			return error;
		return 0;
	}

	/* Anything else: the copy, then the removal. */
	error = task_plan_copy(task, source, target, status);
	if (error != 0)
		return error;
	error = task_plan_delete(task, source, status);
	if (error != 0)
		return error;

	/* Succeeded: the move is planned. */
	return 0;
}

/* Plans the copy of one item (a folder's contents are walked afterwards); nonzero on failure. */
static int
task_plan_copy(
	struct fm_task *task,
	const char *source,
	const char *target,
	const struct stat *status)
{
	struct fm_step *step;
	char link[FM_OPS_PATH_MAX];
	ssize_t length;
	int regular;
	int folder;
	int link_kind;

	/* A folder: made, then its contents walked. */
	folder = S_ISDIR(status->st_mode);
	if (folder != 0) {
		step = task_add(task, FM_STEP_MKDIR, source, target);
		if (step == NULL)
			return ENOMEM;
		step->mode = status->st_mode & 07777;
		return task_push_walk(task, source, target, 0);
	}

	/* A symbolic link: a link to the same text. */
	link_kind = S_ISLNK(status->st_mode);
	if (link_kind != 0) {
		length = readlink(source, link, sizeof(link) - 1U);
		if (length < 0)
			return errno;
		link[length] = '\0';
		step = task_add(task, FM_STEP_SYMLINK, link, target);
		if (step == NULL)
			return ENOMEM;
		return 0;
	}

	/* Anything but a plain file (a device, a pipe) is not copied. */
	regular = S_ISREG(status->st_mode);
	if (regular == 0)
		return ENOTSUP;

	/* A file: its bytes, mode and times. */
	step = task_add(task, FM_STEP_COPY, source, target);
	if (step == NULL)
		return ENOMEM;
	step->size = (uint64_t)status->st_size;
	step->mode = status->st_mode & 07777;
	step->modified = status->st_mtime;
	step->accessed = status->st_atime;
	task->bytes_total += step->size;

	/* Succeeded: the file's copy is planned. */
	return 0;
}

/* Plans the removal of one item (a folder after its contents, which are walked afterwards); nonzero on failure. */
static int
task_plan_delete(
	struct fm_task *task,
	const char *source,
	const struct stat *status)
{
	struct fm_step *step;
	int folder;

	/* A folder: its contents first, walked; the folder when the walk ends. */
	folder = S_ISDIR(status->st_mode);
	if (folder != 0)
		return task_push_walk(task, source, NULL, 1);

	/* Anything else is unlinked. */
	step = task_add(task, FM_STEP_UNLINK, source, NULL);
	if (step == NULL)
		return ENOMEM;

	/* Succeeded: the removal is planned. */
	return 0;
}

/* Puts a folder on the walk stack (opened when it is walked); nonzero when memory runs out. */
static int
task_push_walk(
	struct fm_task *task,
	const char *source,
	const char *target,
	int post_order)
{
	struct fm_walk *grown;
	struct fm_walk *walk;
	size_t capacity;

	/* Room for one more folder. */
	if (task->walk_count == task->walk_capacity) {
		capacity = task->walk_capacity + TASK_WALK_GROWTH;
		grown = realloc(task->walks, capacity * sizeof(task->walks[0]));
		if (grown == NULL)
			return ENOMEM;
		task->walks = grown;
		task->walk_capacity = capacity;
	}

	/* The folder, its paths copied, owned by the source being planned. */
	walk = &task->walks[task->walk_count];
	memset(walk, 0, sizeof(*walk));
	walk->post_order = post_order;
	walk->owner = task->owner;
	walk->source = strdup(source);
	if (walk->source == NULL)
		return ENOMEM;
	if (target != NULL) {
		walk->target = strdup(target);
		if (walk->target == NULL) {
			free(walk->source);
			return ENOMEM;
		}
	}

	/* Succeeded: it is walked next. */
	task->walk_count++;
	return 0;
}

/* Adds a step after the others; NULL when memory runs out. */
static struct fm_step *
task_add(
	struct fm_task *task,
	unsigned kind,
	const char *source,
	const char *target)
{
	struct fm_step *grown;
	struct fm_step *step;
	size_t capacity;

	/* Room for one more step. */
	if (task->step_count == task->step_capacity) {
		capacity = task->step_capacity + TASK_GROWTH;
		grown = realloc(task->steps, capacity * sizeof(task->steps[0]));
		if (grown == NULL)
			return NULL;
		task->steps = grown;
		task->step_capacity = capacity;
	}

	/* The step, its paths copied, owned by the source being planned. */
	step = &task->steps[task->step_count];
	memset(step, 0, sizeof(*step));
	step->kind = kind;
	step->owner = task->owner;
	step->source = strdup(source);
	if (step->source == NULL)
		return NULL;
	if (target != NULL) {
		step->target = strdup(target);
		if (step->target == NULL) {
			free(step->source);
			return NULL;
		}
	}

	/* Every step but the making and removing of folders and records counts as one item. */
	if (kind != FM_STEP_MKDIR &&
	    kind != FM_STEP_RMDIR &&
	    kind != FM_STEP_TRASHINFO &&
	    kind != FM_STEP_UNTRASHINFO)
		task->files_total++;

	/* Succeeded: the step is planned. */
	task->step_count++;
	return step;
}

/* Carries out steps until they are all done (0) or the deadline passes (1). */
static int
task_run(
	struct fm_task *task,
	uint64_t deadline)
{
	struct fm_step *step;
	uint64_t now;
	int more;

	/* Each step in order. */
	while (task->step_index < task->step_count) {
		step = &task->steps[task->step_index];

		/* A step not finished in this slice goes on in the next. */
		more = task_run_step(task, step, deadline);
		if (more != 0)
			return 1;
		task->step_index++;

		/* The time is up: the rest in the next slice. */
		now = fm_ops_clock();
		if (now >= deadline)
			return task->step_index < task->step_count;
	}

	/* Every step is done. */
	return 0;
}

/* Carries out one step; returns 1 when a copy goes on in the next slice. */
static int
task_run_step(
	struct fm_task *task,
	struct fm_step *step,
	uint64_t deadline)
{
	const char *failed_path;
	int status;

	/* The removal of a source whose copy failed is skipped: it would lose the only copy. */
	if ((step->kind == FM_STEP_UNLINK || step->kind == FM_STEP_RMDIR) &&
	    task->kind != FM_TASK_DELETE &&
	    task->failed[step->owner] != 0)
		return 0;

	/* Each kind of step. */
	status = 0;
	switch (step->kind) {
	case FM_STEP_MKDIR:
		status = mkdir(step->target, step->mode | 0700);
		break;
	case FM_STEP_COPY:
		return task_copy_chunk(task, step, deadline);
	case FM_STEP_SYMLINK:
		status = symlink(step->source, step->target);
		break;
	case FM_STEP_RENAME:
		status = rename(step->source, step->target);
		break;
	case FM_STEP_UNLINK:
		status = unlink(step->source);
		break;
	case FM_STEP_RMDIR:
		status = rmdir(step->source);
		break;
	case FM_STEP_TRASHINFO:
		status = fm_trash_info_write(step->target, step->source, time(NULL));
		if (status != 0) {
			errno = status;
			status = -1;
		}

		/* The record is written. */
		break;
	case FM_STEP_UNTRASHINFO:
		status = unlink(step->source);
		break;
	default:
		break;
	}

	/* A failed step is counted against its source. */
	if (status != 0) {
		failed_path = step->source;
		if (step->target != NULL)
			failed_path = step->target;
		task_fail(task, step->owner, errno, failed_path);
		return 0;
	}

	/* Succeeded: one more item is done (folders and records are not counted). */
	if (step->kind != FM_STEP_MKDIR &&
	    step->kind != FM_STEP_RMDIR &&
	    step->kind != FM_STEP_TRASHINFO &&
	    step->kind != FM_STEP_UNTRASHINFO)
		task->files_done++;
	return 0;
}

/* Copies a file a chunk at a time until it is done (0) or the deadline passes (1). */
static int
task_copy_chunk(
	struct fm_task *task,
	struct fm_step *step,
	uint64_t deadline)
{
	struct timespec times[2];
	uint64_t now;
	char *buffer;
	ssize_t got;
	ssize_t put;
	size_t done;
	int status;

	/* The files, opened when the copy starts (the target must not exist). */
	if (task->copy_in < 0) {
		task->copy_in = open(step->source, O_RDONLY);
		if (task->copy_in < 0) {
			task_fail(task, step->owner, errno, step->source);
			return 0;
		}

		/* The target, which must not exist yet. */
		task->copy_out = open(step->target, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (task->copy_out < 0) {
			task_fail(task, step->owner, errno, step->target);
			close(task->copy_in);
			task->copy_in = -1;
			return 0;
		}

		/* Nothing is copied yet. */
		task->copy_done = 0;
	}

	/* A buffer for the chunks. */
	buffer = malloc(FM_TASK_CHUNK);
	if (buffer == NULL) {
		task_fail(task, step->owner, ENOMEM, step->target);
		task_copy_close(task, 0);
		return 0;
	}

	/* Chunks until the end of the file or of the slice. */
	for (;;) {
		got = read(task->copy_in, buffer, FM_TASK_CHUNK);
		if (got < 0) {
			task_fail(task, step->owner, errno, step->source);
			free(buffer);
			task_copy_close(task, 0);
			return 0;
		}

		/* The end of the file ends the copy. */
		if (got == 0)
			break;

		/* The chunk written in full. */
		done = 0;
		while (done < (size_t)got) {
			put = write(task->copy_out, buffer + done, (size_t)got - done);
			if (put <= 0) {
				task_fail(task, step->owner, errno, step->target);
				free(buffer);
				task_copy_close(task, 0);
				return 0;
			}

			/* On to the rest of the chunk. */
			done += (size_t)put;
		}

		/* The chunk counts. */
		task->copy_done += (uint64_t)got;
		task->bytes_done += (uint64_t)got;

		/* The slice is up: the rest of the file in the next. */
		now = fm_ops_clock();
		if (now >= deadline) {
			free(buffer);
			return 1;
		}
	}

	/* The buffer is not needed any more. */
	free(buffer);

	/* The copy gets the source's mode, times and extended attributes (tags among them). */
	status = fchmod(task->copy_out, step->mode);
	(void)status;
	task_copy_xattrs(task->copy_in, task->copy_out);
	times[0].tv_sec = step->accessed;
	times[0].tv_nsec = 0;
	times[1].tv_sec = step->modified;
	times[1].tv_nsec = 0;
	status = futimens(task->copy_out, times);
	(void)status;

	/* Succeeded: the file is copied and closed. */
	task_copy_close(task, 1);
	task->files_done++;
	return 0;
}

/* Copies the extended attributes of one open file to another (those that cannot be set are left out). */
static void
task_copy_xattrs(
	int in,
	int out)
{
	char *names;
	char *value;
	ssize_t length;
	ssize_t size;
	size_t at;
	int status;

	/* The names of the source's attributes. */
	names = malloc(TASK_XATTR_NAMES);
	if (names == NULL)
		return;
	length = flistxattr(in, names, TASK_XATTR_NAMES);
	if (length <= 0) {
		free(names);
		return;
	}

	/* A buffer for one value at a time. */
	value = malloc(TASK_XATTR_VALUE);
	if (value == NULL) {
		free(names);
		return;
	}

	/* Each name (they are NUL-separated) and its value. */
	for (at = 0; at < (size_t)length; at += strlen(names + at) + 1U) {
		size = fgetxattr(in, names + at, value, TASK_XATTR_VALUE);
		if (size < 0)
			continue;
		status = fsetxattr(out, names + at, value, (size_t)size, 0);
		(void)status;
	}

	/* The buffers go. */
	free(value);
	free(names);
}

/* Closes the files of a copy; a copy not kept (failed or cancelled) is removed. */
static void
task_copy_close(
	struct fm_task *task,
	int keep)
{
	struct fm_step *step;

	/* No copy is open. */
	if (task->copy_in < 0)
		return;

	/* Both files closed. */
	close(task->copy_in);
	task->copy_in = -1;
	if (task->copy_out >= 0)
		close(task->copy_out);
	task->copy_out = -1;

	/* A half copy is not left behind. */
	if (keep == 0 && task->step_index < task->step_count) {
		step = &task->steps[task->step_index];
		if (step->kind == FM_STEP_COPY && step->target != NULL)
			(void)unlink(step->target);
	}
}

/* Records a failure of a source: the first one's reason and path are kept for the message. */
static void
task_fail(
	struct fm_task *task,
	size_t owner,
	int error,
	const char *path)
{
	/* The source failed (its removal will be skipped). */
	if (owner < task->source_count)
		task->failed[owner] = 1;
	task->error_count++;

	/* The first failure is the one told. */
	if (task->error != 0)
		return;
	task->error = error;
	if (task->error == 0)
		task->error = EIO;
	snprintf(task->error_path, sizeof(task->error_path), "%s", path);
}

/* Joins a folder and a name into an allocated path. */
static char *
task_join(
	const char *folder,
	const char *name)
{
	size_t length;
	char *path;
	int root;

	/* The folder, a slash (unless the folder is the root) and the name. */
	length = strlen(folder) + strlen(name) + 2U;
	path = malloc(length);
	if (path == NULL)
		return NULL;
	root = strcmp(folder, "/");
	if (root == 0)
		snprintf(path, length, "/%s", name);
	else
		snprintf(path, length, "%s/%s", folder, name);

	/* Reports the joined path. */
	return path;
}

/* Returns the last part of a path. */
static const char *
task_base(
	const char *path)
{
	const char *slash;

	/* After the last slash. */
	slash = strrchr(path, '/');
	if (slash == NULL)
		return path;

	/* Reports the part after it. */
	return slash + 1;
}

/* Writes the folder a path is in ("/" for the root's items). */
static void
task_parent(
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

	/* Reports that part. */
	memcpy(parent, path, length);
	parent[length] = '\0';
}

/* Tells whether a path is a folder or inside it. */
static int
task_inside(
	const char *path,
	const char *folder)
{
	size_t length;
	int match;

	/* The folder must start the path. */
	length = strlen(folder);
	match = strncmp(path, folder, length);
	if (match != 0)
		return 0;

	/* And end there or at a slash. */
	if (path[length] == '\0' || path[length] == '/')
		return 1;

	/* A longer name that only starts the same. */
	return 0;
}

/* Makes a folder and the folders above it that are missing (mkdir -p); nonzero when it cannot. */
static int
task_mkdir_parents(
	const char *path)
{
	char partial[FM_OPS_PATH_MAX];
	size_t index;
	int status;

	/* Each prefix that ends at a slash, and the whole path. */
	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index] != '\0'; index++) {
		if (partial[index] != '/')
			continue;
		partial[index] = '\0';
		(void)mkdir(partial, 0755);
		partial[index] = '/';
	}

	/* The whole path, which must be a folder now. */
	status = mkdir(partial, 0755);
	if (status != 0 && errno != EEXIST)
		return errno;

	/* Succeeded: the folder is there. */
	return 0;
}
