/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws071: checks zdesktop-files' model on the host in a temporary folder:
 * the tasks (copy, duplicate, move within and across file systems, trash
 * and put back, delete), the free names, the trash's records, the undo
 * history and the clipboard.
 *
 *   files-model TEMPORARY-FOLDER [OTHER-FILE-SYSTEM-FOLDER]
 *
 * Prints one line a check and "files-model: PASS" or "FAIL" at the end.
 */

#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static void make_file(const char *path, const char *text);
static int file_is(const char *path, const char *text);
static int exists(const char *path);
static int run(struct fm_task *task);

int
main(
	int argc,
	char **argv)
{
	char root[FM_PATH_MAX];
	char path[2 * FM_PATH_MAX];
	char other[2 * FM_PATH_MAX];
	char trash[FM_PATH_MAX];
	char original[FM_PATH_MAX];
	char value[64];
	char *sources[4];
	char *targets[4];
	char **paths;
	struct fm_task *task;
	struct fm_undo history;
	struct fm_undo_item item;
	unsigned mode;
	size_t count;
	ssize_t length;
	time_t deleted;
	int error;

	if (argc < 2) {
		fprintf(stderr, "usage: files-model TEMPORARY-FOLDER [OTHER-FILE-SYSTEM-FOLDER]\n");
		return 2;
	}
	mkdir(argv[1], 0755);
	if (realpath(argv[1], root) == NULL)
		return 2;
	snprintf(path, sizeof(path), "%s/home", root);
	mkdir(path, 0755);
	setenv("HOME", path, 1);
	snprintf(path, sizeof(path), "%s/data", root);
	setenv("XDG_DATA_HOME", path, 1);
	snprintf(path, sizeof(path), "%s/run", root);
	mkdir(path, 0700);
	setenv("XDG_RUNTIME_DIR", path, 1);

	/* A source tree: a file, a folder with a file, a nested folder and a link, a tag on a file. */
	snprintf(path, sizeof(path), "%s/src", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/src/Report.pdf", root);
	make_file(path, "report");
	error = setxattr(path, "user.zdesktop.tags", "Work", 4, 0);
	check(error == 0, "xattr set on the source (the host's file system has user xattrs)");
	snprintf(path, sizeof(path), "%s/src/Folder.v1", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/src/Folder.v1/inner.txt", root);
	make_file(path, "inner");
	snprintf(path, sizeof(path), "%s/src/Folder.v1/deep", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/src/Folder.v1/deep/leaf.txt", root);
	make_file(path, "leaf");
	snprintf(path, sizeof(path), "%s/src/Folder.v1/link", root);
	symlink("inner.txt", path);
	snprintf(path, sizeof(path), "%s/dst", root);
	mkdir(path, 0755);

	/* 1. Copy a file and a folder; the copy keeps contents, the link and the tag. */
	snprintf(path, sizeof(path), "%s/src/Report.pdf", root);
	sources[0] = strdup(path);
	snprintf(path, sizeof(path), "%s/src/Folder.v1", root);
	sources[1] = strdup(path);
	snprintf(path, sizeof(path), "%s/dst", root);
	task = fm_task_new(FM_TASK_COPY, sources, 2, path);
	check(run(task) == 0 && task->error_count == 0, "copy: no error");
	check(task->files_done == 4, "copy: four items (the top file, two nested files, a link)");
	snprintf(path, sizeof(path), "%s/dst/Report.pdf", root);
	check(file_is(path, "report"), "copy: the file's contents");
	length = getxattr(path, "user.zdesktop.tags", value, sizeof(value));
	check(length == 4 && memcmp(value, "Work", 4) == 0, "copy: the tag (xattr) came along");
	snprintf(path, sizeof(path), "%s/dst/Folder.v1/deep/leaf.txt", root);
	check(file_is(path, "leaf"), "copy: a nested file");
	snprintf(path, sizeof(path), "%s/dst/Folder.v1/link", root);
	length = readlink(path, value, sizeof(value) - 1);
	check(length == 9 && memcmp(value, "inner.txt", 9) == 0, "copy: the link is a link");
	fm_task_free(task);

	/* 2. Copying again keeps both: "Report 2.pdf" and "Folder.v1 2" (a folder's dots are its name). */
	snprintf(path, sizeof(path), "%s/dst", root);
	task = fm_task_new(FM_TASK_COPY, sources, 2, path);
	run(task);
	snprintf(path, sizeof(path), "%s/dst/Report 2.pdf", root);
	check(exists(path), "copy again: Report 2.pdf");
	snprintf(path, sizeof(path), "%s/dst/Folder.v1 2/inner.txt", root);
	check(exists(path), "copy again: Folder.v1 2");
	fm_task_free(task);

	/* 3. Duplicate beside itself. */
	snprintf(path, sizeof(path), "%s/src", root);
	task = fm_task_new(FM_TASK_DUPLICATE, sources, 1, path);
	run(task);
	snprintf(path, sizeof(path), "%s/src/Report copy.pdf", root);
	check(exists(path), "duplicate: Report copy.pdf");
	check(task->results[0] != NULL && strcmp(task->results[0], path) == 0, "duplicate: the result names the copy");
	fm_task_free(task);

	/* 4. A folder cannot be copied into itself. */
	snprintf(path, sizeof(path), "%s/src/Folder.v1/deep", root);
	task = fm_task_new(FM_TASK_COPY, sources + 1, 1, path);
	run(task);
	check(task->error == EINVAL && task->failed[0] != 0, "copy into itself: refused");
	fm_task_free(task);

	/* 5. Move within the file system: a rename. */
	snprintf(path, sizeof(path), "%s/dst/Report 2.pdf", root);
	targets[0] = strdup(path);
	snprintf(path, sizeof(path), "%s/src", root);
	task = fm_task_new(FM_TASK_MOVE, targets, 1, path);
	run(task);
	snprintf(path, sizeof(path), "%s/src/Report 2.pdf", root);
	check(exists(path) && !exists(targets[0]), "move: renamed into the folder");
	check(task->step_count == 1 && task->steps[0].kind == FM_STEP_RENAME, "move: one rename step");
	fm_task_free(task);
	free(targets[0]);

	/* 6. To the trash and back. */
	error = fm_trash_path(trash, sizeof(trash));
	check(error == 0, "trash: the trash's folders are made");
	snprintf(path, sizeof(path), "%s/src/Report 2.pdf", root);
	targets[0] = strdup(path);
	task = fm_task_new(FM_TASK_TRASH, targets, 1, trash);
	run(task);
	snprintf(path, sizeof(path), "%s/files/Report 2.pdf", trash);
	check(exists(path) && !exists(targets[0]), "trash: the file is in the trash");
	error = fm_trash_info_read(trash, "Report 2.pdf", original, sizeof(original), &deleted);
	check(error == 0 && strcmp(original, targets[0]) == 0 && deleted != 0, "trash: the record says where and when");
	snprintf(other, sizeof(other), "%s/info/Report 2.pdf.trashinfo", trash);
	check(exists(other), "trash: the record file");
	fm_task_free(task);

	/* 7. A second file of the same name gets "name.2" in the trash. */
	make_file(targets[0], "second");
	task = fm_task_new(FM_TASK_TRASH, targets, 1, trash);
	run(task);
	snprintf(path, sizeof(path), "%s/files/Report 2.pdf.2", trash);
	check(exists(path), "trash: the second of one name is name.2");
	fm_task_free(task);

	/* 8. Put back: the first one returns to where it was, its record goes. */
	snprintf(path, sizeof(path), "%s/files/Report 2.pdf", trash);
	sources[2] = strdup(path);
	task = fm_task_new(FM_TASK_RESTORE, sources + 2, 1, trash);
	run(task);
	check(file_is(targets[0], "report"), "put back: the file is where it was");
	check(!exists(other), "put back: the record is gone");
	fm_task_free(task);

	/* 9. Delete a folder tree for good. */
	snprintf(path, sizeof(path), "%s/dst/Folder.v1 2", root);
	targets[1] = strdup(path);
	task = fm_task_new(FM_TASK_DELETE, targets + 1, 1, NULL);
	run(task);
	check(!exists(targets[1]) && task->error_count == 0, "delete: the tree is gone");
	fm_task_free(task);

	/* 10. Move across file systems (when a second one is given): copied, then the source removed. */
	if (argc > 2) {
		snprintf(path, sizeof(path), "%s/dst/Folder.v1", root);
		targets[2] = strdup(path);
		task = fm_task_new(FM_TASK_MOVE, targets + 2, 1, argv[2]);
		run(task);
		snprintf(path, sizeof(path), "%s/Folder.v1/deep/leaf.txt", argv[2]);
		check(file_is(path, "leaf") && !exists(targets[2]), "move across file systems: copied and removed");
		fm_task_free(task);
		snprintf(path, sizeof(path), "%s/Folder.v1", argv[2]);
		targets[3] = strdup(path);
		task = fm_task_new(FM_TASK_DELETE, targets + 3, 1, NULL);
		run(task);
		fm_task_free(task);
	} else {
		printf("skip: move across file systems (no second folder)\n");
	}

	/* 11. The undo history: push, take, redo, and a new change empties the redo. */
	memset(&history, 0, sizeof(history));
	fm_undo_push(&history, FM_UNDO_RENAME, 1, sources, targets, NULL, NULL);
	fm_undo_push(&history, FM_UNDO_MOVE, 2, sources, targets, NULL, NULL);
	check(fm_undo_take(&history, 0, &item) == 1 && item.kind == FM_UNDO_MOVE && item.count == 2, "undo: the newest change");
	fm_undo_push_redo(&history, &item);
	check(history.redo_count == 1 && history.undo_count == 1, "undo: kept for redo");
	fm_undo_push(&history, FM_UNDO_COPY, 1, sources, targets, NULL, NULL);
	check(history.redo_count == 0 && history.undo_count == 2, "undo: a new change empties the redo");
	fm_undo_free(&history);

	/* 12. The clipboard. */
	error = fm_clip_set(FM_CLIP_CUT, sources, 2);
	check(error == 0, "clipboard: set");
	error = fm_clip_get(&mode, &paths, &count);
	check(error == 0 && mode == FM_CLIP_CUT && count == 2 && strcmp(paths[1], sources[1]) == 0, "clipboard: read back");
	fm_paths_free(paths, count);
	fm_clip_clear();
	fm_clip_get(&mode, &paths, &count);
	check(mode == FM_CLIP_NONE && count == 0, "clipboard: cleared");

	/* 13. Free names. */
	snprintf(path, sizeof(path), "%s/src", root);
	error = fm_unique_name(path, "Report.pdf", NULL, other, sizeof(other));
	snprintf(path, sizeof(path), "%s/src/Report 3.pdf", root);
	check(error == 0 && strcmp(other, path) == 0, "free name: Report 3.pdf after Report.pdf and Report 2.pdf");

	printf("files-model: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures != 0;
}

static void
check(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		failures++;
}

static void
make_file(
	const char *path,
	const char *text)
{
	FILE *file;

	file = fopen(path, "w");
	if (file == NULL)
		return;
	fputs(text, file);
	fclose(file);
}

static int
file_is(
	const char *path,
	const char *text)
{
	char buffer[256];
	FILE *file;
	size_t length;

	file = fopen(path, "r");
	if (file == NULL)
		return 0;
	length = fread(buffer, 1, sizeof(buffer) - 1, file);
	fclose(file);
	buffer[length] = '\0';
	return strcmp(buffer, text) == 0;
}

static int
exists(
	const char *path)
{
	struct stat status;

	return lstat(path, &status) == 0;
}

static int
run(
	struct fm_task *task)
{
	int rounds;

	for (rounds = 0; rounds < 10000; rounds++) {
		if (fm_task_step(task, 5) == 0)
			return 0;
	}
	return -1;
}
