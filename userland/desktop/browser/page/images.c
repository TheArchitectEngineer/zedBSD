/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A page's images: the <img> elements' sources fetched and decoded before
 * the page is laid out, and kept by their location for the life of the
 * page (a source that could not be fetched or decoded is remembered as
 * failed, and not fetched again).  The fetch is synchronous in this pass;
 * the asynchronous loader (ws074-p050) will fill the same table.
 */

#include "page/page.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The deepest element nesting the walk descends (the parser caps nesting too). */
#define IMAGES_DEPTH		512

/*
 * One image of the page: the location its source resolved to (the key),
 * the decoded bitmap, and whether it failed.
 */
struct page_image {
	char *location;
	struct img_bitmap bitmap;
	int failed;
};

static int images_walk(struct page *page, const struct dom_node *node, const struct vm_string *src, int depth);
static int images_source(struct page *page, const struct dom_element *element, const struct vm_string *src, struct wb_buffer *location, int *found);
static struct page_image *images_find(const struct page *page, const char *location);
static int images_load(struct page *page, const struct dom_element *element, const struct vm_string *src);

/*
 * Starts a page's table of images empty.
 */
void
page_images_init(
	struct page *page)
{
	/* No image yet. */
	wb_vector_init(&page->images, sizeof(struct page_image));
}

/*
 * Fetches and decodes the source of every <img> of the document that is
 * not in the page's table yet.
 */
int
page_load_images(
	struct page *page)
{
	struct vm_string *src;
	int error;

	/* The attribute's name, as the atom the elements keep. */
	src = vm_atom_from_ascii(page->heap, "src");
	if (src == NULL)
		return ENOMEM;

	/* Every element of the document. */
	error = images_walk(page, &page->document->node, src, 0);
	if (error != 0)
		return error;

	/* Succeeded: every image the document names is in the table. */
	return 0;
}

/*
 * Finds the decoded image an <img> element shows, for the layout (its
 * context is the page); NULL for an element with no source, or one that
 * failed or was not loaded.
 */
const struct img_bitmap *
page_image_of(
	void *context,
	const struct dom_element *element)
{
	struct page *page;
	struct page_image *image;
	struct vm_string *src;
	struct wb_buffer location;
	int found;
	int error;

	/* The element's source, resolved against the page's location. */
	page = context;
	src = vm_atom_from_ascii(page->heap, "src");
	if (src == NULL)
		return NULL;
	wb_buffer_init(&location);
	error = images_source(page, element, src, &location, &found);
	if (error != 0 || !found) {
		wb_buffer_release(&location);
		return NULL;
	}

	/* The image of that location, when it was decoded. */
	image = images_find(page, wb_buffer_string(&location));
	wb_buffer_release(&location);
	if (image == NULL || image->failed)
		return NULL;

	/* Succeeded: the image's bitmap. */
	return &image->bitmap;
}

/*
 * Frees the page's images.
 */
void
page_images_release(
	struct page *page)
{
	struct page_image *image;
	size_t index;

	/* Each image's location and pixels, then the table. */
	for (index = 0; index < page->images.count; index++) {
		image = wb_vector_at(&page->images, index);
		free(image->location);
		img_bitmap_release(&image->bitmap);
	}

	/* The table itself. */
	wb_vector_release(&page->images);
}

/* Loads the images of a node's element descendants (and its own, when it is an <img>). */
static int
images_walk(
	struct page *page,
	const struct dom_node *node,
	const struct vm_string *src,
	int depth)
{
	const struct dom_element *element;
	const struct dom_node *child;
	int error;

	/* Stops at the depth the parser stops at. */
	if (depth > IMAGES_DEPTH)
		return 0;

	/* An HTML <img> loads its source. */
	if (node->type == DOM_ELEMENT) {
		element = (const struct dom_element *)node;
		if (element->ns == DOM_NS_HTML && element->tag == DOM_TAG_IMG) {
			error = images_load(page, element, src);
			if (error != 0)
				return error;
		}
	}

	/* The children, in document order. */
	for (child = node->first_child; child != NULL; child = child->next) {
		error = images_walk(page, child, src, depth + 1);
		if (error != 0)
			return error;
	}

	/* Succeeded: the subtree's images are loaded. */
	return 0;
}

/*
 * Writes an element's source resolved against the page's location;
 * *found is 0 for an element with no source (or a page with no location
 * to resolve it against).
 */
static int
images_source(
	struct page *page,
	const struct dom_element *element,
	const struct vm_string *src,
	struct wb_buffer *location,
	int *found)
{
	const struct dom_attribute *attribute;
	struct wb_buffer href;
	int error;

	/* The src attribute, when there is one and the page has a location. */
	*found = 0;
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, src);
	if (attribute == NULL || page->base == NULL)
		return 0;

	/* Its value. */
	wb_buffer_init(&href);
	error = vm_string_to_utf8(attribute->value, &href);
	if (error != 0) {
		wb_buffer_release(&href);
		return error;
	}

	/* The location it names; a source that is not a URL has none. */
	error = page_resolve_location(page->base, wb_buffer_string(&href), location);
	wb_buffer_release(&href);
	if (error == EINVAL)
		return 0;
	if (error != 0)
		return error;

	/* Succeeded: the location is written. */
	*found = 1;
	return 0;
}

/* Finds an image of the table by its location. */
static struct page_image *
images_find(
	const struct page *page,
	const char *location)
{
	struct page_image *image;
	size_t index;
	int differs;

	/* Each image of the table. */
	for (index = 0; index < page->images.count; index++) {
		image = wb_vector_at(&page->images, index);
		differs = strcmp(image->location, location);
		if (differs == 0)
			return image;
	}

	/* The location is not in the table. */
	return NULL;
}

/*
 * Loads an <img>'s source into the table, unless it is there already: its
 * bytes fetched and decoded, or the failure remembered.
 */
static int
images_load(
	struct page *page,
	const struct dom_element *element,
	const struct vm_string *src)
{
	struct page_image image;
	struct wb_buffer location;
	struct wb_buffer bytes;
	struct page_image *known;
	int found;
	int error;

	/* The source's location; an element without one loads nothing. */
	wb_buffer_init(&location);
	error = images_source(page, element, src, &location, &found);
	if (error != 0 || !found) {
		wb_buffer_release(&location);
		return error;
	}

	/* A location loaded before, well or not, is not fetched again. */
	known = images_find(page, wb_buffer_string(&location));
	if (known != NULL) {
		wb_buffer_release(&location);
		return 0;
	}

	/* The table's entry for the location. */
	memset(&image, 0, sizeof(image));
	image.location = strdup(wb_buffer_string(&location));
	wb_buffer_release(&location);
	if (image.location == NULL)
		return ENOMEM;

	/* The bytes, then the decoding; either failing marks the image failed. */
	wb_buffer_init(&bytes);
	error = page_fetch(page->base, image.location, &bytes, NULL);
	if (error == 0)
		error = img_decode(bytes.data, bytes.length, &image.bitmap);
	wb_buffer_release(&bytes);
	if (error != 0)
		image.failed = 1;

	/* The entry goes into the table. */
	error = wb_vector_push(&page->images, &image);
	if (error != 0) {
		free(image.location);
		img_bitmap_release(&image.bitmap);
		return error;
	}

	/* Succeeded: the image is in the table. */
	return 0;
}
