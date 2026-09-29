/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The JavaScript compiler's expressions: each one is computed into a
 * register its caller gives (always a temporary, so the expression may
 * use it for its own steps), using the registers above it for its parts.
 * Names are read and written where the scope pass placed them: a register,
 * a slot of an environment some hops out, or a property of the global
 * object.
 */

#include "js/compile.h"

#include <string.h>

static void expr_function(struct js_function_compiler *fc, struct js_node *node, uint32_t target, const uint16_t *name, size_t length);
static void expr_array(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_object(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static uint32_t expr_property_key(struct js_function_compiler *fc, const struct js_node *key);
static int expr_is_proto(const struct js_node *property);
static void expr_unary(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_typeof(struct js_function_compiler *fc, struct js_node *operand, uint32_t target);
static void expr_delete(struct js_function_compiler *fc, struct js_node *operand, uint32_t target);
static void expr_update(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static uint32_t expr_binary_opcode(int op, int *negate);
static void expr_binary(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_logical(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_nullish_jump(struct js_function_compiler *fc, uint32_t value, uint32_t label);
static void expr_conditional(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_assign(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_assign_logical(struct js_function_compiler *fc, struct js_node *node, struct js_node *left, uint32_t target);
static void expr_member_parts(struct js_function_compiler *fc, struct js_node *member, uint32_t object, uint32_t key);
static void expr_member_get(struct js_function_compiler *fc, struct js_node *member, uint32_t object, uint32_t key, uint32_t target);
static void expr_member_put(struct js_function_compiler *fc, struct js_node *member, uint32_t object, uint32_t key, uint32_t source);
static void expr_member(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_call(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static void expr_new(struct js_function_compiler *fc, struct js_node *node, uint32_t target);
static uint32_t expr_arguments(struct js_function_compiler *fc, struct js_node *list, uint32_t *count);
static void expr_throw_text(struct js_function_compiler *fc, const char *text);
static struct js_node *expr_unwrap(struct js_node *node);
static void expr_unsupported(struct js_function_compiler *fc, struct js_node *node);

/*
 * Compiles an expression into a register.
 */
void
js_compile_expression(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	/* No name for an anonymous function to take. */
	js_compile_expression_named(fc, node, target, NULL, 0);
}

/*
 * Compiles an expression into a register; an anonymous function in it
 * takes the name (what a var, a property or an assignment names it).
 */
void
js_compile_expression_named(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target,
	const uint16_t *name,
	size_t length)
{
	uint32_t saved_line;
	uint32_t saved_column;
	uint32_t constant;

	/* The expression's instructions carry its source position. */
	js_emit_at(fc, node, &saved_line, &saved_column);

	/* Each kind of expression. */
	switch (node->kind) {
	case JS_NODE_NUMBER:
		js_load_number(fc, target, node->number);
		break;
	case JS_NODE_STRING:
		constant = js_constant_string(fc, node->text, node->text_length);
		js_emit2(fc, VM_OP_LOAD_CONST, target, constant);
		break;
	case JS_NODE_REGEXP:
		/* A new object at each evaluation, from the pattern (the body) and the flags (the raw text after it). */
		constant = js_constant_string(fc, node->text, node->text_length);
		js_emit3(fc, VM_OP_NEW_REGEXP, target, constant, js_constant_string(fc, node->raw, node->raw_length));
		break;
	case JS_NODE_TRUE:
		js_load_value(fc, target, VM_VALUE_TRUE);
		break;
	case JS_NODE_FALSE:
		js_load_value(fc, target, VM_VALUE_FALSE);
		break;
	case JS_NODE_NULL:
		js_load_value(fc, target, VM_VALUE_NULL);
		break;
	case JS_NODE_THIS:
		js_emit1(fc, VM_OP_LOAD_THIS, target);
		break;
	case JS_NODE_IDENTIFIER:
		js_load_binding(fc, node->text, node->text_length, target);
		break;
	case JS_NODE_PARENTHESIZED:
		js_compile_expression_named(fc, node->first, target, name, length);
		break;
	case JS_NODE_FUNCTION:
		expr_function(fc, node, target, name, length);
		break;
	case JS_NODE_ARRAY:
		expr_array(fc, node, target);
		break;
	case JS_NODE_OBJECT:
		expr_object(fc, node, target);
		break;
	case JS_NODE_UNARY:
		expr_unary(fc, node, target);
		break;
	case JS_NODE_UPDATE:
		expr_update(fc, node, target);
		break;
	case JS_NODE_BINARY:
		expr_binary(fc, node, target);
		break;
	case JS_NODE_LOGICAL:
		expr_logical(fc, node, target);
		break;
	case JS_NODE_CONDITIONAL:
		expr_conditional(fc, node, target);
		break;
	case JS_NODE_ASSIGN:
		expr_assign(fc, node, target);
		break;
	case JS_NODE_CALL:
		expr_call(fc, node, target);
		break;
	case JS_NODE_NEW:
		expr_new(fc, node, target);
		break;
	case JS_NODE_MEMBER:
		expr_member(fc, node, target);
		break;
	case JS_NODE_SEQUENCE:
		/* Each expression in order; the last one's value stays. */
		for (node = node->first; node != NULL; node = node->next)
			js_compile_expression(fc, node, target);
		break;
	default:
		expr_unsupported(fc, node);
	}

	/* The enclosing expression's later instructions carry its own position again. */
	fc->line = saved_line;
	fc->column = saved_column;
}

/*
 * Reads a name into a register: from its register, its environment's slot,
 * or the global object (a missing global is a ReferenceError).
 */
void
js_load_binding(
	struct js_function_compiler *fc,
	const uint16_t *name,
	size_t length,
	uint32_t target)
{
	struct js_binding *binding;
	uint32_t hops;
	uint32_t key;

	/* A global. */
	binding = js_scope_resolve(fc, name, length, &hops);
	if (binding == NULL) {
		key = js_constant_key(fc, name, length);
		js_emit2(fc, VM_OP_GET_GLOBAL, target, key);
		return;
	}

	/* A captured binding in an environment. */
	if (binding->in_env) {
		js_emit4(fc, VM_OP_GET_ENV, target, fc->env_register, hops, binding->location);
		return;
	}

	/* A binding of this function's frame. */
	js_emit2(fc, VM_OP_MOV, target, binding->location);
}

/*
 * Writes a register's value to a name: its register, its environment's
 * slot, or the global object.  A named function expression's own name
 * cannot be written (silently in sloppy code, a TypeError in strict code).
 */
void
js_store_binding(
	struct js_function_compiler *fc,
	const struct js_node *node,
	const uint16_t *name,
	size_t length,
	uint32_t source)
{
	struct js_binding *binding;
	uint32_t hops;
	uint32_t key;

	UNUSED_PARAMETER(node);

	/* A global. */
	binding = js_scope_resolve(fc, name, length, &hops);
	if (binding == NULL) {
		key = js_constant_key(fc, name, length);
		js_emit2(fc, VM_OP_PUT_GLOBAL, key, source);
		return;
	}

	/* A function expression's own name stays the function. */
	if (binding->kind == JS_BINDING_CALLEE) {
		if (fc->info->strict)
			expr_throw_text(fc, "TypeError: Assignment to constant variable.");
		return;
	}

	/* A captured binding in an environment. */
	if (binding->in_env) {
		js_emit4(fc, VM_OP_PUT_ENV, fc->env_register, hops, binding->location, source);
		return;
	}

	/* A binding of this function's frame. */
	js_emit2(fc, VM_OP_MOV, binding->location, source);
}

/*
 * Writes a register's value to an assignment target: a name or a
 * property.  Any other target (a call, in sloppy code) is a ReferenceError
 * when the assignment runs.
 */
void
js_store_target(
	struct js_function_compiler *fc,
	struct js_node *target,
	uint32_t source)
{
	struct js_node *place;
	uint32_t mark;
	uint32_t object;
	uint32_t key;

	/* A name. */
	place = expr_unwrap(target);
	if (place->kind == JS_NODE_IDENTIFIER) {
		js_store_binding(fc, place, place->text, place->text_length, source);
		return;
	}

	/* A property: its object and key, then the write. */
	if (place->kind == JS_NODE_MEMBER) {
		mark = fc->temp_top;
		object = js_temp(fc);
		key = js_temp(fc);
		expr_member_parts(fc, place, object, key);
		expr_member_put(fc, place, object, key, source);
		fc->temp_top = mark;
		return;
	}

	/* Patterns come with destructuring. */
	if (place->kind == JS_NODE_ARRAY_PATTERN || place->kind == JS_NODE_OBJECT_PATTERN)
		js_compile_unsupported(fc->compiler, place, "destructuring");

	/* Anything else is evaluated, then cannot be assigned. */
	mark = fc->temp_top;
	object = js_temp(fc);
	js_compile_expression(fc, place, object);
	expr_throw_text(fc, "ReferenceError: Invalid left-hand side in assignment");
	fc->temp_top = mark;
}

/* Compiles a function expression: its code unit, and a closure over the running environment. */
static void
expr_function(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target,
	const uint16_t *name,
	size_t length)
{
	struct vm_code *code;
	uint32_t constant;

	/* A function with its own name keeps it; an anonymous one takes the given name. */
	if (node->text != NULL) {
		name = NULL;
		length = 0;
	}

	/* The code unit, a constant of this one. */
	code = js_compile_function(fc->compiler, fc, node, name, length);
	constant = js_constant(fc, vm_value_cell(code));

	/* The closure. */
	js_emit3(fc, VM_OP_NEW_CLOSURE, target, constant, fc->env_register);
}

/* Compiles an array literal: each element pushed in order, a hole leaving a gap. */
static void
expr_array(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	struct js_node *element;
	uint32_t mark;
	uint32_t value;

	/* The array. */
	js_emit1(fc, VM_OP_NEW_ARRAY, target);

	/* Each element. */
	mark = fc->temp_top;
	value = js_temp(fc);
	for (element = node->first; element != NULL; element = element->next) {
		if (element->kind == JS_NODE_HOLE) {
			js_emit1(fc, VM_OP_ARRAY_HOLE, target);
			continue;
		}

		/* Spread comes later. */
		if (element->kind == JS_NODE_SPREAD)
			js_compile_unsupported(fc->compiler, element, "spread");

		/* The value at the end. */
		js_compile_expression(fc, element, value);
		js_emit2(fc, VM_OP_ARRAY_PUSH, target, value);
	}

	/* The temporaries are free again. */
	fc->temp_top = mark;
}

/* Compiles an object literal: each property defined in order (accessors in halves, __proto__ as the prototype). */
static void
expr_object(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	struct js_node *property;
	struct js_node *key_node;
	uint32_t mark;
	uint32_t key;
	uint32_t value;
	uint32_t constant;
	int computed;
	int proto;
	int named;

	/* The object. */
	js_emit1(fc, VM_OP_NEW_OBJECT, target);

	/* Each property. */
	mark = fc->temp_top;
	key = js_temp(fc);
	value = js_temp(fc);
	for (property = node->first; property != NULL; property = property->next) {
		if (property->op == JS_PROPERTY_SPREAD)
			js_compile_unsupported(fc->compiler, property, "spread");

		/* The key: computed into a register, or a constant (loaded when an instruction needs it in one). */
		key_node = property->first;
		computed = 0;
		if ((property->flags & JS_FLAG_COMPUTED) != 0U)
			computed = 1;
		constant = 0;
		if (computed) {
			js_compile_expression(fc, key_node, key);
			js_emit2(fc, VM_OP_TO_PROPERTY_KEY, key, key);
		} else {
			constant = expr_property_key(fc, key_node);
			js_emit2(fc, VM_OP_LOAD_CONST, key, constant);
		}

		/* The value; an anonymous function takes a plain key's name. */
		named = 0;
		if (!computed && (key_node->kind == JS_NODE_IDENTIFIER || key_node->kind == JS_NODE_STRING))
			named = 1;
		if (named) {
			js_compile_expression_named(fc, property->second, value, key_node->text, key_node->text_length);
		} else {
			js_compile_expression(fc, property->second, value);
		}

		/* An accessor's half. */
		if (property->op == JS_PROPERTY_GET) {
			js_emit3(fc, VM_OP_DEFINE_GETTER, target, key, value);
			continue;
		}

		/* A setter's half. */
		if (property->op == JS_PROPERTY_SET) {
			js_emit3(fc, VM_OP_DEFINE_SETTER, target, key, value);
			continue;
		}

		/* __proto__: value sets the prototype instead of defining a property. */
		proto = expr_is_proto(property);
		if (proto) {
			js_emit2(fc, VM_OP_SET_PROTO, target, value);
			continue;
		}

		/* A data property. */
		if (computed) {
			js_emit3(fc, VM_OP_DEFINE_ELEM, target, key, value);
		} else {
			js_emit3(fc, VM_OP_DEFINE_PROP, target, constant, value);
		}
	}

	/* The temporaries are free again. */
	fc->temp_top = mark;
}

/* Makes the constant key of a property's plain key: a name, a string or a number (its numeral). */
static uint32_t
expr_property_key(
	struct js_function_compiler *fc,
	const struct js_node *key)
{
	vm_value property_key;
	uint32_t constant;
	int status;

	/* A name or a string is its text. */
	if (key->kind == JS_NODE_IDENTIFIER || key->kind == JS_NODE_STRING) {
		constant = js_constant_key(fc, key->text, key->text_length);
		return constant;
	}

	/* A number is its string's key. */
	if (key->kind != JS_NODE_NUMBER)
		js_compile_unsupported(fc->compiler, key, "this kind of property key");
	status = vm_to_key(fc->compiler->realm, vm_value_number(key->number), &property_key);
	if (status != 0)
		js_compile_out_of_memory(fc->compiler);

	/* Reports its constant. */
	constant = js_constant(fc, property_key);
	return constant;
}

/* Tells whether a property of an object literal is __proto__: value (which sets the prototype). */
static int
expr_is_proto(
	const struct js_node *property)
{
	const struct js_node *key;
	int spelled;

	/* Only a plain key: value property. */
	if (property->op != JS_PROPERTY_INIT)
		return 0;
	if ((property->flags & (JS_FLAG_COMPUTED | JS_FLAG_SHORTHAND)) != 0U)
		return 0;

	/* The key spells __proto__ as a name or a string. */
	key = property->first;
	if (key->kind != JS_NODE_IDENTIFIER && key->kind != JS_NODE_STRING)
		return 0;
	spelled = js_text_is(key->text, key->text_length, "__proto__");
	if (!spelled)
		return 0;

	/* It is. */
	return 1;
}

/* Compiles a unary operator. */
static void
expr_unary(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t opcode;

	/* typeof and delete look at their operand's form. */
	if (node->op == JS_P_TYPEOF) {
		expr_typeof(fc, node->first, target);
		return;
	}

	/* delete looks at what it deletes. */
	if (node->op == JS_P_DELETE) {
		expr_delete(fc, node->first, target);
		return;
	}

	/* The operand. */
	js_compile_expression(fc, node->first, target);

	/* void is undefined after it. */
	if (node->op == JS_P_VOID) {
		js_load_value(fc, target, VM_VALUE_UNDEFINED);
		return;
	}

	/* The operator on the value. */
	switch (node->op) {
	case JS_P_NOT:
		opcode = VM_OP_NOT;
		break;
	case JS_P_MINUS:
		opcode = VM_OP_NEG;
		break;
	case JS_P_PLUS:
		opcode = VM_OP_TO_NUMBER;
		break;
	case JS_P_TILDE:
		opcode = VM_OP_BIT_NOT;
		break;
	default:
		js_compile_unsupported(fc->compiler, node, "this unary operator");
	}

	/* The operator on the value, in place. */
	js_emit2(fc, opcode, target, target);
}

/* Compiles typeof: a name no scope declares is undefined rather than a ReferenceError. */
static void
expr_typeof(
	struct js_function_compiler *fc,
	struct js_node *operand,
	uint32_t target)
{
	struct js_binding *binding;
	struct js_node *place;
	uint32_t hops;
	uint32_t key;

	/* A global name is read without the error. */
	place = expr_unwrap(operand);
	binding = NULL;
	if (place->kind == JS_NODE_IDENTIFIER)
		binding = js_scope_resolve(fc, place->text, place->text_length, &hops);
	if (place->kind == JS_NODE_IDENTIFIER && binding == NULL) {
		key = js_constant_key(fc, place->text, place->text_length);
		js_emit2(fc, VM_OP_GET_GLOBAL_TYPEOF, target, key);
	} else {
		js_compile_expression(fc, operand, target);
	}

	/* The type's name. */
	js_emit2(fc, VM_OP_TYPEOF, target, target);
}

/* Compiles delete: of a property, of a global name, or of anything else (true). */
static void
expr_delete(
	struct js_function_compiler *fc,
	struct js_node *operand,
	uint32_t target)
{
	struct js_binding *binding;
	struct js_node *place;
	uint32_t mark;
	uint32_t object;
	uint32_t key;
	uint32_t hops;
	uint32_t constant;

	/* A property. */
	place = expr_unwrap(operand);
	if (place->kind == JS_NODE_MEMBER) {
		if (place->second->kind == JS_NODE_PRIVATE_NAME || (place->flags & JS_FLAG_OPTIONAL) != 0U)
			expr_unsupported(fc, place);
		mark = fc->temp_top;
		object = js_temp(fc);
		js_compile_expression(fc, place->first, object);
		if ((place->flags & JS_FLAG_COMPUTED) != 0U) {
			key = js_temp(fc);
			js_compile_expression(fc, place->second, key);
			js_emit3(fc, VM_OP_DELETE_ELEM, target, object, key);
		} else {
			constant = js_constant_key(fc, place->second->text, place->second->text_length);
			js_emit3(fc, VM_OP_DELETE_PROP, target, object, constant);
		}

		/* The temporaries are free again. */
		fc->temp_top = mark;
		return;
	}

	/* A name: a global is deleted from the global object; a declared one stays (false). */
	if (place->kind == JS_NODE_IDENTIFIER) {
		binding = js_scope_resolve(fc, place->text, place->text_length, &hops);
		if (binding != NULL) {
			js_load_value(fc, target, VM_VALUE_FALSE);
			return;
		}

		/* A global is deleted from the global object. */
		constant = js_constant_key(fc, place->text, place->text_length);
		js_emit2(fc, VM_OP_DELETE_GLOBAL, target, constant);
		return;
	}

	/* Anything else is evaluated and deleted trivially. */
	js_compile_expression(fc, operand, target);
	js_load_value(fc, target, VM_VALUE_TRUE);
}

/* Compiles ++ and --: the new number is written back; the value is the new one (prefix) or the old number (postfix). */
static void
expr_update(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	struct js_node *place;
	uint32_t mark;
	uint32_t object;
	uint32_t key;
	uint32_t updated;
	uint32_t opcode;
	int is_member;

	/* The operand's current value (a property's object and key are kept for the write). */
	place = expr_unwrap(node->first);
	mark = fc->temp_top;
	object = js_temp(fc);
	key = js_temp(fc);
	updated = js_temp(fc);
	is_member = 0;
	if (place->kind == JS_NODE_MEMBER) {
		is_member = 1;
		expr_member_parts(fc, place, object, key);
		expr_member_get(fc, place, object, key, target);
	} else if (place->kind == JS_NODE_IDENTIFIER) {
		js_load_binding(fc, place->text, place->text_length, target);
	} else {
		js_compile_expression(fc, place, target);
		expr_throw_text(fc, "ReferenceError: Invalid left-hand side expression in update operation");
		fc->temp_top = mark;
		return;
	}

	/* The old value as a number, and the new one. */
	opcode = VM_OP_INC;
	if (node->op == JS_P_DECREMENT)
		opcode = VM_OP_DEC;
	js_emit2(fc, VM_OP_TO_NUMBER, target, target);
	js_emit2(fc, opcode, updated, target);

	/* The new value written back; the prefix form's value is the new one. */
	if (is_member) {
		expr_member_put(fc, place, object, key, updated);
	} else {
		js_store_binding(fc, place, place->text, place->text_length, updated);
	}

	/* The prefix form's value is the new one. */
	if ((node->flags & JS_FLAG_PREFIX) != 0U)
		js_emit2(fc, VM_OP_MOV, target, updated);
	fc->temp_top = mark;
}

/* Reports the opcode of a binary operator (and whether its result is negated: != and !==). */
static uint32_t
expr_binary_opcode(
	int op,
	int *negate)
{
	/* The operators by their punctuator (the compound assignments' too). */
	*negate = 0;
	switch (op) {
	case JS_P_PLUS:
	case JS_P_PLUS_ASSIGN:
		return VM_OP_ADD;
	case JS_P_MINUS:
	case JS_P_MINUS_ASSIGN:
		return VM_OP_SUB;
	case JS_P_STAR:
	case JS_P_STAR_ASSIGN:
		return VM_OP_MUL;
	case JS_P_SLASH:
	case JS_P_SLASH_ASSIGN:
		return VM_OP_DIV;
	case JS_P_PERCENT:
	case JS_P_PERCENT_ASSIGN:
		return VM_OP_MOD;
	case JS_P_POWER:
	case JS_P_POWER_ASSIGN:
		return VM_OP_EXP;
	case JS_P_SHL:
	case JS_P_SHL_ASSIGN:
		return VM_OP_SHL;
	case JS_P_SAR:
	case JS_P_SAR_ASSIGN:
		return VM_OP_SAR;
	case JS_P_SHR:
	case JS_P_SHR_ASSIGN:
		return VM_OP_SHR;
	case JS_P_AMP:
	case JS_P_AMP_ASSIGN:
		return VM_OP_BIT_AND;
	case JS_P_BAR:
	case JS_P_BAR_ASSIGN:
		return VM_OP_BIT_OR;
	case JS_P_CARET:
	case JS_P_CARET_ASSIGN:
		return VM_OP_BIT_XOR;
	case JS_P_LT:
		return VM_OP_LESS;
	case JS_P_GT:
		return VM_OP_GREATER;
	case JS_P_LE:
		return VM_OP_LESS_EQ;
	case JS_P_GE:
		return VM_OP_GREATER_EQ;
	case JS_P_EQ:
		return VM_OP_LOOSE_EQ;
	case JS_P_NE:
		*negate = 1;
		return VM_OP_LOOSE_EQ;
	case JS_P_STRICT_EQ:
		return VM_OP_STRICT_EQ;
	case JS_P_STRICT_NE:
		*negate = 1;
		return VM_OP_STRICT_EQ;
	case JS_P_INSTANCEOF:
		return VM_OP_INSTANCEOF;
	case JS_P_IN:
		return VM_OP_IN;
	default:
		break;
	}

	/* No such operator. */
	return VM_OPCODE_COUNT;
}

/* Compiles a binary operator: the left, then the right, then the operator. */
static void
expr_binary(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t mark;
	uint32_t right;
	uint32_t opcode;
	int negate;

	/* The operator (#x in o, with private names, comes with classes). */
	opcode = expr_binary_opcode(node->op, &negate);
	if (opcode == VM_OPCODE_COUNT || node->first->kind == JS_NODE_PRIVATE_NAME)
		expr_unsupported(fc, node);

	/* The operands in order. */
	mark = fc->temp_top;
	js_compile_expression(fc, node->first, target);
	right = js_temp(fc);
	js_compile_expression(fc, node->second, right);

	/* The operator, negated for != and !==. */
	js_emit3(fc, opcode, target, target, right);
	if (negate)
		js_emit2(fc, VM_OP_NOT, target, target);
	fc->temp_top = mark;
}

/* Compiles &&, || and ??: the right side runs only when the left does not decide. */
static void
expr_logical(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t end;

	/* The left side decides first. */
	js_compile_expression(fc, node->first, target);
	end = js_label_new(fc);
	if (node->op == JS_P_AND) {
		js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, target, end);
	} else if (node->op == JS_P_OR) {
		js_emit_jump(fc, VM_OP_JUMP_IF_TRUE, target, end);
	} else {
		expr_nullish_jump(fc, target, end);
	}

	/* Otherwise the right side's value. */
	js_compile_expression(fc, node->second, target);
	js_label_place(fc, end);
}

/* Jumps to a label unless a value is undefined or null (what ?? keeps). */
static void
expr_nullish_jump(
	struct js_function_compiler *fc,
	uint32_t value,
	uint32_t label)
{
	uint32_t mark;
	uint32_t test;

	/* == null holds exactly for undefined and null. */
	mark = fc->temp_top;
	test = js_temp(fc);
	js_load_value(fc, test, VM_VALUE_NULL);
	js_emit3(fc, VM_OP_LOOSE_EQ, test, value, test);
	js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, test, label);
	fc->temp_top = mark;
}

/* Compiles the conditional operator. */
static void
expr_conditional(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t otherwise;
	uint32_t end;

	/* The test. */
	js_compile_expression(fc, node->first, target);
	otherwise = js_label_new(fc);
	end = js_label_new(fc);
	js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, target, otherwise);

	/* The consequent, then around the alternate. */
	js_compile_expression(fc, node->second, target);
	js_emit_jump(fc, VM_OP_JUMP, 0, end);

	/* The alternate. */
	js_label_place(fc, otherwise);
	js_compile_expression(fc, node->third, target);
	js_label_place(fc, end);
}

/*
 * Compiles an assignment: plain (=), compound (+= and the others: read,
 * operate, write) or logical (&&=, ||=, ??=).  Its value is the value
 * written.
 */
static void
expr_assign(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	struct js_node *left;
	uint32_t mark;
	uint32_t object;
	uint32_t key;
	uint32_t right;
	uint32_t opcode;
	int negate;
	int is_member;

	/* The logical assignments short-circuit. */
	left = expr_unwrap(node->first);
	if (node->op == JS_P_AND_ASSIGN || node->op == JS_P_OR_ASSIGN || node->op == JS_P_NULLISH_ASSIGN) {
		expr_assign_logical(fc, node, left, target);
		return;
	}

	/* A pattern comes with destructuring; a call cannot be assigned. */
	if (left->kind == JS_NODE_ARRAY_PATTERN || left->kind == JS_NODE_OBJECT_PATTERN)
		js_compile_unsupported(fc->compiler, left, "destructuring");
	if (left->kind != JS_NODE_IDENTIFIER && left->kind != JS_NODE_MEMBER) {
		js_compile_expression(fc, node->second, target);
		js_store_target(fc, left, target);
		return;
	}

	/* A property's object and key first. */
	mark = fc->temp_top;
	object = js_temp(fc);
	key = js_temp(fc);
	right = js_temp(fc);
	is_member = 0;
	if (left->kind == JS_NODE_MEMBER) {
		is_member = 1;
		expr_member_parts(fc, left, object, key);
	}

	/* A plain assignment: the value (an anonymous function takes a name's name). */
	if (node->op == JS_P_ASSIGN) {
		if (is_member) {
			js_compile_expression(fc, node->second, target);
		} else {
			js_compile_expression_named(fc, node->second, target, left->text, left->text_length);
		}
	} else {
		/* A compound one: the current value, the right side, the operator. */
		opcode = expr_binary_opcode(node->op, &negate);
		if (opcode == VM_OPCODE_COUNT)
			expr_unsupported(fc, node);
		if (is_member) {
			expr_member_get(fc, left, object, key, target);
		} else {
			js_load_binding(fc, left->text, left->text_length, target);
		}

		/* The right side, then the operator. */
		js_compile_expression(fc, node->second, right);
		js_emit3(fc, opcode, target, target, right);
	}

	/* The write. */
	if (is_member) {
		expr_member_put(fc, left, object, key, target);
	} else {
		js_store_binding(fc, left, left->text, left->text_length, target);
	}

	/* The temporaries are free again. */
	fc->temp_top = mark;
}

/* Compiles &&=, ||= and ??=: the write happens only when the current value does not decide. */
static void
expr_assign_logical(
	struct js_function_compiler *fc,
	struct js_node *node,
	struct js_node *left,
	uint32_t target)
{
	uint32_t mark;
	uint32_t object;
	uint32_t key;
	uint32_t end;
	int is_member;

	/* Only a name or a property. */
	if (left->kind != JS_NODE_IDENTIFIER && left->kind != JS_NODE_MEMBER)
		expr_unsupported(fc, left);

	/* The current value. */
	mark = fc->temp_top;
	object = js_temp(fc);
	key = js_temp(fc);
	is_member = 0;
	if (left->kind == JS_NODE_MEMBER) {
		is_member = 1;
		expr_member_parts(fc, left, object, key);
		expr_member_get(fc, left, object, key, target);
	} else {
		js_load_binding(fc, left->text, left->text_length, target);
	}

	/* The current value decides first. */
	end = js_label_new(fc);
	if (node->op == JS_P_AND_ASSIGN) {
		js_emit_jump(fc, VM_OP_JUMP_IF_FALSE, target, end);
	} else if (node->op == JS_P_OR_ASSIGN) {
		js_emit_jump(fc, VM_OP_JUMP_IF_TRUE, target, end);
	} else {
		expr_nullish_jump(fc, target, end);
	}

	/* Otherwise the right side, written. */
	if (is_member) {
		js_compile_expression(fc, node->second, target);
		expr_member_put(fc, left, object, key, target);
	} else {
		js_compile_expression_named(fc, node->second, target, left->text, left->text_length);
		js_store_binding(fc, left, left->text, left->text_length, target);
	}

	/* The end, where the current value decided. */
	js_label_place(fc, end);
	fc->temp_top = mark;
}

/* Computes a property access's object and (when computed) its key into registers. */
static void
expr_member_parts(
	struct js_function_compiler *fc,
	struct js_node *member,
	uint32_t object,
	uint32_t key)
{
	/* Private names come with classes; optional chains later. */
	if (member->second->kind == JS_NODE_PRIVATE_NAME || (member->flags & JS_FLAG_OPTIONAL) != 0U)
		expr_unsupported(fc, member);
	if (member->first->kind == JS_NODE_SUPER)
		js_compile_unsupported(fc->compiler, member->first, "super");

	/* The object, then a computed key. */
	js_compile_expression(fc, member->first, object);
	if ((member->flags & JS_FLAG_COMPUTED) != 0U)
		js_compile_expression(fc, member->second, key);
}

/* Reads a property whose object and key are in registers. */
static void
expr_member_get(
	struct js_function_compiler *fc,
	struct js_node *member,
	uint32_t object,
	uint32_t key,
	uint32_t target)
{
	uint32_t constant;

	/* A computed key is in its register; a name is a constant. */
	if ((member->flags & JS_FLAG_COMPUTED) != 0U) {
		js_emit3(fc, VM_OP_GET_ELEM, target, object, key);
		return;
	}

	/* A name is a constant. */
	constant = js_constant_key(fc, member->second->text, member->second->text_length);
	js_emit3(fc, VM_OP_GET_PROP, target, object, constant);
}

/* Writes a property whose object and key are in registers. */
static void
expr_member_put(
	struct js_function_compiler *fc,
	struct js_node *member,
	uint32_t object,
	uint32_t key,
	uint32_t source)
{
	uint32_t constant;

	/* A computed key is in its register; a name is a constant. */
	if ((member->flags & JS_FLAG_COMPUTED) != 0U) {
		js_emit3(fc, VM_OP_PUT_ELEM, object, key, source);
		return;
	}

	/* A name is a constant. */
	constant = js_constant_key(fc, member->second->text, member->second->text_length);
	js_emit3(fc, VM_OP_PUT_PROP, object, constant, source);
}

/* Compiles a property access. */
static void
expr_member(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t mark;
	uint32_t key;

	/* The object into the target itself, a computed key above it, then the read. */
	mark = fc->temp_top;
	key = js_temp(fc);
	expr_member_parts(fc, node, target, key);
	expr_member_get(fc, node, target, key, target);
	fc->temp_top = mark;
}

/* Compiles a call: a property's call has the object as this; any other callee has undefined. */
static void
expr_call(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	struct js_node *callee;
	uint32_t mark;
	uint32_t function;
	uint32_t this_value;
	uint32_t key;
	uint32_t first;
	uint32_t count;

	/* Optional calls and super calls come later. */
	if ((node->flags & JS_FLAG_OPTIONAL) != 0U)
		js_compile_unsupported(fc->compiler, node, "optional chaining");
	callee = expr_unwrap(node->first);
	if (callee->kind == JS_NODE_SUPER)
		js_compile_unsupported(fc->compiler, callee, "super");

	/* The function and this: a property's object, or undefined. */
	mark = fc->temp_top;
	function = js_temp(fc);
	this_value = js_temp(fc);
	if (callee->kind == JS_NODE_MEMBER) {
		key = js_temp(fc);
		expr_member_parts(fc, callee, this_value, key);
		expr_member_get(fc, callee, this_value, key, function);
	} else {
		js_compile_expression(fc, node->first, function);
		js_load_value(fc, this_value, VM_VALUE_UNDEFINED);
	}

	/* The arguments, then the call. */
	first = expr_arguments(fc, node->second, &count);
	js_emit5(fc, VM_OP_CALL, target, function, this_value, first, count);
	fc->temp_top = mark;
}

/* Compiles new: the constructor, the arguments, then the construction. */
static void
expr_new(
	struct js_function_compiler *fc,
	struct js_node *node,
	uint32_t target)
{
	uint32_t mark;
	uint32_t constructor;
	uint32_t first;
	uint32_t count;

	/* The constructor (it is also new.target). */
	mark = fc->temp_top;
	constructor = js_temp(fc);
	js_compile_expression(fc, node->first, constructor);

	/* The arguments, then the construction. */
	first = expr_arguments(fc, node->second, &count);
	js_emit5(fc, VM_OP_CONSTRUCT, target, constructor, constructor, first, count);
	fc->temp_top = mark;
}

/* Computes a call's arguments into consecutive registers; reports the first and the count. */
static uint32_t
expr_arguments(
	struct js_function_compiler *fc,
	struct js_node *list,
	uint32_t *count)
{
	struct js_node *argument;
	uint32_t first;
	uint32_t index;

	/* One register each, taken together so they follow each other. */
	*count = 0;
	first = fc->temp_top;
	for (argument = list; argument != NULL; argument = argument->next) {
		if (argument->kind == JS_NODE_SPREAD)
			js_compile_unsupported(fc->compiler, argument, "spread");
		js_temp(fc);
		(*count)++;
	}

	/* Each argument into its register, in order. */
	index = 0;
	for (argument = list; argument != NULL; argument = argument->next) {
		js_compile_expression(fc, argument, first + index);
		index++;
	}

	/* The first register (with no arguments, where they would start, which must still be one of the frame's). */
	if (fc->register_count <= first)
		fc->register_count = first + 1U;
	return first;
}

/* Throws an error text (the engine's errors are strings until the Error objects exist). */
static void
expr_throw_text(
	struct js_function_compiler *fc,
	const char *text)
{
	struct vm_string *string;
	uint32_t mark;
	uint32_t value;
	uint32_t constant;

	/* The text as a constant. */
	string = vm_string_from_utf8(fc->compiler->realm->heap, text, strlen(text));
	if (string == NULL)
		js_compile_out_of_memory(fc->compiler);
	constant = js_constant(fc, vm_value_cell(string));

	/* Loaded, then thrown. */
	mark = fc->temp_top;
	value = js_temp(fc);
	js_emit2(fc, VM_OP_LOAD_CONST, value, constant);
	js_emit1(fc, VM_OP_THROW, value);
	fc->temp_top = mark;
}

/* Finds the expression inside parentheses. */
static struct js_node *
expr_unwrap(
	struct js_node *node)
{
	/* Through each pair of parentheses. */
	while (node->kind == JS_NODE_PARENTHESIZED)
		node = node->first;

	/* The expression inside. */
	return node;
}

/* Refuses an expression the compiler does not support yet, naming what it is. */
static void
expr_unsupported(
	struct js_function_compiler *fc,
	struct js_node *node)
{
	const char *what;

	/* The construct's name. */
	switch (node->kind) {
	case JS_NODE_TEMPLATE:
	case JS_NODE_TAGGED_TEMPLATE:
		what = "template literals";
		break;
	case JS_NODE_REGEXP:
		what = "regular expressions";
		break;
	case JS_NODE_BIGINT:
		what = "BigInt";
		break;
	case JS_NODE_CLASS:
		what = "classes";
		break;
	case JS_NODE_SUPER:
		what = "super";
		break;
	case JS_NODE_YIELD:
		what = "generators";
		break;
	case JS_NODE_AWAIT:
		what = "async functions";
		break;
	case JS_NODE_META_PROPERTY:
		what = "new.target and import.meta";
		break;
	case JS_NODE_IMPORT_CALL:
		what = "import()";
		break;
	case JS_NODE_SPREAD:
		what = "spread";
		break;
	case JS_NODE_OPTIONAL_CHAIN:
		what = "optional chaining";
		break;
	case JS_NODE_PRIVATE_NAME:
		what = "private names";
		break;
	case JS_NODE_ARRAY_PATTERN:
	case JS_NODE_OBJECT_PATTERN:
	case JS_NODE_ASSIGNMENT_PATTERN:
		what = "destructuring";
		break;
	default:
		what = js_node_kind_name(node->kind);
		break;
	}

	/* The failure. */
	js_compile_unsupported(fc->compiler, node, what);
}
