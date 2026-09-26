/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Sets the environment for a utility (POSIX XCU env).
 *
 *	env [-i] [name=value]... [utility [argument...]]
 *
 * -i (or - alone) starts from an empty environment.  Without a utility the
 * resulting environment is written, one name=value to a line.  It is
 * installed as /usr/bin/env, the path that #! lines name; the shell has an
 * env builtin of its own.
 *
 * GNU's options are taken too: -u name (--unset) takes a name out, -0
 * (--null) ends each written entry with a NUL byte, -C directory (--chdir)
 * runs the utility there, -S string (--split-string) splits the string into
 * words that take its place (as a #! line of "#!/usr/bin/env -S cmd args"
 * needs), and -v (--debug) is taken.  The options end at the first
 * assignment or utility, whose own options they are not.
 *
 * The new environment is an array of env's own, given to the utility with
 * execve, so that the C library's environment is never rebuilt under it.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * What the options ask for.  unset is a list that grows with each -u, and
 * lives until env ends.
 */
struct options {
	int empty;
	int end;
	const char **unset;
	size_t unset_count;
	const char *directory;
};

/* The environment of the process. */
extern char **environ;

static int read_options(int *argc, char ***argv, struct options *options);
static int letter_options(int argc, char **argv, int *index, struct options *options, const char **split);
static int long_option(int argc, char **argv, int *index, struct options *options, const char **split);
static void apply_value(int code, const char *value, struct options *options, const char **split);
static void split_arguments(int *argc, char ***argv, int index, const char *text);
static void set_entry(char **entries, size_t *count, char *assignment);
static void unset_entry(char **entries, size_t *count, const char *name);
static int run_utility(char **arguments, char **entries);
static int path_search(const char *name, char *path, size_t size);
static void usage(void);

/*
 * Runs env.
 */
int
main(
	int argc,
	char **argv)
{
	struct options options;
	char **entries;
	char **entry;
	char *equals;
	size_t count;
	size_t size;
	size_t index_unset;
	int index;
	int status;
	int result;

	/* The options, up to the first assignment or utility (never after). */
	memset(&options, 0, sizeof(options));
	index = read_options(&argc, &argv, &options);

	/* Room for the inherited entries and every assignment. */
	size = (size_t)argc + 1U;
	for (entry = environ; !options.empty && entry != NULL && *entry != NULL; entry++)
		size++;
	entries = calloc(size, sizeof(*entries));
	if (entries == NULL) {
		fprintf(stderr, "env: out of memory\n");
		return 125;
	}

	/* The inherited environment, unless -i. */
	count = 0;
	for (entry = environ; !options.empty && entry != NULL && *entry != NULL; entry++) {
		entries[count] = *entry;
		count++;
	}

	/* -u: the names taken out. */
	for (index_unset = 0; index_unset < options.unset_count; index_unset++)
		unset_entry(entries, &count, options.unset[index_unset]);

	/* Each name=value before the utility, replacing an inherited one. */
	for (; index < argc; index++) {
		equals = strchr(argv[index], '=');
		if (equals == NULL)
			break;
		set_entry(entries, &count, argv[index]);
	}

	/* No utility: the environment, each entry ended by a newline or (-0) a NUL. */
	if (index >= argc) {
		for (count = 0; entries[count] != NULL; count++)
			printf("%s%c", entries[count], options.end);
		return 0;
	}

	/* -C: the directory the utility runs in. */
	if (options.directory != NULL) {
		result = chdir(options.directory);
		if (result != 0) {
			fprintf(stderr, "env: cannot change directory to '%s': %s\n",
				options.directory, strerror(errno));
			return 125;
		}
	}

	/* Succeeded or not: the utility, in env's place. */
	status = run_utility(argv + index, entries);
	return status;
}

/*
 * Reads the options before the first assignment or utility: POSIX's -i
 * (and - alone), and GNU's -u name, -0, -C directory, -S string (split into
 * words that take the option's place, as #! lines use it) and -v.  argc
 * and argv change when -S splits a string.  Returns the index of the first
 * assignment or utility.
 */
static int
read_options(
	int *argc,
	char ***argv,
	struct options *options)
{
	const char *word;
	const char *value;
	int index;
	int taken;

	/* Each option word, until an operand. */
	options->end = '\n';
	for (index = 1; index < *argc; index++) {
		word = (*argv)[index];

		/* - alone is -i. */
		if (word[0] != '-')
			break;
		if (word[1] == '\0') {
			options->empty = 1;
			continue;
		}

		/* -- ends the options. */
		if (word[1] == '-' && word[2] == '\0')
			return index + 1;

		/* A long option, or a run of letters. */
		value = NULL;
		if (word[1] == '-')
			taken = long_option(*argc, *argv, &index, options, &value);
		else
			taken = letter_options(*argc, *argv, &index, options, &value);
		if (!taken)
			usage();

		/* -S: the words of the string take its place. */
		if (value != NULL) {
			split_arguments(argc, argv, index, value);
			index--;
		}
	}

	/* Succeeded: the first operand. */
	return index;
}

/*
 * Applies a run of letters (-i, -0, -v, and -u, -C, -S with a value).  A
 * value of -S is left in split for the caller.  Returns 0 for a letter
 * env does not take.
 */
static int
letter_options(
	int argc,
	char **argv,
	int *index,
	struct options *options,
	const char **split)
{
	const char *letter;
	const char *value;

	/* Each letter; one that takes a value takes the rest of the word. */
	for (letter = argv[*index] + 1; *letter != '\0'; letter++) {
		switch (*letter) {
		case 'i':
			options->empty = 1;
			continue;
		case '0':
			options->end = '\0';
			continue;
		case 'v':
			/* The trace of -v is not written. */
			continue;
		case 'u':
		case 'C':
		case 'S':
			break;
		default:
			return 0;
		}

		/* The value: the rest of the word, or the next one. */
		value = letter + 1;
		if (*value == '\0') {
			if (*index + 1 >= argc)
				return 0;
			(*index)++;
			value = argv[*index];
		}

		/* The option of the value. */
		apply_value(*letter, value, options, split);
		return 1;
	}

	/* Succeeded. */
	return 1;
}

/*
 * Applies a long option (--ignore-environment, --null, --debug, and
 * --unset, --chdir, --split-string with a value).  Returns 0 for one env
 * does not take.
 */
static int
long_option(
	int argc,
	char **argv,
	int *index,
	struct options *options,
	const char **split)
{
	static const struct {
		const char *name;
		int code;
		int takes_value;
	} names[] = {
		{"ignore-environment", 'i', 0},
		{"null", '0', 0},
		{"debug", 'v', 0},
		{"unset", 'u', 1},
		{"chdir", 'C', 1},
		{"split-string", 'S', 1},
		{NULL, 0, 0}
	};
	const char *word;
	const char *value;
	size_t length;
	size_t entry;
	int differs;

	/* The name, up to an =. */
	word = argv[*index] + 2;
	value = strchr(word, '=');
	length = strlen(word);
	if (value != NULL)
		length = (size_t)(value - word);

	/* The option of the name. */
	for (entry = 0; names[entry].name != NULL; entry++) {
		differs = strncmp(word, names[entry].name, length);
		if (differs == 0 && names[entry].name[length] == '\0')
			break;
	}

	/* A name env does not take. */
	if (names[entry].name == NULL)
		return 0;

	/* An option without a value. */
	if (!names[entry].takes_value) {
		if (names[entry].code == 'i')
			options->empty = 1;
		if (names[entry].code == '0')
			options->end = '\0';
		return 1;
	}

	/* The value: after the =, or the next word. */
	if (value != NULL) {
		value++;
	} else {
		if (*index + 1 >= argc)
			return 0;
		(*index)++;
		value = argv[*index];
	}

	/* Succeeded: the option of the value. */
	apply_value(names[entry].code, value, options, split);
	return 1;
}

/* Applies an option with a value: -u adds a name, -C sets the directory, -S leaves its string. */
static void
apply_value(
	int code,
	const char *value,
	struct options *options,
	const char **split)
{
	/* The option of its code. */
	switch (code) {
	case 'u':
		/* One more name, in a list that grows. */
		options->unset = realloc(options->unset, (options->unset_count + 1U) * sizeof(*options->unset));
		if (options->unset == NULL) {
			fprintf(stderr, "env: out of memory\n");
			exit(125);
		}

		/* The name, at the end of the list. */
		options->unset[options->unset_count] = value;
		options->unset_count++;
		break;
	case 'C':
		options->directory = value;
		break;
	default:
		*split = value;
		break;
	}
}

/*
 * Replaces argv[index] (the word -S took its string from, or the string
 * itself) with the words of the string: split at blanks, with '...' and
 * "..." quoting and a backslash taking the next character.
 */
static void
split_arguments(
	int *argc,
	char ***argv,
	int index,
	const char *text)
{
	char **words;
	char *copy;
	char *write;
	const char *read;
	size_t length;
	int count;
	int quote;
	int in_word;
	int position;

	/* Room for every word (at most one per byte) and the rest of argv. */
	length = strlen(text);
	words = calloc(length + (size_t)*argc + 2U, sizeof(*words));
	if (words == NULL) {
		fprintf(stderr, "env: out of memory\n");
		exit(125);
	}

	/* The words' bytes. */
	copy = malloc(length + 1U);
	if (copy == NULL) {
		fprintf(stderr, "env: out of memory\n");
		exit(125);
	}

	/* The words before the option stay. */
	for (position = 0; position < index; position++)
		words[position] = (*argv)[position];
	count = index;

	/* The words of the string, into the copy. */
	write = copy;
	quote = 0;
	in_word = 0;
	for (read = text; *read != '\0'; read++) {
		/* A blank outside quotes ends a word. */
		if (quote == 0 && (*read == ' ' || *read == '\t')) {
			if (in_word) {
				*write = '\0';
				write++;
				in_word = 0;
			}

			/* On to the next character. */
			continue;
		}

		/* A word starts. */
		if (!in_word) {
			words[count] = write;
			count++;
			in_word = 1;
		}

		/* A quote opens. */
		if (quote == 0 && (*read == '\'' || *read == '"')) {
			quote = *read;
			continue;
		}

		/* The same quote closes. */
		if (quote != 0 && *read == quote) {
			quote = 0;
			continue;
		}

		/* A backslash (not in '...') takes the next character. */
		if (*read == '\\' && read[1] != '\0' && quote != '\'')
			read++;

		/* A character of the word. */
		*write = *read;
		write++;
	}

	/* The last word ends. */
	*write = '\0';

	/* The words after the option follow. */
	for (position = index + 1; position < *argc; position++) {
		words[count] = (*argv)[position];
		count++;
	}

	/* Succeeded: the new arguments (kept until env ends). */
	words[count] = NULL;
	*argc = count;
	*argv = words;
}

/* Sets name=value in the entries, replacing an entry of the same name. */
static void
set_entry(
	char **entries,
	size_t *count,
	char *assignment)
{
	size_t name_length;
	size_t index;
	int compare;

	/* The name is everything before the first =. */
	name_length = (size_t)(strchr(assignment, '=') - assignment);

	/* An entry of the same name is replaced. */
	for (index = 0; index < *count; index++) {
		compare = strncmp(entries[index], assignment, name_length + 1U);
		if (compare == 0) {
			entries[index] = assignment;
			return;
		}
	}

	/* Succeeded: a new entry at the end. */
	entries[*count] = assignment;
	(*count)++;
	entries[*count] = NULL;
}

/* Takes the entry of a name out of the entries (-u). */
static void
unset_entry(
	char **entries,
	size_t *count,
	const char *name)
{
	size_t name_length;
	size_t index;
	int compare;

	/* The entry whose name is the name, followed by an =. */
	name_length = strlen(name);
	for (index = 0; index < *count; index++) {
		compare = strncmp(entries[index], name, name_length);
		if (compare != 0 || entries[index][name_length] != '=')
			continue;

		/* The last entry takes its place. */
		(*count)--;
		entries[index] = entries[*count];
		entries[*count] = NULL;
		return;
	}
}

/*
 * Runs a utility with the entries as its environment, found through PATH
 * when its name has no slash.  Returns only on failure: 127 when it cannot
 * be found, 126 when it cannot be run.
 */
static int
run_utility(
	char **arguments,
	char **entries)
{
	char path[4096];
	const char *slash;
	int found;

	/* A name with a slash is run as it is. */
	slash = strchr(arguments[0], '/');
	if (slash != NULL) {
		execve(arguments[0], arguments, entries);
	} else {
		found = path_search(arguments[0], path, sizeof(path));
		if (found)
			execve(path, arguments, entries);
		else
			errno = ENOENT;
	}

	/* It could not be run. */
	fprintf(stderr, "env: %s: %s\n", arguments[0], strerror(errno));
	if (errno == ENOENT)
		return 127;

	/* Found but not runnable. */
	return 126;
}

/* Finds an executable file of a name in PATH; returns whether it did. */
static int
path_search(
	const char *name,
	char *path,
	size_t size)
{
	const char *directories;
	const char *start;
	const char *end;
	size_t length;
	int written;
	int usable;

	/* PATH, or the default search path. */
	directories = getenv("PATH");
	if (directories == NULL)
		directories = "/bin:/usr/bin";

	/* Each directory; an empty one is the current directory. */
	start = directories;
	for (;;) {
		end = strchr(start, ':');
		if (end == NULL)
			end = start + strlen(start);
		length = (size_t)(end - start);
		if (length == 0)
			written = snprintf(path, size, "%s", name);
		else
			written = snprintf(path, size, "%.*s/%s", (int)length, start, name);
		if (written > 0 && (size_t)written < size) {
			usable = access(path, X_OK);
			if (usable == 0)
				return 1;
		}

		/* The last directory has been looked at. */
		if (*end == '\0')
			break;
		start = end + 1;
	}

	/* None. */
	return 0;
}

/* Reports the usage and ends env. */
static void
usage(
	void)
{
	/* The form. */
	fprintf(stderr, "usage: env [-i0v] [-u name] [-C directory] [-S string] "
		"[name=value]... [utility [argument...]]\n");
	exit(125);
}
