/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p023: the host test of the Storage page's Trash
 * (userland/desktop/settings/storage-trash.c compiled unchanged), on a
 * trash the run script makes (run-host-storage-trash.sh):
 *   host-storage-trash HOME OUTSIDE_FILE
 *   - the trash's folder is $HOME/.local/share/Trash ($XDG_DATA_HOME unset);
 *   - emptying removes everything in files/ and info/ (folders inside
 *     folders too), keeps files/ and info/ themselves, and removes a link
 *     without touching the file it names (OUTSIDE_FILE stays).
 * Prints one line a check and "host-storage-trash: PASS" or FAIL.
 */

#include "userland/desktop/settings/storage-trash.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int passed);
static int entries(const char *path);

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* A pass. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/* Counts a folder's entries but . and .. (-1 when it cannot be read). */
static int
entries(
	const char *path)
{
	struct dirent *entry;
	DIR *folder;
	int count;
	int dot;
	int dotdot;

	/* The folder. */
	folder = opendir(path);
	if (folder == NULL)
		return -1;

	/* Each entry. */
	count = 0;
	for (;;) {
		entry = readdir(folder);
		if (entry == NULL)
			break;
		dot = strcmp(entry->d_name, ".");
		dotdot = strcmp(entry->d_name, "..");
		if (dot != 0 && dotdot != 0)
			count++;
	}

	/* The count. */
	(void)closedir(folder);
	return count;
}

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	static struct se_trash trash;
	struct stat status;
	char path[1024];
	char expected[1024];
	char part[1100];
	uint64_t removed;
	unsigned state;
	unsigned waited;
	int error;

	/* The arguments. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-storage-trash HOME OUTSIDE_FILE\n");
		return 2;
	}

	/* The trash's folder. */
	(void)setenv("HOME", argv[1], 1);
	(void)unsetenv("XDG_DATA_HOME");
	error = se_trash_path(path, sizeof(path));
	(void)snprintf(expected, sizeof(expected), "%s/.local/share/Trash", argv[1]);
	check("the home trash", error == 0 && strcmp(path, expected) == 0);

	/* Emptied, to the end. */
	error = se_trash_empty_start(&trash, path);
	check("emptying started", error == 0);
	state = SE_TRASH_EMPTYING;
	for (waited = 0; waited < 10000U && state == SE_TRASH_EMPTYING; waited += 10U) {
		state = se_trash_poll(&trash, &removed, &error);
		(void)usleep(10000);
	}

	/* The thread joined, and how it went. */
	se_trash_finish(&trash);
	printf("removed %llu, errno %d\n", (unsigned long long)removed, error);
	check("emptied without error", state == SE_TRASH_EMPTIED && error == 0);

	/* files/ and info/ empty and kept, the link's file kept. */
	(void)snprintf(part, sizeof(part), "%s/files", path);
	check("files/ empty and kept", entries(part) == 0);
	(void)snprintf(part, sizeof(part), "%s/info", path);
	check("info/ empty and kept", entries(part) == 0);
	error = stat(argv[2], &status);
	check("a link's file is not touched", error == 0 && status.st_size == 5000);

	/* The verdict. */
	if (failures != 0) {
		printf("host-storage-trash: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-storage-trash: PASS\n");
	return 0;
}
