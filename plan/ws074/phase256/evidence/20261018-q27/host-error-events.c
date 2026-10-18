/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises actual ErrorEvent ownership and reentrant collection without conservative stack retention. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Accumulates independent ownership observations until the fixture completes. */
static unsigned checks;

/* Keeps failures visible while later independent collector cases still run. */
static unsigned failures;

static int error_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int error_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int error_cases(struct page *page);
static void error_check(int condition, const char *name);
static int error_retired_event(struct page *page);

/* Runs ordinary Page initialization before disabling conservative stack retention for the actual cases. */
int
main(
	void)
{
	struct page *page;
	static const unsigned char html[] = "<!doctype html><html><body></body></html>";
	int status;
	int printed;

	/* Use the production Page, VM, interfaces and collector. */
	status = page_create(&page, __builtin_frame_address(0));
	if (status != 0)
		return 2;

	/* Build the normal primary realm before exposing a fixture-only collection callback. */
	status = page_load_html(page, html, sizeof(html) - 1U);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* A test-native callback reaches the existing collector without any production switch. */
	status = js_builtin_method(page->realm, page->realm->global, "collectErrorFixture", 0, error_collect);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Heap and VM roots must carry all native event ownership during the selected cases. */
	vm_heap_set_stack_base(page->heap, NULL);
	status = error_cases(page);

	/* Restore ordinary scanning before releasing the complete production Page. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);

	/* Internal execution failure is distinct from an assertion observation. */
	if (status != 0)
		return 2;

	/* Emit one compact aggregate after all ownership checks have finished. */
	printed = printf("native error events: %u/%u passed\n", checks - failures, checks);
	if (printed < 0)
		return 2;

	/* Any failed root or lifetime observation rejects the native implementation. */
	if (failures != 0)
		return 1;

	/* Succeeded: actual event, payload and retired-frame ownership survived collection. */
	return 0;
}

/* Exposes an ordinary native fixture callback that collects at the exact point requested by a getter. */
static int
error_collect(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The production collector sees no conservative C stack roots in this fixture. */
	vm_heap_collect(realm->heap);
	*result = VM_VALUE_UNDEFINED;

	/* Succeeded: execution resumes in the same real conversion callback. */
	return 0;
}

/* Executes ordinary source through the production parser, compiler and interpreter. */
static int
error_script(
	struct vm_realm *realm,
	const char *source,
	vm_value *answer)
{
	struct wb_units units;
	struct js_syntax_error syntax;
	int status;

	/* Convert fixture source without installing substitute product behavior. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Run the real binding APIs and their reentrant native calls. */
	status = js_run_script(realm, units.data, units.length, 0, answer, &syntax);
	wb_units_release(&units);

	/* A thrown script cannot count as a successful ownership observation. */
	if (status != 0)
		return status;

	/* Succeeded: the completion value belongs to the real native execution. */
	return 0;
}

/* Records a named independent observation without hiding later failures. */
static void
error_check(
	int condition,
	const char *name)
{
	int printed;

	/* Advance the total once for each independent invariant checked by the fixture. */
	checks++;

	/* A successful observation needs no failure output. */
	if (condition)
		return;

	/* Preserve a failed invariant while allowing the remaining cases to run. */
	failures++;
	printed = fprintf(stderr, "FAIL: %s\n", name);
	if (printed < 0)
		failures++;

	/* Succeeded: the failed observation remains visible in the final aggregate. */
	return;
}

/* Verifies constructor conversion, actual handler delivery and reentrant collector ownership. */
static int
error_cases(
	struct page *page)
{
	vm_value answer;
	int status;

	/* Newborn payload/string fields survive every later getter collection. */
	status = error_script(
		page->realm,
		"var nativeError=new ErrorEvent('error',{get colno(){collectErrorFixture();return 7;},get error(){collectErrorFixture();return {kept:'newborn'};},get filename(){collectErrorFixture();return {toString:function(){collectErrorFixture();return 'file\\ud800';}};},get lineno(){collectErrorFixture();return {valueOf:function(){collectErrorFixture();return 13;}};},get message(){collectErrorFixture();return {toString:function(){collectErrorFixture();return 'message';}};}});collectErrorFixture();nativeError.error.kept==='newborn'&&nativeError.filename==='file\\ufffd'&&nativeError.message==='message'&&nativeError.colno===7&&nativeError.lineno===13",
		&answer);
	if (status != 0)
		return status;
	error_check(answer == VM_VALUE_TRUE, "newborn payload/string fields survive every later getter collection");

	/* Getter conversion throws the original rooted exception. */
	status = error_script(
		page->realm,
		"var sentinel={kept:'original'},caught=false;try{new ErrorEvent('error',{get error(){return {orphan:true};},get filename(){collectErrorFixture();throw sentinel;}});}catch(e){caught=e===sentinel;}collectErrorFixture();caught",
		&answer);
	if (status != 0)
		return status;
	error_check(answer == VM_VALUE_TRUE, "getter conversion throws the original rooted exception");

	/* Event dispatch retains handler arguments and original error during collection. */
	status = error_script(
		page->realm,
		"var nativeGood=false;onerror=function(m,f,l,c,e){collectErrorFixture();nativeGood=arguments.length===5&&m==='message'&&f==='file\\ufffd'&&l===13&&c===7&&e===nativeError.error;return true;};var nativeCancelable=new ErrorEvent('error',{message:nativeError.message,filename:nativeError.filename,lineno:13,colno:7,error:nativeError.error,cancelable:true});var canceled=!dispatchEvent(nativeCancelable);onerror=null;collectErrorFixture();nativeGood&&canceled&&nativeCancelable.defaultPrevented",
		&answer);
	if (status != 0)
		return status;
	error_check(answer == VM_VALUE_TRUE, "event dispatch retains handler arguments and original error during collection");

	/* Listener snapshot stays rooted through removal, addition and collection. */
	status = error_script(
		page->realm,
		"var nativeOrder=[];function later(){collectErrorFixture();nativeOrder.push('later');}function first(e){collectErrorFixture();nativeOrder.push(e.error.kept);removeEventListener('error',first);addEventListener('error',later);}addEventListener('error',first);dispatchEvent(nativeError);dispatchEvent(nativeError);removeEventListener('error',later);nativeOrder.join(',')==='newborn,later'",
		&answer);
	if (status != 0)
		return status;
	error_check(answer == VM_VALUE_TRUE, "listener snapshot stays rooted through removal, addition and collection");

	/* Subclass prototype remains reachable through conversion callbacks. */
	status = error_script(
		page->realm,
		"class NativeDerived extends ErrorEvent{};var derived=new NativeDerived('x',{get message(){collectErrorFixture();return 'derived';}});collectErrorFixture();derived instanceof NativeDerived&&derived.message==='derived'",
		&answer);
	if (status != 0)
		return status;
	error_check(answer == VM_VALUE_TRUE, "subclass prototype remains reachable through conversion callbacks");

	/* Retired child ownership is checked independently of primary Window roots. */
	status = error_retired_event(page);
	if (status != 0)
		return status;

	/* Succeeded: each independent native invariant remains visible in the aggregate. */
	return 0;
}

/* Retains a constructed child event as the sole explicit root, then proves the released graph is collectible. */
static int
error_retired_event(
	struct page *page)
{
	struct dom_node *node;
	struct dom_document *document;
	struct bind_window *owner;
	struct vm_cell *root;
	struct vm_cell *found;
	vm_value answer;
	uintptr_t document_address;
	uintptr_t realm_address;
	int status;

	/* Construct a real ErrorEvent in a collectible child realm. */
	status = error_script(
		page->realm,
		"var errorFrame=document.createElement('iframe');document.body.appendChild(errorFrame);"
		"var errorDocument=errorFrame.contentDocument;"
		"var savedChildError=new errorFrame.contentWindow.ErrorEvent('error',{message:'child-message',filename:'child-file',error:{kept:'child-payload'}});errorDocument",
		&answer);
	if (status != 0)
		return status;

	/* Resolve the real Document and managed Window without creating an artificial fixture owner. */
	node = bind_node_of(answer);
	if (node == NULL)
		return 2;

	/* The selected script must have returned its actual child Document. */
	if (node->type != DOM_DOCUMENT)
		return 2;

	/* The active Document supplies the genuine relevant Window for its native constructor. */
	document = (struct dom_document *)node;
	owner = document->view;
	if (owner == NULL)
		return 2;

	/* Capture integer addresses that cannot act as conservative roots. */
	document_address = (uintptr_t)&document->node.cell;
	realm_address = (uintptr_t)&owner->realm->cell;

	/* Obtain the actual child wrapper without manufacturing a fixture-side event. */
	status = error_script(page->realm, "savedChildError", &answer);
	if (status != 0)
		return status;

	/* Construction must have created a genuine object before adding its sole retaining root. */
	if (answer == VM_VALUE_NULL || answer == VM_VALUE_UNDEFINED)
		return 2;

	/* The saved event alone now retains any required child realm and error payload graph. */
	root = vm_value_as_cell(answer);
	status = vm_heap_add_root(page->heap, &root);
	if (status != 0)
		return status;

	/* Retire the actual iframe and discard every script alias to its error, Document and saved event. */
	status = error_script(
		page->realm,
		"errorFrame.remove();errorFrame=null;errorDocument=null;savedChildError=null;true",
		&answer);
	if (status != 0)
		goto cleanup;

	/* With native stack scanning disabled, only the explicit saved wrapper can retain this retired graph. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	error_check(found != NULL, "a constructed ErrorEvent alone retains its actual retired child ownership graph");

	/* The native payload must remain usable after another actual collection. */
	status = js_builtin_value(page->realm, page->realm->global, "savedErrorEvent", vm_value_cell(root), VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;

	/* Read actual retained payload through branded getters after source retirement and collection. */
	status = error_script(
		page->realm,
		"collectErrorFixture();var keptGood=savedErrorEvent.message==='child-message'&&savedErrorEvent.filename==='child-file'&&savedErrorEvent.error.kept==='child-payload';"
		"savedErrorEvent=null;keptGood",
		&answer);
	if (status != 0)
		goto cleanup;

	/* The saved event survived source retirement, alias removal and reentrant collection. */
	error_check(answer == VM_VALUE_TRUE, "retired child ErrorEvent retains genuine retained error payload through GC");

	/* Release the sole explicit wrapper root before checking actual graph reclamation. */
	root = NULL;
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	error_check(found == NULL, "releasing the saved event permits its retired child Document to be collected");

	/* Retired realm ownership must also disappear rather than leaking through native registration cycles. */
	found = vm_heap_find_cell(page->heap, realm_address);
	error_check(found == NULL, "releasing the saved event permits its managed child realm to be collected");

cleanup:
	/* Remove the root registry slot on both successful and failed lifecycle verification. */
	vm_heap_remove_root(page->heap, &root);

	/* A failed script or native property operation remains an execution failure. */
	if (status != 0)
		return status;

	/* Succeeded: a real saved event is safe while retained and collectible after release. */
	return 0;
}
