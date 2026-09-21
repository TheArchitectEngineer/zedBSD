/* WS032: glob(3), on the target.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */
#include <sys/stat.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define ROOT "/tmp/globtest"

static int failures;

static void check(const char *what, int ok)
{
	printf("GLB %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

static void make_file(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd >= 0)
		close(fd);
}

/* Joins the results with spaces, so one string says the whole answer. */
static void joined(glob_t *g, char *out, size_t size)
{
	size_t i;
	char *at = out;

	*at = '\0';
	for (i = 0; i < g->gl_pathc; i++) {
		if ((size_t)(at - out) + strlen(g->gl_pathv[g->gl_offs + i]) +
		    2U >= size)
			break;
		if (i != 0)
			*at++ = ' ';
		strcpy(at, g->gl_pathv[g->gl_offs + i]);
		at += strlen(at);
	}
}

int main(void)
{
	glob_t g;
	char out[512];
	int rc;

	(void)mkdir(ROOT, 0755);
	(void)mkdir(ROOT "/sub", 0755);
	(void)mkdir(ROOT "/sub2", 0755);
	make_file(ROOT "/alpha.txt");
	make_file(ROOT "/beta.txt");
	make_file(ROOT "/gamma.log");
	make_file(ROOT "/.hidden");
	make_file(ROOT "/sub/inner.txt");
	make_file(ROOT "/sub2/inner.txt");

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/*.txt", 0, NULL, &g);
	joined(&g, out, sizeof(out));
	check("a star selects what is there, in order",
	      rc == 0 && g.gl_pathc == 2 &&
	      strcmp(out, ROOT "/alpha.txt " ROOT "/beta.txt") == 0);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/*", 0, NULL, &g);
	joined(&g, out, sizeof(out));
	check("a leading dot is not selected by a star",
	      rc == 0 && strstr(out, ".hidden") == NULL && g.gl_pathc == 5);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/.*", 0, NULL, &g);
	check("a pattern that begins with a dot does select it",
	      rc == 0 && g.gl_pathc >= 1 &&
	      strstr(g.gl_pathv[0], ".") != NULL);
	globfree(&g);

	/* A part with magic in the middle reads only the directories it fits. */
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/sub*/inner.txt", 0, NULL, &g);
	joined(&g, out, sizeof(out));
	check("magic in the middle walks each matching directory",
	      rc == 0 && g.gl_pathc == 2 &&
	      strcmp(out, ROOT "/sub/inner.txt " ROOT "/sub2/inner.txt") == 0);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/????a.txt", 0, NULL, &g);
	check("a question mark stands for one character",
	      rc == 0 && g.gl_pathc == 1 &&
	      strcmp(g.gl_pathv[0], ROOT "/alpha.txt") == 0);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/[ab]*.txt", 0, NULL, &g);
	check("a bracket selects one of several characters",
	      rc == 0 && g.gl_pathc == 2);
	globfree(&g);

	/* Nothing matched is a result of its own, not an empty success. */
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/*.absent", 0, NULL, &g);
	check("nothing matched is reported as such",
	      rc == GLOB_NOMATCH && g.gl_pathc == 0 && g.gl_pathv != NULL);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/*.absent", GLOB_NOCHECK, NULL, &g);
	check("GLOB_NOCHECK passes the pattern through",
	      rc == 0 && g.gl_pathc == 1 &&
	      strcmp(g.gl_pathv[0], ROOT "/*.absent") == 0);
	globfree(&g);

	/* A directory is marked only when asked. */
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/sub", GLOB_MARK, NULL, &g);
	check("GLOB_MARK marks a directory",
	      rc == 0 && g.gl_pathc == 1 &&
	      strcmp(g.gl_pathv[0], ROOT "/sub/") == 0);
	globfree(&g);

	/* Appending adds to what the previous call found. */
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/*.txt", 0, NULL, &g);
	rc |= glob(ROOT "/*.log", GLOB_APPEND, NULL, &g);
	joined(&g, out, sizeof(out));
	check("GLOB_APPEND adds to the earlier result",
	      rc == 0 && g.gl_pathc == 3 && strstr(out, "gamma.log") != NULL);
	check("gl_matchc counts only the latest call", g.gl_matchc == 1);
	globfree(&g);

	/* Reserved slots stay empty for the caller to fill. */
	memset(&g, 0, sizeof(g));
	g.gl_offs = 2;
	rc = glob(ROOT "/*.log", GLOB_DOOFFS, NULL, &g);
	check("GLOB_DOOFFS leaves room at the front",
	      rc == 0 && g.gl_pathc == 1 && g.gl_pathv[0] == NULL &&
	      g.gl_pathv[1] == NULL &&
	      strcmp(g.gl_pathv[2], ROOT "/gamma.log") == 0 &&
	      g.gl_pathv[3] == NULL);
	globfree(&g);

	/* Braces are taken apart before anything is matched. */
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/{alpha,gamma}.*", GLOB_BRACE, NULL, &g);
	joined(&g, out, sizeof(out));
	check("GLOB_BRACE expands each alternative",
	      rc == 0 && g.gl_pathc == 2 &&
	      strstr(out, "alpha.txt") != NULL &&
	      strstr(out, "gamma.log") != NULL);
	globfree(&g);

	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/{alpha,{beta,gamma}}.*", GLOB_BRACE, NULL, &g);
	check("GLOB_BRACE expands nested alternatives",
	      rc == 0 && g.gl_pathc == 3);
	globfree(&g);

	/* The magic flag reports what the pattern held. */
	memset(&g, 0, sizeof(g));
	(void)glob(ROOT "/alpha.txt", 0, NULL, &g);
	check("a pattern without magic is reported as such",
	      (g.gl_flags & GLOB_MAGCHAR) == 0 && g.gl_pathc == 1);
	globfree(&g);
	memset(&g, 0, sizeof(g));
	(void)glob(ROOT "/*.txt", 0, NULL, &g);
	check("a pattern with magic is reported as such",
	      (g.gl_flags & GLOB_MAGCHAR) != 0);
	globfree(&g);

	/* A quoted star is an ordinary character. */
	make_file(ROOT "/lit*eral");
	memset(&g, 0, sizeof(g));
	rc = glob(ROOT "/lit\\*eral", 0, NULL, &g);
	check("a backslash quotes the character after it",
	      rc == 0 && g.gl_pathc == 1 &&
	      strcmp(g.gl_pathv[0], ROOT "/lit*eral") == 0);
	globfree(&g);

	(void)unlink(ROOT "/lit*eral");
	(void)unlink(ROOT "/alpha.txt");
	(void)unlink(ROOT "/beta.txt");
	(void)unlink(ROOT "/gamma.log");
	(void)unlink(ROOT "/.hidden");
	(void)unlink(ROOT "/sub/inner.txt");
	(void)unlink(ROOT "/sub2/inner.txt");
	(void)rmdir(ROOT "/sub");
	(void)rmdir(ROOT "/sub2");
	(void)rmdir(ROOT);

	printf("GLB verdict: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
