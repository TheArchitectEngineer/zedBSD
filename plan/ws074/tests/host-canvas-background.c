/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* A zero-height HTML root still positions its propagated body image on canvas. */

#include "page/page.h"

#include <stdio.h>

int
main(
	void)
{
	struct text_font_paths fonts;
	struct page *page;
	const struct paint_item *item;
	size_t index;
	int error;
	int found;
	int passed;

	/* The checked-in fixture has only an absolutely positioned child. */
	fonts.sans = "userland/desktop/fonts/Mahora-Regular.ttf";
	fonts.mono = "userland/desktop/fonts/JetBrainsMono-Regular.ttf";
	fonts.fallback = "userland/desktop/fonts/DroidSansFallbackFull.ttf";
	error = page_create(&page, __builtin_frame_address(0));
	if (error != 0)
		return 2;
	page_set_fonts(page, &fonts);
	error = page_load_file(page, "plan/ws074/tests/pages/canvas-background.html");
	if (error != 0)
		fprintf(stderr, "page_load_file: %d\n", error);
	if (error == 0)
		error = page_open_fonts(page, &fonts);
	if (error != 0)
		fprintf(stderr, "page_open_fonts: %d\n", error);
	if (error == 0)
		error = page_layout(page, 800, 600);
	if (error != 0)
		fprintf(stderr, "page_layout: %d\n", error);
	if (error == 0)
		error = page_paint(page);
	if (error != 0) {
		page_destroy(page);
		return 2;
	}

	/* The image must paint even though the root has no content height. */
	passed = page->layout.root != NULL && page->layout.root->height == 0;
	passed = passed && page->paint.canvas_color == 0xffc0c0c0U;
	found = 0;
	for (index = 0; index < page->paint.items.count; index++) {
		item = wb_vector_at(&page->paint.items, index);
		if (item->kind == PAINT_IMAGE && item->x == 0 && item->y == 0 &&
		    item->width == 2 * LAYOUT_UNIT && item->height == 2 * LAYOUT_UNIT)
			found = 1;
	}
	passed = passed && found;
	page_destroy(page);

	/* This catches the former positioning-area height guard. */
	if (!passed) {
		fprintf(stderr, "FAIL zero-height canvas background\n");
		return 1;
	}
	printf("zero-height canvas background: pass\n");
	return 0;
}
