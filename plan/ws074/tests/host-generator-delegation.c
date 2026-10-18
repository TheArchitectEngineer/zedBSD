/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises yield* protocol ownership with conservative C stack retention disabled. */

#include "bind/internal.h"
#include "js/builtin.h"
#include "page/page.h"

#include <stdio.h>
#include <string.h>

/* Each script returns a Boolean ownership observation after collecting inside actual user callbacks. */
static const char *const delegation_cases[] = {
	"(function(){var x={};Object.defineProperty(x,Symbol.iterator,{get:function(){return function(){collectDelegation();return {next:function(){collectDelegation();return {value:11,done:true};}};};}});var g=(function*(){return yield* x;})();var r=g.next();return r.done&&r.value===11;})()",
	"(function(){var it={get next(){return function(){collectDelegation();return {value:12,done:true};};}},x={};x[Symbol.iterator]=function(){return it;};var g=(function*(){return yield* x;})();return g.next().value===12;})()",
	"(function(){var x={};x[Symbol.iterator]=function(){return {next:function(){return {get done(){collectDelegation();return true;},get value(){collectDelegation();return {owned:13};}};}};};var g=(function*(){return yield* x;})();var r=g.next();return r.done&&r.value.owned===13;})()",
	"(function(){var reads=0,original={get done(){collectDelegation();return false;},get value(){reads++;throw 1;}},x={};x[Symbol.iterator]=function(){return {next:function(){return original;}};};var g=(function*(){yield* x;})();var r=g.next();collectDelegation();return r===original&&reads===0;})()",
	"(function(){var x={};x[Symbol.iterator]=function(){return {next:function(){return {};},get throw(){return function(e){collectDelegation();return {done:true,value:e};};}};};var g=(function*(){return yield* x;})();g.next();var r=g.throw({owned:15});return r.done&&r.value.owned===15;})()",
	"(function(){var x={};x[Symbol.iterator]=function(){return {next:function(){return {};},get return(){return function(e){collectDelegation();return {get done(){collectDelegation();return true;},get value(){collectDelegation();return e;}};};}};};var g=(function*(){return yield* x;})();g.next();var r=g.return({owned:16});return r.done&&r.value.owned===16;})()",
	"(function(){var x={};x[Symbol.iterator]=function(){return {next:function(v){collectDelegation();return {done:v!==undefined,value:v};}};};var g=(function*(){return yield* x;})();g.next();var r=g.next({owned:17});return r.done&&r.value.owned===17;})()",
	"(function(){var calls=0,x={};x[Symbol.iterator]=function(){return {next:function(){return {};},get return(){collectDelegation();return function(){collectDelegation();calls++;return {get done(){throw 9;}};};}};};var g=(function*(){yield* x;})();g.next();try{g.throw({owned:18});}catch(e){return e instanceof TypeError&&calls===1;}return false;})()",
	"(function(){var x={};x[Symbol.iterator]=function(){var n=0;return {next:function(){collectDelegation();return {value:++n,done:n===2};}};};var g=(function*(){return yield* x;})();g.next();x=null;collectDelegation();var r=g.next();return r.done&&r.value===2;})()",
	"(function(){var g=(function*(){try{return yield* [1,2];}finally{collectDelegation();yield 3;}})();g.next();collectDelegation();var a=g.return({owned:20});collectDelegation();var b=g.next();return !a.done&&a.value===3&&b.done&&b.value.owned===20;})()"
};

static int delegation_collect(struct vm_realm *realm, vm_value receiver, const vm_value *args, unsigned count, vm_value *result);
static int delegation_run(struct vm_realm *realm, const char *source, vm_value *answer);

/*
 * Runs the real compiler, generator and collector with only explicit roots.
 */
int
main(
	void)
{
	struct page *page;
	static const unsigned char html[] = "<!doctype html><html><body></body></html>";
	vm_value answer;
	unsigned index;
	unsigned passed;
	unsigned total;
	int status;
	int printed;

	/* Initialize the ordinary production Page before disabling conservative stack retention. */
	status = page_create(&page, __builtin_frame_address(0));
	if (status != 0)
		return 2;
	status = page_load_html(page, html, sizeof(html) - 1U);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Install only a test-native collector callback; product APIs stay unchanged. */
	status = js_builtin_method(page->realm, page->realm->global, "collectDelegation", 0, delegation_collect);
	if (status != 0) {
		page_destroy(page);
		return 2;
	}

	/* Getters and iterator methods collect during actual native protocol operations. */
	vm_heap_set_stack_base(page->heap, NULL);
	passed = 0;
	total = sizeof(delegation_cases) / sizeof(delegation_cases[0]);
	for (index = 0; index < total; index++) {
		status = delegation_run(page->realm, delegation_cases[index], &answer);
		if (status != 0)
			break;
		if (answer == VM_VALUE_TRUE) {
			passed++;
		} else {
			printed = fprintf(stderr, "FAIL: delegation ownership %u\n", index + 1U);
			if (printed < 0) {
				status = 2;
				break;
			}
		}
	}

	/* Restore ordinary retention before destroying the Page graph. */
	vm_heap_set_stack_base(page->heap, __builtin_frame_address(0));
	page_destroy(page);
	if (status != 0)
		return 2;
	printed = printf("native generator delegation: %u/%u passed\n", passed, total);
	if (printed < 0)
		return 2;
	if (passed != total)
		return 1;

	/* Succeeded: all protocol values survived collection without conservative retention. */
	return 0;
}

/* Exposes the existing collector at script-selected protocol boundaries without changing production behavior. */
static int
delegation_collect(
	struct vm_realm *realm,
	vm_value receiver,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(receiver);
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);

	/* Explicit engine and VM roots alone retain the running protocol graph. */
	vm_heap_collect(realm->heap);
	*result = VM_VALUE_UNDEFINED;

	/* Succeeded: the real user callback resumes after collection. */
	return 0;
}

/* Executes fixture source through the actual parser, compiler and VM. */
static int
delegation_run(
	struct vm_realm *realm,
	const char *source,
	vm_value *answer)
{
	struct wb_units units;
	struct js_syntax_error syntax;
	int status;

	/* Convert source without substituting native product values or behavior. */
	wb_units_init(&units);
	status = wb_utf8_to_units((const unsigned char *)source, strlen(source), &units);
	if (status != 0) {
		wb_units_release(&units);
		return status;
	}

	/* Execute the real program before releasing its temporary UTF-16 source. */
	status = js_run_script(realm, units.data, units.length, 0, answer, &syntax);
	wb_units_release(&units);
	if (status != 0)
		return status;

	/* Succeeded: the actual script supplied the observation. */
	return 0;
}
