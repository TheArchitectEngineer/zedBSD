/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The greeter: zsessiond starts it as _greeter with one end of a socket
 * pair as descriptor 3, and answers its requests, one line each:
 *
 *   READY                  the greeter is about to take the display; GO
 *                          (ws035-p101: nothing else holds it then)
 *   AUTH name password     checks the password; OK or FAIL, after a delay
 *                          that grows with the failures in a row
 *   POWER poweroff|reboot  ends the machine; OK
 *
 * Anything else is answered ERROR.  The password is erased as soon as it is
 * checked, and never written anywhere.
 *
 * After OK the greeter stays on the screen ("Starting session") while the
 * session starts; once the session is ready to take the display zsessiond
 * shuts its side of the socket down, the greeter gives the display back,
 * says RELEASED and ends (ws035-p101), so the text console never shows
 * between the two.  At a Log Out a greeter is started while the session
 * still shows; its READY is answered only once the session has given the
 * display back (session.c).
 */

#include "zsessiond.h"
#include "../login/verify.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* The greeter's output. */
#define GREETER_LOG_PATH	"/var/log/greeter.log"

/* A greeter that ends with an error within this many seconds of its start has failed. */
#define GREETER_QUICK_SECONDS	20

/* How long a greeter may take to end after a login before it is killed. */
#define GREETER_END_SECONDS	10

/* How long a greeter may take to say READY, and to give the display back (milliseconds). */
#define GREETER_READY_MS	30000
#define GREETER_RELEASE_MS	5000

/* The delay after a wrong password, and the longest it grows to. */
#define GREETER_DELAY_SECONDS	2U
#define GREETER_DELAY_MAX	16U

/*
 * One running greeter: its process, zsessiond's end of the socket, and the
 * request that has come in so far.
 */
struct greeter {
	pid_t pid;
	int socket;
	time_t started;
	char line[ZSESSIOND_LINE_MAX];
	size_t used;
	unsigned wrong;
	int logged_in;
};

static int greeter_account_find(struct zsessiond_account *greeter_account);
static int greeter_start(struct zsessiond *daemon, struct greeter *greeter, struct zsessiond_account *greeter_account);
static void greeter_child(struct zsessiond *daemon, int socket, struct zsessiond_account *greeter_account);
static int greeter_read(struct greeter *greeter, struct zsessiond_account *account);
static void greeter_request(struct greeter *greeter, char *line, struct zsessiond_account *account);
static void greeter_auth(struct greeter *greeter, char *arguments, struct zsessiond_account *account);
static void greeter_power(struct greeter *greeter, const char *what);
static void greeter_reply(struct greeter *greeter, const char *reply);
static enum zsessiond_greeter_end greeter_wait(struct greeter *greeter);
static void greeter_kill(struct greeter *greeter);

/*
 * Runs one greeter until a user logs in, it ends, or zsessiond stops.
 *
 * On ZSESSIOND_GREETER_LOGIN account holds the user who logged in.
 */
enum zsessiond_greeter_end
zsessiond_greeter_run(
	struct zsessiond *daemon,
	struct zsessiond_account *account)
{
	struct zsessiond_account greeter_account;
	struct greeter greeter;
	struct pollfd poll_entry;
	enum zsessiond_greeter_end end;
	int ready;
	int error;
	int closed;

	/* The greeter a Log Out started, or a new one. */
	memset(&greeter, 0, sizeof(greeter));
	greeter.socket = -1;
	if (daemon->greeter_pid > 0) {
		greeter.pid = daemon->greeter_pid;
		greeter.socket = daemon->greeter_socket;
		greeter.started = daemon->greeter_started;
		daemon->greeter_pid = 0;
		daemon->greeter_socket = -1;
		error = greeter_account_find(&greeter_account);
		zsessiond_log("ZSESSIOND GREETER adopt pid=%ld", (long)greeter.pid);
	} else {
		error = greeter_start(daemon, &greeter, &greeter_account);
	}

	/* Without its account the greeter cannot be looked after. */
	if (error != 0)
		return ZSESSIOND_GREETER_FAILED;

	/* Its requests, until it logs a user in or goes. */
	closed = 0;
	while (!closed) {
		/* zsessiond is being stopped: the greeter goes first. */
		if (zsessiond_stopping) {
			greeter_kill(&greeter);
			return ZSESSIOND_GREETER_STOP;
		}

		/* A request, or a second to give a new input device to the greeter. */
		poll_entry.fd = greeter.socket;
		poll_entry.events = POLLIN;
		poll_entry.revents = 0;
		ready = poll(&poll_entry, 1, 1000);
		if (ready == 0) {
			zsessiond_seat_give(greeter_account.passwd.pw_uid, greeter_account.passwd.pw_gid);
			continue;
		}

		/* An interrupted wait is waited again. */
		if (ready < 0)
			continue;

		/* Reads what came; the greeter closing its end is its end. */
		closed = greeter_read(&greeter, account);
		if (greeter.logged_in)
			break;
	}

	/* After a login the greeter stays on the screen until the session is ready for the display. */
	if (greeter.logged_in && !zsessiond_stopping) {
		daemon->greeter_pid = greeter.pid;
		daemon->greeter_socket = greeter.socket;
		daemon->greeter_started = greeter.started;
		memset(&greeter_account, 0, sizeof(greeter_account));
		zsessiond_log("ZSESSIOND GREETER stays pid=%ld", (long)greeter.pid);
		return ZSESSIOND_GREETER_LOGIN;
	}

	/* The greeter ends: after a login it ends itself, otherwise it has already. */
	end = greeter_wait(&greeter);
	(void)close(greeter.socket);
	memset(&greeter_account, 0, sizeof(greeter_account));

	/* Succeeded: says how the greeter ended. */
	return end;
}

/*
 * Ends the greeter left on the screen after a login: its socket is closed,
 * which it takes as the sign to end; one that does not end in time is
 * ended.  Nothing happens when no greeter was left.
 */
void
zsessiond_greeter_finish(
	struct zsessiond *daemon)
{
	struct greeter greeter;

	/* Only a greeter left on the screen. */
	if (daemon->greeter_pid <= 0)
		return;

	/* The socket closes, then the greeter goes. */
	memset(&greeter, 0, sizeof(greeter));
	greeter.pid = daemon->greeter_pid;
	greeter.socket = daemon->greeter_socket;
	greeter.logged_in = 1;
	(void)close(greeter.socket);
	(void)greeter_wait(&greeter);

	/* Succeeded: no greeter is left. */
	daemon->greeter_pid = 0;
	daemon->greeter_socket = -1;
}

/*
 * Has the greeter left on the screen give the display back ahead of its
 * end: zsessiond's side of the socket is shut down, which the greeter takes
 * as the sign to go, and its RELEASED (or its end) is waited for, at most
 * GREETER_RELEASE_MS.  The process is reaped by zsessiond_greeter_finish.
 */
void
zsessiond_greeter_release(
	struct zsessiond *daemon)
{
	char line[ZSESSIOND_LINE_MAX];
	int released;
	int got;
	int match;

	/* Only a greeter left on the screen. */
	if (daemon->greeter_pid <= 0)
		return;

	/* The sign to go, then RELEASED. */
	(void)shutdown(daemon->greeter_socket, SHUT_WR);
	released = 0;
	while (!released) {
		got = zsessiond_read_line(daemon->greeter_socket, line, sizeof(line), GREETER_RELEASE_MS);
		if (got <= 0)
			break;
		match = strcmp(line, "RELEASED");
		if (match == 0)
			released = 1;
	}

	/* Succeeded: the display is free (or the greeter has gone, or was given its time). */
	zsessiond_log("ZSESSIOND HANDOFF greeter released=%d at_ms=%lld", released, zsessiond_milliseconds());
}

/*
 * Starts a greeter while the session still shows (a Log Out, ws035-p101),
 * the seat given to it, and waits for its READY, which is not answered
 * yet (zsessiond_greeter_go does).  The greeter is left for
 * zsessiond_greeter_run to adopt.  Returns 0 once it said READY, or an
 * errno value.
 */
int
zsessiond_greeter_prepare(
	struct zsessiond *daemon)
{
	struct zsessiond_account greeter_account;
	struct greeter greeter;
	char line[ZSESSIOND_LINE_MAX];
	int error;
	int got;
	int match;

	/* The greeter, as at any start. */
	memset(&greeter, 0, sizeof(greeter));
	greeter.socket = -1;
	error = greeter_start(daemon, &greeter, &greeter_account);
	memset(&greeter_account, 0, sizeof(greeter_account));
	if (error != 0)
		return error;

	/* Left for zsessiond_greeter_run. */
	daemon->greeter_pid = greeter.pid;
	daemon->greeter_socket = greeter.socket;
	daemon->greeter_started = greeter.started;

	/* Its READY (it says nothing before it). */
	for (;;) {
		got = zsessiond_read_line(greeter.socket, line, sizeof(line), GREETER_READY_MS);
		if (got <= 0) {
			zsessiond_log("ZSESSIOND HANDOFF greeter not ready");
			return ETIMEDOUT;
		}

		/* Only READY ends the wait. */
		match = strcmp(line, "READY");
		if (match == 0)
			break;
	}

	/* Succeeded: the greeter waits for GO. */
	zsessiond_log("ZSESSIOND HANDOFF greeter ready: waits at_ms=%lld", zsessiond_milliseconds());
	return 0;
}

/* Tells the greeter a Log Out started that the display is free. */
void
zsessiond_greeter_go(
	struct zsessiond *daemon)
{
	ssize_t written;

	/* Only a greeter left for zsessiond_greeter_run. */
	if (daemon->greeter_pid <= 0)
		return;

	/* GO. */
	written = write(daemon->greeter_socket, "GO\n", 3U);
	zsessiond_log("ZSESSIOND HANDOFF greeter go written=%ld at_ms=%lld", (long)written, zsessiond_milliseconds());
}

/* Looks up the greeter's account.  Returns 0, or ENOENT. */
static int
greeter_account_find(
	struct zsessiond_account *greeter_account)
{
	struct passwd *found;
	int error;

	/* The account by its name. */
	found = NULL;
	error = getpwnam_r(ZSESSIOND_GREETER_USER, &greeter_account->passwd, greeter_account->buffer, sizeof(greeter_account->buffer), &found);
	if (error != 0 || found == NULL) {
		zsessiond_log("ZSESSIOND GREETER no account=%s", ZSESSIOND_GREETER_USER);
		return ENOENT;
	}

	/* Succeeded. */
	return 0;
}

/* Starts the greeter as _greeter with the display and the input devices given to it. */
static int
greeter_start(
	struct zsessiond *daemon,
	struct greeter *greeter,
	struct zsessiond_account *greeter_account)
{
	int pair[2];
	int error;

	/* The greeter's account. */
	error = greeter_account_find(greeter_account);
	if (error != 0)
		return error;

	/* The seat is the greeter's while it runs. */
	zsessiond_seat_give(greeter_account->passwd.pw_uid, greeter_account->passwd.pw_gid);

	/* The socket the greeter asks on. */
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
	if (error != 0) {
		zsessiond_log("ZSESSIOND GREETER socketpair errno=%d", errno);
		return errno;
	}

	/* The greeter's process. */
	greeter->pid = fork();
	if (greeter->pid < 0) {
		zsessiond_log("ZSESSIOND GREETER fork errno=%d", errno);
		(void)close(pair[0]);
		(void)close(pair[1]);
		return EAGAIN;
	}

	/* The child becomes the greeter and does not come back. */
	if (greeter->pid == 0) {
		(void)close(pair[0]);
		greeter_child(daemon, pair[1], greeter_account);
	}

	/* zsessiond keeps its own end. */
	(void)close(pair[1]);
	greeter->socket = pair[0];
	(void)fcntl(greeter->socket, F_SETFD, FD_CLOEXEC);
	greeter->started = time(NULL);

	/* Succeeded: the greeter is starting. */
	zsessiond_log("ZSESSIOND GREETER start pid=%ld uid=%u", (long)greeter->pid, (unsigned)greeter_account->passwd.pw_uid);
	return 0;
}

/* Becomes the greeter: its descriptors, its account, its environment, then the program. */
static void
greeter_child(
	struct zsessiond *daemon,
	int socket,
	struct zsessiond_account *greeter_account)
{
	char *arguments[5];
	char *environment[5];
	char user[96];
	char wallpaper[160];
	struct stat status;
	int descriptor;
	int error;

	/* The socket as descriptor 3, the log as its output, nothing to read on standard input. */
	(void)dup2(socket, ZSESSIOND_AUTH_FD);
	descriptor = open("/dev/null", O_RDONLY);
	if (descriptor >= 0)
		(void)dup2(descriptor, STDIN_FILENO);
	descriptor = open(GREETER_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0640);
	if (descriptor >= 0) {
		(void)dup2(descriptor, STDOUT_FILENO);
		(void)dup2(descriptor, STDERR_FILENO);
	}

	/* No other descriptor of zsessiond's goes with it. */
	for (descriptor = ZSESSIOND_AUTH_FD + 1; descriptor < 256; descriptor++)
		(void)close(descriptor);

	/* Its own session, and its account's groups and ids (the last step as root). */
	(void)setsid();
	error = initgroups(greeter_account->passwd.pw_name, greeter_account->passwd.pw_gid);
	if (error != 0)
		_exit(126);
	error = setgid(greeter_account->passwd.pw_gid);
	if (error != 0)
		_exit(126);
	error = setuid(greeter_account->passwd.pw_uid);
	if (error != 0)
		_exit(126);
	(void)chdir("/");

	/* A small environment of its own. */
	snprintf(user, sizeof(user), "USER=%s", greeter_account->passwd.pw_name);
	environment[0] = user;
	environment[1] = "HOME=/var/empty";
	environment[2] = "PATH=/bin:/sbin:/usr/bin";
	environment[3] = NULL;

	/* The greeter's options: the wallpaper when the image has one. */
	arguments[0] = (char *)daemon->greeter;
	arguments[1] = "--greeter";
	arguments[2] = "--auth-fd=3";
	arguments[3] = NULL;
	error = stat(ZSESSIOND_WALLPAPER, &status);
	if (error == 0) {
		snprintf(wallpaper, sizeof(wallpaper), "--wallpaper=%s", ZSESSIOND_WALLPAPER);
		arguments[3] = wallpaper;
		arguments[4] = NULL;
	}

	/* Only a failed exec comes back. */
	(void)execve(daemon->greeter, arguments, environment);
	_exit(127);
}

/* Reads what the greeter sent and answers each whole line; reports 1 when the greeter has closed its end. */
static int
greeter_read(
	struct greeter *greeter,
	struct zsessiond_account *account)
{
	char *end;
	ssize_t count;
	size_t length;

	/* What came. */
	count = read(greeter->socket, greeter->line + greeter->used, sizeof(greeter->line) - 1U - greeter->used);
	if (count < 0 && errno == EINTR)
		return 0;
	if (count <= 0)
		return 1;
	greeter->used += (size_t)count;
	greeter->line[greeter->used] = '\0';

	/* Each whole line. */
	for (;;) {
		end = strchr(greeter->line, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		length = (size_t)(end - greeter->line) + 1U;
		greeter_request(greeter, greeter->line, account);

		/* The rest moves to the front, and what was read is erased (a password may be in it). */
		memmove(greeter->line, greeter->line + length, greeter->used - length);
		greeter->used -= length;
		memset(greeter->line + greeter->used, 0, sizeof(greeter->line) - greeter->used);
		if (greeter->logged_in)
			return 0;
	}

	/* A line longer than any request is thrown away. */
	if (greeter->used + 1U >= sizeof(greeter->line)) {
		memset(greeter->line, 0, sizeof(greeter->line));
		greeter->used = 0;
		greeter_reply(greeter, "ERROR");
	}

	/* Succeeded: the greeter is still there. */
	return 0;
}

/* Answers one request of the greeter. */
static void
greeter_request(
	struct greeter *greeter,
	char *line,
	struct zsessiond_account *account)
{
	int match;

	/* The greeter is about to take the display: nothing holds it while no session runs. */
	match = strcmp(line, "READY");
	if (match == 0) {
		zsessiond_log("ZSESSIOND HANDOFF greeter ready: go");
		greeter_reply(greeter, "GO");
		return;
	}

	/* A login. */
	match = strncmp(line, "AUTH ", 5);
	if (match == 0) {
		greeter_auth(greeter, line + 5, account);
		return;
	}

	/* The power button. */
	match = strncmp(line, "POWER ", 6);
	if (match == 0) {
		greeter_power(greeter, line + 6);
		return;
	}

	/* Anything else. */
	greeter_reply(greeter, "ERROR");
}

/* Checks a user's password; a right one logs the user in, a wrong one is answered after a delay. */
static void
greeter_auth(
	struct greeter *greeter,
	char *arguments,
	struct zsessiond_account *account)
{
	char name[ZSESSIOND_NAME_MAX];
	char *password;
	unsigned delay;
	unsigned doublings;
	size_t length;
	int verified;

	/* The name, up to the first space, and the password after it (which may have spaces). */
	password = strchr(arguments, ' ');
	if (password == NULL) {
		greeter_reply(greeter, "ERROR");
		return;
	}

	/* A name that is empty or too long is refused. */
	length = (size_t)(password - arguments);
	password++;
	if (length == 0 || length >= sizeof(name)) {
		memset(password, 0, strlen(password));
		greeter_reply(greeter, "ERROR");
		return;
	}

	/* The name on its own. */
	memcpy(name, arguments, length);
	name[length] = '\0';

	/* The check, which erases the password (login's own, verify.c). */
	memset(account, 0, sizeof(*account));
	verified = login_verify(name, password, &account->passwd, account->buffer, sizeof(account->buffer));
	if (verified == 0) {
		syslog(LOG_NOTICE, "login %s on the graphical seat", name);
		zsessiond_log("ZSESSIOND AUTH ok user=%s uid=%u", name, (unsigned)account->passwd.pw_uid);
		greeter->logged_in = 1;
		greeter->wrong = 0;
		greeter_reply(greeter, "OK");
		return;
	}

	/* A wrong password: 2 seconds, twice as long after every three in a row, at most 16. */
	greeter->wrong++;
	doublings = (greeter->wrong - 1U) / 3U;
	delay = GREETER_DELAY_SECONDS;
	while (doublings > 0U && delay < GREETER_DELAY_MAX) {
		delay *= 2U;
		doublings--;
	}

	/* The failure goes on record, and the answer waits out the delay. */
	syslog(LOG_WARNING, "failed login %s on the graphical seat (%u in a row)", name, greeter->wrong);
	zsessiond_log("ZSESSIOND AUTH fail user=%s wrong=%u delay=%u", name, greeter->wrong, delay);
	memset(account, 0, sizeof(*account));
	sleep(delay);
	greeter_reply(greeter, "FAIL");
}

/* Ends the machine the way the greeter's power button asks. */
static void
greeter_power(
	struct greeter *greeter,
	const char *what)
{
	const char *program;
	pid_t child;
	int match;

	/* Which program ends the machine that way. */
	program = NULL;
	match = strcmp(what, "poweroff");
	if (match == 0)
		program = "/sbin/poweroff";
	match = strcmp(what, "reboot");
	if (match == 0)
		program = "/sbin/reboot";
	if (program == NULL) {
		greeter_reply(greeter, "ERROR");
		return;
	}

	/* Runs it; it asks init, which stops zsessiond among the rest. */
	zsessiond_log("ZSESSIOND POWER %s", what);
	syslog(LOG_NOTICE, "%s from the graphical login", what);
	child = fork();
	if (child == 0) {
		(void)execl(program, program, (char *)NULL);
		_exit(127);
	}

	/* The greeter hears that the machine is ending. */
	greeter_reply(greeter, "OK");
}

/* Sends one reply line to the greeter. */
static void
greeter_reply(
	struct greeter *greeter,
	const char *reply)
{
	char line[16];
	size_t length;
	ssize_t written;

	/* The reply and its line end, in one write. */
	snprintf(line, sizeof(line), "%s\n", reply);
	length = strlen(line);
	written = write(greeter->socket, line, length);
	if (written != (ssize_t)length)
		zsessiond_log("ZSESSIOND GREETER reply lost");
}

/* Waits for the greeter to end and says how it ended. */
static enum zsessiond_greeter_end
greeter_wait(
	struct greeter *greeter)
{
	time_t waited_from;
	time_t now;
	pid_t waited;
	int status;
	int exited;
	int code;
	int failed;

	/* A greeter that logged a user in ends by itself; one that does not in time is ended. */
	waited_from = time(NULL);
	for (;;) {
		waited = waitpid(greeter->pid, &status, WNOHANG);
		if (waited == greeter->pid)
			break;

		/* A greeter that is gone some other way has no status. */
		if (waited < 0 && errno != EINTR) {
			status = 0;
			break;
		}

		/* One that takes too long is ended. */
		now = time(NULL);
		if (now - waited_from >= GREETER_END_SECONDS) {
			greeter_kill(greeter);
			status = 0;
			break;
		}

		/* Looks again shortly. */
		usleep(50000);
	}

	/* After a login the session starts, however the greeter went. */
	zsessiond_log("ZSESSIOND GREETER end pid=%ld status=%d", (long)greeter->pid, status);
	if (greeter->logged_in)
		return ZSESSIOND_GREETER_LOGIN;

	/* A greeter that ended with an error soon after it started has failed. */
	now = time(NULL);
	exited = WIFEXITED(status);
	code = WEXITSTATUS(status);
	failed = 0;
	if (!exited)
		failed = 1;
	else if (code != 0)
		failed = 1;
	if (failed && now - greeter->started < GREETER_QUICK_SECONDS)
		return ZSESSIOND_GREETER_FAILED;

	/* Succeeded: the greeter ended by itself. */
	return ZSESSIOND_GREETER_ENDED;
}

/* Ends the greeter: TERM, then KILL when it does not go. */
static void
greeter_kill(
	struct greeter *greeter)
{
	pid_t waited;
	int status;
	int tries;

	/* Asks it to go. */
	(void)kill(greeter->pid, SIGTERM);

	/* Gives it three seconds. */
	for (tries = 0; tries < 60; tries++) {
		waited = waitpid(greeter->pid, &status, WNOHANG);
		if (waited == greeter->pid)
			return;
		usleep(50000);
	}

	/* Then makes it go. */
	(void)kill(greeter->pid, SIGKILL);
	(void)waitpid(greeter->pid, &status, 0);
}
