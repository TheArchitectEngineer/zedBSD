/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The user's session: after a login zsessiond gives the seat to the user,
 * makes the user's runtime directory (/run/user/UID, 0700, the user's: the
 * Wayland socket goes there, where no other user can reach it), and runs
 * the session script (/etc/zdesktop/session) with /bin/sh as the user, the
 * way login starts a shell: initgroups, setgid, setuid, HOME, USER,
 * LOGNAME, PATH, SHELL and XDG_RUNTIME_DIR.  The login is recorded in
 * utmpx.
 *
 * The session ends when the script ends (zdesktop's Log Out).  Whatever of
 * it is left is ended too: its process group, and every process of the
 * user (not for root, whose processes are the system's).
 */

#include "zsessiond.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

/* Where the users' runtime directories are. */
#define SESSION_RUNTIME_ROOT	"/run/user"

/* The utmpx line of the graphical seat. */
#define SESSION_LINE		"seat0"

static int session_runtime(struct zsessiond_account *account, char *directory, size_t size);
static void session_runtime_clean(const char *directory);
static void session_child(struct zsessiond *daemon, struct zsessiond_account *account, const char *directory);
static void session_record(int type, pid_t pid, const char *user);
static void session_sweep(struct zsessiond_account *account, pid_t leader);
static void session_signal_user(uid_t uid, int signal_number);

/*
 * Runs a user's session until it ends or zsessiond stops.
 */
int
zsessiond_session_run(
	struct zsessiond *daemon,
	struct zsessiond_account *account)
{
	char directory[64];
	pid_t child;
	pid_t waited;
	int status;
	int error;

	/* The seat is the user's now. */
	zsessiond_seat_give(account->passwd.pw_uid, account->passwd.pw_gid);

	/* The user's runtime directory. */
	error = session_runtime(account, directory, sizeof(directory));
	if (error != 0)
		return error;

	/* The session's process. */
	child = fork();
	if (child < 0) {
		zsessiond_log("ZSESSIOND SESSION fork errno=%d", errno);
		return EAGAIN;
	}

	/* The child becomes the session and does not come back. */
	if (child == 0)
		session_child(daemon, account, directory);

	/* The login is on record while the session runs. */
	session_record(USER_PROCESS, child, account->passwd.pw_name);
	zsessiond_log("ZSESSIOND SESSION start user=%s uid=%u pid=%ld runtime=%s", account->passwd.pw_name, (unsigned)account->passwd.pw_uid, (long)child, directory);

	/* Waits for it to end, giving a new input device to the user every second. */
	status = 0;
	for (;;) {
		waited = waitpid(child, &status, WNOHANG);
		if (waited == child)
			break;
		if (waited < 0 && errno != EINTR)
			break;

		/* zsessiond is being stopped: the session goes first. */
		if (zsessiond_stopping) {
			(void)kill(-child, SIGTERM);
			(void)kill(child, SIGTERM);
		}

		/* A second, then any new input device is the user's too. */
		sleep(1);
		zsessiond_seat_give(account->passwd.pw_uid, account->passwd.pw_gid);
	}

	/* The session has ended: what is left of it goes, and the record says so. */
	zsessiond_log("ZSESSIOND SESSION end user=%s pid=%ld status=%d", account->passwd.pw_name, (long)child, status);
	session_sweep(account, child);
	session_record(DEAD_PROCESS, child, "");
	session_runtime_clean(directory);

	/* Succeeded: the session ran and ended. */
	return 0;
}

/* Makes the user's runtime directory: 0700, the user's, and a directory (not a link someone left). */
static int
session_runtime(
	struct zsessiond_account *account,
	char *directory,
	size_t size)
{
	struct stat status;
	int error;

	/* The root of the runtime directories, root's and readable by all. */
	(void)mkdir("/run", 0755);
	(void)mkdir(SESSION_RUNTIME_ROOT, 0755);

	/* The user's own. */
	snprintf(directory, size, "%s/%u", SESSION_RUNTIME_ROOT, (unsigned)account->passwd.pw_uid);
	error = mkdir(directory, 0700);
	if (error != 0 && errno != EEXIST) {
		zsessiond_log("ZSESSIOND SESSION runtime=%s errno=%d", directory, errno);
		return errno;
	}

	/* What is there must be a directory (lstat: not a link to somewhere else). */
	error = lstat(directory, &status);
	if (error != 0) {
		zsessiond_log("ZSESSIOND SESSION runtime=%s errno=%d", directory, errno);
		return errno;
	}

	/* Anything but a directory is refused. */
	if ((status.st_mode & S_IFMT) != S_IFDIR) {
		zsessiond_log("ZSESSIOND SESSION runtime=%s not a directory", directory);
		return ENOTDIR;
	}

	/* The user's, and no one else's. */
	error = chown(directory, account->passwd.pw_uid, account->passwd.pw_gid);
	if (error != 0)
		return errno;
	error = chmod(directory, 0700);
	if (error != 0)
		return errno;

	/* Succeeded: the directory is ready. */
	return 0;
}

/* Removes the session's Wayland socket from its runtime directory. */
static void
session_runtime_clean(
	const char *directory)
{
	char path[96];

	/* The socket and its lock; the log stays for the user to read. */
	snprintf(path, sizeof(path), "%s/wayland-0", directory);
	(void)unlink(path);
	snprintf(path, sizeof(path), "%s/wayland-0.lock", directory);
	(void)unlink(path);
}

/* Becomes the session: the user's ids, the user's environment, then the script. */
static void
session_child(
	struct zsessiond *daemon,
	struct zsessiond_account *account,
	const char *directory)
{
	char *arguments[3];
	char *environment[8];
	char home[320];
	char user[96];
	char logname[96];
	char runtime[96];
	char log_path[96];
	int descriptor;
	int error;

	/* Its own session, and the user's groups and ids (the last step as root). */
	(void)setsid();
	error = initgroups(account->passwd.pw_name, account->passwd.pw_gid);
	if (error != 0)
		_exit(126);
	error = setgid(account->passwd.pw_gid);
	if (error != 0)
		_exit(126);
	error = setuid(account->passwd.pw_uid);
	if (error != 0)
		_exit(126);

	/* Nothing to read, and the output in the runtime directory (opened as the user). */
	descriptor = open("/dev/null", O_RDONLY);
	if (descriptor >= 0)
		(void)dup2(descriptor, STDIN_FILENO);
	snprintf(log_path, sizeof(log_path), "%s/session.log", directory);
	descriptor = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor >= 0) {
		(void)dup2(descriptor, STDOUT_FILENO);
		(void)dup2(descriptor, STDERR_FILENO);
	}

	/* No descriptor of zsessiond's goes with it. */
	for (descriptor = 3; descriptor < 256; descriptor++)
		(void)close(descriptor);

	/* The home directory, or / when it cannot be entered. */
	error = chdir(account->passwd.pw_dir);
	if (error != 0)
		(void)chdir("/");

	/* The environment login gives a shell, and the runtime directory. */
	snprintf(home, sizeof(home), "HOME=%s", account->passwd.pw_dir);
	snprintf(user, sizeof(user), "USER=%s", account->passwd.pw_name);
	snprintf(logname, sizeof(logname), "LOGNAME=%s", account->passwd.pw_name);
	snprintf(runtime, sizeof(runtime), "XDG_RUNTIME_DIR=%s", directory);
	environment[0] = home;
	environment[1] = user;
	environment[2] = logname;
	environment[3] = "PATH=/bin:/sbin:/usr/bin";
	environment[4] = "SHELL=/bin/sh";
	environment[5] = runtime;
	environment[6] = NULL;

	/* The session script; only a failed exec comes back. */
	arguments[0] = "sh";
	arguments[1] = (char *)daemon->session;
	arguments[2] = NULL;
	(void)execve("/bin/sh", arguments, environment);
	_exit(127);
}

/* Writes the session's utmpx record: logged in, or ended. */
static void
session_record(
	int type,
	pid_t pid,
	const char *user)
{
	struct utmpx record;
	struct timespec now;
	int error;

	/* The seat's line, the session's process, and who. */
	memset(&record, 0, sizeof(record));
	record.ut_type = (short)type;
	record.ut_pid = pid;
	strncpy(record.ut_line, SESSION_LINE, sizeof(record.ut_line) - 1U);
	strncpy(record.ut_id, SESSION_LINE, sizeof(record.ut_id));
	strncpy(record.ut_user, user, sizeof(record.ut_user) - 1U);

	/* When. */
	error = clock_gettime(CLOCK_REALTIME, &now);
	if (error == 0) {
		record.ut_tv.tv_sec = now.tv_sec;
		record.ut_tv.tv_usec = (long)(now.tv_nsec / 1000L);
	}

	/* The record. */
	(void)pututxline(&record);
}

/* Ends what is left of a session: its process group, then every process of the user. */
static void
session_sweep(
	struct zsessiond_account *account,
	pid_t leader)
{
	/* The session's process group (the script's children that stayed in it). */
	(void)kill(-leader, SIGTERM);

	/* Root's processes are the system's: only an ordinary user's all go. */
	if (account->passwd.pw_uid == 0)
		return;

	/* The user's other processes (applications started in their own sessions), asked, then made to go. */
	session_signal_user(account->passwd.pw_uid, SIGTERM);
	sleep(2);
	session_signal_user(account->passwd.pw_uid, SIGKILL);
	(void)kill(-leader, SIGKILL);
}

/* Sends a signal to every process of a user, from a child that has become that user. */
static void
session_signal_user(
	uid_t uid,
	int signal_number)
{
	pid_t child;
	int status;
	int error;

	/* kill(-1) as the user reaches exactly the user's processes. */
	child = fork();
	if (child == 0) {
		error = setuid(uid);
		if (error != 0)
			_exit(126);
		(void)kill(-1, signal_number);
		_exit(0);
	}

	/* Waits for the child that sent it. */
	if (child > 0)
		(void)waitpid(child, &status, 0);
}
