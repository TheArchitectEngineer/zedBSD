/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises actual observer ownership and reentrant collection without conservative stack retention. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Accumulates independent ownership observations until the fixture completes. */
static unsigned checks;

/* Keeps failures visible while later independent collector cases still run. */
static unsigned failures;

static int observer_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int observer_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int observer_cases(struct page *page);
static void observer_check(int condition, const char *name);
static int observer_retired_list(struct page *page);

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
	status = js_builtin_method(page->realm, page->realm->global, "collectObserverFixture", 0, observer_collect);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Heap and VM roots must carry all native entry ownership during the selected cases. */
	vm_heap_set_stack_base(page->heap, NULL);
	status = observer_cases(page);

	/* Restore ordinary scanning before releasing the complete production Page. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);

	/* Internal execution failure is distinct from an assertion observation. */
	if (status != 0)
		return 2;

	/* Emit one compact aggregate after all ownership checks have finished. */
	printed = printf("native performance observer: %u/%u passed\n", checks - failures, checks);
	if (printed < 0)
		return 2;

	/* Any failed root or lifetime observation rejects the native implementation. */
	if (failures != 0)
		return 1;

	/* Succeeded: actual buffer, entry and retired-frame ownership survived collection. */
	return 0;
}

/* Exposes an ordinary native fixture callback that collects at the exact point requested by a getter. */
static int
observer_collect(
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
observer_script(
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
observer_check(
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

/* Verifies genuine queued records, callback reentry, exceptions, microtasks and uncancellable task ownership. */
static int
observer_cases(
	struct page *page)
{
	vm_value answer;
	int status;

	/* Only native registration retains this anonymous observer and its callback before collection. */
	status = observer_script(
		page->realm,
		"performance.clearMarks();var delivered=[],identity=false;"
		"new PerformanceObserver(function(list,observer,options){collectObserverFixture();"
		"delivered=list.getEntries().map(function(e){return e.name+':'+e.detail;});"
		"identity=this===observer&&list instanceof PerformanceObserverEntryList&&options.droppedEntriesCount===0;"
		"observer.disconnect();}).observe({entryTypes:{[Symbol.iterator]:function(){var n=0;return {next:function(){"
		"collectObserverFixture();return n++===0?{done:false,value:{toString:function(){collectObserverFixture();return 'mark';}}}:{done:true};}};}}});"
		"performance.mark('late',{startTime:10,detail:'kept-late'});performance.mark('early',{startTime:1,detail:'kept-early'});true",
		&answer);
	if (status != 0)
		return status;

	/* The production collector must preserve registered callback and entry identities without C stack roots. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Independent names, details and receiver/list identities prove actual positive delivery. */
	status = observer_script(page->realm, "identity&&delivered.join('|')==='early:kept-early|late:kept-late'", &answer);
	if (status != 0)
		return status;

	/* A no-op callback or empty synthetic list cannot satisfy this positive observation. */
	observer_check(answer == VM_VALUE_TRUE, "registration alone retains callback and real marks through iterator and delivery GC");

	/* Reentrant mark creation must populate a distinct next batch while the first list stays immutable. */
	status = observer_script(
		page->realm,
		"performance.clearMarks();var batches=[],retainedList=null;var recursive=new PerformanceObserver(function(list){"
		"collectObserverFixture();batches.push(list.getEntries().map(function(e){return e.name;}).join(','));"
		"if(batches.length===1){retainedList=list;performance.mark('second');}else recursive.disconnect();});"
		"recursive.observe({type:'mark'});performance.mark('first');true",
		&answer);
	if (status != 0)
		return status;

	/* The existing task generation boundary separates reentrant work into a later round. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* First-round inspection occurs before the later native task is allowed to execute. */
	status = observer_script(page->realm, "batches.join('|')==='first'&&retainedList.getEntries()[0].name==='first'", &answer);
	if (status != 0)
		return status;

	/* A reentrant record must not mutate or synchronously redeliver the current batch. */
	observer_check(answer == VM_VALUE_TRUE, "reentrant mark waits for the next ordinary task generation");

	/* A second actual Window task round must deliver the queued new mark. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Saved list contents remain independent of later batches and explicit mark clearing. */
	status = observer_script(
		page->realm,
		"performance.clearMarks();collectObserverFixture();batches.join('|')==='first|second'&&retainedList.getEntries()[0].name==='first'",
		&answer);
	if (status != 0)
		return status;

	/* Both positive batches and the old actual entry survived reentrant collection. */
	observer_check(answer == VM_VALUE_TRUE, "later task delivers real second batch without altering the saved first list");

	/* One observer exception must be reported without preventing another observer's independent callback. */
	status = observer_script(
		page->realm,
		"var errorCalls=0,followingCalls=0;var bad=new PerformanceObserver(function(){errorCalls++;bad.disconnect();throw new Error('observer fixture error');});"
		"var following=new PerformanceObserver(function(){collectObserverFixture();followingCalls++;following.disconnect();});"
		"bad.observe({type:'mark'});following.observe({type:'mark'});performance.mark('exception');true",
		&answer);
	if (status != 0)
		return status;

	/* Dispatch uses the production exception reporter and continues its original notification snapshot. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* Both callbacks must actually execute once despite the first thrown error. */
	status = observer_script(page->realm, "errorCalls===1&&followingCalls===1", &answer);
	if (status != 0)
		return status;

	/* Exception continuation is a real positive callback observation rather than an absence check. */
	observer_check(answer == VM_VALUE_TRUE, "a reported callback exception does not suppress the next real observer");

	/* WebIDL callback cleanup drains queued microtasks before invoking the next registered observer. */
	status = observer_script(
		page->realm,
		"var order=[];var one=new PerformanceObserver(function(){order.push('first');"
		"Promise.resolve().then(function(){collectObserverFixture();order.push('micro');});one.disconnect();});"
		"var two=new PerformanceObserver(function(){order.push('second');two.disconnect();});"
		"one.observe({type:'mark'});two.observe({type:'mark'});performance.mark('order');true",
		&answer);
	if (status != 0)
		return status;

	/* The actual task callback performs the same microtask boundary as the independent Chrome observation. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* The observable ordering includes a collector call from inside the microtask. */
	status = observer_script(page->realm, "order.join('|')==='first|micro|second'", &answer);
	if (status != 0)
		return status;

	/* Ordinary task batching must preserve each callback's own microtask cleanup semantics. */
	observer_check(answer == VM_VALUE_TRUE, "microtask checkpoint between native callbacks matches the real event loop");

	/* Public timer cancellation cannot cancel the unexposed native performance task. */
	status = observer_script(
		page->realm,
		"var protectedCalls=0;var protectedObserver=new PerformanceObserver(function(){protectedCalls++;protectedObserver.disconnect();});"
		"protectedObserver.observe({type:'mark'});performance.mark('protected');for(var id=0;id<64;id++)clearTimeout(id);true",
		&answer);
	if (status != 0)
		return status;

	/* Native private task ownership remains intact after all guessed public timer ids are canceled. */
	status = bind_run_timers(page->window);
	if (status != 0)
		return status;

	/* A real delivered callback proves that cancellation did not remove the private task. */
	status = observer_script(page->realm, "protectedCalls===1", &answer);
	if (status != 0)
		return status;

	/* No script-visible timer identifier supplied access to the observer task. */
	observer_check(answer == VM_VALUE_TRUE, "public clearTimeout cannot cancel native observer delivery");

	/* Source-root-only retired child ownership is checked separately from ordinary parent globals. */
	status = observer_retired_list(page);
	if (status != 0)
		return status;

	/* Succeeded: all selected real task, callback and ownership paths completed. */
	return 0;
}

/* Retains a delivered child list as the sole explicit root, then proves the released graph is collectible. */
static int
observer_retired_list(
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

	/* Build an actual child context and deliver its genuine queued mark while it is active. */
	status = observer_script(
		page->realm,
		"var observerFrame=document.createElement('iframe');document.body.appendChild(observerFrame);"
		"var observerDocument=observerFrame.contentDocument,deliveredChildList=null;"
		"var childObserver=new observerFrame.contentWindow.PerformanceObserver(function(list,observer){"
		"collectObserverFixture();deliveredChildList=list;observer.disconnect();});"
		"childObserver.observe({type:'mark'});observerFrame.contentWindow.performance.mark('child-list',{detail:'child-payload'});observerDocument",
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

	/* The active Document supplies the genuine relevant Window for its native task. */
	document = (struct dom_document *)node;
	owner = document->view;
	if (owner == NULL)
		return 2;

	/* Capture integer addresses that cannot act as conservative roots. */
	document_address = (uintptr_t)&document->node.cell;
	realm_address = (uintptr_t)&owner->realm->cell;
	status = bind_run_timers(owner);
	if (status != 0)
		return status;

	/* Obtain the actual delivered wrapper rather than constructing a snapshot in the fixture. */
	status = observer_script(page->realm, "deliveredChildList", &answer);
	if (status != 0)
		return status;

	/* Delivery must have created a genuine object before adding its sole retaining root. */
	if (answer == VM_VALUE_NULL || answer == VM_VALUE_UNDEFINED)
		return 2;

	/* The saved list alone now retains any required child realm and mark payload graph. */
	root = vm_value_as_cell(answer);
	status = vm_heap_add_root(page->heap, &root);
	if (status != 0)
		return status;

	/* Retire the actual iframe and discard every script alias to its observer, Document and delivered list. */
	status = observer_script(
		page->realm,
		"observerFrame.remove();observerFrame=null;observerDocument=null;childObserver=null;deliveredChildList=null;true",
		&answer);
	if (status != 0)
		goto cleanup;

	/* With native stack scanning disabled, only the explicit saved wrapper can retain this retired graph. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	observer_check(found != NULL, "a delivered EntryList alone retains its actual retired child ownership graph");

	/* The native query must remain usable through a conversion that forces another actual collection. */
	status = js_builtin_value(page->realm, page->realm->global, "savedObserverList", vm_value_cell(root), VM_PROPERTY_DEFAULT);
	if (status != 0)
		goto cleanup;

	/* Read independently retained real name/detail data through the ordinary native EntryList API. */
	status = observer_script(
		page->realm,
		"var kept=savedObserverList.getEntriesByName({toString:function(){collectObserverFixture();return 'child-list';}})[0];"
		"var keptGood=kept.name==='child-list'&&kept.detail==='child-payload';"
		"savedObserverList=null;kept=null;keptGood",
		&answer);
	if (status != 0)
		goto cleanup;

	/* The saved snapshot survived source retirement, alias removal and reentrant filter collection. */
	observer_check(answer == VM_VALUE_TRUE, "retired child EntryList retains genuine immutable mark payload through GC");

	/* Release the sole explicit wrapper root before checking actual graph reclamation. */
	root = NULL;
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	found = vm_heap_find_cell(page->heap, document_address);
	observer_check(found == NULL, "releasing the saved list permits its retired child Document to be collected");

	/* Retired realm ownership must also disappear rather than leaking through native registration cycles. */
	found = vm_heap_find_cell(page->heap, realm_address);
	observer_check(found == NULL, "releasing the saved list permits its managed child realm to be collected");

cleanup:
	/* Remove the root registry slot on both successful and failed lifecycle verification. */
	vm_heap_remove_root(page->heap, &root);

	/* A failed script or native property operation remains an execution failure. */
	if (status != 0)
		return status;

	/* Succeeded: a real saved list is safe while retained and collectible after release. */
	return 0;
}
