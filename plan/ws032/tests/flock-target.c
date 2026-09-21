/* WS032: flock(2) and setproctitle(3) on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <uapi/system.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PATH "/tmp/flock-target.tmp"

static int failures;

static void check(const char *what, int ok)
{
	printf("LCK %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* Reads this process's title back out of the process table, as ps does. */
static int own_title(char *out, size_t size)
{
	struct process_info info;
	int fd, found = -1;
	pid_t self = getpid();
	int32_t cursor = -1;

	fd = open("/dev/system", O_RDONLY);
	if (fd < 0)
		return -1;
	for (;;) {
		memset(&info, 0, sizeof(info));
		info.pid = cursor;
		if (ioctl(fd, KERN_SYSTEM_GET_PROCESS, &info) != 0)
			break;
		if (info.pid == (int32_t)self) {
			info.command[sizeof(info.command) - 1] = '\0';
			snprintf(out, size, "%s", info.command);
			found = 0;
			break;
		}
		cursor = info.pid;
	}
	close(fd);
	return found;
}

/* Records that the holder is about to release, for the waiter to confirm. */
static void mark_released(const char *path)
{
	int fd = open(path, O_WRONLY);

	if (fd >= 0) {
		(void)write(fd, "R", 1);
		close(fd);
	}
}

/* Reports whether the release had already been recorded. */
static int waited_flag(const char *path)
{
	char byte = 0;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return 0;
	if (read(fd, &byte, 1) != 1)
		byte = 0;
	close(fd);
	return byte == 'R';
}

int main(void)
{
	char title[128], original[128];
	struct flock lock;
	int a, b, c;
	pid_t child;
	int status;

	(void)unlink(PATH);
	a = open(PATH, O_RDWR | O_CREAT, 0600);
	if (a < 0) {
		printf("LCK FAIL cannot create %s\n", PATH);
		return 1;
	}

	check("exclusive lock is taken", flock(a, LOCK_EX | LOCK_NB) == 0);

	/*
	 * A second open of the same file is a second description, so its
	 * lock is independent and must collide.  A process-owned record lock
	 * would have granted this, which is the whole difference.
	 */
	b = open(PATH, O_RDWR);
	errno = 0;
	check("a second open of the same file collides",
	      b >= 0 && flock(b, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);

	/* A duplicate shares the one lock, so unlocking through it releases it. */
	c = dup(a);
	check("a duplicate shares the lock, not a second one",
	      c >= 0 && flock(c, LOCK_EX | LOCK_NB) == 0);
	check("unlocking through the duplicate releases the original",
	      flock(c, LOCK_UN) == 0 &&
	      flock(b, LOCK_EX | LOCK_NB) == 0 && flock(b, LOCK_UN) == 0);
	close(c);

	/* Two shared holders coexist; an upgrade under them does not. */
	check("two shared locks coexist",
	      flock(a, LOCK_SH | LOCK_NB) == 0 && flock(b, LOCK_SH | LOCK_NB) == 0);
	errno = 0;
	check("an upgrade under another shared holder is refused",
	      flock(a, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
	check("the upgrade succeeds once the other holder leaves",
	      flock(b, LOCK_UN) == 0 && flock(a, LOCK_EX | LOCK_NB) == 0);

	/* Closing the last descriptor of a description drops its lock. */
	close(a);
	check("closing the last descriptor releases the lock",
	      flock(b, LOCK_EX | LOCK_NB) == 0);
	close(b);

	/* A read-only descriptor may still hold the file exclusively. */
	a = open(PATH, O_RDONLY);
	check("a read-only descriptor may lock exclusively",
	      a >= 0 && flock(a, LOCK_EX | LOCK_NB) == 0);

	/* An inherited descriptor is the same description, so it is no conflict. */
	child = fork();
	if (child == 0)
		_exit(flock(a, LOCK_EX | LOCK_NB) == 0 ? 0 : 1);
	check("an inherited descriptor holds the same lock",
	      child > 0 && waitpid(child, &status, 0) == child &&
	      WIFEXITED(status) && WEXITSTATUS(status) == 0);
	close(a);

	/*
	 * Waiting is what the call is for: the child must sleep on the lock
	 * this process holds and take it only once it is released.
	 */
	a = open(PATH, O_RDWR);
	if (a >= 0 && flock(a, LOCK_EX | LOCK_NB) == 0) {
		child = fork();
		if (child == 0) {
			/*
			 * Its own open, because the inherited descriptor is
			 * the same description and would never contend.
			 */
			int own = open(PATH, O_RDWR);

			if (own < 0 || flock(own, LOCK_EX) != 0)
				_exit(1);

			/* Proves the wait ended after the release, not before. */
			_exit(waited_flag(PATH) ? 0 : 2);
		}
		sleep(1);
		mark_released(PATH);
		check("a waiting lock is granted after the release",
		      flock(a, LOCK_UN) == 0 &&
		      waitpid(child, &status, 0) == child &&
		      WIFEXITED(status) && WEXITSTATUS(status) == 0);
	} else {
		check("a waiting lock is granted after the release", 0);
	}
	close(a);

	errno = 0;
	a = open(PATH, O_RDWR);
	check("an operation naming no action is refused",
	      a >= 0 && flock(a, LOCK_NB) == -1 && errno == EINVAL);
	errno = 0;
	check("two actions at once are refused",
	      flock(a, LOCK_SH | LOCK_EX) == -1 && errno == EINVAL);
	close(a);
	(void)unlink(PATH);

	/*
	 * fcntl record locks share the machinery flock was built on, so the
	 * rules that separate them are checked here too: an owner that is
	 * the process rather than the description, and the access-mode and
	 * file-type rules flock is exempt from.
	 */
	a = open(PATH, O_RDWR | O_CREAT, 0600);
	b = open(PATH, O_RDWR);
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	check("fcntl: a process-owned lock is taken",
	      a >= 0 && b >= 0 && fcntl(a, F_SETLK, &lock) == 0);
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	check("fcntl: the same process does not collide with itself",
	      fcntl(b, F_SETLK, &lock) == 0);
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	(void)fcntl(a, F_SETLK, &lock);
	(void)fcntl(b, F_SETLK, &lock);

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	errno = 0;
	check("fcntl: an open-file-owned lock does collide",
	      fcntl(a, F_OFD_SETLK, &lock) == 0 &&
	      fcntl(b, F_OFD_SETLK, &lock) == -1 && errno == EAGAIN);
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	(void)fcntl(a, F_OFD_SETLK, &lock);
	close(a);
	close(b);

	a = open(PATH, O_RDONLY);
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	errno = 0;
	check("fcntl: a write lock still needs write access",
	      a >= 0 && fcntl(a, F_SETLK, &lock) == -1 && errno == EBADF);
	close(a);
	(void)unlink(PATH);

	/* setproctitle. */
	if (own_title(original, sizeof(original)) != 0) {
		printf("LCK FAIL cannot read own title\n");
		failures++;
	} else {
		setproctitle("holding %d lock%s", 2, "s");
		check("the title becomes progname plus the text",
		      own_title(title, sizeof(title)) == 0 &&
		      strcmp(title, "flocktest: holding 2 locks") == 0);
		setproctitle(NULL);
		check("a null format restores the original title",
		      own_title(title, sizeof(title)) == 0 &&
		      strcmp(title, original) == 0);
	}

	printf("LCK verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
