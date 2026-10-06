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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"
#include "writer.h"

/* What has been done to an object (ws175-p003a): nothing, deleted, or placed by a matrix of the shown space. */
#define EDITOR_KEPT	0U
#define EDITOR_DELETED	1U
#define EDITOR_PLACED	2U

/* No object hidden from the preview. */
#define EDITOR_NONE	((size_t)-1)

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
	unsigned *states;
	double *placements;
};

static int editor_inside(const double quad[8], double x, double y);
static int editor_writable(const struct pdf_page_editor *editor, size_t index);
static void editor_multiply(const double left[6], const double right[6], double product[6]);
static int editor_invert(const double matrix[6], double inverse[6]);
static void editor_number(struct pdf_buffer *buffer, double number);
static void editor_corners(const double placement[6], const double quad[8], double placed[8]);

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

	/* Every object as it is, with room for a placement each (p003a). */
	if (opened->scan.count > 0) {
		opened->states = calloc(opened->scan.count, sizeof(*opened->states));
		opened->placements = calloc(opened->scan.count * 6U, sizeof(*opened->placements));
		if (opened->states == NULL || opened->placements == NULL) {
			pdf_page_editor_close(opened);
			return ENOMEM;
		}
	}

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

	/* The scan, the content, the objects' states, the record. */
	pdf_scan_free(&editor->scan);
	free(editor->content);
	free(editor->states);
	free(editor->placements);
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

	/* A deleted object says so; a placed one is where its placement puts it. */
	if (editor->states[index] == EDITOR_DELETED)
		object->flags |= PDF_EDIT_OBJECT_DELETED;
	if (editor->states[index] == EDITOR_PLACED)
		editor_corners(&editor->placements[6U * index], found->quad, object->quad);
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
 * Puts an object back as the page has it.  Returns 0, EINVAL, ENOENT, or
 * EPERM for a page that cannot be edited.
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

	/* As it was. */
	editor->states[index] = EDITOR_KEPT;
	return 0;
}

/*
 * Deletes an object: the new content leaves its bytes out.  Returns 0,
 * EINVAL, ENOENT, or EPERM for a page that cannot be edited.
 */
int
pdf_page_editor_delete(
	struct pdf_page_editor *editor,
	size_t index)
{
	int error;

	/* An object of a page that can be edited. */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;

	/* Gone from the new content. */
	editor->states[index] = EDITOR_DELETED;
	return 0;
}

/*
 * Moves and sizes an object: transform is the affine map of the shown
 * space (a point p goes to p times it, design.md [M1]) from where the page
 * draws the object to where it goes.  A placement that cannot be undone
 * (no area) is refused.  Returns 0, EINVAL, ENOENT, or EPERM.
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

	/* An object of a page that can be edited, and a placement. */
	error = editor_writable(editor, index);
	if (error != 0)
		return error;
	if (transform == NULL)
		return EINVAL;

	/* Numbers (not NaN, within what a PDF real holds), and a map that keeps an area (the object's matrix too). */
	for (item = 0; item < 6; item++) {
		size = fabs(transform[item]);
		if (!(transform[item] == transform[item]) || size >= 1e9)
			return EINVAL;
	}

	/* Both matrices keep an area. */
	error = editor_invert(transform, inverse);
	if (error != 0)
		return EINVAL;
	error = editor_invert(editor->scan.objects[index].ctm, inverse);
	if (error != 0)
		return EINVAL;

	/* Placed. */
	memcpy(&editor->placements[6U * index], transform, 6U * sizeof(double));
	editor->states[index] = EDITOR_PLACED;
	return 0;
}

/*
 * Draws the page with the editor's changes (the preview): the new content
 * on the page's resources, an object hidden from it when hidden is its
 * index ((size_t)-1 for none, as while it is dragged).  Returns 0, EINVAL,
 * ENOMEM, or the failure of the page.
 */
int
pdf_page_editor_render(
	struct pdf_page_editor *editor,
	size_t hidden,
	struct pdf_display_list **list)
{
	struct pdf_buffer content;
	int error;

	/* Refuses a missing editor or list. */
	if (editor == NULL || list == NULL)
		return EINVAL;

	/* A page that cannot be edited is drawn as it is. */
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return pdf_page_render(editor->document, editor->index, list);

	/* The new content, then the page drawn with it. */
	memset(&content, 0, sizeof(content));
	error = pdf_editor_content(editor, hidden, &content);
	if (error == 0)
		error = pdf_content_render(editor->document, editor->index, content.data, content.length, list);
	free(content.data);
	return error;
}

/*
 * Builds a page's new content (design.md section 3.4): the page's own
 * within q and Q, each changed object's bytes replaced (a deleted one, or
 * the one hidden, left out; a placed one inside q M cm ... Q, M the
 * placement in the object's own space), the Q without a q left out, then
 * the text object and the q the content left open closed.  Returns 0 or
 * ENOMEM.
 */
int
pdf_editor_content(
	const struct pdf_page_editor *editor,
	size_t hidden,
	struct pdf_buffer *out)
{
	const struct pdf_scan_object *object;
	double inverse[6];
	double through[6];
	double matrix[6];
	size_t position;
	size_t object_at;
	size_t stray_at;
	size_t offset;
	size_t item;
	unsigned state;
	int error;

	/* The page's content, from its start, within a level of its own. */
	pdf_buffer_append(out, "q\n", 2);
	position = 0;
	object_at = 0;
	stray_at = 0;

	/* The changed objects and the stray Q, in the order of the content. */
	for (;;) {
		/* The next object that changed, and the next stray Q. */
		while (object_at < editor->scan.count && editor->states[object_at] == EDITOR_KEPT && object_at != hidden)
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
		pdf_buffer_append(out, editor->content + position, object->offset - position);
		position = object->offset + object->length;
		state = editor->states[object_at];
		if (object_at == hidden)
			state = EDITOR_DELETED;
		object_at++;

		/* A deleted object leaves only a space. */
		if (state == EDITOR_DELETED) {
			pdf_buffer_append(out, " ", 1);
			continue;
		}

		/* A placed one: M = C_rec times S times C_rec's inverse, so that M times C_rec is C_rec times S. */
		error = editor_invert(object->ctm, inverse);
		if (error != 0)
			return EINVAL;
		editor_multiply(object->ctm, &editor->placements[6U * (object_at - 1U)], through);
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
	}

	/* The rest of the content, a text object left open closed, and every level it left open. */
	pdf_buffer_append(out, editor->content + position, editor->size - position);
	pdf_buffer_append(out, "\n", 1);
	if (editor->scan.in_text)
		pdf_buffer_append(out, "ET\n", 3);
	for (item = 0; item < editor->scan.open_saves; item++)
		pdf_buffer_append(out, "Q\n", 2);
	pdf_buffer_append(out, "Q\n", 2);

	/* Reports a buffer that could not grow. */
	if (out->error != 0)
		return out->error;

	/* Succeeded: the content is built. */
	return 0;
}

/*
 * Lists the editor's page in an update with its changes (the editor's new
 * content), and opens it for drawing over it, as
 * pdf_writer_begin_page_over draws (the page as shown).  The editor must be
 * of the update's document, its page the next to list and editable.
 * Returns 0, EINVAL, EPERM, ENOMEM, or the failure of the page.
 */
int
pdf_writer_begin_page_edited(
	struct pdf_writer *writer,
	const struct pdf_page_editor *editor)
{
	struct pdf_buffer edited;
	int error;

	/* An editor of this update's document, of a page that can be edited. */
	if (writer == NULL || editor == NULL || editor->document != writer->base)
		return EINVAL;
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return EPERM;

	/* The page's new content, which the update's page takes. */
	memset(&edited, 0, sizeof(edited));
	error = pdf_editor_content(editor, EDITOR_NONE, &edited);
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
	if (index >= editor->scan.count)
		return ENOENT;

	/* A page whose content was read whole. */
	if ((editor->status & PDF_EDIT_PAGE_READ_ONLY) != 0U)
		return EPERM;

	/* It may. */
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

/* Maps an object's corners through a placement of the shown space. */
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
