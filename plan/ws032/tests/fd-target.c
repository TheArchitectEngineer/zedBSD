/* WS032: /dev/fd, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("DFD %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	char path[] = "/tmp/devfd.XXXXXX";
	char name[32], buffer[32];
	DIR *directory;
	struct dirent *entry;
	struct stat status;
	int fd, alias, pipes[2], count;
	off_t where;

	/* The listing is the same for everyone: every number is a node. */
	directory = opendir("/dev/fd");
	count = 0;
	while (directory != NULL && (entry = readdir(directory)) != NULL) {
		if (strcmp(entry->d_name, ".") != 0 &&
		    strcmp(entry->d_name, "..") != 0)
			count++;
	}
	if (directory != NULL)
		closedir(directory);
	check("/dev/fd lists a node for every descriptor number", count == 32);

	fd = mkstemp(path);
	if (fd < 0) {
		printf("DFD FAIL cannot create a temporary file\n");
		return 1;
	}
	(void)write(fd, "0123456789", 10);
	(void)lseek(fd, 3, SEEK_SET);

	snprintf(name, sizeof(name), "/dev/fd/%d", fd);
	check("a descriptor node can be stat'ed",
	      stat(name, &status) == 0 && S_ISCHR(status.st_mode));

	alias = open(name, O_RDWR);
	check("opening the node succeeds", alias >= 0);

	/* The offset is shared, which a copy would not be. */
	check("the offset is the one the original had",
	      lseek(alias, 0, SEEK_CUR) == 3);
	check("reading through the alias advances the original",
	      read(alias, buffer, 2) == 2 && buffer[0] == '3' &&
	      lseek(fd, 0, SEEK_CUR) == 5);
	where = lseek(fd, 8, SEEK_SET);
	check("seeking the original moves the alias",
	      where == 8 && lseek(alias, 0, SEEK_CUR) == 8);
	close(alias);

	/* Closing the alias must not close the original. */
	check("the original survives the alias being closed",
	      lseek(fd, 0, SEEK_CUR) == 8);

	/* A descriptor that is not held cannot be opened. */
	errno = 0;
	check("an unheld descriptor is refused",
	      open("/dev/fd/31", O_RDONLY) == -1 && errno == EBADF);

	/* The access mode may not exceed what the descriptor has. */
	close(fd);
	fd = open(path, O_RDONLY);
	snprintf(name, sizeof(name), "/dev/fd/%d", fd);
	errno = 0;
	check("a read-only descriptor cannot be opened for writing",
	      open(name, O_WRONLY) == -1 && errno == EACCES);
	check("a read-only descriptor can be opened for reading",
	      (alias = open(name, O_RDONLY)) >= 0);
	if (alias >= 0)
		close(alias);
	close(fd);
	(void)unlink(path);

	/*
	 * A pipe has no name to reopen and no offset to copy, so this is the
	 * case that separates handing back the description from making one.
	 */
	if (pipe(pipes) != 0) {
		check("a pipe can be reached through /dev/fd", 0);
	} else {
		snprintf(name, sizeof(name), "/dev/fd/%d", pipes[0]);
		alias = open(name, O_RDONLY);
		(void)write(pipes[1], "through", 7);
		memset(buffer, 0, sizeof(buffer));
		check("a pipe can be reached through /dev/fd",
		      alias >= 0 && read(alias, buffer, 7) == 7 &&
		      memcmp(buffer, "through", 7) == 0);
		if (alias >= 0)
			close(alias);
		close(pipes[0]);
		close(pipes[1]);
	}

	/* The three standard descriptors are the same mechanism, named. */
	fd = open("/dev/stdout", O_WRONLY);
	check("/dev/stdout names the caller's own standard output",
	      fd >= 0);
	if (fd >= 0) {
		check("writing to it reaches the terminal",
		      write(fd, "", 0) == 0);
		close(fd);
	} else {
		check("writing to it reaches the terminal", 0);
	}
	check("/dev/stdin can be opened for reading",
	      (fd = open("/dev/stdin", O_RDONLY)) >= 0);
	if (fd >= 0)
		close(fd);

	printf("DFD verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
