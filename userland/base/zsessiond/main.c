/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zsessiond, the graphical login's session manager
 * (plan/ws035/login-manager-design.md).
 *
 * init starts it as the service "greeter" in place of the console's getty.
 * It is small and runs as root: it gives the display and the input devices
 * to the seat's user, starts the greeter (zdesktop --greeter) as the
 * unprivileged _greeter account, checks the passwords the greeter sends it,
 * starts the user's session (/etc/zdesktop/session) as the user, and starts
 * the greeter again when the session ends.  It draws nothing and reads no
 * image or font.
 *
 * It keeps to the console when the boot parameters do not ask for the
 * graphical login (login=graphical), when there is no greeter or no display,
 * and after the greeter has failed three times in a row: it exits 0, and
 * init starts the console's getty that the greeter service replaces.
 */

#include "zsessiond.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* How many greeters in a row may fail before zsessiond gives the console back. */
#define MAIN_GREETER_FAILURES	3

/* The log zsessiond writes its lines to. */
#define MAIN_LOG_PATH		"/var/log/zsessiond.log"

/* The boot parameter that chooses the graphical login, as the kernel reports it. */
#define MAIN_LOGIN_SYSCTL	"kern.boot.login"

/* Set by SIGTERM and SIGINT: zsessiond ends its greeter or session and stops. */
volatile sig_atomic_t zsessiond_stopping;

static int main_options(struct zsessiond *daemon, int count, char **arguments, int *graphical, int *console);
static int main_boot_graphical(void);
static int main_ready(struct zsessiond *daemon);
static void main_stop(int signal_number);
static void main_open_log(void);

/*
 * Runs the session manager until it is stopped or gives the console back.
 */
int
main(
	int count,
	char **arguments)
{
	struct zsessiond daemon;
	struct zsessiond_account account;
	enum zsessiond_greeter_end end;
	struct sigaction action;
	unsigned failures;
	int graphical;
	int console;
	uid_t uid;
	int error;
	int ready;

	/* Only root can give devices away and start sessions as other users. */
	uid = geteuid();
	if (uid != 0) {
		fprintf(stderr, "zsessiond: must be run as root\n");
		return 1;
	}

	/* Reads the options. */
	memset(&daemon, 0, sizeof(daemon));
	daemon.greeter = ZSESSIOND_GREETER;
	daemon.session = ZSESSIOND_SESSION;
	daemon.greeter_socket = -1;
	graphical = 0;
	console = 0;
	error = main_options(&daemon, count, arguments, &graphical, &console);
	if (error != 0) {
		fprintf(stderr, "usage: zsessiond [--graphical | --console] [--greeter=/path] [--session=/path]\n");
		return 2;
	}

	/* The log, and the security log for logins. */
	main_open_log();
	openlog("zsessiond", LOG_PID, LOG_AUTH);

	/* The boot parameters choose the console login unless an option says which. */
	if (!graphical && !console)
		graphical = main_boot_graphical();

	/* The console login: init starts the console's getty when this exits. */
	if (!graphical || console) {
		zsessiond_log("ZSESSIOND CONSOLE reason=boot-parameters");
		return 0;
	}

	/* Without a greeter or a display there is no graphical login either. */
	ready = main_ready(&daemon);
	if (!ready)
		return 0;

	/* SIGTERM and SIGINT end the greeter or the session and then zsessiond. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = main_stop;
	sigemptyset(&action.sa_mask);
	(void)sigaction(SIGTERM, &action, NULL);
	(void)sigaction(SIGINT, &action, NULL);

	/* A client of the session writing to a closed pipe must not stop zsessiond. */
	(void)signal(SIGPIPE, SIG_IGN);

	/* The greeter, then a session for each login, until stopped. */
	zsessiond_log("ZSESSIOND START greeter=%s session=%s pid=%ld", daemon.greeter, daemon.session, (long)getpid());
	failures = 0;
	while (!zsessiond_stopping) {
		end = zsessiond_greeter_run(&daemon, &account);

		/* zsessiond is being stopped. */
		if (end == ZSESSIOND_GREETER_STOP)
			break;

		/* A greeter that failed: after three in a row the console's login takes over. */
		if (end == ZSESSIOND_GREETER_FAILED) {
			failures++;
			if (failures >= MAIN_GREETER_FAILURES) {
				zsessiond_log("ZSESSIOND CONSOLE reason=greeter-failed failures=%u", failures);
				syslog(LOG_ERR, "the greeter failed %u times; the console login takes over", failures);
				zsessiond_seat_restore();
				return 0;
			}

			/* Another try after a moment. */
			sleep(1);
			continue;
		}

		/* A greeter that ended by itself is started again. */
		failures = 0;
		if (end == ZSESSIOND_GREETER_ENDED)
			continue;

		/* A login: the user's session (the greeter left on the screen goes once it is ready), and the greeter again when it ends. */
		(void)zsessiond_session_run(&daemon, &account);
		memset(&account, 0, sizeof(account));
	}

	/* Stopped: a greeter a Log Out left goes, and the devices go back to root. */
	zsessiond_greeter_finish(&daemon);
	zsessiond_seat_restore();
	zsessiond_log("ZSESSIOND STOP");

	/* Succeeded: zsessiond ended its greeter or session. */
	return 0;
}

/*
 * Writes one line to zsessiond's log with the time in front.
 */
void
zsessiond_log(
	const char *format,
	...)
{
	char text[ZSESSIOND_LINE_MAX];
	va_list arguments;

	/* Formats the line. */
	va_start(arguments, format);
	(void)vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);

	/* Writes it whole. */
	printf("%lld %s\n", (long long)time(NULL), text);
	fflush(stdout);
}

/*
 * Reads one line from a descriptor, a byte at a time (nothing after it is
 * taken), waiting at most timeout_ms in all.  Returns 1 with the line (its
 * line end removed; a line too long is cut), 0 when the time ran out, or
 * -1 at the end of the stream or on an error.
 */
int
zsessiond_read_line(
	int descriptor,
	char *line,
	size_t size,
	int timeout_ms)
{
	struct pollfd entry;
	long long deadline;
	long long left;
	size_t used;
	ssize_t count;
	char byte;
	int ready;

	/* Byte after byte until the line ends. */
	deadline = zsessiond_milliseconds() + timeout_ms;
	used = 0;
	for (;;) {
		/* The time left. */
		left = deadline - zsessiond_milliseconds();
		if (left <= 0)
			return 0;

		/* A byte, or the end of the wait. */
		entry.fd = descriptor;
		entry.events = POLLIN;
		entry.revents = 0;
		ready = poll(&entry, 1, (int)left);
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0)
			return -1;
		if (ready == 0)
			return 0;
		count = read(descriptor, &byte, 1U);
		if (count < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (count <= 0)
			return -1;

		/* The line ends, or grows while there is room. */
		if (byte == '\n')
			break;
		if (used + 1U < size)
			line[used++] = byte;
	}

	/* Succeeded: a whole line. */
	line[used] = '\0';
	return 1;
}

/* Returns a monotonic time in milliseconds (0 when the clock cannot be read). */
long long
zsessiond_milliseconds(
	void)
{
	struct timespec now;
	int error;

	/* The monotonic clock. */
	error = clock_gettime(CLOCK_MONOTONIC, &now);
	if (error != 0)
		return 0;

	/* Succeeded. */
	return (long long)now.tv_sec * 1000LL + (long long)(now.tv_nsec / 1000000L);
}

/* Reads the options: which greeter and session, and whether to go graphical regardless of the boot parameters. */
static int
main_options(
	struct zsessiond *daemon,
	int count,
	char **arguments,
	int *graphical,
	int *console)
{
	const char *argument;
	int index;
	int match;

	/* Each option on its own. */
	for (index = 1; index < count; index++) {
		argument = arguments[index];

		/* The graphical login, whatever the boot parameters say (tests). */
		match = strcmp(argument, "--graphical");
		if (match == 0) {
			*graphical = 1;
			continue;
		}

		/* The console login, whatever the boot parameters say. */
		match = strcmp(argument, "--console");
		if (match == 0) {
			*console = 1;
			continue;
		}

		/* Another greeter program (an absolute path). */
		match = strncmp(argument, "--greeter=", 10);
		if (match == 0 && argument[10] == '/') {
			daemon->greeter = argument + 10;
			continue;
		}

		/* Another session script (an absolute path). */
		match = strncmp(argument, "--session=", 10);
		if (match == 0 && argument[10] == '/') {
			daemon->session = argument + 10;
			continue;
		}

		/* Anything else is a mistake. */
		return EINVAL;
	}

	/* Succeeded: every option is understood. */
	return 0;
}

/* Reports whether the boot parameters ask for the graphical login (login=graphical). */
static int
main_boot_graphical(
	void)
{
	char value[32];
	size_t size;
	int error;
	int equal;

	/* Asks the kernel for the login= boot parameter. */
	memset(value, 0, sizeof(value));
	size = sizeof(value) - 1U;
	error = sysctlbyname(MAIN_LOGIN_SYSCTL, value, &size, NULL, 0);
	if (error != 0)
		return 0;

	/* Only login=graphical asks for it; login=console and no parameter do not. */
	equal = strcmp(value, "graphical");
	if (equal != 0)
		return 0;

	/* Succeeded: the boot parameters ask for the graphical login. */
	return 1;
}

/* Reports whether a graphical login can start: the greeter is there, and so is the display. */
static int
main_ready(
	struct zsessiond *daemon)
{
	struct stat status;
	int error;

	/* The greeter program. */
	error = access(daemon->greeter, X_OK);
	if (error != 0) {
		zsessiond_log("ZSESSIOND CONSOLE reason=no-greeter path=%s errno=%d", daemon->greeter, errno);
		return 0;
	}

	/* The display. */
	error = stat("/dev/gpu0", &status);
	if (error != 0) {
		zsessiond_log("ZSESSIOND CONSOLE reason=no-display errno=%d", errno);
		return 0;
	}

	/* Succeeded: the greeter can start. */
	return 1;
}

/* Asks the main loop to end the greeter or the session and stop. */
static void
main_stop(
	int signal_number)
{
	(void)signal_number;

	/* The loops see this between their steps. */
	zsessiond_stopping = 1;
}

/* Sends zsessiond's output to its log (the console has no one reading it). */
static void
main_open_log(
	void)
{
	int descriptor;

	/* The log's directory may not be there yet on a new root. */
	(void)mkdir("/var/log", 0755);

	/* Appends to the log; without one, output stays where it was. */
	descriptor = open(MAIN_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
	if (descriptor < 0)
		return;

	/* Standard output and error go to it; the original descriptor is not kept. */
	(void)dup2(descriptor, STDOUT_FILENO);
	(void)dup2(descriptor, STDERR_FILENO);
	(void)close(descriptor);
	setvbuf(stdout, NULL, _IOLBF, 0);
}
