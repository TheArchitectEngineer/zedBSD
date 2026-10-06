/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The editor of a page's objects (ws175-p002, plan/ws175/phase001/
 * design.md section 3): the page's content scanned once by the
 * interpreter (content.c, pdf_content_scan), and the images and graphics
 * of its top level offered by index, by key and by the point they cover.
 *
 * An editor keeps the page's decoded content, which the objects' bytes are
 * ranges of, and the scan's records (where q and Q are unbalanced), which
 * the content's new version is built from (ws175-p003).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

/*
 * One page's editor: the document and the page, the decoded content, the
 * scan of it, and the page's PDF_EDIT_PAGE_* state.
 */
struct pdf_page_editor {
	struct pdf_document *document;
	size_t index;
	unsigned char *content;
	size_t size;
	struct pdf_scan scan;
	unsigned status;
};

static int editor_inside(const double quad[8], double x, double y);

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
	/* Nothing to close. */
	if (editor == NULL)
		return;

	/* The scan, the content, the record. */
	pdf_scan_free(&editor->scan);
	free(editor->content);
	free(editor);
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
 * Reports how many objects the page has.
 */
size_t
pdf_page_editor_count(
	const struct pdf_page_editor *editor)
{
	/* No editor, no objects. */
	if (editor == NULL)
		return 0;

	/* The scan's. */
	return editor->scan.count;
}

/*
 * Copies one object as the editor shows it.  object->size must be the
 * caller's sizeof (at least the fields up to image_height).  Returns 0,
 * EINVAL, or ENOENT for an index past the last object.
 */
int
pdf_page_editor_object(
	const struct pdf_page_editor *editor,
	size_t index,
	struct pdf_edit_object *object)
{
	const struct pdf_scan_object *found;
	size_t size;

	/* Refuses a missing editor or object, or one of an unknown size. */
	if (editor == NULL || object == NULL)
		return EINVAL;
	size = object->size;
	if (size < sizeof(*object))
		return EINVAL;

	/* The object by its index. */
	if (index >= editor->scan.count)
		return ENOENT;
	found = &editor->scan.objects[index];

	/* Its kind, flags and corners; an image's samples; no text. */
	memset(object, 0, sizeof(*object));
	object->size = size;
	object->kind = PDF_EDIT_GRAPHIC;
	if (found->kind == (unsigned)PDF_EDIT_IMAGE)
		object->kind = PDF_EDIT_IMAGE;
	if (found->clipped)
		object->flags |= PDF_EDIT_OBJECT_CLIPPED;
	memcpy(object->quad, found->quad, sizeof(object->quad));
	object->text = "";
	object->image_width = found->width;
	object->image_height = found->height;

	/* Succeeded: the object is copied. */
	return 0;
}

/*
 * Gives an object's key.  Returns 0, EINVAL, ENOENT for an index past the
 * last object, or ERANGE for bytes past what a key holds.
 */
int
pdf_page_editor_key(
	const struct pdf_page_editor *editor,
	size_t index,
	struct pdf_edit_key *key)
{
	const struct pdf_scan_object *found;

	/* Refuses a missing editor or key. */
	if (editor == NULL || key == NULL)
		return EINVAL;

	/* The object by its index. */
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
 * Finds the object a key names: its kind, place, length and fingerprint
 * all the same.  Returns 0, EINVAL, or ENOENT when the page has none.
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

	/* Compares the key with each object's. */
	for (at = 0; at < editor->scan.count; at++) {
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
 * in the content's order whose corners hold it).  Returns 0, EINVAL, or
 * ENOENT when no object is there.
 */
int
pdf_page_editor_hit(
	const struct pdf_page_editor *editor,
	double x,
	double y,
	size_t *index)
{
	size_t at;
	int inside;

	/* Refuses a missing editor or result. */
	if (editor == NULL || index == NULL)
		return EINVAL;

	/* From the last object drawn to the first. */
	for (at = editor->scan.count; at > 0; at--) {
		/* An object whose corners hold the point. */
		inside = editor_inside(editor->scan.objects[at - 1].quad, x, y);
		if (!inside)
			continue;

		/* Succeeded: the object on top there. */
		*index = at - 1;
		return 0;
	}

	/* Nothing there. */
	return ENOENT;
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
