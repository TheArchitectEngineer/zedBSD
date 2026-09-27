/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The JavaScript compiler's entry, its functions and its statements
 * (plan/ws074/design.md §12.2): a parsed program becomes a function of the
 * shared bytecode, one code unit for the program and one for each function
 * in it.
 *
 * A function's frame holds its parameters first, then its other bindings
 * that no nested function captures, then the register of its environment
 * (the one it makes when it has captured bindings, or the one of the code
 * around it), then temporary values.  Its code starts by making the
 * environment, moving captured parameters into it, and making the
 * functions it declares; then its statements; then a return of undefined.
 *
 * The first pass covers the core of ES5: var, functions and closures,
 * arguments, this, the operators, object and array literals with
 * accessors, every ES5 statement but with, and labels.  What later phases
 * bring (let and const, arrow functions, classes, destructuring, spread,
 * templates, generators, async functions, regular expressions, modules,
 * with, direct eval) is refused with a "not supported yet" error.
 */

#include "js/compile.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void compile_trace(struct vm_heap *heap, void *context);
static void compile_release_all(struct js_compiler *compiler);
static void compile_prepare(struct js_function_compiler *fc);
static void compile_place_binding(struct js_function_compiler *fc, struct js_binding *binding, uint32_t *env_count);
static void compile_prologue(struct js_function_compiler *fc, uint32_t env_count);
static void compile_hoisted(struct js_function_compiler *fc);
static uint32_t compile_flags(const struct js_function_info *info);
static void compile_statement(struct js_function_compiler *fc, struct js_node *node);
static void compile_variables(struct js_function_compiler *fc, struct js_node *node);
static void compile_expression_statement(struct js_function_compiler *fc, struct js_node *node);
static void compile_if(struct js_function_compiler *fc, struct js_node *node);
static struct js_target *compile_target_push(struct js_function_compiler *fc, int loop);
static void compile_target_pop(struct js_function_compiler *fc, struct js_target *target);
static void compile_for(struct js_function_compiler *fc, struct js_node *node);
static void compile_for_in(struct js_function_compiler *fc, struct js_node *node);
static void compile_while(struct js_function_compiler *fc, struct js_node *node);
static void compile_do_while(struct js_function_compiler *fc, struct js_node *node);
static void compile_jump_statement(struct js_function_compiler *fc, struct js_node *node);
static void compile_jump_out(struct js_function_compiler *fc, uint32_t label, uint32_t finally_depth);
static int compile_has_label(const struct js_target *target, const uint16_t *name, size_t length);
static void compile_return(struct js_function_compiler *fc, struct js_node *node);
static void compile_throw(struct js_function_compiler *fc, struct js_node *node);
static void compile_switch(struct js_function_compiler *fc, struct js_node *node);
static void compile_labeled(struct js_function_compiler *fc, struct js_node *node);
static void compile_try(struct js_function_compiler *fc, struct js_node *node);
static void compile_catch(struct js_function_compiler *fc, struct js_node *node, uint32_t start, uint32_t after);
static void compile_finally_dispatch(struct js_function_compiler *fc, struct js_finally *handler);
static void compile_dispatch_case(struct js_function_compiler *fc, uint32_t kind_register, int code, uint32_t skip);

/*
 * Compiles a parsed program into a function of a realm (the program's code
 * with no parameters, to be called with the global object as this).
 * Returns 0, EINVAL with *error saying what is not supported, or ENOMEM.
 */
int
js_compile(
	struct vm_realm *realm,
	struct js_program *program,
	struct vm_function **function,
	struct js_syntax_error *error)
{
	struct js_compiler *compiler;
	struct js_function_info *info;
	struct vm_function *made;
	struct vm_code *code;
	int jumped;
	int status;

	/* The compiler lives in the program's arena (it must outlive setjmp's frame). */
	*function = NULL;
	memset(error, 0, sizeof(*error));
	compiler = wb_arena_zalloc(&program->arena, sizeof(*compiler));
	if (compiler == NULL)
		return ENOMEM;
	compiler->realm = realm;
	compiler->arena = &program->arena;
	compiler->error = error;

	/* The constants of the code being written are the collector's to see. */
	status = vm_heap_add_tracer(realm->heap, compile_trace, compiler);
	if (status != 0)
		return status;

	/* A failure comes back here. */
	jumped = setjmp(compiler->failure);
	if (jumped) {
		compile_release_all(compiler);
		vm_heap_remove_tracer(realm->heap, compile_trace, compiler);
		return compiler->status;
	}

	/* The scopes, then the program's code. */
	info = js_scope_analyze(compiler, program->root);
	code = js_compile_function(compiler, NULL, info->node, NULL, 0);
	vm_heap_remove_tracer(realm->heap, compile_trace, compiler);

	/* The program as a function (no environment around it). */
	made = vm_closure_create(realm, code, NULL);
	if (made == NULL)
		return ENOMEM;

	/* Succeeded: the program's function. */
	*function = made;
	return 0;
}

/*
 * Parses, compiles and runs a script with the global object as this;
 * stores its completion value (its last expression statement's).  Returns 0, VM_THROWN with
 * the realm's exception set, EINVAL with *error describing a syntax error
 * or what is not supported, or ENOMEM.
 */
int
js_run_script(
	struct vm_realm *realm,
	const uint16_t *source,
	size_t length,
	unsigned how,
	vm_value *result,
	struct js_syntax_error *error)
{
	struct js_program program;
	struct vm_function *function;
	int status;

	/* The tree. */
	*result = VM_VALUE_UNDEFINED;
	status = js_parse(source, length, how, &program, error);
	if (status != 0)
		return status;

	/* The code (the tree is no longer needed after it). */
	status = js_compile(realm, &program, &function, error);
	js_program_release(&program);
	if (status != 0)
		return status;

	/* The run. */
	status = vm_call(realm, vm_value_cell(function), vm_value_cell(realm->global), NULL, 0, result);
	if (status != 0)
		return status;

	/* Succeeded: the script ran to its end. */
	return 0;
}

/*
 * Fails the compilation at a node with a message.
 */
void
js_compile_fail(
	struct js_compiler *compiler,
	const struct js_node *node,
	const char *message)
{
	/* The place and the message, then back to js_compile. */
	if (node != NULL) {
		compiler->error->line = node->line;
		compiler->error->column = node->column;
	}

	/* The message and the status, then back to js_compile. */
	snprintf(compiler->error->message, sizeof(compiler->error->message), "%s", message);
	compiler->status = EINVAL;
	longjmp(compiler->failure, 1);
}

/*
 * Fails the compilation at a node that uses what the compiler does not
 * support yet.
 */
void
js_compile_unsupported(
	struct js_compiler *compiler,
	const struct js_node *node,
	const char *what)
{
	char message[160];

	/* The message names the construct; the error says the script is sound. */
	snprintf(message, sizeof(message), "not supported yet: %s", what);
	compiler->error->unsupported = 1;
	js_compile_fail(compiler, node, message);
}

/*
 * Fails the compilation for want of memory.
 */
void
js_compile_out_of_memory(
	struct js_compiler *compiler)
{
	/* The status, then back to js_compile. */
	snprintf(compiler->error->message, sizeof(compiler->error->message), "out of memory");
	compiler->status = ENOMEM;
	longjmp(compiler->failure, 1);
}

/*
 * Compiles a function (or, with no parent, the program) into its code
 * unit; name is its name (NULL for the node's own).
 */
struct vm_code *
js_compile_function(
	struct js_compiler *compiler,
	struct js_function_compiler *parent,
	struct js_node *node,
	const uint16_t *name,
	size_t name_length)
{
	struct js_function_compiler *fc;
	struct js_function_info *info;
	struct vm_code *code;
	uint32_t result;
	uint32_t flags;

	/* What the language does not have in this compiler yet. */
	info = node->scope->function;
	if ((node->flags & JS_FLAG_ARROW) != 0U)
		js_compile_unsupported(compiler, node, "arrow functions");
	if ((node->flags & JS_FLAG_GENERATOR) != 0U)
		js_compile_unsupported(compiler, node, "generators");
	if ((node->flags & JS_FLAG_ASYNC) != 0U)
		js_compile_unsupported(compiler, node, "async functions");
	if (info->unsupported != NULL)
		js_compile_unsupported(compiler, info->unsupported, "destructuring, default and rest parameters");

	/* The function's state, in the arena, the innermost being compiled. */
	fc = wb_arena_zalloc(compiler->arena, sizeof(*fc));
	if (fc == NULL)
		js_compile_out_of_memory(compiler);
	fc->compiler = compiler;
	fc->parent = parent;
	fc->info = info;
	fc->scope = info->scope;
	js_emit_begin(fc);
	compiler->current = fc;

	/* The registers and the environment, the prologue, then the body. */
	compile_prepare(fc);
	if (info->program) {
		js_compile_statements(fc, node->first);
	} else {
		js_compile_statements(fc, node->second);
	}

	/* Falling off the end returns undefined (the program its completion value). */
	if (info->program) {
		js_emit1(fc, VM_OP_RETURN, fc->completion_register);
	} else {
		result = js_temp(fc);
		js_load_value(fc, result, VM_VALUE_UNDEFINED);
		js_emit1(fc, VM_OP_RETURN, result);
	}

	/* The name: the one given, or the node's own. */
	if (name == NULL && node->text != NULL) {
		name = node->text;
		name_length = node->text_length;
	}

	/* The finished unit; the parent is the innermost again. */
	flags = compile_flags(info);
	code = js_emit_finish(fc, name, name_length, flags);
	compiler->current = parent;

	/* Succeeded: the code unit. */
	return code;
}

/*
 * Compiles a list of statements.
 */
void
js_compile_statements(
	struct js_function_compiler *fc,
	struct js_node *list)
{
	struct js_node *node;

	/* Each statement in order. */
	for (node = list; node != NULL; node = node->next)
		compile_statement(fc, node);
}

/*
 * Returns a register's value from the function, through the finally
 * blocks of the try statements it leaves.
 */
void
js_emit_return(
	struct js_function_compiler *fc,
	uint32_t value_register)
{
	struct js_finally *handler;

	/* Without a finally block to pass, the return itself. */
	handler = fc->finally;
	if (handler == NULL) {
		js_emit1(fc, VM_OP_RETURN, value_register);
		return;
	}

	/* The value and the completion code go to the finally block, which returns after it. */
	js_emit2(fc, VM_OP_MOV, handler->value_register, value_register);
	js_emit2(fc, VM_OP_LOAD_INT, handler->kind_register, JS_COMPLETION_RETURN);
	js_emit_jump(fc, VM_OP_JUMP, 0, handler->entry_label);
}

/* Marks the constants of every code unit being written. */
static void
compile_trace(
	struct vm_heap *heap,
	void *context)
{
	struct js_compiler *compiler;
	struct js_function_compiler *fc;
	vm_value *constant;
	size_t index;

	/* Each function being compiled, from the innermost outwards. */
	compiler = context;
	for (fc = compiler->current; fc != NULL; fc = fc->parent) {
		if (fc->released)
			continue;

		/* Each of its constants. */
		for (index = 0; index < fc->constants.count; index++) {
			constant = wb_vector_at(&fc->constants, index);
			vm_heap_mark_value(heap, *constant);
		}
	}
}

/* Frees what the functions being compiled hold, after a failure. */
static void
compile_release_all(
	struct js_compiler *compiler)
{
	struct js_function_compiler *fc;

	/* Each function being compiled. */
	for (fc = compiler->current; fc != NULL; fc = fc->parent)
		js_emit_release(fc);
	compiler->current = NULL;
}

/* Places every binding of a function, counts its registers, and writes its prologue. */
static void
compile_prepare(
	struct js_function_compiler *fc)
{
	struct js_function_info *info;
	struct js_scope *scope;
	struct js_binding *binding;
	struct js_node *parameter;
	uint32_t env_count;

	/* The parameters take the first registers (the program has none). */
	info = fc->info;
	fc->parameter_count = 0;
	parameter = NULL;
	if (!info->program)
		parameter = info->node->first;
	for (;
	     parameter != NULL;
	     parameter = parameter->next)
		fc->parameter_count++;
	fc->local_count = fc->parameter_count;

	/* Every binding of every scope of the function gets its place. */
	env_count = 0;
	for (scope = info->scopes; scope != NULL; scope = scope->next_in_function) {
		for (binding = scope->bindings; binding != NULL; binding = binding->next)
			compile_place_binding(fc, binding, &env_count);
	}

	/* The program's completion value (the last expression statement's), the environment's register, then the temporaries. */
	fc->completion_register = fc->local_count;
	if (info->program)
		fc->local_count++;
	fc->env_register = fc->local_count;
	fc->local_count++;
	fc->register_count = fc->local_count;
	fc->temp_top = fc->local_count;

	/* The code that sets them up. */
	compile_prologue(fc, env_count);
}

/* Gives one binding its place: a slot of the environment when captured, a register otherwise. */
static void
compile_place_binding(
	struct js_function_compiler *fc,
	struct js_binding *binding,
	uint32_t *env_count)
{
	/* The arguments object always arrives in a register of its own. */
	if (binding->kind == JS_BINDING_ARGUMENTS) {
		fc->arguments_register = fc->local_count;
		fc->local_count++;
	}

	/* A captured binding lives in the environment. */
	if (binding->captured) {
		binding->in_env = 1;
		binding->location = *env_count;
		(*env_count)++;
		return;
	}

	/* A parameter keeps its argument's register; arguments its own. */
	binding->in_env = 0;
	if (binding->kind == JS_BINDING_PARAMETER) {
		binding->location = binding->parameter;
		return;
	}

	/* The arguments object keeps its own register. */
	if (binding->kind == JS_BINDING_ARGUMENTS) {
		binding->location = fc->arguments_register;
		return;
	}

	/* Any other binding gets a register. */
	binding->location = fc->local_count;
	fc->local_count++;
}

/*
 * Writes a function's prologue: its environment, the captured parameters
 * and arguments moved into it, its own name for a named function
 * expression, and its function declarations.
 */
static void
compile_prologue(
	struct js_function_compiler *fc,
	uint32_t env_count)
{
	struct js_function_info *info;
	struct js_scope *scope;
	struct js_binding *binding;
	uint32_t callee;

	/* The environment of the code around it, then its own on top when it has captured bindings. */
	info = fc->info;
	js_emit1(fc, VM_OP_LOAD_CLOSURE_ENV, fc->env_register);
	if (info->has_env)
		js_emit3(fc, VM_OP_NEW_ENV, fc->env_register, fc->env_register, env_count);

	/* The captured parameters and arguments object move into the environment. */
	for (binding = info->scope->bindings; binding != NULL; binding = binding->next) {
		if (!binding->in_env)
			continue;
		if (binding->kind == JS_BINDING_PARAMETER)
			js_emit4(fc, VM_OP_PUT_ENV, fc->env_register, 0, binding->location, binding->parameter);
		if (binding->kind == JS_BINDING_ARGUMENTS)
			js_emit4(fc, VM_OP_PUT_ENV, fc->env_register, 0, binding->location, fc->arguments_register);
	}

	/* A named function expression's own name is the function. */
	for (scope = info->scopes; scope != NULL; scope = scope->next_in_function) {
		if (scope->kind != JS_SCOPE_CALLEE)
			continue;

		/* Its one binding, in the environment or in a register. */
		binding = scope->bindings;
		if (binding->in_env) {
			callee = js_temp(fc);
			js_emit1(fc, VM_OP_LOAD_CALLEE, callee);
			js_emit4(fc, VM_OP_PUT_ENV, fc->env_register, 0, binding->location, callee);
			fc->temp_top--;
		} else {
			js_emit1(fc, VM_OP_LOAD_CALLEE, binding->location);
		}
	}

	/* The function declarations, made before anything runs. */
	compile_hoisted(fc);
}

/* Makes the function declarations of a function (the program's become globals, its vars too). */
static void
compile_hoisted(
	struct js_function_compiler *fc)
{
	struct js_function_info *info;
	struct js_hoisted *hoisted;
	struct js_global_name *global;
	struct vm_code *code;
	uint32_t mark;
	uint32_t closure;
	uint32_t constant;
	uint32_t key;

	/* Each declaration: its closure, bound to its name. */
	info = fc->info;
	mark = fc->temp_top;
	for (hoisted = info->hoisted; hoisted != NULL; hoisted = hoisted->next) {
		code = js_compile_function(fc->compiler, fc, hoisted->node, NULL, 0);
		constant = js_constant(fc, vm_value_cell(code));
		closure = js_temp(fc);
		js_emit3(fc, VM_OP_NEW_CLOSURE, closure, constant, fc->env_register);

		/* The program's function is a global; a function's is its binding. */
		if (info->program) {
			key = js_constant_key(fc, hoisted->node->text, hoisted->node->text_length);
			js_emit2(fc, VM_OP_DEFINE_GLOBAL_FUNCTION, key, closure);
		} else {
			js_store_binding(fc, hoisted->node, hoisted->node->text, hoisted->node->text_length, closure);
		}

		/* The temporaries are free again. */
		fc->temp_top = mark;
	}

	/* The program's vars (a name a function took already keeps it). */
	for (global = info->global_vars; global != NULL; global = global->next) {
		key = js_constant_key(fc, global->name, global->length);
		js_emit1(fc, VM_OP_DEFINE_GLOBAL_VAR, key);
	}
}

/* Reports the flags of a function's code unit. */
static uint32_t
compile_flags(
	const struct js_function_info *info)
{
	uint32_t flags;

	/* Strict mode code. */
	flags = 0;
	if (info->strict)
		flags |= VM_CODE_STRICT;

	/* A function that uses its arguments object. */
	if (info->arguments != NULL)
		flags |= VM_CODE_ARGUMENTS;

	/* An ordinary function (not the program, not a method or an accessor) can be called with new. */
	if (!info->program && (info->node->flags & JS_FLAG_METHOD) == 0U)
		flags |= VM_CODE_CONSTRUCTOR;

	/* Reports the flags. */
	return flags;
}

/* Compiles one statement. */
static void
compile_statement(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	/* Each kind of statement. */
	switch (node->kind) {
	case JS_NODE_VARIABLES:
		compile_variables(fc, node);
		break;
	case JS_NODE_FUNCTION_DECLARATION:
	case JS_NODE_EMPTY:
	case JS_NODE_DEBUGGER:
		/* A declaration was made by the prologue; the others do nothing. */
		break;
	case JS_NODE_BLOCK:
		js_compile_statements(fc, node->first);
		break;
	case JS_NODE_EXPRESSION_STATEMENT:
		compile_expression_statement(fc, node);
		break;
	case JS_NODE_IF:
		compile_if(fc, node);
		break;
	case JS_NODE_FOR:
		compile_for(fc, node);
		break;
	case JS_NODE_FOR_IN:
		compile_for_in(fc, node);
		break;
	case JS_NODE_WHILE:
		compile_while(fc, node);
		break;
	case JS_NODE_DO_WHILE:
		compile_do_while(fc, node);
		break;
	case JS_NODE_CONTINUE:
	case JS_NODE_BREAK:
		compile_jump_statement(fc, node);
		break;
	case JS_NODE_RETURN:
		compile_return(fc, node);
		break;
	case JS_NODE_THROW:
		compile_throw(fc, node);
		break;
	case JS_NODE_SWITCH:
		compile_switch(fc, node);
		break;
	case JS_NODE_LABELED:
		compile_labeled(fc, node);
		break;
	case JS_NODE_TRY:
		compile_try(fc, node);
		break;
	case JS_NODE_CLASS_DECLARATION:
		js_compile_unsupported(fc->compiler, node, "classes");
	case JS_NODE_FOR_OF:
		js_compile_unsupported(fc->compiler, node, "for-of");
	case JS_NODE_WITH:
		js_compile_unsupported(fc->compiler, node, "with");
	default:
		js_compile_unsupported(fc->compiler, node, "modules");
	}
}

/* Compiles a var statement: each declarator with an initializer assigns it. */
static void
compile_variables(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_node *declarator;
	struct js_node *target;
	uint32_t mark;
	uint32_t value;

	/* let and const need block scopes. */
	if (node->op != JS_P_VAR)
		js_compile_unsupported(fc->compiler, node, "let and const");

	/* Each declarator: a name, assigned when it has an initializer. */
	mark = fc->temp_top;
	for (declarator = node->first; declarator != NULL; declarator = declarator->next) {
		target = declarator->first;
		if (target->kind != JS_NODE_IDENTIFIER)
			js_compile_unsupported(fc->compiler, target, "destructuring");
		if (declarator->second == NULL)
			continue;

		/* The initializer (an anonymous function takes the name), then the assignment. */
		value = js_temp(fc);
		js_compile_expression_named(fc, declarator->second, value, target->text, target->text_length);
		js_store_binding(fc, target, target->text, target->text_length, value);
		fc->temp_top = mark;
	}
}

/* Compiles an expression statement: the value is computed and dropped. */
static void
compile_expression_statement(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	uint32_t mark;
	uint32_t value;

	/* The expression into a temporary register given back after (the program keeps it as its completion value). */
	mark = fc->temp_top;
	value = js_temp(fc);
	js_compile_expression(fc, node->first, value);
	if (fc->info->program)
		js_emit2(fc, VM_OP_MOV, fc->completion_register, value);
	fc->temp_top = mark;
}

/* Compiles an if statement. */
static void
compile_if(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	uint32_t mark;
	uint32_t test;
	uint32_t otherwise;
	uint32_t end;

	/* The test, which skips the consequent when false. */
	mark = fc->temp_top;
	test = js_temp(fc);
	js_compile_expression(fc, node->first, test);
	fc->temp_top = mark;
	otherwise = js_label_new(fc);
	js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, test, otherwise);

	/* The consequent, and without an alternate, the end. */
	compile_statement(fc, node->second);
	if (node->third == NULL) {
		js_label_place(fc, otherwise);
		return;
	}

	/* Around the alternate. */
	end = js_label_new(fc);
	js_emit_jump(fc, VM_OP_JUMP, 0, end);

	/* The alternate. */
	js_label_place(fc, otherwise);
	compile_statement(fc, node->third);
	js_label_place(fc, end);
}

/*
 * Pushes the target of a statement break can leave (with continue's label
 * for a loop), taking the labels written before it.
 */
static struct js_target *
compile_target_push(
	struct js_function_compiler *fc,
	int loop)
{
	struct js_target *target;

	/* The target in the arena. */
	target = wb_arena_zalloc(fc->compiler->arena, sizeof(*target));
	if (target == NULL)
		js_compile_out_of_memory(fc->compiler);
	target->outer = fc->targets;
	target->labels = fc->pending_labels;
	target->break_label = js_label_new(fc);
	target->continue_label = JS_LABEL_UNPLACED;
	if (loop)
		target->continue_label = js_label_new(fc);
	target->finally_depth = fc->finally_depth;
	target->unlabeled = 1;

	/* The labels are taken, and the target is the innermost now. */
	fc->pending_labels = NULL;
	fc->targets = target;

	/* Succeeded: the target. */
	return target;
}

/* Pops a target, placing its break label after the statement. */
static void
compile_target_pop(
	struct js_function_compiler *fc,
	struct js_target *target)
{
	/* A break lands after the statement. */
	js_label_place(fc, target->break_label);
	fc->targets = target->outer;
}

/* Compiles a for statement. */
static void
compile_for(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	uint32_t mark;
	uint32_t value;
	uint32_t head;
	int declares;

	/* The initialization: a var statement or an expression. */
	mark = fc->temp_top;
	target = compile_target_push(fc, 1);
	declares = 0;
	if (node->first != NULL && node->first->kind == JS_NODE_VARIABLES)
		declares = 1;
	if (declares) {
		compile_variables(fc, node->first);
	} else if (node->first != NULL) {
		value = js_temp(fc);
		js_compile_expression(fc, node->first, value);
		fc->temp_top = mark;
	}

	/* The head: the test leaves the loop when false. */
	head = js_label_new(fc);
	js_label_place(fc, head);
	js_emit0(fc, VM_OP_LOOP_HINT);
	if (node->second != NULL) {
		value = js_temp(fc);
		js_compile_expression(fc, node->second, value);
		js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, value, target->break_label);
		fc->temp_top = mark;
	}

	/* The body; continue goes to the update, which goes back to the head. */
	compile_statement(fc, node->fourth);
	js_label_place(fc, target->continue_label);
	if (node->third != NULL) {
		value = js_temp(fc);
		js_compile_expression(fc, node->third, value);
		fc->temp_top = mark;
	}

	/* Back to the head; break lands after. */
	js_emit_jump(fc, VM_OP_JUMP, 0, head);
	compile_target_pop(fc, target);
}

/* Compiles a for-in statement: the body runs once for each enumerable key, assigned to the left side. */
static void
compile_for_in(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	struct js_node *left;
	struct js_node *declarator;
	uint32_t mark;
	uint32_t object;
	uint32_t iterator;
	uint32_t key;
	uint32_t value;

	/* The left side: a var's name (with Annex B's initializer run first), or a target. */
	mark = fc->temp_top;
	left = node->first;
	if (left->kind == JS_NODE_VARIABLES) {
		if (left->op != JS_P_VAR)
			js_compile_unsupported(fc->compiler, left, "let and const");
		declarator = left->first;
		left = declarator->first;
		if (left->kind != JS_NODE_IDENTIFIER)
			js_compile_unsupported(fc->compiler, left, "destructuring");
		if (declarator->second != NULL) {
			value = js_temp(fc);
			js_compile_expression(fc, declarator->second, value);
			js_store_binding(fc, left, left->text, left->text_length, value);
			fc->temp_top = mark;
		}
	}

	/* The object, and the iterator over its keys (held for the whole loop). */
	iterator = js_temp(fc);
	key = js_temp(fc);
	object = js_temp(fc);
	js_compile_expression(fc, node->second, object);
	js_emit2(fc, VM_OP_FOR_IN_START, iterator, object);

	/* The head: the next key, or out of the loop. */
	target = compile_target_push(fc, 1);
	js_label_place(fc, target->continue_label);
	js_emit0(fc, VM_OP_LOOP_HINT);
	js_emit_for_in_next(fc, key, iterator, target->break_label);

	/* The key to the left side, then the body, then the next key. */
	js_store_target(fc, left, key);
	compile_statement(fc, node->fourth);
	js_emit_jump(fc, VM_OP_JUMP, 0, target->continue_label);
	compile_target_pop(fc, target);
	fc->temp_top = mark;
}

/* Compiles a while statement. */
static void
compile_while(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	uint32_t mark;
	uint32_t test;

	/* The head: the test leaves the loop when false; continue comes back here. */
	mark = fc->temp_top;
	target = compile_target_push(fc, 1);
	js_label_place(fc, target->continue_label);
	js_emit0(fc, VM_OP_LOOP_HINT);
	test = js_temp(fc);
	js_compile_expression(fc, node->first, test);
	js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, test, target->break_label);
	fc->temp_top = mark;

	/* The body, then back to the head. */
	compile_statement(fc, node->fourth);
	js_emit_jump(fc, VM_OP_JUMP, 0, target->continue_label);
	compile_target_pop(fc, target);
}

/* Compiles a do-while statement. */
static void
compile_do_while(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	uint32_t mark;
	uint32_t test;
	uint32_t head;

	/* The body first. */
	mark = fc->temp_top;
	target = compile_target_push(fc, 1);
	head = js_label_new(fc);
	js_label_place(fc, head);
	js_emit0(fc, VM_OP_LOOP_HINT);
	compile_statement(fc, node->fourth);

	/* The test (continue comes here) goes back to the body when true. */
	js_label_place(fc, target->continue_label);
	test = js_temp(fc);
	js_compile_expression(fc, node->first, test);
	js_emit_jump(fc, VM_OP_JUMP_IF_TRUE, test, head);
	fc->temp_top = mark;
	compile_target_pop(fc, target);
}

/* Compiles break and continue: a jump to the target the label (or the innermost loop or switch) names. */
static void
compile_jump_statement(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	int is_continue;
	int labeled;

	/* The target, from the innermost outwards. */
	is_continue = 0;
	if (node->kind == JS_NODE_CONTINUE)
		is_continue = 1;
	for (target = fc->targets; target != NULL; target = target->outer) {
		/* continue needs a loop. */
		if (is_continue && target->continue_label == JS_LABEL_UNPLACED)
			continue;

		/* A label names its statement; without one, the innermost loop or switch. */
		if (node->text != NULL) {
			labeled = compile_has_label(target, node->text, node->text_length);
			if (labeled)
				break;
		} else if (target->unlabeled) {
			break;
		}
	}

	/* The parser saw that the target exists. */
	if (target == NULL)
		js_compile_fail(fc->compiler, node, "internal error: no target for break or continue");

	/* The jump, through the finally blocks it leaves. */
	if (is_continue) {
		compile_jump_out(fc, target->continue_label, target->finally_depth);
	} else {
		compile_jump_out(fc, target->break_label, target->finally_depth);
	}
}

/* Jumps to a label outside the try statements whose finally blocks must run first. */
static void
compile_jump_out(
	struct js_function_compiler *fc,
	uint32_t label,
	uint32_t finally_depth)
{
	struct js_finally *handler;
	struct js_exit *exit;

	/* No finally block in between: the jump itself. */
	if (fc->finally_depth <= finally_depth) {
		js_emit_jump(fc, VM_OP_JUMP, 0, label);
		return;
	}

	/* The innermost finally block runs first, with the jump's completion code (one code for each place). */
	handler = fc->finally;
	for (exit = handler->exits; exit != NULL; exit = exit->next) {
		if (exit->label == label)
			break;
	}

	/* A place not seen before gets the next code. */
	if (exit == NULL) {
		exit = wb_arena_zalloc(fc->compiler->arena, sizeof(*exit));
		if (exit == NULL)
			js_compile_out_of_memory(fc->compiler);
		exit->code = handler->next_code;
		handler->next_code++;
		exit->label = label;
		exit->finally_depth = finally_depth;
		exit->next = handler->exits;
		handler->exits = exit;
	}

	/* The code, then the finally block. */
	js_emit2(fc, VM_OP_LOAD_INT, handler->kind_register, (uint32_t)exit->code);
	js_emit_jump(fc, VM_OP_JUMP, 0, handler->entry_label);
}

/* Tells whether a target has a label. */
static int
compile_has_label(
	const struct js_target *target,
	const uint16_t *name,
	size_t length)
{
	const struct js_label_name *label;
	int same;

	/* Each label written before the statement. */
	for (label = target->labels; label != NULL; label = label->next) {
		same = js_text_equal(label->name, label->length, name, length);
		if (same)
			return 1;
	}

	/* Not one of its labels. */
	return 0;
}

/* Compiles a return statement. */
static void
compile_return(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	uint32_t mark;
	uint32_t value;

	/* The value (undefined without one), then the return. */
	mark = fc->temp_top;
	value = js_temp(fc);
	if (node->first != NULL) {
		js_compile_expression(fc, node->first, value);
	} else {
		js_load_value(fc, value, VM_VALUE_UNDEFINED);
	}

	/* The return, through the finally blocks it leaves. */
	js_emit_return(fc, value);
	fc->temp_top = mark;
}

/* Compiles a throw statement. */
static void
compile_throw(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	uint32_t mark;
	uint32_t value;

	/* The value, thrown. */
	mark = fc->temp_top;
	value = js_temp(fc);
	js_compile_expression(fc, node->first, value);
	js_emit1(fc, VM_OP_THROW, value);
	fc->temp_top = mark;
}

/* Compiles a switch statement: the cases' tests in order, then the bodies, which fall through. */
static void
compile_switch(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_target *target;
	struct js_node *clause;
	uint32_t mark;
	uint32_t discriminant;
	uint32_t test;
	uint32_t first_label;
	uint32_t label;
	uint32_t default_label;

	/* The discriminant. */
	mark = fc->temp_top;
	discriminant = js_temp(fc);
	js_compile_expression(fc, node->first, discriminant);
	target = compile_target_push(fc, 0);

	/* A label for each clause's body, made in order (their numbers follow each other). */
	first_label = JS_LABEL_UNPLACED;
	for (clause = node->second; clause != NULL; clause = clause->next) {
		label = js_label_new(fc);
		if (first_label == JS_LABEL_UNPLACED)
			first_label = label;
	}

	/* Each test compares strictly with the discriminant and jumps to its body. */
	test = js_temp(fc);
	default_label = target->break_label;
	label = first_label;
	for (clause = node->second; clause != NULL; clause = clause->next) {
		if (clause->first == NULL) {
			default_label = label;
		} else {
			js_compile_expression(fc, clause->first, test);
			js_emit3(fc, VM_OP_STRICT_EQ, test, discriminant, test);
			js_emit_jump(fc, VM_OP_JUMP_IF_TRUE, test, label);
		}

		/* The next clause's label. */
		label++;
	}

	/* No case matched: the default's body, or out. */
	js_emit_jump(fc, VM_OP_JUMP, 0, default_label);

	/* The bodies in order. */
	label = first_label;
	for (clause = node->second; clause != NULL; clause = clause->next) {
		js_label_place(fc, label);
		js_compile_statements(fc, clause->second);
		label++;
	}

	/* The end, where break lands. */
	compile_target_pop(fc, target);
	fc->temp_top = mark;
}

/* Compiles a labeled statement: a loop takes the labels; anything else is a target break can leave. */
static void
compile_labeled(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_label_name *label;
	struct js_target *target;
	struct js_node *body;

	/* The label joins those waiting for the statement. */
	label = wb_arena_zalloc(fc->compiler->arena, sizeof(*label));
	if (label == NULL)
		js_compile_out_of_memory(fc->compiler);
	label->name = node->text;
	label->length = node->text_length;
	label->next = fc->pending_labels;
	fc->pending_labels = label;

	/* A loop (or another label) takes them. */
	body = node->fourth;
	if (body->kind == JS_NODE_LABELED ||
	    body->kind == JS_NODE_FOR ||
	    body->kind == JS_NODE_FOR_IN ||
	    body->kind == JS_NODE_FOR_OF ||
	    body->kind == JS_NODE_WHILE ||
	    body->kind == JS_NODE_DO_WHILE) {
		compile_statement(fc, body);
		return;
	}

	/* Any other statement is a target only a labeled break leaves. */
	target = compile_target_push(fc, 0);
	target->unlabeled = 0;
	compile_statement(fc, body);
	compile_target_pop(fc, target);
}

/*
 * Compiles a try statement.  A catch clause is a handler of the protected
 * block; a finally block is a handler of both, and every way out of them
 * (falling through, an exception, a return, a jump) goes through it with a
 * completion code saying where to go after.
 */
static void
compile_try(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	struct js_finally *handler;
	uint32_t mark;
	uint32_t start;
	uint32_t protected_end;
	uint32_t after_catch;
	uint32_t finally_handler;

	/* With a finally block, the registers of its completion and the state of the jumps through it. */
	mark = fc->temp_top;
	handler = NULL;
	if (node->fourth != NULL) {
		handler = wb_arena_zalloc(fc->compiler->arena, sizeof(*handler));
		if (handler == NULL)
			js_compile_out_of_memory(fc->compiler);
		handler->outer = fc->finally;
		handler->entry_label = js_label_new(fc);
		handler->kind_register = js_temp(fc);
		handler->value_register = js_temp(fc);
		handler->next_code = JS_COMPLETION_JUMPS;
		fc->finally = handler;
		fc->finally_depth++;
	}

	/* The protected block. */
	start = js_here(fc);
	compile_statement(fc, node->first);

	/* The catch clause, around which the protected block's normal end jumps. */
	after_catch = js_label_new(fc);
	if (node->third != NULL)
		compile_catch(fc, node, start, after_catch);
	js_label_place(fc, after_catch);
	protected_end = js_here(fc);

	/* Without a finally block, that is all. */
	if (handler == NULL) {
		fc->temp_top = mark;
		return;
	}

	/* The finally block is outside what it protects. */
	fc->finally = handler->outer;
	fc->finally_depth--;

	/* The normal completion goes to the finally block. */
	js_emit2(fc, VM_OP_LOAD_INT, handler->kind_register, JS_COMPLETION_NORMAL);
	js_emit_jump(fc, VM_OP_JUMP, 0, handler->entry_label);

	/* An exception from the blocks it protects arrives with its value, completing by a throw. */
	finally_handler = js_label_new(fc);
	js_label_place(fc, finally_handler);
	js_emit_handler(fc, start, protected_end, finally_handler, handler->value_register);
	js_emit2(fc, VM_OP_LOAD_INT, handler->kind_register, JS_COMPLETION_THROW);

	/* The finally block, then where the completion goes. */
	js_label_place(fc, handler->entry_label);
	compile_statement(fc, node->fourth);
	compile_finally_dispatch(fc, handler);
	fc->temp_top = mark;
}

/* Compiles a catch clause: the handler of the protected block, which binds the exception and runs the clause. */
static void
compile_catch(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t start,
	uint32_t after)
{
	struct js_scope *outer;
	struct js_node *parameter;
	uint32_t end;
	uint32_t caught;
	uint32_t landing;
	uint32_t mark;

	/* The protected block's normal end skips the clause. */
	end = js_here(fc);
	js_emit_jump(fc, VM_OP_JUMP, 0, after);

	/* The handler of the protected block lands here with the exception in a register. */
	mark = fc->temp_top;
	caught = js_temp(fc);
	landing = js_label_new(fc);
	js_label_place(fc, landing);
	js_emit_handler(fc, start, end, landing, caught);

	/* The clause's scope, with its parameter bound to the exception. */
	outer = fc->scope;
	fc->scope = node->scope;
	parameter = node->second;
	if (parameter != NULL && parameter->kind != JS_NODE_IDENTIFIER)
		js_compile_unsupported(fc->compiler, parameter, "destructuring");
	if (parameter != NULL)
		js_store_binding(fc, parameter, parameter->text, parameter->text_length, caught);
	fc->temp_top = mark;

	/* The clause's block, then the scope around it again. */
	compile_statement(fc, node->third);
	fc->scope = outer;
}

/* Writes what follows a finally block: a throw, a return, or a jump by the completion code; the normal completion falls through. */
static void
compile_finally_dispatch(
	struct js_function_compiler *fc,
	struct js_finally *handler)
{
	struct js_exit *exit;
	uint32_t skip;

	/* A throw goes on with the exception. */
	skip = js_label_new(fc);
	compile_dispatch_case(fc, handler->kind_register, JS_COMPLETION_THROW, skip);
	js_emit1(fc, VM_OP_THROW, handler->value_register);
	js_label_place(fc, skip);

	/* A return goes on with the value (through the finally blocks further out). */
	skip = js_label_new(fc);
	compile_dispatch_case(fc, handler->kind_register, JS_COMPLETION_RETURN, skip);
	js_emit_return(fc, handler->value_register);
	js_label_place(fc, skip);

	/* Each jump goes on to its place. */
	for (exit = handler->exits; exit != NULL; exit = exit->next) {
		skip = js_label_new(fc);
		compile_dispatch_case(fc, handler->kind_register, exit->code, skip);
		compile_jump_out(fc, exit->label, exit->finally_depth);
		js_label_place(fc, skip);
	}
}

/* Writes the test of one completion code: the code that follows runs only for it. */
static void
compile_dispatch_case(
	struct js_function_compiler *fc,
	uint32_t kind_register,
	int code,
	uint32_t skip)
{
	uint32_t mark;
	uint32_t test;

	/* The completion code compared with the one wanted. */
	mark = fc->temp_top;
	test = js_temp(fc);
	js_emit2(fc, VM_OP_LOAD_INT, test, (uint32_t)code);
	js_emit3(fc, VM_OP_STRICT_EQ, test, kind_register, test);
	js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, test, skip);
	fc->temp_top = mark;
}
