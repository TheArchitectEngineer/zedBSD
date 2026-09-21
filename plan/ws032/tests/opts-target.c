/* WS032: getopt_long and the symbolic file modes, on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/stat.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("OPT %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

static int flagged;

static struct option longopts[] = {
	{ "verbose", no_argument,       NULL,     'v' },
	{ "file",    required_argument, NULL,     'f' },
	{ "colour",  optional_argument, NULL,     'c' },
	{ "quiet",   no_argument,       &flagged, 42  },
	{ "quarrel", no_argument,       NULL,     'q' },
	{ NULL,      0,                 NULL,     0   }
};

/* Runs one argument vector through getopt_long and records what came out. */
static void run(char *const *argv, int argc, char *out, size_t size,
		int single_dash)
{
	char *at = out;
	int c, index;

	optind = 1;
	opterr = 0;
	optarg = NULL;
	*at = '\0';
	while ((c = single_dash ?
		getopt_long_only(argc, argv, "vf:xy", longopts, &index) :
		getopt_long(argc, argv, "vf:xy", longopts, &index)) != -1) {
		if ((size_t)(at - out) + 32U >= size)
			break;
		if (c == 0)
			at += sprintf(at, "[flag=%d]", flagged);
		else if (optarg != NULL)
			at += sprintf(at, "%c(%s)", c, optarg);
		else
			at += sprintf(at, "%c", c);
	}
	at += sprintf(at, "|");
	while (optind < argc)
		at += sprintf(at, "%s ", argv[optind++]);
}

int main(void)
{
	char out[256];
	void *compiled;
	mode_t mask;

	{
		char *argv[] = { "prog", "--verbose", "--file=x.txt",
				 "operand", "--verbose", NULL };

		run(argv, 5, out, sizeof(out), 0);
		check("a name in full, and a value after an equals sign",
		      strcmp(out, "vf(x.txt)|operand --verbose ") == 0);
	}
	{
		char *argv[] = { "prog", "--file", "next.txt", NULL };

		run(argv, 3, out, sizeof(out), 0);
		check("a required value may be the next argument",
		      strcmp(out, "f(next.txt)|") == 0);
	}
	{
		char *argv[] = { "prog", "--colour", "notavalue", NULL };

		run(argv, 3, out, sizeof(out), 0);
		check("an optional value is not taken from the next argument",
		      strcmp(out, "c|notavalue ") == 0);
	}
	{
		char *argv[] = { "prog", "--colour=always", NULL };

		run(argv, 2, out, sizeof(out), 0);
		check("an optional value is taken after an equals sign",
		      strcmp(out, "c(always)|") == 0);
	}
	{
		char *argv[] = { "prog", "--verb", NULL };

		run(argv, 2, out, sizeof(out), 0);
		check("an unambiguous abbreviation is accepted",
		      strcmp(out, "v|") == 0);
	}
	{
		char *argv[] = { "prog", "--q", NULL };

		run(argv, 2, out, sizeof(out), 0);
		check("an abbreviation that fits two names is refused",
		      strcmp(out, "?|") == 0);
	}
	{
		char *argv[] = { "prog", "--quiet", NULL };

		flagged = 0;
		run(argv, 2, out, sizeof(out), 0);
		check("a table with a flag sets it and reports nothing",
		      strcmp(out, "[flag=42]|") == 0 && flagged == 42);
	}
	{
		char *argv[] = { "prog", "-vf", "short.txt", "--verbose", NULL };

		run(argv, 4, out, sizeof(out), 0);
		check("single letters still work, and a name may follow them",
		      strcmp(out, "vf(short.txt)v|") == 0);
	}
	{
		char *argv[] = { "prog", "--", "--verbose", NULL };

		run(argv, 3, out, sizeof(out), 0);
		check("two dashes alone end the options",
		      strcmp(out, "|--verbose ") == 0);
	}
	{
		char *argv[] = { "prog", "-verbose", "-xy", NULL };

		run(argv, 3, out, sizeof(out), 1);
		if (strcmp(out, "vxy|") != 0)
			printf("OPT info got [%s]\n", out);
		check("with one dash a name in full is recognised too",
		      strcmp(out, "vxy|") == 0);
	}

	/* File modes written as text. */
	compiled = setmode("755");
	check("a mode written as a number replaces every bit",
	      compiled != NULL && getmode(compiled, 0644) == 0755 &&
	      getmode(compiled, 0777) == 0755);
	free(compiled);

	compiled = setmode("u+w,go-w");
	check("clauses are applied in the order they were written",
	      compiled != NULL && getmode(compiled, 0444) == 0644);
	free(compiled);

	compiled = setmode("a=rx");
	check("an equals clause replaces what it names",
	      compiled != NULL && getmode(compiled, 0777) == 0555);
	free(compiled);

	compiled = setmode("u+s");
	check("the set-user-id bit can be named",
	      compiled != NULL && getmode(compiled, 0755) == (mode_t)(S_ISUID | 0755));
	free(compiled);

	compiled = setmode("+t");
	check("the sticky bit can be named",
	      compiled != NULL &&
	      (getmode(compiled, 0755) & (mode_t)S_ISVTX) != 0);
	free(compiled);

	/* X grants execute only where there already is some. */
	compiled = setmode("a+X");
	check("X spares a file that nobody may run",
	      compiled != NULL && getmode(compiled, 0644) == 0644);
	check("X grants execute where a file already has some",
	      getmode(compiled, 0744) == 0755);
	check("X grants execute on a directory",
	      getmode(compiled, (mode_t)(S_IFDIR | 0644)) ==
	      (mode_t)(S_IFDIR | 0755));
	free(compiled);

	compiled = setmode("g=u");
	check("bits can be copied from the file's own owner",
	      compiled != NULL && getmode(compiled, 0750) == 0770);
	free(compiled);

	/* A clause naming nobody spares the creation mask. */
	mask = umask(022);
	compiled = setmode("+w");
	(void)umask(mask);
	check("a clause naming nobody spares the creation mask",
	      compiled != NULL && getmode(compiled, 0444) == 0644);
	free(compiled);

	check("text that is not a mode is refused",
	      setmode("u+q") == NULL && setmode("999") == NULL);

	printf("OPT verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
