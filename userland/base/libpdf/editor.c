/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The editor of a page's objects (ws175-p002 and p003, plan/ws175/
 * phase001/design.md section 3): the page's content scanned once by the
 * interpreter (content.c, pdf_content_scan), the images and graphics of
 * its top level offered by index, by key and by the point they cover, and
 * the changes to them -- deleted, moved and sized, an image put in place
 * of one, new images inserted -- from which the page's new content is
 * built (section 3.4), drawn for the preview, and written by an update.
 *
 * An editor keeps the page's decoded content, which the objects' bytes are
 * ranges of, the scan's records (where q and Q are unbalanced, the page's
 * matrix), each object's change, the inserted objects after the page's
 * own, and the images it was given (their bytes copied).  The preview draws
 * the images through stream objects of its own arena on the page's
 * resources merged by a shallow copy (design.md [N5]: the page's fonts stay
 * the document's objects; the reader keeps no image by its object, so the
 * preview's objects may go with the editor, [H5] concerns the fonts of
 * p005).
 *
 * ws175-p006: the images may also be PNG rows taken as they are
 * (intake.c), turned by their EXIF orientation, and named by Notes' id,
 * which an update writes as their private key, shares among its pages,
 * and by which pdf_page_editor_read_image reads them back; the samples
 * and masks the update writes are compressed.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>
#include <sha2.h>

#include "internal.h"
#include "writer.h"

/* What has been done to an object: nothing, deleted, or placed by a matrix of the shown space. */
#define EDITOR_KEPT		0U
#define EDITOR_DELETED		1U
#define EDITOR_PLACED		2U

/* No object (hidden from the preview), no image. */
#define EDITOR_NONE		((size_t)-1)

/* The largest side of an image given, the writer's, and the most pixels (design.md section 5.1). */
#define EDITOR_IMAGE_SIDE_MAX	16384U
#define EDITOR_IMAGE_PIXELS_MAX	((size_t)64 * 1024 * 1024)

/* The largest side of a blank editor's page, in points (Notes' own largest). */
#define EDITOR_BLANK_SIDE_MAX	100000.0

/* The size of a source as ws175-p003 gave it, before its orientation and id. */
#define EDITOR_SOURCE_SIZE_P003	offsetof(struct pdf_image_source, orientation)

/* The prefix of the preview's names of the images (its own resources, never written). */
#define EDITOR_PREVIEW_PREFIX	"ZedPreviewIm"

/*
 * One image the editor was given: its bytes (a JPEG's as they are, a PNG's
 * compressed rows (rows), or RGB samples with an alpha mask when some
 * pixel is not opaque), its size and components, its EXIF orientation,
 * Notes' id and the digest of its bytes (ws175-p006), and the stream
 * object the preview draws it through (made the first time, in the
 * editor's arena; its bytes stay the editor's, design.md [N4]).
 */
struct editor_image {
	unsigned char *data;
	size_t length;
	unsigned char *alpha;
	size_t width;
	size_t height;
	int components;
	int is_jpeg;
	int rows;
	int orientation;
	unsigned long id;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	struct pdf_object *preview;
};

/*
 * One object's change: its state, its placement (the shown space's map
 * from where it was to where it goes), the image put in its place (or
 * inserted), and for an inserted object the map of an image's unit square
 * onto the shown space.
 */
struct editor_change {
	unsigned state;
	double placement[6];
	size_t image;
	int inserted;
	double square[6];
};

/*
 * One line of text of the page (ws175-p002b, design.md section 3.1): its
 * shown strings (first and count in the scan's), its flags
 * (PDF_EDIT_OBJECT_*), its corners, its characters in UTF-8 (in the
 * editor's arena), its font's name and its size as shown.
 */
struct editor_line {
	size_t first;
	size_t count;
	unsigned flags;
	double quad[8];
	char *text;
	char font_name[64];
	double size;
};

/*
 * One page's editor: the document and the page, the decoded content, the
 * scan of it, the page's PDF_EDIT_PAGE_* state, the changes of the
 * objects -- the page's images and graphics, then its lines of text, then
 * the inserted ones --, the lines, the images given, and the arena of the
 * preview's objects and the lines' text.  A blank editor (ws175-p007) owns
 * its document, one empty page made for it.
 */
struct pdf_page_editor {
	struct pdf_document *document;
	size_t index;
	unsigned char *content;
	size_t size;
	struct pdf_scan scan;
	unsigned status;
	struct editor_change *changes;
	size_t count;
	size_t capacity;
	struct editor_image *images;
	size_t image_count;
	size_t image_capacity;
	struct editor_line *lines;
	size_t line_count;
	struct pdf_arena arena;
	struct pdf_document *blank;
};

static int editor_writable(const struct pdf_page_editor *editor, size_t index);
static int editor_grow(struct pdf_page_editor *editor);
static int editor_take_image(struct pdf_page_editor *editor, const struct pdf_image_source *source, size_t *taken);
static int editor_write_image(struct pdf_writer *writer, const struct editor_image *image, size_t *index);
static int editor_read_back(const struct pdf_page_editor *editor, struct pdf_object *stream, unsigned long id, struct pdf_image_source *image, void **owned);
static int editor_read_samples(const struct pdf_page_editor *editor, struct pdf_object *stream, size_t width, size_t height, int components, unsigned char **pixels);
static void editor_quad(const struct pdf_page_editor *editor, size_t index, double quad[8]);
static int editor_fit(const struct editor_image *image, const double quad[8], double square[6]);
static int editor_draw_image(struct pdf_buffer *out, const struct editor_image *image, const double square[6], const double ctm[6], const char *prefix, size_t name);
static int editor_preview_resources(struct pdf_page_editor *editor, struct pdf_object **resources);
static struct pdf_object *editor_preview_image(struct pdf_page_editor *editor, struct editor_image *image);
static struct pdf_object *editor_object(struct pdf_arena *arena, int type, const char *text, long integer);
static struct pdf_object *editor_dictionary(struct pdf_arena *arena, size_t capacity);
static int editor_put(struct pdf_object *dictionary, size_t capacity, struct pdf_object *key, struct pdf_object *value);
static void editor_multiply(const double left[6], const double right[6], double product[6]);
static int editor_invert(const double matrix[6], double inverse[6]);
static void editor_number(struct pdf_buffer *buffer, double number);
static void editor_corners(const double placement[6], const double quad[8], double placed[8]);
static int editor_inside(const double quad[8], double x, double y);
static int editor_lines(struct pdf_page_editor *editor);
static int editor_joins(const struct pdf_scan_show *before, const struct pdf_scan_show *show);
static void editor_point(const double text[6], const double ctm[6], double x, double y, double point[2]);
static int editor_line_text(struct pdf_page_editor *editor, struct editor_line *line);
static size_t editor_drawn_at(const struct pdf_page_editor *editor, size_t index);

/*
 * Opens the editor of a page: the page's content is scanned, and the
 * page's state says whether it can be edited.  Returns 0, EINVAL, ENOMEM,
 * or the error of reading the page.
 */
int
pdf_page_editor_open(
	struct pdf_document *document,
	size_t index,
	struct pdf_page_editor **editor)
{
	struct pdf_page_editor *opened;
	unsigned read_flags;
	size_t at;
	int error;

	/* Refuses a missing document or result. */
	if (document == NULL || editor == NULL)
		return EINVAL;

	/* The editor's record. */
	opened = calloc(1, sizeof(*opened));
	if (opened == NULL)
		return ENOMEM;
	opened->document = document;
	opened->index = index;

	/* Scans the page's content. */
	error = pdf_content_scan(document, index, &opened->scan, &opened->content, &opened->size, &read_flags);
	if (error != 0) {
		free(opened);
		return error;
	}

	/* A stream that was not read keeps the page from being edited (design.md [M5]). */
	if ((read_flags & PDF_DISPLAY_SKIPPED) != 0U)
		opened->status |= PDF_EDIT_PAGE_SKIPPED;
	if ((read_flags & PDF_DISPLAY_LIMITED) != 0U)
		opened->status |= PDF_EDIT_PAGE_LIMITED;
	if ((read_flags & PDF_DISPLAY_DAMAGED) != 0U)
		opened->status |= PDF_EDIT_PAGE_DAMAGED;

	/* Objects the scan left out. */
	if (opened->scan.partial)
		opened->status |= PDF_EDIT_PAGE_PARTIAL;

	/* The page's lines of text (p002b). */
	error = editor_lines(opened);
	if (error != 0) {
		pdf_page_editor_close(opened);
		return error;
	}

	/* Every object of the page as it is: its images and graphics, then its lines. */
	while (opened->capacity < opened->scan.count + opened->line_count) {
		error = editor_grow(opened);
		if (error != 0) {
			pdf_page_editor_close(opened);
			return error;
		}
	}

	/* Nothing done to them yet. */
	for (at = 0; at < opened->scan.count + opened->line_count; at++)
		opened->changes[at].image = EDITOR_NONE;
	opened->count = opened->scan.count + opened->line_count;

	/* Succeeded: the page's objects are listed. */
	*editor = opened;
	return 0;
}

/*
 * Closes an editor.
 */
void
pdf_page_editor_close(
	struct pdf_page_editor *editor)
{
	size_t at;

	/* Nothing to close. */
	if (editor == NULL)
		return;

	/* The images given. */
	for (at = 0; at < editor->image_count; at++) {
		free(editor->images[at].data);
		free(editor->images[at].alpha);
	}

	/* The images' list. */
	free(editor->images);

	/* The scan, the content, the changes, the preview's objects, a blank editor's document, the record. */
	pdf_scan_free(&editor->scan);
	free(editor->content);
	free(editor->changes);
	free(editor->lines);
	pdf_arena_free(&editor->arena);
	pdf_document_close(editor->blank);
	free(editor);
}

/*
 * Opens a blank editor (ws175-p007, design.md [N2]): the editor of an
 * empty page of a size (points), without objects of its own, for the
 * images inserted on a page of Notes' own (a new page, or one whose
 * content is Notes' strokes), which pdf_writer_draw_page_editor draws.
 * Returns 0, EINVAL for a size that is not a page's, or ENOMEM.
 */
int
pdf_page_editor_blank(
	double width,
	double height,
	struct pdf_page_editor **editor)
{
	struct pdf_document *document;
	struct pdf_buffer file;
	size_t offsets[5];
	size_t table;
	size_t at;
	int error;

	/* A size a page has. */
	if (editor == NULL || !(width > 0.0 && width <= EDITOR_BLANK_SIDE_MAX) || !(height > 0.0 && height <= EDITOR_BLANK_SIDE_MAX))
		return EINVAL;

	/* The document: a catalog, the page tree, the page of that size, its empty content. */
	memset(&file, 0, sizeof(file));
	pdf_buffer_printf(&file, "%%PDF-1.7\n");
	offsets[1] = file.length;
	pdf_buffer_printf(&file, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	offsets[2] = file.length;
	pdf_buffer_printf(&file, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
	offsets[3] = file.length;
	pdf_buffer_printf(&file, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
	pdf_buffer_append_number(&file, width);
	pdf_buffer_append(&file, " ", 1);
	pdf_buffer_append_number(&file, height);
	pdf_buffer_printf(&file, "] /Resources << >> /Contents 4 0 R >>\nendobj\n");
	offsets[4] = file.length;
	pdf_buffer_printf(&file, "4 0 obj\n<< /Length 0 >>\nstream\n\nendstream\nendobj\n");

	/* Its cross-reference table and trailer. */
	table = file.length;
	pdf_buffer_printf(&file, "xref\n0 5\n0000000000 65535 f \n");
	for (at = 1; at < 5; at++)
		pdf_buffer_printf(&file, "%010lu 00000 n \n", (unsigned long)offsets[at]);
	pdf_buffer_printf(&file, "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)table);
	if (file.error != 0) {
		free(file.data);
		return ENOMEM;
	}

	/* Read as any document (the reader keeps its own copy of the bytes). */
	error = pdf_document_open_memory(file.data, file.length, &document);
	free(file.data);
	if (error != 0)
		return error;

	/* Its page's editor, which owns it. */
	error = pdf_page_editor_open(document, 0, editor);
	if (error != 0) {
		pdf_document_close(document);
		return error;
	}

	/* Succeeded: a blank editor. */
	(*editor)->blank = document;
	return 0;
}

/*
 * Reports the page's PDF_EDIT_PAGE_* state (0 for a page that can be
 * edited and whose objects are all listed).
 */
unsigned
pdf_page_editor_status(
	const struct pdf_page_editor *editor)
{
	/* No editor, no state. */
	if (editor == NULL)
		return 0U;

	/* The state found when it was opened. */
	return editor->status;
}

/*
 * Reports how many objects the page has: its own, then the inserted ones.
 */
size_t
pdf_page_editor_count(
	const struct pdf_page_editor *editor)
{
	/* No editor, no objects. */
	if (editor == NULL)
		return 0;

	/* The page's and the inserted. */
	return editor->count;
}

/*
 * Copies one object as the editor shows it (where it is now, deleted,
 * inserted).  object->size must be the caller's sizeof (at least the
 * fields up to image_height).  Returns 0, EINVAL, or ENOENT for an index
 * past the last object.
 */
int
pdf_page_editor_object(
	const struct pdf_page_editor *editor,
	size_t index,
	struct pdf_edit_object *object)
{
	const struct editor_change *change;
	const struct pdf_scan_object *found;
	const struct editor_line *line;
	size_t size;

	/* Refuses a missing editor or object, or one of an unknown size. */
	if (editor == NULL || object == NULL)
		return EINVAL;
	size = object->size;
	if (size < sizeof(*object))
		return EINVAL;

	/* The object by its index. */
	if (index >= editor->count)
		return ENOENT;
	change = &editor->changes[index];

	/* An image inserted: its image's size; one of the page: its kind and flags. */
	memset(object, 0, sizeof(*object));
	object->size = size;
	object->text = "";
	object->kind = PDF_EDIT_IMAGE;
	if (change->inserted) {
		object->flags |= PDF_EDIT_OBJECT_INSERTED;
	} else if (index >= editor->scan.count) {
		/* A line of text: its characters, its font and size, its flags. */
		line = &editor->lines[index - editor->scan.count];
		object->kind = PDF_EDIT_TEXT;
		object->flags |= line->flags;
		object->text = line->text;
		memcpy(object->font_name, line->font_name, sizeof(object->font_name));
		object->font_size = line->size;
	} else {
		found = &editor->scan.objects[index];
		if (found->kind != (unsigned)PDF_EDIT_IMAGE)
			object->kind = PDF_EDIT_GRAPHIC;
		if (found->clipped)
			object->flags |= PDF_EDIT_OBJECT_CLIPPED;
		object->image_width = found->width;
		object->image_height = found->height;
	}

	/* An image put in its place, or inserted, is that image's size and kind. */
	if (change->image != EDITOR_NONE) {
		object->kind = PDF_EDIT_IMAGE;
		object->image_width = editor->images[change->image].width;
		object->image_height = editor->images[change->image].height;
	}

	/* Where it is drawn now, and whether it was deleted. */
	editor_quad(editor, index, object->quad);
	if (change->state == EDITOR_DELETED)
		object->flags |= PDF_EDIT_OBJECT_DELETED;

	/* Succeeded: the object is copied. */
	return 0;
}

/*
 * Gives an object's key (an inserted object has none: ENOENT).  Returns 0,
 * EINVAL, ENOENT, or ERANGE for bytes past what a key holds.
 */
int
pdf_page_editor_key(
	const struct pdf_page_editor *editor,
	size_t index,
	struct pdf_edit_key *key)
{
	const struct pdf_scan_object *found;
	const struct pdf_scan_show *show;

	/* Refuses a missing editor or key. */
	if (editor == NULL || key == NULL)
		return EINVAL;

	/* A line of text: its first shown string's bytes and their fingerprint (design.md [H3]). */
	if (index >= editor->scan.count && index < editor->scan.count + editor->line_count) {
		show = &editor->scan.shows[editor->lines[index - editor->scan.count].first];
		if (show->length > 0xffffffffUL)
			return ERANGE;
		memset(key, 0, sizeof(*key));
		key->kind = PDF_EDIT_TEXT;
		key->offset = (uint64_t)show->offset;
		key->length = (uint32_t)show->length;
		memcpy(key->fingerprint, show->fingerprint, sizeof(key->fingerprint));
		return 0;
	}

	/* An image or a graphic of the page by its index. */
	if (index >= editor->scan.count)
		return ENOENT;
	found = &editor->scan.objects[index];

	/* A length a key cannot hold (the content's limit is 256 MB, so this does not happen). */
	if (found->length > 0xffffffffUL)
		return ERANGE;

	/* Its kind, the place and length of its bytes, and their fingerprint. */
	memset(key, 0, sizeof(*key));
	key->kind = PDF_EDIT_GRAPHIC;
	if (found->kind == (unsigned)PDF_EDIT_IMAGE)
		key->kind = PDF_EDIT_IMAGE;
	key->offset = (uint64_t)found->offset;
	key->length = (uint32_t)found->length;
	memcpy(key->fingerprint, found->fingerprint, sizeof(key->fingerprint));

	/* Succeeded: the key is the object's. */
	return 0;
}

/*
 * Finds the object of the page a key names: its kind, place, length and
 * fingerprint all the same.  Returns 0, EINVAL, or ENOENT when the page
 * has none.
 */
int
pdf_page_editor_find(
	const struct pdf_page_editor *editor,
	const struct pdf_edit_key *key,
	size_t *index)
{
	struct pdf_edit_key each;
	size_t at;
	int differs;
	int error;

	/* Refuses a missing editor, key or result. */
	if (editor == NULL || key == NULL || index == NULL)
		return EINVAL;

	/* Compares the key with each object's of the page (its lines too). */
	for (at = 0; at < editor->scan.count + editor->line_count; at++) {
		/* The object's key. */
		error = pdf_page_editor_key(editor, at, &each);
		if (error != 0)
			continue;

		/* The same kind, place and length. */
		if (each.kind != key->kind || each.offset != key->offset || each.length != key->length)
			continue;

		/* And the same bytes. */
		differs = memcmp(each.fingerprint, key->fingerprint, sizeof(each.fingerprint));
		if (differs != 0)
			continue;

		/* Succeeded: the key's object. */
		*index = at;
		return 0;
	}

	/* No object of the key. */
	return ENOENT;
}

/*
 * Finds the object drawn on top at a point of the shown space (the last
 * drawn whose corners hold it, the inserted ones over the page's; a
 * deleted one is not there).  Returns 0, EINVAL, or ENOENT when no object
 * is there.
 */
int
pdf_page_editor_hit(
	const struct pdf_page_editor *editor,
	double x,
	double y,
	size_t *index)
{
	double quad[8];
	size_t best;
	size_t best_at;
	size_t drawn;
	size_t at;
	int invisible;
	int pass;
	int inside;

	/* Refuses a missing editor or result. */
	if (editor == NULL || index == NULL)
		return EINVAL;

	/* The visible objects first; an invisible line (an OCR layer) only where nothing else is (design.md [M9]). */
	for (pass = 0; pass < 2; pass++) {
		/* The object drawn last whose corners hold the point. */
		best = EDITOR_NONE;
		best_at = 0;
		for (at = 0; at < editor->count; at++) {
			/* A deleted object is not there; the invisible lines wait for the second pass. */
			if (editor->changes[at].state == EDITOR_DELETED)
				continue;
			invisible = 0;
			if (at >= editor->scan.count && at < editor->scan.count + editor->line_count && (editor->lines[at - editor->scan.count].flags & PDF_EDIT_OBJECT_INVISIBLE) != 0U)
				invisible = 1;
			if (invisible != pass)
				continue;

			/* Its corners, and where in the drawing it comes. */
			editor_quad(editor, at, quad);
			inside = editor_inside(quad, x, y);
			if (!inside)
				continue;
			drawn = editor_drawn_at(editor, at);
			if (best == EDITOR_NONE || drawn >= best_at) {
				best = at;
				best_at = drawn;
			}
		}

		/* Succeeded: the object on top there. */
		if (best != EDITOR_NONE) {
			*index = best;
			return 0;
		}
	}

	/* Nothing there. */
	return ENOENT;
}

/*
 * Puts an object back as the page has it (its image too); an inserted one
 * back where it was inserted.  Returns 0, EINVAL, ENOENT, or EPERM for a
 * page that cannot be edited.
 */
int
pdf_page_editor_reset(
	struct pdf_page_editor *editor,
	size_t index)
{
	int error;

	/* An object of a page that can be edited. */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;

	/* As it was (an inserted object keeps its image). */
	editor->changes[index].state = EDITOR_KEPT;
	if (!editor->changes[index].inserted)
		editor->changes[index].image = EDITOR_NONE;
	return 0;
}

/*
 * Deletes an object: the new content leaves it out.  Returns 0, EINVAL,
 * ENOENT, or EPERM for a page that cannot be edited.
 */
int
pdf_page_editor_delete(
	struct pdf_page_editor *editor,
	size_t index)
{
	int error;

	/* An object of a page that can be edited (a line of text is p004's). */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;
	if (index >= editor->scan.count && index < editor->scan.count + editor->line_count)
		return ENOTSUP;

	/* Gone from the new content. */
	editor->changes[index].state = EDITOR_DELETED;
	return 0;
}

/*
 * Moves and sizes an object: transform is the affine map of the shown
 * space (a point p goes to p times it, design.md [M1]) from where the page
 * (or the insertion) draws the object to where it goes.  A placement that
 * cannot be undone (no area) is refused.  Returns 0, EINVAL, ENOENT, or
 * EPERM.
 */
int
pdf_page_editor_place(
	struct pdf_page_editor *editor,
	size_t index,
	const double transform[6])
{
	double inverse[6];
	double size;
	size_t item;
	int error;

	/* An object of a page that can be edited (a line of text is p004's), and a placement. */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;
	if (index >= editor->scan.count && index < editor->scan.count + editor->line_count)
		return ENOTSUP;
	if (transform == NULL)
		return EINVAL;

	/* Numbers (not NaN, within what a PDF real holds). */
	for (item = 0; item < 6; item++) {
		size = fabs(transform[item]);
		if (!(transform[item] == transform[item]) || size >= 1e9)
			return EINVAL;
	}

	/* A map that keeps an area, and an object's matrix that does too. */
	error = editor_invert(transform, inverse);
	if (error != 0)
		return EINVAL;
	if (!editor->changes[index].inserted) {
		error = editor_invert(editor->scan.objects[index].ctm, inverse);
		if (error != 0)
			return EINVAL;
	}

	/* Placed. */
	memcpy(editor->changes[index].placement, transform, sizeof(editor->changes[index].placement));
	editor->changes[index].state = EDITOR_PLACED;
	return 0;
}

/*
 * Puts an image in an object's place (an image or a graphic of the page,
 * or an inserted image): the image fills the object's corners where they
 * are now, its own proportions kept, centred.  Returns 0, EINVAL for an
 * image that cannot be taken (a JPEG of four components, design.md [M15]),
 * ENOENT, EPERM, or ENOMEM.
 */
int
pdf_page_editor_set_image(
	struct pdf_page_editor *editor,
	size_t index,
	const struct pdf_image_source *image)
{
	size_t taken;
	int error;

	/* An object of a page that can be edited (not a line of text), and an image. */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;
	if (index >= editor->scan.count && index < editor->scan.count + editor->line_count)
		return ENOTSUP;
	error = editor_take_image(editor, image, &taken);
	if (error != 0)
		return error;

	/* The image in the object's place (a deleted object comes back with it). */
	editor->changes[index].image = taken;
	if (editor->changes[index].state == EDITOR_DELETED)
		editor->changes[index].state = EDITOR_KEPT;
	return 0;
}

/*
 * Inserts an image over the page's objects: placement maps the image's
 * unit square onto the shown space (its top left where (0, 1) goes).  The
 * new object comes after every other.  Returns 0, EINVAL, EPERM, or
 * ENOMEM.
 */
int
pdf_page_editor_insert_image(
	struct pdf_page_editor *editor,
	const struct pdf_image_source *image,
	const double placement[6],
	size_t *index)
{
	struct editor_change *change;
	double inverse[6];
	size_t taken;
	int error;

	/* A page that can be edited, an image and where it goes. */
	if (editor == NULL || placement == NULL || index == NULL)
		return EINVAL;
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return EPERM;
	error = editor_invert(placement, inverse);
	if (error != 0)
		return EINVAL;

	/* Room for one more object. */
	if (editor->count == editor->capacity) {
		error = editor_grow(editor);
		if (error != 0)
			return error;
	}

	/* The image. */
	error = editor_take_image(editor, image, &taken);
	if (error != 0)
		return error;

	/* Succeeded: the new object, the last. */
	change = &editor->changes[editor->count];
	memset(change, 0, sizeof(*change));
	change->state = EDITOR_KEPT;
	change->image = taken;
	change->inserted = 1;
	memcpy(change->square, placement, sizeof(change->square));
	*index = editor->count;
	editor->count++;
	return 0;
}

/*
 * Draws the page with the editor's changes (the preview): the new content
 * on the page's resources with the editor's images, an object hidden from
 * it when hidden is its index ((size_t)-1 for none, as while it is
 * dragged).  Returns 0, EINVAL, ENOMEM, or the failure of the page.
 */
int
pdf_page_editor_render(
	struct pdf_page_editor *editor,
	size_t hidden,
	struct pdf_display_list **list)
{
	struct pdf_object *resources;
	struct pdf_buffer content;
	size_t *names;
	size_t at;
	int error;

	/* Refuses a missing editor or list. */
	if (editor == NULL || list == NULL)
		return EINVAL;

	/* A page that cannot be edited is drawn as it is. */
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return pdf_page_render(editor->document, editor->index, list);

	/* The preview's names of the images are their indices. */
	names = NULL;
	if (editor->image_count > 0) {
		names = malloc(editor->image_count * sizeof(*names));
		if (names == NULL)
			return ENOMEM;
		for (at = 0; at < editor->image_count; at++)
			names[at] = at;
	}

	/* The page's resources with the images. */
	resources = NULL;
	error = 0;
	if (editor->image_count > 0)
		error = editor_preview_resources(editor, &resources);

	/* The new content, then the page drawn with it. */
	memset(&content, 0, sizeof(content));
	if (error == 0)
		error = pdf_editor_content(editor, hidden, EDITOR_PREVIEW_PREFIX, names, &content);
	if (error == 0)
		error = pdf_content_render(editor->document, editor->index, content.data, content.length, resources, list);
	free(content.data);
	free(names);
	return error;
}

/*
 * Builds a page's new content (design.md section 3.4): the page's own
 * within q and Q, each changed object's bytes replaced (a deleted one, or
 * the one hidden, left out; a placed one inside q M cm ... Q, M the
 * placement in the object's own space; one given an image drawn as that
 * image fitted into its corners), the Q without a q left out, the text
 * object and the q the content left open closed; then the inserted images.
 * An image is named prefix and names[its index].  Returns 0, EINVAL, or
 * ENOMEM.
 */
int
pdf_editor_content(
	const struct pdf_page_editor *editor,
	size_t hidden,
	const char *prefix,
	const size_t *names,
	struct pdf_buffer *out)
{
	const struct pdf_scan_object *object;
	const struct editor_change *change;
	double inverse[6];
	double through[6];
	double matrix[6];
	double square[6];
	double quad[8];
	size_t position;
	size_t object_at;
	size_t stray_at;
	size_t offset;
	size_t item;
	int error;

	/* The page's content, from its start, within a level of its own. */
	pdf_buffer_append(out, "q\n", 2);
	position = 0;
	object_at = 0;
	stray_at = 0;

	/* The changed objects and the stray Q, in the order of the content. */
	for (;;) {
		/* The next object of the page that changed, and the next stray Q. */
		while (object_at < editor->scan.count &&
		       editor->changes[object_at].state == EDITOR_KEPT &&
		       editor->changes[object_at].image == EDITOR_NONE &&
		       object_at != hidden)
			object_at++;
		if (object_at >= editor->scan.count && stray_at >= editor->scan.stray_count)
			break;

		/* A stray Q before the next changed object is left out. */
		if (stray_at < editor->scan.stray_count && (object_at >= editor->scan.count || editor->scan.stray_restores[stray_at] < editor->scan.objects[object_at].offset)) {
			offset = editor->scan.stray_restores[stray_at];
			pdf_buffer_append(out, editor->content + position, offset - position);
			pdf_buffer_append(out, " ", 1);
			position = offset + 1U;
			stray_at++;
			continue;
		}

		/* The bytes before the object as they are. */
		object = &editor->scan.objects[object_at];
		change = &editor->changes[object_at];
		pdf_buffer_append(out, editor->content + position, object->offset - position);
		position = object->offset + object->length;

		/* A deleted (or hidden) object leaves only a space. */
		if (change->state == EDITOR_DELETED || object_at == hidden) {
			pdf_buffer_append(out, " ", 1);
			object_at++;
			continue;
		}

		/* An image in its place: fitted into its corners where they are now. */
		if (change->image != EDITOR_NONE) {
			editor_quad(editor, object_at, quad);
			error = editor_fit(&editor->images[change->image], quad, square);
			if (error == 0)
				error = editor_draw_image(out, &editor->images[change->image], square, object->ctm, prefix, names[change->image]);
			if (error != 0)
				return error;
			object_at++;
			continue;
		}

		/* A placed one: M = C_rec times S times C_rec's inverse, so that M times C_rec is C_rec times S. */
		error = editor_invert(object->ctm, inverse);
		if (error != 0)
			return EINVAL;
		editor_multiply(object->ctm, change->placement, through);
		editor_multiply(through, inverse, matrix);
		pdf_buffer_append(out, " q ", 3);
		for (item = 0; item < 6; item++) {
			editor_number(out, matrix[item]);
			pdf_buffer_append(out, " ", 1);
		}

		/* The object's own bytes inside its level. */
		pdf_buffer_append(out, "cm ", 3);
		pdf_buffer_append(out, editor->content + object->offset, object->length);
		pdf_buffer_append(out, " Q ", 3);
		object_at++;
	}

	/* The rest of the content, a text object left open closed, and every level it left open. */
	pdf_buffer_append(out, editor->content + position, editor->size - position);
	pdf_buffer_append(out, "\n", 1);
	if (editor->scan.in_text)
		pdf_buffer_append(out, "ET\n", 3);
	for (item = 0; item < editor->scan.open_saves; item++)
		pdf_buffer_append(out, "Q\n", 2);
	pdf_buffer_append(out, "Q\n", 2);

	/* The inserted images over the page, in the page's first matrix (B). */
	for (object_at = editor->scan.count + editor->line_count; object_at < editor->count; object_at++) {
		/* One that is still there, where it is now. */
		change = &editor->changes[object_at];
		if (change->state == EDITOR_DELETED || object_at == hidden)
			continue;
		memcpy(square, change->square, sizeof(square));
		if (change->state == EDITOR_PLACED)
			editor_multiply(change->square, change->placement, square);
		error = editor_draw_image(out, &editor->images[change->image], square, editor->scan.base, prefix, names[change->image]);
		if (error != 0)
			return error;
		pdf_buffer_append(out, "\n", 1);
	}

	/* Reports a buffer that could not grow. */
	if (out->error != 0)
		return out->error;

	/* Succeeded: the content is built. */
	return 0;
}

/*
 * Lists the editor's page in an update with its changes (the editor's new
 * content, its images the document's from now), and opens it for drawing
 * over it, as pdf_writer_begin_page_over draws (the page as shown).  The
 * editor must be of the update's document, its page the next to list and
 * editable.  An image of Notes (its id) the update has already written
 * for another page is that page's (the same bytes; another image of the
 * same id is EINVAL); the others are compressed when that is shorter
 * (ws175-p006, design.md [H6] and [M6]).  Returns 0, EINVAL, EPERM,
 * ENOMEM, ENOSPC, or the failure of the page.
 */
int
pdf_writer_begin_page_edited(
	struct pdf_writer *writer,
	const struct pdf_page_editor *editor)
{
	struct pdf_buffer edited;
	char prefix[32];
	size_t *names;
	size_t object;
	size_t at;
	int used;
	int error;

	/* An editor of this update's document, of a page that can be edited. */
	if (writer == NULL || editor == NULL || editor->document != writer->base)
		return EINVAL;
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return EPERM;

	/* Each image the editor was given becomes the document's, named by its index there. */
	names = NULL;
	if (editor->image_count > 0) {
		names = calloc(editor->image_count, sizeof(*names));
		if (names == NULL)
			return ENOMEM;
	}

	/* Each image an object still shows (one replaced again or deleted is not written). */
	for (at = 0; at < editor->image_count; at++) {
		/* Only an image in use. */
		used = 0;
		for (object = 0; object < editor->count; object++) {
			if (editor->changes[object].image == at && editor->changes[object].state != EDITOR_DELETED)
				used = 1;
		}

		/* One not in use is left out. */
		if (!used)
			continue;

		/* The document's image. */
		error = editor_write_image(writer, &editor->images[at], &names[at]);
		if (error != 0) {
			free(names);
			return error;
		}
	}

	/* The page's new content, the images named as the writer names them (its prefix, "Im", the index), which the update's page takes. */
	(void)snprintf(prefix, sizeof(prefix), "%sIm", writer->name_prefix);
	memset(&edited, 0, sizeof(edited));
	error = pdf_editor_content(editor, EDITOR_NONE, prefix, names, &edited);
	free(names);
	if (error != 0) {
		free(edited.data);
		return error;
	}

	/* Listed, open for drawing over it. */
	error = pdf_update_begin_edited(writer, editor->index, &edited);
	if (error != 0)
		return error;

	/* Succeeded: drawing goes over the changed page. */
	return 0;
}

/*
 * Reads an image of Notes back from the editor's page: the image of the
 * page's XObjects whose private key /KeiNotesImage is id (ws175-p006,
 * design.md [M6]), as a source the editor takes again -- a JPEG's bytes
 * (DCTDecode), a PNG's rows (FlateDecode with the PNG predictor, kept
 * compressed), or RGBA (the samples and the soft mask decoded) --, its
 * data in *owned, which the caller frees.  Returns 0, EINVAL, ENOENT for
 * an id the page does not have, ENOTSUP for an image of another form or
 * of an encrypted document, ENOMEM, or the failure of its streams.
 */
int
pdf_page_editor_read_image(
	const struct pdf_page_editor *editor,
	unsigned long id,
	struct pdf_image_source *image,
	void **owned)
{
	struct pdf_object *page;
	struct pdf_object *resources;
	struct pdf_object *xobjects;
	struct pdf_object *stream;
	struct pdf_object *key;
	size_t at;
	int error;

	/* An editor, an id and room for the image. */
	if (editor == NULL || id == 0UL || image == NULL || owned == NULL || image->size < EDITOR_SOURCE_SIZE_P003)
		return EINVAL;
	*owned = NULL;

	/* The page's XObjects. */
	error = pdf_reader_page(editor->document, editor->index, &page, &resources);
	if (error != 0)
		return error;
	if (resources == NULL || resources->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;
	error = pdf_reader_resolve_key(editor->document, resources, "XObject", &xobjects);
	if (error != 0 || xobjects->type != PDF_OBJECT_DICTIONARY)
		return ENOENT;

	/* The image whose private key is the id. */
	for (at = 0; at < xobjects->count; at++) {
		error = pdf_reader_resolve(editor->document, xobjects->values[at], &stream);
		if (error != 0 || stream->type != PDF_OBJECT_STREAM)
			continue;
		key = pdf_object_get(stream, "KeiNotesImage");
		if (key == NULL || key->type != PDF_OBJECT_INTEGER || key->integer <= 0 || (unsigned long)key->integer != id)
			continue;

		/* Found: read back. */
		return editor_read_back(editor, stream, id, image, owned);
	}

	/* Not on the page. */
	return ENOENT;
}

/*
 * Draws a blank editor's inserted images on the writer's open page, under
 * what is drawn on it next (ws175-p007, design.md [N2]): a page Notes made
 * or replaces, whose drawing is in the page's shown space.  The images
 * become the document's as pdf_writer_begin_page_edited makes them (an
 * image of Notes shared by its id).  Returns 0, EINVAL (not a blank
 * editor, no open page), ENOMEM, or ENOSPC.
 */
int
pdf_writer_draw_page_editor(
	struct pdf_writer *writer,
	const struct pdf_page_editor *editor)
{
	static const double identity[6] = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
	const struct editor_change *change;
	struct pdf_buffer *content;
	double square[6];
	char prefix[32];
	size_t object;
	size_t name;
	int error;

	/* A blank editor, and a page open for drawing. */
	if (writer == NULL || editor == NULL || editor->blank == NULL || !writer->page_is_open || writer->pages_count == 0)
		return EINVAL;
	content = &writer->pages[writer->pages_count - 1U]->content;
	(void)snprintf(prefix, sizeof(prefix), "%sIm", writer->name_prefix);

	/* Each inserted image still there, where it is now. */
	for (object = editor->scan.count + editor->line_count; object < editor->count; object++) {
		change = &editor->changes[object];
		if (change->state == EDITOR_DELETED || change->image == EDITOR_NONE)
			continue;
		memcpy(square, change->square, sizeof(square));
		if (change->state == EDITOR_PLACED)
			editor_multiply(change->square, change->placement, square);

		/* The document's image, drawn in the shown space. */
		error = editor_write_image(writer, &editor->images[change->image], &name);
		if (error != 0)
			return error;
		error = editor_draw_image(content, &editor->images[change->image], square, identity, prefix, name);
		if (error != 0)
			return error;
		pdf_buffer_append(content, "\n", 1);
	}

	/* Reports a content that could not grow. */
	if (content->error != 0)
		return content->error;

	/* Succeeded: the images are drawn. */
	return 0;
}

/*
 * Tells whether an object of an editor may be changed.  Returns 0,
 * EINVAL, ENOENT, or EPERM for a page that cannot be edited.
 */
static int
editor_writable(
	const struct pdf_page_editor *editor,
	size_t index)
{
	/* An editor and an object of it. */
	if (editor == NULL)
		return EINVAL;
	if (index >= editor->count)
		return ENOENT;

	/* A page whose content was read whole. */
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return EPERM;

	/* It may. */
	return 0;
}

/*
 * Gathers the page's shown strings into lines (design.md section 3.1): a
 * string joins the line before it when it is of the same text object, or
 * of the next one with nothing drawn and no graphics state changed between
 * (design.md [M9][N16]), in the same font, size and visibility, the same
 * direction and baseline (within a quarter of the size), and starts within
 * three sizes of where the line ends.  The strings of a text object that
 * clips (rendering modes 4 to 7, design.md [H2]) and empty ones are left
 * out.  Returns 0 or ENOMEM.
 */
static int
editor_lines(
	struct pdf_page_editor *editor)
{
	const struct pdf_scan_show *show;
	struct editor_line *line;
	struct editor_line *grown;
	size_t capacity;
	size_t at;
	int joins;
	int error;

	/* Each shown string in the order of the content. */
	capacity = 0;
	for (at = 0; at < editor->scan.show_count; at++) {
		/* A string of a clipping text object, or an empty one, is no line's. */
		show = &editor->scan.shows[at];
		if (show->characters_count == 0)
			continue;
		if (show->block < editor->scan.block_count && editor->scan.block_clips[show->block] != 0U)
			continue;

		/* Joins the line before it. */
		joins = 0;
		if (editor->line_count > 0) {
			line = &editor->lines[editor->line_count - 1U];
			joins = editor_joins(&editor->scan.shows[line->first + line->count - 1U], show);
		}

		/* The line grows by it. */
		if (joins) {
			line->count++;
			continue;
		}

		/* Or starts a new one. */
		if (editor->line_count == capacity) {
			capacity = capacity + capacity / 2U + 8U;
			grown = realloc(editor->lines, capacity * sizeof(*grown));
			if (grown == NULL)
				return ENOMEM;
			editor->lines = grown;
		}

		/* The new line, of this string. */
		line = &editor->lines[editor->line_count];
		memset(line, 0, sizeof(*line));
		line->first = at;
		line->count = 1;
		editor->line_count++;
	}

	/* Each line's corners, text, font and flags. */
	for (at = 0; at < editor->line_count; at++) {
		error = editor_line_text(editor, &editor->lines[at]);
		if (error != 0)
			return error;
	}

	/* Succeeded: the lines. */
	return 0;
}

/* Tells whether a shown string joins the line whose last string is before it. */
static int
editor_joins(
	const struct pdf_scan_show *before,
	const struct pdf_scan_show *show)
{
	double before_end[2];
	double ahead[2];
	double start[2];
	double across;
	double along;
	double length;
	double size;
	int differs;

	/* The same text object, or the next one with nothing drawn or changed between and the same matrix. */
	if (show->block != before->block) {
		if (show->block != before->block + 1U || show->drawn_before != 0)
			return 0;
		differs = memcmp(show->ctm, before->ctm, sizeof(show->ctm));
		if (differs != 0)
			return 0;
	}

	/* The same font, size, visibility and kind. */
	if (show->font != before->font || show->font_size != before->font_size)
		return 0;
	if ((show->render_mode == 3) != (before->render_mode == 3) || show->flags != before->flags)
		return 0;

	/* The direction: where one unit of the text's x goes from the line's end, in the shown space. */
	editor_point(before->end, before->ctm, 0.0, 0.0, before_end);
	editor_point(before->end, before->ctm, 1.0, 0.0, ahead);
	ahead[0] -= before_end[0];
	ahead[1] -= before_end[1];
	length = sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1]);
	if (!(length > 1e-9))
		return 0;
	ahead[0] /= length;
	ahead[1] /= length;

	/* The new string's start, along the line and across it, from the line's end. */
	editor_point(show->start, show->ctm, 0.0, 0.0, start);
	along = (start[0] - before_end[0]) * ahead[0] + (start[1] - before_end[1]) * ahead[1];
	across = -(start[0] - before_end[0]) * ahead[1] + (start[1] - before_end[1]) * ahead[0];

	/* Within a quarter of the size of the baseline, from a size back to three ahead. */
	size = fabs(before->font_size) * length;
	across = fabs(across);
	if (across > size / 4.0)
		return 0;
	if (along < -size || along > 3.0 * size)
		return 0;

	/* It joins. */
	return 1;
}

/* Maps a point of the text space through the text matrix and the matrix in force to the shown space. */
static void
editor_point(
	const double text[6],
	const double ctm[6],
	double x,
	double y,
	double point[2])
{
	double user[2];

	/* Into the user space, then the shown space. */
	user[0] = text[0] * x + text[2] * y + text[4];
	user[1] = text[1] * x + text[3] * y + text[5];
	point[0] = ctm[0] * user[0] + ctm[2] * user[1] + ctm[4];
	point[1] = ctm[1] * user[0] + ctm[3] * user[1] + ctm[5];
}

/*
 * Makes a line's corners (its first string's start to its last's end),
 * its characters in UTF-8 (a space between two strings apart by more than
 * a fifth of the size), its font's name without a subset's prefix, its size
 * as shown, and its flags.  Returns 0 or ENOMEM.
 */
static int
editor_line_text(
	struct pdf_page_editor *editor,
	struct editor_line *line)
{
	const struct pdf_scan_show *show;
	const struct pdf_scan_show *last;
	const struct pdf_object *dictionary;
	const struct pdf_object *name;
	double end[2];
	double start[2];
	double up[2];
	double gap;
	uint32_t character;
	size_t bytes;
	size_t length;
	size_t skip;
	size_t at;
	size_t from;
	char *text;

	/* The corners: the first string's left side, the last's right side. */
	show = &editor->scan.shows[line->first];
	last = &editor->scan.shows[line->first + line->count - 1U];
	line->quad[0] = show->quad[0];
	line->quad[1] = show->quad[1];
	line->quad[2] = last->quad[2];
	line->quad[3] = last->quad[3];
	line->quad[4] = last->quad[4];
	line->quad[5] = last->quad[5];
	line->quad[6] = show->quad[6];
	line->quad[7] = show->quad[7];

	/* The size as shown: how far one unit of the text's y goes, times the font size. */
	editor_point(show->start, show->ctm, 0.0, 0.0, start);
	editor_point(show->start, show->ctm, 0.0, 1.0, up);
	line->size = fabs(show->font_size) * sqrt((up[0] - start[0]) * (up[0] - start[0]) + (up[1] - start[1]) * (up[1] - start[1]));

	/* The flags: words that cannot be changed, an invisible line. */
	if ((show->flags & (PDF_SCAN_SHOW_UNKNOWN | PDF_SCAN_SHOW_TYPE3 | PDF_SCAN_SHOW_VERTICAL)) != 0U)
		line->flags |= PDF_EDIT_OBJECT_TEXT_FIXED;
	for (at = 0; at < line->count; at++) {
		if ((editor->scan.shows[line->first + at].flags & PDF_SCAN_SHOW_UNKNOWN) != 0U)
			line->flags |= PDF_EDIT_OBJECT_TEXT_FIXED;
	}

	/* Rendering mode 3 shows nothing. */
	if (show->render_mode == 3)
		line->flags |= PDF_EDIT_OBJECT_INVISIBLE;

	/* The font's name, without the six letters and the plus of a subset. */
	dictionary = pdf_font_dictionary(show->font);
	name = pdf_object_get(dictionary, "BaseFont");
	if (name != NULL && name->type == PDF_OBJECT_NAME) {
		skip = 0;
		if (name->length > 7U && name->bytes[6] == '+')
			skip = 7;
		length = name->length - skip;
		if (length >= sizeof(line->font_name))
			length = sizeof(line->font_name) - 1U;
		memcpy(line->font_name, name->bytes + skip, length);
		line->font_name[length] = '\0';
	}

	/* The UTF-8 text: at most four bytes a character, a space between strings, the NUL. */
	bytes = 1;
	for (at = 0; at < line->count; at++)
		bytes += 4U * editor->scan.shows[line->first + at].characters_count + 1U;
	text = pdf_arena_allocate(&editor->arena, bytes);
	if (text == NULL)
		return ENOMEM;
	length = 0;
	for (at = 0; at < line->count; at++) {
		/* A space where the string starts apart from the one before. */
		show = &editor->scan.shows[line->first + at];
		if (at > 0) {
			editor_point(editor->scan.shows[line->first + at - 1U].end, show->ctm, 0.0, 0.0, end);
			editor_point(show->start, show->ctm, 0.0, 0.0, start);
			gap = sqrt((start[0] - end[0]) * (start[0] - end[0]) + (start[1] - end[1]) * (start[1] - end[1]));
			if (gap > line->size / 5.0)
				text[length++] = ' ';
		}

		/* Each character in UTF-8. */
		for (from = 0; from < show->characters_count; from++) {
			character = editor->scan.characters[show->characters_from + from];
			if (character < 0x80U) {
				text[length++] = (char)character;
			} else if (character < 0x800U) {
				text[length++] = (char)(0xc0U | (character >> 6));
				text[length++] = (char)(0x80U | (character & 0x3fU));
			} else if (character < 0x10000U) {
				text[length++] = (char)(0xe0U | (character >> 12));
				text[length++] = (char)(0x80U | ((character >> 6) & 0x3fU));
				text[length++] = (char)(0x80U | (character & 0x3fU));
			} else {
				text[length++] = (char)(0xf0U | (character >> 18));
				text[length++] = (char)(0x80U | ((character >> 12) & 0x3fU));
				text[length++] = (char)(0x80U | ((character >> 6) & 0x3fU));
				text[length++] = (char)(0x80U | (character & 0x3fU));
			}
		}
	}

	/* Succeeded: the text, ended. */
	text[length] = '\0';
	line->text = text;
	return 0;
}

/* Gives where in the drawing an object comes: its bytes' place (an inserted one after every byte). */
static size_t
editor_drawn_at(
	const struct pdf_page_editor *editor,
	size_t index)
{
	/* An image or a graphic of the page. */
	if (index < editor->scan.count)
		return editor->scan.objects[index].offset;

	/* A line: its last string's place. */
	if (index < editor->scan.count + editor->line_count)
		return editor->scan.shows[editor->lines[index - editor->scan.count].first + editor->lines[index - editor->scan.count].count - 1U].offset;

	/* An inserted object: after the page's own, in the order inserted. */
	return editor->size + index;
}

/* Grows the changes by half again.  Returns 0 or ENOMEM. */
static int
editor_grow(
	struct pdf_page_editor *editor)
{
	struct editor_change *grown;
	size_t capacity;

	/* The new room, every new change as nothing done. */
	capacity = editor->capacity + editor->capacity / 2 + 8;
	grown = realloc(editor->changes, capacity * sizeof(*grown));
	if (grown == NULL)
		return ENOMEM;
	memset(grown + editor->capacity, 0, (capacity - editor->capacity) * sizeof(*grown));

	/* Succeeded: the larger room. */
	editor->changes = grown;
	editor->capacity = capacity;
	return 0;
}

/*
 * Takes an image given: a JPEG of one or three components (its bytes as
 * they are), 8-bit RGBA (split into RGB samples and, when some pixel is
 * not opaque, an alpha mask), a PNG whose rows are taken as they are, or
 * such rows read back (ws175-p006), with its orientation and id, and the
 * digest by which an update shares it.  Returns 0, EINVAL, E2BIG past the
 * largest side or the most pixels, ENOTSUP for a PNG of another kind, or
 * ENOMEM.
 */
static int
editor_take_image(
	struct pdf_page_editor *editor,
	const struct pdf_image_source *source,
	size_t *taken)
{
	struct pdf_image_source given;
	struct editor_image image;
	struct editor_image *grown;
	SHA2_CTX context;
	const unsigned char *pixels;
	unsigned char kind;
	size_t capacity;
	size_t copied;
	size_t count;
	size_t at;
	int translucent;
	int error;

	/* An image (an older caller's without the orientation and id, which are then none). */
	if (source == NULL || source->size < EDITOR_SOURCE_SIZE_P003 || source->data == NULL || source->bytes == 0)
		return EINVAL;
	memset(&given, 0, sizeof(given));
	copied = source->size;
	if (copied > sizeof(given))
		copied = sizeof(given);
	memcpy(&given, source, copied);
	if (given.orientation < 0 || given.orientation > 8)
		return EINVAL;
	memset(&image, 0, sizeof(image));
	image.orientation = given.orientation;
	image.id = given.id;

	/* A PNG: its rows as they are, its size and components the file's. */
	if (given.kind == PDF_IMAGE_SOURCE_PNG) {
		error = pdf_png_rows(given.data, given.bytes, &image.width, &image.height, &image.components, &image.data, &image.length);
		if (error != 0)
			return error;
		image.rows = 1;
	} else {
		/* Any other: its size as given, within the reader's. */
		if (given.width == 0 || given.height == 0)
			return EINVAL;
		image.width = given.width;
		image.height = given.height;
	}

	/* Within the largest side and the most pixels. */
	if (image.width > EDITOR_IMAGE_SIDE_MAX || image.height > EDITOR_IMAGE_SIDE_MAX || image.width * image.height > EDITOR_IMAGE_PIXELS_MAX) {
		free(image.data);
		return E2BIG;
	}

	/* A JPEG: Gray or RGB, its bytes as they are (CMYK is refused, design.md [M15]). */
	if (given.kind == PDF_IMAGE_SOURCE_JPEG) {
		if (given.components != 1 && given.components != 3)
			return EINVAL;
		image.data = malloc(given.bytes);
		if (image.data == NULL)
			return ENOMEM;
		memcpy(image.data, given.data, given.bytes);
		image.length = given.bytes;
		image.components = given.components;
		image.is_jpeg = 1;
	} else if (given.kind == PDF_IMAGE_SOURCE_IDAT) {
		/* A PNG's rows read back: checked as a PNG's are, kept as they are. */
		error = pdf_png_check_rows(given.data, given.bytes, given.width, given.height, given.components);
		if (error != 0)
			return error;
		image.data = malloc(given.bytes);
		if (image.data == NULL)
			return ENOMEM;
		memcpy(image.data, given.data, given.bytes);
		image.length = given.bytes;
		image.components = given.components;
		image.rows = 1;
	} else if (given.kind == PDF_IMAGE_SOURCE_RGBA) {
		/* RGBA: the bytes are the rows of pixels. */
		count = given.width * given.height;
		if (given.bytes != count * 4U)
			return EINVAL;
		image.data = malloc(count * 3U);
		image.alpha = malloc(count);
		if (image.data == NULL || image.alpha == NULL) {
			free(image.data);
			free(image.alpha);
			return ENOMEM;
		}

		/* Each pixel's colour and alpha, noting whether any is not opaque. */
		pixels = given.data;
		translucent = 0;
		for (at = 0; at < count; at++) {
			image.data[at * 3U] = pixels[at * 4U];
			image.data[at * 3U + 1U] = pixels[at * 4U + 1U];
			image.data[at * 3U + 2U] = pixels[at * 4U + 2U];
			image.alpha[at] = pixels[at * 4U + 3U];
			if (pixels[at * 4U + 3U] != 255U)
				translucent = 1;
		}

		/* An opaque image needs no mask. */
		if (!translucent) {
			free(image.alpha);
			image.alpha = NULL;
		}

		/* Its length and components. */
		image.length = count * 3U;
		image.components = 3;
	} else if (given.kind != PDF_IMAGE_SOURCE_PNG) {
		return EINVAL;
	}

	/* The digest of what it is: its form, its bytes and its mask. */
	kind = 0;
	if (image.is_jpeg)
		kind = 1;
	if (image.rows)
		kind = 2;
	SHA256Init(&context);
	SHA256Update(&context, &kind, 1);
	SHA256Update(&context, image.data, image.length);
	if (image.alpha != NULL)
		SHA256Update(&context, image.alpha, image.width * image.height);
	SHA256Final(image.digest, &context);

	/* Room for it. */
	if (editor->image_count == editor->image_capacity) {
		capacity = editor->image_capacity + editor->image_capacity / 2 + 4;
		grown = realloc(editor->images, capacity * sizeof(*grown));
		if (grown == NULL) {
			free(image.data);
			free(image.alpha);
			return ENOMEM;
		}

		/* The larger list. */
		editor->images = grown;
		editor->image_capacity = capacity;
	}

	/* Succeeded: the editor's image, by its index. */
	editor->images[editor->image_count] = image;
	*taken = editor->image_count;
	editor->image_count++;
	return 0;
}

/* Gives an object's corners where it is drawn now (an image in its place fitted into them). */
static void
editor_quad(
	const struct pdf_page_editor *editor,
	size_t index,
	double quad[8])
{
	static const double unit[8] = { 0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0 };
	const struct editor_change *change;
	double square[6];
	double placed[8];
	int fitted;

	/* An inserted image: its square where its placement takes it. */
	change = &editor->changes[index];
	if (change->inserted) {
		memcpy(square, change->square, sizeof(square));
		if (change->state == EDITOR_PLACED)
			editor_multiply(change->square, change->placement, square);
		editor_corners(square, unit, quad);
		return;
	}

	/* A line of text: its corners (it is not moved yet, p004). */
	if (index >= editor->scan.count) {
		memcpy(quad, editor->lines[index - editor->scan.count].quad, 8U * sizeof(double));
		return;
	}

	/* An object of the page: its corners, where its placement takes them. */
	memcpy(quad, editor->scan.objects[index].quad, 8U * sizeof(double));
	if (change->state == EDITOR_PLACED) {
		editor_corners(change->placement, quad, placed);
		memcpy(quad, placed, sizeof(placed));
	}

	/* An image in its place, fitted into them. */
	if (change->image == EDITOR_NONE)
		return;
	fitted = editor_fit(&editor->images[change->image], quad, square);
	if (fitted == 0)
		editor_corners(square, unit, quad);
}

/*
 * Fits an image into an object's corners (top left, top right, bottom
 * right, bottom left): the map of the image's unit square onto the shown
 * space that keeps its proportions, centred.  Returns 0, or EINVAL for
 * corners without area.
 */
static int
editor_fit(
	const struct editor_image *image,
	const double quad[8],
	double square[6])
{
	double across[2];
	double down[2];
	double corner[2];
	double width;
	double height;
	double aspect;
	double kept;

	/* The corners' sides from the top left: across to the top right, down to the bottom left. */
	across[0] = quad[2] - quad[0];
	across[1] = quad[3] - quad[1];
	down[0] = quad[6] - quad[0];
	down[1] = quad[7] - quad[1];
	width = sqrt(across[0] * across[0] + across[1] * across[1]);
	height = sqrt(down[0] * down[0] + down[1] * down[1]);
	if (!(width > 1e-6) || !(height > 1e-6))
		return EINVAL;

	/* The image's proportions kept: the side that is too long shortened, the image centred along it. */
	corner[0] = quad[0];
	corner[1] = quad[1];
	aspect = (double)image->width / (double)image->height;
	if (image->orientation >= 5)
		aspect = (double)image->height / (double)image->width;
	if (width / height > aspect) {
		kept = height * aspect / width;
		corner[0] += across[0] * (1.0 - kept) / 2.0;
		corner[1] += across[1] * (1.0 - kept) / 2.0;
		across[0] *= kept;
		across[1] *= kept;
	} else {
		kept = width / aspect / height;
		corner[0] += down[0] * (1.0 - kept) / 2.0;
		corner[1] += down[1] * (1.0 - kept) / 2.0;
		down[0] *= kept;
		down[1] *= kept;
	}

	/* The square's (x, y) goes to the corner plus x across plus (1 - y) down: its top (y = 1) at the corner. */
	square[0] = across[0];
	square[1] = across[1];
	square[2] = -down[0];
	square[3] = -down[1];
	square[4] = corner[0] + down[0];
	square[5] = corner[1] + down[1];
	return 0;
}

/*
 * Appends the drawing of an image named prefix and name, its unit square
 * as shown mapped by square onto the shown space, where the matrix in
 * force is ctm: q M cm /Name Do Q with M = O times square times ctm's
 * inverse, O the map of the image's samples onto it as shown (its EXIF
 * orientation, ws175-p006).  Returns 0 or EINVAL.
 */
static int
editor_draw_image(
	struct pdf_buffer *out,
	const struct editor_image *image,
	const double square[6],
	const double ctm[6],
	const char *prefix,
	size_t name)
{
	/*
	 * The samples' unit square (y up, the first row at the top) as each
	 * orientation shows it: 1 as stored, 2 mirrored across, 3 turned
	 * half, 4 mirrored down, 5 transposed, 6 turned a quarter clockwise,
	 * 7 transversed, 8 turned a quarter anticlockwise.
	 */
	static const double orientations[8][6] = {
		{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 },
		{ -1.0, 0.0, 0.0, 1.0, 1.0, 0.0 },
		{ -1.0, 0.0, 0.0, -1.0, 1.0, 1.0 },
		{ 1.0, 0.0, 0.0, -1.0, 0.0, 1.0 },
		{ 0.0, -1.0, -1.0, 0.0, 1.0, 1.0 },
		{ 0.0, -1.0, 1.0, 0.0, 0.0, 1.0 },
		{ 0.0, 1.0, 1.0, 0.0, 0.0, 0.0 },
		{ 0.0, 1.0, -1.0, 0.0, 1.0, 0.0 }
	};
	double inverse[6];
	double shown[6];
	double matrix[6];
	size_t item;
	int orientation;
	int error;

	/* The samples as shown, then the matrix in the space in force. */
	orientation = image->orientation;
	if (orientation == 0)
		orientation = 1;
	editor_multiply(orientations[orientation - 1], square, shown);
	error = editor_invert(ctm, inverse);
	if (error != 0)
		return EINVAL;
	editor_multiply(shown, inverse, matrix);

	/* The image within a level of its own. */
	pdf_buffer_append(out, " q ", 3);
	for (item = 0; item < 6; item++) {
		editor_number(out, matrix[item]);
		pdf_buffer_append(out, " ", 1);
	}

	/* Its name. */
	pdf_buffer_printf(out, "cm /%s%lu Do Q ", prefix, (unsigned long)name);
	return 0;
}

/*
 * Makes the preview's resources: the page's, by a shallow copy (the values
 * stay the document's objects, design.md [N5]), with the editor's images
 * added to its XObject dictionary under the preview's names.  Returns 0
 * or ENOMEM.
 */
static int
editor_preview_resources(
	struct pdf_page_editor *editor,
	struct pdf_object **resources)
{
	struct pdf_object *page;
	struct pdf_object *own;
	struct pdf_object *own_xobjects;
	struct pdf_object *merged;
	struct pdf_object *xobjects;
	struct pdf_object *image;
	struct pdf_object *key;
	char name[64];
	size_t capacity;
	size_t xcapacity;
	size_t at;
	int same;
	int error;

	/* The page's resources and their XObject dictionary (none for a page without). */
	error = pdf_reader_page(editor->document, editor->index, &page, &own);
	if (error != 0)
		return error;
	if (own != NULL && own->type != PDF_OBJECT_DICTIONARY)
		own = NULL;
	own_xobjects = NULL;
	if (own != NULL) {
		error = pdf_reader_resolve_key(editor->document, own, "XObject", &own_xobjects);
		if (error != 0 || own_xobjects->type != PDF_OBJECT_DICTIONARY)
			own_xobjects = NULL;
	}

	/* The merged dictionaries. */
	capacity = 1;
	if (own != NULL)
		capacity += own->count;
	xcapacity = editor->image_count;
	if (own_xobjects != NULL)
		xcapacity += own_xobjects->count;
	merged = editor_dictionary(&editor->arena, capacity);
	xobjects = editor_dictionary(&editor->arena, xcapacity);
	if (merged == NULL || xobjects == NULL)
		return ENOMEM;

	/* The page's entries but XObject, then the merged XObject dictionary. */
	for (at = 0; own != NULL && at < own->count; at++) {
		same = pdf_object_is_name(own->keys[at], "XObject");
		if (same)
			continue;
		error = editor_put(merged, capacity, own->keys[at], own->values[at]);
		if (error != 0)
			return error;
	}

	/* The merged XObject dictionary under its name. */
	key = editor_object(&editor->arena, PDF_OBJECT_NAME, "XObject", 0);
	if (key == NULL)
		return ENOMEM;
	error = editor_put(merged, capacity, key, xobjects);
	if (error != 0)
		return error;

	/* The page's XObjects, then the images under the preview's names. */
	for (at = 0; own_xobjects != NULL && at < own_xobjects->count; at++) {
		error = editor_put(xobjects, xcapacity, own_xobjects->keys[at], own_xobjects->values[at]);
		if (error != 0)
			return error;
	}

	/* The images under the preview's names. */
	for (at = 0; at < editor->image_count; at++) {
		image = editor_preview_image(editor, &editor->images[at]);
		(void)snprintf(name, sizeof(name), "%s%lu", EDITOR_PREVIEW_PREFIX, (unsigned long)at);
		key = editor_object(&editor->arena, PDF_OBJECT_NAME, name, 0);
		if (image == NULL || key == NULL)
			return ENOMEM;
		error = editor_put(xobjects, xcapacity, key, image);
		if (error != 0)
			return error;
	}

	/* Succeeded: the preview's resources. */
	*resources = merged;
	return 0;
}

/*
 * Gives the stream object the preview draws an image through (made the
 * first time): an image XObject of the image's bytes (DCTDecode for a
 * JPEG, FlateDecode with the PNG predictor for a PNG's rows; 8-bit RGB
 * with a soft mask of the alpha otherwise).  NULL when memory runs out.
 */
static struct pdf_object *
editor_preview_image(
	struct pdf_page_editor *editor,
	struct editor_image *image)
{
	struct pdf_object *stream;
	struct pdf_object *mask;
	struct pdf_object *parameters;
	struct pdf_arena *arena;
	int error;

	/* Made already. */
	if (image->preview != NULL)
		return image->preview;

	/* The image's dictionary and bytes. */
	arena = &editor->arena;
	stream = editor_dictionary(arena, 8);
	if (stream == NULL)
		return NULL;
	stream->type = PDF_OBJECT_STREAM;
	stream->bytes = image->data;
	stream->data_length = image->length;
	error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "Subtype", 0), editor_object(arena, PDF_OBJECT_NAME, "Image", 0));
	if (error == 0)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "Width", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->width));
	if (error == 0)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "Height", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->height));
	if (error == 0)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "BitsPerComponent", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, 8L));
	if (error == 0 && image->components == 1)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "ColorSpace", 0), editor_object(arena, PDF_OBJECT_NAME, "DeviceGray", 0));
	if (error == 0 && image->components != 1)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "ColorSpace", 0), editor_object(arena, PDF_OBJECT_NAME, "DeviceRGB", 0));
	if (error == 0 && image->is_jpeg)
		error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "Filter", 0), editor_object(arena, PDF_OBJECT_NAME, "DCTDecode", 0));
	if (error != 0)
		return NULL;

	/* A PNG's rows: Flate with the PNG predictor (ws175-p006). */
	if (image->rows) {
		parameters = editor_dictionary(arena, 4);
		if (parameters == NULL)
			return NULL;
		error = editor_put(parameters, 4, editor_object(arena, PDF_OBJECT_NAME, "Predictor", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, 15L));
		if (error == 0)
			error = editor_put(parameters, 4, editor_object(arena, PDF_OBJECT_NAME, "Colors", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->components));
		if (error == 0)
			error = editor_put(parameters, 4, editor_object(arena, PDF_OBJECT_NAME, "BitsPerComponent", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, 8L));
		if (error == 0)
			error = editor_put(parameters, 4, editor_object(arena, PDF_OBJECT_NAME, "Columns", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->width));
		if (error == 0)
			error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "Filter", 0), editor_object(arena, PDF_OBJECT_NAME, "FlateDecode", 0));
		if (error == 0)
			error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "DecodeParms", 0), parameters);
		if (error != 0)
			return NULL;
	}

	/* The alpha's soft mask, a gray image of the same size. */
	if (image->alpha != NULL) {
		mask = editor_dictionary(arena, 6);
		if (mask == NULL)
			return NULL;
		mask->type = PDF_OBJECT_STREAM;
		mask->bytes = image->alpha;
		mask->data_length = image->width * image->height;
		error = editor_put(mask, 6, editor_object(arena, PDF_OBJECT_NAME, "Subtype", 0), editor_object(arena, PDF_OBJECT_NAME, "Image", 0));
		if (error == 0)
			error = editor_put(mask, 6, editor_object(arena, PDF_OBJECT_NAME, "Width", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->width));
		if (error == 0)
			error = editor_put(mask, 6, editor_object(arena, PDF_OBJECT_NAME, "Height", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, (long)image->height));
		if (error == 0)
			error = editor_put(mask, 6, editor_object(arena, PDF_OBJECT_NAME, "BitsPerComponent", 0), editor_object(arena, PDF_OBJECT_INTEGER, NULL, 8L));
		if (error == 0)
			error = editor_put(mask, 6, editor_object(arena, PDF_OBJECT_NAME, "ColorSpace", 0), editor_object(arena, PDF_OBJECT_NAME, "DeviceGray", 0));
		if (error == 0)
			error = editor_put(stream, 8, editor_object(arena, PDF_OBJECT_NAME, "SMask", 0), mask);
		if (error != 0)
			return NULL;
	}

	/* Succeeded: made once. */
	image->preview = stream;
	return stream;
}

/*
 * Makes an image of the editor the update's document's (its index there):
 * an image of Notes already written for another page is that one (the same
 * digest; another is EINVAL); the others are copied, their samples and
 * mask compressed when that is shorter (a JPEG and a PNG's rows are
 * already).  Returns 0, EINVAL, ENOSPC, or ENOMEM.
 */
static int
editor_write_image(
	struct pdf_writer *writer,
	const struct editor_image *image,
	size_t *index)
{
	struct pdf_writer_image written;
	unsigned char *packed;
	size_t packed_size;
	size_t count;
	size_t at;
	int same;
	int error;

	/* An image of Notes the update has: the same bytes are that image, others are refused. */
	for (at = 0; image->id != 0UL && at < writer->images_count; at++) {
		if (writer->images[at].id != image->id)
			continue;
		same = memcmp(writer->images[at].digest, image->digest, sizeof(image->digest)) == 0;
		if (!same)
			return EINVAL;
		*index = at;
		return 0;
	}

	/* Its description. */
	memset(&written, 0, sizeof(written));
	written.width = image->width;
	written.height = image->height;
	written.components = image->components;
	written.is_jpeg = image->is_jpeg;
	written.png_rows = image->rows;
	written.flate = image->rows;
	written.id = image->id;
	memcpy(written.digest, image->digest, sizeof(written.digest));

	/* The samples compressed when that is shorter. */
	packed = NULL;
	packed_size = 0;
	if (!image->is_jpeg && !image->rows) {
		error = pdf_writer_pack(image->data, image->length, &packed, &packed_size);
		if (error != 0)
			return error;
	}

	/* The compressed samples, or a copy of the bytes. */
	if (packed != NULL) {
		written.data = packed;
		written.size = packed_size;
		written.flate = 1;
	} else {
		written.data = malloc(image->length);
		if (written.data == NULL)
			return ENOMEM;
		memcpy(written.data, image->data, image->length);
		written.size = image->length;
	}

	/* The mask likewise. */
	if (image->alpha != NULL) {
		count = image->width * image->height;
		error = pdf_writer_pack(image->alpha, count, &packed, &packed_size);
		if (error != 0) {
			free(written.data);
			return error;
		}

		/* The compressed mask, or a copy. */
		if (packed != NULL) {
			written.alpha = packed;
			written.alpha_size = packed_size;
			written.alpha_flate = 1;
		} else {
			written.alpha = malloc(count);
			if (written.alpha == NULL) {
				free(written.data);
				return ENOMEM;
			}

			/* The mask's bytes. */
			memcpy(written.alpha, image->alpha, count);
			written.alpha_size = count;
		}
	}

	/* The document's, which owns the bytes from now. */
	error = pdf_writer_add_image_object(writer, &written, index);
	if (error != 0) {
		free(written.data);
		free(written.alpha);
		return error;
	}

	/* Succeeded: the image by its index. */
	return 0;
}

/*
 * Reads an image stream of Notes back as a source: a JPEG's bytes, a PNG's
 * compressed rows, or the samples and the mask as RGBA.  Returns 0,
 * ENOTSUP for another form, PDF_EFORMAT for a malformed one, ENOMEM, or
 * the failure of its streams.
 */
static int
editor_read_back(
	const struct pdf_page_editor *editor,
	struct pdf_object *stream,
	unsigned long id,
	struct pdf_image_source *image,
	void **owned)
{
	const unsigned char *data;
	struct pdf_crypt *crypt;
	struct pdf_object *filter;
	struct pdf_object *parameters;
	struct pdf_object *predictor;
	struct pdf_object *space;
	unsigned char *decoded;
	unsigned char *buffer;
	double width_number;
	double height_number;
	double bits;
	size_t width;
	size_t height;
	size_t length;
	int components;
	int is_dct;
	int is_flate;
	int is_gray;
	int is_rgb;
	int dct;
	int kind;
	int error;

	/* Its size, of 8-bit samples. */
	error = pdf_object_number(pdf_object_get(stream, "Width"), &width_number);
	if (error == 0)
		error = pdf_object_number(pdf_object_get(stream, "Height"), &height_number);
	if (error == 0)
		error = pdf_object_number(pdf_object_get(stream, "BitsPerComponent"), &bits);
	if (error != 0 || bits != 8.0)
		return ENOTSUP;
	if (!(width_number >= 1.0 && width_number <= (double)EDITOR_IMAGE_SIDE_MAX) || !(height_number >= 1.0 && height_number <= (double)EDITOR_IMAGE_SIDE_MAX))
		return PDF_EFORMAT;
	width = (size_t)width_number;
	height = (size_t)height_number;

	/* Gray or RGB. */
	space = pdf_object_get(stream, "ColorSpace");
	is_gray = pdf_object_is_name(space, "DeviceGray");
	is_rgb = pdf_object_is_name(space, "DeviceRGB");
	if (!is_gray && !is_rgb)
		return ENOTSUP;
	components = 3;
	if (is_gray)
		components = 1;

	/* Its filter: a JPEG's, Flate (a PNG's rows when it has the PNG predictor), or none. */
	filter = pdf_object_get(stream, "Filter");
	is_dct = pdf_object_is_name(filter, "DCTDecode");
	is_flate = pdf_object_is_name(filter, "FlateDecode");
	if (filter != NULL && !is_dct && !is_flate)
		return ENOTSUP;
	parameters = pdf_object_get(stream, "DecodeParms");
	predictor = NULL;
	if (parameters != NULL && parameters->type == PDF_OBJECT_DICTIONARY)
		predictor = pdf_object_get(parameters, "Predictor");
	if (parameters != NULL && (!is_flate || predictor == NULL || predictor->type != PDF_OBJECT_INTEGER || predictor->integer != 15))
		return ENOTSUP;

	/* A JPEG: its bytes, the filter left undone. */
	if (is_dct) {
		error = pdf_filter_decode(editor->document, stream, 1, &data, &length, &decoded, &dct);
		if (error != 0)
			return error;

		/* Its own buffer. */
		buffer = decoded;
		if (buffer == NULL) {
			buffer = malloc(length);
			if (buffer == NULL)
				return ENOMEM;
			memcpy(buffer, data, length);
		}

		/* The JPEG. */
		kind = PDF_IMAGE_SOURCE_JPEG;
	} else if (predictor != NULL) {
		/* A PNG's rows: the stream's bytes as they are in the file (not of an encrypted document). */
		crypt = pdf_reader_crypt(editor->document);
		if (crypt != NULL)
			return ENOTSUP;
		data = pdf_reader_bytes(editor->document) + stream->data_offset;
		if (stream->bytes != NULL)
			data = stream->bytes + stream->data_offset;
		length = stream->data_length;
		buffer = malloc(length + 1U);
		if (buffer == NULL)
			return ENOMEM;
		memcpy(buffer, data, length);
		kind = PDF_IMAGE_SOURCE_IDAT;
	} else {
		/* Samples and a mask: RGBA. */
		error = editor_read_samples(editor, stream, width, height, components, &buffer);
		if (error != 0)
			return error;
		length = width * height * 4U;
		components = 4;
		kind = PDF_IMAGE_SOURCE_RGBA;
	}

	/* Succeeded: the source, its orientation none, its id this one. */
	image->kind = kind;
	image->data = buffer;
	image->bytes = length;
	image->width = width;
	image->height = height;
	image->components = components;
	if (image->size >= sizeof(*image)) {
		image->orientation = 0;
		image->id = id;
	}

	/* The buffer the caller frees. */
	*owned = buffer;
	return 0;
}

/*
 * Decodes an image's samples (Gray or RGB, 8 bits) and its soft mask, if
 * any, into a new buffer of RGBA.  Returns 0, PDF_EFORMAT when a stream's
 * length is not the image's, ENOMEM, or the failure of its streams.
 */
static int
editor_read_samples(
	const struct pdf_page_editor *editor,
	struct pdf_object *stream,
	size_t width,
	size_t height,
	int components,
	unsigned char **pixels)
{
	const unsigned char *samples;
	const unsigned char *alpha;
	struct pdf_object *mask;
	unsigned char *samples_owned;
	unsigned char *alpha_owned;
	unsigned char *rgba;
	size_t samples_size;
	size_t alpha_size;
	size_t count;
	size_t at;
	int dct;
	int error;

	/* The samples, of the image's length. */
	count = width * height;
	error = pdf_filter_decode(editor->document, stream, 0, &samples, &samples_size, &samples_owned, &dct);
	if (error != 0)
		return error;
	if (samples_size != count * (size_t)components) {
		free(samples_owned);
		return PDF_EFORMAT;
	}

	/* The mask, if any, of as many. */
	alpha = NULL;
	alpha_owned = NULL;
	mask = pdf_object_get(stream, "SMask");
	if (mask != NULL) {
		error = pdf_reader_resolve_key(editor->document, stream, "SMask", &mask);
		if (error == 0 && mask->type != PDF_OBJECT_STREAM)
			error = PDF_EFORMAT;
		if (error == 0)
			error = pdf_filter_decode(editor->document, mask, 0, &alpha, &alpha_size, &alpha_owned, &dct);
		if (error == 0 && alpha_size != count)
			error = PDF_EFORMAT;
		if (error != 0) {
			free(alpha_owned);
			free(samples_owned);
			return error;
		}
	}

	/* RGBA: the colour (a gray sample three times) and the alpha (opaque without a mask). */
	rgba = malloc(count * 4U);
	if (rgba == NULL) {
		free(alpha_owned);
		free(samples_owned);
		return ENOMEM;
	}

	/* Each pixel. */
	for (at = 0; at < count; at++) {
		if (components == 1) {
			rgba[at * 4U] = samples[at];
			rgba[at * 4U + 1U] = samples[at];
			rgba[at * 4U + 2U] = samples[at];
		} else {
			rgba[at * 4U] = samples[at * 3U];
			rgba[at * 4U + 1U] = samples[at * 3U + 1U];
			rgba[at * 4U + 2U] = samples[at * 3U + 2U];
		}

		/* The alpha. */
		rgba[at * 4U + 3U] = 255U;
		if (alpha != NULL)
			rgba[at * 4U + 3U] = alpha[at];
	}

	/* Succeeded: the decoded buffers go, the pixels stay. */
	free(alpha_owned);
	free(samples_owned);
	*pixels = rgba;
	return 0;
}

/* Makes a name (text) or an integer object in an arena; NULL when memory runs out. */
static struct pdf_object *
editor_object(
	struct pdf_arena *arena,
	int type,
	const char *text,
	long integer)
{
	struct pdf_object *object;
	unsigned char *bytes;
	size_t length;

	/* The object. */
	object = pdf_arena_allocate(arena, sizeof(*object));
	if (object == NULL)
		return NULL;
	memset(object, 0, sizeof(*object));
	object->type = (enum pdf_object_type)type;
	object->integer = integer;

	/* A name's bytes, with the NUL the reader's names have. */
	if (text != NULL) {
		length = strlen(text);
		bytes = pdf_arena_allocate(arena, length + 1U);
		if (bytes == NULL)
			return NULL;
		memcpy(bytes, text, length + 1U);
		object->bytes = bytes;
		object->length = length;
	}

	/* Succeeded: the object. */
	return object;
}

/* Makes an empty dictionary with room for some entries in an arena; NULL when memory runs out. */
static struct pdf_object *
editor_dictionary(
	struct pdf_arena *arena,
	size_t capacity)
{
	struct pdf_object *dictionary;

	/* The dictionary and its two arrays. */
	dictionary = editor_object(arena, PDF_OBJECT_DICTIONARY, NULL, 0);
	if (dictionary == NULL)
		return NULL;
	if (capacity == 0)
		capacity = 1;
	dictionary->keys = pdf_arena_allocate(arena, capacity * sizeof(*dictionary->keys));
	dictionary->values = pdf_arena_allocate(arena, capacity * sizeof(*dictionary->values));
	if (dictionary->keys == NULL || dictionary->values == NULL)
		return NULL;

	/* Succeeded: an empty dictionary. */
	return dictionary;
}

/* Adds an entry to a dictionary made with room for capacity entries.  Returns 0, ENOMEM for a missing object, or ENOSPC. */
static int
editor_put(
	struct pdf_object *dictionary,
	size_t capacity,
	struct pdf_object *key,
	struct pdf_object *value)
{
	/* Both objects, and room. */
	if (key == NULL || value == NULL)
		return ENOMEM;
	if (dictionary->count >= capacity)
		return ENOSPC;

	/* Succeeded: the entry. */
	dictionary->keys[dictionary->count] = key;
	dictionary->values[dictionary->count] = value;
	dictionary->count++;
	return 0;
}

/* Multiplies two matrices (a point goes through left, then right). */
static void
editor_multiply(
	const double left[6],
	const double right[6],
	double product[6])
{
	double result[6];

	/* The product of the 3 by 3 matrices whose third column is 0, 0, 1. */
	result[0] = left[0] * right[0] + left[1] * right[2];
	result[1] = left[0] * right[1] + left[1] * right[3];
	result[2] = left[2] * right[0] + left[3] * right[2];
	result[3] = left[2] * right[1] + left[3] * right[3];
	result[4] = left[4] * right[0] + left[5] * right[2] + right[4];
	result[5] = left[4] * right[1] + left[5] * right[3] + right[5];
	memcpy(product, result, sizeof(result));
}

/* Inverts a matrix.  Returns 0, or EINVAL for one without area. */
static int
editor_invert(
	const double matrix[6],
	double inverse[6])
{
	double determinant;
	double size;

	/* A matrix that keeps an area. */
	determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
	size = fabs(determinant);
	if (!(size > 1e-12))
		return EINVAL;

	/* The inverse. */
	inverse[0] = matrix[3] / determinant;
	inverse[1] = -matrix[1] / determinant;
	inverse[2] = -matrix[2] / determinant;
	inverse[3] = matrix[0] / determinant;
	inverse[4] = (matrix[2] * matrix[5] - matrix[3] * matrix[4]) / determinant;
	inverse[5] = (matrix[1] * matrix[4] - matrix[0] * matrix[5]) / determinant;
	return 0;
}

/* Appends a number with nine decimals (design.md [L4]: four would show in a small scale of a large page). */
static void
editor_number(
	struct pdf_buffer *buffer,
	double number)
{
	char text[32];
	double size;
	int length;

	/* Zero for a value too small to matter (no exponent in PDF). */
	size = fabs(number);
	if (size < 1e-9)
		number = 0.0;

	/* A value too large for PDF's reals is refused. */
	if (size >= 1e9) {
		pdf_buffer_fail(buffer, EINVAL);
		return;
	}

	/* The digits, without an exponent. */
	length = snprintf(text, sizeof(text), "%.9f", number);
	if (length < 0 || (size_t)length >= sizeof(text)) {
		pdf_buffer_fail(buffer, EINVAL);
		return;
	}

	/* Trailing zeros and a bare point go. */
	while (length > 1 && text[length - 1] == '0')
		length--;
	if (length > 1 && text[length - 1] == '.')
		length--;
	pdf_buffer_append(buffer, text, (size_t)length);
}

/* Maps four corners (x, y pairs) through a matrix of the shown space. */
static void
editor_corners(
	const double placement[6],
	const double quad[8],
	double placed[8])
{
	size_t corner;
	double x;
	double y;

	/* Each corner. */
	for (corner = 0; corner < 4; corner++) {
		x = quad[2 * corner];
		y = quad[2 * corner + 1];
		placed[2 * corner] = placement[0] * x + placement[2] * y + placement[4];
		placed[2 * corner + 1] = placement[1] * x + placement[3] * y + placement[5];
	}
}

/*
 * Tells whether a point lies in a quadrilateral (four corners in order,
 * convex, as an affine map of a rectangle makes them): on the same side of
 * every edge.  A quadrilateral without area holds no point.
 */
static int
editor_inside(
	const double quad[8],
	double x,
	double y)
{
	double cross;
	double area;
	double ex;
	double ey;
	int positive;
	int negative;
	size_t corner;
	size_t next;

	/* Twice the area (the shoelace sum); a quadrilateral without area holds nothing. */
	area = 0.0;
	for (corner = 0; corner < 4; corner++) {
		next = (corner + 1) % 4;
		area += quad[2 * corner] * quad[2 * next + 1] - quad[2 * next] * quad[2 * corner + 1];
	}

	/* No area: nothing inside. */
	if (area < 1e-9 && area > -1e-9)
		return 0;

	/* The side of the point from each edge. */
	positive = 0;
	negative = 0;
	for (corner = 0; corner < 4; corner++) {
		/* The edge from this corner to the next, and the point from this corner. */
		next = (corner + 1) % 4;
		ex = quad[2 * next] - quad[2 * corner];
		ey = quad[2 * next + 1] - quad[2 * corner + 1];
		cross = ex * (y - quad[2 * corner + 1]) - ey * (x - quad[2 * corner]);
		if (cross > 0.0)
			positive = 1;
		if (cross < 0.0)
			negative = 1;
	}

	/* Points on both sides: outside. */
	if (positive && negative)
		return 0;

	/* Inside (or on an edge). */
	return 1;
}
