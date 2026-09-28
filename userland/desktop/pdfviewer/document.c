/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The open document of PDF Viewer and the cache of its pages.
 *
 * A page is interpreted once into its display list, when it is first
 * shown, and rasterized on white at the scale it is shown at; the raster
 * is kept until the scale changes or the cache needs its memory, the least
 * recently shown page going first.
 */

#include "viewer.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The memory the page rasters may take together. */
#define DOCUMENT_CACHE_BYTES	((size_t)192 * 1024 * 1024)

/* The largest raster side, in pixels. */
#define DOCUMENT_RASTER_SIDE	8192

/* The white of a page. */
#define DOCUMENT_PAGE_WHITE	0xffffffffU

static void drop_raster(struct pv_document *document, struct pv_page *page);
static void evict(struct pv_document *document, size_t needed, size_t keep);

/*
 * Opens a PDF file and reads the shown size of each of its pages.
 *
 * Returns 0, or an errno value (libpdf's: ENOTSUP for a PDF that uses what
 * the reader does not read yet, PDF_EFORMAT for a malformed one, EINVAL for
 * a document without pages).
 */
int
pv_document_open(
	struct pv_document *document,
	const char *path)
{
	struct pdf_page_box box;
	size_t index;
	int error;

	/* Starts with nothing open. */
	memset(document, 0, sizeof(*document));
	snprintf(document->path, sizeof(document->path), "%s", path);

	/* Opens the file with libpdf. */
	error = pdf_document_open(path, &document->document);
	if (error != 0) {
		document->document = NULL;
		return error;
	}

	/* A document without pages has nothing to show. */
	document->count = pdf_document_page_count(document->document);
	if (document->count == 0) {
		pv_document_close(document);
		return EINVAL;
	}

	/* Allocates the pages. */
	document->pages = calloc(document->count, sizeof(document->pages[0]));
	if (document->pages == NULL) {
		pv_document_close(document);
		return ENOMEM;
	}

	/* Reads each page's shown size; a page whose boxes cannot be read is shown as A4. */
	for (index = 0; index < document->count; index++) {
		document->pages[index].width = 595.276;
		document->pages[index].height = 841.89;
		error = pdf_document_page_box(document->document, index, &box);
		if (error == 0 && box.width >= 1.0 && box.height >= 1.0 && box.width < 20000.0 && box.height < 20000.0) {
			document->pages[index].width = box.width;
			document->pages[index].height = box.height;
		}
		if (document->pages[index].width > document->widest)
			document->widest = document->pages[index].width;
		if (document->pages[index].height > document->tallest)
			document->tallest = document->pages[index].height;
	}

	/* Succeeded: the pages can be shown. */
	return 0;
}

/*
 * Closes the document and frees its pages' lists and rasters.
 */
void
pv_document_close(
	struct pv_document *document)
{
	size_t index;

	/* Frees each page's list and raster. */
	for (index = 0; document->pages != NULL && index < document->count; index++) {
		pdf_display_list_destroy(document->pages[index].list);
		free(document->pages[index].raster);
	}
	free(document->pages);

	/* Closes libpdf's document. */
	if (document->document != NULL)
		pdf_document_close(document->document);

	/* Nothing is open any more. */
	memset(document, 0, sizeof(*document));
}

/*
 * Makes a page's raster at a scale (pixels per point), interpreting the
 * page first when it has not been.
 *
 * The page's raster may be at another scale when the requested one is too
 * large (larger than DOCUMENT_RASTER_SIDE a side) or cannot be made; the
 * caller draws raster_scale's size.  Returns 0, or an errno value when the
 * page has no raster at all.
 */
int
pv_document_raster(
	struct pv_document *document,
	size_t index,
	double scale,
	const struct pv_page **shown)
{
	struct pv_page *page;
	uint32_t *pixels;
	uint64_t started;
	size_t count;
	size_t cell;
	int width;
	int height;
	int error;

	/* Refuses a page the document does not have. */
	if (index >= document->count)
		return EINVAL;
	page = &document->pages[index];
	document->clock++;
	page->used = document->clock;
	*shown = page;

	/* Interprets the page once; a page that cannot be interpreted stays blank. */
	if (page->list == NULL && page->list_error == 0) {
		started = pv_clock();
		error = pdf_page_render(document->document, index, &page->list);
		if (error != 0) {
			page->list = NULL;
			page->list_error = error;
			pv_log("PAGE index=%lu render-error=%d", (unsigned long)index, error);
		} else {
			document->flags |= page->list->flags;
			pv_log("PAGE index=%lu items=%lu flags=%u ms=%lu", (unsigned long)index, (unsigned long)page->list->count, page->list->flags,
			    (unsigned long)(pv_clock() - started));
		}
	}

	/* A raster at the scale is kept. */
	if (page->raster != NULL && fabs(page->raster_scale - scale) < 1e-6)
		return 0;

	/* Keeps the raster within the largest side. */
	if (page->width * scale > (double)DOCUMENT_RASTER_SIDE)
		scale = (double)DOCUMENT_RASTER_SIDE / page->width;
	if (page->height * scale > (double)DOCUMENT_RASTER_SIDE)
		scale = (double)DOCUMENT_RASTER_SIDE / page->height;
	width = (int)ceil(page->width * scale - 1e-6);
	height = (int)ceil(page->height * scale - 1e-6);
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	/* The old raster goes, and others when the cache would pass its size. */
	drop_raster(document, page);
	count = (size_t)width * (size_t)height;
	evict(document, count * 4, index);

	/* Allocates the new one on white. */
	pixels = malloc(count * 4);
	if (pixels == NULL)
		return ENOMEM;
	for (cell = 0; cell < count; cell++)
		pixels[cell] = DOCUMENT_PAGE_WHITE;

	/* Draws the page's list, when it has one. */
	if (page->list != NULL) {
		started = pv_clock();
		error = pdf_display_list_rasterize(page->list, pixels, (size_t)width, (size_t)width, (size_t)height, scale, 0.0, 0.0);
		if (error != 0)
			pv_log("PAGE index=%lu raster-error=%d", (unsigned long)index, error);
		pv_log("RASTER index=%lu size=%dx%d ms=%lu", (unsigned long)index, width, height, (unsigned long)(pv_clock() - started));
	}

	/* Keeps the raster. */
	page->raster = pixels;
	page->raster_width = width;
	page->raster_height = height;
	page->raster_scale = scale;
	document->raster_bytes += count * 4;

	/* Succeeded: the page has a raster. */
	return 0;
}

/*
 * Frees the rasters of the pages outside a range, which the view no
 * longer shows (their lists are kept).
 */
void
pv_document_trim(
	struct pv_document *document,
	size_t keep_first,
	size_t keep_last)
{
	size_t index;

	/* Frees each raster outside the range while the cache is above half its size. */
	for (index = 0; index < document->count; index++) {
		if (document->raster_bytes <= DOCUMENT_CACHE_BYTES / 2)
			break;
		if (index >= keep_first && index <= keep_last)
			continue;
		drop_raster(document, &document->pages[index]);
	}
}

/* Frees a page's raster and uncounts its bytes. */
static void
drop_raster(
	struct pv_document *document,
	struct pv_page *page)
{
	size_t bytes;

	/* Nothing to free for a page without one. */
	if (page->raster == NULL)
		return;

	/* Uncounts and frees it. */
	bytes = (size_t)page->raster_width * (size_t)page->raster_height * 4;
	document->raster_bytes -= bytes;
	free(page->raster);
	page->raster = NULL;
	page->raster_width = 0;
	page->raster_height = 0;
	page->raster_scale = 0.0;
}

/* Frees the least recently shown rasters until a new one of some bytes fits, never the page kept. */
static void
evict(
	struct pv_document *document,
	size_t needed,
	size_t keep)
{
	size_t index;
	size_t oldest;
	uint64_t oldest_used;

	/* Frees one raster at a time while the new one does not fit. */
	while (document->raster_bytes + needed > DOCUMENT_CACHE_BYTES) {
		/* Finds the least recently shown raster. */
		oldest = document->count;
		oldest_used = 0;
		for (index = 0; index < document->count; index++) {
			if (index == keep || document->pages[index].raster == NULL)
				continue;
			if (oldest == document->count || document->pages[index].used < oldest_used) {
				oldest = index;
				oldest_used = document->pages[index].used;
			}
		}

		/* Nothing is left to free. */
		if (oldest == document->count)
			return;
		drop_raster(document, &document->pages[oldest]);
	}
}
