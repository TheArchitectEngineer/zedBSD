/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The journal of Notes: every change since the last save, on disk as soon
 * as it is made, so that a crash or a killed process loses nothing.
 *
 * One journal belongs to one document file and lives in
 * $XDG_DATA_HOME/keiland/notes/ (or ~/.local/share/keiland/notes/), named
 * after a hash of the document's absolute path.  It does not exist while
 * the document is as it was saved.  The first change after a save writes
 * a new journal (to a temporary name, then renamed over the old one): the
 * header with the document's path, a snapshot of the whole document as
 * edit data (encode.c), and the change.  Every later change is appended
 * and flushed to the disk before the document applies it.  A save removes
 * the journal.
 *
 * A record is a type byte, a 32-bit length, the body and a 32-bit FNV-1a
 * checksum of the three.  Recovery reads the snapshot and replays the
 * records up to the first one that is incomplete or damaged (the one a
 * crash cut short).  The snapshot makes recovery independent of the saved
 * PDF: it works before Notes can read PDFs back.
 */

#include "notes.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The magic and version of a journal file. */
#define JOURNAL_MAGIC		"ZNJL"
#define JOURNAL_VERSION		1U

/* The record types. */
#define JOURNAL_SNAPSHOT	1U
#define JOURNAL_ADD_STROKE	2U
#define JOURNAL_REMOVE_STROKE	3U
#define JOURNAL_ADD_PAGE	4U
#define JOURNAL_REMOVE_PAGE	5U

/* The file name's ending. */
#define JOURNAL_SUFFIX		".journal"

/* The longest path a journal keeps, and the largest journal read back. */
#define JOURNAL_PATH_MAX	4096U
#define JOURNAL_SIZE_MAX	(512U * 1024U * 1024U)

/*
 * The journal of one document file.
 *
 * It lives as long as Notes shows the document.  descriptor is -1 while no
 * journal file is open: before the first change, and after a save or a
 * failure removed it.
 */
struct notes_journal {
	char document_path[JOURNAL_PATH_MAX];
	char path[JOURNAL_PATH_MAX];
	int descriptor;
};

static int journal_start(struct notes_journal *journal, const struct notes_document *document);
static int journal_append(struct notes_journal *journal, const struct notes_document *document, unsigned type, const struct notes_buffer *body);
static void journal_record(struct notes_buffer *out, unsigned type, const unsigned char *body, size_t length);
static uint32_t journal_checksum(const unsigned char *data, size_t length);
static int journal_make_folder(const char *path);
static int write_all(int descriptor, const unsigned char *data, size_t length);
static int read_file(const char *path, unsigned char **data, size_t *size);
static int replay(struct notes_document *document, unsigned type, const unsigned char *body, size_t length);
static int read_number(const unsigned char *body, size_t length, size_t *offset, uint64_t *value);

/*
 * Writes the folder the journals live in: $XDG_DATA_HOME/keiland/notes, or
 * ~/.local/share/keiland/notes.
 *
 * Returns 0, or ENAMETOOLONG, or ENOENT when neither variable is set.
 */
int
notes_journal_folder(
	char *path,
	size_t size)
{
	const char *base;
	const char *home;
	int written;

	/* $XDG_DATA_HOME when it is set. */
	base = getenv("XDG_DATA_HOME");
	if (base != NULL && base[0] == '/') {
		written = snprintf(path, size, "%s/keiland/notes", base);
	} else {
		/* Otherwise under the home directory. */
		home = getenv("HOME");
		if (home == NULL || home[0] != '/')
			return ENOENT;
		written = snprintf(path, size, "%s/.local/share/keiland/notes", home);
	}

	/* A path that did not fit is refused. */
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the folder's path. */
	return 0;
}

/*
 * Writes the path of the journal of a document file (an absolute path).
 *
 * Returns 0, ENOENT or ENAMETOOLONG.
 */
int
notes_journal_path(
	const char *document_path,
	char *path,
	size_t size)
{
	char folder[JOURNAL_PATH_MAX];
	uint64_t hash;
	const unsigned char *byte;
	int written;
	int error;

	/* The folder. */
	error = notes_journal_folder(folder, sizeof(folder));
	if (error != 0)
		return error;

	/* The 64-bit FNV-1a hash of the document's path names the journal. */
	hash = 0xcbf29ce484222325ULL;
	for (byte = (const unsigned char *)document_path; *byte != '\0'; byte++) {
		hash ^= *byte;
		hash *= 0x100000001b3ULL;
	}

	/* The folder, the hash in hexadecimal and the ending. */
	written = snprintf(path, size, "%s/%08lx%08lx%s", folder, (unsigned long)(hash >> 32), (unsigned long)(hash & 0xffffffffU), JOURNAL_SUFFIX);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the journal's path. */
	return 0;
}

/*
 * Makes the journal of a document file; nothing is written until the first change.
 *
 * Returns NULL with errno set when the path is too long or has no folder.
 */
struct notes_journal *
notes_journal_create(
	const char *document_path)
{
	struct notes_journal *journal;
	size_t length;
	int error;

	/* A path that does not fit is refused. */
	length = strlen(document_path);
	if (length >= JOURNAL_PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	/* Allocates the journal. */
	journal = calloc(1U, sizeof(*journal));
	if (journal == NULL)
		return NULL;

	/* The document's path and the journal's. */
	memcpy(journal->document_path, document_path, length + 1U);
	error = notes_journal_path(document_path, journal->path, sizeof(journal->path));
	if (error != 0) {
		free(journal);
		errno = error;
		return NULL;
	}

	/* Succeeded: no file is open yet. */
	journal->descriptor = -1;
	return journal;
}

/*
 * Closes the journal's file, which stays on the disk, and frees it.
 */
void
notes_journal_destroy(
	struct notes_journal *journal)
{
	/* A missing journal frees nothing. */
	if (journal == NULL)
		return;

	/* The file, when one is open. */
	if (journal->descriptor >= 0)
		(void)close(journal->descriptor);
	free(journal);
}

/*
 * Tells the path of the document the journal belongs to.
 */
const char *
notes_journal_document_path(
	const struct notes_journal *journal)
{
	/* Reports the path given at creation. */
	return journal->document_path;
}

/*
 * Logs a stroke put on a page at a place.
 *
 * Returns 0, or an errno value (the change must then not be made).
 */
int
notes_journal_add_stroke(
	struct notes_journal *journal,
	const struct notes_document *document,
	size_t page,
	size_t place,
	const struct notes_stroke *stroke)
{
	struct notes_buffer body;
	int error;

	/* The page, the place and the stroke. */
	notes_buffer_init(&body);
	notes_buffer_varint(&body, page);
	notes_buffer_varint(&body, place);
	error = notes_encode_stroke(stroke, &body);
	if (error != 0) {
		notes_buffer_free(&body);
		return error;
	}

	/* Appends the record. */
	error = journal_append(journal, document, JOURNAL_ADD_STROKE, &body);
	notes_buffer_free(&body);
	if (error != 0)
		return error;

	/* Succeeded: the record is on the disk. */
	return 0;
}

/*
 * Logs a stroke taken off a page by its number.
 */
int
notes_journal_remove_stroke(
	struct notes_journal *journal,
	const struct notes_document *document,
	size_t page,
	uint32_t id)
{
	struct notes_buffer body;
	int error;

	/* The page and the number. */
	notes_buffer_init(&body);
	notes_buffer_varint(&body, page);
	notes_buffer_varint(&body, id);

	/* Appends the record. */
	error = journal_append(journal, document, JOURNAL_REMOVE_STROKE, &body);
	notes_buffer_free(&body);
	if (error != 0)
		return error;

	/* Succeeded: the record is on the disk. */
	return 0;
}

/*
 * Logs a blank page put in at an index.
 */
int
notes_journal_add_page(
	struct notes_journal *journal,
	const struct notes_document *document,
	size_t index)
{
	struct notes_buffer body;
	int error;

	/* The index. */
	notes_buffer_init(&body);
	notes_buffer_varint(&body, index);

	/* Appends the record. */
	error = journal_append(journal, document, JOURNAL_ADD_PAGE, &body);
	notes_buffer_free(&body);
	if (error != 0)
		return error;

	/* Succeeded: the record is on the disk. */
	return 0;
}

/*
 * Logs the page at an index taken out.
 */
int
notes_journal_remove_page(
	struct notes_journal *journal,
	const struct notes_document *document,
	size_t index)
{
	struct notes_buffer body;
	int error;

	/* The index. */
	notes_buffer_init(&body);
	notes_buffer_varint(&body, index);

	/* Appends the record. */
	error = journal_append(journal, document, JOURNAL_REMOVE_PAGE, &body);
	notes_buffer_free(&body);
	if (error != 0)
		return error;

	/* Succeeded: the record is on the disk. */
	return 0;
}

/*
 * Removes the journal's file: the document is saved and needs no journal.
 *
 * The next change starts a new one.  Returns 0, or the errno value of a
 * removal that failed for another reason than the file being absent.
 */
int
notes_journal_discard(
	struct notes_journal *journal)
{
	int status;

	/* The open file is closed first. */
	if (journal->descriptor >= 0) {
		(void)close(journal->descriptor);
		journal->descriptor = -1;
	}

	/* Removes the file; one that is not there is already discarded. */
	status = unlink(journal->path);
	if (status != 0 && errno != ENOENT)
		return errno;

	/* Succeeded: no journal is left. */
	return 0;
}

/*
 * Rebuilds a document from a journal file.
 *
 * The document is made here from the journal's snapshot, then every intact
 * record is replayed onto it; *records tells how many were.  The path of
 * the journal's document is written to document_path.  Returns 0, ENOENT
 * when there is no journal, EINVAL for a file that is not a journal or has
 * no snapshot, or ENOMEM.
 */
int
notes_journal_recover(
	const char *journal_path,
	struct notes_document *document,
	char *document_path,
	size_t size,
	size_t *records)
{
	unsigned char *data;
	size_t length;
	size_t offset;
	size_t path_length;
	size_t body_length;
	uint32_t stored;
	uint32_t computed;
	unsigned type;
	int have_snapshot;
	int matched;
	int error;

	/* Reads the whole file. */
	*records = 0;
	data = NULL;
	length = 0;
	error = read_file(journal_path, &data, &length);
	if (error != 0)
		return error;

	/* The header: the magic, then the version this file writes. */
	matched = -1;
	if (length >= 12U)
		matched = memcmp(data, JOURNAL_MAGIC, 4U);
	if (matched != 0 ||
	    data[4] != JOURNAL_VERSION ||
	    data[5] != 0U) {
		free(data);
		return EINVAL;
	}

	/* The document's path follows the header. */
	path_length = (size_t)data[8] | ((size_t)data[9] << 8) | ((size_t)data[10] << 16) | ((size_t)data[11] << 24);
	if (path_length > length - 12U || path_length >= size) {
		free(data);
		return EINVAL;
	}

	/* The path, and the records after it. */
	memcpy(document_path, data + 12U, path_length);
	document_path[path_length] = '\0';
	offset = 12U + path_length;

	/* Each record whose bytes are all there and whose checksum holds. */
	have_snapshot = 0;
	while (length - offset >= 9U) {
		/* The type and the body's length, which must fit with the checksum. */
		type = data[offset];
		body_length = (size_t)data[offset + 1U] | ((size_t)data[offset + 2U] << 8) | ((size_t)data[offset + 3U] << 16) | ((size_t)data[offset + 4U] << 24);
		if (body_length > length - offset - 9U)
			break;

		/* The checksum over the type, the length and the body. */
		stored = (uint32_t)data[offset + 5U + body_length];
		stored |= (uint32_t)data[offset + 6U + body_length] << 8;
		stored |= (uint32_t)data[offset + 7U + body_length] << 16;
		stored |= (uint32_t)data[offset + 8U + body_length] << 24;
		computed = journal_checksum(data + offset, 5U + body_length);
		if (stored != computed)
			break;

		/* The first record is the snapshot the others change. */
		if (!have_snapshot) {
			if (type != JOURNAL_SNAPSHOT)
				break;
			error = notes_decode_document(data + offset + 5U, body_length, document);
			if (error != 0) {
				free(data);
				return error;
			}

			/* The records that follow change it. */
			have_snapshot = 1;
		} else {
			/* A later record is a change; one that does not apply ends the replay. */
			error = replay(document, type, data + offset + 5U, body_length);
			if (error != 0)
				break;
		}

		/* The next record. */
		(*records)++;
		offset += 9U + body_length;
	}

	/* A journal without its snapshot rebuilds nothing. */
	free(data);
	if (!have_snapshot)
		return EINVAL;

	/* Succeeded: the document as it was when the journal was last written. */
	document->dirty = 1;
	return 0;
}

/*
 * Finds the most recently written journal in the folder.
 *
 * Returns 0 with its path, or ENOENT when there is none.
 */
int
notes_journal_newest(
	char *path,
	size_t size)
{
	char folder[JOURNAL_PATH_MAX];
	char candidate[JOURNAL_PATH_MAX];
	struct dirent *entry;
	struct stat status;
	DIR *directory;
	size_t name_length;
	size_t suffix_length;
	time_t newest;
	int matched;
	int found;
	int written;
	int error;

	/* The folder, which may not exist yet. */
	error = notes_journal_folder(folder, sizeof(folder));
	if (error != 0)
		return error;
	directory = opendir(folder);
	if (directory == NULL)
		return ENOENT;

	/* Each file whose name ends like a journal's. */
	found = 0;
	newest = 0;
	suffix_length = strlen(JOURNAL_SUFFIX);
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;

		/* Only journals: a name longer than the ending, that ends with it. */
		name_length = strlen(entry->d_name);
		if (name_length <= suffix_length)
			continue;
		matched = strcmp(entry->d_name + name_length - suffix_length, JOURNAL_SUFFIX);
		if (matched != 0)
			continue;

		/* Its time; the newest wins. */
		written = snprintf(candidate, sizeof(candidate), "%s/%s", folder, entry->d_name);
		if (written < 0 || (size_t)written >= sizeof(candidate))
			continue;
		error = stat(candidate, &status);
		if (error != 0)
			continue;
		if (found && status.st_mtime < newest)
			continue;
		if ((size_t)written >= size)
			continue;
		memcpy(path, candidate, (size_t)written + 1U);
		newest = status.st_mtime;
		found = 1;
	}

	/* The folder is not read again. */
	(void)closedir(directory);

	/* No journal. */
	if (!found)
		return ENOENT;

	/* Succeeded: the path of the newest journal. */
	return 0;
}

/* Writes a new journal: the header and a snapshot of the document, then keeps it open for appending. */
static int
journal_start(
	struct notes_journal *journal,
	const struct notes_document *document)
{
	char temporary[JOURNAL_PATH_MAX + 8U];
	struct notes_buffer snapshot;
	struct notes_buffer file;
	size_t path_length;
	int descriptor;
	int status;
	int error;

	/* The folder, made when it is missing. */
	error = journal_make_folder(journal->path);
	if (error != 0)
		return error;

	/* The snapshot of the document as it stands before the change. */
	notes_buffer_init(&snapshot);
	error = notes_encode_document(document, &snapshot);
	if (error != 0) {
		notes_buffer_free(&snapshot);
		return error;
	}

	/* The header and the snapshot's record. */
	notes_buffer_init(&file);
	path_length = strlen(journal->document_path);
	notes_buffer_bytes(&file, JOURNAL_MAGIC, 4U);
	notes_buffer_u16(&file, JOURNAL_VERSION);
	notes_buffer_u16(&file, 0U);
	notes_buffer_u32(&file, (uint32_t)path_length);
	notes_buffer_bytes(&file, journal->document_path, path_length);
	journal_record(&file, JOURNAL_SNAPSHOT, snapshot.data, snapshot.length);
	notes_buffer_free(&snapshot);
	if (file.error != 0) {
		error = file.error;
		notes_buffer_free(&file);
		return error;
	}

	/* Writes them to a temporary file. */
	(void)snprintf(temporary, sizeof(temporary), "%s.tmp", journal->path);
	descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0) {
		error = errno;
		notes_buffer_free(&file);
		return error;
	}

	/* The bytes. */
	error = write_all(descriptor, file.data, file.length);
	notes_buffer_free(&file);

	/* Puts the bytes on the disk before the name. */
	if (error == 0) {
		status = fsync(descriptor);
		if (status != 0)
			error = errno;
	}

	/* The file is closed whatever happened. */
	(void)close(descriptor);
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* The new journal replaces any old one in one step. */
	status = rename(temporary, journal->path);
	if (status != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Opens it for the records that follow. */
	journal->descriptor = open(journal->path, O_WRONLY | O_APPEND);
	if (journal->descriptor < 0)
		return errno;

	/* Succeeded: the journal holds the document as it stands. */
	return 0;
}

/* Appends one record, starting the journal first when there is none, and flushes it to the disk. */
static int
journal_append(
	struct notes_journal *journal,
	const struct notes_document *document,
	unsigned type,
	const struct notes_buffer *body)
{
	struct notes_buffer record;
	int status;
	int error;

	/* A body that failed to build is not written. */
	if (body->error != 0)
		return body->error;

	/* The journal file, when the change is the first since a save. */
	if (journal->descriptor < 0) {
		error = journal_start(journal, document);
		if (error != 0)
			return error;
	}

	/* The record. */
	notes_buffer_init(&record);
	journal_record(&record, type, body->data, body->length);
	if (record.error != 0) {
		error = record.error;
		notes_buffer_free(&record);
		return error;
	}

	/* Writes it and puts it on the disk. */
	error = write_all(journal->descriptor, record.data, record.length);
	notes_buffer_free(&record);
	if (error != 0)
		return error;
	status = fsync(journal->descriptor);
	if (status != 0)
		return errno;

	/* Succeeded: the change survives a crash from now on. */
	return 0;
}

/* Appends a record: type, length, body and checksum. */
static void
journal_record(
	struct notes_buffer *out,
	unsigned type,
	const unsigned char *body,
	size_t length)
{
	size_t start;
	uint32_t checksum;

	/* The type, the length and the body. */
	start = out->length;
	notes_buffer_u8(out, type);
	notes_buffer_u32(out, (uint32_t)length);
	notes_buffer_bytes(out, body, length);
	if (out->error != 0)
		return;

	/* The checksum of the three. */
	checksum = journal_checksum(out->data + start, out->length - start);
	notes_buffer_u32(out, checksum);
}

/* Computes the 32-bit FNV-1a hash of bytes. */
static uint32_t
journal_checksum(
	const unsigned char *data,
	size_t length)
{
	uint32_t hash;
	size_t index;

	/* Each byte folds into the hash. */
	hash = 0x811c9dc5U;
	for (index = 0; index < length; index++) {
		hash ^= data[index];
		hash *= 0x01000193U;
	}

	/* Reports the hash. */
	return hash;
}

/* Makes the folders of a file's path that are missing, private to the user. */
static int
journal_make_folder(
	const char *path)
{
	char folder[JOURNAL_PATH_MAX];
	char *slash;
	size_t length;
	int status;

	/* The file's folder. */
	length = strlen(path);
	if (length >= sizeof(folder))
		return ENAMETOOLONG;
	memcpy(folder, path, length + 1U);
	slash = strrchr(folder, '/');
	if (slash == NULL || slash == folder)
		return 0;
	*slash = '\0';

	/* Each folder from the root down; one that exists is passed. */
	for (slash = folder + 1; *slash != '\0'; slash++) {
		if (*slash != '/')
			continue;
		*slash = '\0';
		status = mkdir(folder, 0700);
		*slash = '/';
		if (status != 0 && errno != EEXIST)
			return errno;
	}

	/* The last folder. */
	status = mkdir(folder, 0700);
	if (status != 0 && errno != EEXIST)
		return errno;

	/* Succeeded: the folder is there. */
	return 0;
}

/* Writes every byte, going on after interruptions and short writes. */
static int
write_all(
	int descriptor,
	const unsigned char *data,
	size_t length)
{
	ssize_t written;

	/* Until nothing is left. */
	while (length > 0U) {
		written = write(descriptor, data, length);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return EIO;
		data += written;
		length -= (size_t)written;
	}

	/* Succeeded: every byte is written. */
	return 0;
}

/* Reads a whole file into memory the caller frees. */
static int
read_file(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	struct stat status;
	unsigned char *bytes;
	ssize_t got;
	size_t done;
	int descriptor;
	int error;

	/* Opens it. */
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
		return errno;

	/* Its size, which must be sane. */
	error = fstat(descriptor, &status);
	if (error != 0 ||
	    status.st_size < 0 ||
	    (unsigned long)status.st_size > JOURNAL_SIZE_MAX) {
		(void)close(descriptor);
		return EINVAL;
	}

	/* Allocates the bytes (at least one, for an empty file). */
	bytes = malloc((size_t)status.st_size + 1U);
	if (bytes == NULL) {
		(void)close(descriptor);
		return ENOMEM;
	}

	/* Reads until the end. */
	done = 0;
	while (done < (size_t)status.st_size) {
		got = read(descriptor, bytes + done, (size_t)status.st_size - done);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		done += (size_t)got;
	}

	/* The file is closed whatever happened. */
	(void)close(descriptor);

	/* Succeeded: what could be read. */
	*data = bytes;
	*size = done;
	return 0;
}

/* Applies one recorded change to the document. */
static int
replay(
	struct notes_document *document,
	unsigned type,
	const unsigned char *body,
	size_t length)
{
	struct notes_stroke *stroke;
	struct notes_page *page;
	uint64_t first;
	uint64_t second;
	size_t offset;
	size_t used;
	size_t place;
	int error;

	/* Every record starts with one or two numbers. */
	offset = 0;
	error = read_number(body, length, &offset, &first);
	if (error != 0)
		return error;

	/* Each type applies its change with the document's primitives. */
	switch (type) {
	case JOURNAL_ADD_STROKE:
		/* The page, the place and the stroke. */
		error = read_number(body, length, &offset, &second);
		if (error != 0)
			return error;
		error = notes_decode_stroke(body + offset, length - offset, &used, &stroke);
		if (error != 0)
			return error;
		error = notes_document_insert_stroke(document, (size_t)first, (size_t)second, stroke);
		if (error != 0) {
			notes_stroke_free(stroke);
			return error;
		}

		break;
	case JOURNAL_REMOVE_STROKE:
		/* The page and the number. */
		error = read_number(body, length, &offset, &second);
		if (error != 0)
			return error;
		stroke = notes_document_remove_stroke(document, (size_t)first, (uint32_t)second, &place);
		if (stroke == NULL)
			return EINVAL;
		notes_stroke_free(stroke);
		break;
	case JOURNAL_ADD_PAGE:
		/* A blank page the size of the first. */
		page = notes_page_create(document->pages[0]->width, document->pages[0]->height, NOTES_BACKGROUND_PLAIN);
		if (page == NULL)
			return ENOMEM;
		error = notes_document_insert_page(document, (size_t)first, page);
		if (error != 0) {
			notes_page_free(page);
			return error;
		}

		break;
	case JOURNAL_REMOVE_PAGE:
		/* The page at the index. */
		page = notes_document_remove_page(document, (size_t)first);
		if (page == NULL)
			return EINVAL;
		notes_page_free(page);
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the change is applied. */
	return 0;
}

/* Reads one LEB128 number from a record's body. */
static int
read_number(
	const unsigned char *body,
	size_t length,
	size_t *offset,
	uint64_t *value)
{
	unsigned shift;
	unsigned byte;

	/* Seven bits a byte, low first, while the continuation bit is set. */
	*value = 0U;
	for (shift = 0; shift < 64U; shift += 7U) {
		if (*offset >= length)
			return EINVAL;
		byte = body[*offset];
		(*offset)++;
		*value |= (uint64_t)(byte & 0x7fU) << shift;
		if ((byte & 0x80U) == 0U)
			return 0;
	}

	/* A number longer than 64 bits is malformed. */
	return EINVAL;
}
