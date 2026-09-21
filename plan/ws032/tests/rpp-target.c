/* WS032: readpassphrase(3) at the target's own terminal.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <readpassphrase.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
	struct termios before, during, after;
	char answer[64];
	char *result;
	int failures = 0;
	int had_echo;

	if (tcgetattr(STDIN_FILENO, &before) != 0) {
		printf("RPP FAIL no terminal to ask at\n");
		return 1;
	}
	had_echo = (before.c_lflag & ECHO) != 0;

	result = readpassphrase("passphrase: ", answer, sizeof(answer),
				RPP_ECHO_OFF);
	printf("RPP %-4s the answer is read\n",
	       result == answer ? "ok" : "FAIL");
	if (result != answer)
		failures++;
	printf("RPP %-4s the answer is what was typed [%s]\n",
	       strcmp(answer, "hunter2") == 0 ? "ok" : "FAIL", answer);
	if (strcmp(answer, "hunter2") != 0)
		failures++;

	/* The newline must not be part of it, and echo must be back on. */
	printf("RPP %-4s no newline is kept\n",
	       strchr(answer, '\n') == NULL ? "ok" : "FAIL");
	if (strchr(answer, '\n') != NULL)
		failures++;

	if (tcgetattr(STDIN_FILENO, &after) != 0)
		after = before;
	printf("RPP %-4s echo is restored\n",
	       (!had_echo || (after.c_lflag & ECHO) != 0) ? "ok" : "FAIL");
	if (had_echo && (after.c_lflag & ECHO) == 0)
		failures++;

	(void)during;
	printf("RPP verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
