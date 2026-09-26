/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The core of the AML evaluator: term lists, control flow, TermArgs,
 * targets and the Store rules, and method invocation.
 *
 * The evaluator walks the AML bytes directly by recursive descent; nothing
 * is parsed ahead.  A method body is interpreted from its first byte each
 * time the method runs.  C recursion follows the nesting of the AML, so
 * every recursive step checks the stack budget (drv_acpi_stack_check()).
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * How long one While loop may run, in 100-nanosecond timer units: thirty
 * seconds, after which firmware that waits for hardware that never answers
 * is abandoned.
 */
#define WHILE_TIMEOUT_TICKS (30ULL * 10000000ULL)

/*
 * The deepest the stack has been below an entry's frame, for measurement.
 * It only grows, and only the holder of the interpreter lock writes it.
 */
static size_t stack_deepest;

static int exec_term(struct drv_acpi_eval *eval);
static int exec_block(struct drv_acpi_eval *eval, const uint8_t *block_end);
static int exec_if(struct drv_acpi_eval *eval);
static int exec_while(struct drv_acpi_eval *eval);
static int exec_return(struct drv_acpi_eval *eval);
static int eval_name(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int eval_method_call(struct drv_acpi_eval *eval, struct drv_acpi_node *node, struct drv_acpi_object **result);
static int eval_constant(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int eval_local(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int store_local(struct drv_acpi_object **slot, struct drv_acpi_object *value);
static int store_node(struct drv_acpi_eval *eval, struct drv_acpi_node *node, struct drv_acpi_object *value);
static int store_reference(struct drv_acpi_eval *eval, struct drv_acpi_object *reference, struct drv_acpi_object *value);
static int store_index(struct drv_acpi_object *reference, struct drv_acpi_object *value);
static int store_buffer_into(struct drv_acpi_object *target, struct drv_acpi_object *value);
static int parse_reference_target(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_target *target);
static void debug_store(struct drv_acpi_object *value);
static void frame_release(struct drv_acpi_frame *frame);
static int serialize(struct drv_acpi_eval *eval, struct drv_acpi_object *method, bool acquire);
static void report_name(const char *what, const struct drv_acpi_name *name, int error);

/*
 * Evaluates a named object for a driver: a method is invoked with the
 * arguments, anything else is read.
 *
 * path is relative to scope, or NULL for scope itself.  The result is
 * NULL when a method returned nothing.
 */
int
drv_acpi_evaluate(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_object **arguments,
	unsigned argument_count,
	struct drv_acpi_object **result)
{
	struct drv_acpi_thread storage;
	struct drv_acpi_thread *thread;
	struct drv_acpi_eval entry;
	struct drv_acpi_node *node;
	int error;

	/* Enters the interpreter, measuring the stack from here. */
	*result = NULL;
	thread = drv_acpi_enter(&storage, __builtin_frame_address(0));

	/* Finds the object: the scope itself, or the path from it. */
	node = scope;
	error = 0;
	if (path != NULL)
		error = drv_acpi_lookup(scope, path, &node);
	if (error == 0 && node == NULL)
		error = ENOENT;

	/* Invokes a method or reads anything else, as the entered thread. */
	if (error == 0) {
		node = drv_acpi_ns_resolve_alias(node);
		kern_memset(&entry, 0, sizeof(entry));
		entry.thread = thread;
		if (node->object != NULL && node->object->type == DRV_ACPI_TYPE_METHOD) {
			error = drv_acpi_invoke(&entry, node, arguments, argument_count, result);
		} else {
			error = drv_acpi_read_node(&entry, node, result);
		}
	}

	/* Leaves the interpreter and reports a failed evaluation. */
	drv_acpi_leave(thread);
	if (error != 0)
		return error;

	/* Succeeded: the caller holds the result, if any. */
	return 0;
}

/*
 * Evaluates a named object that must produce an integer.
 */
int
drv_acpi_evaluate_integer(
	struct drv_acpi_node *scope,
	const char *path,
	uint64_t *value)
{
	struct drv_acpi_object *object;
	int error;

	/* Evaluates the object without arguments. */
	error = drv_acpi_evaluate(scope, path, NULL, 0, &object);
	if (error != 0)
		return error;

	/* Refuses a method that returned nothing. */
	if (object == NULL)
		return EINVAL;

	/* Converts the value by the implicit rules. */
	error = drv_acpi_convert_integer(object, value);
	drv_acpi_object_release(object);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Runs the terms of the current term list until its end or a control
 * transfer.
 */
int
drv_acpi_exec_term_list(
	struct drv_acpi_eval *eval)
{
	int error;

	/* Runs one term at a time while nothing has transferred control. */
	while (eval->position < eval->end) {
		/* Runs the next term. */
		error = exec_term(eval);
		if (error != 0)
			return error;

		/* Stops when a Return, Break or Continue leaves this list. */
		if (eval->control != DRV_ACPI_CONTROL_NEXT)
			return 0;
	}

	/* Succeeded: the list ran to its end. */
	return 0;
}

/*
 * Runs a term list that ends at end in the scope of another node, as the
 * body of a Scope or a Device does, then continues after it.
 */
int
drv_acpi_exec_scope(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *scope,
	const uint8_t *end)
{
	struct drv_acpi_node *outer_scope;
	int error;

	/* Runs the list with the node as the scope. */
	outer_scope = eval->scope;
	eval->scope = scope;
	error = exec_block(eval, end);
	eval->scope = outer_scope;
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Evaluates one TermArg and reports the object it produced.
 *
 * The caller receives one reference.  A name that resolves to a method is
 * invoked; a name that resolves to a field is read.
 */
int
drv_acpi_eval_term_arg(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	const uint8_t *start;
	unsigned opcode;
	bool named;
	int error;

	/* Refuses to go deeper than the stack budget allows. */
	error = drv_acpi_stack_check();
	if (error != 0)
		return error;

	/* A name is a reference to an object or a method invocation. */
	named = drv_acpi_stream_at_name(eval);
	if (named) {
		error = eval_name(eval, result);
		return error;
	}

	/* Reads the opcode. */
	start = eval->position;
	error = drv_acpi_stream_opcode(eval, &opcode);
	if (error != 0)
		return error;

	/* Chooses the evaluation by the kind of opcode. */
	switch (opcode) {
	case DRV_ACPI_OP_ZERO:
	case DRV_ACPI_OP_ONE:
	case DRV_ACPI_OP_ONES:
	case DRV_ACPI_OP_BYTE_PREFIX:
	case DRV_ACPI_OP_WORD_PREFIX:
	case DRV_ACPI_OP_DWORD_PREFIX:
	case DRV_ACPI_OP_QWORD_PREFIX:
	case DRV_ACPI_OP_STRING_PREFIX:
	case DRV_ACPI_OP_REVISION:
		error = eval_constant(eval, opcode, result);
		return error;
	case DRV_ACPI_OP_BUFFER:
		error = drv_acpi_build_buffer(eval, result);
		return error;
	case DRV_ACPI_OP_PACKAGE:
		error = drv_acpi_build_package(eval, false, result);
		return error;
	case DRV_ACPI_OP_VAR_PACKAGE:
		error = drv_acpi_build_package(eval, true, result);
		return error;
	default:
		break;
	}

	/* Locals and arguments hold objects of the running invocation. */
	if (opcode >= DRV_ACPI_OP_LOCAL0 && opcode <= DRV_ACPI_OP_ARG6) {
		error = eval_local(eval, opcode, result);
		return error;
	}

	/* Everything else is an operator that computes a value. */
	error = drv_acpi_eval_operator(eval, opcode, result);
	if (error == ENOSYS) {
		/* No operator has the opcode: the table is damaged or newer than the interpreter. */
		drv_acpi_os_log(
			"ACPI: unknown opcode 0x%x at offset 0x%lx\n",
			opcode,
			(unsigned long)(start - eval->table->data));
		return EIO;
	} else if (error != 0) {
		return error;
	}

	/* Succeeded: the caller holds the value. */
	return 0;
}

/*
 * Evaluates a TermArg and converts its value to an integer.
 */
int
drv_acpi_eval_integer(
	struct drv_acpi_eval *eval,
	uint64_t *value)
{
	struct drv_acpi_object *object;
	int error;

	/* Evaluates the operand as data. */
	error = drv_acpi_eval_data(eval, &object);
	if (error != 0)
		return error;

	/* Converts it by the implicit rules. */
	error = drv_acpi_convert_integer(object, value);
	drv_acpi_object_release(object);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Evaluates a TermArg and resolves references to the data they point at.
 *
 * An operand that computes (Add, Concatenate, ...) needs the value an
 * Index reference or a local's object reference stands for, not the
 * reference itself.
 */
int
drv_acpi_eval_data(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	struct drv_acpi_object *resolved;
	int error;

	/* Evaluates the operand. */
	error = drv_acpi_eval_term_arg(eval, &object);
	if (error != 0)
		return error;

	/* Anything but a reference is data already. */
	if (object->type != DRV_ACPI_TYPE_REFERENCE) {
		*result = object;
		return 0;
	}

	/* Resolves the reference by its kind. */
	resolved = NULL;
	switch (object->value.reference.kind) {
	case DRV_ACPI_REFERENCE_OBJECT:
		resolved = object->value.reference.target;
		drv_acpi_object_ref(resolved);
		error = 0;
		break;
	case DRV_ACPI_REFERENCE_INDEX:
		error = drv_acpi_index_read(eval, object, &resolved);
		break;
	case DRV_ACPI_REFERENCE_NODE:
		error = drv_acpi_read_node(eval, object->value.reference.node, &resolved);
		break;
	default:
		error = EINVAL;
		break;
	}

	/* The reference is no longer needed. */
	drv_acpi_object_release(object);
	if (error != 0)
		return error;

	/* Succeeded: the caller holds the value. */
	*result = resolved;
	return 0;
}

/*
 * Parses a SuperName or Target: where a result is to be stored.
 */
int
drv_acpi_parse_target(
	struct drv_acpi_eval *eval,
	struct drv_acpi_target *target)
{
	struct drv_acpi_name name;
	struct drv_acpi_node *node;
	unsigned opcode;
	uint8_t byte;
	bool named;
	int error;

	/* Starts with a target that stores nowhere. */
	kern_memset(target, 0, sizeof(*target));

	/*
	 * A name is a namespace node.  A method named here is not invoked:
	 * ObjectType, SizeOf, RefOf and the stores all name it as an object.
	 */
	named = drv_acpi_stream_at_name(eval);
	if (named) {
		error = drv_acpi_stream_name(eval, &name);
		if (error != 0)
			return error;

		/* Resolves the name with the search rules of references. */
		error = drv_acpi_ns_lookup(eval->scope, &name, true, &node);
		if (error != 0) {
			report_name("target", &name, error);
			return error;
		}

		/* An alias stands for the node it names. */
		node = drv_acpi_ns_resolve_alias(node);

		/* Succeeded: the target is the node. */
		target->kind = DRV_ACPI_TARGET_NODE;
		target->node = node;
		return 0;
	}

	/* The null name discards the result. */
	error = drv_acpi_stream_peek(eval, &byte);
	if (error != 0)
		return error;
	if (byte == 0x00U) {
		eval->position++;
		return 0;
	}

	/* Reads the opcode of any other target. */
	error = drv_acpi_stream_opcode(eval, &opcode);
	if (error != 0)
		return error;

	/* A local of the running invocation. */
	if (opcode >= DRV_ACPI_OP_LOCAL0 && opcode <= DRV_ACPI_OP_LOCAL7) {
		/* Refuses a local outside any method. */
		if (eval->frame == NULL)
			return EINVAL;
		target->kind = DRV_ACPI_TARGET_LOCAL;
		target->index = opcode - DRV_ACPI_OP_LOCAL0;
		return 0;
	}

	/* An argument of the running invocation. */
	if (opcode >= DRV_ACPI_OP_ARG0 && opcode <= DRV_ACPI_OP_ARG6) {
		/* Refuses an argument outside any method. */
		if (eval->frame == NULL)
			return EINVAL;
		target->kind = DRV_ACPI_TARGET_ARGUMENT;
		target->index = opcode - DRV_ACPI_OP_ARG0;
		return 0;
	}

	/* The debug object logs what is stored into it. */
	if (opcode == DRV_ACPI_OP_DEBUG) {
		target->kind = DRV_ACPI_TARGET_DEBUG;
		return 0;
	}

	/* RefOf, DerefOf and Index produce a reference to store through. */
	error = parse_reference_target(eval, opcode, target);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Releases what a parsed target holds.
 */
void
drv_acpi_target_release(
	struct drv_acpi_target *target)
{
	/* Only a reference target holds anything. */
	drv_acpi_object_release(target->reference);
	target->reference = NULL;
}

/*
 * Stores a value into a target by the rules of the Store operator.
 */
int
drv_acpi_store(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *value,
	struct drv_acpi_target *target)
{
	struct drv_acpi_object *argument;
	int error;

	/* Chooses the store by where the target is. */
	switch (target->kind) {
	case DRV_ACPI_TARGET_NONE:
		return 0;
	case DRV_ACPI_TARGET_DEBUG:
		debug_store(value);
		return 0;
	case DRV_ACPI_TARGET_LOCAL:
		error = store_local(&eval->frame->locals[target->index], value);
		return error;
	case DRV_ACPI_TARGET_ARGUMENT:
		argument = eval->frame->arguments[target->index];

		/* An argument that holds a reference is stored through. */
		if (argument != NULL && argument->type == DRV_ACPI_TYPE_REFERENCE) {
			error = store_reference(eval, argument, value);
			return error;
		}

		/* Any other argument is replaced like a local. */
		error = store_local(&eval->frame->arguments[target->index], value);
		return error;
	case DRV_ACPI_TARGET_NODE:
		error = store_node(eval, target->node, value);
		return error;
	case DRV_ACPI_TARGET_REFERENCE:
		error = store_reference(eval, target->reference, value);
		return error;
	default:
		break;
	}

	/* Reports a target kind that does not exist. */
	return EINVAL;
}

/*
 * Invokes a control method.
 *
 * The arguments are shared with the invocation, which adds its own
 * references.  The result is what the method returned, or NULL when it
 * returned nothing.
 */
int
drv_acpi_invoke(
	struct drv_acpi_eval *caller,
	struct drv_acpi_node *node,
	struct drv_acpi_object **arguments,
	unsigned argument_count,
	struct drv_acpi_object **result)
{
	struct drv_acpi_method *method;
	struct drv_acpi_frame *frame;
	struct drv_acpi_eval eval;
	unsigned depth;
	unsigned index;
	int error;

	/* Refuses anything but a method. */
	*result = NULL;
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_METHOD)
		return EINVAL;
	method = &node->object->value.method;

	/* Refuses more arguments than a method can have. */
	if (argument_count > DRV_ACPI_ARGUMENT_COUNT)
		return EINVAL;

	/* A native method runs in C. */
	if (method->native != NULL) {
		error = method->native(caller, arguments, argument_count, result);
		return error;
	}

	/* Refuses an invocation nested deeper than the interpreter allows. */
	depth = 0;
	if (caller != NULL && caller->frame != NULL)
		depth = caller->frame->depth + 1U;
	if (depth >= DRV_ACPI_CALL_DEPTH_MAX)
		return E2BIG;

	/* Allocates the invocation's locals and arguments. */
	frame = drv_acpi_os_alloc(sizeof(*frame));
	if (frame == NULL)
		return ENOMEM;
	kern_memset(frame, 0, sizeof(*frame));
	frame->method = node;
	frame->depth = depth;
	if (caller != NULL)
		frame->caller = caller->frame;

	/* Gives the invocation its own reference to each argument. */
	for (index = 0; index < argument_count; index++) {
		frame->arguments[index] = arguments[index];
		if (arguments[index] != NULL)
			drv_acpi_object_ref(arguments[index]);
	}

	/* Runs the body in the scope of the method's node, as the caller's thread. */
	kern_memset(&eval, 0, sizeof(eval));
	eval.position = method->start;
	eval.end = method->end;
	eval.scope = node;
	eval.frame = frame;
	eval.table = method->table;
	eval.thread = drv_acpi_eval_thread(caller);
	error = serialize(&eval, node->object, true);
	if (error == 0) {
		error = drv_acpi_exec_term_list(&eval);
		serialize(&eval, node->object, false);
	}

	/* Hands the returned value to the caller, or drops it on failure. */
	if (error == 0) {
		*result = eval.return_value;
	} else {
		drv_acpi_object_release(eval.return_value);
	}

	/* Deletes what the invocation created and releases its objects. */
	frame_release(frame);

	/* Reports a failed body. */
	if (error != 0)
		return error;

	/* Succeeded: the caller holds the result, if any. */
	return 0;
}

/*
 * Reads the value of a named object: a field is read from its region, a
 * method is invoked without arguments, and anything else is shared.
 */
int
drv_acpi_read_node(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	int error;

	/* Reads through an alias. */
	node = drv_acpi_ns_resolve_alias(node);
	object = node->object;

	/* A node without an object has no value. */
	if (object == NULL)
		return EINVAL;

	/* Chooses the read by the kind of object. */
	switch (object->type) {
	case DRV_ACPI_TYPE_FIELD_UNIT:
		error = drv_acpi_field_read(eval, object, result);
		return error;
	case DRV_ACPI_TYPE_BUFFER_FIELD:
		error = drv_acpi_buffer_field_read(object, result);
		return error;
	case DRV_ACPI_TYPE_METHOD:
		error = drv_acpi_invoke(eval, node, NULL, 0, result);
		if (error != 0)
			return error;

		/* A method that returned nothing reads as zero. */
		if (*result == NULL) {
			*result = drv_acpi_object_integer_new(0);
			if (*result == NULL)
				return ENOMEM;
		}

		/* Succeeded: the caller holds what the method returned. */
		return 0;
	default:
		break;
	}

	/* Succeeded: the caller shares the object. */
	drv_acpi_object_ref(object);
	*result = object;
	return 0;
}

/*
 * Reads the element an index reference points at.
 *
 * A package element is shared, and one that names an object reads as that
 * object's value, as the reference interpreter does.  A byte of a buffer or
 * a character of a string becomes an integer.  An element that was never
 * set is an error.
 */
int
drv_acpi_index_read(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *reference,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *container;
	struct drv_acpi_object *element;
	struct drv_acpi_node *node;
	uint32_t index;
	int error;

	/* Finds the container and the element. */
	container = reference->value.reference.target;
	index = reference->value.reference.index;

	/* A package element is shared, unless it was never set. */
	if (container->type == DRV_ACPI_TYPE_PACKAGE) {
		element = container->value.package.elements[index];
		if (element == NULL)
			return EINVAL;

		/* An element that names an object reads as the object's value. */
		node = drv_acpi_object_reference_node(element);
		if (node != NULL) {
			error = drv_acpi_read_node(eval, node, result);
			return error;
		}

		/* Succeeded: the caller shares the element. */
		drv_acpi_object_ref(element);
		*result = element;
		return 0;
	}

	/* A byte or a character becomes an integer. */
	if (container->type == DRV_ACPI_TYPE_BUFFER) {
		element = drv_acpi_object_integer_new(container->value.buffer.bytes[index]);
	} else {
		element = drv_acpi_object_integer_new((uint8_t)container->value.string.text[index]);
	}

	/* Reports an integer that could not be allocated. */
	if (element == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = element;
	return 0;
}

/*
 * Refuses to recurse further when the stack budget is spent.
 */
int
drv_acpi_stack_check(void)
{
	struct drv_acpi_thread *thread;
	uintptr_t stack_base;
	uintptr_t here;
	size_t used;
	size_t budget;

	/* Nothing is measured outside an entry. */
	thread = drv_acpi_active_thread();
	if (thread == NULL || thread->stack_base == 0)
		return 0;
	stack_base = thread->stack_base;

	/*
	 * Measures from the outermost evaluation down to this frame.  The
	 * frame address is the machine stack even when a sanitizer moves
	 * locals elsewhere.
	 */
	here = (uintptr_t)__builtin_frame_address(0);
	if (here < stack_base) {
		used = (size_t)(stack_base - here);
	} else {
		used = (size_t)(here - stack_base);
	}

	/* Keeps the deepest point for measurement. */
	if (used > stack_deepest)
		stack_deepest = used;

	/* Refuses to go on past the budget. */
	budget = drv_acpi_os_stack_budget();
	if (used > budget) {
		drv_acpi_os_log("ACPI: AML nests deeper than the stack budget\n");
		return E2BIG;
	}

	/* Succeeded: there is room to go deeper. */
	return 0;
}

/*
 * Reports the deepest stack use measured so far.
 */
size_t
drv_acpi_stack_deepest(void)
{
	/* Reports the measurement. */
	return stack_deepest;
}

/* Runs one term: a definition, a control statement, or an expression. */
static int
exec_term(
	struct drv_acpi_eval *eval)
{
	struct drv_acpi_object *object;
	const uint8_t *start;
	unsigned opcode;
	bool named;
	bool definition;
	int error;

	/* Refuses to go deeper than the stack budget allows. */
	error = drv_acpi_stack_check();
	if (error != 0)
		return error;

	/* A name is a method invocation or a reference whose value is dropped. */
	named = drv_acpi_stream_at_name(eval);
	if (named) {
		error = drv_acpi_eval_term_arg(eval, &object);
		if (error != 0)
			return error;
		drv_acpi_object_release(object);
		return 0;
	}

	/* Reads the opcode, keeping its start to hand it back to an expression. */
	start = eval->position;
	error = drv_acpi_stream_opcode(eval, &opcode);
	if (error != 0)
		return error;

	/* A definition creates a named object. */
	definition = drv_acpi_is_definition(opcode);
	if (definition) {
		error = drv_acpi_define(eval, opcode);
		return error;
	}

	/* Chooses the statement by its opcode. */
	switch (opcode) {
	case DRV_ACPI_OP_IF:
		error = exec_if(eval);
		return error;
	case DRV_ACPI_OP_ELSE:
		/* An Else without its If is skipped. */
		error = drv_acpi_stream_package_length(eval, &eval->position);
		return error;
	case DRV_ACPI_OP_WHILE:
		error = exec_while(eval);
		return error;
	case DRV_ACPI_OP_RETURN:
		error = exec_return(eval);
		return error;
	case DRV_ACPI_OP_BREAK:
		eval->control = DRV_ACPI_CONTROL_BREAK;
		return 0;
	case DRV_ACPI_OP_CONTINUE:
		eval->control = DRV_ACPI_CONTROL_CONTINUE;
		return 0;
	case DRV_ACPI_OP_NOOP:
	case DRV_ACPI_OP_BREAK_POINT:
		return 0;
	default:
		break;
	}

	/* Anything else is an expression whose value is dropped. */
	eval->position = start;
	error = drv_acpi_eval_term_arg(eval, &object);
	if (error != 0)
		return error;
	drv_acpi_object_release(object);

	/* Succeeded. */
	return 0;
}

/* Runs a nested term list that ends at block_end, then continues after it. */
static int
exec_block(
	struct drv_acpi_eval *eval,
	const uint8_t *block_end)
{
	const uint8_t *outer_end;
	int error;

	/* Bounds the stream by the block while it runs. */
	outer_end = eval->end;
	eval->end = block_end;
	error = drv_acpi_exec_term_list(eval);
	eval->end = outer_end;
	if (error != 0)
		return error;

	/* Continues after the block unless control left it. */
	if (eval->control == DRV_ACPI_CONTROL_NEXT)
		eval->position = block_end;

	/* Succeeded. */
	return 0;
}

/* Runs an If and the Else that may follow it. */
static int
exec_if(
	struct drv_acpi_eval *eval)
{
	const uint8_t *if_end;
	const uint8_t *else_end;
	const uint8_t *outer_end;
	uint64_t predicate;
	uint8_t byte;
	int error;

	/* Reads the extent of the If. */
	error = drv_acpi_stream_package_length(eval, &if_end);
	if (error != 0)
		return error;

	/* Evaluates the predicate inside the If's extent. */
	outer_end = eval->end;
	eval->end = if_end;
	error = drv_acpi_eval_integer(eval, &predicate);
	eval->end = outer_end;
	if (error != 0)
		return error;

	/* Runs the If's body when the predicate holds. */
	if (predicate != 0) {
		error = exec_block(eval, if_end);
		if (error != 0)
			return error;

		/* A Return, Break or Continue leaves before any Else. */
		if (eval->control != DRV_ACPI_CONTROL_NEXT)
			return 0;
	} else {
		eval->position = if_end;
	}

	/* Nothing more to do when no Else follows. */
	if (eval->position >= eval->end)
		return 0;
	byte = *eval->position;
	if (byte != DRV_ACPI_OP_ELSE)
		return 0;

	/* Reads the extent of the Else. */
	eval->position++;
	error = drv_acpi_stream_package_length(eval, &else_end);
	if (error != 0)
		return error;

	/* Skips the Else when the If ran. */
	if (predicate != 0) {
		eval->position = else_end;
		return 0;
	}

	/* Runs the Else's body. */
	error = exec_block(eval, else_end);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs a While loop. */
static int
exec_while(
	struct drv_acpi_eval *eval)
{
	const uint8_t *while_end;
	const uint8_t *predicate_start;
	const uint8_t *outer_end;
	uint64_t predicate;
	uint64_t started;
	uint64_t now;
	int error;

	/* Reads the extent of the loop. */
	error = drv_acpi_stream_package_length(eval, &while_end);
	if (error != 0)
		return error;
	predicate_start = eval->position;
	started = drv_acpi_os_timer();

	/* Runs the body for as long as the predicate holds. */
	for (;;) {
		/* Evaluates the predicate inside the loop's extent. */
		eval->position = predicate_start;
		outer_end = eval->end;
		eval->end = while_end;
		error = drv_acpi_eval_integer(eval, &predicate);
		eval->end = outer_end;
		if (error != 0)
			return error;

		/* Leaves when the predicate fails. */
		if (predicate == 0)
			break;

		/* Runs the body. */
		error = exec_block(eval, while_end);
		if (error != 0)
			return error;

		/* A Break leaves the loop, a Return the method; a Continue starts the next turn. */
		if (eval->control == DRV_ACPI_CONTROL_BREAK) {
			eval->control = DRV_ACPI_CONTROL_NEXT;
			break;
		} else if (eval->control == DRV_ACPI_CONTROL_RETURN) {
			return 0;
		} else if (eval->control == DRV_ACPI_CONTROL_CONTINUE) {
			eval->control = DRV_ACPI_CONTROL_NEXT;
		}

		/* Abandons a loop that waits for hardware that never answers. */
		now = drv_acpi_os_timer();
		if (now - started > WHILE_TIMEOUT_TICKS) {
			drv_acpi_os_log("ACPI: While loop timed out\n");
			return ETIMEDOUT;
		}
	}

	/* Succeeded: continues after the loop. */
	eval->position = while_end;
	return 0;
}

/* Runs a Return. */
static int
exec_return(
	struct drv_acpi_eval *eval)
{
	struct drv_acpi_object *value;
	int error;

	/* Evaluates the returned object. */
	error = drv_acpi_eval_term_arg(eval, &value);
	if (error != 0)
		return error;

	/* Hands it to the invocation and leaves every enclosing term list. */
	drv_acpi_object_release(eval->return_value);
	eval->return_value = value;
	eval->control = DRV_ACPI_CONTROL_RETURN;

	/* Succeeded. */
	return 0;
}

/* Evaluates a name in a TermArg position. */
static int
eval_name(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_name name;
	struct drv_acpi_node *node;
	int error;

	/* Reads the name. */
	error = drv_acpi_stream_name(eval, &name);
	if (error != 0)
		return error;

	/* Resolves it with the search rules of references. */
	error = drv_acpi_ns_lookup(eval->scope, &name, true, &node);
	if (error != 0) {
		report_name("name", &name, error);
		return error;
	}

	/* An alias stands for the node it names. */
	node = drv_acpi_ns_resolve_alias(node);

	/* A method is invoked with the arguments that follow the name. */
	if (node->object != NULL && node->object->type == DRV_ACPI_TYPE_METHOD) {
		error = eval_method_call(eval, node, result);
		if (error != 0)
			return error;

		/* A method that returned nothing gives zero as its value. */
		if (*result == NULL) {
			*result = drv_acpi_object_integer_new(0);
			if (*result == NULL)
				return ENOMEM;
		}

		/* Succeeded: the caller holds what the method returned. */
		return 0;
	}

	/* Anything else is read. */
	error = drv_acpi_read_node(eval, node, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Evaluates the arguments that follow a method's name and invokes it. */
static int
eval_method_call(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *arguments[DRV_ACPI_ARGUMENT_COUNT];
	unsigned count;
	unsigned index;
	int error;

	/* The method's definition says how many arguments follow. */
	count = node->object->value.method.argument_count;

	/* Evaluates each argument in order. */
	for (index = 0; index < count; index++) {
		/* Evaluates one argument. */
		error = drv_acpi_eval_term_arg(eval, &arguments[index]);
		if (error != 0) {
			/* Releases the arguments evaluated before this one. */
			while (index != 0) {
				index--;
				drv_acpi_object_release(arguments[index]);
			}

			/* Reports the argument that failed. */
			return error;
		}
	}

	/* Invokes the method with them. */
	error = drv_acpi_invoke(eval, node, arguments, count, result);

	/* The invocation took its own references. */
	for (index = 0; index < count; index++)
		drv_acpi_object_release(arguments[index]);

	/* Reports a failed invocation. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Evaluates a constant or literal data object. */
static int
eval_constant(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	const char *text;
	uint64_t value;
	size_t length;
	int error;

	/* Chooses the constant by its opcode. */
	value = 0;
	switch (opcode) {
	case DRV_ACPI_OP_ZERO:
		value = 0;
		break;
	case DRV_ACPI_OP_ONE:
		value = 1;
		break;
	case DRV_ACPI_OP_ONES:
		value = drv_acpi_integer_mask();
		break;
	case DRV_ACPI_OP_BYTE_PREFIX:
		error = drv_acpi_stream_integer(eval, 1, &value);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_OP_WORD_PREFIX:
		error = drv_acpi_stream_integer(eval, 2, &value);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_OP_DWORD_PREFIX:
		error = drv_acpi_stream_integer(eval, 4, &value);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_OP_QWORD_PREFIX:
		error = drv_acpi_stream_integer(eval, 8, &value);
		if (error != 0)
			return error;
		value &= drv_acpi_integer_mask();
		break;
	case DRV_ACPI_OP_REVISION:
		value = DRV_ACPI_AML_REVISION;
		break;
	case DRV_ACPI_OP_STRING_PREFIX:
		/* Reads the string, which stays in the table until copied. */
		error = drv_acpi_stream_string(eval, &text, &length);
		if (error != 0)
			return error;

		/* Copies it into a string object. */
		object = drv_acpi_object_string_new_length(text, length);
		if (object == NULL)
			return ENOMEM;
		*result = object;
		return 0;
	default:
		return EINVAL;
	}

	/* Makes the integer object. */
	object = drv_acpi_object_integer_new(value);
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/* Reads a local or an argument of the running invocation. */
static int
eval_local(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;

	/* Refuses a local or an argument outside any method. */
	if (eval->frame == NULL)
		return EINVAL;

	/* Finds the slot. */
	if (opcode <= DRV_ACPI_OP_LOCAL7) {
		object = eval->frame->locals[opcode - DRV_ACPI_OP_LOCAL0];
	} else {
		object = eval->frame->arguments[opcode - DRV_ACPI_OP_ARG0];
	}

	/* Refuses a slot that was never stored to. */
	if (object == NULL) {
		drv_acpi_os_log("ACPI: read of an uninitialized local or argument\n");
		return EINVAL;
	}

	/* Succeeded: the caller shares the object. */
	drv_acpi_object_ref(object);
	*result = object;
	return 0;
}

/* Replaces a local or argument with a copy of the value. */
static int
store_local(
	struct drv_acpi_object **slot,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *copy;
	int error;

	/* A local gets its own copy of data and shares anything else. */
	error = drv_acpi_object_copy(value, &copy);
	if (error != 0)
		return error;

	/* Replaces the old value. */
	drv_acpi_object_release(*slot);
	*slot = copy;

	/* Succeeded. */
	return 0;
}

/* Stores into a named object, converting to the object's type. */
static int
store_node(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *object;
	struct drv_acpi_object *converted;
	uint64_t integer;
	int error;

	/* Stores through an alias. */
	node = drv_acpi_ns_resolve_alias(node);
	object = node->object;

	/* A node without a value takes a copy of anything. */
	if (object == NULL || object->type == DRV_ACPI_TYPE_UNINITIALIZED) {
		error = drv_acpi_object_copy(value, &converted);
		if (error != 0)
			return error;
		drv_acpi_ns_attach(node, converted);
		drv_acpi_object_release(converted);
		return 0;
	}

	/* Chooses the conversion by the target's type. */
	switch (object->type) {
	case DRV_ACPI_TYPE_INTEGER:
		/* Converts to an integer and replaces the value. */
		error = drv_acpi_convert_integer(value, &integer);
		if (error != 0)
			return error;
		converted = drv_acpi_object_integer_new(integer & drv_acpi_integer_mask());
		if (converted == NULL)
			return ENOMEM;
		break;
	case DRV_ACPI_TYPE_STRING:
		/* Converts to a string and replaces the value. */
		error = drv_acpi_convert_string(value, &converted);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_TYPE_BUFFER:
		/* Copies into the existing buffer, which keeps its length. */
		error = store_buffer_into(object, value);
		return error;
	case DRV_ACPI_TYPE_PACKAGE:
		/* Only a package replaces a package. */
		if (value->type != DRV_ACPI_TYPE_PACKAGE)
			return EINVAL;
		error = drv_acpi_object_copy(value, &converted);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_TYPE_FIELD_UNIT:
		error = drv_acpi_field_write(eval, object, value);
		return error;
	case DRV_ACPI_TYPE_BUFFER_FIELD:
		error = drv_acpi_buffer_field_write(object, value);
		return error;
	default:
		drv_acpi_os_log("ACPI: store to an object of type %u\n", (unsigned)object->type);
		return EINVAL;
	}

	/* Replaces the node's value with the converted one. */
	drv_acpi_ns_attach(node, converted);
	drv_acpi_object_release(converted);

	/* Succeeded. */
	return 0;
}

/* Stores through a reference object. */
static int
store_reference(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *reference,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *target;
	int error;

	/* Chooses the store by what the reference points at. */
	switch (reference->value.reference.kind) {
	case DRV_ACPI_REFERENCE_NODE:
		error = store_node(eval, reference->value.reference.node, value);
		return error;
	case DRV_ACPI_REFERENCE_INDEX:
		error = store_index(reference, value);
		return error;
	case DRV_ACPI_REFERENCE_OBJECT:
		target = reference->value.reference.target;

		/* Only a buffer can be changed in place through such a reference. */
		if (target->type != DRV_ACPI_TYPE_BUFFER)
			return EINVAL;
		error = store_buffer_into(target, value);
		return error;
	default:
		break;
	}

	/* Reports a reference that cannot be stored through. */
	return EINVAL;
}

/* Stores into one element of a package, buffer or string. */
static int
store_index(
	struct drv_acpi_object *reference,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *container;
	struct drv_acpi_object *copy;
	uint64_t integer;
	uint32_t index;
	int error;

	/* Finds the container and the element. */
	container = reference->value.reference.target;
	index = reference->value.reference.index;

	/* A package element is replaced by a copy of the value. */
	if (container->type == DRV_ACPI_TYPE_PACKAGE) {
		error = drv_acpi_object_copy(value, &copy);
		if (error != 0)
			return error;
		drv_acpi_object_release(container->value.package.elements[index]);
		container->value.package.elements[index] = copy;
		return 0;
	}

	/* A byte or a character takes the low byte of the value as an integer. */
	error = drv_acpi_convert_integer(value, &integer);
	if (error != 0)
		return error;

	/* Writes the byte into the buffer or the string. */
	if (container->type == DRV_ACPI_TYPE_BUFFER) {
		container->value.buffer.bytes[index] = (uint8_t)integer;
	} else {
		container->value.string.text[index] = (char)integer;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Copies a value into an existing buffer, which keeps its length: a
 * shorter value is followed by zeros and a longer one is cut.
 */
static int
store_buffer_into(
	struct drv_acpi_object *target,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *source;
	size_t length;
	int error;

	/* Converts the value to a buffer. */
	error = drv_acpi_convert_buffer(value, &source);
	if (error != 0)
		return error;

	/* Clears the target and copies as much of the source as fits. */
	length = source->value.buffer.length;
	if (length > target->value.buffer.length)
		length = target->value.buffer.length;
	if (target->value.buffer.length != 0)
		kern_memset(target->value.buffer.bytes, 0, target->value.buffer.length);
	if (length != 0)
		kern_memcpy(target->value.buffer.bytes, source->value.buffer.bytes, length);
	drv_acpi_object_release(source);

	/* Succeeded. */
	return 0;
}

/* Parses a RefOf, DerefOf, Index or method-invocation target. */
static int
parse_reference_target(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_target *target)
{
	struct drv_acpi_object *object;
	struct drv_acpi_node *node;
	int error;

	/* Only operators that produce references can be targets. */
	if (opcode != DRV_ACPI_OP_REF_OF &&
	    opcode != DRV_ACPI_OP_DEREF_OF &&
	    opcode != DRV_ACPI_OP_INDEX) {
		drv_acpi_os_log("ACPI: opcode 0x%x is not a target\n", opcode);
		return EIO;
	}

	/* DerefOf in a target names what its operand refers to. */
	if (opcode == DRV_ACPI_OP_DEREF_OF) {
		error = drv_acpi_eval_term_arg(eval, &object);
		if (error != 0)
			return error;

		/* A string operand is the path of the target. */
		if (object->type == DRV_ACPI_TYPE_STRING) {
			error = drv_acpi_lookup_path(eval->scope, object->value.string.text, true, &node);
			drv_acpi_object_release(object);
			if (error != 0)
				return error;
			target->kind = DRV_ACPI_TARGET_NODE;
			target->node = node;
			return 0;
		}

		/* Anything else must be a reference. */
		if (object->type != DRV_ACPI_TYPE_REFERENCE) {
			drv_acpi_object_release(object);
			return EINVAL;
		}

		/* Succeeded: the target stores through the reference. */
		target->kind = DRV_ACPI_TARGET_REFERENCE;
		target->reference = object;
		return 0;
	}

	/* RefOf and Index compute the reference themselves. */
	error = drv_acpi_eval_operator(eval, opcode, &object);
	if (error != 0)
		return error;

	/* Succeeded: the target stores through the reference. */
	target->kind = DRV_ACPI_TARGET_REFERENCE;
	target->reference = object;
	return 0;
}

/* Logs a value stored into the debug object. */
static void
debug_store(
	struct drv_acpi_object *value)
{
	/* Writes the value in the form its type suggests. */
	switch (value->type) {
	case DRV_ACPI_TYPE_INTEGER:
		drv_acpi_os_log("ACPI Debug: 0x%llx\n", (unsigned long long)value->value.integer);
		break;
	case DRV_ACPI_TYPE_STRING:
		drv_acpi_os_log("ACPI Debug: \"%s\"\n", value->value.string.text);
		break;
	case DRV_ACPI_TYPE_BUFFER:
		drv_acpi_os_log("ACPI Debug: buffer of %lu bytes\n", (unsigned long)value->value.buffer.length);
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		drv_acpi_os_log("ACPI Debug: package of %u elements\n", (unsigned)value->value.package.count);
		break;
	default:
		drv_acpi_os_log("ACPI Debug: object of type %u\n", (unsigned)value->type);
		break;
	}
}

/* Ends an invocation: deletes the nodes it created and releases its objects. */
static void
frame_release(
	struct drv_acpi_frame *frame)
{
	struct drv_acpi_node *node;
	unsigned index;

	/* Deletes the nodes, newest first so that children go before parents. */
	while (frame->created != NULL) {
		node = frame->created;
		frame->created = node->temporary_next;
		drv_acpi_ns_delete(node);
	}

	/* Releases the locals and the arguments. */
	for (index = 0; index < DRV_ACPI_LOCAL_COUNT; index++)
		drv_acpi_object_release(frame->locals[index]);
	for (index = 0; index < DRV_ACPI_ARGUMENT_COUNT; index++)
		drv_acpi_object_release(frame->arguments[index]);

	/* Frees the frame. */
	drv_acpi_os_free(frame);
}

/*
 * Acquires or releases the mutex of a serialized method, which keeps two
 * threads from running it at once; its sync level is the method's.
 */
static int
serialize(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *method,
	bool acquire)
{
	struct drv_acpi_object *mutex;
	bool timed_out;
	int error;

	/* A method that is not serialized has no mutex. */
	if (!method->value.method.serialized)
		return 0;

	/* Releases the mutex after the body. */
	if (!acquire) {
		error = drv_acpi_mutex_release(eval->thread, method->value.method.serialization);
		return error;
	}

	/* Creates the mutex on the first invocation. */
	mutex = method->value.method.serialization;
	if (mutex == NULL) {
		mutex = drv_acpi_object_new(DRV_ACPI_TYPE_MUTEX);
		if (mutex == NULL)
			return ENOMEM;
		mutex->value.mutex.sync_level = method->value.method.sync_level;
		method->value.method.serialization = mutex;
	}

	/* Acquires it, waiting as long as another thread runs the method. */
	error = drv_acpi_mutex_acquire(eval->thread, mutex, 0xffffU, &timed_out);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Logs a name that could not be resolved. */
static void
report_name(
	const char *what,
	const struct drv_acpi_name *name,
	int error)
{
	char text[128];
	int converted;

	/* Converts the name to text. */
	converted = drv_acpi_ns_name_text(name, text, sizeof(text));
	if (converted != 0)
		kern_strcpy(text, "(long name)");

	/* Logs it with the error. */
	drv_acpi_os_log("ACPI: %s %s not resolved (error %d)\n", what, text, error);
}
