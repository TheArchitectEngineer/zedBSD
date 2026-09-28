/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF reader of libpdf.
 *
 * This first reader reads the documents libpdf's writer produces
 * (plan/ws079/design-pdf.md sections 1 to 3): a classic cross-reference
 * table, possibly followed by older ones through /Prev, indirect objects
 * found through it, the page tree, the attached edit data and the page
 * content streams, uncompressed.  A cross-reference stream, a compressed
 * stream and encryption are reported as ENOTSUP.
 *
 * The file is not trusted.  The whole file is held in memory, every offset
 * and length read from it is checked against its size, objects are loaded
 * once and a loop of objects that need each other is refused, and the page
 * and name trees are walked at most once per node and to a bounded depth.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha2.h>
#include <time.h>

#include <pdf.h>

#include "internal.h"

/* The largest file the reader opens. */
#define PDF_READER_FILE_MAX ((size_t)512 * 1024 * 1024)

/* The size of each read while a file is loaded. */
#define PDF_READER_READ_CHUNK 65536

/* How far from the start the header, and from the end the startxref keyword, are looked for. */
#define PDF_READER_HEADER_WINDOW 1024
#define PDF_READER_TRAILER_WINDOW 1024

/* The most cross-reference sections one /Prev chain may link. */
#define PDF_READER_SECTIONS_MAX 32

/* The largest object number, the PDF reference's implementation limit. */
#define PDF_READER_OBJECT_MAX 8388607UL

/* The most pages one document may have. */
#define PDF_READER_PAGES_MAX 1048576

/* The bytes of the first element of the trailer's file identifier. */
#define PDF_READER_ID_SIZE 16

/*
 * The load state of an indirect object.
 *
 * LOADING marks an object whose parse is under way, so an object that needs
 * itself, through its stream length for example, is refused rather than
 * loaded forever.
 */
enum pdf_entry_state {
	PDF_ENTRY_UNLOADED = 0,
	PDF_ENTRY_LOADING,
	PDF_ENTRY_LOADED,
	PDF_ENTRY_FAILED
};

/*
 * One entry of the cross-reference table.
 *
 * sequence is the order in which the entry was read; sections are read from
 * the newest, so for one object number the lowest sequence wins.  mark is
 * the number of the last tree walk that reached the object, which keeps a
 * walk from visiting a node twice.  object is the loaded object while state
 * is LOADED, and error the reason while it is FAILED.
 */
struct pdf_xref_entry {
	unsigned long number;
	unsigned long generation;
	size_t offset;
	size_t sequence;
	int in_use;
	enum pdf_entry_state state;
	unsigned long mark;
	struct pdf_object *object;
	int error;
};

/*
 * One page of a document and the attributes it inherits.
 *
 * The values are as the page tree holds them, possibly references; they
 * are resolved when the page is asked about.
 */
struct pdf_reader_page {
	struct pdf_object *page;
	struct pdf_object *media_box;
	struct pdf_object *crop_box;
	struct pdf_object *rotate;
};

/*
 * The attributes a page tree node passes down to its pages.
 */
struct pdf_page_inheritance {
	struct pdf_object *media_box;
	struct pdf_object *crop_box;
	struct pdf_object *rotate;
};

/*
 * A document being read.
 *
 * data holds the whole file, which the document owns.  The entries are
 * sorted by object number once they are all read, with one entry per
 * number.  null_object stands for every missing object.  mark numbers the
 * current tree walk and only grows.
 */
struct pdf_document {
	unsigned char *data;
	size_t size;
	struct pdf_arena arena;
	struct pdf_xref_entry *entries;
	size_t entries_count;
	size_t entries_capacity;
	struct pdf_object *trailer;
	struct pdf_object *catalog;
	struct pdf_object null_object;
	struct pdf_reader_page *pages;
	size_t pages_count;
	size_t pages_capacity;
	unsigned long mark;
	unsigned char id[PDF_READER_ID_SIZE];
	int has_id;
	time_t creation_time;
	time_t modification_time;
};

static int open_owned(unsigned char *data, size_t size, struct pdf_document **document);
static int read_file(const char *path, unsigned char **data, size_t *size);
static int check_header(const struct pdf_document *document);
static int find_startxref(struct pdf_document *document, size_t *offset);
static int read_cross_references(struct pdf_document *document, size_t offset);
static int read_section(struct pdf_document *document, size_t offset, struct pdf_object **trailer);
static int read_subsection(struct pdf_document *document, struct pdf_lexer *lexer, long start, long count);
static int add_entry(struct pdf_document *document, unsigned long number, unsigned long generation, size_t offset, int in_use);
static void sort_entries(struct pdf_document *document);
static int compare_entries(const void *left, const void *right);
static struct pdf_xref_entry *find_entry(struct pdf_document *document, unsigned long number);
static int load_object(struct pdf_document *document, unsigned long number, unsigned long generation, int depth, struct pdf_object **object);
static int parse_indirect(struct pdf_document *document, const struct pdf_xref_entry *entry, int depth, struct pdf_object **object);
static int read_stream_data(struct pdf_document *document, struct pdf_lexer *lexer, struct pdf_object *stream, int depth);
static int resolve(struct pdf_document *document, struct pdf_object *object, int depth, struct pdf_object **resolved);
static int resolve_key(struct pdf_document *document, const struct pdf_object *dictionary, const char *key, int depth, struct pdf_object **resolved);
static int read_catalog(struct pdf_document *document);
static int walk_page_tree(struct pdf_document *document, struct pdf_object *node, const struct pdf_page_inheritance *inherited, int depth);
static int visit_reference(struct pdf_document *document, const struct pdf_object *node);
static int add_page(struct pdf_document *document, struct pdf_object *page, const struct pdf_page_inheritance *inherited);
static void read_information(struct pdf_document *document);
static int read_identifier(struct pdf_document *document, struct pdf_object **first);
static time_t parse_date(const struct pdf_object *date);
static int read_digits(const unsigned char *text, size_t length, size_t *position, size_t digits, long *value);
static int read_rectangle(struct pdf_document *document, struct pdf_object *value, double rectangle[4]);
static int check_unfiltered(struct pdf_document *document, const struct pdf_object *stream);
static int find_in_associated_files(struct pdf_document *document, const char *name, const char *mime_type, struct pdf_object **stream);
static int find_in_embedded_files(struct pdf_document *document, const char *name, const char *mime_type, struct pdf_object **stream);
static int find_in_name_tree(struct pdf_document *document, struct pdf_object *node, const char *name, int depth, struct pdf_object **value);
static int match_file_specification(struct pdf_document *document, struct pdf_object *value, const char *name, const char *mime_type, struct pdf_object **stream);
static int string_equals(const struct pdf_object *string, const char *text);

/*
 * Opens a PDF file for reading.
 *
 * The whole file is read into memory, so the file may change or go away
 * while the document is open.
 */
int
pdf_document_open(
	const char *path,
	struct pdf_document **document)
{
	unsigned char *data;
	size_t size;
	int error;

	/* Refuses a missing path. */
	if (path == NULL)
		return EINVAL;
	if (document == NULL)
		return EINVAL;

	/* Reads the whole file. */
	data = NULL;
	size = 0;
	error = read_file(path, &data, &size);
	if (error != 0)
		return error;

	/* Reads the document from the bytes, which it now owns. */
	error = open_owned(data, size, document);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the open document. */
	return 0;
}

/*
 * Opens a PDF held in memory for reading.
 *
 * The document keeps its own copy of the bytes, so the caller's buffer may
 * go away.
 */
int
pdf_document_open_memory(
	const void *data,
	size_t size,
	struct pdf_document **document)
{
	unsigned char *copy;
	int error;

	/* Refuses missing bytes, and more than the reader opens. */
	if (data == NULL && size != 0)
		return EINVAL;
	if (document == NULL)
		return EINVAL;
	if (size > PDF_READER_FILE_MAX)
		return EFBIG;

	/* Copies the bytes; one extra byte keeps an empty buffer's allocation valid. */
	copy = malloc(size + 1);
	if (copy == NULL)
		return ENOMEM;
	if (size != 0)
		memcpy(copy, data, size);

	/* Reads the document from the copy, which it now owns. */
	error = open_owned(copy, size, document);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the open document. */
	return 0;
}

/*
 * Closes a document and frees every object read from it.
 */
void
pdf_document_close(
	struct pdf_document *document)
{
	/* Nothing was opened. */
	if (document == NULL)
		return;

	/* Frees the objects, the tables, the bytes and the document. */
	pdf_arena_free(&document->arena);
	free(document->entries);
	free(document->pages);
	free(document->data);
	free(document);
}

/*
 * Reports the number of pages of a document.
 */
size_t
pdf_document_page_count(
	const struct pdf_document *document)
{
	/* Reports the number of pages the page tree listed. */
	return document->pages_count;
}

/*
 * Reports the media box, crop box and rotation of a page.
 *
 * index counts the pages from 0 in the order the page tree lists them.
 */
int
pdf_document_page_box(
	struct pdf_document *document,
	size_t index,
	struct pdf_page_box *box)
{
	struct pdf_reader_page *page;
	struct pdf_object *rotate;
	double media[4];
	double crop[4];
	long rotation;
	int error;

	/* Refuses a page the document does not have. */
	if (index >= document->pages_count)
		return EINVAL;
	page = &document->pages[index];

	/* Reads the media box, which every page must have. */
	error = read_rectangle(document, page->media_box, media);
	if (error != 0)
		return error;

	/* Reads the crop box, which defaults to the media box. */
	memcpy(crop, media, sizeof(crop));
	if (page->crop_box != NULL) {
		error = read_rectangle(document, page->crop_box, crop);
		if (error != 0)
			return error;
	}

	/* Clips the crop box to the media box; one that leaves nothing falls back to the media box. */
	if (crop[0] < media[0])
		crop[0] = media[0];
	if (crop[1] < media[1])
		crop[1] = media[1];
	if (crop[2] > media[2])
		crop[2] = media[2];
	if (crop[3] > media[3])
		crop[3] = media[3];
	if (crop[0] >= crop[2] || crop[1] >= crop[3])
		memcpy(crop, media, sizeof(crop));

	/* Reads the rotation, a multiple of 90 degrees; anything else leaves the page upright. */
	error = resolve(document, page->rotate, 0, &rotate);
	if (error != 0)
		return error;
	rotation = 0;
	if (rotate->type == PDF_OBJECT_INTEGER) {
		rotation = rotate->integer % 360;
		if (rotation < 0)
			rotation += 360;
		if (rotation % 90 != 0)
			rotation = 0;
	}

	/* Fills in the boxes. */
	box->media_left = media[0];
	box->media_bottom = media[1];
	box->media_right = media[2];
	box->media_top = media[3];
	box->crop_left = crop[0];
	box->crop_bottom = crop[1];
	box->crop_right = crop[2];
	box->crop_top = crop[3];
	box->rotation = (int)rotation;

	/* Gives the size as shown; a quarter turn swaps the sides. */
	box->width = crop[2] - crop[0];
	box->height = crop[3] - crop[1];
	if (rotation == 90 || rotation == 270) {
		box->width = crop[3] - crop[1];
		box->height = crop[2] - crop[0];
	}

	/* Succeeded: box describes the page. */
	return 0;
}

/*
 * Computes the SHA-256 of a page's content.
 *
 * The content is the page's content streams, decoded and joined in the
 * order /Contents lists them; a page without content hashes no bytes.
 * Notes compares it with the hash its edit data recorded to learn whether
 * another program changed the page.
 */
int
pdf_document_page_content_hash(
	struct pdf_document *document,
	size_t index,
	unsigned char digest[32])
{
	struct pdf_object *contents;
	struct pdf_object *stream;
	SHA2_CTX context;
	size_t item;
	int error;

	/* Refuses a page the document does not have. */
	if (index >= document->pages_count)
		return EINVAL;

	/* Finds the page's content: nothing, one stream or an array of them. */
	error = resolve_key(document, document->pages[index].page, "Contents", 0, &contents);
	if (error != 0)
		return error;

	/* Refuses content that is neither. */
	if (contents->type != PDF_OBJECT_NULL &&
	    contents->type != PDF_OBJECT_STREAM &&
	    contents->type != PDF_OBJECT_ARRAY)
		return PDF_EFORMAT;

	/* Hashes one stream. */
	SHA256Init(&context);
	if (contents->type == PDF_OBJECT_STREAM) {
		error = check_unfiltered(document, contents);
		if (error != 0)
			return error;
		SHA256Update(&context, document->data + contents->data_offset, contents->data_length);
	}

	/* Hashes each stream of an array in order. */
	if (contents->type == PDF_OBJECT_ARRAY) {
		for (item = 0; item < contents->count; item++) {
			/* Finds the stream; an array of anything else is malformed. */
			error = resolve(document, contents->values[item], 0, &stream);
			if (error != 0)
				return error;
			if (stream->type != PDF_OBJECT_STREAM)
				return PDF_EFORMAT;

			/* Adds its bytes, which must not need decoding. */
			error = check_unfiltered(document, stream);
			if (error != 0)
				return error;
			SHA256Update(&context, document->data + stream->data_offset, stream->data_length);
		}
	}

	/* Succeeded: digest is the content's hash. */
	SHA256Final(digest, &context);
	return 0;
}

/*
 * Finds an attached file by its name.
 *
 * It is pdf_document_find_attachment_type() with no media type.
 */
int
pdf_document_find_attachment(
	struct pdf_document *document,
	const char *name,
	const void **data,
	size_t *size)
{
	int error;

	/* Looks for the name with any media type. */
	error = pdf_document_find_attachment_type(document, name, NULL, data, size);
	if (error != 0)
		return error;

	/* Succeeded: data and size are the file's bytes. */
	return 0;
}

/*
 * Finds an attached file by its name and, unless mime_type is NULL, its
 * media type.
 *
 * The catalog's associated files are searched first, then the
 * EmbeddedFiles name tree.  The bytes stay in the document and live until
 * it is closed.  A file that is not there reports ENOENT.
 */
int
pdf_document_find_attachment_type(
	struct pdf_document *document,
	const char *name,
	const char *mime_type,
	const void **data,
	size_t *size)
{
	struct pdf_object *stream;
	int error;

	/* Refuses a missing name or result. */
	if (name == NULL)
		return EINVAL;
	if (data == NULL || size == NULL)
		return EINVAL;

	/* Searches the catalog's associated files, then the EmbeddedFiles name tree. */
	error = find_in_associated_files(document, name, mime_type, &stream);
	if (error == ENOENT)
		error = find_in_embedded_files(document, name, mime_type, &stream);

	/* Reports a file that is not there or could not be read. */
	if (error != 0)
		return error;

	/* Succeeded: the file's bytes are the stream's. */
	*data = document->data + stream->data_offset;
	*size = stream->data_length;
	return 0;
}

/*
 * Reports the permanent part of the document's file identifier.
 *
 * Notes gives it back to the writer, so a saved revision keeps being the
 * same document.  A document without a sixteen-byte identifier reports
 * ENOENT.
 */
int
pdf_document_get_id(
	const struct pdf_document *document,
	unsigned char id[16])
{
	/* Refuses a document without the identifier. */
	if (!document->has_id)
		return ENOENT;

	/* Copies the identifier out. */
	memcpy(id, document->id, PDF_READER_ID_SIZE);

	/* Succeeded: id holds the identifier. */
	return 0;
}

/*
 * Reports the creation and modification dates of the document.
 *
 * A date the information dictionary does not have, or that is not a valid
 * PDF date, is reported as 0.
 */
int
pdf_document_get_dates(
	const struct pdf_document *document,
	time_t *creation,
	time_t *modification)
{
	/* Copies the dates read when the document was opened. */
	*creation = document->creation_time;
	*modification = document->modification_time;

	/* Succeeded: both dates are given. */
	return 0;
}

/*
 * Reads a document from bytes the document takes over.
 *
 * On failure the bytes are freed.
 */
static int
open_owned(
	unsigned char *data,
	size_t size,
	struct pdf_document **document)
{
	struct pdf_document *created;
	size_t offset;
	int error;

	/* Allocates the document, which owns the bytes from here on. */
	created = calloc(1, sizeof(*created));
	if (created == NULL) {
		free(data);
		return ENOMEM;
	}
	created->data = data;
	created->size = size;
	created->null_object.type = PDF_OBJECT_NULL;

	/* Checks that the bytes start as a PDF. */
	error = check_header(created);
	if (error != 0) {
		pdf_document_close(created);
		return error;
	}

	/* Finds the newest cross-reference section. */
	error = find_startxref(created, &offset);
	if (error != 0) {
		pdf_document_close(created);
		return error;
	}

	/* Reads it and the older ones it links. */
	error = read_cross_references(created, offset);
	if (error != 0) {
		pdf_document_close(created);
		return error;
	}

	/* Reads the catalog and lists the pages. */
	error = read_catalog(created);
	if (error != 0) {
		pdf_document_close(created);
		return error;
	}

	/* Reads the identifier and the dates, which a document may lack. */
	read_information(created);

	/* Succeeded: the caller owns the document. */
	*document = created;
	return 0;
}

/* Reads a whole file into a new allocation. */
static int
read_file(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	unsigned char *buffer;
	unsigned char *grown;
	size_t length;
	size_t capacity;
	size_t got;
	FILE *stream;
	int failed;
	int error;

	/* Opens the file. */
	stream = fopen(path, "rb");
	if (stream == NULL) {
		error = errno;
		return error;
	}

	/* Reads chunks until the end, growing the buffer ahead of each one. */
	buffer = NULL;
	length = 0;
	capacity = 0;
	for (;;) {
		/* Refuses a file larger than the reader opens. */
		if (length > PDF_READER_FILE_MAX) {
			free(buffer);
			fclose(stream);
			return EFBIG;
		}

		/* Makes room for the next chunk. */
		if (capacity - length < PDF_READER_READ_CHUNK) {
			capacity = capacity * 2 + PDF_READER_READ_CHUNK;
			grown = realloc(buffer, capacity);
			if (grown == NULL) {
				free(buffer);
				fclose(stream);
				return ENOMEM;
			}
			buffer = grown;
		}

		/* Reads the chunk; a short one is the end or an error. */
		got = fread(buffer + length, 1, PDF_READER_READ_CHUNK, stream);
		length += got;
		if (got < PDF_READER_READ_CHUNK)
			break;
	}

	/* Reports a read error rather than a short file. */
	failed = ferror(stream);
	fclose(stream);
	if (failed) {
		free(buffer);
		return EIO;
	}

	/* Refuses a last chunk that took the file past the limit. */
	if (length > PDF_READER_FILE_MAX) {
		free(buffer);
		return EFBIG;
	}

	/* Succeeded: the caller owns the file's bytes. */
	*data = buffer;
	*size = length;
	return 0;
}

/* Checks that the file has a PDF header near its start. */
static int
check_header(
	const struct pdf_document *document)
{
	size_t position;
	size_t window;
	int difference;

	/* Looks for %PDF- in the first bytes, where a header after some junk is still found. */
	window = document->size;
	if (window > PDF_READER_HEADER_WINDOW)
		window = PDF_READER_HEADER_WINDOW;
	for (position = 0; position + 5 <= window; position++) {
		difference = memcmp(document->data + position, "%PDF-", 5);
		if (difference == 0)
			return 0;
	}

	/* The file is not a PDF. */
	return PDF_EFORMAT;
}

/* Finds the offset of the newest cross-reference section after the last startxref keyword. */
static int
find_startxref(
	struct pdf_document *document,
	size_t *offset)
{
	struct pdf_lexer lexer;
	struct pdf_token token;
	size_t position;
	size_t lowest;
	int difference;
	int found;
	int error;

	/* Refuses a file too short to hold the keyword. */
	if (document->size < 9)
		return PDF_EFORMAT;

	/* Looks for the last startxref in the end of the file. */
	lowest = 0;
	if (document->size > PDF_READER_TRAILER_WINDOW)
		lowest = document->size - PDF_READER_TRAILER_WINDOW;
	found = 0;
	for (position = document->size - 9; position + 1 > lowest; position--) {
		difference = memcmp(document->data + position, "startxref", 9);
		if (difference == 0) {
			found = 1;
			break;
		}
	}
	if (!found)
		return PDF_EFORMAT;

	/* Reads the offset that follows the keyword. */
	memset(&lexer, 0, sizeof(lexer));
	lexer.data = document->data;
	lexer.size = document->size;
	lexer.position = position + 9;
	lexer.arena = &document->arena;
	error = pdf_lexer_next(&lexer, &token);
	if (error != 0)
		return error;

	/* Refuses an offset that is not in the file. */
	if (token.type != PDF_TOKEN_INTEGER)
		return PDF_EFORMAT;
	if (token.integer < 0 || (unsigned long)token.integer >= document->size)
		return PDF_EFORMAT;

	/* Succeeded: offset is where the newest section starts. */
	*offset = (size_t)token.integer;
	return 0;
}

/*
 * Reads the newest cross-reference section and every older one its /Prev
 * chain links, then sorts the entries.
 *
 * The newest section's trailer is the document's.
 */
static int
read_cross_references(
	struct pdf_document *document,
	size_t offset)
{
	size_t visited[PDF_READER_SECTIONS_MAX];
	struct pdf_object *trailer;
	struct pdf_object *previous;
	size_t sections;
	size_t index;
	int error;

	/* Reads the sections from the newest along the chain. */
	for (sections = 0; sections < PDF_READER_SECTIONS_MAX; sections++) {
		/* Refuses a chain that comes back to a section it read. */
		for (index = 0; index < sections; index++) {
			if (visited[index] == offset)
				return PDF_EFORMAT;
		}
		visited[sections] = offset;

		/* Reads the section and its trailer; the first trailer is the document's. */
		error = read_section(document, offset, &trailer);
		if (error != 0)
			return error;
		if (document->trailer == NULL)
			document->trailer = trailer;

		/* Ends at the oldest section. */
		previous = pdf_object_get(trailer, "Prev");
		if (previous == NULL)
			break;

		/* Refuses a link that is not an offset in the file. */
		if (previous->type != PDF_OBJECT_INTEGER)
			return PDF_EFORMAT;
		if (previous->integer < 0 || (unsigned long)previous->integer >= document->size)
			return PDF_EFORMAT;
		offset = (size_t)previous->integer;
	}

	/* Refuses a chain longer than the limit. */
	if (sections == PDF_READER_SECTIONS_MAX)
		return PDF_EFORMAT;

	/* Keeps the newest entry of each object and sorts them for lookup. */
	sort_entries(document);

	/* Succeeded: every section is read. */
	return 0;
}

/*
 * Reads one classic cross-reference section and its trailer.
 *
 * A cross-reference stream starts with an object header instead of the
 * xref keyword and is not read yet.
 */
static int
read_section(
	struct pdf_document *document,
	size_t offset,
	struct pdf_object **trailer)
{
	struct pdf_lexer lexer;
	struct pdf_token token;
	struct pdf_token count;
	struct pdf_object *dictionary;
	int is_keyword;
	int error;

	/* Reads the first token of the section. */
	memset(&lexer, 0, sizeof(lexer));
	lexer.data = document->data;
	lexer.size = document->size;
	lexer.position = offset;
	lexer.arena = &document->arena;
	error = pdf_lexer_next(&lexer, &token);
	if (error != 0)
		return error;

	/* Refuses a cross-reference stream, which a reader of stage 2 reads, and anything else. */
	is_keyword = pdf_token_is_keyword(&token, "xref");
	if (!is_keyword) {
		if (token.type == PDF_TOKEN_INTEGER)
			return ENOTSUP;
		return PDF_EFORMAT;
	}

	/* Reads the subsections until the trailer. */
	for (;;) {
		error = pdf_lexer_next(&lexer, &token);
		if (error != 0)
			return error;

		/* The trailer ends the subsections. */
		is_keyword = pdf_token_is_keyword(&token, "trailer");
		if (is_keyword)
			break;

		/* Reads the subsection's first object number and its count. */
		if (token.type != PDF_TOKEN_INTEGER)
			return PDF_EFORMAT;
		error = pdf_lexer_next(&lexer, &count);
		if (error != 0)
			return error;
		if (count.type != PDF_TOKEN_INTEGER)
			return PDF_EFORMAT;

		/* Reads the subsection's entries. */
		error = read_subsection(document, &lexer, token.integer, count.integer);
		if (error != 0)
			return error;
	}

	/* Reads the trailer's dictionary. */
	error = pdf_parse_object(&lexer, 0, &dictionary);
	if (error != 0)
		return error;
	if (dictionary->type != PDF_OBJECT_DICTIONARY)
		return PDF_EFORMAT;

	/* Succeeded: trailer is the section's trailer. */
	*trailer = dictionary;
	return 0;
}

/*
 * Reads the entries of one subsection.
 *
 * Each entry is an offset, a generation and n (in use) or f (free).  The
 * writer's entries are twenty bytes each, but any white space between the
 * fields is accepted.
 */
static int
read_subsection(
	struct pdf_document *document,
	struct pdf_lexer *lexer,
	long start,
	long count)
{
	struct pdf_token offset;
	struct pdf_token generation;
	struct pdf_token kind;
	unsigned long number;
	long index;
	int in_use;
	int is_free;
	int error;

	/* Refuses numbers outside the limit, which also keeps start + count from overflowing. */
	if (start < 0 || count < 0)
		return PDF_EFORMAT;
	if ((unsigned long)start > PDF_READER_OBJECT_MAX)
		return PDF_EFORMAT;
	if ((unsigned long)count > PDF_READER_OBJECT_MAX + 1 - (unsigned long)start)
		return PDF_EFORMAT;

	/* Refuses a count the rest of the file cannot hold at the shortest entry of six bytes. */
	if ((unsigned long)count > (document->size - lexer->position) / 6)
		return PDF_EFORMAT;

	/* Reads each entry. */
	for (index = 0; index < count; index++) {
		/* Reads the three fields. */
		error = pdf_lexer_next(lexer, &offset);
		if (error != 0)
			return error;
		error = pdf_lexer_next(lexer, &generation);
		if (error != 0)
			return error;
		error = pdf_lexer_next(lexer, &kind);
		if (error != 0)
			return error;

		/* Refuses an entry whose numbers are not numbers. */
		if (offset.type != PDF_TOKEN_INTEGER || generation.type != PDF_TOKEN_INTEGER)
			return PDF_EFORMAT;
		if (offset.integer < 0 || generation.integer < 0)
			return PDF_EFORMAT;

		/* Reads the kind of the entry. */
		in_use = pdf_token_is_keyword(&kind, "n");
		is_free = pdf_token_is_keyword(&kind, "f");
		if (!in_use && !is_free)
			return PDF_EFORMAT;

		/* Adds the entry; an object in use must start inside the file. */
		number = (unsigned long)start + (unsigned long)index;
		if (in_use && (unsigned long)offset.integer >= document->size)
			return PDF_EFORMAT;
		error = add_entry(document, number, (unsigned long)generation.integer, (size_t)offset.integer, in_use);
		if (error != 0)
			return error;
	}

	/* Succeeded: every entry of the subsection is added. */
	return 0;
}

/* Appends one cross-reference entry in the order it was read. */
static int
add_entry(
	struct pdf_document *document,
	unsigned long number,
	unsigned long generation,
	size_t offset,
	int in_use)
{
	struct pdf_xref_entry *grown;
	struct pdf_xref_entry *entry;
	size_t capacity;

	/* Grows the table when it is full. */
	if (document->entries_count == document->entries_capacity) {
		capacity = document->entries_capacity * 2;
		if (capacity == 0)
			capacity = 64;
		if (capacity > PDF_READER_ARENA_MAX / sizeof(*grown))
			return ENOMEM;
		grown = realloc(document->entries, capacity * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		document->entries = grown;
		document->entries_capacity = capacity;
	}

	/* Fills in the entry; nothing is loaded yet. */
	entry = &document->entries[document->entries_count];
	memset(entry, 0, sizeof(*entry));
	entry->number = number;
	entry->generation = generation;
	entry->offset = offset;
	entry->in_use = in_use;
	entry->sequence = document->entries_count;
	entry->state = PDF_ENTRY_UNLOADED;
	document->entries_count++;

	/* Succeeded: the entry is the table's last. */
	return 0;
}

/*
 * Sorts the entries by object number and keeps only the newest entry of
 * each number, the one read first.
 */
static void
sort_entries(
	struct pdf_document *document)
{
	size_t index;
	size_t kept;

	/* Nothing to sort. */
	if (document->entries_count == 0)
		return;

	/* Orders the entries by number, and by reading order within a number. */
	qsort(document->entries, document->entries_count, sizeof(*document->entries), compare_entries);

	/* Keeps the first entry of each number. */
	kept = 1;
	for (index = 1; index < document->entries_count; index++) {
		if (document->entries[index].number == document->entries[kept - 1].number)
			continue;
		document->entries[kept] = document->entries[index];
		kept++;
	}
	document->entries_count = kept;
}

/* Orders two entries by object number, then by the order they were read. */
static int
compare_entries(
	const void *left,
	const void *right)
{
	const struct pdf_xref_entry *first;
	const struct pdf_xref_entry *second;

	/* Names the two entries. */
	first = left;
	second = right;

	/* A lower object number comes first. */
	if (first->number < second->number)
		return -1;
	if (first->number > second->number)
		return 1;

	/* Within a number, the entry read first, which is the newest, comes first. */
	if (first->sequence < second->sequence)
		return -1;
	if (first->sequence > second->sequence)
		return 1;

	/* The same entry. */
	return 0;
}

/* Finds the entry of an object number, or NULL when the table has none. */
static struct pdf_xref_entry *
find_entry(
	struct pdf_document *document,
	unsigned long number)
{
	size_t low;
	size_t high;
	size_t middle;

	/* Halves the sorted table until the number is found or the range is empty. */
	low = 0;
	high = document->entries_count;
	while (low < high) {
		middle = low + (high - low) / 2;
		if (document->entries[middle].number == number)
			return &document->entries[middle];
		if (document->entries[middle].number < number) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}

	/* The table has no entry of the number. */
	return NULL;
}

/*
 * Loads an indirect object, once.
 *
 * A number the table does not have, a free entry and a generation that
 * does not match all name the null object, as PDF defines.  depth bounds a
 * chain of objects that each need another to be read.
 */
static int
load_object(
	struct pdf_document *document,
	unsigned long number,
	unsigned long generation,
	int depth,
	struct pdf_object **object)
{
	struct pdf_xref_entry *entry;
	struct pdf_object *loaded;
	int error;

	/* Refuses a chain of loads deeper than the limit. */
	if (depth > PDF_READER_DEPTH_MAX)
		return PDF_EFORMAT;

	/* A missing, free or superseded object is null. */
	entry = find_entry(document, number);
	if (entry == NULL ||
	    !entry->in_use ||
	    entry->generation != generation) {
		*object = &document->null_object;
		return 0;
	}

	/* Answers from what an earlier load found. */
	switch (entry->state) {
	case PDF_ENTRY_LOADED:
		*object = entry->object;
		return 0;
	case PDF_ENTRY_FAILED:
		return entry->error;
	case PDF_ENTRY_LOADING:
		/* The object needs itself to be read. */
		return PDF_EFORMAT;
	case PDF_ENTRY_UNLOADED:
		break;
	}

	/* Parses the object while marking it as under way. */
	entry->state = PDF_ENTRY_LOADING;
	error = parse_indirect(document, entry, depth, &loaded);
	if (error != 0) {
		entry->state = PDF_ENTRY_FAILED;
		entry->error = error;
		return error;
	}

	/* Keeps the object for every later load. */
	entry->state = PDF_ENTRY_LOADED;
	entry->object = loaded;

	/* Succeeded: object is the loaded object. */
	*object = loaded;
	return 0;
}

/* Parses the indirect object an entry points at, with its stream data if it has one. */
static int
parse_indirect(
	struct pdf_document *document,
	const struct pdf_xref_entry *entry,
	int depth,
	struct pdf_object **object)
{
	struct pdf_lexer lexer;
	struct pdf_token token;
	struct pdf_object *parsed;
	size_t after_object;
	int is_keyword;
	int error;

	/* Reads the object number at the entry's offset, which must be the entry's. */
	memset(&lexer, 0, sizeof(lexer));
	lexer.data = document->data;
	lexer.size = document->size;
	lexer.position = entry->offset;
	lexer.arena = &document->arena;
	error = pdf_lexer_next(&lexer, &token);
	if (error != 0)
		return error;
	if (token.type != PDF_TOKEN_INTEGER)
		return PDF_EFORMAT;
	if (token.integer < 0)
		return PDF_EFORMAT;
	if ((unsigned long)token.integer != entry->number)
		return PDF_EFORMAT;

	/* Reads the generation and the obj keyword. */
	error = pdf_lexer_next(&lexer, &token);
	if (error != 0)
		return error;
	if (token.type != PDF_TOKEN_INTEGER)
		return PDF_EFORMAT;
	error = pdf_lexer_next(&lexer, &token);
	if (error != 0)
		return error;
	is_keyword = pdf_token_is_keyword(&token, "obj");
	if (!is_keyword)
		return PDF_EFORMAT;

	/* Parses the object itself. */
	error = pdf_parse_object(&lexer, 0, &parsed);
	if (error != 0)
		return error;

	/* A dictionary followed by the stream keyword is a stream's. */
	if (parsed->type == PDF_OBJECT_DICTIONARY) {
		after_object = lexer.position;
		error = pdf_lexer_next(&lexer, &token);
		if (error != 0)
			token.type = PDF_TOKEN_END;
		is_keyword = pdf_token_is_keyword(&token, "stream");
		if (is_keyword) {
			error = read_stream_data(document, &lexer, parsed, depth);
			if (error != 0)
				return error;
		} else {
			lexer.position = after_object;
		}
	}

	/* Succeeded: object is the parsed object; its endobj is not needed. */
	*object = parsed;
	return 0;
}

/*
 * Finds the data of a stream after its stream keyword and turns the
 * dictionary into the stream.
 *
 * The data starts after the line end that follows the keyword and is as
 * long as /Length says; the endstream keyword must follow it.
 */
static int
read_stream_data(
	struct pdf_document *document,
	struct pdf_lexer *lexer,
	struct pdf_object *stream,
	int depth)
{
	struct pdf_object *length;
	struct pdf_token token;
	size_t start;
	int is_keyword;
	int error;

	/* Steps over the line end after the keyword: CR LF, LF, or a lone CR. */
	start = lexer->position;
	if (start < document->size && document->data[start] == '\r')
		start++;
	if (start < document->size && document->data[start] == '\n')
		start++;

	/* Reads the length, which may itself be an indirect object. */
	error = resolve_key(document, stream, "Length", depth + 1, &length);
	if (error != 0)
		return error;

	/* Refuses a length that is not a count of bytes inside the file. */
	if (length->type != PDF_OBJECT_INTEGER)
		return PDF_EFORMAT;
	if (length->integer < 0)
		return PDF_EFORMAT;
	if ((unsigned long)length->integer > document->size - start)
		return PDF_EFORMAT;

	/* Refuses data not followed by endstream, which a wrong length leaves behind. */
	lexer->position = start + (size_t)length->integer;
	error = pdf_lexer_next(lexer, &token);
	if (error != 0)
		return error;
	is_keyword = pdf_token_is_keyword(&token, "endstream");
	if (!is_keyword)
		return PDF_EFORMAT;

	/* Succeeded: the dictionary is now the stream's, with its data's range. */
	stream->type = PDF_OBJECT_STREAM;
	stream->data_offset = start;
	stream->data_length = (size_t)length->integer;
	return 0;
}

/*
 * Resolves an object that may be a reference to the object it names.
 *
 * A missing object (NULL) resolves to null.  A reference to a reference is
 * followed, to a bounded depth.
 */
static int
resolve(
	struct pdf_document *document,
	struct pdf_object *object,
	int depth,
	struct pdf_object **resolved)
{
	int hops;
	int error;

	/* A missing object is null. */
	if (object == NULL) {
		*resolved = &document->null_object;
		return 0;
	}

	/* Follows references until a direct object. */
	for (hops = 0; object->type == PDF_OBJECT_REFERENCE; hops++) {
		/* Refuses a chain of references longer than the limit. */
		if (hops > PDF_READER_DEPTH_MAX)
			return PDF_EFORMAT;
		error = load_object(document, object->number, object->generation, depth + hops, &object);
		if (error != 0)
			return error;
	}

	/* Succeeded: resolved is the direct object. */
	*resolved = object;
	return 0;
}

/*
 * Resolves the value of a key of a dictionary or a stream.
 *
 * A missing dictionary or key resolves to null.
 */
static int
resolve_key(
	struct pdf_document *document,
	const struct pdf_object *dictionary,
	const char *key,
	int depth,
	struct pdf_object **resolved)
{
	struct pdf_object *value;
	int error;

	/* Finds the value, which may be a reference. */
	value = pdf_object_get(dictionary, key);

	/* Resolves it. */
	error = resolve(document, value, depth, resolved);
	if (error != 0)
		return error;

	/* Succeeded: resolved is the key's direct value. */
	return 0;
}

/* Reads the catalog from the trailer and lists the pages of its page tree. */
static int
read_catalog(
	struct pdf_document *document)
{
	struct pdf_page_inheritance inheritance;
	struct pdf_object *encrypt;
	int error;

	/* Refuses an encrypted document, which a reader of stage 3 reads. */
	encrypt = pdf_object_get(document->trailer, "Encrypt");
	if (encrypt != NULL)
		return ENOTSUP;

	/* Finds the catalog, which must be a dictionary. */
	error = resolve_key(document, document->trailer, "Root", 0, &document->catalog);
	if (error != 0)
		return error;
	if (document->catalog->type != PDF_OBJECT_DICTIONARY)
		return PDF_EFORMAT;

	/* Walks the page tree from its root, which passes nothing down. */
	memset(&inheritance, 0, sizeof(inheritance));
	document->mark++;
	error = walk_page_tree(document, pdf_object_get(document->catalog, "Pages"), &inheritance, 0);
	if (error != 0)
		return error;

	/* Succeeded: the pages are listed. */
	return 0;
}

/*
 * Walks one node of the page tree, listing its pages in order.
 *
 * A node passes its MediaBox, CropBox and Rotate down to the nodes and
 * pages under it, which may replace them.  A node reached a second time is
 * skipped, so a tree that loops ends.
 */
static int
walk_page_tree(
	struct pdf_document *document,
	struct pdf_object *node,
	const struct pdf_page_inheritance *inherited,
	int depth)
{
	struct pdf_page_inheritance inheritance;
	struct pdf_object *resolved;
	struct pdf_object *kids;
	struct pdf_object *type;
	struct pdf_object *value;
	size_t index;
	int is_pages;
	int visited;
	int error;

	/* Refuses a tree deeper than the limit. */
	if (depth > PDF_READER_DEPTH_MAX)
		return PDF_EFORMAT;

	/* Refuses a missing node. */
	if (node == NULL)
		return PDF_EFORMAT;

	/* Skips a node the walk has reached before. */
	visited = visit_reference(document, node);
	if (visited)
		return 0;

	/* Finds the node, which must be a dictionary. */
	error = resolve(document, node, 0, &resolved);
	if (error != 0)
		return error;
	if (resolved->type != PDF_OBJECT_DICTIONARY)
		return PDF_EFORMAT;

	/* Takes over the inherited attributes, replacing those the node sets. */
	inheritance = *inherited;
	value = pdf_object_get(resolved, "MediaBox");
	if (value != NULL)
		inheritance.media_box = value;
	value = pdf_object_get(resolved, "CropBox");
	if (value != NULL)
		inheritance.crop_box = value;
	value = pdf_object_get(resolved, "Rotate");
	if (value != NULL)
		inheritance.rotate = value;

	/* Tells a node of the tree from a page: by its type, or by its kids when it has no type. */
	error = resolve_key(document, resolved, "Type", 0, &type);
	if (error != 0)
		return error;
	kids = pdf_object_get(resolved, "Kids");
	is_pages = pdf_object_is_name(type, "Pages");
	if (type->type == PDF_OBJECT_NULL && kids != NULL)
		is_pages = 1;

	/* Lists a page. */
	if (!is_pages) {
		error = add_page(document, resolved, &inheritance);
		if (error != 0)
			return error;
		return 0;
	}

	/* Finds the node's kids, which must be an array. */
	error = resolve(document, kids, 0, &kids);
	if (error != 0)
		return error;
	if (kids->type != PDF_OBJECT_ARRAY)
		return PDF_EFORMAT;

	/* Walks each kid in order. */
	for (index = 0; index < kids->count; index++) {
		error = walk_page_tree(document, kids->values[index], &inheritance, depth + 1);
		if (error != 0)
			return error;
	}

	/* Succeeded: the node's pages are listed. */
	return 0;
}

/*
 * Marks the object a reference names as reached by the current walk, and
 * reports whether it had been reached already.
 *
 * A direct object is part of its parent and cannot be reached twice.
 */
static int
visit_reference(
	struct pdf_document *document,
	const struct pdf_object *node)
{
	struct pdf_xref_entry *entry;

	/* Only a reference names a shared object. */
	if (node->type != PDF_OBJECT_REFERENCE)
		return 0;

	/* A reference to nothing reaches nothing that could be reached again. */
	entry = find_entry(document, node->number);
	if (entry == NULL)
		return 0;

	/* Reports an object the walk has reached. */
	if (entry->mark == document->mark)
		return 1;

	/* Marks the object for the rest of the walk. */
	entry->mark = document->mark;

	/* The object is reached for the first time. */
	return 0;
}

/* Appends a page with the attributes it inherits. */
static int
add_page(
	struct pdf_document *document,
	struct pdf_object *page,
	const struct pdf_page_inheritance *inherited)
{
	struct pdf_reader_page *grown;
	size_t capacity;

	/* Refuses more pages than the limit. */
	if (document->pages_count == PDF_READER_PAGES_MAX)
		return ENOMEM;

	/* Grows the page array when it is full. */
	if (document->pages_count == document->pages_capacity) {
		capacity = document->pages_capacity * 2;
		if (capacity == 0)
			capacity = 16;
		grown = realloc(document->pages, capacity * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		document->pages = grown;
		document->pages_capacity = capacity;
	}

	/* Fills in the page. */
	document->pages[document->pages_count].page = page;
	document->pages[document->pages_count].media_box = inherited->media_box;
	document->pages[document->pages_count].crop_box = inherited->crop_box;
	document->pages[document->pages_count].rotate = inherited->rotate;
	document->pages_count++;

	/* Succeeded: the page is the document's last. */
	return 0;
}

/*
 * Reads the trailer's file identifier and the information dictionary's
 * dates.
 *
 * Neither is required, so anything missing or malformed is left unknown.
 */
static void
read_information(
	struct pdf_document *document)
{
	struct pdf_object *first;
	struct pdf_object *information;
	struct pdf_object *date;
	int error;

	/* Keeps the first element of the identifier when it is a sixteen-byte string. */
	error = read_identifier(document, &first);
	if (error == 0) {
		memcpy(document->id, first->bytes, PDF_READER_ID_SIZE);
		document->has_id = 1;
	}

	/* Finds the information dictionary. */
	error = resolve_key(document, document->trailer, "Info", 0, &information);
	if (error != 0)
		return;
	if (information->type != PDF_OBJECT_DICTIONARY)
		return;

	/* Reads the creation date. */
	error = resolve_key(document, information, "CreationDate", 0, &date);
	if (error == 0)
		document->creation_time = parse_date(date);

	/* Reads the modification date. */
	error = resolve_key(document, information, "ModDate", 0, &date);
	if (error == 0)
		document->modification_time = parse_date(date);
}

/*
 * Finds the first element of the trailer's file identifier.
 *
 * It must be a sixteen-byte string in an array of two; anything else
 * reports ENOENT.
 */
static int
read_identifier(
	struct pdf_document *document,
	struct pdf_object **first)
{
	struct pdf_object *identifier;
	struct pdf_object *element;
	int error;

	/* Finds the identifier, an array of two strings. */
	error = resolve_key(document, document->trailer, "ID", 0, &identifier);
	if (error != 0)
		return error;
	if (identifier->type != PDF_OBJECT_ARRAY)
		return ENOENT;
	if (identifier->count != 2)
		return ENOENT;

	/* Finds its first element, the permanent one. */
	error = resolve(document, identifier->values[0], 0, &element);
	if (error != 0)
		return error;
	if (element->type != PDF_OBJECT_STRING)
		return ENOENT;
	if (element->length != PDF_READER_ID_SIZE)
		return ENOENT;

	/* Succeeded: first is the identifier's permanent element. */
	*first = element;
	return 0;
}

/*
 * Converts a PDF date string, (D:YYYYMMDDHHmmSSOHH'mm'), to a time.
 *
 * Every field after the year may be left out, and O is Z, + or - before
 * the offset from universal time.  A date that is not valid, or before
 * 1970, is 0.
 */
static time_t
parse_date(
	const struct pdf_object *date)
{
	const unsigned char *text;
	size_t length;
	size_t position;
	long year;
	long month;
	long day;
	long hour;
	long minute;
	long second;
	long offset_hours;
	long offset_minutes;
	long shifted_year;
	long era;
	long year_of_era;
	long day_of_year;
	long day_of_era;
	long days;
	long *fields[5];
	size_t field;
	int sign;
	int error;
	double seconds;

	/* Only a string is a date. */
	if (date->type != PDF_OBJECT_STRING)
		return 0;
	text = date->bytes;
	length = date->length;

	/* Steps over the D: prefix, which the reference asks for but not every writer writes. */
	position = 0;
	if (length >= 2 &&
	    text[0] == 'D' &&
	    text[1] == ':')
		position = 2;

	/* Reads the year, which every date has. */
	error = read_digits(text, length, &position, 4, &year);
	if (error != 0)
		return 0;

	/* Reads each later field while they are there; the rest keep their defaults. */
	fields[0] = &month;
	fields[1] = &day;
	fields[2] = &hour;
	fields[3] = &minute;
	fields[4] = &second;
	month = 1;
	day = 1;
	hour = 0;
	minute = 0;
	second = 0;
	for (field = 0; field < 5; field++) {
		error = read_digits(text, length, &position, 2, fields[field]);
		if (error != 0)
			break;
	}

	/* Refuses fields outside their ranges. */
	if (month < 1 || month > 12)
		return 0;
	if (day < 1 || day > 31)
		return 0;
	if (hour > 23)
		return 0;
	if (minute > 59)
		return 0;
	if (second > 59)
		return 0;

	/* Reads the offset from universal time: + is ahead of it, - behind it. */
	sign = 0;
	offset_hours = 0;
	offset_minutes = 0;
	if (position < length && text[position] == '+')
		sign = 1;
	if (position < length && text[position] == '-')
		sign = -1;
	if (sign != 0) {
		position++;
		error = read_digits(text, length, &position, 2, &offset_hours);
		if (error == 0) {
			/* The minutes follow an apostrophe; a missing field leaves them zero. */
			if (position < length && text[position] == '\'')
				position++;
			error = read_digits(text, length, &position, 2, &offset_minutes);
			if (error != 0)
				offset_minutes = 0;
		}
	}

	/* Counts the days from 1970-01-01 to the date in the proleptic Gregorian calendar. */
	shifted_year = year;
	if (month <= 2)
		shifted_year = year - 1;
	era = shifted_year / 400;
	year_of_era = shifted_year - era * 400;
	if (month > 2) {
		day_of_year = (153 * (month - 3) + 2) / 5 + day - 1;
	} else {
		day_of_year = (153 * (month + 9) + 2) / 5 + day - 1;
	}
	day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	days = era * 146097 + day_of_era - 719468;

	/* Adds the time of day and removes the offset to reach universal time. */
	seconds = (double)days * 86400.0 + (double)hour * 3600.0 + (double)minute * 60.0 + (double)second;
	seconds -= (double)sign * ((double)offset_hours * 3600.0 + (double)offset_minutes * 60.0);

	/* Refuses a time before 1970 or past what a 32-bit time holds where time is 32 bits. */
	if (seconds < 0.0)
		return 0;
	if (sizeof(time_t) < 8 && seconds > 2147483647.0)
		return 0;

	/* Reports the time. */
	return (time_t)seconds;
}

/* Reads a fixed number of decimal digits at a position of a date. */
static int
read_digits(
	const unsigned char *text,
	size_t length,
	size_t *position,
	size_t digits,
	long *value)
{
	size_t index;
	long read;

	/* Refuses a field the string ends inside. */
	if (*position > length)
		return PDF_EFORMAT;
	if (length - *position < digits)
		return PDF_EFORMAT;

	/* Adds each digit, refusing anything else. */
	read = 0;
	for (index = 0; index < digits; index++) {
		if (text[*position + index] < '0' || text[*position + index] > '9')
			return PDF_EFORMAT;
		read = read * 10 + (text[*position + index] - '0');
	}

	/* Succeeded: value is the field and the position is past it. */
	*position += digits;
	*value = read;
	return 0;
}

/*
 * Reads a rectangle, an array of four numbers, with its corners put in
 * order: left, bottom, right, top.
 */
static int
read_rectangle(
	struct pdf_document *document,
	struct pdf_object *value,
	double rectangle[4])
{
	struct pdf_object *array;
	struct pdf_object *item;
	double swap;
	size_t index;
	int error;

	/* Finds the array of four items. */
	error = resolve(document, value, 0, &array);
	if (error != 0)
		return error;
	if (array->type != PDF_OBJECT_ARRAY || array->count != 4)
		return PDF_EFORMAT;

	/* Reads each coordinate, which must be a finite number. */
	for (index = 0; index < 4; index++) {
		error = resolve(document, array->values[index], 0, &item);
		if (error != 0)
			return error;
		error = pdf_object_number(item, &rectangle[index]);
		if (error != 0)
			return error;
		if (!(rectangle[index] - rectangle[index] == 0.0))
			return PDF_EFORMAT;
	}

	/* Puts the left edge before the right one. */
	if (rectangle[0] > rectangle[2]) {
		swap = rectangle[0];
		rectangle[0] = rectangle[2];
		rectangle[2] = swap;
	}

	/* Puts the bottom edge before the top one. */
	if (rectangle[1] > rectangle[3]) {
		swap = rectangle[1];
		rectangle[1] = rectangle[3];
		rectangle[3] = swap;
	}

	/* Succeeded: rectangle holds the ordered corners. */
	return 0;
}

/*
 * Checks that a stream's data needs no decoding.
 *
 * A filter, which the reader of stage 1 decodes, is reported as ENOTSUP.
 */
static int
check_unfiltered(
	struct pdf_document *document,
	const struct pdf_object *stream)
{
	struct pdf_object *filter;
	int error;

	/* Finds the filter, if any. */
	error = resolve_key(document, stream, "Filter", 0, &filter);
	if (error != 0)
		return error;

	/* No filter, or an empty list of them, leaves the data as it is. */
	if (filter->type == PDF_OBJECT_NULL)
		return 0;
	if (filter->type == PDF_OBJECT_ARRAY && filter->count == 0)
		return 0;

	/* Any filter is not decoded yet. */
	return ENOTSUP;
}

/*
 * Searches the catalog's associated files (/AF) for an attached file.
 *
 * A file that is not there reports ENOENT.
 */
static int
find_in_associated_files(
	struct pdf_document *document,
	const char *name,
	const char *mime_type,
	struct pdf_object **stream)
{
	struct pdf_object *associated;
	size_t index;
	int error;

	/* Finds the list of associated files; a catalog without one has none. */
	error = resolve_key(document, document->catalog, "AF", 0, &associated);
	if (error != 0)
		return error;
	if (associated->type != PDF_OBJECT_ARRAY)
		return ENOENT;

	/* Tries each file specification in order. */
	for (index = 0; index < associated->count; index++) {
		error = match_file_specification(document, associated->values[index], name, mime_type, stream);
		if (error != ENOENT)
			return error;
	}

	/* No associated file is the one asked for. */
	return ENOENT;
}

/*
 * Searches the EmbeddedFiles name tree for an attached file.
 *
 * A file that is not there reports ENOENT.
 */
static int
find_in_embedded_files(
	struct pdf_document *document,
	const char *name,
	const char *mime_type,
	struct pdf_object **stream)
{
	struct pdf_object *names;
	struct pdf_object *tree;
	struct pdf_object *value;
	int error;

	/* Finds the tree; a catalog without one has no embedded files. */
	error = resolve_key(document, document->catalog, "Names", 0, &names);
	if (error != 0)
		return error;
	tree = pdf_object_get(names, "EmbeddedFiles");
	if (tree == NULL)
		return ENOENT;

	/* Looks the name up in a new walk of the tree. */
	document->mark++;
	error = find_in_name_tree(document, tree, name, 0, &value);
	if (error != 0)
		return error;

	/* Checks the file specification the tree gives. */
	error = match_file_specification(document, value, name, mime_type, stream);
	if (error != 0)
		return error;

	/* Succeeded: stream is the attached file. */
	return 0;
}

/*
 * Looks a name up in a name tree.
 *
 * Every node is searched rather than trusting /Limits, and a node reached
 * a second time in one search is skipped.  A name that is not there reports
 * ENOENT.
 */
static int
find_in_name_tree(
	struct pdf_document *document,
	struct pdf_object *node,
	const char *name,
	int depth,
	struct pdf_object **value)
{
	struct pdf_object *resolved;
	struct pdf_object *names;
	struct pdf_object *kids;
	struct pdf_object *key;
	size_t index;
	int visited;
	int equal;
	int error;

	/* Refuses a tree deeper than the limit. */
	if (depth > PDF_READER_DEPTH_MAX)
		return PDF_EFORMAT;

	/* Skips a node the search has reached before. */
	visited = visit_reference(document, node);
	if (visited)
		return ENOENT;

	/* Finds the node; one that is not a dictionary holds nothing. */
	error = resolve(document, node, 0, &resolved);
	if (error != 0)
		return error;
	if (resolved->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;

	/* Looks through the node's own pairs of key and value. */
	error = resolve_key(document, resolved, "Names", 0, &names);
	if (error != 0)
		return error;
	if (names->type == PDF_OBJECT_ARRAY) {
		for (index = 0; index + 1 < names->count; index += 2) {
			error = resolve(document, names->values[index], 0, &key);
			if (error != 0)
				return error;
			equal = string_equals(key, name);
			if (equal) {
				*value = names->values[index + 1];
				return 0;
			}
		}
	}

	/* Looks through the node's kids. */
	error = resolve_key(document, resolved, "Kids", 0, &kids);
	if (error != 0)
		return error;
	if (kids->type == PDF_OBJECT_ARRAY) {
		for (index = 0; index < kids->count; index++) {
			error = find_in_name_tree(document, kids->values[index], name, depth + 1, value);
			if (error != ENOENT)
				return error;
		}
	}

	/* The node and its kids do not have the name. */
	return ENOENT;
}

/*
 * Checks whether a file specification names an attached file and returns
 * its embedded file stream.
 *
 * The file's name is /UF or /F; the media type, when one is asked for, is
 * the stream's /Subtype.  A stream whose /Params /Size disagrees with its
 * length is malformed.  A specification that does not match reports ENOENT.
 */
static int
match_file_specification(
	struct pdf_document *document,
	struct pdf_object *value,
	const char *name,
	const char *mime_type,
	struct pdf_object **stream)
{
	struct pdf_object *specification;
	struct pdf_object *file_name;
	struct pdf_object *files;
	struct pdf_object *file;
	struct pdf_object *subtype;
	struct pdf_object *parameters;
	struct pdf_object *size;
	int equal;
	int error;

	/* Finds the specification, which must be a dictionary to hold an embedded file. */
	error = resolve(document, value, 0, &specification);
	if (error != 0)
		return error;
	if (specification->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;

	/* Compares the name, the Unicode one first. */
	error = resolve_key(document, specification, "UF", 0, &file_name);
	if (error != 0)
		return error;
	equal = string_equals(file_name, name);
	if (!equal) {
		error = resolve_key(document, specification, "F", 0, &file_name);
		if (error != 0)
			return error;
		equal = string_equals(file_name, name);
	}
	if (!equal)
		return ENOENT;

	/* Finds the embedded file stream. */
	error = resolve_key(document, specification, "EF", 0, &files);
	if (error != 0)
		return error;
	file = pdf_object_get(files, "UF");
	if (file == NULL)
		file = pdf_object_get(files, "F");
	error = resolve(document, file, 0, &file);
	if (error != 0)
		return error;
	if (file->type != PDF_OBJECT_STREAM)
		return ENOENT;

	/* Compares the media type when one is asked for. */
	if (mime_type != NULL) {
		error = resolve_key(document, file, "Subtype", 0, &subtype);
		if (error != 0)
			return error;
		equal = pdf_object_is_name(subtype, mime_type);
		if (!equal)
			return ENOENT;
	}

	/* Refuses data that needs decoding. */
	error = check_unfiltered(document, file);
	if (error != 0)
		return error;

	/* Refuses a stream whose recorded size disagrees with its data. */
	error = resolve_key(document, file, "Params", 0, &parameters);
	if (error != 0)
		return error;
	error = resolve_key(document, parameters, "Size", 0, &size);
	if (error != 0)
		return error;
	if (size->type == PDF_OBJECT_INTEGER) {
		if (size->integer < 0)
			return PDF_EFORMAT;
		if ((unsigned long)size->integer != file->data_length)
			return PDF_EFORMAT;
	}

	/* Succeeded: stream is the attached file. */
	*stream = file;
	return 0;
}

/* Reports whether a string object holds exactly the bytes of a C string. */
static int
string_equals(
	const struct pdf_object *string,
	const char *text)
{
	size_t length;
	int difference;

	/* Only a string object can hold the bytes. */
	if (string->type != PDF_OBJECT_STRING)
		return 0;

	/* Compares the length, then the bytes. */
	length = strlen(text);
	if (string->length != length)
		return 0;
	difference = memcmp(string->bytes, text, length);
	if (difference != 0)
		return 0;

	/* The string holds the text. */
	return 1;
}
