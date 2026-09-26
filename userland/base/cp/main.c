/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: t; -*- */
/* Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib */

/*
 * Copies regular files with explicit creation and metadata policies.
 *
 * The options are read by cp_read_options (ws045): POSIX's -R, -r, -f, -p,
 * GNU's -a, -n, -T, -t directory, -v, --preserve[=list], --update[=WHEN],
 * --attributes-only, the long forms, and zedBSD's --report-file; options
 * may follow operands unless POSIXLY_CORRECT is set.
 */
#include "userland/base/common/command.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One successfully copied hard-link group, owned by the invocation. */
struct copy_link {
	struct copy_link *next;
	struct stat source;
	struct stat destination;
	char *path;
};

/* Copy policy and the shared hard-link registry across source operands. */
struct copy_report {
	int descriptor;
	int failed;
};

/* Copy policy shares invocation-owned link and completion state. */
struct copy_options {
	int literal;
	int exclusive;
	int conflict_fails;
	int attributes_only;
	int preserve_mode;
	int preserve_owner;
	int preserve_times;
	int recursive;
	int preserve_links;
	int force;
	/* -v: each copy is written before it is made. */
	int verbose;
	struct copy_link **links;
	struct copy_report *report;
};

/* The codes of the long options that have no letter. */
#define CP_OPTION_ATTRIBUTES	256
#define CP_OPTION_PRESERVE	257
#define CP_OPTION_NO_PRESERVE	258
#define CP_OPTION_UPDATE	259
#define CP_OPTION_REPORT	260
#define CP_OPTION_HELP		261
#define CP_OPTION_VERSION	262

/*
 * The options written in full.
 *
 * The table is read by the scan of the command line only; the letter a
 * long option shares its code with makes the two forms one case.
 */
static const struct command_long_option cp_long_options[] = {
	{"archive", COMMAND_VALUE_NONE, 'a'},
	{"attributes-only", COMMAND_VALUE_NONE, CP_OPTION_ATTRIBUTES},
	{"force", COMMAND_VALUE_NONE, 'f'},
	{"help", COMMAND_VALUE_NONE, CP_OPTION_HELP},
	{"no-clobber", COMMAND_VALUE_NONE, 'n'},
	{"no-preserve", COMMAND_VALUE_REQUIRED, CP_OPTION_NO_PRESERVE},
	{"no-target-directory", COMMAND_VALUE_NONE, 'T'},
	{"preserve", COMMAND_VALUE_OPTIONAL, CP_OPTION_PRESERVE},
	{"recursive", COMMAND_VALUE_NONE, 'R'},
	{"report-file", COMMAND_VALUE_REQUIRED, CP_OPTION_REPORT},
	{"target-directory", COMMAND_VALUE_REQUIRED, 't'},
	{"update", COMMAND_VALUE_OPTIONAL, CP_OPTION_UPDATE},
	{"verbose", COMMAND_VALUE_NONE, 'v'},
	{"version", COMMAND_VALUE_NONE, CP_OPTION_VERSION},
	{NULL, 0, 0}
};
static int cp_read_options(int argc, char **argv, struct copy_options *options, const char **report_path, const char **target_directory);
static void cp_apply_preserve(struct copy_options *options, const char *list);
static void cp_preserve_word(struct copy_options *options, const char *word, size_t length);
static void cp_apply_update(struct copy_options *options, const char *word);
static void cp_usage(void);
static void copy_verbose(const struct copy_options *options, const char *source, const char *destination);
static const char *leaf(const char *path);
static int copy_file(const char *source, const char *destination,
		     const struct copy_options *options,
		     const char **failed_operand);
static int preserve_file_attributes(int descriptor, const struct stat *source, const struct copy_options *options);
static int copy_tree(const char *source, const char *destination, const struct copy_options *options, unsigned depth);
static int copy_directory(const char *source, const char *destination, const struct stat *from, const struct copy_options *options, unsigned depth);
static int copy_node(const char *source, const char *destination, const struct stat *from, const struct copy_options *options);
static int admit_tree_destination(const char *source, const char *destination);
static int tree_error(const char *path);
static int copy_link_existing(const char *source, const char *destination, const struct stat *from, const struct copy_options *options);
static int copy_link_remember(const char *destination, const struct stat *from, const struct copy_options *options);
static int copy_link_identity(const struct stat *left, const struct stat *right);
static void copy_links_free(struct copy_link *entry);
static int copy_report_record(const struct copy_options *options, const char *source, int directory);
static int copy_report_open(struct copy_report *report, const char *path, int argc, char **argv, int first);
static int copy_report_path(const char *path, char *resolved);

int
main(int argc, char **argv)
{
	struct copy_options options;
	struct copy_link *links;
	struct copy_report report;
	const char *report_path;
	struct stat status;
	const char *operand;
	const char *destination;
	const char *target_directory;
	char target[PATH_MAX + 1];
	char source_name[PATH_MAX + 1];
	size_t source_length;
	int first, index, failed, isdir;
	int copy_status;
	int count;

	memset(&options, 0, sizeof(options));
	links = NULL;
	options.links = &links;
	report.descriptor = -1;
	report.failed = 0;
	options.report = &report;
	report_path = NULL;
	first = 1;
	failed = 0;

	/* The options; the operands follow argv[0], -t's directory after them. */
	target_directory = NULL;
	count = cp_read_options(argc, argv, &options, &report_path, &target_directory);
	argc = first + count;
	if (target_directory != NULL) {
		argv[argc] = (char *)target_directory;
		argc++;
	}
	if (argc - first < 2)
		cp_usage();

	/* Whether the destination is a directory the sources go into. */
	isdir = 0;
	if (!options.literal)
		isdir = stat(argv[argc - 1], &status) == 0 &&
			S_ISDIR(status.st_mode);
	if (target_directory != NULL && !isdir) {
		fprintf(stderr, "cp: target '%s' is not a directory\n",
			target_directory);
		return 1;
	}
	if (argc - first > 2 && !isdir) {
		fprintf(stderr, "cp: destination is not a directory\n");
		return 1;
	}
	/* Open the exclusive report only after excluding all source/target trees. */
	if (report_path != NULL) {
		copy_status = copy_report_open(&report, report_path, argc, argv, first);
		if (copy_status != 0)
			return 1;
	}

	for (index = first; index < argc - 1; index++) {
		/* A failed observer channel must stop further unreportable copies. */
		if (report.failed) {
			failed = 1;
			break;
		}
		destination = argv[argc - 1];
		if (isdir) {
			/* A trailing slash does not erase the source directory's basename. */
			source_length = strlen(argv[index]);
			if (source_length >= sizeof(source_name)) {
				errno = ENAMETOOLONG;
				command_error("cp", argv[index]);
				failed = 1;
				continue;
			}
			memcpy(source_name, argv[index], source_length + 1);
			while (source_length > 1 && source_name[source_length - 1] == '/')
				source_name[--source_length] = '\0';

			if (snprintf(target, sizeof(target), "%s/%s",
				     destination, leaf(source_name)) >=
			    (int)sizeof(target)) {
				errno = ENAMETOOLONG;
				command_error("cp", argv[index]);
				failed = 1;
				continue;
			}
			destination = target;
		}
		/* Tree errors carry their full child path before stack buffers expire. */
		if (options.recursive) {
			if (copy_tree(argv[index], destination, &options, 0) != 0)
				failed = 1;
			continue;
		}

		copy_verbose(&options, argv[index], destination);
		copy_status = copy_file(argv[index], destination, &options, &operand);
		if (copy_status < 0) {
			command_error("cp", operand);
			failed = 1;
		} else if (copy_status == 0) {
			copy_status = copy_report_record(&options, argv[index], 0);
			if (copy_status != 0)
				failed = 1;
		}
	}
	/* The registry never outlives this invocation, including failed operands. */
	copy_links_free(links);

	/* A final marker is valid only together with a successful process exit. */
	if (report.descriptor >= 0) {
		if (!failed && !report.failed) {
			copy_status = command_write_all(report.descriptor, "END\n", 4);
			if (copy_status != 0) {
				command_error("cp", report_path);
				failed = 1;
			}
		}
		copy_status = close(report.descriptor);
		if (copy_status != 0) {
			command_error("cp", report_path);
			failed = 1;
		}
	}
	return failed;
}

/*
 * Reads the options into the copy policy; returns the number of operands,
 * which are left in argv from argv[1] on.  --report-file's path and -t's
 * directory are set when given.
 */
static int
cp_read_options(
	int argc,
	char **argv,
	struct copy_options *options,
	const char **report_path,
	const char **target_directory)
{
	struct command_options scan;
	int code;

	/* The scan of the command line. */
	memset(&scan, 0, sizeof(scan));
	scan.argc = argc;
	scan.argv = argv;
	scan.program = "cp";
	scan.letters = "RrfapnTvt:";
	scan.names = cp_long_options;
	command_options_start(&scan);

	/* Each option in turn. */
	for (;;) {
		code = command_options_next(&scan);
		if (code == COMMAND_OPTION_END)
			break;
		switch (code) {
		case 'R':
		case 'r':
			options->recursive = 1;
			break;
		case 'f':
			options->force = 1;
			break;
		case 'a':
			/* -R and every attribute, and the links between files. */
			options->recursive = 1;
			cp_apply_preserve(options, "all");
			break;
		case 'p':
			cp_apply_preserve(options, NULL);
			break;
		case CP_OPTION_PRESERVE:
			cp_apply_preserve(options, scan.value);
			break;
		case CP_OPTION_NO_PRESERVE:
			/* Nothing is kept that was not asked for. */
			break;
		case 'n':
			options->exclusive = 1;
			options->conflict_fails = 0;
			break;
		case CP_OPTION_UPDATE:
			cp_apply_update(options, scan.value);
			break;
		case 'T':
			options->literal = 1;
			break;
		case 't':
			*target_directory = scan.value;
			break;
		case 'v':
			options->verbose = 1;
			break;
		case CP_OPTION_ATTRIBUTES:
			options->attributes_only = 1;
			break;
		case CP_OPTION_REPORT:
			*report_path = scan.value;
			break;
		case CP_OPTION_VERSION:
			printf("cp (zedBSD) 1.0\n");
			exit(0);
		default:
			cp_usage();
		}
	}

	/* Succeeded: the operands follow argv[0]. */
	return scan.operand_count;
}

/*
 * Applies --preserve[=list]: mode, ownership, timestamps, links and all
 * (the list is words separated by commas); no list is mode, ownership and
 * timestamps, as -p.
 */
static void
cp_apply_preserve(
	struct copy_options *options,
	const char *list)
{
	const char *word;
	size_t length;

	/* No list: -p. */
	if (list == NULL) {
		options->preserve_mode = 1;
		options->preserve_owner = 1;
		options->preserve_times = 1;
		return;
	}

	/* Each word of the list, and the comma after it. */
	for (word = list; *word != '\0'; word += length) {
		length = strcspn(word, ",");
		cp_preserve_word(options, word, length);
		if (word[length] == ',')
			length++;
	}
}

/* Applies one word of --preserve's list. */
static void
cp_preserve_word(
	struct copy_options *options,
	const char *word,
	size_t length)
{
	static const char *const names[] = {"mode", "ownership", "timestamps", "links", "all"};
	size_t index;
	size_t name_length;
	int differs;

	/* The name the word is. */
	for (index = 0; index < 5U; index++) {
		name_length = strlen(names[index]);
		if (name_length != length)
			continue;
		differs = strncmp(word, names[index], length);
		if (differs == 0)
			break;
	}

	/* The attribute of the name; all is every one. */
	switch (index) {
	case 0:
		options->preserve_mode = 1;
		break;
	case 1:
		options->preserve_owner = 1;
		break;
	case 2:
		options->preserve_times = 1;
		break;
	case 3:
		options->preserve_links = 1;
		break;
	case 4:
		options->preserve_mode = 1;
		options->preserve_owner = 1;
		options->preserve_times = 1;
		options->preserve_links = 1;
		break;
	default:
		/* Other attributes are not kept. */
		break;
	}
}

/* Applies --update[=WHEN]: none (-n) and none-fail; all and older copy as usual. */
static void
cp_apply_update(
	struct copy_options *options,
	const char *word)
{
	int differs;

	/* none: never replace, quietly. */
	if (word == NULL)
		return;
	differs = strcmp(word, "none");
	if (differs == 0) {
		options->exclusive = 1;
		options->conflict_fails = 0;
		return;
	}

	/* none-fail: never replace, and fail. */
	differs = strcmp(word, "none-fail");
	if (differs == 0) {
		options->exclusive = 1;
		options->conflict_fails = 1;
	}
}

/* Writes a copy about to be made, with -v, as GNU cp does. */
static void
copy_verbose(
	const struct copy_options *options,
	const char *source,
	const char *destination)
{
	/* Only with -v. */
	if (!options->verbose)
		return;
	printf("'%s' -> '%s'\n", source, destination);
}

/* Reports the usage and ends cp. */
static void
cp_usage(
	void)
{
	/* The forms. */
	fprintf(stderr, "usage: cp [-R|-a] [-fnpTv] [--preserve[=list]] "
		"[--update=none|none-fail] [--attributes-only] [--report-file=path] "
		"source... destination\n"
		"       cp [options] -t directory source...\n");
	exit(1);
}

static const char *
leaf(const char *path)
{
	const char *slash;

	slash = strrchr(path, '/');
	return slash != NULL ? slash + 1 : path;
}

static int
copy_file(const char *source, const char *destination,
	  const struct copy_options *options, const char **failed_operand)
{
	int removed;
	struct stat from, to;
	mode_t mode, previous_mask;
	int input, output, flags, error, saved;
	int skipped;

	input = -1;
	output = -1;
	error = 0;
	skipped = 0;
	*failed_operand = source;

	/* Nonblocking open lets a changed source be rejected before a FIFO
	 * read. */
	flags = O_RDONLY | O_NONBLOCK;
	if (options->recursive)
		flags |= O_NOFOLLOW;
	input = open(source, flags);
	if (input < 0) {
		error = errno;
		goto done;
	}
	if (fstat(input, &from) != 0) {
		error = errno;
		goto done;
	}
	if (!S_ISREG(from.st_mode)) {
		error = EINVAL;
		goto done;
	}

	/* Creation policy is atomic; truncation waits until identities are
	 * checked. */
	*failed_operand = destination;
	flags = O_WRONLY | O_CREAT;
	if (options->recursive)
		flags |= O_NOFOLLOW;
	if (options->exclusive)
		flags |= O_EXCL;
	mode = from.st_mode & (options->preserve_mode ? 07777U : 0777U);
	previous_mask = 0;
	if (options->preserve_mode)
		previous_mask = umask(0);
	output = open(destination, flags, mode);
	saved = errno;

	/* -f: a destination that cannot be opened is removed and made anew. */
	if (output < 0 && options->force && !options->exclusive &&
	    (saved == EACCES || saved == EPERM)) {
		removed = unlink(destination);
		if (removed == 0) {
			output = open(destination, flags, mode);
			saved = errno;
		}
	}
	if (options->preserve_mode)
		(void)umask(previous_mask);
	if (output < 0) {
		if (options->exclusive && !options->conflict_fails && saved == EEXIST)
			skipped = 1;
		else
			error = saved;
		goto done;
	}
	if (fstat(output, &to) != 0) {
		error = errno;
		goto done;
	}
	if (from.st_dev == to.st_dev && from.st_ino == to.st_ino) {
		error = EINVAL;
		goto done;
	}
	if (!options->attributes_only) {
		if (S_ISREG(to.st_mode) && ftruncate(output, 0) != 0) {
			error = errno;
			goto done;
		}
		if (command_copy_fd(input, output) != 0) {
			error = errno != 0 ? errno : EIO;
			goto done;
		}
	}
	/* Ownership precedes final mode, and timestamps follow all content writes. */
	if (preserve_file_attributes(output, &from, options) != 0)
		error = errno;

done:
	/* Each owned descriptor is closed, even when copying or chmod failed.
	 */
	if (output >= 0 && close(output) != 0 && error == 0)
		error = errno;
	if (input >= 0 && close(input) != 0 && error == 0) {
		error = errno;
		*failed_operand = source;
	}
	if (error != 0) {
		errno = error;
		return -1;
	}
	/* Distinguish a preserved destination from a successfully copied file. */
	return skipped;
}

/* Restores the source metadata after writes, without hiding partial failures. */
static int
preserve_file_attributes(
	int descriptor,
	const struct stat *source,
	const struct copy_options *options)
{
	struct timespec times[2];
	int status;

	/* chown may clear set-id bits, so final mode must be applied afterwards. */
	if (options->preserve_owner) {
		status = fchown(descriptor, source->st_uid, source->st_gid);
		if (status != 0)
			return -1;
	}

	/* Restore the requested access and special permission bits. */
	if (options->preserve_mode) {
		status = fchmod(descriptor, source->st_mode & 07777U);
		if (status != 0)
			return -1;
	}

	/* Preserve nanoseconds from the pre-copy source snapshot. */
	if (options->preserve_times) {
		times[0] = source->st_atim;
		times[1] = source->st_mtim;
		status = futimens(descriptor, times);
		if (status != 0)
			return -1;
	}

	/* Succeeded: every requested attribute was applied. */
	return 0;
}

/* Reports the failing child while its pathname is still valid. */
static int
tree_error(
	const char *path)
{
	command_error("cp", path);

	/* The caller must propagate the failed copy without duplicate diagnostics. */
	return -1;
}

/* Refuses same-tree destinations before creating any copied object. */
static int
admit_tree_destination(
	const char *source,
	const char *destination)
{
	char origin[PATH_MAX + 1];
	char target[PATH_MAX + 1];
	char parent[PATH_MAX + 1];
	char resolved_parent[PATH_MAX + 1];
	char *resolved;
	char *slash;
	const char *name;
	size_t length;
	int count;

	/* Resolve the source, including a top-level trailing dot or slash. */
	resolved = realpath(source, origin);
	if (resolved == NULL)
		return -1;

	/* A missing leaf is allowed, but its parent must already exist. */
	resolved = realpath(destination, target);
	if (resolved == NULL) {
		if (errno != ENOENT)
			return -1;

		length = strlen(destination);
		if (length >= sizeof(parent)) {
			errno = ENAMETOOLONG;
			return -1;
		}

		memcpy(parent, destination, length + 1);
		while (length > 1 && parent[length - 1] == '/')
			parent[--length] = '\0';
		slash = strrchr(parent, '/');
		name = parent;
		if (slash == NULL) {
			resolved = realpath(".", resolved_parent);
		} else {
			name = slash + 1;
			*slash = '\0';
			if (slash == parent)
				resolved = realpath("/", resolved_parent);
			else
				resolved = realpath(parent, resolved_parent);
		}

		if (resolved == NULL)
			return -1;

		count = snprintf(target, sizeof(target), "%s/%s", resolved_parent, name);
		if (count < 0 || (size_t)count >= sizeof(target)) {
			errno = ENAMETOOLONG;
			return -1;
		}
	}

	/* A path boundary matters: /tree2 is not below /tree. */
	length = strlen(origin);
	if (strncmp(origin, target, length) == 0) {
		if (length == 1 || target[length] == '\0' || target[length] == '/') {
			errno = EINVAL;
			return -1;
		}
	}

	/* Succeeded: destination is outside the source's directory tree. */
	return 0;
}

/* Dispatches physical tree entries without following symbolic links. */
static int
copy_tree(
	const char *source,
	const char *destination,
	const struct copy_options *options,
	unsigned depth)
{
	struct stat from;
	const char *operand;
	int status;

	/* Bound both path recursion and open directory ownership. */
	if (options->report->failed)
		return -1;

	if (depth >= 128) {
		errno = ELOOP;
		tree_error(source);
		return -1;
	}

	status = lstat(source, &from);
	if (status != 0) {
		tree_error(source);
		return -1;
	}

	/* -v: the copy about to be made. */
	copy_verbose(options, source, destination);

	/* Reuse only a previously completed and still-identical archive object. */
	if (options->preserve_links && !S_ISDIR(from.st_mode) && from.st_nlink > 1) {
		status = copy_link_existing(source, destination, &from, options);
		if (status < 0)
			return -1;
		if (status == 1)
			return 0;
		if (status == 2) {
			status = copy_report_record(options, source, 0);
			if (status != 0)
				return -1;
			return 0;
		}
	}

	/* Directory admission precedes all writes for this source operand. */
	if (S_ISDIR(from.st_mode)) {
		if (depth == 0) {
			status = admit_tree_destination(source, destination);
			if (status != 0) {
				tree_error(destination);
				return -1;
			}
		}

		status = copy_directory(source, destination, &from, options, depth);
	} else if (S_ISREG(from.st_mode)) {
		status = copy_file(source, destination, options, &operand);
		if (status < 0)
			tree_error(operand);
	} else {
		status = copy_node(source, destination, &from, options);
	}

	/* Preserve any content, metadata or traversal failure. */
	if (status < 0)
		return -1;

	/* Skipped names must never become representatives of a source link group. */
	if (status > 0)
		return 0;

	if (options->preserve_links && !S_ISDIR(from.st_mode) && from.st_nlink > 1) {
		status = copy_link_remember(destination, &from, options);
		if (status != 0)
			return -1;
	}

	/* Succeeded: this entry and any descendants were copied. */
	status = copy_report_record(options, source, S_ISDIR(from.st_mode));
	if (status != 0)
		return -1;

	return 0;
}

/* Populates a directory before restoring its final permissions and times. */
static int
copy_directory(
	const char *source,
	const char *destination,
	const struct stat *from,
	const struct copy_options *options,
	unsigned depth)
{
	struct stat target;
	struct stat attributes;
	struct copy_options final_options;
	DIR *stream;
	struct dirent *entry;
	char child_source[PATH_MAX + 1];
	char child_target[PATH_MAX + 1];
	mode_t mask;
	int created;
	int descriptor;
	int failed;
	int status;
	int count;

	/* Capture the normal creation mask, restoring it immediately. */
	mask = umask(0);
	(void)umask(mask);
	created = 0;
	failed = 0;
	status = lstat(destination, &target);
	if (status != 0) {
		if (errno != ENOENT) {
			tree_error(destination);
			return -1;
		}

		/* Temporary owner access allows copying read-only source directories. */
		(void)umask(0);
		status = mkdir(destination, 0700);
		(void)umask(mask);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
		created = 1;
	} else {
		/* Refuse a destination symlink or a source/destination inode alias. */
		if (!S_ISDIR(target.st_mode)) {
			errno = ENOTDIR;
			tree_error(destination);
			return -1;
		}
		if (from->st_dev == target.st_dev && from->st_ino == target.st_ino) {
			errno = EINVAL;
			tree_error(destination);
			return -1;
		}
	}

	/* Walk every child, distinguishing failed readdir from complete EOF. */
	stream = opendir(source);
	if (stream == NULL) {
		tree_error(source);
		return -1;
	}

	while (1) {
		errno = 0;
		entry = readdir(stream);
		if (entry == NULL) {
			if (errno != 0) {
				tree_error(source);
				failed = 1;
			}
			break;
		}

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		count = snprintf(child_source, sizeof(child_source), "%s/%s", source, entry->d_name);
		if (count < 0 || (size_t)count >= sizeof(child_source)) {
			errno = ENAMETOOLONG;
			tree_error(source);
			failed = 1;
			continue;
		}

		count = snprintf(child_target, sizeof(child_target), "%s/%s", destination, entry->d_name);
		if (count < 0 || (size_t)count >= sizeof(child_target)) {
			errno = ENAMETOOLONG;
			tree_error(destination);
			failed = 1;
			continue;
		}

		status = copy_tree(child_source, child_target, options, depth + 1);
		if (status != 0)
			failed = 1;
	}

	/* Close directory enumeration even when a child failed. */
	status = closedir(stream);
	if (status != 0) {
		tree_error(source);
		failed = 1;
	}

	/* Finalize newly created modes or explicitly requested metadata. */
	attributes = *from;
	final_options = *options;
	if (created && !options->preserve_mode) {
		attributes.st_mode &= 0777U & ~mask;
		final_options.preserve_mode = 1;
	}

	descriptor = open(destination, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (descriptor < 0) {
		tree_error(destination);
		return -1;
	}

	status = preserve_file_attributes(descriptor, &attributes, &final_options);
	if (status != 0) {
		tree_error(destination);
		failed = 1;
	}

	status = close(descriptor);
	if (status != 0) {
		tree_error(destination);
		failed = 1;
	}

	/* Do not hide a failed descendant behind successful directory metadata. */
	if (failed)
		return -1;

	/* Succeeded: descendants and final directory attributes are complete. */
	return 0;
}

/* Recreates symbolic links and filesystem nodes without reading their targets. */
static int
copy_node(
	const char *source,
	const char *destination,
	const struct stat *from,
	const struct copy_options *options)
{
	struct stat target;
	struct timespec times[2];
	char link_target[PATH_MAX + 1];
	ssize_t length;
	int status;
	mode_t mask;

	/* Socket endpoints have no meaningful persistent archive representation. */
	if (S_ISSOCK(from->st_mode)) {
		errno = EOPNOTSUPP;
		tree_error(source);
		return -1;
	}

	/* Read the full link before replacing any existing destination name. */
	if (S_ISLNK(from->st_mode)) {
		length = readlink(source, link_target, sizeof(link_target));
		if (length < 0) {
			tree_error(source);
			return -1;
		}
		if ((size_t)length >= sizeof(link_target)) {
			errno = ENAMETOOLONG;
			tree_error(source);
			return -1;
		}
		link_target[length] = '\0';
	}

	/* Existing names obey the same non-clobber policy as regular files. */
	status = lstat(destination, &target);
	if (status == 0) {
		if (options->exclusive) {
			if (!options->conflict_fails)
				return 1;
			errno = EEXIST;
			tree_error(destination);
			return -1;
		}
		if (from->st_dev == target.st_dev && from->st_ino == target.st_ino) {
			errno = EINVAL;
			tree_error(destination);
			return -1;
		}
		status = unlink(destination);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
	} else if (errno != ENOENT) {
		tree_error(destination);
		return -1;
	}

	/* Creation does not open FIFOs or devices and therefore cannot block. */
	if (S_ISLNK(from->st_mode)) {
		status = symlink(link_target, destination);
	} else {
		mask = umask(0);
		(void)umask(mask);
		status = mknod(destination, (from->st_mode & S_IFMT) | (from->st_mode & 0777U & ~mask), from->st_rdev);
	}
	if (status != 0) {
		tree_error(destination);
		return -1;
	}

	/* Ownership is applied without dereferencing a recreated symbolic link. */
	if (options->preserve_owner) {
		status = lchown(destination, from->st_uid, from->st_gid);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
	}

	/* Normal symbolic links already have mode 0777; do not chmod their target. */
	if (options->preserve_mode && (!S_ISLNK(from->st_mode) || (from->st_mode & 07777U) != 0777U)) {
		status = fchmodat(AT_FDCWD, destination, from->st_mode & 07777U, AT_SYMLINK_NOFOLLOW);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
	}

	/* Restore timestamps after node creation and ownership changes. */
	if (options->preserve_times) {
		times[0] = from->st_atim;
		times[1] = from->st_mtim;
		status = utimensat(AT_FDCWD, destination, times, AT_SYMLINK_NOFOLLOW);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
	}

	/* Succeeded: the node and requested attributes were recreated. */
	return 0;
}

/* Compares identity and content-related metadata without volatile link counts. */
static int
copy_link_identity(
	const struct stat *left,
	const struct stat *right)
{
	if (left->st_dev != right->st_dev || left->st_ino != right->st_ino)
		return 0;
	if (left->st_mode != right->st_mode || left->st_size != right->st_size)
		return 0;
	if (left->st_uid != right->st_uid || left->st_gid != right->st_gid)
		return 0;
	if (left->st_mtim.tv_sec != right->st_mtim.tv_sec || left->st_mtim.tv_nsec != right->st_mtim.tv_nsec)
		return 0;

	/* Succeeded: reads and additional hard links cannot invalidate this match. */
	return 1;
}

/* Links another name to a verified representative; zero requests a normal copy. */
static int
copy_link_existing(
	const char *source,
	const char *destination,
	const struct stat *from,
	const struct copy_options *options)
{
	struct copy_link *entry;
	struct stat current;
	int status;

	/* Find the source group across directories and independent source operands. */
	for (entry = *options->links; entry != NULL; entry = entry->next) {
		if (entry->source.st_dev == from->st_dev && entry->source.st_ino == from->st_ino)
			break;
	}
	if (entry == NULL)
		return 0;

	status = copy_link_identity(from, &entry->source);
	if (!status) {
		errno = ESTALE;
		tree_error(source);
		return -1;
	}
	status = lstat(entry->path, &current);
	if (status != 0) {
		tree_error(entry->path);
		return -1;
	}
	status = copy_link_identity(&current, &entry->destination);
	if (!status) {
		errno = ESTALE;
		tree_error(entry->path);
		return -1;
	}

	/* Do not replace a no-clobber destination or any original source inode. */
	status = lstat(destination, &current);
	if (status == 0) {
		if (options->exclusive) {
			if (!options->conflict_fails)
				return 1;
			errno = EEXIST;
			tree_error(destination);
			return -1;
		}
		if (current.st_dev == entry->destination.st_dev && current.st_ino == entry->destination.st_ino)
			return 2;
		if (current.st_dev == from->st_dev && current.st_ino == from->st_ino) {
			errno = EINVAL;
			tree_error(destination);
			return -1;
		}
		status = unlink(destination);
		if (status != 0) {
			tree_error(destination);
			return -1;
		}
	} else if (errno != ENOENT) {
		tree_error(destination);
		return -1;
	}

	/* Verify the created link rather than trusting a stale representative path. */
	status = link(entry->path, destination);
	if (status != 0) {
		tree_error(destination);
		return -1;
	}
	status = lstat(destination, &current);
	if (status != 0) {
		tree_error(destination);
		return -1;
	}
	status = copy_link_identity(&current, &entry->destination);
	if (!status) {
		errno = ESTALE;
		tree_error(destination);
		return -1;
	}

	/* Succeeded: this additional name refers to the completed copied object. */
	return 2;
}

/* Retains only completed destinations as representatives of hard-link groups. */
static int
copy_link_remember(
	const char *destination,
	const struct stat *from,
	const struct copy_options *options)
{
	struct copy_link *entry;
	int status;

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		tree_error(destination);
		return -1;
	}
	entry->path = strdup(destination);
	if (entry->path == NULL) {
		free(entry);
		tree_error(destination);
		return -1;
	}
	status = lstat(destination, &entry->destination);
	if (status != 0) {
		free(entry->path);
		free(entry);
		tree_error(destination);
		return -1;
	}
	entry->source = *from;
	entry->next = *options->links;
	*options->links = entry;

	/* Succeeded: the invocation owns this representative until final cleanup. */
	return 0;
}

/* Releases all path and identity records after success or failure. */
static void
copy_links_free(
	struct copy_link *entry)
{
	struct copy_link *next;

	while (entry != NULL) {
		next = entry->next;
		free(entry->path);
		free(entry);
		entry = next;
	}
}

/* Canonicalizes an existing path or a missing leaf under an existing parent. */
static int
copy_report_path(
	const char *path,
	char *resolved)
{
	char parent[PATH_MAX + 1];
	char canonical[PATH_MAX + 1];
	char *result;
	char *slash;
	const char *leaf_name;
	size_t length;
	int count;

	result = realpath(path, resolved);
	if (result != NULL)
		return 0;
	if (errno != ENOENT)
		return -1;

	length = strlen(path);
	if (length == 0 || length > PATH_MAX) {
		errno = EINVAL;
		return -1;
	}
	memcpy(parent, path, length + 1);
	while (length > 1 && parent[length - 1] == '/')
		parent[--length] = '\0';
	slash = strrchr(parent, '/');
	leaf_name = parent;
	if (slash == NULL) {
		result = realpath(".", canonical);
	} else {
		leaf_name = slash + 1;
		*slash = '\0';
		if (slash == parent)
			result = realpath("/", canonical);
		else
			result = realpath(parent, canonical);
	}
	if (result == NULL)
		return -1;
	count = snprintf(resolved, PATH_MAX + 1, "%s%s%s", canonical,
		strcmp(canonical, "/") == 0 ? "" : "/", leaf_name);
	if (count < 0 || count > PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* Succeeded: the missing leaf has an unambiguous existing parent. */
	return 0;
}

/* Creates a report outside all copied paths without overwriting any object. */
static int
copy_report_open(
	struct copy_report *report,
	const char *path,
	int argc,
	char **argv,
	int first)
{
	char output[PATH_MAX + 1];
	char root[PATH_MAX + 1];
	size_t length;
	int index;
	int status;

	status = copy_report_path(path, output);
	if (status != 0) {
		tree_error(path);
		return -1;
	}
	/* The destination operand also excludes an entire existing container. */
	for (index = first; index < argc; index++) {
		status = copy_report_path(argv[index], root);
		if (status != 0) {
			tree_error(argv[index]);
			return -1;
		}
		length = strlen(root);
		if (strncmp(output, root, length) == 0) {
			if (length == 1 || output[length] == '\0' || output[length] == '/') {
				errno = EINVAL;
				tree_error(path);
				return -1;
			}
		}
	}
	status = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (status < 0) {
		tree_error(path);
		return -1;
	}
	report->descriptor = status;
	status = command_write_all(report->descriptor, "CPCOPY1\n", 8);
	if (status != 0) {
		tree_error(path);
		(void)close(report->descriptor);
		report->descriptor = -1;
		return -1;
	}

	/* Succeeded: only this invocation can write completion records. */
	return 0;
}

/* Emits a bounded record only after content, metadata and close have succeeded. */
static int
copy_report_record(
	const struct copy_options *options,
	const char *source,
	int directory)
{
	static const char hex[] = "0123456789abcdef";
	char record[2 * PATH_MAX + 4];
	size_t length;
	size_t index;
	unsigned byte;
	int status;

	/* Ordinary cp has no report overhead beyond this disabled check. */
	if (options->report->descriptor < 0)
		return 0;
	if (options->report->failed)
		return -1;
	length = strlen(source);
	if (length > PATH_MAX) {
		errno = ENAMETOOLONG;
		options->report->failed = 1;
		tree_error(source);
		return -1;
	}
	record[0] = directory ? 'D' : 'F';
	record[1] = '\t';
	/* Hex encoding prevents filename newlines from masquerading as records. */
	for (index = 0; index < length; index++) {
		byte = (unsigned char)source[index];
		record[2 + index * 2] = hex[byte >> 4];
		record[3 + index * 2] = hex[byte & 15];
	}
	record[2 + length * 2] = '\n';
	status = command_write_all(options->report->descriptor, record, 3 + length * 2);
	if (status != 0) {
		options->report->failed = 1;
		tree_error("completion report");
		return -1;
	}

	/* Succeeded: the caller can count one completed file or directory. */
	return 0;
}
