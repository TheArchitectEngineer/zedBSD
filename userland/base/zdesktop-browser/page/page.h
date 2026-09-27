/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A page: one document loaded into a tab, with its heap, its DOM, its
 * style sheets and, as the engine grows, its layout and display list.
 *
 * The headless modes and the window both load pages through here, so a
 * page looks the same whether it is drawn on the host for a test or in a
 * zdesktop window.
 */

#ifndef ZDESKTOP_BROWSER_PAGE_H
#define ZDESKTOP_BROWSER_PAGE_H

#include "css/css.h"
#include "dom/dom.h"
#include "html/html.h"
#include "layout/layout.h"
#include "paint/paint.h"
#include "text/text.h"

/*
 * A loaded page.
 *
 * The page owns its heap; the document is a root of it for as long as the
 * page lives.
 */
struct page {
	struct vm_heap *heap;
	struct dom_document *document;
	struct css_engine *css;
	struct text_system text;
	int text_open;
	struct layout_tree layout;
	int laid_out;
	struct paint_list paint;
	int painted;
};

/* Pages (page.c). */
int page_create(struct page **page, const void *stack_base);
void page_destroy(struct page *page);
int page_load_html(struct page *page, const unsigned char *bytes, size_t length);
int page_load_file(struct page *page, const char *path);
int page_dump_dom(const struct page *page, struct wb_buffer *out);
int page_dump_style(struct page *page, struct wb_buffer *out);
int page_open_fonts(struct page *page, const struct text_font_paths *paths);
int page_layout(struct page *page, int width, int height);
int page_paint(struct page *page);
int page_title(const struct page *page, struct wb_buffer *out);

#endif
