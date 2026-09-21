/* WS032: the memory devices and the time types, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("DEV %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	struct timeval when;
	suseconds_t micro;
	char buffer[8];
	int fd;

	/* A position may be asked for, and is remembered. */
	fd = open("/dev/null", O_RDWR);
	check("/dev/null can be positioned",
	      fd >= 0 && lseek(fd, 0, SEEK_END) == 0);
	check("a position set is the position reported",
	      lseek(fd, 100, SEEK_SET) == 100 && lseek(fd, 0, SEEK_CUR) == 100);
	check("a position may be moved from where it is",
	      lseek(fd, 5, SEEK_CUR) == 105);
	check("a position before the start is refused",
	      lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL);

	/* The position changes nothing about what the device does. */
	check("reading still reports the end", read(fd, buffer, sizeof(buffer)) == 0);
	check("writing still accepts everything", write(fd, "xyz", 3) == 3);
	close(fd);

	fd = open("/dev/zero", O_RDONLY);
	memset(buffer, 0xff, sizeof(buffer));
	check("/dev/zero can be positioned and still reads as zeros",
	      fd >= 0 && lseek(fd, 4096, SEEK_SET) == 4096 &&
	      read(fd, buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer) &&
	      buffer[0] == 0 && buffer[7] == 0);
	close(fd);

	/* A terminal has no position, and says so. */
	errno = 0;
	check("a terminal refuses to be positioned",
	      lseek(STDIN_FILENO, 0, SEEK_SET) == -1 && errno == ESPIPE);

	/* The microseconds of a time of day have a type, and it is signed. */
	when.tv_sec = 1;
	when.tv_usec = -1;
	micro = when.tv_usec;
	check("suseconds_t names the microseconds and is signed",
	      micro < 0 && sizeof(micro) == sizeof(when.tv_usec));
	check("a time of day still measures one",
	      gettimeofday(&when, NULL) == 0 && when.tv_sec > 0 &&
	      when.tv_usec >= 0 && when.tv_usec < 1000000);

	printf("DEV verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
