/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-079 probe: replacing an environment variable keeps every other one,
 * in this process and in a program it execs.
 *
 *   env-replace            runs the checks, then execs itself as "child"
 *   env-replace child      checks the environment it was given
 *
 * The first variable is replaced with setenv and putenv, a variable is
 * removed, environ is assigned an array of the program's own, and the
 * last step is zdesktop's App Home launch: fork, setenv of a variable the
 * parent already has, and execl.  Prints ENV:PASS and exits 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int failures;

static void check(const char *what, const char *name, const char *expected);
static int child(void);
static int report(const char *what);
static int launch(const char *self);

/* Reports one check: NAME has the value EXPECTED, or is unset when it is NULL. */
static void
check(
	const char *what,
	const char *name,
	const char *expected)
{
	const char *value;
	int same;

	/* The value, and whether it is the expected one. */
	value = getenv(name);
	if (value == NULL || expected == NULL)
		same = value == expected;
	else
		same = strcmp(value, expected) == 0;

	/* Handles the different value condition. */
	if (!same) {
		if (value == NULL)
			value = "(unset)";
		if (expected == NULL)
			expected = "(unset)";
		printf("ENV:FAIL %s: %s=%s, expected %s\n", what, name, value, expected);
		failures++;
	}
}

/* Prints the result line and returns the exit status. */
static int
report(
	const char *what)
{
	/* Handles the failed checks condition. */
	if (failures != 0) {
		printf("ENV:FAIL%s\n", what);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("ENV:PASS%s\n", what);
	return 0;
}

/* Checks the environment the exec'd child was given. */
static int
child(
	void)
{
	check("child", "ENV_A", "launched");
	check("child", "ENV_B", "two");
	check("child", "ENV_C", "three");
	check("child", "ENV_D", "four");

	/* Reports the child's result. */
	return report(" child");
}

/* zdesktop's launch: fork, setenv of a variable the parent has, execl. */
static int
launch(
	const char *self)
{
	pid_t pid;
	pid_t waited;
	int status;
	int exited;

	/* The child replaces a variable and execs this program. */
	pid = fork();
	if (pid == 0) {
		(void)setenv("ENV_A", "launched", 1);
		(void)execl(self, self, "child", (char *)NULL);
		_exit(127);
	}

	/* Handles the failed fork condition. */
	if (pid < 0)
		return -1;

	/* The child's own checks decide its status. */
	waited = waitpid(pid, &status, 0);
	if (waited != pid)
		return -1;

	/* Handles the child that did not exit normally. */
	exited = WIFEXITED(status);
	if (!exited)
		return -1;

	/* Returns the child's exit status. */
	return WEXITSTATUS(status);
}

/* Runs the checks in this process, then the fork and exec one. */
int
main(
	int argc,
	char **argv)
{
	static char put_a[] = "ENV_A=put";
	static char own_a[] = "ENV_A=own";
	static char own_b[] = "ENV_B=two";
	static char *own[] = {own_a, own_b, NULL};
	int unchanged;
	int launched;

	/* The exec'd child only checks. */
	if (argc > 1 && argv[1][0] == 'c')
		return child();

	/* Three variables in libc's own array. */
	(void)clearenv();
	(void)setenv("ENV_A", "one", 1);
	(void)setenv("ENV_B", "two", 1);
	(void)setenv("ENV_C", "three", 1);

	/* Replacing the first variable keeps the later ones. */
	(void)setenv("ENV_A", "again", 1);
	check("setenv", "ENV_A", "again");
	check("setenv", "ENV_B", "two");
	check("setenv", "ENV_C", "three");
	(void)putenv(put_a);
	check("putenv", "ENV_A", "put");
	check("putenv", "ENV_C", "three");

	/* Removing one keeps the others, and a new one follows them. */
	(void)unsetenv("ENV_B");
	(void)setenv("ENV_D", "four", 1);
	check("unsetenv", "ENV_B", NULL);
	check("unsetenv", "ENV_C", "three");
	check("unsetenv", "ENV_D", "four");

	/* An environ of the program's own is used, then copied by setenv. */
	environ = own;
	check("environ", "ENV_A", "own");
	check("environ", "ENV_C", NULL);
	(void)setenv("ENV_A", "again", 1);
	(void)setenv("ENV_C", "three", 1);
	(void)setenv("ENV_D", "four", 1);
	check("environ", "ENV_A", "again");
	check("environ", "ENV_B", "two");
	check("environ", "ENV_C", "three");
	check("environ", "ENV_D", "four");

	/* The program's array is left as it was. */
	unchanged = own[0] == own_a && own[1] == own_b && own[2] == NULL;
	if (!unchanged) {
		printf("ENV:FAIL environ: the program's array changed\n");
		failures++;
	}

	/* The launch, as zdesktop's App Home does it. */
	launched = launch(argv[0]);
	if (launched != 0) {
		printf("ENV:FAIL exec: the child failed\n");
		failures++;
	}

	/* Reports the result. */
	return report("");
}
