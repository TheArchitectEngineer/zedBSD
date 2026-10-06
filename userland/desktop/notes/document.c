/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The notebook model of Notes: pages of strokes, and the history that
 * takes changes back (plan/ws079/design-input-notes.md section 5.1).
 *
 * Four primitive changes alter a document: a stroke inserted at a place on
 * a page, a stroke removed by its number, a page inserted, a page removed.
 * Each is logged to the journal before it is applied, so that replaying the
 * journal rebuilds the document.  The user's actions -- a stroke drawn, the
 * strokes an eraser touched, a page added, undo and redo -- are made of
 * these primitives, and only they enter the undo history.
 */

#include "notes.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The fewest samples, strokes and pages an array grows to. */
#define DOCUMENT_GROW_MIN	16U

static int grow(void **array, size_t *capacity, size_t needed, size_t element);
static void undo_release(struct notes_undo *entry);
static int undo_push(struct notes_document *document, unsigned kind, size_t page);
static int undo_hold(struct notes_undo *entry);
static int undo_revert(struct notes_document *document, struct notes_undo *entry);
static int undo_apply(struct notes_document *document, struct notes_undo *entry);
static int undo_hold_more(struct notes_undo *entry, size_t more);
static int erase_entry(struct notes_document *document, size_t page, struct notes_undo **entry);
static int stroke_touches(struct notes_stroke *stroke, float x, float y, float radius);
static int stroke_split(struct notes_document *document, const struct notes_stroke *stroke, float x, float y, float reach, struct notes_stroke ***pieces, size_t *piece_count, int *crossed);
static int segment_inside(const struct notes_point *a, const struct notes_point *b, float x, float y, float reach, float *enter, float *leave);
static void point_between(const struct notes_point *a, const struct notes_point *b, float share, struct notes_point *point);
static int piece_add(struct notes_document *document, const struct notes_stroke *stroke, struct notes_stroke **piece, const struct notes_point *point);
static int piece_close(struct notes_stroke **piece, struct notes_stroke ***pieces, size_t *piece_count, size_t *piece_capacity);
static void pieces_free(struct notes_stroke **pieces, size_t first, size_t count);
static float segment_distance(float px, float py, float ax, float ay, float bx, float by);
static int edit_change(struct notes_document *document, size_t page, const struct notes_edit *which, const struct notes_edit *state);
static int edit_swap(struct notes_document *document, const struct notes_undo *entry, const struct notes_edit *from, const struct notes_edit *to);

/*
 * Makes an empty document with one blank page.
 *
 * Returns 0, or ENOMEM.
 */
int
notes_document_init(
	struct notes_document *document,
	uint64_t time_base)
{
	struct notes_page *page;
	int error;

	/* Nothing is owned yet; the first stroke is number 1. */
	memset(document, 0, sizeof(*document));
	document->next_id = 1U;
	document->time_base = time_base;

	/* The first page, A4 and plain. */
	page = notes_page_create(NOTES_PAGE_WIDTH, NOTES_PAGE_HEIGHT, NOTES_BACKGROUND_PLAIN);
	if (page == NULL)
		return ENOMEM;

	/* The page goes in without a journal record: an empty document is where every journal starts. */
	error = grow((void **)&document->pages, &document->page_capacity, 1U, sizeof(document->pages[0]));
	if (error != 0) {
		notes_page_free(page);
		return error;
	}

	/* Succeeded: one blank page, unchanged since it was made. */
	document->pages[0] = page;
	document->page_count = 1U;
	return 0;
}

/*
 * Frees the pages, their strokes, the undo history and the PDF the
 * notebook writes on.
 *
 * The journal is not the document's to free.
 */
void
notes_document_free(
	struct notes_document *document)
{
	size_t index;

	/* The history, with whatever its entries hold. */
	for (index = 0; index < document->undo_count; index++)
		undo_release(&document->undo[index]);
	free(document->undo);

	/* The pages, with their strokes. */
	for (index = 0; index < document->page_count; index++)
		notes_page_free(document->pages[index]);
	free(document->pages);

	/* The PDF the notebook writes on, if any. */
	pdf_document_close(document->base);

	/* Nothing is owned any more. */
	memset(document, 0, sizeof(*document));
}

/*
 * Makes an empty stroke of a tool, colour (0xRRGGBBAA) and width.
 *
 * Returns NULL when memory runs out.
 */
struct notes_stroke *
notes_stroke_create(
	uint32_t id,
	unsigned tool,
	uint32_t color,
	float width,
	uint64_t start_ms)
{
	struct notes_stroke *stroke;

	/* Allocates the stroke. */
	stroke = calloc(1U, sizeof(*stroke));
	if (stroke == NULL)
		return NULL;

	/* Succeeded: a stroke with no samples, its width on the edit data's grid. */
	stroke->id = id;
	stroke->tool = tool;
	stroke->color = color;
	stroke->width = notes_quantize(width);
	stroke->start_ms = start_ms;
	return stroke;
}

/*
 * Frees a stroke, its samples and its outline.
 */
void
notes_stroke_free(
	struct notes_stroke *stroke)
{
	/* A missing stroke frees nothing. */
	if (stroke == NULL)
		return;

	/* The outline, the samples and the stroke. */
	pdf_outline_free(stroke->outline);
	free(stroke->points);
	free(stroke);
}

/*
 * Adds a sample to a stroke, on the edit data's 1/64 point grid.
 *
 * The outline is dropped and made again when next asked for.  Returns 0,
 * or ENOMEM.
 */
int
notes_stroke_append(
	struct notes_stroke *stroke,
	const struct notes_point *point)
{
	struct notes_point *kept;
	int error;

	/* Room for one more sample. */
	error = grow((void **)&stroke->points, &stroke->point_capacity, stroke->point_count + 1U, sizeof(stroke->points[0]));
	if (error != 0)
		return error;

	/* The sample, its position rounded to the grid the edit data keeps. */
	kept = &stroke->points[stroke->point_count];
	*kept = *point;
	kept->x = notes_quantize(point->x);
	kept->y = notes_quantize(point->y);
	stroke->point_count++;

	/* The outline no longer matches the samples. */
	pdf_outline_free(stroke->outline);
	stroke->outline = NULL;
	stroke->outline_count = 0;

	/* Succeeded: the sample is the stroke's last. */
	return 0;
}

/*
 * Makes the stroke's outline polygon and its bounding box, when they are
 * not made yet.
 *
 * The polygon comes from libpdf's pdf_outline_stroke(), the one source of a
 * stroke's shape on the screen and in the PDF.  A highlighter ignores the
 * pressure and draws its full width.  Returns 0, or the errno value
 * pdf_outline_stroke() reports.
 */
int
notes_stroke_outline(
	struct notes_stroke *stroke)
{
	struct pdf_stroke_point *samples;
	struct pdf_point *outline;
	size_t outline_count;
	size_t index;
	int error;

	/* An outline that is made already stands. */
	if (stroke->outline != NULL)
		return 0;

	/* A stroke without samples has no shape. */
	if (stroke->point_count == 0)
		return EINVAL;

	/* Allocates the samples in libpdf's form. */
	samples = malloc(stroke->point_count * sizeof(*samples));
	if (samples == NULL)
		return ENOMEM;

	/* Converts each sample: its position and its pressure from 0 to 1. */
	for (index = 0; index < stroke->point_count; index++) {
		samples[index].x = stroke->points[index].x;
		samples[index].y = stroke->points[index].y;
		samples[index].pressure = (double)stroke->points[index].pressure / (double)NOTES_PRESSURE_MAX;

		/* A highlighter's width does not follow the pressure. */
		if (stroke->tool == NOTES_TOOL_HIGHLIGHTER)
			samples[index].pressure = 1.0;
	}

	/* Builds the polygon. */
	error = pdf_outline_stroke(samples, stroke->point_count, stroke->width, &outline, &outline_count);
	free(samples);
	if (error != 0)
		return error;

	/* Measures the box the polygon lies in. */
	stroke->bounds[0] = (float)outline[0].x;
	stroke->bounds[1] = (float)outline[0].y;
	stroke->bounds[2] = (float)outline[0].x;
	stroke->bounds[3] = (float)outline[0].y;
	for (index = 1; index < outline_count; index++) {
		/* Widens the box to the corner horizontally. */
		if ((float)outline[index].x < stroke->bounds[0])
			stroke->bounds[0] = (float)outline[index].x;
		if ((float)outline[index].x > stroke->bounds[2])
			stroke->bounds[2] = (float)outline[index].x;

		/* And vertically. */
		if ((float)outline[index].y < stroke->bounds[1])
			stroke->bounds[1] = (float)outline[index].y;
		if ((float)outline[index].y > stroke->bounds[3])
			stroke->bounds[3] = (float)outline[index].y;
	}

	/* Succeeded: the stroke keeps its outline until its samples change. */
	stroke->outline = outline;
	stroke->outline_count = outline_count;
	return 0;
}

/*
 * Rounds a length in points to the 1/64 point grid of the edit data.
 */
float
notes_quantize(
	float value)
{
	double units;

	/* The nearest whole number of 1/64 points. */
	units = floor((double)value * (double)NOTES_UNITS_PER_POINT + 0.5);

	/* Reports it in points again. */
	return (float)(units / (double)NOTES_UNITS_PER_POINT);
}

/*
 * Makes an empty page of a size and background.
 *
 * Returns NULL when memory runs out.
 */
struct notes_page *
notes_page_create(
	float width,
	float height,
	unsigned background)
{
	struct notes_page *page;

	/* Allocates the page. */
	page = calloc(1U, sizeof(*page));
	if (page == NULL)
		return NULL;

	/* Succeeded: a page without strokes, its size on the edit data's grid. */
	page->width = notes_quantize(width);
	page->height = notes_quantize(height);
	page->background = background;
	return page;
}

/*
 * Frees a page and its strokes.
 */
void
notes_page_free(
	struct notes_page *page)
{
	size_t index;

	/* A missing page frees nothing. */
	if (page == NULL)
		return;

	/* The strokes, the edits and the editor, then the page. */
	for (index = 0; index < page->stroke_count; index++)
		notes_stroke_free(page->strokes[index]);
	free(page->strokes);
	for (index = 0; index < page->edit_count; index++)
		notes_edit_free(page->edits[index]);
	free(page->edits);
	notes_page_close_editor(page);
	free(page);
}

/*
 * Inserts a page before the page at an index (the page count appends).
 *
 * The primitive change: logged, applied, and not entered in the history.
 * The document owns the page afterwards.  Returns 0, EINVAL or ENOMEM.
 */
int
notes_document_insert_page(
	struct notes_document *document,
	size_t index,
	struct notes_page *page)
{
	int error;

	/* An index past the end names no place. */
	if (index > document->page_count)
		return EINVAL;

	/* Room for one more page. */
	error = grow((void **)&document->pages, &document->page_capacity, document->page_count + 1U, sizeof(document->pages[0]));
	if (error != 0)
		return error;

	/* Logs the change before it is made. */
	if (document->journal != NULL) {
		error = notes_journal_add_page(document->journal, document, index);
		if (error != 0)
			return error;
	}

	/* Moves the later pages up and puts the page in. */
	memmove(&document->pages[index + 1U], &document->pages[index], (document->page_count - index) * sizeof(document->pages[0]));
	document->pages[index] = page;
	document->page_count++;

	/* Succeeded: the document changed since its last save, and the pages moved. */
	document->dirty = 1;
	document->reshaped++;
	return 0;
}

/*
 * Takes the page at an index out of the document.
 *
 * The primitive change: logged, applied, and not entered in the history.
 * Returns the page, which the caller owns, or NULL for a bad index, the
 * last page, or a journal that could not log it.
 */
struct notes_page *
notes_document_remove_page(
	struct notes_document *document,
	size_t index)
{
	struct notes_page *page;
	int error;

	/* A document keeps at least one page. */
	if (index >= document->page_count || document->page_count == 1U)
		return NULL;

	/* Logs the change before it is made. */
	if (document->journal != NULL) {
		error = notes_journal_remove_page(document->journal, document, index);
		if (error != 0)
			return NULL;
	}

	/* Moves the later pages down over it. */
	page = document->pages[index];
	memmove(&document->pages[index], &document->pages[index + 1U], (document->page_count - index - 1U) * sizeof(document->pages[0]));
	document->page_count--;

	/* Succeeded: the document changed since its last save, and the pages moved. */
	document->dirty = 1;
	document->reshaped++;
	return page;
}

/*
 * Inserts a stroke on a page before the stroke at a place (the count appends).
 *
 * The primitive change: logged, applied, and not entered in the history.
 * The page owns the stroke afterwards.  Returns 0, EINVAL or ENOMEM.
 */
int
notes_document_insert_stroke(
	struct notes_document *document,
	size_t page,
	size_t place,
	struct notes_stroke *stroke)
{
	struct notes_page *target;
	int error;

	/* A page that is not there has no place. */
	if (page >= document->page_count)
		return EINVAL;
	target = document->pages[page];
	if (place > target->stroke_count)
		return EINVAL;

	/* Room for one more stroke. */
	error = grow((void **)&target->strokes, &target->stroke_capacity, target->stroke_count + 1U, sizeof(target->strokes[0]));
	if (error != 0)
		return error;

	/* Logs the change before it is made. */
	if (document->journal != NULL) {
		error = notes_journal_add_stroke(document->journal, document, page, place, stroke);
		if (error != 0)
			return error;
	}

	/* A stroke that goes below others changes what the page's picture holds. */
	if (place != target->stroke_count)
		document->reshaped++;

	/* Moves the later strokes up and puts the stroke in. */
	memmove(&target->strokes[place + 1U], &target->strokes[place], (target->stroke_count - place) * sizeof(target->strokes[0]));
	target->strokes[place] = stroke;
	target->stroke_count++;

	/* A replayed stroke may carry a number the document has not handed out yet. */
	if (stroke->id >= document->next_id)
		document->next_id = stroke->id + 1U;

	/* Succeeded: the document changed since its last save. */
	document->dirty = 1;
	return 0;
}

/*
 * Takes the stroke with a number off a page, and tells where it was.
 *
 * The primitive change: logged, applied, and not entered in the history.
 * Returns the stroke, which the caller owns, or NULL when the page has no
 * such stroke or the journal could not log the change.
 */
struct notes_stroke *
notes_document_remove_stroke(
	struct notes_document *document,
	size_t page,
	uint32_t id,
	size_t *place)
{
	struct notes_page *target;
	struct notes_stroke *stroke;
	size_t index;
	int error;

	/* A page that is not there has no strokes. */
	if (page >= document->page_count)
		return NULL;
	target = document->pages[page];

	/* Finds the stroke by its number. */
	for (index = 0; index < target->stroke_count; index++) {
		if (target->strokes[index]->id == id)
			break;
	}

	/* A number that is not on the page removes nothing. */
	if (index == target->stroke_count)
		return NULL;

	/* Logs the change before it is made. */
	if (document->journal != NULL) {
		error = notes_journal_remove_stroke(document->journal, document, page, id);
		if (error != 0)
			return NULL;
	}

	/* Moves the later strokes down over it. */
	stroke = target->strokes[index];
	memmove(&target->strokes[index], &target->strokes[index + 1U], (target->stroke_count - index - 1U) * sizeof(target->strokes[0]));
	target->stroke_count--;

	/* Succeeded: the document changed since its last save, and a stroke left a page's picture. */
	document->dirty = 1;
	document->reshaped++;
	*place = index;
	return stroke;
}

/*
 * Adds a finished stroke on top of a page, as a change the user can undo.
 *
 * The page owns the stroke afterwards; on failure the caller still does.
 * Returns 0, EINVAL or ENOMEM.
 */
int
notes_document_add_stroke(
	struct notes_document *document,
	size_t page,
	struct notes_stroke *stroke)
{
	size_t place;
	int error;

	/* A page that is not there takes nothing. */
	if (page >= document->page_count)
		return EINVAL;

	/* The history's entry: the stroke on top of the page. */
	error = undo_push(document, NOTES_UNDO_ADD_STROKE, page);
	if (error != 0)
		return error;

	/* Puts the stroke on top of the page. */
	place = document->pages[page]->stroke_count;
	error = notes_document_insert_stroke(document, page, place, stroke);
	if (error != 0) {
		document->undo_done--;
		document->undo_count--;
		return error;
	}

	/* Succeeded: the entry names the stroke's place. */
	document->undo[document->undo_done - 1U].place = place;
	return 0;
}

/*
 * Inserts a blank page at an index, as a change the user can undo.
 *
 * Returns 0, EINVAL or ENOMEM.
 */
int
notes_document_add_page(
	struct notes_document *document,
	size_t index)
{
	struct notes_page *page;
	int error;

	/* An index past the end names no place. */
	if (index > document->page_count)
		return EINVAL;

	/* The blank page, the size of the document's first. */
	page = notes_page_create(document->pages[0]->width, document->pages[0]->height, NOTES_BACKGROUND_PLAIN);
	if (page == NULL)
		return ENOMEM;

	/* The history's entry. */
	error = undo_push(document, NOTES_UNDO_ADD_PAGE, index);
	if (error != 0) {
		notes_page_free(page);
		return error;
	}

	/* Puts the page in. */
	error = notes_document_insert_page(document, index, page);
	if (error != 0) {
		document->undo_done--;
		document->undo_count--;
		notes_page_free(page);
		return error;
	}

	/* Succeeded: the page is in the document. */
	return 0;
}

/*
 * Starts an eraser drag: its removals gather into one change to undo.
 */
void
notes_document_erase_begin(
	struct notes_document *document)
{
	/* The first removal of the drag makes the entry; the next ones join it. */
	document->erasing = 1;
}

/*
 * Removes every stroke of a page that an eraser circle touches.
 *
 * The circle is at (x, y) with a radius, in points.  *removed tells how
 * many strokes went.  Returns 0, or ENOMEM (the strokes removed so far stay
 * removed and in the history).
 */
int
notes_document_erase_at(
	struct notes_document *document,
	size_t page,
	float x,
	float y,
	float radius,
	size_t *removed)
{
	struct notes_page *target;
	struct notes_undo *entry;
	struct notes_stroke *stroke;
	uint32_t id;
	size_t index;
	size_t place;
	int touched;
	int error;

	/* Nothing is removed yet. */
	*removed = 0;
	if (page >= document->page_count)
		return EINVAL;
	target = document->pages[page];

	/* Each stroke from the top down; a removal leaves the index at the stroke below. */
	index = target->stroke_count;
	while (index > 0) {
		index--;

		/* A stroke the circle does not touch stays. */
		touched = stroke_touches(target->strokes[index], x, y, radius);
		if (!touched)
			continue;

		/* The drag's entry: the one it made already, or a new one. */
		entry = NULL;
		if (document->erasing == 2 && document->undo_done > 0U)
			entry = &document->undo[document->undo_done - 1U];
		if (entry == NULL ||
		    entry->kind != NOTES_UNDO_REMOVE_STROKES ||
		    entry->page != page) {
			error = undo_push(document, NOTES_UNDO_REMOVE_STROKES, page);
			if (error != 0)
				return error;
			entry = &document->undo[document->undo_done - 1U];
			entry->owned = 1;
			if (document->erasing != 0)
				document->erasing = 2;
		}

		/* Room in the entry for the stroke. */
		error = undo_hold(entry);
		if (error != 0)
			return error;

		/* Takes the stroke off the page. */
		id = target->strokes[index]->id;
		stroke = notes_document_remove_stroke(document, page, id, &place);
		if (stroke == NULL)
			return ENOMEM;

		/* The entry keeps it and its place, in the order of removal. */
		entry->strokes[entry->count] = stroke;
		entry->places[entry->count] = place;
		entry->count++;
		(*removed)++;
	}

	/* Succeeded: every touched stroke is removed. */
	return 0;
}

/*
 * Cuts the ink an eraser circle covers out of the strokes of a page (the
 * partial eraser, design-input-notes.md section 5.1).
 *
 * The circle is at (x, y) with a radius, in points.  A stroke it touches
 * is taken off the page and the pieces of its path outside the circle
 * (widened by half the stroke's width, so that the circle's edge is where
 * the ink ends) are put in its place, each a new stroke of the same tool,
 * colour and width with a new number; where the path crosses the circle
 * the pieces end at the crossing.  A stroke wholly inside leaves no piece.
 * *cut tells how many strokes were cut.  The drag's cuts gather into one
 * change to undo.  Returns 0, EINVAL or ENOMEM (the cuts made so far stay,
 * and in the history).
 */
int
notes_document_erase_parts_at(
	struct notes_document *document,
	size_t page,
	float x,
	float y,
	float radius,
	size_t *cut)
{
	struct notes_page *target;
	struct notes_undo *entry;
	struct notes_stroke *stroke;
	struct notes_stroke **pieces;
	size_t piece_count;
	size_t index;
	size_t place;
	size_t piece;
	int touched;
	int crossed;
	int error;

	/* Nothing is cut yet. */
	*cut = 0;
	if (page >= document->page_count)
		return EINVAL;
	target = document->pages[page];

	/* Each stroke from the top down; the pieces go where the stroke was, above the index. */
	index = target->stroke_count;
	while (index > 0) {
		index--;

		/* A stroke the circle does not touch stays. */
		stroke = target->strokes[index];
		touched = stroke_touches(stroke, x, y, radius);
		if (!touched)
			continue;

		/* The pieces of the path outside the circle; a path the circle does not cross stays. */
		error = stroke_split(document, stroke, x, y, radius + stroke->width / 2.0f, &pieces, &piece_count, &crossed);
		if (error != 0)
			return error;
		if (!crossed)
			continue;

		/* The drag's entry. */
		error = erase_entry(document, page, &entry);
		if (error != 0) {
			pieces_free(pieces, 0, piece_count);
			return error;
		}

		/* Room in it for the removal and the pieces. */
		error = undo_hold_more(entry, 1U + piece_count);
		if (error != 0) {
			pieces_free(pieces, 0, piece_count);
			return error;
		}

		/* Takes the stroke off the page; the entry keeps it and its place. */
		stroke = notes_document_remove_stroke(document, page, stroke->id, &place);
		if (stroke == NULL) {
			pieces_free(pieces, 0, piece_count);
			return ENOMEM;
		}

		/* The removal is the entry's next change. */
		entry->strokes[entry->count] = stroke;
		entry->places[entry->count] = place;
		entry->inserted[entry->count] = 0;
		entry->count++;

		/* Each piece, in the path's order, where the stroke was, with the next number. */
		for (piece = 0; piece < piece_count; piece++) {
			pieces[piece]->id = document->next_id;
			document->next_id++;
			error = notes_document_insert_stroke(document, page, place + piece, pieces[piece]);
			if (error != 0)
				break;

			/* The insertion is the entry's next change. */
			entry->strokes[entry->count] = pieces[piece];
			entry->places[entry->count] = place + piece;
			entry->inserted[entry->count] = 1;
			entry->count++;
		}

		/* Pieces that could not be put in are freed; the cut stands as far as it went. */
		if (error != 0) {
			pieces_free(pieces, piece, piece_count);
			return error;
		}

		/* The stroke is cut. */
		free(pieces);
		(*cut)++;
	}

	/* Succeeded: every stroke the circle crossed is cut. */
	return 0;
}

/*
 * Ends an eraser drag; a later removal starts a change of its own.
 */
void
notes_document_erase_end(
	struct notes_document *document)
{
	/* No drag gathers removals any more. */
	document->erasing = 0;
}

/*
 * Takes back the last change that stands.
 *
 * *page tells the page it touched.  Returns 0, ENOENT when nothing is left
 * to undo, or ENOMEM.
 */
int
notes_document_undo(
	struct notes_document *document,
	size_t *page)
{
	struct notes_undo *entry;
	int error;

	/* Nothing stands to take back. */
	if (document->undo_done == 0U)
		return ENOENT;

	/* Takes the entry's change back. */
	entry = &document->undo[document->undo_done - 1U];
	error = undo_revert(document, entry);
	if (error != 0)
		return error;

	/* Succeeded: the entry can be redone. */
	document->undo_done--;
	*page = entry->page;
	return 0;
}

/*
 * Makes the last change that was taken back again.
 *
 * *page tells the page it touched.  Returns 0, ENOENT when nothing is left
 * to redo, or ENOMEM.
 */
int
notes_document_redo(
	struct notes_document *document,
	size_t *page)
{
	struct notes_undo *entry;
	int error;

	/* Nothing was taken back. */
	if (document->undo_done == document->undo_count)
		return ENOENT;

	/* Makes the entry's change again. */
	entry = &document->undo[document->undo_done];
	error = undo_apply(document, entry);
	if (error != 0)
		return error;

	/* Succeeded: the entry stands again. */
	document->undo_done++;
	*page = entry->page;
	return 0;
}

/*
 * Edits an object of a page, as a change the user can undo (ws175-p007):
 * state is the object's new state (copied, its image held once more) --
 * one of the page's own objects by its key, on a page drawn over the base
 * PDF, or an inserted image by its number, which a new one puts over the
 * page's other inserted objects.  Returns 0, EINVAL, or ENOMEM.
 */
int
notes_document_edit_object(
	struct notes_document *document,
	size_t page,
	const struct notes_edit *state)
{
	unsigned known;

	/* A page that is there, a state of known flags. */
	if (page >= document->page_count || state == NULL)
		return EINVAL;
	known = NOTES_EDIT_KNOWN;
	if ((state->flags & ~known) != 0U)
		return EINVAL;

	/* New words are a line's of the page, UTF-8 there (ws175-p004). */
	if ((state->flags & NOTES_EDIT_TEXT) != 0U && (state->text == NULL || (state->flags & NOTES_EDIT_INSERTED) != 0U))
		return EINVAL;

	/* An inserted object is an image that is there; a page's own is of a page of the base. */
	if ((state->flags & NOTES_EDIT_INSERTED) != 0U) {
		if (state->image == NULL || (state->flags & NOTES_EDIT_DELETED) != 0U)
			return EINVAL;
	} else if (document->pages[page]->origin != NOTES_ORIGIN_OVER) {
		return EINVAL;
	}

	/* An image's edit has its image. */
	if ((state->flags & NOTES_EDIT_IMAGE) != 0U && state->image == NULL)
		return EINVAL;

	/* The change. */
	return edit_change(document, page, state, state);
}

/*
 * Puts an object of a page back as the page has it (Reset), or takes an
 * inserted object off the page, as a change the user can undo (ws175-p007).
 * Returns 0, EINVAL, ENOENT when the object has no edit, or ENOMEM.
 */
int
notes_document_reset_object(
	struct notes_document *document,
	size_t page,
	const struct notes_edit *which)
{
	/* A page that is there and an object. */
	if (page >= document->page_count || which == NULL)
		return EINVAL;

	/* The change: no state after it. */
	return edit_change(document, page, which, NULL);
}

/*
 * Counts the strokes on every page.
 */
size_t
notes_document_stroke_total(
	const struct notes_document *document)
{
	size_t total;
	size_t index;

	/* Adds up each page's strokes. */
	total = 0;
	for (index = 0; index < document->page_count; index++)
		total += document->pages[index]->stroke_count;

	/* Reports the sum. */
	return total;
}

/* Grows an array so that it holds at least a number of elements. */
static int
grow(
	void **array,
	size_t *capacity,
	size_t needed,
	size_t element)
{
	void *larger;
	size_t count;

	/* An array that is large enough stays. */
	if (needed <= *capacity)
		return 0;

	/* Doubles the size, from a small start, until it is enough. */
	count = *capacity;
	if (count < DOCUMENT_GROW_MIN)
		count = DOCUMENT_GROW_MIN;
	while (count < needed)
		count *= 2U;

	/* Refuses a size whose bytes overflow. */
	if (count > (size_t)-1 / element)
		return ENOMEM;

	/* Reallocates the array. */
	larger = realloc(*array, count * element);
	if (larger == NULL)
		return ENOMEM;

	/* Succeeded: the array holds the count. */
	*array = larger;
	*capacity = count;
	return 0;
}

/* Frees what an entry holds and its arrays. */
static void
undo_release(
	struct notes_undo *entry)
{
	size_t index;

	/*
	 * A cutting eraser's entry holds, while it stands, the strokes it took
	 * off, and while it is taken back, the pieces it had put in.
	 */
	if (entry->kind == NOTES_UNDO_ERASE_PARTS) {
		for (index = 0; index < entry->count; index++) {
			if (entry->owned && entry->inserted[index] == 0)
				notes_stroke_free(entry->strokes[index]);
			else if (!entry->owned && entry->inserted[index] != 0)
				notes_stroke_free(entry->strokes[index]);
		}
	} else if (entry->owned) {
		/* The strokes or the page the entry holds while they are off the document. */
		for (index = 0; entry->strokes != NULL && index < entry->count; index++)
			notes_stroke_free(entry->strokes[index]);
		notes_page_free(entry->page_held);
	}

	/* The states of an edited object, then the arrays. */
	notes_edit_free(entry->edit_before);
	notes_edit_free(entry->edit_after);
	free(entry->strokes);
	free(entry->places);
	free(entry->inserted);
	memset(entry, 0, sizeof(*entry));
}

/*
 * Starts a new entry after the ones that stand.
 *
 * The entries that were taken back can no longer be redone and go; the
 * oldest goes when the history is full.
 */
static int
undo_push(
	struct notes_document *document,
	unsigned kind,
	size_t page)
{
	struct notes_undo *larger;
	size_t index;

	/* The changes taken back are forgotten. */
	for (index = document->undo_done; index < document->undo_count; index++)
		undo_release(&document->undo[index]);
	document->undo_count = document->undo_done;

	/* A full history forgets its oldest change. */
	if (document->undo_count == NOTES_UNDO_LIMIT) {
		undo_release(&document->undo[0]);
		memmove(&document->undo[0], &document->undo[1], (NOTES_UNDO_LIMIT - 1U) * sizeof(document->undo[0]));
		document->undo_count--;
		document->undo_done--;
	}

	/* The history's array, made once at its full size. */
	if (document->undo == NULL) {
		larger = calloc(NOTES_UNDO_LIMIT, sizeof(*larger));
		if (larger == NULL)
			return ENOMEM;
		document->undo = larger;
	}

	/* Succeeded: an empty entry of the kind stands last. */
	memset(&document->undo[document->undo_count], 0, sizeof(document->undo[0]));
	document->undo[document->undo_count].kind = kind;
	document->undo[document->undo_count].page = page;
	document->undo_count++;
	document->undo_done++;
	return 0;
}

/* Makes room in an entry for one more stroke and its place. */
static int
undo_hold(
	struct notes_undo *entry)
{
	struct notes_stroke **strokes;
	size_t *places;
	unsigned char *inserted;
	size_t count;

	/* An entry with room keeps its arrays. */
	if (entry->count < entry->capacity)
		return 0;

	/* Doubles the arrays from a small start. */
	count = entry->capacity * 2U;
	if (count < DOCUMENT_GROW_MIN)
		count = DOCUMENT_GROW_MIN;

	/* The strokes' array. */
	strokes = realloc(entry->strokes, count * sizeof(entry->strokes[0]));
	if (strokes == NULL)
		return ENOMEM;
	entry->strokes = strokes;

	/* The places' array. */
	places = realloc(entry->places, count * sizeof(entry->places[0]));
	if (places == NULL)
		return ENOMEM;
	entry->places = places;

	/* The array that tells insertions from removals (a cutting eraser's). */
	inserted = realloc(entry->inserted, count * sizeof(entry->inserted[0]));
	if (inserted == NULL)
		return ENOMEM;
	entry->inserted = inserted;

	/* Succeeded: the entry has room. */
	entry->capacity = count;
	return 0;
}

/* Makes room in an entry for a number of strokes and their places. */
static int
undo_hold_more(
	struct notes_undo *entry,
	size_t more)
{
	struct notes_stroke **strokes;
	size_t *places;
	unsigned char *inserted;
	size_t needed;
	size_t count;

	/* An entry with room keeps its arrays. */
	needed = entry->count + more;
	if (needed <= entry->capacity)
		return 0;

	/* Doubles the size from a small start until the number fits. */
	count = entry->capacity * 2U;
	if (count < DOCUMENT_GROW_MIN)
		count = DOCUMENT_GROW_MIN;
	while (count < needed)
		count *= 2U;

	/* The strokes' array. */
	strokes = realloc(entry->strokes, count * sizeof(entry->strokes[0]));
	if (strokes == NULL)
		return ENOMEM;
	entry->strokes = strokes;

	/* The places' array. */
	places = realloc(entry->places, count * sizeof(entry->places[0]));
	if (places == NULL)
		return ENOMEM;
	entry->places = places;

	/* The array that tells insertions from removals. */
	inserted = realloc(entry->inserted, count * sizeof(entry->inserted[0]));
	if (inserted == NULL)
		return ENOMEM;
	entry->inserted = inserted;

	/* Succeeded: the entry has room for them all. */
	entry->capacity = count;
	return 0;
}

/*
 * Finds the entry a cutting eraser's drag gathers its changes in: the one
 * it made already, or a new one.
 */
static int
erase_entry(
	struct notes_document *document,
	size_t page,
	struct notes_undo **entry)
{
	struct notes_undo *last;
	int error;

	/* The drag's own entry, when it made one on this page. */
	last = NULL;
	if (document->erasing == 2 && document->undo_done > 0U)
		last = &document->undo[document->undo_done - 1U];
	if (last != NULL &&
	    last->kind == NOTES_UNDO_ERASE_PARTS &&
	    last->page == page) {
		*entry = last;
		return 0;
	}

	/* Otherwise a new entry, which stands and holds what the drag takes off. */
	error = undo_push(document, NOTES_UNDO_ERASE_PARTS, page);
	if (error != 0)
		return error;
	last = &document->undo[document->undo_done - 1U];
	last->owned = 1;

	/* A drag under way gathers its next changes in it. */
	if (document->erasing != 0)
		document->erasing = 2;

	/* Succeeded: the new entry. */
	*entry = last;
	return 0;
}

/* Takes an entry's change back with the primitive changes. */
static int
undo_revert(
	struct notes_document *document,
	struct notes_undo *entry)
{
	struct notes_page *target;
	struct notes_stroke *stroke;
	struct notes_page *page;
	size_t place;
	size_t index;
	int error;

	/* Each kind of change has its opposite. */
	switch (entry->kind) {
	case NOTES_UNDO_ADD_STROKE:
		/* The added stroke comes off the place it was put at. */
		target = document->pages[entry->page];
		place = entry->place;
		if (place >= target->stroke_count)
			return EINVAL;
		stroke = notes_document_remove_stroke(document, entry->page, target->strokes[place]->id, &place);
		if (stroke == NULL)
			return ENOMEM;

		/* The entry holds it until it is redone. */
		entry->strokes = malloc(sizeof(entry->strokes[0]));
		if (entry->strokes == NULL) {
			(void)notes_document_insert_stroke(document, entry->page, place, stroke);
			return ENOMEM;
		}

		/* The entry holds the stroke until it is redone. */
		entry->strokes[0] = stroke;
		entry->owned = 1;
		break;
	case NOTES_UNDO_REMOVE_STROKES:
		/* The removed strokes go back, the last removed first, each at its place. */
		index = entry->count;
		while (index > 0) {
			index--;
			error = notes_document_insert_stroke(document, entry->page, entry->places[index], entry->strokes[index]);
			if (error != 0)
				return error;
		}

		/* The page owns the strokes again. */
		entry->owned = 0;
		break;
	case NOTES_UNDO_ADD_PAGE:
		/* The added page comes out, and the entry holds it. */
		page = notes_document_remove_page(document, entry->page);
		if (page == NULL)
			return EINVAL;
		entry->page_held = page;
		entry->owned = 1;
		break;
	case NOTES_UNDO_ERASE_PARTS:
		/* The cuts' changes, the last first: each piece comes off, each cut stroke goes back to its place. */
		index = entry->count;
		while (index > 0) {
			index--;
			if (entry->inserted[index] != 0) {
				stroke = notes_document_remove_stroke(document, entry->page, entry->strokes[index]->id, &place);
				if (stroke == NULL)
					return ENOMEM;
			} else {
				error = notes_document_insert_stroke(document, entry->page, entry->places[index], entry->strokes[index]);
				if (error != 0)
					return error;
			}
		}

		/* The entry holds the pieces until it is redone. */
		entry->owned = 0;
		break;
	case NOTES_UNDO_EDIT_OBJECT:
		/* The object's state goes back to the one before. */
		error = edit_swap(document, entry, entry->edit_after, entry->edit_before);
		if (error != 0)
			return error;
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the change is taken back. */
	return 0;
}

/* Makes an entry's change again with the primitive changes. */
static int
undo_apply(
	struct notes_document *document,
	struct notes_undo *entry)
{
	struct notes_stroke *stroke;
	size_t place;
	size_t index;
	int error;

	/* Each kind of change is made as it was. */
	switch (entry->kind) {
	case NOTES_UNDO_ADD_STROKE:
		/* The stroke goes back to its place. */
		error = notes_document_insert_stroke(document, entry->page, entry->place, entry->strokes[0]);
		if (error != 0)
			return error;
		free(entry->strokes);
		entry->strokes = NULL;
		entry->owned = 0;
		break;
	case NOTES_UNDO_REMOVE_STROKES:
		/* The strokes come off again, in the order they were removed. */
		for (index = 0; index < entry->count; index++) {
			stroke = notes_document_remove_stroke(document, entry->page, entry->strokes[index]->id, &place);
			if (stroke == NULL)
				return ENOMEM;
		}

		/* The entry holds the strokes again. */
		entry->owned = 1;
		break;
	case NOTES_UNDO_ADD_PAGE:
		/* The page goes back in. */
		error = notes_document_insert_page(document, entry->page, entry->page_held);
		if (error != 0)
			return error;
		entry->page_held = NULL;
		entry->owned = 0;
		break;
	case NOTES_UNDO_ERASE_PARTS:
		/* The cuts' changes again, in order: each cut stroke comes off, each piece goes to its place. */
		for (index = 0; index < entry->count; index++) {
			if (entry->inserted[index] != 0) {
				error = notes_document_insert_stroke(document, entry->page, entry->places[index], entry->strokes[index]);
				if (error != 0)
					return error;
			} else {
				stroke = notes_document_remove_stroke(document, entry->page, entry->strokes[index]->id, &place);
				if (stroke == NULL)
					return ENOMEM;
			}
		}

		/* The entry holds the cut strokes again. */
		entry->owned = 1;
		break;
	case NOTES_UNDO_EDIT_OBJECT:
		/* The object's state is the one after again. */
		error = edit_swap(document, entry, entry->edit_before, entry->edit_after);
		if (error != 0)
			return error;
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the change stands again. */
	return 0;
}

/* Tells whether an eraser circle touches a stroke's ink. */
static int
stroke_touches(
	struct notes_stroke *stroke,
	float x,
	float y,
	float radius)
{
	float reach;
	float distance;
	size_t index;
	int error;

	/* The outline's box, which a circle outside cannot touch. */
	error = notes_stroke_outline(stroke);
	if (error != 0)
		return 0;
	if (x + radius < stroke->bounds[0] || x - radius > stroke->bounds[2])
		return 0;
	if (y + radius < stroke->bounds[1] || y - radius > stroke->bounds[3])
		return 0;

	/* The circle touches the ink when it comes within half the width of the stroke's path. */
	reach = radius + stroke->width / 2.0f;

	/* A single sample is a dot. */
	if (stroke->point_count == 1U) {
		distance = segment_distance(x, y, stroke->points[0].x, stroke->points[0].y, stroke->points[0].x, stroke->points[0].y);
		if (distance <= reach)
			return 1;
		return 0;
	}

	/* Each segment of the path. */
	for (index = 0; index + 1U < stroke->point_count; index++) {
		distance = segment_distance(x, y, stroke->points[index].x, stroke->points[index].y, stroke->points[index + 1U].x, stroke->points[index + 1U].y);
		if (distance <= reach)
			return 1;
	}

	/* No segment comes near. */
	return 0;
}

/* Measures the distance from a point to a segment. */
static float
segment_distance(
	float px,
	float py,
	float ax,
	float ay,
	float bx,
	float by)
{
	float dx;
	float dy;
	float length;
	float share;
	float cx;
	float cy;

	/* The segment's direction and squared length. */
	dx = bx - ax;
	dy = by - ay;
	length = dx * dx + dy * dy;

	/* The share of the way along the segment of the point's nearest place, kept within it. */
	share = 0.0f;
	if (length > 0.0f) {
		share = ((px - ax) * dx + (py - ay) * dy) / length;
		if (share < 0.0f)
			share = 0.0f;
		if (share > 1.0f)
			share = 1.0f;
	}

	/* Reports the distance to that place. */
	cx = ax + dx * share - px;
	cy = ay + dy * share - py;
	return (float)sqrt((double)(cx * cx + cy * cy));
}

/*
 * Cuts a stroke's path by a circle of a reach: makes the pieces of the path
 * outside it, in the path's order, and tells whether the path crossed it
 * at all (when it did not, no pieces are made).  The caller owns the
 * pieces and their array.
 */
static int
stroke_split(
	struct notes_document *document,
	const struct notes_stroke *stroke,
	float x,
	float y,
	float reach,
	struct notes_stroke ***pieces,
	size_t *piece_count,
	int *crossed)
{
	struct notes_stroke *piece;
	struct notes_point point;
	const struct notes_point *a;
	const struct notes_point *b;
	size_t piece_capacity;
	size_t index;
	float enter;
	float leave;
	float dx;
	float dy;
	int inside;
	int error;

	/* No pieces yet, and nothing crossed. */
	*pieces = NULL;
	*piece_count = 0;
	piece_capacity = 0;
	*crossed = 0;
	piece = NULL;
	error = 0;

	/* The first sample starts a piece when it lies outside the circle. */
	dx = stroke->points[0].x - x;
	dy = stroke->points[0].y - y;
	if (dx * dx + dy * dy < reach * reach) {
		*crossed = 1;
	} else {
		error = piece_add(document, stroke, &piece, &stroke->points[0]);
		if (error != 0)
			return error;
	}

	/* Each segment of the path: the part inside the circle is cut out of it. */
	for (index = 1; index < stroke->point_count; index++) {
		a = &stroke->points[index - 1U];
		b = &stroke->points[index];
		inside = segment_inside(a, b, x, y, reach, &enter, &leave);

		/* A segment outside the circle carries the piece on to its end. */
		if (!inside) {
			if (piece == NULL) {
				error = piece_add(document, stroke, &piece, a);
				if (error != 0)
					break;
			}

			/* The segment's end joins the piece. */
			error = piece_add(document, stroke, &piece, b);
			if (error != 0)
				break;
			continue;
		}

		/* The segment enters the circle: the piece ends where it does. */
		*crossed = 1;
		if (enter > 0.0f && piece != NULL) {
			point_between(a, b, enter, &point);
			error = piece_add(document, stroke, &piece, &point);
			if (error != 0)
				break;
		}

		/* The piece before the circle is finished. */
		error = piece_close(&piece, pieces, piece_count, &piece_capacity);
		if (error != 0)
			break;

		/* A segment that leaves the circle starts a new piece where it does. */
		if (leave < 1.0f) {
			point_between(a, b, leave, &point);
			error = piece_add(document, stroke, &piece, &point);
			if (error != 0)
				break;

			/* The segment's end joins it. */
			error = piece_add(document, stroke, &piece, b);
			if (error != 0)
				break;
		}
	}

	/* The last piece is finished. */
	if (error == 0)
		error = piece_close(&piece, pieces, piece_count, &piece_capacity);

	/* A failure frees every piece made. */
	if (error != 0) {
		notes_stroke_free(piece);
		pieces_free(*pieces, 0, *piece_count);
		*pieces = NULL;
		*piece_count = 0;
		return error;
	}

	/* A path that was not cut needs none of its pieces. */
	if (!*crossed) {
		pieces_free(*pieces, 0, *piece_count);
		*pieces = NULL;
		*piece_count = 0;
	}

	/* Succeeded: the pieces, when the path was cut. */
	return 0;
}

/*
 * Finds the part of a segment inside a circle, as shares of the way from a
 * to b (enter to leave, within 0 to 1).  Returns 0 when no part of it is.
 */
static int
segment_inside(
	const struct notes_point *a,
	const struct notes_point *b,
	float x,
	float y,
	float reach,
	float *enter,
	float *leave)
{
	double dx;
	double dy;
	double fx;
	double fy;
	double along;
	double across;
	double rest;
	double discriminant;
	double root;
	double first;
	double second;

	/* The segment's direction, and its start seen from the circle's centre. */
	dx = (double)b->x - (double)a->x;
	dy = (double)b->y - (double)a->y;
	fx = (double)a->x - (double)x;
	fy = (double)a->y - (double)y;

	/* The quadratic of the shares where the segment's line meets the circle. */
	along = dx * dx + dy * dy;
	across = 2.0 * (fx * dx + fy * dy);
	rest = fx * fx + fy * fy - (double)reach * (double)reach;

	/* A segment of no length is inside when its point is. */
	if (along <= 0.0) {
		if (rest >= 0.0)
			return 0;
		*enter = 0.0f;
		*leave = 1.0f;
		return 1;
	}

	/* A line that misses the circle, or only touches it, has no part inside. */
	discriminant = across * across - 4.0 * along * rest;
	if (discriminant <= 0.0)
		return 0;

	/* The two meeting shares, kept within the segment. */
	root = sqrt(discriminant);
	first = (-across - root) / (2.0 * along);
	second = (-across + root) / (2.0 * along);
	if (first < 0.0)
		first = 0.0;
	if (second > 1.0)
		second = 1.0;

	/* A segment that ends before the circle, or starts after it, has no part inside. */
	if (first >= second)
		return 0;

	/* Succeeded: the part inside. */
	*enter = (float)first;
	*leave = (float)second;
	return 1;
}

/* Makes the sample a share of the way from one sample to the next, on the edit data's grid. */
static void
point_between(
	const struct notes_point *a,
	const struct notes_point *b,
	float share,
	struct notes_point *point)
{
	/* The place, on the 1/64 point grid. */
	memset(point, 0, sizeof(*point));
	point->x = notes_quantize(a->x + (b->x - a->x) * share);
	point->y = notes_quantize(a->y + (b->y - a->y) * share);

	/* The pressure, the tilt and the time, in between. */
	point->pressure = (uint16_t)((float)a->pressure + ((float)b->pressure - (float)a->pressure) * share + 0.5f);
	point->tilt_x = (int16_t)((float)a->tilt_x + ((float)b->tilt_x - (float)a->tilt_x) * share);
	point->tilt_y = (int16_t)((float)a->tilt_y + ((float)b->tilt_y - (float)a->tilt_y) * share);
	point->time_ms = a->time_ms + (uint32_t)((float)(b->time_ms - a->time_ms) * share);
}

/*
 * Adds a sample to the piece being made of a stroke, making the piece
 * first (a stroke of the same tool, colour and width) when there is none.
 */
static int
piece_add(
	struct notes_document *document,
	const struct notes_stroke *stroke,
	struct notes_stroke **piece,
	const struct notes_point *point)
{
	int error;

	/* A new piece; it takes its number once it is kept. */
	(void)document;
	if (*piece == NULL) {
		*piece = notes_stroke_create(0U, stroke->tool, stroke->color, stroke->width, stroke->start_ms);
		if (*piece == NULL)
			return ENOMEM;
		(*piece)->has_tilt = stroke->has_tilt;
	}

	/* The sample joins it. */
	error = notes_stroke_append(*piece, point);
	if (error != 0)
		return error;

	/* Succeeded: the piece has the sample. */
	return 0;
}

/*
 * Finishes the piece being made: one of two samples or more joins the
 * pieces, a shorter one (a crumb at the circle's edge) is dropped.
 */
static int
piece_close(
	struct notes_stroke **piece,
	struct notes_stroke ***pieces,
	size_t *piece_count,
	size_t *piece_capacity)
{
	uint32_t shift;
	size_t index;
	int error;

	/* No piece under way. */
	if (*piece == NULL)
		return 0;

	/* A crumb is dropped. */
	if ((*piece)->point_count < 2U) {
		notes_stroke_free(*piece);
		*piece = NULL;
		return 0;
	}

	/*
	 * The piece starts when its first sample was drawn, and its samples'
	 * times count from there, as a stroke's always do (the edit data keeps
	 * them so).
	 */
	shift = (*piece)->points[0].time_ms;
	(*piece)->start_ms += shift;
	for (index = 0; index < (*piece)->point_count; index++)
		(*piece)->points[index].time_ms -= shift;

	/* Room for one more piece. */
	error = grow((void **)pieces, piece_capacity, *piece_count + 1U, sizeof((*pieces)[0]));
	if (error != 0)
		return error;

	/* Succeeded: the piece is the last, and none is under way. */
	(*pieces)[*piece_count] = *piece;
	(*piece_count)++;
	*piece = NULL;
	return 0;
}

/* Frees pieces from one index to a count, and their array. */
static void
pieces_free(
	struct notes_stroke **pieces,
	size_t first,
	size_t count)
{
	size_t index;

	/* Each piece, then the array. */
	for (index = first; index < count; index++)
		notes_stroke_free(pieces[index]);
	free(pieces);
}

/*
 * Changes the state of an object of a page (which names it) to a state
 * (NULL: none), entered in the history: the old state taken off and the
 * new one put at its place (a new inserted object on top of the page's
 * edits).
 */
static int
edit_change(
	struct notes_document *document,
	size_t page,
	const struct notes_edit *which,
	const struct notes_edit *state)
{
	struct notes_undo *entry;
	struct notes_edit *before;
	struct notes_edit *after;
	struct notes_edit *kept;
	size_t place;
	int error;

	/* The new state, on its grids, and a copy for the history. */
	after = NULL;
	kept = NULL;
	if (state != NULL) {
		after = notes_edit_copy(state);
		if (after == NULL)
			return ENOMEM;
		notes_edit_quantize(after);
		kept = notes_edit_copy(after);
		if (kept == NULL) {
			notes_edit_free(after);
			return ENOMEM;
		}
	}

	/* The history's entry. */
	error = undo_push(document, NOTES_UNDO_EDIT_OBJECT, page);
	if (error != 0) {
		notes_edit_free(after);
		notes_edit_free(kept);
		return error;
	}

	/* The old state off the page; a reset of an object without one changes nothing. */
	place = document->pages[page]->edit_count;
	before = notes_document_take_edit(document, page, which, &place);
	if (before == NULL && after == NULL) {
		document->undo_done--;
		document->undo_count--;
		return ENOENT;
	}

	/* The new state in its place. */
	if (after != NULL) {
		error = notes_document_put_edit(document, page, place, after);
		if (error != 0) {
			if (before != NULL)
				(void)notes_document_put_edit(document, page, place, before);
			notes_edit_free(after);
			notes_edit_free(kept);
			document->undo_done--;
			document->undo_count--;
			return error;
		}
	}

	/* Succeeded: the entry holds the states before and after, and the place. */
	entry = &document->undo[document->undo_done - 1U];
	entry->edit_before = before;
	entry->edit_after = kept;
	entry->place = place;
	return 0;
}

/*
 * Changes an object's state from one an entry holds to the other (either
 * NULL: none) at the entry's place: the page's state off, a copy of the
 * other on.
 */
static int
edit_swap(
	struct notes_document *document,
	const struct notes_undo *entry,
	const struct notes_edit *from,
	const struct notes_edit *to)
{
	struct notes_edit *taken;
	struct notes_edit *copy;
	size_t place;
	int error;

	/* The copy that goes on the page. */
	copy = NULL;
	if (to != NULL) {
		copy = notes_edit_copy(to);
		if (copy == NULL)
			return ENOMEM;
	}

	/* The state the page has off it. */
	taken = NULL;
	if (from != NULL) {
		taken = notes_document_take_edit(document, entry->page, from, &place);
		if (taken == NULL) {
			notes_edit_free(copy);
			return EINVAL;
		}
	}

	/* The other on, at the entry's place (the one taken off goes back when it cannot be). */
	if (copy != NULL) {
		error = notes_document_put_edit(document, entry->page, entry->place, copy);
		if (error != 0) {
			if (taken != NULL)
				(void)notes_document_put_edit(document, entry->page, place, taken);
			notes_edit_free(copy);
			return error;
		}
	}

	/* Succeeded: the object's state is the other. */
	notes_edit_free(taken);
	return 0;
}
