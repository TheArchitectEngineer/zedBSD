/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The applications that open files: which ones fit a file, and starting
 * one on it (spec §14).
 *
 * The ways to open a file come from three lists, the first match of the
 * earliest list being the default: the user's
 * $XDG_CONFIG_HOME/keiland/open-with, the system's /etc/keiland/open-with,
 * and the built-in table below.  A list's line is
 *
 *	PATTERNS<TAB>NAME<TAB>COMMAND
 *
 * where PATTERNS are MIME-type globs separated by commas (a star stands for
 * any characters, as in the shell), NAME is what the window shows, and
 * COMMAND is run by the shell with %f standing for the file's path, quoted
 * for the shell (the path is added at the end when there is no %f).  A command that starts
 * with "@terminal " runs the rest in a new terminal window; "@quicklook"
 * shows the file in Quick Look.  Lines starting with '#' are comments.
 *
 * The lists are read each time a file is opened, so an edit takes effect
 * at once, and nothing of them is kept between openings.
 */

#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* The system's list, and the user's under the configuration folder. */
#define APPS_SYSTEM_LIST	"/etc/keiland/open-with"
#define APPS_USER_LIST		"keiland/open-with"

/* The terminal a "@terminal" command runs in, and the words that mark the two special commands. */
#define APPS_TERMINAL		"/bin/terminal"
#define APPS_TERMINAL_WORD	"@terminal "
#define APPS_QUICKLOOK_WORD	"@quicklook"

/* The longest line of a list, and the longest command once the path is put in. */
#define APPS_LINE		1024
#define APPS_EXPANDED		(FM_OPENER_COMMAND + 4 * FM_PATH_MAX)

/* How many descriptors a started program closes before it runs (all the window may have open). */
#define APPS_DESCRIPTORS	1024

/* The types that read as text for the viewers, beyond text/ itself. */
#define APPS_TEXT_TYPES		"text/" "*,application/json,application/xml,application/x-shellscript,application/javascript"

/*
 * One built-in way to open files: the types it fits, its name, its command,
 * and the program it needs (NULL when it needs none), without which it is
 * not offered.
 */
struct apps_builtin {
	const char *patterns;
	const char *name;
	const char *command;
	const char *needs;
};

/*
 * The built-in ways, after the lists of the user and the system.  Anything
 * the others do not fit is shown by less in a terminal.
 */
static const struct apps_builtin apps_builtins[] = {
	{ "image/" "*", "Quick Look", "@quicklook", NULL },
	{ APPS_TEXT_TYPES, "Terminal (less)", "@terminal less %f", NULL },
	{ APPS_TEXT_TYPES, "Remacs", "@terminal remacs %f", "remacs" },
	{ APPS_TEXT_TYPES, "Terminal (ed)", "@terminal ed %f", "ed" },
	{ "*", "Terminal (less)", "@terminal less %f", NULL }
};

/* The folders a needed program is looked for in. */
static const char *const apps_program_folders[] = { "/bin", "/usr/bin", "/usr/local/bin" };

static void apps_read_list(const char *path, const char *type, struct fm_opener *openers, int capacity, int *count);
static int apps_parse_line(char *line, char **patterns, char **name, char **command);
static int apps_matches(const char *patterns, const char *type);
static void apps_add(struct fm_opener *openers, int capacity, int *count, const char *name, const char *command);
static int apps_program_exists(const char *program);
static int apps_expand(const char *command, const char *path, char *expanded, size_t size);
static int apps_quote(const char *path, char *quoted, size_t size);
static void apps_run(const char *command);
static void apps_exec(char *const arguments[]);

/*
 * Fills the ways a file can be opened, the default first, and reports how
 * many there are (at least one: the built-in viewer fits anything).
 *
 * A file whose mode lets it run is first offered to run in a terminal.
 */
int
fm_apps_for(
	const char *path,
	const struct fm_mime *mime,
	mode_t mode,
	struct fm_opener *openers,
	int capacity)
{
	char list[FM_PATH_MAX];
	const char *config;
	size_t index;
	int regular;
	int matched;
	int count;
	int found;

	/* A program runs in a terminal first. */
	(void)path;
	count = 0;
	regular = S_ISREG(mode);
	if (regular != 0 && (mode & 0111) != 0)
		apps_add(openers, capacity, &count, "Run in Terminal", "@terminal %f");

	/* The user's list, under $XDG_CONFIG_HOME or ~/.config. */
	config = getenv("XDG_CONFIG_HOME");
	list[0] = '\0';
	if (config != NULL && config[0] != '\0') {
		snprintf(list, sizeof(list), "%s/%s", config, APPS_USER_LIST);
	} else {
		config = getenv("HOME");
		if (config != NULL && config[0] != '\0')
			snprintf(list, sizeof(list), "%s/.config/%s", config, APPS_USER_LIST);
	}

	/* The user's list, when there is a place for it. */
	if (list[0] != '\0')
		apps_read_list(list, mime->type, openers, capacity, &count);

	/* The system's list. */
	apps_read_list(APPS_SYSTEM_LIST, mime->type, openers, capacity, &count);

	/* The built-in ways that fit, and whose program is there. */
	for (index = 0; index < sizeof(apps_builtins) / sizeof(apps_builtins[0]); index++) {
		matched = apps_matches(apps_builtins[index].patterns, mime->type);
		if (matched == 0)
			continue;

		/* A way that needs a program the system does not have is left out. */
		if (apps_builtins[index].needs != NULL) {
			found = apps_program_exists(apps_builtins[index].needs);
			if (found == 0)
				continue;
		}

		/* It is offered. */
		apps_add(openers, capacity, &count, apps_builtins[index].name, apps_builtins[index].command);
	}

	/* Reports how many ways there are. */
	return count;
}

/*
 * Tells whether an opener is Quick Look, which the window shows itself
 * rather than starting a program.
 */
int
fm_apps_is_quicklook(
	const struct fm_opener *opener)
{
	int match;

	/* The command is the Quick Look word alone. */
	match = strcmp(opener->command, APPS_QUICKLOOK_WORD);
	if (match != 0)
		return 0;

	/* It is Quick Look. */
	return 1;
}

/*
 * Starts an opener's command on a file, apart from the file manager (in a
 * session of its own, so it outlives the window).
 *
 * Returns 0, ENAMETOOLONG when the command with the path is too long, or
 * the errno value of a failed fork.
 */
int
fm_apps_launch(
	const struct fm_opener *opener,
	const char *path)
{
	char expanded[APPS_EXPANDED];
	pid_t child;
	int status;
	int error;

	/* The command with the path put in. */
	error = apps_expand(opener->command, path, expanded, sizeof(expanded));
	if (error != 0)
		return error;

	/*
	 * A child that starts a grandchild and leaves at once: the grandchild
	 * runs the command, and, having no parent left, is not the file
	 * manager's to wait for.
	 */
	child = fork();
	if (child < 0)
		return errno;
	if (child == 0) {
		apps_run(expanded);
		_exit(0);
	}

	/* The child leaves at once; it is waited for so it does not linger. */
	(void)waitpid(child, &status, 0);

	/* The log line the tests wait for. */
	fm_log("LAUNCH name=%s command=%s", opener->name, expanded);

	/* Succeeded: the command is on its way. */
	return 0;
}

/*
 * Starts a program with its arguments apart from the file manager, as a
 * launch does, but keeping the standard input, output and error (a new
 * window of this program writes its log where this one does).
 *
 * Returns 0, or the errno value of a failed fork.
 */
int
fm_apps_spawn(
	char *const arguments[])
{
	pid_t child;
	int status;

	/* A child that starts a grandchild and leaves, as for a launch. */
	child = fork();
	if (child < 0)
		return errno;
	if (child == 0) {
		apps_exec(arguments);
		_exit(0);
	}

	/* The child leaves at once; it is waited for so it does not linger. */
	(void)waitpid(child, &status, 0);
	fm_log("SPAWN program=%s", arguments[0]);

	/* Succeeded: the program is on its way. */
	return 0;
}

/* Adds the openers of a list that fit a type; a list that cannot be read adds none. */
static void
apps_read_list(
	const char *path,
	const char *type,
	struct fm_opener *openers,
	int capacity,
	int *count)
{
	char line[APPS_LINE];
	char *patterns;
	char *name;
	char *command;
	char *read;
	FILE *file;
	int parsed;
	int matched;

	/* The list. */
	file = fopen(path, "r");
	if (file == NULL)
		return;

	/* Each line that names a way to open the type. */
	for (;;) {
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;

		/* A comment, a blank or a malformed line says nothing. */
		parsed = apps_parse_line(line, &patterns, &name, &command);
		if (parsed == 0)
			continue;

		/* A line for other types says nothing either. */
		matched = apps_matches(patterns, type);
		if (matched == 0)
			continue;

		/* The way is offered. */
		apps_add(openers, capacity, count, name, command);
	}

	/* The list is not needed any more. */
	fclose(file);
}

/* Splits a list's line at its tabs into its patterns, name and command; zero for a line that is not one. */
static int
apps_parse_line(
	char *line,
	char **patterns,
	char **name,
	char **command)
{
	char *tab;
	size_t length;

	/* The line without its end. */
	length = strlen(line);
	while (length > 0U && (line[length - 1U] == '\n' || line[length - 1U] == '\r')) {
		line[length - 1U] = '\0';
		length--;
	}

	/* A blank line and a comment are not ways. */
	if (line[0] == '\0' || line[0] == '#')
		return 0;

	/* The patterns end at the first tab. */
	*patterns = line;
	tab = strchr(line, '\t');
	if (tab == NULL)
		return 0;
	*tab = '\0';

	/* The name ends at the second. */
	*name = tab + 1;
	tab = strchr(*name, '\t');
	if (tab == NULL)
		return 0;
	*tab = '\0';

	/* The command is the rest, and none of the three may be empty. */
	*command = tab + 1;
	if ((*patterns)[0] == '\0' || (*name)[0] == '\0' || (*command)[0] == '\0')
		return 0;

	/* Succeeded: the line is a way to open files. */
	return 1;
}

/* Tells whether a type fits one of the comma-separated globs of a list. */
static int
apps_matches(
	const char *patterns,
	const char *type)
{
	char pattern[128];
	const char *start;
	const char *end;
	size_t length;
	int miss;

	/* Each glob between the commas, spaces around it left out. */
	start = patterns;
	while (*start != '\0') {
		while (*start == ' ' || *start == ',')
			start++;
		if (*start == '\0')
			break;
		end = strchr(start, ',');
		if (end == NULL)
			end = start + strlen(start);

		/* The glob alone, without the spaces after it. */
		length = (size_t)(end - start);
		while (length > 0U && start[length - 1U] == ' ')
			length--;
		if (length >= sizeof(pattern))
			length = sizeof(pattern) - 1U;
		memcpy(pattern, start, length);
		pattern[length] = '\0';

		/* The type fits this glob. */
		miss = fnmatch(pattern, type, 0);
		if (miss == 0)
			return 1;

		/* The next glob. */
		start = end;
	}

	/* No glob fits. */
	return 0;
}

/* Adds a way to open a file unless one of the same name is there already or the list is full. */
static void
apps_add(
	struct fm_opener *openers,
	int capacity,
	int *count,
	const char *name,
	const char *command)
{
	int index;
	int match;

	/* No room. */
	if (*count >= capacity)
		return;

	/* A name offered already (by an earlier list) is not offered twice. */
	for (index = 0; index < *count; index++) {
		match = strcmp(openers[index].name, name);
		if (match == 0)
			return;
	}

	/* The way, at the end. */
	snprintf(openers[*count].name, sizeof(openers[*count].name), "%s", name);
	snprintf(openers[*count].command, sizeof(openers[*count].command), "%s", command);
	(*count)++;
}

/* Tells whether a program of a name is in one of the folders programs are looked for in. */
static int
apps_program_exists(
	const char *program)
{
	char path[FM_PATH_MAX];
	size_t index;
	int error;

	/* Each folder, for a file that may run. */
	for (index = 0; index < sizeof(apps_program_folders) / sizeof(apps_program_folders[0]); index++) {
		snprintf(path, sizeof(path), "%s/%s", apps_program_folders[index], program);
		error = access(path, X_OK);
		if (error == 0)
			return 1;
	}

	/* No folder has it. */
	return 0;
}

/*
 * Writes a command with each %f replaced by the path quoted for the shell
 * (the path added at the end when there is none); returns 0 or
 * ENAMETOOLONG.
 */
static int
apps_expand(
	const char *command,
	const char *path,
	char *expanded,
	size_t size)
{
	char quoted[4 * FM_PATH_MAX];
	size_t length;
	size_t done;
	int placed;
	int error;

	/* The path, quoted once for all the places it goes. */
	error = apps_quote(path, quoted, sizeof(quoted));
	if (error != 0)
		return error;
	length = strlen(quoted);

	/* The command, character by character, the path where %f is. */
	done = 0;
	placed = 0;
	while (*command != '\0') {
		if (command[0] == '%' && command[1] == 'f') {
			if (done + length >= size)
				return ENAMETOOLONG;
			memcpy(expanded + done, quoted, length);
			done += length;
			command += 2;
			placed = 1;
			continue;
		}

		/* Any other character is kept. */
		if (done + 1U >= size)
			return ENAMETOOLONG;
		expanded[done] = *command;
		done++;
		command++;
	}

	/* A command without %f takes the path at its end. */
	if (placed == 0) {
		if (done + 1U + length >= size)
			return ENAMETOOLONG;
		expanded[done] = ' ';
		memcpy(expanded + done + 1U, quoted, length);
		done += 1U + length;
	}

	/* Succeeded: the command ends there. */
	expanded[done] = '\0';
	return 0;
}

/* Quotes a path for the shell: in single quotes, each single quote written as '\''; returns 0 or ENAMETOOLONG. */
static int
apps_quote(
	const char *path,
	char *quoted,
	size_t size)
{
	size_t done;

	/* The opening quote. */
	if (size < 3U)
		return ENAMETOOLONG;
	quoted[0] = '\'';
	done = 1;

	/* Each character; a single quote closes the quote, is escaped, and opens it again. */
	while (*path != '\0') {
		if (done + 5U >= size)
			return ENAMETOOLONG;
		if (*path == '\'') {
			memcpy(quoted + done, "'\\''", 4);
			done += 4;
		} else {
			quoted[done] = *path;
			done++;
		}

		/* The next character. */
		path++;
	}

	/* The closing quote. */
	quoted[done] = '\'';
	quoted[done + 1U] = '\0';

	/* Succeeded: the path is quoted. */
	return 0;
}

/*
 * Runs a command from the child that fork made: a grandchild in a session
 * of its own runs it, in a terminal or by the shell, with nothing of the
 * window's input or output.
 */
static void
apps_run(
	const char *command)
{
	char terminal_command[APPS_EXPANDED + 16];
	pid_t grandchild;
	int descriptor;
	int is_terminal;

	/* The grandchild; the child leaves as soon as it is made. */
	grandchild = fork();
	if (grandchild != 0)
		return;

	/* A session of its own, and no terminal of the window's. */
	(void)setsid();
	descriptor = open("/dev/null", O_RDWR);
	if (descriptor >= 0) {
		(void)dup2(descriptor, 0);
		(void)dup2(descriptor, 1);
		(void)dup2(descriptor, 2);
		if (descriptor > 2)
			close(descriptor);
	}

	/*
	 * None of the window's other descriptors: the program must not keep
	 * the file manager's connection to the compositor (or its GPU) open
	 * after the window closes.
	 */
	for (descriptor = 3; descriptor < APPS_DESCRIPTORS; descriptor++)
		(void)close(descriptor);

	/* A terminal command: the rest runs in a new terminal window. */
	is_terminal = strncmp(command, APPS_TERMINAL_WORD, strlen(APPS_TERMINAL_WORD));
	if (is_terminal == 0) {
		snprintf(terminal_command, sizeof(terminal_command), "--command=%s", command + strlen(APPS_TERMINAL_WORD));
		execl(APPS_TERMINAL, "terminal", terminal_command, (char *)NULL);
		_exit(127);
	}

	/* Anything else runs by the shell. */
	execl("/bin/sh", "sh", "-c", command, (char *)NULL);
	_exit(127);
}

/* Runs a program from the child that fork made: a grandchild in a session of its own, with only the standard descriptors. */
static void
apps_exec(
	char *const arguments[])
{
	pid_t grandchild;
	int descriptor;

	/* The grandchild; the child leaves as soon as it is made. */
	grandchild = fork();
	if (grandchild != 0)
		return;

	/* A session of its own, and none of the window's descriptors but the standard ones. */
	(void)setsid();
	for (descriptor = 3; descriptor < APPS_DESCRIPTORS; descriptor++)
		(void)close(descriptor);

	/* The program; only a failed exec comes back. */
	execv(arguments[0], arguments);
	_exit(127);
}
