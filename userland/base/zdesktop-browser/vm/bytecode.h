/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The shared bytecode (plan/ws074/design.md §11.5): the instructions the
 * interpreter runs for JavaScript and for Wasm, and the code unit that
 * holds them.  The JS compiler (ws074-p025) and the Wasm compiler
 * (ws074-p033) write it; the tests assemble it by hand.
 *
 * An instruction is one 32-bit word naming its opcode followed by the
 * operand words the opcode's entry in vm_opcodes says: register numbers
 * (slots of the frame), constant numbers (the code's table of boxed
 * values), immediates, jump offsets (in words, from the start of the
 * instruction) and counts.  JavaScript's instructions work on boxed values
 * (vm_value); Wasm's work on the raw 64 bits in a register (an i32 in the
 * low 32 bits), since Wasm's types are known when it is compiled.
 */

#ifndef ZDESKTOP_BROWSER_VM_BYTECODE_H
#define ZDESKTOP_BROWSER_VM_BYTECODE_H

#include "vm/vm.h"

/* The most operands an instruction has. */
#define VM_OPERANDS_MAX		5U

/* The kinds of operand. */
enum vm_operand_kind {
	VM_OPERAND_REGISTER,
	VM_OPERAND_CONSTANT,
	VM_OPERAND_IMMEDIATE,
	VM_OPERAND_JUMP,
	VM_OPERAND_COUNT
};

/*
 * The opcodes.  The comment of each gives its operands (R a register, C a
 * constant, I an immediate, J a jump, N a count) and what it does.
 */
enum vm_opcode {
	/* Shared. */
	VM_OP_NOP,		/* -: nothing */
	VM_OP_MOV,		/* R dst, R src */
	VM_OP_LOAD_CONST,	/* R dst, C value */
	VM_OP_LOAD_INT,		/* R dst, I int32 (boxed) */
	VM_OP_JUMP,		/* J target */
	VM_OP_JUMP_IF_TRUE,	/* R value, J target (ToBoolean) */
	VM_OP_JUMP_IF_FALSE,	/* R value, J target (ToBoolean) */
	VM_OP_CALL,		/* R dst, R callee, R this, R first argument, N argument count */
	VM_OP_RETURN,		/* R value */
	VM_OP_THROW,		/* R value */
	VM_OP_LOOP_HINT,	/* -: a loop's head (counted for the JIT's tier-up later) */

	/* JavaScript (boxed values). */
	VM_OP_ADD,		/* R dst, R left, R right */
	VM_OP_SUB,		/* R dst, R left, R right */
	VM_OP_MUL,		/* R dst, R left, R right */
	VM_OP_LESS,		/* R dst, R left, R right */
	VM_OP_STRICT_EQ,	/* R dst, R left, R right */
	VM_OP_NEW_OBJECT,	/* R dst */
	VM_OP_NEW_ARRAY,	/* R dst */
	VM_OP_GET_PROP,		/* R dst, R object, C key */
	VM_OP_PUT_PROP,		/* R object, C key, R value */
	VM_OP_GET_ELEM,		/* R dst, R object, R key */
	VM_OP_PUT_ELEM,		/* R object, R key, R value */
	VM_OP_GET_GLOBAL,	/* R dst, C key */
	VM_OP_PUT_GLOBAL,	/* C key, R value */

	/* Wasm (raw values). */
	VM_OP_I32_CONST,	/* R dst, I value */
	VM_OP_I32_ADD,		/* R dst, R left, R right */
	VM_OP_I32_SUB,		/* R dst, R left, R right */
	VM_OP_I32_MUL,		/* R dst, R left, R right */
	VM_OP_I32_LT_S,		/* R dst, R left, R right */
	VM_OP_I32_EQZ,		/* R dst, R value */
	VM_OP_BR_IF,		/* R i32, J target (taken when not zero) */
	VM_OP_I64_CONST,	/* R dst, I low, I high */
	VM_OP_I64_ADD,		/* R dst, R left, R right */
	VM_OP_F64_CONST,	/* R dst, I low bits, I high bits */
	VM_OP_F64_ADD,		/* R dst, R left, R right */
	VM_OP_F64_MUL,		/* R dst, R left, R right */
	VM_OP_BOX_I32,		/* R dst, R i32: the JS number of a raw i32 */
	VM_OP_BOX_F64,		/* R dst, R f64: the JS number of a raw f64 */

	VM_OPCODE_COUNT
};

/*
 * What the interpreter and the checker know of an opcode: its name and
 * the kinds of its operands.
 */
struct vm_opcode_info {
	const char *name;
	unsigned operand_count;
	unsigned char kinds[VM_OPERANDS_MAX];
};

/*
 * One exception handler: an exception thrown by an instruction in
 * [start, end) (word offsets) lands at handler with the exception in the
 * register.
 */
struct vm_handler {
	uint32_t start;
	uint32_t end;
	uint32_t handler;
	uint32_t exception_register;
};

/*
 * A code unit: one function's instructions and what they refer to.
 *
 * The words, the constants and the handlers are malloc'd copies freed with
 * the cell; the constants are boxed values the cell keeps alive.  The
 * first parameter_count registers receive the arguments.  A code unit is
 * checked when it is made and never changes.
 */
struct vm_code {
	struct vm_cell cell;
	uint32_t *words;
	uint32_t word_count;
	uint32_t register_count;
	uint32_t parameter_count;
	uint32_t constant_count;
	vm_value *constants;
	struct vm_handler *handlers;
	uint32_t handler_count;
	uint32_t reserved;
	struct vm_string *name;
};

/* The opcodes' table (code.c). */
extern const struct vm_opcode_info vm_opcodes[VM_OPCODE_COUNT];

/* Code units (code.c). */
extern const struct vm_cell_type vm_code_type;
int vm_code_create(struct vm_heap *heap, const struct vm_code *model, struct vm_code **code, uint32_t *bad_offset);
int vm_code_dump(const struct vm_code *code, struct wb_buffer *out);

/* Bytecode functions (function.c). */
struct vm_function *vm_function_create(struct vm_realm *realm, struct vm_code *code);

#endif
