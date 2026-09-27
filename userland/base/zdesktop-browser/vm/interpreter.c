/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The interpreter (plan/ws074/design.md §11.5, §11.6): runs checked code
 * units on the realm's VM stack.
 *
 * A frame is FRAME_HEADER slots followed by the function's registers.  The
 * header says where the caller's frame is (0 for the frame a run was
 * entered with), where the caller goes on, which function runs, which of
 * the caller's registers takes the result, the this value and the number
 * of arguments.  A call from bytecode to bytecode pushes a frame and a
 * return pops it, both inside one loop, so script recursion does not
 * recurse in C; a native function runs as a C call, and when it calls a
 * script function the interpreter is entered again (vm_interpret), with
 * its own entry frame, up to INTERPRETER_DEPTH_MAX times.
 *
 * A thrown exception (VM_THROWN) unwinds through the frames of the run:
 * the first handler whose range holds the throwing instruction (for a
 * caller, its call instruction) takes it; a run whose entry frame has no
 * handler returns VM_THROWN to its C caller.  Any other failure (out of
 * memory) ends the run at once.
 */

#include "vm/bytecode.h"
#include "vm/internal.h"

#include <errno.h>
#include <string.h>

/* The header's slots. */
#define FRAME_CALLER		0U
#define FRAME_RETURN		1U
#define FRAME_FUNCTION		2U
#define FRAME_RESULT		3U
#define FRAME_THIS		4U
#define FRAME_ARGC		5U
#define FRAME_HEADER		6U

/* How many times the interpreter may be entered from C at once. */
#define INTERPRETER_DEPTH_MAX	256U

/* The words of a call instruction (the opcode and five operands). */
#define INTERPRETER_CALL_WORDS	6U

/* What a step reports when the run's entry frame returned. */
#define INTERPRETER_DONE	(-2)

/*
 * One run of the interpreter: the realm, the base of the run's entry frame
 * and of the frame running, the offset of the instruction to run in that
 * frame's code, and the value the entry frame returned.
 */
struct interpreter {
	struct vm_realm *realm;
	uint32_t entry;
	uint32_t base;
	uint32_t pc;
	struct vm_code *code;
	vm_value result;
};

static int interpreter_push(struct vm_realm *realm, struct vm_function *function, vm_value this_value, const vm_value *args, unsigned count, uint32_t caller, uint32_t return_pc, uint32_t result_register, uint32_t *base);
static int interpreter_run(struct interpreter *run);
static int interpreter_step(struct interpreter *run);
static int interpreter_step_js(struct interpreter *run, const uint32_t *words, vm_value *registers);
static int interpreter_step_wasm(struct interpreter *run, const uint32_t *words, vm_value *registers);
static int interpreter_call(struct interpreter *run, const uint32_t *words, vm_value *registers, uint32_t next);
static int interpreter_return(struct interpreter *run, vm_value value);
static int interpreter_unwind(struct interpreter *run);
static struct vm_function *interpreter_function(const struct vm_realm *realm, uint32_t base);
static double interpreter_f64(vm_value bits);
static vm_value interpreter_bits(double number);

/*
 * Runs a bytecode function with a this value and arguments, entering the
 * interpreter from C; stores the result and returns 0, VM_THROWN with the
 * realm's exception set, or an errno value.
 */
int
vm_interpret(
	struct vm_realm *realm,
	struct vm_function *function,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct interpreter run;
	int status;

	/* Too many entries from C is a stack overflow for the script. */
	*result = VM_VALUE_UNDEFINED;
	if (realm->depth >= INTERPRETER_DEPTH_MAX) {
		status = vm_throw_range_error(realm, "Maximum call stack size exceeded");
		return status;
	}

	/* The entry frame: no caller. */
	memset(&run, 0, sizeof(run));
	run.realm = realm;
	run.entry = realm->stack_top;
	status = interpreter_push(realm, function, this_value, args, count, 0, 0, 0, &run.base);
	if (status != 0)
		return status;
	run.code = function->code;
	run.pc = 0;

	/* Runs it, one more entry from C while it runs. */
	realm->depth++;
	status = interpreter_run(&run);
	realm->depth--;
	if (status != 0)
		return status;

	/* Succeeded: the function's result. */
	*result = run.result;
	return 0;
}

/*
 * Pushes a frame for a bytecode function: the header, the arguments in
 * the first registers (undefined for the missing ones) and every other
 * register undefined.  caller is the caller frame's base plus one (0 for
 * an entry frame).
 */
static int
interpreter_push(
	struct vm_realm *realm,
	struct vm_function *function,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	uint32_t caller,
	uint32_t return_pc,
	uint32_t result_register,
	uint32_t *base)
{
	struct vm_code *code;
	vm_value *frame;
	uint32_t size;
	uint32_t index;
	int status;

	/* A frame that does not fit is a stack overflow for the script. */
	code = function->code;
	size = FRAME_HEADER + code->register_count;
	if (size > realm->stack_capacity - realm->stack_top) {
		status = vm_throw_range_error(realm, "Maximum call stack size exceeded");
		return status;
	}

	/* The frame goes on top of the stack. */
	*base = realm->stack_top;
	frame = &realm->stack[*base];
	realm->stack_top += size;

	/* The header. */
	frame[FRAME_CALLER] = caller;
	frame[FRAME_RETURN] = return_pc;
	frame[FRAME_FUNCTION] = vm_value_cell(function);
	frame[FRAME_RESULT] = result_register;
	frame[FRAME_THIS] = this_value;
	frame[FRAME_ARGC] = count;

	/* The registers: the arguments, then undefined. */
	for (index = 0; index < code->register_count; index++) {
		frame[FRAME_HEADER + index] = VM_VALUE_UNDEFINED;
		if (index < code->parameter_count && index < count)
			frame[FRAME_HEADER + index] = args[index];
	}

	/* Succeeded: the frame is pushed. */
	return 0;
}

/* Runs instructions until the entry frame returns, an exception leaves the run, or something fails. */
static int
interpreter_run(
	struct interpreter *run)
{
	int status;

	/* One instruction after another. */
	for (;;) {
		status = interpreter_step(run);
		if (status == 0)
			continue;

		/* The entry frame returned. */
		if (status == INTERPRETER_DONE)
			return 0;

		/* An exception looks for a handler; without one it leaves the run. */
		if (status == VM_THROWN) {
			status = interpreter_unwind(run);
			if (status == 0)
				continue;
			return VM_THROWN;
		}

		/* Anything else ends the run and drops its frames. */
		run->realm->stack_top = run->entry;
		return status;
	}
}

/* Runs one instruction: the shared ones here, the others by their group. */
static int
interpreter_step(
	struct interpreter *run)
{
	const uint32_t *words;
	vm_value *registers;
	uint32_t next;
	int truth;
	int status;

	/* The instruction, the offset after it and the frame's registers. */
	words = run->code->words + run->pc;
	next = run->pc + 1U + vm_opcodes[words[0]].operand_count;
	registers = &run->realm->stack[run->base + FRAME_HEADER];

	/* The shared instructions. */
	switch (words[0]) {
	case VM_OP_NOP:
	case VM_OP_LOOP_HINT:
		run->pc = next;
		return 0;
	case VM_OP_MOV:
		registers[words[1]] = registers[words[2]];
		run->pc = next;
		return 0;
	case VM_OP_LOAD_CONST:
		registers[words[1]] = run->code->constants[words[2]];
		run->pc = next;
		return 0;
	case VM_OP_LOAD_INT:
		registers[words[1]] = vm_value_int32((int32_t)words[2]);
		run->pc = next;
		return 0;
	case VM_OP_JUMP:
		run->pc += words[1];
		return 0;
	case VM_OP_JUMP_IF_TRUE:
	case VM_OP_JUMP_IF_FALSE:
		/* The jump is taken when the truth is the one the instruction names. */
		truth = vm_to_boolean(registers[words[1]]);
		if (words[0] == VM_OP_JUMP_IF_FALSE)
			truth = !truth;
		if (truth) {
			run->pc += words[2];
		} else {
			run->pc = next;
		}

		/* The run goes on from there. */
		return 0;
	case VM_OP_CALL:
		status = interpreter_call(run, words, registers, next);
		return status;
	case VM_OP_RETURN:
		status = interpreter_return(run, registers[words[1]]);
		return status;
	case VM_OP_THROW:
		status = vm_throw(run->realm, registers[words[1]]);
		return status;
	default:
		break;
	}

	/* The JavaScript instructions. */
	if (words[0] < VM_OP_I32_CONST) {
		status = interpreter_step_js(run, words, registers);
		if (status != 0)
			return status;
		run->pc = next;
		return 0;
	}

	/* The Wasm instructions. */
	status = interpreter_step_wasm(run, words, registers);
	if (status != 0)
		return status;

	/* Succeeded: br_if set the pc itself when it jumped. */
	return 0;
}

/* Runs one JavaScript instruction on boxed values; the pc is moved by the caller. */
static int
interpreter_step_js(
	struct interpreter *run,
	const uint32_t *words,
	vm_value *registers)
{
	struct vm_realm *realm;
	struct vm_object *object;
	vm_value value;
	vm_value key;
	double left;
	double right;
	int status;

	/* Each instruction's result is written only when it succeeds. */
	realm = run->realm;
	switch (words[0]) {
	case VM_OP_ADD:
		status = vm_add(realm, registers[words[2]], registers[words[3]], &value);
		break;
	case VM_OP_SUB:
	case VM_OP_MUL:
		/* Both sides as numbers. */
		status = vm_to_number(realm, registers[words[2]], &left);
		if (status == 0)
			status = vm_to_number(realm, registers[words[3]], &right);
		if (status == 0 && words[0] == VM_OP_SUB)
			value = vm_value_number(left - right);
		if (status == 0 && words[0] == VM_OP_MUL)
			value = vm_value_number(left * right);
		break;
	case VM_OP_LESS:
		status = vm_less(realm, registers[words[2]], registers[words[3]], &value);
		break;
	case VM_OP_STRICT_EQ:
		value = vm_value_boolean(vm_strict_equals(registers[words[2]], registers[words[3]]));
		status = 0;
		break;
	case VM_OP_NEW_OBJECT:
		/* A plain object from Object.prototype. */
		object = vm_object_create(realm->heap, realm->object_prototype);
		status = ENOMEM;
		if (object != NULL) {
			value = vm_value_cell(object);
			status = 0;
		}

		/* The object, or out of memory. */
		break;
	case VM_OP_NEW_ARRAY:
		/* An empty array from Array.prototype. */
		object = vm_array_create(realm->heap, realm->array_prototype);
		status = ENOMEM;
		if (object != NULL) {
			value = vm_value_cell(object);
			status = 0;
		}

		/* The array, or out of memory. */
		break;
	case VM_OP_GET_PROP:
		status = vm_get(realm, registers[words[2]], run->code->constants[words[3]], &value);
		break;
	case VM_OP_PUT_PROP:
		/* Nothing is written to a register. */
		status = vm_put(realm, registers[words[1]], run->code->constants[words[2]], registers[words[3]]);
		return status;
	case VM_OP_GET_ELEM:
		/* The key is converted first. */
		status = vm_to_key(realm, registers[words[3]], &key);
		if (status == 0)
			status = vm_get(realm, registers[words[2]], key, &value);
		break;
	case VM_OP_PUT_ELEM:
		/* The key is converted first; nothing is written to a register. */
		status = vm_to_key(realm, registers[words[2]], &key);
		if (status == 0)
			status = vm_put(realm, registers[words[1]], key, registers[words[3]]);
		return status;
	case VM_OP_GET_GLOBAL:
		status = vm_get(realm, vm_value_cell(realm->global), run->code->constants[words[2]], &value);
		if (status == 0) {
			registers[words[1]] = value;
			return 0;
		}

		/* The global could not be read. */
		return status;
	case VM_OP_PUT_GLOBAL:
		/* Nothing is written to a register. */
		status = vm_put(realm, vm_value_cell(realm->global), run->code->constants[words[1]], registers[words[2]]);
		return status;
	default:
		return EINVAL;
	}

	/* A failed instruction writes nothing. */
	if (status != 0)
		return status;

	/* Succeeded: the result in the first operand's register. */
	registers[words[1]] = value;
	return 0;
}

/* Runs one Wasm instruction on raw values, moving the pc itself. */
static int
interpreter_step_wasm(
	struct interpreter *run,
	const uint32_t *words,
	vm_value *registers)
{
	uint32_t start;
	uint32_t left;
	uint32_t right;

	/* The next instruction, unless a branch is taken (jumps count from the instruction's start). */
	start = run->pc;
	run->pc = start + 1U + vm_opcodes[words[0]].operand_count;

	/* Each instruction; an i32 is the low 32 bits, zero-extended. */
	switch (words[0]) {
	case VM_OP_I32_CONST:
		registers[words[1]] = (uint64_t)words[2];
		break;
	case VM_OP_I32_ADD:
		registers[words[1]] = (uint64_t)(uint32_t)((uint32_t)registers[words[2]] + (uint32_t)registers[words[3]]);
		break;
	case VM_OP_I32_SUB:
		registers[words[1]] = (uint64_t)(uint32_t)((uint32_t)registers[words[2]] - (uint32_t)registers[words[3]]);
		break;
	case VM_OP_I32_MUL:
		registers[words[1]] = (uint64_t)(uint32_t)((uint32_t)registers[words[2]] * (uint32_t)registers[words[3]]);
		break;
	case VM_OP_I32_LT_S:
		/* Signed: the two as int32. */
		left = (uint32_t)registers[words[2]];
		right = (uint32_t)registers[words[3]];
		registers[words[1]] = 0U;
		if ((int32_t)left < (int32_t)right)
			registers[words[1]] = 1U;
		break;
	case VM_OP_I32_EQZ:
		registers[words[1]] = 0U;
		if ((uint32_t)registers[words[2]] == 0U)
			registers[words[1]] = 1U;
		break;
	case VM_OP_BR_IF:
		/* Taken when the i32 is not zero. */
		if ((uint32_t)registers[words[1]] != 0U)
			run->pc = start + words[2];
		break;
	case VM_OP_I64_CONST:
	case VM_OP_F64_CONST:
		/* The low and high words of the bits. */
		registers[words[1]] = (uint64_t)words[2] | ((uint64_t)words[3] << 32);
		break;
	case VM_OP_I64_ADD:
		registers[words[1]] = registers[words[2]] + registers[words[3]];
		break;
	case VM_OP_F64_ADD:
		registers[words[1]] = interpreter_bits(interpreter_f64(registers[words[2]]) + interpreter_f64(registers[words[3]]));
		break;
	case VM_OP_F64_MUL:
		registers[words[1]] = interpreter_bits(interpreter_f64(registers[words[2]]) * interpreter_f64(registers[words[3]]));
		break;
	case VM_OP_BOX_I32:
		registers[words[1]] = vm_value_int32((int32_t)(uint32_t)registers[words[2]]);
		break;
	case VM_OP_BOX_F64:
		registers[words[1]] = vm_value_number(interpreter_f64(registers[words[2]]));
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the instruction ran. */
	return 0;
}

/*
 * Calls a function from bytecode: a bytecode callee gets a frame and runs
 * next; a native one runs now and its result goes to the register.
 */
static int
interpreter_call(
	struct interpreter *run,
	const uint32_t *words,
	vm_value *registers,
	uint32_t next)
{
	struct vm_function *function;
	vm_value callee;
	vm_value value;
	uint32_t base;
	int callable;
	int status;

	/* Only functions can be called. */
	callee = registers[words[2]];
	callable = vm_value_is_callable(callee);
	if (!callable) {
		status = vm_throw_type_error(run->realm, "value is not a function");
		return status;
	}

	/* A bytecode function: its frame, whose caller goes on after the call. */
	function = (struct vm_function *)vm_value_as_cell(callee);
	if (function->code != NULL) {
		status = interpreter_push(run->realm, function, registers[words[3]], &registers[words[4]], words[5], run->base + 1U, next,
		    words[1], &base);
		if (status != 0)
			return status;
		run->base = base;
		run->code = function->code;
		run->pc = 0;
		return 0;
	}

	/* A native function runs now, in its own realm. */
	status = function->native(function->realm, registers[words[3]], &registers[words[4]], words[5], &value);
	if (status != 0)
		return status;

	/* Succeeded: the result is in the register and the caller goes on. */
	registers[words[1]] = value;
	run->pc = next;
	return 0;
}

/* Returns from the running frame: to its caller's register, or out of the run from the entry frame. */
static int
interpreter_return(
	struct interpreter *run,
	vm_value value)
{
	struct vm_realm *realm;
	struct vm_function *function;
	vm_value *frame;
	uint32_t caller;
	uint32_t result_register;
	uint32_t return_pc;

	/* The frame's header, read before the frame is popped. */
	realm = run->realm;
	frame = &realm->stack[run->base];
	caller = (uint32_t)frame[FRAME_CALLER];
	result_register = (uint32_t)frame[FRAME_RESULT];
	return_pc = (uint32_t)frame[FRAME_RETURN];
	realm->stack_top = run->base;

	/* The entry frame's return ends the run. */
	if (caller == 0U) {
		run->result = value;
		return INTERPRETER_DONE;
	}

	/* The caller goes on after its call with the value in its register. */
	run->base = caller - 1U;
	function = interpreter_function(realm, run->base);
	run->code = function->code;
	run->pc = return_pc;
	realm->stack[run->base + FRAME_HEADER + result_register] = value;

	/* Succeeded: the caller runs next. */
	return 0;
}

/*
 * Finds the handler of the realm's exception from the running
 * instruction outwards through the run's frames; 0 when one takes it (the
 * run goes on there), VM_THROWN when the run's entry frame has none.
 */
static int
interpreter_unwind(
	struct interpreter *run)
{
	struct vm_realm *realm;
	struct vm_function *function;
	const struct vm_handler *handler;
	uint32_t throw_pc;
	uint32_t caller;
	uint32_t index;

	/* From the instruction that threw, frame by frame. */
	realm = run->realm;
	throw_pc = run->pc;
	for (;;) {
		/* A handler of this frame whose range holds the instruction. */
		for (index = 0; index < run->code->handler_count; index++) {
			handler = &run->code->handlers[index];
			if (throw_pc < handler->start || throw_pc >= handler->end)
				continue;

			/* It takes the exception, and the frame goes on there. */
			realm->stack[run->base + FRAME_HEADER + handler->exception_register] = realm->exception;
			realm->exception = VM_VALUE_UNDEFINED;
			run->pc = handler->handler;
			return 0;
		}

		/* Without one, the frame is popped; the entry frame's end leaves the run. */
		caller = (uint32_t)realm->stack[run->base + FRAME_CALLER];
		throw_pc = (uint32_t)realm->stack[run->base + FRAME_RETURN] - INTERPRETER_CALL_WORDS;
		realm->stack_top = run->base;
		if (caller == 0U)
			return VM_THROWN;

		/* The caller's call instruction is where the search goes on. */
		run->base = caller - 1U;
		function = interpreter_function(realm, run->base);
		run->code = function->code;
	}
}

/* Reports the function a frame runs. */
static struct vm_function *
interpreter_function(
	const struct vm_realm *realm,
	uint32_t base)
{
	/* The header's function slot holds its cell. */
	return (struct vm_function *)vm_value_as_cell(realm->stack[base + FRAME_FUNCTION]);
}

/* Reads a raw f64's bits as a double. */
static double
interpreter_f64(
	vm_value bits)
{
	double number;

	/* The same 64 bits. */
	memcpy(&number, &bits, sizeof(number));

	/* Reports the double. */
	return number;
}

/* Writes a double as a raw f64's bits. */
static vm_value
interpreter_bits(
	double number)
{
	vm_value bits;

	/* The same 64 bits. */
	memcpy(&bits, &number, sizeof(bits));

	/* Reports the bits. */
	return bits;
}
