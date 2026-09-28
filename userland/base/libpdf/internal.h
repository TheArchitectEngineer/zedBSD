/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libpdf's private declarations for the reader: the objects a document is
 * made of, the arena they live in, and the lexer and parser that object.c
 * provides to reader.c.  None of these leave the library (exports.map).
 */

#ifndef LIBPDF_INTERNAL_H
#define LIBPDF_INTERNAL_H

#include <stddef.h>

/* The deepest nesting of arrays and dictionaries, and of page and name trees (design-pdf.md section 4.3). */
#define PDF_READER_DEPTH_MAX 32

/* The most bytes the objects of one document may take, the decode limit of design-pdf.md section 4.3. */
#define PDF_READER_ARENA_MAX ((size_t)256 * 1024 * 1024)

/* The longest name, twice what the PDF reference's implementation limits allow. */
#define PDF_READER_NAME_MAX 255

/* The longest number token; anything longer is not a number PDF writes. */
#define PDF_READER_NUMBER_MAX 64

/*
 * The kinds of PDF object.
 */
enum pdf_object_type {
	PDF_OBJECT_NULL = 0,
	PDF_OBJECT_BOOLEAN,
	PDF_OBJECT_INTEGER,
	PDF_OBJECT_REAL,
	PDF_OBJECT_NAME,
	PDF_OBJECT_STRING,
	PDF_OBJECT_ARRAY,
	PDF_OBJECT_DICTIONARY,
	PDF_OBJECT_STREAM,
	PDF_OBJECT_REFERENCE
};

/*
 * The kinds of token the lexer reads.
 */
enum pdf_token_type {
	PDF_TOKEN_END = 0,
	PDF_TOKEN_INTEGER,
	PDF_TOKEN_REAL,
	PDF_TOKEN_NAME,
	PDF_TOKEN_STRING,
	PDF_TOKEN_ARRAY_OPEN,
	PDF_TOKEN_ARRAY_CLOSE,
	PDF_TOKEN_DICTIONARY_OPEN,
	PDF_TOKEN_DICTIONARY_CLOSE,
	PDF_TOKEN_KEYWORD
};

/*
 * One PDF object, direct or loaded from an indirect one.
 *
 * Only the fields of its type are meaningful.  A name's or a string's bytes
 * are decoded and followed by a NUL that is not counted.  A dictionary and a
 * stream keep their keys (names) and values side by side; a stream's data
 * is a range of the document's bytes.  Every object lives in the document's
 * arena and is freed with it.
 */
struct pdf_object {
	enum pdf_object_type type;
	int boolean;
	long integer;
	double real;
	const unsigned char *bytes;
	size_t length;
	struct pdf_object **keys;
	struct pdf_object **values;
	size_t count;
	size_t data_offset;
	size_t data_length;
	unsigned long number;
	unsigned long generation;
};

/*
 * One block of an arena.
 *
 * The block's bytes follow this header in the same allocation.
 */
struct pdf_arena_block {
	struct pdf_arena_block *next;
	size_t used;
	size_t size;
};

/*
 * The memory every object of one document is carved from.
 *
 * Nothing is freed on its own; the whole arena is freed when the document
 * closes.  total counts every byte handed out, which the arena's limit
 * bounds.
 */
struct pdf_arena {
	struct pdf_arena_block *blocks;
	size_t total;
};

/*
 * A position in a document's bytes and the arena its tokens are decoded into.
 */
struct pdf_lexer {
	const unsigned char *data;
	size_t size;
	size_t position;
	struct pdf_arena *arena;
};

/*
 * One token.
 *
 * A keyword's bytes point into the document; a name's and a string's are
 * decoded into the arena.
 */
struct pdf_token {
	enum pdf_token_type type;
	long integer;
	double real;
	const unsigned char *bytes;
	size_t length;
};

void *pdf_arena_allocate(struct pdf_arena *arena, size_t size);
void pdf_arena_free(struct pdf_arena *arena);
int pdf_lexer_next(struct pdf_lexer *lexer, struct pdf_token *token);
void pdf_lexer_skip_space(struct pdf_lexer *lexer);
int pdf_token_is_keyword(const struct pdf_token *token, const char *keyword);
int pdf_parse_object(struct pdf_lexer *lexer, int depth, struct pdf_object **object);
struct pdf_object *pdf_object_get(const struct pdf_object *dictionary, const char *key);
int pdf_object_is_name(const struct pdf_object *object, const char *name);
int pdf_object_number(const struct pdf_object *object, double *number);

#endif /* LIBPDF_INTERNAL_H */
