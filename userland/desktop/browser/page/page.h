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
#include "image/image.h"
#include "layout/layout.h"
#include "paint/paint.h"
#include "text/text.h"

/*
 * Where a page's console lines go: a function of the embedder's with its
 * context (NULL writes them to standard error).
 */
typedef void (*page_console)(void *context, int level, const char *text, size_t length);

/* The network's loader and its requests (net/net.h), which the embedder runs through page_net_*. */
struct net_loader;
struct net_request;
struct pollfd;

/* A request's callback, as the loader calls it (net_request_done). */
typedef void (*page_request_done)(void *context, struct net_request *request);

/*
 * A loaded page.
 *
 * The page owns its heap; the document is a root of it for as long as the
 * page lives.  Its realm's global object is the document's window, which
 * runs the page's scripts.  base is the page's location: the absolute path
 * of its file, or its URL (what relative URLs resolve against); now the
 * page's clock in milliseconds, and the two
 * generations the document's generation when the style sheets were
 * gathered and when the page was last laid out.  images is the table of
 * the page's decoded images (page/images.c), by location; loader (the
 * embedder's, or NULL) fetches http and https images without blocking,
 * and images_generation counts the images that arrived, which
 * laid_out_images is compared with to lay the page out again.
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
	struct wb_vector images;
	struct net_loader *loader;
	uint32_t images_generation;
	uint32_t laid_out_images;
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
void page_images_init(struct page *page);
void page_set_loader(struct page *page, struct net_loader *loader);
int page_load_bytes(struct page *page, const unsigned char *bytes, size_t length, const char *location);
int page_net_create(struct net_loader **loader);
void page_net_destroy(struct net_loader *loader);
size_t page_net_poll_fds(const struct net_loader *loader, struct pollfd *fds, size_t capacity);
int page_net_timeout(const struct net_loader *loader);
void page_net_process(struct net_loader *loader, const struct pollfd *fds, size_t count);
int page_net_is_remote(const char *location);
int page_net_fetch(struct net_loader *loader, const char *location, page_request_done done, void *context, struct net_request **request);
void page_net_cancel(struct net_request *request);
int page_net_result(const struct net_request *request, const unsigned char **bytes, size_t *length, const char **url);
int page_load_images(struct page *page);
void page_set_viewport(struct page *page, int width, int height);
void page_set_console(struct page *page, page_console console, void *context);
const char *page_failure_reason(void);
const struct img_bitmap *page_image_of(void *context, const struct dom_element *element);
const struct img_bitmap *page_image_by_url(void *context, const struct vm_string *url);
void page_images_release(struct page *page);
int page_fetch(const char *base, const char *href, struct wb_buffer *bytes, struct wb_buffer *final_url);

#endif
