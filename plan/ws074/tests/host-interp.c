/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p023: the host test of the shared bytecode and the interpreter,
 * with code assembled by hand: JavaScript-typed functions (recursion,
 * loops, exceptions, calls to and from native functions, accessors,
 * strings), Wasm-typed ones (raw i32, i64, f64), the checker's refusals,
 * the stack's limit and a collection in the middle of a run.
 *
 *   host-interp
 *
 * Prints one line per failed check and a summary.
 */

#include "vm/bytecode.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * A code unit being assembled.
 */
struct assembler {
	uint32_t words[1024];
	uint32_t count;
	vm_value constants[32];
	uint32_t constant_count;
	struct vm_handler handlers[8];
	uint32_t handler_count;
};

static int failures;
static int checks;
static struct vm_realm *realm;

static void check(int condition, const char *what);
static uint32_t emit(struct assembler *code, unsigned opcode, ...);
static void patch(struct assembler *code, uint32_t instruction, unsigned operand, uint32_t target);
static uint32_t constant(struct assembler *code, vm_value value);
static struct vm_function *finish(struct assembler *code, const char *name, uint32_t registers, uint32_t parameters);
static vm_value key(const char *name);
static vm_value string(const char *text);
static void define_global(const char *name, struct vm_function *function);
static int call(struct vm_function *function, vm_value argument, vm_value *result);
static int native_twice(struct vm_realm *realm_of, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int native_apply(struct vm_realm *realm_of, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static void test_fib(void);
static void test_loop(void);
static void test_exceptions(void);
static void test_natives(void);
static void test_accessors(void);
static void test_strings(void);
static void test_limits(void);
static void test_rejected(void);
static void test_wasm(void);
static void test_collection(void);

int
main(void)
{
	struct vm_heap *heap;
	int error;

	error = vm_heap_create(&heap, 0);
	check(error == 0, "heap: create");
	if (error != 0)
		return 1;
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	error = vm_realm_create(heap, &realm);
	check(error == 0, "realm: create");
	if (error != 0)
		return 1;

	test_fib();
	test_loop();
	test_exceptions();
	test_natives();
	test_accessors();
	test_strings();
	test_limits();
	test_rejected();
	test_wasm();
	test_collection();

	check(realm->stack_top == 0 && realm->depth == 0, "stack: empty after every run");
	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	printf("host-interp: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	printf("FAIL %s\n", what);
}

/* Appends an instruction and its operands; reports its offset. */
static uint32_t
emit(
	struct assembler *code,
	unsigned opcode,
	...)
{
	va_list operands;
	uint32_t start;
	unsigned index;

	start = code->count;
	code->words[code->count++] = opcode;
	va_start(operands, opcode);
	for (index = 0; index < vm_opcodes[opcode].operand_count; index++)
		code->words[code->count++] = va_arg(operands, uint32_t);
	va_end(operands);
	return start;
}

/* Points an instruction's jump operand at a target offset. */
static void
patch(
	struct assembler *code,
	uint32_t instruction,
	unsigned operand,
	uint32_t target)
{
	code->words[instruction + 1U + operand] = target - instruction;
}

static uint32_t
constant(
	struct assembler *code,
	vm_value value)
{
	code->constants[code->constant_count] = value;
	return code->constant_count++;
}

/* Makes the checked code unit and its function. */
static struct vm_function *
finish(
	struct assembler *code,
	const char *name,
	uint32_t registers,
	uint32_t parameters)
{
	struct vm_code model;
	struct vm_code *made;
	uint32_t bad;
	int error;

	memset(&model, 0, sizeof(model));
	model.words = code->words;
	model.word_count = code->count;
	model.register_count = registers;
	model.parameter_count = parameters;
	model.constants = code->constants;
	model.constant_count = code->constant_count;
	model.handlers = code->handlers;
	model.handler_count = code->handler_count;
	model.name = vm_string_from_utf8(realm->heap, name, strlen(name));
	error = vm_code_create(realm->heap, &model, &made, &bad);
	if (error != 0) {
		printf("code %s refused at %u\n", name, bad);
		return NULL;
	}
	return vm_function_create(realm, made);
}

static vm_value
key(
	const char *name)
{
	return vm_key_from_ascii(realm->heap, name);
}

static vm_value
string(
	const char *text)
{
	return vm_value_cell(vm_string_from_utf8(realm->heap, text, strlen(text)));
}

static void
define_global(
	const char *name,
	struct vm_function *function)
{
	vm_object_define(realm->heap, realm->global, key(name), vm_value_cell(function), VM_PROPERTY_DEFAULT);
}

static int
call(
	struct vm_function *function,
	vm_value argument,
	vm_value *result)
{
	return vm_call(realm, vm_value_cell(function), VM_VALUE_UNDEFINED, &argument, 1, result);
}

/* fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 2), calling itself through the global. */
static void
test_fib(void)
{
	struct assembler code;
	struct vm_function *fib;
	struct vm_heap_stats stats;
	vm_value result;
	uint32_t jump;
	uint32_t name;
	int status;

	memset(&code, 0, sizeof(code));
	name = constant(&code, key("fib"));
	emit(&code, VM_OP_LOAD_INT, 1U, 2U);
	emit(&code, VM_OP_LESS, 2U, 0U, 1U);
	jump = emit(&code, VM_OP_JUMP_IF_FALSE, 2U, 0U);
	emit(&code, VM_OP_RETURN, 0U);
	patch(&code, jump, 1, code.count);
	emit(&code, VM_OP_LOAD_INT, 1U, 1U);
	emit(&code, VM_OP_SUB, 3U, 0U, 1U);
	emit(&code, VM_OP_GET_GLOBAL, 4U, name);
	emit(&code, VM_OP_CALL, 5U, 4U, 6U, 3U, 1U);
	emit(&code, VM_OP_LOAD_INT, 1U, 2U);
	emit(&code, VM_OP_SUB, 3U, 0U, 1U);
	emit(&code, VM_OP_CALL, 7U, 4U, 6U, 3U, 1U);
	emit(&code, VM_OP_ADD, 8U, 5U, 7U);
	emit(&code, VM_OP_RETURN, 8U);
	fib = finish(&code, "fib", 9, 1);
	check(fib != NULL, "fib: assembled");
	if (fib == NULL)
		return;
	define_global("fib", fib);
	status = call(fib, vm_value_int32(20), &result);
	check(status == 0 && result == vm_value_int32(6765), "fib: fib(20) is 6765");
	status = call(fib, vm_value_int32(1), &result);
	check(status == 0 && result == vm_value_int32(1), "fib: fib(1) is 1");
	vm_heap_stats(realm->heap, &stats);
	printf("host-interp: fib(20) ran; %zu collections so far\n", stats.collections);
}

/* sum(n): s = 0; for (i = 0; i < n; i = i + 1) s = s + i; return s. */
static void
test_loop(void)
{
	struct assembler code;
	struct vm_function *sum;
	vm_value result;
	uint32_t head;
	uint32_t exit;
	int status;

	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_LOAD_INT, 1U, 0U);
	emit(&code, VM_OP_LOAD_INT, 2U, 0U);
	emit(&code, VM_OP_LOAD_INT, 4U, 1U);
	head = emit(&code, VM_OP_LOOP_HINT);
	emit(&code, VM_OP_LESS, 3U, 2U, 0U);
	exit = emit(&code, VM_OP_JUMP_IF_FALSE, 3U, 0U);
	emit(&code, VM_OP_ADD, 1U, 1U, 2U);
	emit(&code, VM_OP_ADD, 2U, 2U, 4U);
	patch(&code, emit(&code, VM_OP_JUMP, 0U), 0, head);
	patch(&code, exit, 1, code.count);
	emit(&code, VM_OP_RETURN, 1U);
	sum = finish(&code, "sum", 5, 1);
	check(sum != NULL, "loop: assembled");
	if (sum == NULL)
		return;
	status = call(sum, vm_value_int32(1000), &result);
	check(status == 0 && result == vm_value_int32(499500), "loop: sum(1000) is 499500");
	status = call(sum, vm_value_double(100000.0), &result);
	check(status == 0 && vm_value_as_number(result) == 4999950000.0, "loop: sum(100000) leaves int32");
}

/*
 * thrower(x): throw x.
 * catcher(x): try { thrower(x) } catch (e) { return e + 1 }.
 * rethrower(x): try { thrower(x) } catch (e) { throw e + 100 }.
 * outer(x): try { rethrower(x) } catch (e) { return e }.
 */
static void
test_exceptions(void)
{
	struct assembler code;
	struct vm_function *thrower;
	struct vm_function *catcher;
	struct vm_function *rethrower;
	struct vm_function *outer;
	vm_value result;
	uint32_t name;
	uint32_t start;
	uint32_t end;
	int status;

	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_THROW, 0U);
	thrower = finish(&code, "thrower", 1, 1);
	define_global("thrower", thrower);

	memset(&code, 0, sizeof(code));
	name = constant(&code, key("thrower"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	start = emit(&code, VM_OP_CALL, 2U, 1U, 3U, 0U, 1U);
	end = code.count;
	emit(&code, VM_OP_RETURN, 2U);
	code.handlers[0].start = start;
	code.handlers[0].end = end;
	code.handlers[0].handler = code.count;
	code.handlers[0].exception_register = 4U;
	code.handler_count = 1;
	emit(&code, VM_OP_LOAD_INT, 5U, 1U);
	emit(&code, VM_OP_ADD, 6U, 4U, 5U);
	emit(&code, VM_OP_RETURN, 6U);
	catcher = finish(&code, "catcher", 7, 1);

	memset(&code, 0, sizeof(code));
	name = constant(&code, key("thrower"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	start = emit(&code, VM_OP_CALL, 2U, 1U, 3U, 0U, 1U);
	end = code.count;
	emit(&code, VM_OP_RETURN, 2U);
	code.handlers[0].start = start;
	code.handlers[0].end = end;
	code.handlers[0].handler = code.count;
	code.handlers[0].exception_register = 4U;
	code.handler_count = 1;
	emit(&code, VM_OP_LOAD_INT, 5U, 100U);
	emit(&code, VM_OP_ADD, 6U, 4U, 5U);
	emit(&code, VM_OP_THROW, 6U);
	rethrower = finish(&code, "rethrower", 7, 1);
	define_global("rethrower", rethrower);

	memset(&code, 0, sizeof(code));
	name = constant(&code, key("rethrower"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	start = emit(&code, VM_OP_CALL, 2U, 1U, 3U, 0U, 1U);
	end = code.count;
	emit(&code, VM_OP_RETURN, 2U);
	code.handlers[0].start = start;
	code.handlers[0].end = end;
	code.handlers[0].handler = code.count;
	code.handlers[0].exception_register = 4U;
	code.handler_count = 1;
	emit(&code, VM_OP_RETURN, 4U);
	outer = finish(&code, "outer", 5, 1);

	check(thrower != NULL && catcher != NULL && rethrower != NULL && outer != NULL, "exceptions: assembled");
	if (outer == NULL)
		return;
	status = call(catcher, vm_value_int32(42), &result);
	check(status == 0 && result == vm_value_int32(43), "exceptions: catch 42, return 43");
	status = call(outer, vm_value_int32(1), &result);
	check(status == 0 && result == vm_value_int32(101), "exceptions: rethrown 101 caught outside");
	status = call(rethrower, vm_value_int32(5), &result);
	check(status == VM_THROWN && realm->exception == vm_value_int32(105), "exceptions: uncaught leaves the run");
	status = call(thrower, string("boom"), &result);
	check(status == VM_THROWN && vm_strict_equals(realm->exception, string("boom")), "exceptions: a string thrown");
}

/* Bytecode calling native (twice), native calling bytecode (apply(fib, 10)). */
static void
test_natives(void)
{
	struct assembler code;
	struct vm_function *twice;
	struct vm_function *apply;
	struct vm_function *user;
	vm_value result;
	uint32_t name;
	int status;

	twice = vm_function_create_native(realm, "twice", 1, native_twice);
	apply = vm_function_create_native(realm, "apply", 2, native_apply);
	define_global("twice", twice);
	define_global("apply", apply);

	/* user(x) = twice(apply(fib, x)). */
	memset(&code, 0, sizeof(code));
	name = constant(&code, key("apply"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	name = constant(&code, key("fib"));
	emit(&code, VM_OP_GET_GLOBAL, 3U, name);
	emit(&code, VM_OP_MOV, 4U, 0U);
	emit(&code, VM_OP_CALL, 5U, 1U, 2U, 3U, 2U);
	name = constant(&code, key("twice"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	emit(&code, VM_OP_CALL, 6U, 1U, 2U, 5U, 1U);
	emit(&code, VM_OP_RETURN, 6U);
	user = finish(&code, "user", 7, 1);
	check(user != NULL, "natives: assembled");
	if (user == NULL)
		return;
	status = call(user, vm_value_int32(10), &result);
	check(status == 0 && result == vm_value_int32(110), "natives: twice(apply(fib, 10)) is 110");

	/* Calling what is not a function throws a TypeError. */
	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_CALL, 1U, 0U, 0U, 0U, 0U);
	emit(&code, VM_OP_RETURN, 1U);
	user = finish(&code, "bad", 2, 1);
	status = call(user, vm_value_int32(3), &result);
	check(status == VM_THROWN && vm_value_is_cell(realm->exception), "natives: calling a number throws");
}

/* A getter and a setter written in bytecode, used by get_prop and put_prop. */
static void
test_accessors(void)
{
	struct assembler code;
	struct vm_function *getter;
	struct vm_function *setter;
	struct vm_function *user;
	struct vm_accessor *accessor;
	struct vm_object *object;
	vm_value result;
	vm_value stored;
	uint32_t name;
	int status;

	/* getter(): return the global "seven" (the frame's this is not readable by an instruction yet). */
	memset(&code, 0, sizeof(code));
	name = constant(&code, key("seven"));
	emit(&code, VM_OP_GET_GLOBAL, 0U, name);
	emit(&code, VM_OP_RETURN, 0U);
	getter = finish(&code, "get", 1, 0);

	/* setter(v): global.stored = v. */
	memset(&code, 0, sizeof(code));
	name = constant(&code, key("stored"));
	emit(&code, VM_OP_PUT_GLOBAL, name, 0U);
	emit(&code, VM_OP_RETURN, 0U);
	setter = finish(&code, "set", 1, 1);

	vm_object_define(realm->heap, realm->global, key("seven"), vm_value_int32(7), VM_PROPERTY_DEFAULT);
	object = vm_object_create(realm->heap, realm->object_prototype);
	accessor = vm_accessor_create(realm->heap, vm_value_cell(getter), vm_value_cell(setter));
	vm_object_define(realm->heap, object, key("x"), vm_value_cell(accessor), VM_PROPERTY_ACCESSOR | VM_PROPERTY_CONFIGURABLE);

	/* user(o): o.x = 9; return o.x. */
	memset(&code, 0, sizeof(code));
	name = constant(&code, key("x"));
	emit(&code, VM_OP_LOAD_INT, 1U, 9U);
	emit(&code, VM_OP_PUT_PROP, 0U, name, 1U);
	emit(&code, VM_OP_GET_PROP, 2U, 0U, name);
	emit(&code, VM_OP_RETURN, 2U);
	user = finish(&code, "user", 3, 1);
	check(getter != NULL && setter != NULL && user != NULL, "accessors: assembled");
	if (user == NULL)
		return;
	status = call(user, vm_value_cell(object), &result);
	vm_object_get(realm->global, key("stored"), &stored);
	check(status == 0 && result == vm_value_int32(7), "accessors: the getter's 7");
	check(stored == vm_value_int32(9), "accessors: the setter stored 9");

	/* Reading a property of undefined throws. */
	status = call(user, VM_VALUE_UNDEFINED, &result);
	check(status == VM_THROWN, "accessors: undefined.x throws");
}

/* Strings: "n=" + 42, === on strings, a character by get_elem, new objects and arrays. */
static void
test_strings(void)
{
	struct assembler code;
	struct vm_function *user;
	vm_value result;
	uint32_t prefix;
	int status;

	memset(&code, 0, sizeof(code));
	prefix = constant(&code, string("n="));
	emit(&code, VM_OP_LOAD_CONST, 1U, prefix);
	emit(&code, VM_OP_ADD, 2U, 1U, 0U);
	emit(&code, VM_OP_RETURN, 2U);
	user = finish(&code, "join", 3, 1);
	status = call(user, vm_value_int32(42), &result);
	check(status == 0 && vm_strict_equals(result, string("n=42")), "strings: \"n=\" + 42");
	status = call(user, vm_value_double(0.1), &result);
	check(status == 0 && vm_strict_equals(result, string("n=0.1")), "strings: \"n=\" + 0.1");
	status = call(user, VM_VALUE_TRUE, &result);
	check(status == 0 && vm_strict_equals(result, string("n=true")), "strings: \"n=\" + true");

	/* s[1] of "abc" and s.length. */
	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_LOAD_INT, 1U, 1U);
	emit(&code, VM_OP_GET_ELEM, 2U, 0U, 1U);
	emit(&code, VM_OP_RETURN, 2U);
	user = finish(&code, "at", 3, 1);
	status = call(user, string("abc"), &result);
	check(status == 0 && vm_strict_equals(result, string("b")), "strings: \"abc\"[1] is \"b\"");

	/* An array built with put_elem, and its length read by get_prop. */
	memset(&code, 0, sizeof(code));
	prefix = constant(&code, key("length"));
	emit(&code, VM_OP_NEW_ARRAY, 1U);
	emit(&code, VM_OP_LOAD_INT, 2U, 4U);
	emit(&code, VM_OP_PUT_ELEM, 1U, 2U, 0U);
	emit(&code, VM_OP_GET_PROP, 3U, 1U, prefix);
	emit(&code, VM_OP_RETURN, 3U);
	user = finish(&code, "arr", 4, 1);
	status = call(user, VM_VALUE_NULL, &result);
	check(status == 0 && result == vm_value_int32(5), "arrays: a[4] = x makes length 5");
}

/* f() { return f() } runs out of stack: a RangeError, and the stack is empty again. */
static void
test_limits(void)
{
	struct assembler code;
	struct vm_function *forever;
	struct vm_string *message;
	vm_value result;
	uint32_t name;
	int status;

	memset(&code, 0, sizeof(code));
	name = constant(&code, key("forever"));
	emit(&code, VM_OP_GET_GLOBAL, 1U, name);
	emit(&code, VM_OP_CALL, 2U, 1U, 3U, 0U, 0U);
	emit(&code, VM_OP_RETURN, 2U);
	forever = finish(&code, "forever", 4, 0);
	define_global("forever", forever);
	status = call(forever, VM_VALUE_UNDEFINED, &result);
	message = NULL;
	if (vm_value_is_cell(realm->exception))
		message = (struct vm_string *)vm_value_as_cell(realm->exception);
	check(status == VM_THROWN && message != NULL && vm_string_equal_ascii(message, "RangeError: Maximum call stack size exceeded"),
	    "limits: endless recursion is a RangeError");
	check(realm->stack_top == 0, "limits: the stack is empty after it");
}

/* The checker refuses bad code. */
static void
test_rejected(void)
{
	struct vm_code model;
	struct vm_code *made;
	uint32_t words[16];
	uint32_t bad;
	int error;

	memset(&model, 0, sizeof(model));
	model.words = words;
	model.register_count = 2;

	words[0] = VM_OPCODE_COUNT;
	model.word_count = 1;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL && bad == 0, "check: unknown opcode");

	words[0] = VM_OP_MOV;
	words[1] = 0;
	words[2] = 5;
	words[3] = VM_OP_RETURN;
	words[4] = 0;
	model.word_count = 5;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL && bad == 0, "check: register out of range");

	words[0] = VM_OP_JUMP;
	words[1] = 1;
	words[2] = VM_OP_RETURN;
	words[3] = 0;
	model.word_count = 4;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL, "check: a jump into an instruction's operands");

	words[0] = VM_OP_LOAD_INT;
	words[1] = 0;
	words[2] = 1;
	model.word_count = 3;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL && bad == 3, "check: running off the end");

	words[0] = VM_OP_RETURN;
	model.word_count = 1;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL, "check: an instruction cut short");

	words[0] = VM_OP_CALL;
	words[1] = 0;
	words[2] = 0;
	words[3] = 0;
	words[4] = 1;
	words[5] = 2;
	words[6] = VM_OP_RETURN;
	words[7] = 0;
	model.word_count = 8;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == EINVAL, "check: call arguments past the frame");

	words[0] = VM_OP_LOAD_INT;
	words[1] = 0;
	words[2] = 1;
	words[3] = VM_OP_RETURN;
	words[4] = 0;
	model.word_count = 5;
	error = vm_code_create(realm->heap, &model, &made, &bad);
	check(error == 0 && made != NULL, "check: good code passes");
}

/*
 * Wasm-typed: factorial(n) over raw i32 with br_if, then boxed; an i64
 * add past 32 bits; f64 arithmetic.
 */
static void
test_wasm(void)
{
	struct assembler code;
	struct vm_function *factorial;
	struct vm_function *wide;
	struct vm_function *floating;
	struct wb_buffer dump;
	vm_value result;
	uint32_t head;
	uint32_t branch;
	int status;

	/*
	 * r0: n (boxed int32 from JS, whose low 32 bits are n: the JS-to-Wasm
	 * conversion is the low word here); r1: acc = 1; r2: one; r3: flag.
	 */
	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_I32_CONST, 1U, 1U);
	emit(&code, VM_OP_I32_CONST, 2U, 1U);
	emit(&code, VM_OP_I32_CONST, 4U, 0U);
	emit(&code, VM_OP_I32_ADD, 0U, 0U, 4U);
	head = emit(&code, VM_OP_LOOP_HINT);
	emit(&code, VM_OP_I32_MUL, 1U, 1U, 0U);
	emit(&code, VM_OP_I32_SUB, 0U, 0U, 2U);
	emit(&code, VM_OP_I32_LT_S, 3U, 2U, 0U);
	branch = emit(&code, VM_OP_BR_IF, 3U, 0U);
	patch(&code, branch, 1, head);
	emit(&code, VM_OP_BOX_I32, 5U, 1U);
	emit(&code, VM_OP_RETURN, 5U);
	factorial = finish(&code, "factorial", 6, 1);
	check(factorial != NULL, "wasm: factorial assembled");
	if (factorial == NULL)
		return;
	status = call(factorial, vm_value_int32(10), &result);
	check(status == 0 && result == vm_value_int32(3628800), "wasm: 10! is 3628800");
	status = call(factorial, vm_value_int32(13), &result);
	check(status == 0 && result == vm_value_int32(1932053504), "wasm: 13! wraps in i32");

	/* i64: 0xFFFFFFFF + 1 carries into the high word; boxed through f64 of the value. */
	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_I64_CONST, 0U, 0xFFFFFFFFU, 0U);
	emit(&code, VM_OP_I64_CONST, 1U, 1U, 0U);
	emit(&code, VM_OP_I64_ADD, 2U, 0U, 1U);
	emit(&code, VM_OP_I64_CONST, 3U, 0U, 1U);
	emit(&code, VM_OP_I32_CONST, 4U, 0U);
	emit(&code, VM_OP_I64_ADD, 5U, 2U, 4U);
	emit(&code, VM_OP_RETURN, 5U);
	wide = finish(&code, "wide", 6, 0);
	status = vm_call(realm, vm_value_cell(wide), VM_VALUE_UNDEFINED, NULL, 0, &result);
	check(status == 0 && result == 0x100000000ULL, "wasm: i64 carries (raw result)");

	/* f64: 1.5 * 2.0 + 0.25, boxed. */
	memset(&code, 0, sizeof(code));
	emit(&code, VM_OP_F64_CONST, 0U, 0U, 0x3FF80000U);
	emit(&code, VM_OP_F64_CONST, 1U, 0U, 0x40000000U);
	emit(&code, VM_OP_F64_CONST, 2U, 0U, 0x3FD00000U);
	emit(&code, VM_OP_F64_MUL, 3U, 0U, 1U);
	emit(&code, VM_OP_F64_ADD, 3U, 3U, 2U);
	emit(&code, VM_OP_BOX_F64, 4U, 3U);
	emit(&code, VM_OP_RETURN, 4U);
	floating = finish(&code, "floating", 5, 0);
	status = vm_call(realm, vm_value_cell(floating), VM_VALUE_UNDEFINED, NULL, 0, &result);
	check(status == 0 && vm_value_is_double(result) && vm_value_as_double(result) == 3.25, "wasm: f64 1.5*2+0.25 is 3.25");

	/* The dump names the instructions. */
	wb_buffer_init(&dump);
	vm_code_dump(factorial->code, &dump);
	check(strstr(wb_buffer_string(&dump), "br_if r3 @") != NULL && strstr(wb_buffer_string(&dump), "i32.mul r1 r1 r0") != NULL,
	    "dump: names the instructions");
	wb_buffer_release(&dump);
}

/* A run that makes 200000 objects, keeping the first 2000 in an array, while collections happen. */
static void
test_collection(void)
{
	struct assembler code;
	struct vm_function *maker;
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	vm_value result;
	uint32_t head;
	uint32_t exit;
	uint32_t skip;
	uint32_t name;
	int status;

	/*
	 * r0: n; r1: i; r2: the kept array; r3: the new object; r4: a flag; r5: one; r6: 2000.
	 * Every object gets n = i; the first 2000 are kept in the array, the rest are garbage.
	 */
	memset(&code, 0, sizeof(code));
	name = constant(&code, key("n"));
	emit(&code, VM_OP_NEW_ARRAY, 2U);
	emit(&code, VM_OP_LOAD_INT, 1U, 0U);
	emit(&code, VM_OP_LOAD_INT, 5U, 1U);
	emit(&code, VM_OP_LOAD_INT, 6U, 2000U);
	head = emit(&code, VM_OP_LOOP_HINT);
	emit(&code, VM_OP_LESS, 4U, 1U, 0U);
	exit = emit(&code, VM_OP_JUMP_IF_FALSE, 4U, 0U);
	emit(&code, VM_OP_NEW_OBJECT, 3U);
	emit(&code, VM_OP_PUT_PROP, 3U, name, 1U);
	emit(&code, VM_OP_LESS, 4U, 1U, 6U);
	skip = emit(&code, VM_OP_JUMP_IF_FALSE, 4U, 0U);
	emit(&code, VM_OP_PUT_ELEM, 2U, 1U, 3U);
	patch(&code, skip, 1, code.count);
	emit(&code, VM_OP_ADD, 1U, 1U, 5U);
	patch(&code, emit(&code, VM_OP_JUMP, 0U), 0, head);
	patch(&code, exit, 1, code.count);
	emit(&code, VM_OP_RETURN, 2U);
	maker = finish(&code, "maker", 9, 1);
	check(maker != NULL, "collection: assembled");
	if (maker == NULL)
		return;
	vm_heap_stats(realm->heap, &before);
	status = call(maker, vm_value_int32(200000), &result);
	vm_heap_stats(realm->heap, &after);
	check(status == 0 && vm_value_is_cell(result), "collection: the run finished");
	if (status == 0) {
		struct vm_object *array;
		vm_value item;
		vm_value n;
		int ok;
		int index;

		array = (struct vm_object *)vm_value_as_cell(result);
		ok = array->length == 2000;
		for (index = 0; ok && index < 2000; index++) {
			vm_object_get(array, vm_value_int32(index), &item);
			vm_object_get((struct vm_object *)vm_value_as_cell(item), key("n"), &n);
			if (n != vm_value_int32(index))
				ok = 0;
		}
		check(ok, "collection: the kept objects survive with their values");
	}
	check(after.collections > before.collections, "collection: collections ran during the run");
	printf("host-interp: the run collected %zu times, %zu live cells\n", after.collections - before.collections, after.live_cells);
}

static int
native_twice(
	struct vm_realm *realm_of,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	(void)realm_of;
	(void)this_value;
	if (count < 1) {
		*result = vm_value_double(NAN);
		return 0;
	}
	*result = vm_value_number(vm_value_as_number(args[0]) * 2.0);
	return 0;
}

static int
native_apply(
	struct vm_realm *realm_of,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	(void)this_value;
	if (count < 2)
		return vm_throw_type_error(realm_of, "apply needs a function and a value");
	return vm_call(realm_of, args[0], VM_VALUE_UNDEFINED, &args[1], 1, result);
}
