/* WS032: /dev/full, ttyname and the session ioctls, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("SES %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	char buffer[64], zeros[16], name[64];
	int fd, master, slave;
	struct pollfd waiting;
	pid_t child;
	int status;
	ssize_t n;

	/* /dev/full reads as zeros and never accepts a byte. */
	fd = open("/dev/full", O_RDWR);
	memset(zeros, 0xff, sizeof(zeros));
	n = fd >= 0 ? read(fd, zeros, sizeof(zeros)) : -1;
	check("/dev/full reads as zeros",
	      n == (ssize_t)sizeof(zeros) && zeros[0] == 0 &&
	      zeros[sizeof(zeros) - 1] == 0);
	errno = 0;
	check("/dev/full refuses a write with ENOSPC",
	      write(fd, "x", 1) == -1 && errno == ENOSPC);
	check("/dev/full grants a write of nothing", write(fd, "", 0) == 0);
	if (fd >= 0)
		close(fd);

	/* ttyname must name the terminal a descriptor actually refers to. */
	check("ttyname names the console",
	      isatty(STDIN_FILENO) && ttyname(STDIN_FILENO) != NULL &&
	      strncmp(ttyname(STDIN_FILENO), "/dev/", 5) == 0);
	check("ttyname_r agrees with ttyname",
	      ttyname_r(STDIN_FILENO, name, sizeof(name)) == 0 &&
	      strcmp(name, ttyname(STDIN_FILENO)) == 0);
	errno = 0;
	check("ttyname_r refuses a buffer that is too small",
	      ttyname_r(STDIN_FILENO, name, 2) == ERANGE);
	fd = open("/dev/null", O_RDONLY);
	errno = 0;
	check("ttyname refuses what is not a terminal",
	      fd >= 0 && ttyname(fd) == NULL && errno == ENOTTY);
	close(fd);

	/* The controlling terminal is there before anything is detached. */
	fd = open("/dev/tty", O_RDWR);
	check("/dev/tty opens while a terminal is held", fd >= 0);
	if (fd >= 0)
		close(fd);

	/*
	 * A child leaves the session, takes a pty as its own terminal, and
	 * must then find that pty through /dev/tty.  This is the sequence a
	 * login server performs for every session it accepts.
	 */
	if (openpty(&master, &slave, name, NULL, NULL) != 0) {
		check("a new session takes a pty as its terminal", 0);
		check("the pty is what /dev/tty then names", 0);
	} else {
		printf("SES info pty is %s\n", name);
		child = fork();
		if (child == 0) {
			char reported[64];
			int own, ok = 0;

			/* Leaving the session drops the old terminal. */
			if (setsid() == -1)
				_exit(10);
			if (open("/dev/tty", O_RDWR) != -1)
				_exit(11);
			if (errno != ENXIO)
				_exit(12);

			/* The pty becomes this session's terminal. */
			if (ioctl(slave, TIOCSCTTY) != 0)
				_exit(13);

			/* The pty is named by its own descriptor. */
			if (ttyname_r(slave, reported, sizeof(reported)) != 0 ||
			    strcmp(reported, name) != 0)
				_exit(18);
			own = open("/dev/tty", O_RDWR);
			if (own < 0)
				_exit(14);

			/*
			 * What goes to /dev/tty must come out of the pty the
			 * session just took, which is the whole claim.
			 */
			if (write(own, "ctty", 4) == 4)
				ok = 1;
			close(own);

			/*
			 * Giving up the terminal sends SIGHUP to the
			 * foreground group, which is this process: that is
			 * what a controlling process leaving is defined to
			 * do, so it is ignored rather than avoided.
			 */
			(void)signal(SIGHUP, SIG_IGN);
			own = open("/dev/tty", O_RDWR);
			if (own >= 0) {
				if (ioctl(own, TIOCNOTTY) != 0)
					_exit(15);
				close(own);
			}
			if (open("/dev/tty", O_RDWR) != -1)
				_exit(16);
			_exit(ok ? 0 : 17);
		}
		close(slave);

		/*
		 * Read the pty before reaping the child: the last close of
		 * the other end is what ends this stream, so anything still
		 * waiting in it has to be taken first.
		 */
		memset(buffer, 0, sizeof(buffer));
		waiting.fd = master;
		waiting.events = POLLIN;
		waiting.revents = 0;
		n = poll(&waiting, 1, 5000) > 0 ?
		    read(master, buffer, sizeof(buffer) - 1) : -1;
		status = 0;
		if (waitpid(child, &status, 0) != child)
			printf("SES info waitpid failed, errno %d\n", errno);
		else if (WIFSIGNALED(status))
			printf("SES info child died of signal %d\n",
			       WTERMSIG(status));
		else if (WIFEXITED(status))
			printf("SES info child exited %d\n",
			       WEXITSTATUS(status));
		check("a new session takes a pty as its terminal",
		      WIFEXITED(status) && WEXITSTATUS(status) != 10 &&
		      WEXITSTATUS(status) != 11 && WEXITSTATUS(status) != 12 &&
		      WEXITSTATUS(status) != 13 && WEXITSTATUS(status) != 14);
		check("TIOCNOTTY gives the terminal back up",
		      WIFEXITED(status) && WEXITSTATUS(status) == 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			printf("SES info child reported %d\n",
			       WEXITSTATUS(status));

		/* The bytes the child sent to /dev/tty came out of this pty. */
		check("what went to /dev/tty came out of the pty",
		      n >= 4 && strstr(buffer, "ctty") != NULL);
		close(master);
	}

	/* This process still has the terminal it started with. */
	fd = open("/dev/tty", O_RDWR);
	check("the parent keeps its own terminal throughout", fd >= 0);
	if (fd >= 0) {
		n = write(fd, "", 0);
		check("the parent's terminal is still usable", n == 0);
		close(fd);
	} else {
		check("the parent's terminal is still usable", 0);
	}

	(void)buffer;
	printf("SES verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
