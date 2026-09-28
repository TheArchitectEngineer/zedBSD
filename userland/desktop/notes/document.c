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
static int stroke_touches(struct notes_stroke *stroke, float x, float y, float radius);
static float segment_distance(float px, float py, float ax, float ay, float bx, float by);

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
 * Frees the pages, their strokes and the undo history.
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

	/* The strokes, then the page. */
	for (index = 0; index < page->stroke_count; index++)
		notes_stroke_free(page->strokes[index]);
	free(page->strokes);
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

	/* Succeeded: the document changed since its last save. */
	document->dirty = 1;
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

	/* Succeeded: the document changed since its last save. */
	document->dirty = 1;
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

	/* Succeeded: the document changed since its last save. */
	document->dirty = 1;
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
		if (entry == NULL || entry->kind != NOTES_UNDO_REMOVE_STROKES || entry->page != page) {
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

	/* The strokes or the page the entry holds while they are off the document. */
	if (entry->owned) {
		for (index = 0; entry->strokes != NULL && index < entry->count; index++)
			notes_stroke_free(entry->strokes[index]);
		notes_page_free(entry->page_held);
	}

	/* The arrays. */
	free(entry->strokes);
	free(entry->places);
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

	/* Succeeded: the entry has room. */
	entry->capacity = count;
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
