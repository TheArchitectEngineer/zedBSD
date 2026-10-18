/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises actual mark ownership and reentrant collection without conservative stack retention. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Inert allocation pressure reaches the production collector threshold without retaining other cells. */
static const struct vm_cell_type marks_pressure_type = {"marks-fixture-pressure", NULL, NULL};

/* Accumulates independent ownership observations until the fixture completes. */
static unsigned checks;

/* Keeps failures visible while later independent collector cases still run. */
static unsigned failures;

static int marks_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int marks_script(struct vm_realm *realm, const char *source, vm_value *answer);
static int marks_case(struct page *page);
static void marks_check(int condition, const char *name);

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
	status = js_builtin_method(page->realm, page->realm->global, "collectMarksFixture", 0, marks_collect);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Heap and VM roots must carry all native entry ownership during the selected cases. */
	vm_heap_set_stack_base(page->heap, NULL);
	status = marks_case(page);

	/* Restore ordinary scanning before releasing the complete production Page. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);

	/* Internal execution failure is distinct from an assertion observation. */
	if (status != 0)
		return 2;

	/* Emit one compact aggregate after all ownership checks have finished. */
	printed = printf("native performance marks: %u/%u passed\n", checks - failures, checks);
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
marks_collect(
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
marks_script(
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
marks_check(
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

/* Covers buffer-only reachability, saved entry lifetimes, conversion GC and retired child ownership. */
static int
marks_case(
	struct page *page)
{
	vm_value answer;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	struct vm_cell *pressure;
	int status;

	/* Only the timeline retains the two hundred created marks after this script ends. */
	status = marks_script(
		page->realm,
		"performance.clearMarks();for(var i=0;i<200;i++)performance.mark('buffer-'+i,{detail:'payload-'+i,startTime:i});true",
		&answer);
	if (status != 0)
		return status;

	/* Force collection after discarding the native completion value. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_collect(page->heap);
	status = marks_script(
		page->realm,
		"var a=performance.getEntriesByType('mark');a.length===200&&a[199].name==='buffer-199'&&a[199].detail==='payload-199'",
		&answer);
	if (status != 0)
		return status;

	/* All payloads survived through the real managed timeline trace. */
	marks_check(answer == VM_VALUE_TRUE, "buffer alone retains actual mark names and primitive details");

	/* A separately saved entry must survive clearing the only timeline references. */
	status = marks_script(
		page->realm,
		"var saved=a[99];a=null;performance.clearMarks();collectMarksFixture();saved.name==='buffer-99'&&saved.detail==='payload-99'&&performance.getEntriesByType('mark').length===0",
		&answer);
	if (status != 0)
		return status;

	/* Entry payload lifetime is independent of the buffer's references. */
	marks_check(answer == VM_VALUE_TRUE, "saved entry remains readable after clearMarks and GC");

	/* Name and detail conversion temporaries must survive collector reentry before publication. */
	status = marks_script(
		page->realm,
		"var converted=performance.mark({toString:function(){return 'converted-'+Math.random();}},"
		"{get detail(){collectMarksFixture();return 'detail-'+Math.random();},get startTime(){collectMarksFixture();return 17;}});"
		"collectMarksFixture();converted.name.indexOf('converted-')===0&&converted.detail.indexOf('detail-')===0&&converted.startTime===17",
		&answer);
	if (status != 0)
		return status;

	/* Getter-triggered collection did not orphan either converted primitive. */
	marks_check(answer == VM_VALUE_TRUE, "name and detail roots survive allocating option getters and GC");

	/* A second filter conversion may collect the string returned by the first one. */
	status = marks_script(
		page->realm,
		"var selected=performance.getEntriesByName({toString:function(){return converted.name.slice(0);}},"
		"{toString:function(){collectMarksFixture();return 'mark';}});selected.length===1&&selected[0]===converted",
		&answer);
	if (status != 0)
		return status;

	/* The completed snapshot preserves actual entry identity through both conversions. */
	marks_check(answer == VM_VALUE_TRUE, "filter conversion roots survive reentrant collection");

	/* Real frame creation supplies an independent relevant realm and mark buffer. */
	status = marks_script(
		page->realm,
		"var f=document.createElement('iframe');document.body.appendChild(f);"
		"var child=f.contentWindow;var childMark=child.performance.mark('child-kept',{detail:'child-payload',startTime:23});"
		"var borrowed=performance.mark.call(child.performance,'borrowed',{detail:'borrowed-payload',startTime:24});"
		"var independent=child.performance.getEntriesByType('mark').length===2&&performance.getEntriesByName('child-kept').length===0;"
		"independent=independent&&Object.getPrototypeOf(borrowed)===child.PerformanceMark.prototype;"
		"f.remove();f=null;child=null;collectMarksFixture();"
		"independent&&childMark.name==='child-kept'&&childMark.detail==='child-payload'&&childMark.startTime===23&&borrowed.name==='borrowed'",
		&answer);
	if (status != 0)
		return status;

	/* A saved entry never dereferences retired frame or document storage. */
	marks_check(answer == VM_VALUE_TRUE, "independent child mark survives iframe retirement and source-root-only GC");

	/* Push the unchanged production allocation threshold before creating another independent child. */
	pressure = vm_heap_alloc(page->heap, &marks_pressure_type, 8U * 1024U * 1024U);
	if (pressure == NULL)
		return 2;

	/* Realm initialization and global publication must retain genuine performance state under pressure. */
	vm_heap_stats(page->heap, &before);
	status = marks_script(
		page->realm,
		"var pressureFrame=document.createElement('iframe');document.body.appendChild(pressureFrame);"
		"var pressureMark=pressureFrame.contentWindow.performance.mark('pressure',{detail:'kept'});"
		"collectMarksFixture();pressureFrame.remove();pressureFrame=null;collectMarksFixture();"
		"pressureMark.name==='pressure'&&pressureMark.detail==='kept'",
		&answer);
	if (status != 0)
		return status;

	/* The collector ran while the new managed context and its native API remained usable. */
	vm_heap_stats(page->heap, &after);
	marks_check(answer == VM_VALUE_TRUE && after.collections > before.collections, "performance initialization and publication survive real allocation pressure");

	/* Release all script references after proving the retained graph was usable. */
	status = marks_script(
		page->realm,
		"saved=null;converted=null;selected=null;childMark=null;borrowed=null;pressureMark=null;performance.clearMarks();true",
		&answer);
	if (status != 0)
		return status;

	/* Collection should reclaim the now-unreachable entry graphs. */
	answer = VM_VALUE_UNDEFINED;
	vm_heap_stats(page->heap, &before);
	vm_heap_collect(page->heap);
	vm_heap_stats(page->heap, &after);
	marks_check(after.freed_cells > before.freed_cells, "released mark and retired child graphs are collectible");

	/* Succeeded: every native ownership path completed without hidden stack roots. */
	return 0;
}
