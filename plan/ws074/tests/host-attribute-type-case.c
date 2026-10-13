/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* HTML type values use host-language matching unless a selector flag overrides it. */

#include "page/page.h"

#include <stdio.h>
#include <string.h>

/*
 * Checks HTML type value matching, explicit flags, other attributes and SVG.
 *
 * The real cascade and layout expose both the hidden input and selector cases.
 */
int
main(
	void)
{
	static const unsigned char html[] =
	    "<!doctype html><html><head><style>"
	    "#implicit[type=mixed],#insensitive[type=mixed i],#token[type~=mixed],#prefix[type^=mix],"
	    "#suffix[type$=xed],#dash[type|=mixed],#substring[type*=ixe]{display:none}"
	    "#sensitive[type=mixed s],#other[data-kind=mixed],#svg[type=mixed]{display:none}"
	    "#svg{display:block}"
	    "</style></head><body><form><input type=HIDDEN></form><p>AFTER</p>"
	    "<div id=implicit type=MIXED>IMPLICIT_HIDE</div>"
	    "<div id=insensitive type=MIXED>INSENSITIVE_HIDE</div>"
	    "<div id=token type=MIXED>TOKEN_HIDE</div>"
	    "<div id=prefix type=MIXED>PREFIX_HIDE</div>"
	    "<div id=suffix type=MIXED>SUFFIX_HIDE</div>"
	    "<div id=dash type=MIXED>DASH_HIDE</div>"
	    "<div id=substring type=MIXED>SUBSTRING_HIDE</div>"
	    "<div id=sensitive type=MIXED>SENSITIVE_VISIBLE</div>"
	    "<div id=other data-kind=MIXED>OTHER_VISIBLE</div>"
	    "<svg id=svg type=MIXED><text>SVG_VISIBLE</text></svg>"
	    "</body></html>";
	struct text_font_paths fonts;
	struct wb_buffer dom;
	struct wb_buffer layout;
	struct page *page;
	int error;
	int passed;

	fonts.sans = "userland/desktop/fonts/Mahora-Regular.ttf";
	fonts.mono = "userland/desktop/fonts/JetBrainsMono-Regular.ttf";
	fonts.fallback = "userland/desktop/fonts/DroidSansFallbackFull.ttf";
	wb_buffer_init(&dom);
	wb_buffer_init(&layout);
	error = page_create(&page, __builtin_frame_address(0));
	if (error != 0)
		return 2;
	page_set_fonts(page, &fonts);
	error = page_load_html(page, html, sizeof(html) - 1U);
	if (error == 0)
		error = page_dump_dom(page, &dom);
	if (error == 0)
		error = page_open_fonts(page, &fonts);
	if (error == 0)
		error = page_layout(page, 800, 600);
	if (error == 0)
		error = layout_dump(&page->layout, &layout);
	if (error != 0) {
		fprintf(stderr, "attribute type setup: %d\n", error);
		page_destroy(page);
		wb_buffer_release(&dom);
		wb_buffer_release(&layout);
		return 2;
	}

	passed = strstr(wb_buffer_string(&dom), "type=\"HIDDEN\"") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "replaced 0.00") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "IMPLICIT_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "INSENSITIVE_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "TOKEN_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "PREFIX_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "SUFFIX_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "DASH_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "SUBSTRING_HIDE") == NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "SENSITIVE_VISIBLE") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "OTHER_VISIBLE") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "<svg>") != NULL;
	passed = passed && strstr(wb_buffer_string(&layout), "AFTER") != NULL;
	if (!passed)
		fprintf(stderr, "FAIL HTML type selector case:\n%s\n", wb_buffer_string(&layout));
	page_destroy(page);
	wb_buffer_release(&dom);
	wb_buffer_release(&layout);
	if (!passed)
		return 1;
	printf("HTML type selector case: pass\n");
	return 0;
}
