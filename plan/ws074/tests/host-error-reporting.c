/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises actual error-report ownership and reentrant collection without conservative stack retention. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Accumulates independent ownership observations until the fixture completes. */
static unsigned checks;

/* Keeps failures visible while later independent collector cases still run. */
static unsigned failures;

/* Watches a real child Document by integer address without acting as a collector root. */
static uintptr_t watched_document;

/* Counts collections that prove the reporting native frame still retains its retired owner. */
static unsigned watched_alive;

static int report_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int report_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int report_cases(struct page *page);
static void report_check(int condition, const char *name);
static int report_source_lifetime(struct page *page);
static int report_retired_conversion(struct page *page);
static int report_retired_checkpoint(struct page *page);

/*
 * Runs ordinary Page initialization before disabling conservative stack retention.
 *
 * The fixture observes actual collector ownership rather than conservative C stack retention.
 */
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
	status = js_builtin_method(page->realm, page->realm->global, "collectReportFixture", 0, report_collect);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Heap and VM roots must carry all native event ownership during the selected cases. */
	vm_heap_set_stack_base(page->heap, NULL);
	status = report_cases(page);

	/* Restore ordinary scanning before releasing the complete production Page. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);

	/* Internal execution failure is distinct from an assertion observation. */
	if (status != 0)
		return 2;

	/* Emit one compact aggregate after all ownership checks have finished. */
	printed = printf("native error reporting: %u/%u passed\n", checks - failures, checks);
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
report_collect(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_cell *found;

	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* The production collector sees no conservative C stack roots in this fixture. */
	vm_heap_collect(realm->heap);

	/* A watch is an observation only; its integer address cannot retain a retired graph. */
	if (watched_document != 0U) {
		found = vm_heap_find_cell(realm->heap, watched_document);
		if (found != NULL)
			watched_alive++;
	}

	/* Collection returns no substitute API value to the actual script. */
	*result = VM_VALUE_UNDEFINED;

	/* Succeeded: execution resumes in the same real conversion callback. */
	return 0;
}

/* Executes ordinary source through the production parser, compiler and interpreter. */
static int
report_script(
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
report_check(
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

/* Verifies original exception roots, conservative disclosure, reentry and real source lifetime. */
static int
report_cases(
	struct page *page)
{
	vm_value answer;
	int status;

	/* Create an actual known inline timer whose pending exception is its only temporary owner. */
	status = report_script(
		page->realm,
		"var inlineReportScript=document.createElement('script');inlineReportScript.textContent=\"var reportCalls=0,keptReport=null;addEventListener('error',function(event){collectReportFixture();reportCalls++;keptReport=event;event.preventDefault();});setTimeout(function(){throw {payload:'original-owned',toString:function(){collectReportFixture();return 'owned-message';}};},0);\";document.body.appendChild(inlineReportScript);true",
		&answer);
	if (status != 0)
		return status;

	/* Report through the ordinary native timer path with conservative stack scanning disabled. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Read genuine original payload and defining metadata after conversion and listener collection. */
	status = report_script(
		page->realm,
		"collectReportFixture();reportCalls===1&&keptReport.error.payload==='original-owned'&&keptReport.message.indexOf('owned-message')>=0&&keptReport.filename==='about:blank'&&keptReport.lineno>0&&keptReport.isTrusted",
		&answer);
	if (status != 0)
		return status;

	/* Record this independent invariant without hiding later collector observations. */
	report_check(answer == VM_VALUE_TRUE, "original exception and defining source survive ToString and listener GC");

	/* Unverified direct VM source cannot forge native provenance with a script property. */
	status = report_script(
		page->realm,
		"keptReport=null;reportCalls=0;function unverifiedReport(){collectReportFixture();throw {payload:'unverified',toString:function(){collectReportFixture();return 'unverified-message';}};}unverifiedReport.source={filename:'https://forged.invalid/',visible:true};setTimeout(unverifiedReport,0);true",
		&answer);
	if (status != 0)
		return status;

	/* The same production reporter must conservatively mask an unknown source. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Inspect real redacted ErrorEvent fields rather than patching a runner result. */
	status = report_script(
		page->realm,
		"collectReportFixture();reportCalls===1&&keptReport.message==='Script error.'&&keptReport.filename===''&&keptReport.lineno===0&&keptReport.colno===0&&keptReport.error===null",
		&answer);
	if (status != 0)
		return status;

	/* Record this independent invariant without hiding later collector observations. */
	report_check(answer == VM_VALUE_TRUE, "unknown source remains redacted despite an own source property");

	/* Exercise both conversion-time and listener-time nested reporting without losing the original exception. */
	status = report_script(
		page->realm,
		"var inlineReportScript=document.createElement('script');inlineReportScript.textContent=\"keptReport=null;reportCalls=0;var continuedReport=0;addEventListener('error',function(){collectReportFixture();throw new Error('listener-secondary');},{once:true});addEventListener('error',function(){collectReportFixture();continuedReport++;},{once:true});setTimeout(function(){throw {payload:'outer-owned',toString:function(){collectReportFixture();var target=new EventTarget();target.addEventListener('x',function(){collectReportFixture();throw new Error('conversion-secondary');});target.dispatchEvent(new Event('x'));collectReportFixture();return 'outer-message';}};},0);\";document.body.appendChild(inlineReportScript);true",
		&answer);
	if (status != 0)
		return status;

	/* Native reentry bounds must continue the remaining real listeners once. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Inspect actual continuation and original error identity after nested failures. */
	status = report_script(
		page->realm,
		"collectReportFixture();reportCalls===1&&continuedReport===1&&keptReport.error.payload==='outer-owned'&&keptReport.message.indexOf('outer-message')>=0",
		&answer);
	if (status != 0)
		return status;

	/* Record this independent invariant without hiding later collector observations. */
	report_check(answer == VM_VALUE_TRUE, "bounded nested reporting preserves the original event and later listeners");

	/* Reporting consumes its throw metadata without leaving a stale source or pending exception. */
	report_check(page->realm->exception == VM_VALUE_UNDEFINED && page->realm->throw_source == VM_VALUE_UNDEFINED, "reported pending state is cleared by the ordinary caller");

	/* Promise rejection is still handled by its separate existing reporting path. */
	status = report_script(
		page->realm,
		"var inlineReportScript=document.createElement('script');inlineReportScript.textContent=\"keptReport=null;reportCalls=0;Promise.reject('rejection-stays-separate');\";document.body.appendChild(inlineReportScript);true",
		&answer);
	if (status != 0)
		return status;

	/* Explicitly finish the ordinary checkpoint before inspecting Window error notifications. */
	status = bind_checkpoint(page->window);
	if (status != 0)
		return status;

	/* A rejected promise must never become a normal Window ErrorEvent. */
	status = report_script(
		page->realm,
		"reportCalls===0&&keptReport===null",
		&answer);
	if (status != 0)
		return status;

	/* Record this independent invariant without hiding later collector observations. */
	report_check(answer == VM_VALUE_TRUE, "promise rejection does not dispatch Window error");

	/* Metadata alone must retain its filename without retaining a retired child owner. */
	status = report_source_lifetime(page);
	if (status != 0)
		return status;

	/* A foreign conversion can retire the Window whose native report is still active. */
	status = report_retired_conversion(page);
	if (status != 0)
		return status;

	/* A successful foreign microtask must also retain its native checkpoint owner through retirement. */
	status = report_retired_checkpoint(page);
	if (status != 0)
		return status;

	/* Succeeded: all runtime ownership observations completed on the production paths. */
	return 0;
}

/* Proves source retention and foreign transfer without adding a hidden Window ownership cycle. */
static int
report_source_lifetime(
	struct page *page)
{
	struct vm_function *function;
	struct vm_cell *root;
	struct vm_cell *found;
	struct dom_node *node;
	struct dom_document *document;
	struct bind_window *owner;
	vm_value answer;
	uintptr_t source_address;
	uintptr_t filename_address;
	uintptr_t document_address;
	uintptr_t realm_address;
	int managed;
	int status;

	/* Build a real child document stream and its actual nested source function. */
	status = report_script(
		page->realm,
		"var sourceFrame=document.createElement('iframe');document.body.appendChild(sourceFrame);"
		"var sourceDocument=sourceFrame.contentDocument;sourceDocument.open();"
		"sourceDocument.write('<script>window.actualSourceFunction=function(){throw 42;};</script>');sourceDocument.close();"
		"var savedSourceFunction=sourceFrame.contentWindow.actualSourceFunction;savedSourceFunction",
		&answer);
	if (status != 0)
		return status;

	/* Actual VM metadata is opaque to script and must already be a managed embedding value. */
	managed = vm_value_is_callable(answer);
	if (!managed)
		return 2;
	function = (struct vm_function *)vm_value_as_cell(answer);
	managed = vm_value_is_cell(function->source);
	if (!managed)
		return 2;
	root = vm_value_as_cell(function->source);
	source_address = (uintptr_t)root;
	status = vm_heap_add_root(page->heap, &root);
	if (status != 0)
		return status;

	/* Capture the actual collectible owner without making an artificial fixture-side realm. */
	status = report_script(page->realm, "sourceDocument", &answer);
	if (status != 0)
		goto cleanup;
	node = bind_node_of(answer);
	if (node == NULL || node->type != DOM_DOCUMENT) {
		status = 2;
		goto cleanup;
	}

	/* Keep only integer addresses for later lifetime observations. */
	document = (struct dom_document *)node;
	owner = document->view;
	if (owner == NULL) {
		status = 2;
		goto cleanup;
	}

	/* These integers cannot keep either the child Document or realm alive. */
	document_address = (uintptr_t)&document->node.cell;
	realm_address = (uintptr_t)&owner->realm->cell;
	status = report_script(page->realm, "keptReport=null;reportCalls=0;setTimeout(savedSourceFunction,0);true", &answer);
	if (status != 0)
		goto cleanup;
	status = bind_run_timers(page->window);
	if (status != 0)
		goto cleanup;

	/* The parent receives real child metadata across the ordinary foreign exception transfer. */
	status = report_script(page->realm, "reportCalls===1&&keptReport.error===42&&keptReport.filename==='about:blank'", &answer);
	if (status != 0)
		goto cleanup;
	report_check(answer == VM_VALUE_TRUE, "foreign primitive exception preserves actual inline source metadata");
	status = report_script(page->realm, "keptReport.filename", &answer);
	if (status != 0)
		goto cleanup;
	filename_address = (uintptr_t)vm_value_as_cell(answer);

	/* Retire the owner and remove every script alias to the function, event and child graph. */
	status = report_script(page->realm, "sourceFrame.remove();sourceFrame=null;sourceDocument=null;savedSourceFunction=null;keptReport=null;true", &answer);
	if (status != 0)
		goto cleanup;
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, source_address);
	report_check(found != NULL, "an opaque metadata root alone retains the actual source cell");
	found = vm_heap_find_cell(page->heap, filename_address);
	report_check(found != NULL, "opaque metadata traces the actual immutable filename string");
	found = vm_heap_find_cell(page->heap, document_address);
	report_check(found == NULL, "opaque source metadata does not retain a retired Document");
	found = vm_heap_find_cell(page->heap, realm_address);
	report_check(found == NULL, "opaque source metadata does not retain its retired child realm");

	/* Removing the sole source root permits the actual metadata cell to be reclaimed. */
	root = NULL;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, source_address);
	report_check(found == NULL, "released source metadata is collectible after reporting and retirement");

cleanup:
	/* Both script failure and a completed lifetime observation release the same source root slot. */
	vm_heap_remove_root(page->heap, &root);
	if (status != 0)
		return status;

	/* Succeeded: provenance retains text without retaining the source owner. */
	return 0;
}

/* Proves the reporting native frame retains a child removed by a foreign conversion callback. */
static int
report_retired_conversion(
	struct page *page)
{
	struct dom_node *node;
	struct dom_document *document;
	struct bind_window *owner;
	struct vm_cell *found;
	vm_value answer;
	uintptr_t document_address;
	uintptr_t realm_address;
	int status;

	/* The queued callback and thrown object's converter belong to the active parent, not to the child. */
	status = report_script(
		page->realm,
		"var retireFrame=document.createElement('iframe');document.body.appendChild(retireFrame);"
		"var retireDocument=retireFrame.contentDocument,lateReportCalls=0;"
		"retireFrame.contentWindow.addEventListener('error',function(event){lateReportCalls++;event.preventDefault();});"
		"retireFrame.contentWindow.setTimeout(function(){throw {toString:function(){retireFrame.remove();retireFrame=null;retireDocument=null;collectReportFixture();return 'retired-conversion';}};},0);retireDocument",
		&answer);
	if (status != 0)
		return status;
	node = bind_node_of(answer);
	if (node == NULL || node->type != DOM_DOCUMENT)
		return 2;

	/* Integer observation addresses avoid introducing a fixture owner. */
	document = (struct dom_document *)node;
	owner = document->view;
	if (owner == NULL)
		return 2;
	document_address = (uintptr_t)&document->node.cell;
	realm_address = (uintptr_t)&owner->realm->cell;
	watched_document = document_address;
	watched_alive = 0;
	answer = VM_VALUE_UNDEFINED;

	/* Only the production report/checkpoint roots may protect this Window during the actual converter collection. */
	status = bind_run_timers(owner);
	watched_document = 0;
	if (status != 0)
		return status;
	report_check(watched_alive != 0U, "reporting retains its native child owner through foreign conversion retirement and GC");
	status = report_script(page->realm, "lateReportCalls===0", &answer);
	if (status != 0)
		return status;
	report_check(answer == VM_VALUE_TRUE, "a Window retired during conversion receives no late error callback");

	/* Once the active native report returns, neither its Document nor realm may leak. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	report_check(found == NULL, "retired reporting Document is reclaimed after its native frame returns");
	found = vm_heap_find_cell(page->heap, realm_address);
	report_check(found == NULL, "retired reporting realm is reclaimed after its native frame returns");

	/* Succeeded: retirement is safe while active and collectible after return. */
	return 0;
}

/* Proves a successful foreign microtask retains the native checkpoint owner it retires. */
static int
report_retired_checkpoint(
	struct page *page)
{
	struct dom_node *node;
	struct dom_document *document;
	struct bind_window *owner;
	struct vm_cell *found;
	vm_value answer;
	uintptr_t document_address;
	uintptr_t realm_address;
	int status;

	/* A parent-owned callback must not rely on its source function to retain this child's queue. */
	status = report_script(
		page->realm,
		"var checkpointFrame=document.createElement('iframe');document.body.appendChild(checkpointFrame);"
		"var checkpointDocument=checkpointFrame.contentDocument,checkpointCalls=0;"
		"checkpointFrame.contentWindow.queueMicrotask(function(){checkpointCalls++;checkpointFrame.remove();checkpointFrame=null;checkpointDocument=null;collectReportFixture();});checkpointDocument",
		&answer);
	if (status != 0)
		return status;
	node = bind_node_of(answer);
	if (node == NULL || node->type != DOM_DOCUMENT)
		return 2;

	/* Integer observation addresses avoid introducing a fixture owner. */
	document = (struct dom_document *)node;
	owner = document->view;
	if (owner == NULL)
		return 2;
	document_address = (uintptr_t)&document->node.cell;
	realm_address = (uintptr_t)&owner->realm->cell;
	watched_document = document_address;
	watched_alive = 0;
	answer = VM_VALUE_UNDEFINED;

	/* Only the production report/checkpoint roots may protect this Window during the actual job collection. */
	status = bind_checkpoint(owner);
	watched_document = 0;
	if (status != 0)
		return status;
	report_check(watched_alive != 0U, "checkpoint retains its child owner through a successful foreign job retirement and GC");
	status = report_script(page->realm, "checkpointCalls===1", &answer);
	if (status != 0)
		return status;
	report_check(answer == VM_VALUE_TRUE, "foreign microtask executes exactly once before its retired queue becomes quiet");

	/* Once the active native checkpoint returns, neither its Document nor realm may leak. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	report_check(found == NULL, "retired checkpoint Document is reclaimed after its native frame returns");
	found = vm_heap_find_cell(page->heap, realm_address);
	report_check(found == NULL, "retired checkpoint realm is reclaimed after its native frame returns");

	/* Succeeded: retirement is safe while active and collectible after return. */
	return 0;
}
