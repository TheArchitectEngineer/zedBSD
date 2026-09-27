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

#ifndef KEILAND_BROWSER_PAGE_H
#define KEILAND_BROWSER_PAGE_H

#include "bind/bind.h"
#include "css/css.h"
#include "dom/dom.h"
#include "html/html.h"
#include "layout/layout.h"
#include "paint/paint.h"
#include "text/text.h"

/*
 * Where a page's console lines go: a function of the embedder's with its
 * context (NULL writes them to standard error).
 */
typedef void (*page_console)(void *context, int level, const char *text, size_t length);

/*
 * A loaded page.
 *
 * The page owns its heap; the document is a root of it for as long as the
 * page lives.  Its realm's global object is the document's window, which
 * runs the page's scripts.  base is the page's location: the absolute path
 * of its file, or its URL (what relative URLs resolve against); now the
 * page's clock in milliseconds, and the two
 * generations the document's generation when the style sheets were
 * gathered and when the page was last laid out.
 */
struct page {
	struct vm_heap *heap;
	struct dom_document *document;
	struct vm_realm *realm;
	struct bind_window *window;
	struct css_engine *css;
	struct text_system text;
	int text_open;
	struct layout_tree layout;
	int laid_out;
	struct paint_list paint;
	int painted;
	char *base;
	double now;
	uint32_t styled_generation;
	uint32_t laid_out_generation;
	page_console console;
	void *console_context;
};

/* Pages (page.c). */
int page_create(struct page **page, const void *stack_base);
void page_destroy(struct page *page);
int page_load_html(struct page *page, const unsigned char *bytes, size_t length);
int page_load_file(struct page *page, const char *path);
int page_load_location(struct page *page, const char *location);
int page_dump_dom(const struct page *page, struct wb_buffer *out);
int page_dump_style(struct page *page, struct wb_buffer *out);
int page_open_fonts(struct page *page, const struct text_font_paths *paths);
int page_layout(struct page *page, int width, int height);
int page_paint(struct page *page);
int page_title(const struct page *page, struct wb_buffer *out);

/* Scripts, events and time (script.c). */
int page_start_scripts(struct page *page);
void page_run_script_element(void *context, struct dom_element *script);
int page_fire_load(struct page *page);
int page_set_time(struct page *page, double now);
int page_next_timer(const struct page *page, double *due);
int page_settle(struct page *page, double budget);
int page_click(struct page *page, int x, int y, int client_x, int client_y, int *canceled);
int page_needs_layout(const struct page *page);

/* Links (link.c). */
int page_link_at(struct page *page, int x, int y, struct wb_buffer *href, int *found);
int page_resolve_file(const char *base, const char *href, struct wb_buffer *out);
int page_resolve_location(const char *base, const char *href, struct wb_buffer *out);
int page_fetch(const char *base, const char *href, struct wb_buffer *bytes, struct wb_buffer *final_url);

#endif
