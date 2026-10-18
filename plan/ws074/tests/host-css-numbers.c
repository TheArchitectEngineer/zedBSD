/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Checks precise inline factors, committed notifications and traced source lifetime through real collection. */

#include "bind/internal.h"
#include "page/page.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

/* Count independent numeric and lifetime observations across the native fixture. */
static unsigned checks;

/* Preserve failed observations until the final result is printed. */
static unsigned failures;

/* Select a fixture notification error or nested commit without changing production document state. */
static int notification_error;

static void numbers_check(int condition, const char *name);
static int numbers_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int numbers_case(struct page *page);
static int numbers_hook(struct dom_document *document, struct dom_element *element, struct vm_string *name, struct vm_string *old_value);

/*
 * Runs real Page layout, native inline source lifecycle and collector observations.
 */
int
main(
	void)
{
	struct page *page;
	struct text_font_paths paths;
	static const unsigned char html[] = "<!doctype html><html><body></body></html>";
	int status;
	int printed;

	/* Checked-in fonts supply the ordinary Page layout environment. */
	paths.sans = "userland/desktop/fonts/Mahora-Regular.ttf";
	paths.mono = "userland/desktop/fonts/JetBrainsMono-Regular.ttf";
	paths.fallback = "userland/desktop/fonts/DroidSansFallbackFull.ttf";

	/* Allocates the real Page before lending it font paths and viewport state. */
	status = page_create(&page, __builtin_frame_address(0));
	if (status != 0)
		return 2;

	/* Installs ordinary layout inputs on the completed Page. */
	page_set_fonts(page, &paths);
	page_set_viewport(page, 800, 600);

	/* Load a genuine primary Page before constructing the binding-owned child. */
	status = page_load_html(page, html, sizeof(html) - 1U);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Exercises source models and collector ownership against the actual Page. */
	status = numbers_case(page);
	if (status != 0) {
		vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
		page_destroy(page);
		return 2;
	}

	/* Restores ordinary stack scanning before tearing down the complete Page. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);

	/* Publish complete observations after every production resource was released. */
	printed = printf("native CSS numbers: %u/%u passed\n", checks - failures, checks);
	if (printed < 0)
		return 2;

	/* Any failed independent source or lifetime observation rejects this fixture. */
	if (failures != 0)
		return 1;

	/* Succeeded: actual layout, source ownership and collector lifetime were verified. */
	return 0;
}

/* Records an independent layout or lifetime observation. */
static void
numbers_check(
	int condition,
	const char *name)
{
	int printed;

	/* Retain all failures while later independent observations still run. */
	checks++;
	if (!condition) {
		failures++;
		printed = fprintf(stderr, "FAIL %s\n", name);
		if (printed < 0)
			failures++;
	}

	/* Succeeded: the observation remains in the final result. */
	return;
}

/* Executes ordinary scripts to construct production managed child contexts. */
static int
numbers_script(
	struct vm_realm *realm,
	const char *source,
	vm_value *answer)
{
	struct wb_units units;
	struct js_syntax_error syntax;
	int status;

	/* Convert fixture input to the interpreter's ordinary source representation. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Execute genuine DOM operations without a production test switch. */
	status = js_run_script(realm, units.data, units.length, 0, answer, &syntax);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Releases source storage after ordinary script execution has finished. */
	wb_units_release(&units);

	/* Succeeded: the script supplied actual native state. */
	return 0;
}

/* An allocating committed notification can fail or perform one later CSSOM write. */
static int
numbers_hook(
	struct dom_document *document,
	struct dom_element *element,
	struct vm_string *name,
	struct vm_string *old_value)
{
	struct vm_string *source;
	struct vm_string *visible;
	struct vm_cell *root;
	int status;

	UNUSED_PARAMETER(name);
	UNUSED_PARAMETER(old_value);

	/* An actual collection observes production commit roots while the hook borrows the element. */
	numbers_check(element->inline_style_updating == 1, "notification sees updating flag");
	vm_heap_collect(document->heap);
	numbers_check(element->inline_style_source != NULL, "committed source survives allocating notification");
	if (notification_error)
		return EACCES;

	/* Disable the fixture hook before the nested ordinary CSSOM commit. */
	document->attribute_changed = NULL;
	source = vm_string_from_utf8(document->heap, "flex-grow: 0.3333333333333333;", 30U);
	if (source == NULL)
		return ENOMEM;
	root = &source->cell;
	status = vm_heap_add_root(document->heap, &root);
	if (status != 0)
		return status;
	visible = vm_string_from_utf8(document->heap, "flex-grow: 0.333333;", 20U);
	if (visible == NULL) {
		vm_heap_remove_root(document->heap, &root);
		return ENOMEM;
	}

	/* The inner source must remain authoritative after the outer commit returns. */
	status = dom_element_set_style_attribute(element, source, visible);
	vm_heap_remove_root(document->heap, &root);
	if (status != 0)
		return status;

	/* Succeeded: the inner mutation has published a later precise source. */
	return 0;
}

/* Checks actual native factors and source ownership; no rounded getter stands in for the stored float. */
static int
numbers_case(
	struct page *page)
{
	static const struct vm_cell_type pressure_type = {"numbers-pressure", NULL, NULL};
	/* Tiny declared doubles and overflow retain property-specific native float conversion. */
	static const char *const inputs[] = {"e.style.flexGrow='1e-40';true", "e.style.flexGrow='1e-100';true", "e.style.flexGrow='1e1000';true", "e.style.flexGrow='1.234565';true"};
	static const float factors[] = {(float)1e-40, 0.0f, FLT_MAX, (float)1.234565};
	struct dom_node *node;
	struct dom_element *element;
	struct dom_document *document;
	struct dom_attribute *attribute;
	struct css_style style;
	struct vm_string *source;
	struct vm_string *visible;
	struct vm_string *key;
	struct vm_cell *roots[2];
	struct vm_cell *pressure;
	struct vm_cell *found;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	int (*hook)(struct dom_document *, struct dom_element *, struct vm_string *, struct vm_string *);
	vm_value answer;
	uintptr_t retired;
	int status;
	int truth;
	int same;
	unsigned index;

	/* The real script setter publishes a high precision factor and canonical DOM text. */
	status = numbers_script(page->realm, "var e=document.createElement('div');document.body.appendChild(e);e.style.flexGrow='0.123456789';e", &answer);
	if (status != 0)
		return status;
	node = bind_node_of(answer);
	if (node == NULL ||
	    node->type != DOM_ELEMENT)
		return EINVAL;
	element = (struct dom_element *)node;
	document = element->node.document;
	hook = document->attribute_changed;

	/* Used float conversion reads the original double rather than the rounded public string. */
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, element, NULL, &style);
	if (status != 0)
		return status;
	numbers_check(style.flex_grow == (float)0.123456789, "initial underlying float remains precise");
	numbers_check(style.flex_grow != (float)0.123457, "underlying float differs from canonical public text");
	retired = (uintptr_t)element->inline_style_source;

	/* Disable conservative scanning so only actual production edges retain the original source. */
	vm_heap_set_stack_base(page->heap, NULL);
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, retired);
	numbers_check(found != NULL, "DOM element traces exact source during actual GC");
	status = numbers_script(page->realm, "e.style.flexGrow==='0.123457' && e.style.cssText==='flex-grow: 0.123457;' && e.getAttribute('style')==='flex-grow: 0.123457;'", &answer);
	if (status != 0)
		return status;
	truth = vm_to_boolean(answer);
	numbers_check(truth, "all public views agree after collection");
	status = numbers_script(page->realm, "e.setAttribute('title','x');e.style.color='red';true", &answer);
	if (status != 0)
		return status;
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, element, NULL, &style);
	if (status != 0)
		return status;
	numbers_check(style.flex_grow == (float)0.123456789, "unrelated DOM and CSSOM mutations preserve precise float");

	/* The clone reparses the canonical serialized attribute, as an external attribute input does. */
	status = numbers_script(page->realm, "var clone=e.cloneNode(false);clone", &answer);
	if (status != 0)
		return status;
	node = bind_node_of(answer);
	if (node == NULL)
		return EINVAL;
	numbers_check(((struct dom_element *)node)->inline_style_source == NULL, "clone owns no stale source cache");
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, (struct dom_element *)node, NULL, &style);
	if (status != 0)
		return status;
	numbers_check(style.flex_grow == (float)0.123457, "clone uses canonical attribute as its own declared input");

	/* Even assignment of the identical serialized string invalidates the cached source. */
	retired = (uintptr_t)element->inline_style_source;
	key = vm_atom_from_ascii(page->heap, "style");
	if (key == NULL)
		return ENOMEM;
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, key);
	if (attribute == NULL)
		return EINVAL;
	status = dom_element_set_attribute(element, key, attribute->value);
	if (status != 0)
		return status;
	numbers_check(element->inline_style_source == NULL, "same-string external assignment invalidates source");
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, element, NULL, &style);
	if (status != 0)
		return status;
	numbers_check(style.flex_grow == (float)0.123457, "external assignment reparses rounded input");
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, retired);
	numbers_check(found == NULL, "retired source is reclaimed while element remains alive");

	/* Compute fresh native values after representative scalar conversions, independently of getter strings. */
	for (index = 0; index < sizeof(inputs) / sizeof(inputs[0]); index++) {
		status = numbers_script(page->realm, inputs[index], &answer);
		if (status != 0)
			return status;
		css_engine_forget_styles(page->css);
		status = css_engine_compute(page->css, element, NULL, &style);
		if (status != 0)
			return status;
		numbers_check(style.flex_grow == factors[index], "tiny, underflow, overflow and tie cases use original numeric conversion");
	}

	/* External removal invalidates the source even when a standalone owner installs no mutation hook. */
	document->attribute_changed = NULL;
	retired = (uintptr_t)element->inline_style_source;
	status = dom_element_remove_attribute(element, key);
	document->attribute_changed = hook;
	if (status != 0)
		return status;
	numbers_check(element->inline_style_source == NULL, "ownerless external removal clears exact source");
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, retired);
	numbers_check(found == NULL, "removed source is actually reclaimed");

	/* Create fresh bare caller strings before approaching the unchanged production collection threshold. */
	source = vm_string_from_utf8(page->heap, "flex-grow: 1.234565;", 20U);
	if (source == NULL)
		return ENOMEM;
	roots[0] = &source->cell;
	roots[1] = NULL;
	status = vm_heap_add_root(page->heap, &roots[0]);
	if (status != 0)
		return status;
	status = vm_heap_add_root(page->heap, &roots[1]);
	if (status != 0) {
		vm_heap_remove_root(page->heap, &roots[0]);
		return status;
	}

	/* Protect the source while allocating the separate public string. */
	visible = vm_string_from_utf8(page->heap, "flex-grow: 1.23456;", 19U);
	if (visible == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Both fresh strings survive the threshold-pressure allocation. */
	roots[1] = &visible->cell;
	pressure = vm_heap_alloc(page->heap, &pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL) {
		status = ENOMEM;
		goto cleanup;
	}

	/* Only the production commit helper protects the bare arguments during its allocating notification. */
	roots[0] = NULL;
	roots[1] = NULL;
	notification_error = 1;
	document->attribute_changed = numbers_hook;
	vm_heap_stats(page->heap, &before);
	status = dom_element_set_style_attribute(element, source, visible);
	vm_heap_stats(page->heap, &after);
	numbers_check(status == EACCES, "committed notification error is propagated");
	numbers_check(after.collections > before.collections, "bare direct commit crosses actual collection");
	numbers_check(element->inline_style_source == source && element->inline_style_updating == 0, "notification failure preserves committed source and restores flag");
	attribute = dom_element_find_attribute(element, DOM_NS_NONE, key);
	numbers_check(attribute != NULL && attribute->value == visible, "notification failure preserves committed serialized text");
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, element, NULL, &style);
	if (status != 0)
		goto cleanup;
	numbers_check(style.flex_grow == (float)1.234565, "failed notification does not round committed native float");

	/* Reentry publishes a later source, which outer restoration must never overwrite. */
	notification_error = 0;
	document->attribute_changed = numbers_hook;
	status = dom_element_set_style_attribute(element, source, visible);
	if (status != 0)
		goto cleanup;
	same = vm_string_equal_ascii(element->inline_style_source, "flex-grow: 0.3333333333333333;");
	numbers_check(same && element->inline_style_updating == 0, "nested CSSOM source survives outer return");
	/* A direct CSS engine caller begins a new query after native or script mutations. */
	css_engine_forget_styles(page->css);
	status = css_engine_compute(page->css, element, NULL, &style);
	if (status != 0)
		goto cleanup;
	numbers_check(style.flex_grow == (float)0.3333333333333333, "nested used factor reads later precise source");

cleanup:
	/* Restore the real document's owner hook and fixture mode even on a native failure. */
	document->attribute_changed = hook;
	notification_error = 0;
	vm_heap_remove_root(page->heap, &roots[1]);
	vm_heap_remove_root(page->heap, &roots[0]);
	if (status != 0)
		return status;

	/* Succeeded: observations compare true used floats, actual collection and committed reentrant state. */
	return 0;
}
