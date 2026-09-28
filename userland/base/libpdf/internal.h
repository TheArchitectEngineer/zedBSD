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
 * is a range of the document's bytes, or of bytes when they are set (an
 * inline image, whose data is in a content stream).  Every object lives in
 * the document's arena (or a page run's) and is freed with it.
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

/* The most bytes one stream may decode to, the decode limit of design-pdf.md section 4.3. */
#define PDF_FILTER_OUTPUT_MAX ((size_t)256 * 1024 * 1024)

/* The longest side of an image, in samples. */
#define PDF_IMAGE_SIDE_MAX 16384

/* The most items, path points and image pixels one display list may hold. */
#define PDF_DISPLAY_ITEMS_MAX ((size_t)1048576)
#define PDF_DISPLAY_POINTS_MAX ((size_t)8388608)
#define PDF_DISPLAY_PIXELS_MAX ((size_t)64 * 1024 * 1024)

/*
 * A display list while a page is interpreted into it.
 *
 * The public list comes first, so the builder is the list the caller
 * receives.  Each item's path is kept as offsets into the shared verb and
 * point arrays while they may still move; pdf_display_finish() turns the
 * offsets into the items' pointers.  The images' pixels belong to the
 * builder and are freed with it.
 */
struct pdf_display_builder {
	struct pdf_display_list list;
	struct pdf_display_item *items;
	size_t *verb_starts;
	size_t *point_starts;
	size_t items_count;
	size_t items_capacity;
	unsigned char *verbs;
	size_t verbs_count;
	size_t verbs_capacity;
	struct pdf_point *points;
	size_t points_count;
	size_t points_capacity;
	unsigned char **images;
	size_t images_count;
	size_t images_capacity;
	size_t pixels_count;
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

/* What the content interpreter needs of a document being read (reader.c). */
int pdf_reader_resolve(struct pdf_document *document, struct pdf_object *object, struct pdf_object **resolved);
int pdf_reader_resolve_key(struct pdf_document *document, const struct pdf_object *dictionary, const char *key, struct pdf_object **resolved);
int pdf_reader_page(struct pdf_document *document, size_t index, struct pdf_object **page, struct pdf_object **resources);
const unsigned char *pdf_reader_bytes(const struct pdf_document *document);

/* What an update needs of the document it adds to (reader.c). */
size_t pdf_reader_size(const struct pdf_document *document);
void pdf_reader_roots(struct pdf_document *document, struct pdf_object **trailer, struct pdf_object **catalog);
unsigned long pdf_reader_next_number(const struct pdf_document *document);
int pdf_reader_page_reference(struct pdf_document *document, size_t index, struct pdf_object **reference);

/* The stream filters (filter.c). */
int pdf_filter_decode(struct pdf_document *document, const struct pdf_object *stream, int stop_at_dct, const unsigned char **data, size_t *size, unsigned char **owned, int *dct);

/* The images (image.c). */
int pdf_image_decode(struct pdf_document *document, const struct pdf_object *stream, const double fill[3], unsigned char **pixels, size_t *width, size_t *height, int *interpolate, unsigned *flags);

/* The display list being built (display.c). */
int pdf_display_create(struct pdf_display_builder **builder);
void pdf_display_free(struct pdf_display_builder *builder);
int pdf_display_add_path(struct pdf_display_builder *builder, enum pdf_item_type type, const unsigned char *verbs, size_t verb_count, const struct pdf_point *points, size_t point_count, const struct pdf_display_item *style);
int pdf_display_add_image(struct pdf_display_builder *builder, unsigned char *pixels, const struct pdf_display_item *style);
int pdf_display_add_clip_pop(struct pdf_display_builder *builder);
void pdf_display_finish(struct pdf_display_builder *builder);

/* The stroker (stroke.c): the outline a stroked path covers, in the path's own space. */
struct pdf_stroke_style {
	double width;
	int cap;
	int join;
	double miter_limit;
	const double *dash;
	size_t dash_count;
	double dash_phase;
	double tolerance;
};
int pdf_stroke_path(const unsigned char *verbs, size_t verb_count, const struct pdf_point *points, size_t point_count, const struct pdf_stroke_style *style, unsigned char **out_verbs, size_t *out_verb_count, struct pdf_point **out_points, size_t *out_point_count);

/*
 * One glyph name of a /Differences array and the character it stands for
 * (encoding.c).
 */
struct pdf_glyph_name {
	const char *name;
	unsigned short unicode;
};

/* The simple fonts' base encodings and glyph names (encoding.c). */
extern const unsigned short pdf_encoding_standard[256];
extern const unsigned short pdf_encoding_win_ansi[256];
extern const unsigned short pdf_encoding_mac_roman[256];
extern const struct pdf_glyph_name pdf_glyph_names[];
extern const size_t pdf_glyph_names_count;

/*
 * A font of a document, and a document's fonts (font.c).
 *
 * The fonts live in the document until it is closed.
 */
struct pdf_font;
struct pdf_font_cache;

/*
 * What one character code of a shown string draws.
 *
 * width is the horizontal displacement in ems of the font size (a vertical
 * font's glyph moves by vertical_advance instead, from the origin
 * origin_x, origin_y).  When drawable, the outline (verbs and points) is in
 * ems with y upward, and transform (a b c d) maps it into text space before
 * the font size: it narrows or leans a substitute.  bold is the width, in
 * ems, of the stroke that thickens a substitute standing in for a bold
 * face.
 */
struct pdf_glyph {
	double width;
	double vertical_advance;
	double origin_x;
	double origin_y;
	double transform[4];
	double bold;
	int drawable;
	const unsigned char *verbs;
	size_t verb_count;
	const struct pdf_point *points;
	size_t point_count;
};

int pdf_font_get(struct pdf_document *document, struct pdf_object *dictionary, struct pdf_font **font);
void pdf_font_cache_free(struct pdf_font_cache *cache);
unsigned pdf_font_status(const struct pdf_font *font);
int pdf_font_vertical(const struct pdf_font *font);
size_t pdf_font_next_code(const struct pdf_font *font, const unsigned char *bytes, size_t length, unsigned *code, int *single_byte);
int pdf_font_glyph(struct pdf_font *font, unsigned code, struct pdf_glyph *glyph);
unsigned pdf_glyph_name_unicode(const unsigned char *name, size_t length);
struct pdf_font_cache *pdf_reader_font_cache(struct pdf_document *document);

/*
 * A font program whose glyphs are charstrings: Type 1 (/FontFile) or the
 * Compact Font Format (/FontFile3), read by charstrings.c, type1.c and
 * cff.c (stage 3).
 */
struct pdf_charstrings;

/* Where a charstring's outline goes: one path step in ems with y upward, 0 or an errno value. */
typedef int (*pdf_charstrings_emit)(void *context, enum pdf_path_verb verb, const double *coordinates, size_t count);

int pdf_type1_open(const unsigned char *data, size_t size, size_t clear_length, struct pdf_charstrings **font);
int pdf_type1_encoding(const unsigned char *data, size_t size, const unsigned char *names[256], size_t lengths[256], int *standard);
int pdf_cff_open(const unsigned char *data, size_t size, struct pdf_charstrings **font);
int pdf_opentype_cff(const unsigned char *data, size_t size, const unsigned char **cff, size_t *cff_size);
void pdf_charstrings_close(struct pdf_charstrings *font);
size_t pdf_charstrings_count(const struct pdf_charstrings *font);
int pdf_charstrings_find(const struct pdf_charstrings *font, const unsigned char *name, size_t length, unsigned *glyph);
int pdf_charstrings_name(const struct pdf_charstrings *font, unsigned glyph, const unsigned char **name, size_t *length);
int pdf_charstrings_builtin(const struct pdf_charstrings *font, unsigned code, unsigned *glyph);
int pdf_charstrings_cid_keyed(const struct pdf_charstrings *font);
int pdf_charstrings_cid(const struct pdf_charstrings *font, unsigned cid, unsigned *glyph);
int pdf_charstrings_outline(struct pdf_charstrings *font, unsigned glyph, pdf_charstrings_emit emit, void *context, double *advance);

/* The smooth shadings (shading.c): a shading drawn into an image over a region of the page. */
int pdf_shading_image(struct pdf_document *document, struct pdf_object *object, const double matrix[6], const double bounds[4], unsigned char **pixels, size_t *width, size_t *height, double placement[6]);
void pdf_reader_set_font_cache(struct pdf_document *document, struct pdf_font_cache *cache, void (*release)(struct pdf_font_cache *cache));

#endif /* LIBPDF_INTERNAL_H */
