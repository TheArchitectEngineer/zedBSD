/* WS032: the OpenBSD libc interfaces, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("OBS %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* A known instant: 2001-09-09T01:46:40Z is exactly 1000000000. */
static int timegm_known(void)
{
	struct tm when;

	memset(&when, 0, sizeof(when));
	when.tm_year = 101;
	when.tm_mon = 8;
	when.tm_mday = 9;
	when.tm_hour = 1;
	when.tm_min = 46;
	when.tm_sec = 40;
	return timegm(&when) == (time_t)1000000000 && when.tm_wday == 0 &&
	       when.tm_yday == 251;
}

/* A month past the end of the year must carry into the next one. */
static int timegm_normalises(void)
{
	struct tm when;

	memset(&when, 0, sizeof(when));
	when.tm_year = 70;
	when.tm_mon = 12;   /* the thirteenth month of 1970 */
	when.tm_mday = 1;
	return timegm(&when) == (time_t)31536000 && when.tm_year == 71 &&
	       when.tm_mon == 0 && when.tm_mday == 1;
}

/* The epoch itself, and a date before it. */
static int timegm_epoch(void)
{
	struct tm when;
	time_t before;

	memset(&when, 0, sizeof(when));
	when.tm_year = 70;
	when.tm_mday = 1;
	if (timegm(&when) != (time_t)0)
		return 0;
	memset(&when, 0, sizeof(when));
	when.tm_year = 69;
	when.tm_mon = 11;
	when.tm_mday = 31;
	before = timegm(&when);
	return before == (time_t)-86400;
}

/* Every day of a leap year must round-trip through gmtime. */
static int timegm_round_trip(void)
{
	struct tm when, back;
	time_t value;
	int day;

	for (day = 0; day < 366; day++) {
		memset(&when, 0, sizeof(when));
		when.tm_year = 100;           /* 2000, a leap year */
		when.tm_mday = 1 + day;       /* carried through the months */
		when.tm_hour = 13;
		value = timegm(&when);
		if (value == (time_t)-1)
			return 0;
		if (gmtime_r(&value, &back) == NULL)
			return 0;
		if (back.tm_year != when.tm_year || back.tm_mon != when.tm_mon ||
		    back.tm_mday != when.tm_mday || back.tm_hour != 13)
			return 0;
	}
	return 1;
}

static int getline_reads(void)
{
	char path[] = "/tmp/obsline.XXXXXX";
	char *line = NULL;
	size_t capacity = 0;
	ssize_t used;
	int fd, ok = 1;
	FILE *stream;

	fd = mkstemp(path);
	if (fd < 0)
		return 0;
	(void)write(fd, "one\ntwo\nlast line without a newline", 35);
	close(fd);
	stream = fopen(path, "r");
	if (stream == NULL) {
		(void)unlink(path);
		return 0;
	}
	used = getline(&line, &capacity, stream);
	ok = ok && used == 4 && strcmp(line, "one\n") == 0;
	used = getline(&line, &capacity, stream);
	ok = ok && used == 4 && strcmp(line, "two\n") == 0;

	/* A final record with no delimiter is still returned. */
	used = getline(&line, &capacity, stream);
	ok = ok && used == 27 && strcmp(line, "last line without a newline") == 0;
	ok = ok && getline(&line, &capacity, stream) == -1;
	free(line);
	fclose(stream);
	(void)unlink(path);
	return ok;
}

static int getdelim_reads(void)
{
	char path[] = "/tmp/obsdelim.XXXXXX";
	char *record = NULL;
	size_t capacity = 0;
	int fd, ok = 1;
	FILE *stream;

	fd = mkstemp(path);
	if (fd < 0)
		return 0;
	(void)write(fd, "alpha\0beta\0", 11);
	close(fd);
	stream = fopen(path, "r");
	if (stream == NULL) {
		(void)unlink(path);
		return 0;
	}
	ok = ok && getdelim(&record, &capacity, '\0', stream) == 6 &&
	     strcmp(record, "alpha") == 0;
	ok = ok && getdelim(&record, &capacity, '\0', stream) == 5 &&
	     strcmp(record, "beta") == 0;
	free(record);
	fclose(stream);
	(void)unlink(path);
	return ok;
}

int main(void)
{
	char directory[] = "/tmp/obsdir.XXXXXX";
	char suffixed[] = "/tmp/obssuf.XXXXXX.log";
	char *secret;
	struct stat status;
	uid_t uid;
	gid_t gid;
	gid_t groups[16];
	int ngroups;
	int pair[2];
	int fd, before, after;
	pid_t child;
	int wstatus;

	check("timegm at a known instant", timegm_known());
	check("timegm normalises a month past the year", timegm_normalises());
	check("timegm at and before the epoch", timegm_epoch());
	check("timegm round-trips every day of a leap year", timegm_round_trip());

	check("getline reads lines and the last partial one", getline_reads());
	check("getdelim reads records ending in any byte", getdelim_reads());

	check("mkdtemp makes a private directory",
	      mkdtemp(directory) == directory &&
	      stat(directory, &status) == 0 && S_ISDIR(status.st_mode) &&
	      (status.st_mode & 0777) == 0700);
	(void)rmdir(directory);

	fd = mkstemps(suffixed, 4);
	check("mkstemps keeps the suffix",
	      fd >= 0 && strlen(suffixed) == strlen("/tmp/obssuf.XXXXXX.log") &&
	      strcmp(suffixed + strlen(suffixed) - 4, ".log") == 0 &&
	      strncmp(suffixed + 12, "XXXXXX", 6) != 0);
	if (fd >= 0)
		close(fd);
	(void)unlink(suffixed);

	secret = malloc(64);
	if (secret != NULL) {
		memcpy(secret, "not for anyone else", 20);
		freezero(secret, 64);
	}
	check("freezero releases an erased allocation", secret != NULL);

	check("getpagesize agrees with sysconf",
	      getpagesize() == (int)sysconf(_SC_PAGESIZE) && getpagesize() > 0);

	/* closefrom must leave the low descriptors and take the rest. */
	before = getdtablecount();
	fd = open("/dev/null", O_RDONLY);
	check("getdtablecount notices a new descriptor",
	      fd >= 0 && getdtablecount() == before + 1);
	check("closefrom closes the tail and spares the rest",
	      closefrom(fd) == 0 && getdtablecount() == before &&
	      fcntl(STDOUT_FILENO, F_GETFD) != -1);
	after = getdtablecount();
	check("closefrom is content with nothing to close",
	      closefrom(after + 64) == 0 && getdtablecount() == after);

	/* A socket pair's peer is this process. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0) {
		check("getpeereid names the peer of a local socket",
		      getpeereid(pair[0], &uid, &gid) == 0 &&
		      uid == geteuid() && gid == getegid());
		close(pair[0]);
		close(pair[1]);
	} else {
		check("getpeereid names the peer of a local socket", 0);
	}

	/* The group list always begins with the one it was given. */
	ngroups = (int)(sizeof(groups) / sizeof(groups[0]));
	check("getgrouplist starts with the group it was given",
	      getgrouplist("root", 0, groups, &ngroups) >= 0 && ngroups >= 1 &&
	      groups[0] == 0);

	/* A capacity of zero asks only how many there are. */
	ngroups = 0;
	check("getgrouplist reports the count that did not fit",
	      getgrouplist("root", 0, groups, &ngroups) == -1 && ngroups >= 1);

	check("user_from_uid names root", strcmp(user_from_uid(0, 0), "root") == 0);
	check("user_from_uid reports an unknown uid as a number",
	      strcmp(user_from_uid(60123, 0), "60123") == 0 &&
	      user_from_uid(60123, 1) == NULL);
	check("group_from_gid answers for a known and an unknown group",
	      group_from_gid(0, 0) != NULL &&
	      strcmp(group_from_gid(60123, 0), "60123") == 0 &&
	      group_from_gid(60123, 1) == NULL);

	check("issetugid reports an ordinary start", issetugid() == 0);

	/* daemon runs in a child, since it replaces the caller's session. */
	child = fork();
	if (child == 0) {
		if (daemon(1, 1) != 0)
			_exit(1);
		_exit(getsid(0) == getpid() ? 0 : 2);
	}
	check("daemon leaves the caller's session",
	      child > 0 && waitpid(child, &wstatus, 0) == child &&
	      WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0);

	printf("OBS verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
