/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Reading a file into the buffer and writing the buffer back safely.
 */

#include "userland/base/vi/vi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The size of the blocks the buffer is written out in. */
#define FILE_WRITE_BLOCK 65536U

/*
 * The count of what a write put in the file, for the message after it.
 */
struct file_totals {
	size_t lines;
	unsigned long bytes;
};

static int read_whole(int descriptor, size_t size, char **contents, size_t *length);
static int split_lines(struct vi_buffer *buffer, const char *contents, size_t length);
static int write_lines(int descriptor, const struct vi_buffer *buffer, struct file_totals *totals);
static int write_all(int descriptor, const char *bytes, size_t length);
static int write_replacing(const char *target, const struct stat *existing, int exists, const struct vi_buffer *buffer, struct file_totals *totals);
static int write_in_place(const char *target, const struct vi_buffer *buffer, struct file_totals *totals);
static int resolve_target(const char *path, char **target);

/*
 * Reads a file into the buffer, replacing what the buffer held.
 *
 * A file that does not exist gives an empty buffer to start a new file
 * in.  On failure the buffer is unchanged and the message says why.
 * Returns 0 or an errno value.
 */
int
vi_file_load(
	struct vi_editor *editor,
	const char *path)
{
	struct vi_buffer buffer;
	struct stat status;
	char *contents;
	size_t length;
	int descriptor;
	int incomplete;
	int error;
	int result;

	/* The new lines are read aside, so a failure leaves the buffer alone. */
	vi_buffer_init(&buffer);
	contents = NULL;
	length = 0;
	incomplete = 0;

	/* Opens the file; a missing one starts a new file. */
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0) {
		error = errno;
		if (error != ENOENT) {
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
			return error;
		}
	}

	/* Reads an existing file whole. */
	if (descriptor >= 0) {
		/* Learns what the name refers to and how large it is. */
		result = fstat(descriptor, &status);
		if (result != 0) {
			error = errno;
			(void)close(descriptor);
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
			return error;
		}

		/* Only a regular file is edited; a directory or a device is refused. */
		if (!S_ISREG(status.st_mode)) {
			(void)close(descriptor);
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": not a regular file", path);
			return EINVAL;
		}

		/* A file over the limit is refused before it is read. */
		if ((unsigned long)status.st_size > VI_FILE_SIZE_LIMIT) {
			(void)close(descriptor);
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": file too large", path);
			return EFBIG;
		}

		/* Reads the bytes of the file. */
		error = read_whole(descriptor, (size_t)status.st_size, &contents, &length);
		(void)close(descriptor);
		if (error != 0) {
			if (error == EFBIG) {
				(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": file too large", path);
			} else {
				(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
			}
			return error;
		}

		/* A last line without a newline is noted; writing adds the newline. */
		if (length != 0) {
			if (contents[length - 1] != '\n')
				incomplete = 1;
		}
	}

	/* Cuts the bytes into lines; an empty or new file is one empty line. */
	error = split_lines(&buffer, contents, length);
	free(contents);
	if (error != 0) {
		vi_buffer_free(&buffer);
		if (error == EFBIG) {
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": file too large", path);
		} else {
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
		}
		return error;
	}

	/* Replaces the buffer and forgets the history of the old one. */
	vi_buffer_free(&editor->buffer);
	editor->buffer = buffer;
	vi_undo_free(&editor->undo);
	editor->cursor.line = 0;
	editor->cursor.offset = 0;
	editor->want_column = 0;
	editor->top = 0;
	editor->top_skip = 0;
	vi_screen_invalidate(editor);

	/* Tells what was read. */
	if (descriptor < 0) {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\" [New file]", path);
	} else if (incomplete) {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\" %lu lines, %lu bytes [Incomplete last line]",
			       path,
			       (unsigned long)editor->buffer.count,
			       (unsigned long)length);
	} else {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\" %lu lines, %lu bytes",
			       path,
			       (unsigned long)editor->buffer.count,
			       (unsigned long)length);
	}

	/* Succeeded: the buffer holds the file. */
	return 0;
}

/*
 * Writes the buffer to a file.
 *
 * With no path the buffer's own file is written.  Another file that
 * exists, or a file that is not writable, is refused unless force is set.
 * The file is replaced through a new file in the same directory, so a
 * failed write leaves the old contents whole.  Returns 0 or an errno value,
 * with the message saying what happened.
 */
int
vi_file_write(
	struct vi_editor *editor,
	const char *path,
	int force)
{
	struct file_totals totals;
	struct stat existing;
	char *target;
	char *name_copy;
	int is_own_file;
	int exists;
	int result;
	int error;

	/* Uses the buffer's own name when no other is given. */
	if (path == NULL)
		path = editor->file_name;
	if (path == NULL) {
		vi_message(editor, "No file name");
		return EINVAL;
	}

	/* Tells whether this is the file the buffer came from. */
	is_own_file = 0;
	if (editor->file_name != NULL) {
		result = strcmp(path, editor->file_name);
		if (result == 0)
			is_own_file = 1;
	}

	/* Follows a symbolic link to the file it names, which is the file written. */
	target = NULL;
	error = resolve_target(path, &target);
	if (error != 0) {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
		return error;
	}

	/* Learns whether the file exists and what it is. */
	exists = 0;
	result = stat(target, &existing);
	if (result == 0) {
		exists = 1;
	} else if (errno != ENOENT) {
		error = errno;
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": %s", path, strerror(error));
		free(target);
		return error;
	}

	/* An existing file must be a regular file. */
	if (exists) {
		if (!S_ISREG(existing.st_mode)) {
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": not a regular file", path);
			free(target);
			return EINVAL;
		}
	}

	/* Another existing file is overwritten only when asked with !. */
	if (exists && !is_own_file && !force) {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": file exists (add ! to override)", path);
		free(target);
		return EEXIST;
	}

	/* A file without write permission is tried only when asked with !. */
	if (exists && !force) {
		result = access(target, W_OK);
		if (result != 0) {
			(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": file is read-only (add ! to override)", path);
			free(target);
			return EACCES;
		}
	}

	/*
	 * A file with other hard links is written where it is, so every name
	 * keeps showing the same file.  Anything else is replaced whole.
	 */
	memset(&totals, 0, sizeof(totals));
	if (exists && existing.st_nlink > 1) {
		error = write_in_place(target, &editor->buffer, &totals);
	} else {
		error = write_replacing(target, &existing, exists, &editor->buffer, &totals);

		/* A directory vi cannot add a file to still lets the file itself be written. */
		if (exists) {
			if (error == EACCES || error == EROFS || error == EPERM) {
				memset(&totals, 0, sizeof(totals));
				error = write_in_place(target, &editor->buffer, &totals);
			}
		}
	}
	free(target);

	/* Reports a failed write; the message names the file as it was typed. */
	if (error != 0) {
		(void)snprintf(editor->message, sizeof(editor->message), "\"%s\": write failed: %s", path, strerror(error));
		return error;
	}

	/* A buffer without a name takes the name it was written under. */
	if (editor->file_name == NULL) {
		name_copy = strdup(path);
		if (name_copy != NULL) {
			editor->file_name = name_copy;
			is_own_file = 1;
		}
	}

	/* The buffer now matches its file. */
	if (is_own_file)
		vi_undo_mark_saved(editor);

	/* Tells what was written. */
	(void)snprintf(editor->message, sizeof(editor->message), "\"%s\" %lu lines, %lu bytes written",
		       path,
		       (unsigned long)totals.lines,
		       totals.bytes);

	/* Succeeded. */
	return 0;
}

/* Reads a whole file of about the given size into one allocation. */
static int
read_whole(
	int descriptor,
	size_t size,
	char **contents,
	size_t *length)
{
	char *data;
	char *grown;
	size_t capacity;
	size_t used;
	ssize_t count;

	/* Starts with room for the size the file had, plus a little for growth. */
	capacity = size + 4096;
	data = malloc(capacity);
	if (data == NULL)
		return ENOMEM;

	/* Reads until the end of the file, growing if the file grows. */
	used = 0;
	for (;;) {
		/* Grows the storage when it is full, within the limit. */
		if (used == capacity) {
			if (capacity > VI_FILE_SIZE_LIMIT) {
				free(data);
				return EFBIG;
			}

			capacity = capacity * 2;
			grown = realloc(data, capacity);
			if (grown == NULL) {
				free(data);
				return ENOMEM;
			}
			data = grown;
		}

		/* Reads the next part of the file. */
		count = read(descriptor, data + used, capacity - used);
		if (count < 0) {
			/* A signal only interrupts the read. */
			if (errno == EINTR)
				continue;
			free(data);
			return errno;
		}
		if (count == 0)
			break;

		used += (size_t)count;
	}

	/* A file that grew past the limit while it was read is refused too. */
	if (used > VI_FILE_SIZE_LIMIT) {
		free(data);
		return EFBIG;
	}

	*contents = data;
	*length = used;

	/* Succeeded: the caller owns the bytes. */
	return 0;
}

/* Adds the lines of a file's bytes to an empty buffer. */
static int
split_lines(
	struct vi_buffer *buffer,
	const char *contents,
	size_t length)
{
	size_t start;
	size_t index;
	int error;

	/* Adds each line that a newline ends. */
	start = 0;
	for (index = 0; index < length; index++) {
		if (contents[index] == '\n') {
			error = vi_buffer_append_line(buffer, contents + start, index - start);
			if (error != 0)
				return error;
			start = index + 1;
		}
	}

	/* Adds a last line that has no newline. */
	if (start < length) {
		error = vi_buffer_append_line(buffer, contents + start, length - start);
		if (error != 0)
			return error;
	}

	/* An empty file is one empty line. */
	if (buffer->count == 0) {
		error = vi_buffer_append_line(buffer, "", 0);
		if (error != 0)
			return error;
	}

	/* Succeeded. */
	return 0;
}

/* Writes every line with its newline to a descriptor, counting what was written. */
static int
write_lines(
	int descriptor,
	const struct vi_buffer *buffer,
	struct file_totals *totals)
{
	char *block;
	size_t used;
	size_t index;
	const struct vi_line *line;
	int error;

	/* Lines are gathered into large blocks so a big file takes few writes. */
	block = malloc(FILE_WRITE_BLOCK);
	if (block == NULL)
		return ENOMEM;

	/* Writes each line and the newline after it. */
	used = 0;
	for (index = 0; index < buffer->count; index++) {
		line = &buffer->lines[index];

		/* A line that does not fit flushes the block first; a long line goes straight out. */
		if (used + line->length + 1 > FILE_WRITE_BLOCK) {
			error = write_all(descriptor, block, used);
			if (error != 0) {
				free(block);
				return error;
			}
			used = 0;
		}

		if (line->length + 1 > FILE_WRITE_BLOCK) {
			error = write_all(descriptor, line->text, line->length);
			if (error != 0) {
				free(block);
				return error;
			}
		} else {
			memcpy(block + used, line->text, line->length);
			used += line->length;
		}

		/* Every line ends with a newline, the last one included. */
		block[used] = '\n';
		used++;
		totals->lines++;
		totals->bytes += (unsigned long)line->length + 1;
	}

	/* Writes what is left in the block. */
	error = write_all(descriptor, block, used);
	free(block);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Writes all of the bytes, resuming after a signal or a short write. */
static int
write_all(
	int descriptor,
	const char *bytes,
	size_t length)
{
	ssize_t count;

	/* Writes until every byte is out. */
	while (length != 0) {
		count = write(descriptor, bytes, length);
		if (count < 0) {
			/* A signal only interrupts the write. */
			if (errno == EINTR)
				continue;
			return errno;
		}

		bytes += count;
		length -= (size_t)count;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Writes the buffer to a new file next to the target and renames it over
 * the target, keeping the target's permissions and owner.  The target is
 * untouched unless the whole write succeeded.
 */
static int
write_replacing(
	const char *target,
	const struct stat *existing,
	int exists,
	const struct vi_buffer *buffer,
	struct file_totals *totals)
{
	char *temporary;
	const char *slash;
	size_t directory_length;
	size_t size;
	mode_t mode;
	mode_t mask;
	int descriptor;
	int result;
	int error;

	/* The new file is .NAME.vi-XXXXXX in the directory of the target. */
	slash = strrchr(target, '/');
	directory_length = 0;
	if (slash != NULL)
		directory_length = (size_t)(slash - target) + 1;
	size = strlen(target) + 16;
	temporary = malloc(size);
	if (temporary == NULL)
		return ENOMEM;

	memcpy(temporary, target, directory_length);
	(void)snprintf(temporary + directory_length, size - directory_length, ".%s.vi-XXXXXX", target + directory_length);

	/* Creates the new file. */
	descriptor = mkstemp(temporary);
	if (descriptor < 0) {
		error = errno;
		free(temporary);
		return error;
	}

	/* Writes the lines. */
	error = write_lines(descriptor, buffer, totals);
	if (error != 0)
		goto fail;

	/* An existing file keeps its permissions and, where allowed, its owner. */
	if (exists) {
		mode = existing->st_mode & 07777;
		result = fchmod(descriptor, mode);
		if (result != 0) {
			error = errno;
			goto fail;
		}

		/* Only the superuser may give a file away, so a refusal is expected. */
		(void)fchown(descriptor, existing->st_uid, existing->st_gid);
	} else {
		/* A new file gets the permissions any new file would, after the umask. */
		mask = umask(0);
		(void)umask(mask);
		result = fchmod(descriptor, 0666 & ~mask);
		if (result != 0) {
			error = errno;
			goto fail;
		}
	}

	/* Puts the contents on the disk before the name points at them. */
	result = fsync(descriptor);
	if (result != 0) {
		error = errno;
		goto fail;
	}

	/* Closes the new file; a failed close can mean a failed write. */
	result = close(descriptor);
	descriptor = -1;
	if (result != 0) {
		error = errno;
		goto fail;
	}

	/* Replaces the target with the new file in one step. */
	result = rename(temporary, target);
	if (result != 0) {
		error = errno;
		goto fail;
	}

	free(temporary);

	/* Succeeded: the target holds the buffer. */
	return 0;

fail:
	if (descriptor >= 0)
		(void)close(descriptor);
	(void)unlink(temporary);
	free(temporary);
	return error;
}

/*
 * Writes the buffer over the target itself, keeping its inode and its
 * links.  A failure part way leaves the file cut short, which the
 * message after it reports.
 */
static int
write_in_place(
	const char *target,
	const struct vi_buffer *buffer,
	struct file_totals *totals)
{
	int descriptor;
	int result;
	int error;

	/* Opens the file and empties it. */
	descriptor = open(target, O_WRONLY | O_TRUNC);
	if (descriptor < 0)
		return errno;

	/* Writes the lines. */
	error = write_lines(descriptor, buffer, totals);
	if (error != 0) {
		(void)close(descriptor);
		return error;
	}

	/* Puts the contents on the disk. */
	result = fsync(descriptor);
	if (result != 0) {
		error = errno;
		(void)close(descriptor);
		return error;
	}

	/* Closes the file; a failed close can mean a failed write. */
	result = close(descriptor);
	if (result != 0)
		return errno;

	/* Succeeded. */
	return 0;
}

/*
 * Finds the file a name stands for: the name itself, or the file a
 * symbolic link points to, so that writing replaces the file rather than
 * the link.
 */
static int
resolve_target(
	const char *path,
	char **target)
{
	struct stat link_status;
	char *resolved;
	int result;

	/* A name that is not a symbolic link is its own target. */
	result = lstat(path, &link_status);
	if (result != 0 || !S_ISLNK(link_status.st_mode)) {
		*target = strdup(path);
		if (*target == NULL)
			return ENOMEM;
		return 0;
	}

	/* Follows every link in the name to the real file. */
	resolved = realpath(path, NULL);
	if (resolved == NULL)
		return errno;

	*target = resolved;

	/* Succeeded: the caller owns the target name. */
	return 0;
}
