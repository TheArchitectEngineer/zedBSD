/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The graphical login's session manager (plan/ws035/login-manager-design.md).
 */

#ifndef ZSESSIOND_H
#define ZSESSIOND_H

#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

/* The greeter program and the session script zsessiond starts by default. */
#define ZSESSIOND_GREETER	"/bin/zdesktop"
#define ZSESSIOND_SESSION	"/etc/zdesktop/session"

/* The unprivileged account the greeter runs as. */
#define ZSESSIOND_GREETER_USER	"_greeter"

/* The wallpaper the greeter shows when the image has one. */
#define ZSESSIOND_WALLPAPER	"/usr/share/zdesktop/wallpaper.ppm"

/* The descriptor the greeter talks to zsessiond on, and the one the session does (ws035-p101). */
#define ZSESSIOND_AUTH_FD	3
#define ZSESSIOND_CONTROL_FD	3

/* The longest line of the greeter's protocol, and the longest user name. */
#define ZSESSIOND_LINE_MAX	512U
#define ZSESSIOND_NAME_MAX	64U

/* The size of the buffer an account's strings are kept in. */
#define ZSESSIOND_ACCOUNT_BUFFER	2048U

/*
 * How a greeter ended: a user logged in, it failed, it ended by itself, or
 * zsessiond is being stopped.
 */
enum zsessiond_greeter_end {
	ZSESSIOND_GREETER_LOGIN,
	ZSESSIOND_GREETER_FAILED,
	ZSESSIOND_GREETER_ENDED,
	ZSESSIOND_GREETER_STOP
};

/*
 * One seat's user: who owns the display and the input devices, and the
 * buffer passwd's strings for the account live in.
 */
struct zsessiond_account {
	struct passwd passwd;
	char buffer[ZSESSIOND_ACCOUNT_BUFFER];
};

/*
 * What zsessiond was started with: the greeter program and the session
 * script.  A greeter that outlives the step that started it (ws035-p101:
 * after a login it stays on the screen until the session is ready for the
 * display; at a Log Out it starts while the session still shows): its
 * process (0 for none), zsessiond's end of its socket, and when it started.
 */
struct zsessiond {
	const char *greeter;
	const char *session;
	pid_t greeter_pid;
	int greeter_socket;
	time_t greeter_started;
};

/* Set by SIGTERM and SIGINT: zsessiond ends its greeter or session and stops. */
extern volatile sig_atomic_t zsessiond_stopping;

enum zsessiond_greeter_end zsessiond_greeter_run(struct zsessiond *daemon, struct zsessiond_account *account);
void zsessiond_greeter_finish(struct zsessiond *daemon);
void zsessiond_greeter_release(struct zsessiond *daemon);
int zsessiond_greeter_prepare(struct zsessiond *daemon);
void zsessiond_greeter_go(struct zsessiond *daemon);
int zsessiond_read_line(int descriptor, char *line, size_t size, int timeout_ms);
long long zsessiond_milliseconds(void);
int zsessiond_session_run(struct zsessiond *daemon, struct zsessiond_account *account);
void zsessiond_seat_give(uid_t uid, gid_t gid);
void zsessiond_seat_restore(void);
void zsessiond_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
