/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The BUG-061 regression: /dev/fd/N and /dev/stdin are symbolic links to
 * what the descriptor holds when not followed, and the file itself when
 * followed.  Prints one line per check and exits with the number of
 * failures.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int check(const char *what, int passed);
static int link_to(const char *path, const char *expected);

/* The number of failed checks, which is the exit status. */
static int failures;

/*
 * Runs every check.
 */
int
main(
	void)
{
	struct stat followed;
	struct stat link;
	struct stat direct;
	char path[64];
	char target[256];
	char *slave_name;
	int directory;
	int regular;
	int master;
	int slave;
	int pipes[2];
	int status;
	ssize_t length;

	/* Opens a directory, a regular file and a pipe to name through /dev/fd. */
	directory = open("/dev", O_RDONLY | O_DIRECTORY);
	regular = open("/etc/passwd", O_RDONLY);
	status = pipe(pipes);
	if (directory < 0 || regular < 0 || status != 0) {
		perror("setup");
		return 100;
	}

	/* A directory: a link when not followed, the directory when followed. */
	snprintf(path, sizeof(path), "/dev/fd/%d", directory);
	status = lstat(path, &link);
	check("dir lstat is a link", status == 0 && S_ISLNK(link.st_mode));
	status = stat(path, &followed);
	check("dir stat is a directory", status == 0 && S_ISDIR(followed.st_mode));
	status = fstat(directory, &direct);
	check("dir stat is fstat", followed.st_ino == direct.st_ino);
	check("dir link names /dev", link_to(path, "/dev"));
	length = readlink(path, target, sizeof(target));
	check("dir link size is target length",
	    length > 0 && link.st_size == (off_t)length);
	status = fstatat(AT_FDCWD, path, &link, AT_SYMLINK_NOFOLLOW);
	check("fstatat nofollow is a link", status == 0 && S_ISLNK(link.st_mode));

	/* A regular file opened read only. */
	snprintf(path, sizeof(path), "/dev/fd/%d", regular);
	status = lstat(path, &link);
	check("file lstat is a link", status == 0 && S_ISLNK(link.st_mode));
	check("file link is r-x", (link.st_mode & 0777) == 0500);
	status = stat(path, &followed);
	check("file stat is regular", status == 0 && S_ISREG(followed.st_mode));
	check("file link names /etc/passwd", link_to(path, "/etc/passwd"));

	/* A pipe, which has no name and is described by its kind. */
	snprintf(path, sizeof(path), "/dev/fd/%d", pipes[1]);
	status = lstat(path, &link);
	check("pipe lstat is a link", status == 0 && S_ISLNK(link.st_mode));
	check("pipe write end is -wx", (link.st_mode & 0777) == 0300);
	length = readlink(path, target, sizeof(target) - 1);
	if (length > 0)
		target[length] = '\0';
	check("pipe link is pipe:[N]",
	    length > 6 && strncmp(target, "pipe:[", 6) == 0);
	status = stat(path, &followed);
	check("pipe stat is a FIFO", status == 0 && S_ISFIFO(followed.st_mode));

	/* The standard names point into /dev/fd. */
	status = lstat("/dev/stdin", &link);
	check("stdin lstat is a link", status == 0 && S_ISLNK(link.st_mode));
	check("stdin link is fd/0", link_to("/dev/stdin", "fd/0"));

	/* A terminal slave, which devfs keeps one directory below its top. */
	master = posix_openpt(O_RDWR | O_NOCTTY);
	check("posix_openpt", master >= 0);
	if (master >= 0) {
		(void)grantpt(master);
		(void)unlockpt(master);
		slave_name = ptsname(master);
		slave = -1;
		if (slave_name != NULL)
			slave = open(slave_name, O_RDWR | O_NOCTTY);
		check("open the slave", slave >= 0);
		snprintf(path, sizeof(path), "/dev/fd/%d", slave);
		if (slave >= 0)
			check("slave link names ptsname", link_to(path, slave_name));
	}

	/* A short buffer gets the start of the target, unterminated. */
	snprintf(path, sizeof(path), "/dev/fd/%d", directory);
	length = readlink(path, target, 3);
	check("short readlink is cut", length == 3 && memcmp(target, "/de", 3) == 0);

	/* Reports the number of failures. */
	printf("failures=%d\n", failures);
	return failures;
}

/* Prints one check and counts it when it failed. */
static int
check(
	const char *what,
	int passed)
{
	/* Prints the outcome. */
	if (passed) {
		printf("PASS %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		failures++;
	}

	/* Reports the outcome to a caller that chains checks. */
	return passed;
}

/* Reports whether a link's target is the expected text. */
static int
link_to(
	const char *path,
	const char *expected)
{
	char target[256];
	ssize_t length;
	int difference;

	/* Reads the target and terminates it. */
	length = readlink(path, target, sizeof(target) - 1);
	if (length < 0)
		return 0;
	target[length] = '\0';

	/* Shows a mismatch to the reader. */
	difference = strcmp(target, expected);
	if (difference != 0) {
		printf("  %s -> %s (expected %s)\n", path, target, expected);
		return 0;
	}

	/* Succeeded: the target matches. */
	return 1;
}
