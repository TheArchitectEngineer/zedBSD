/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compute part of the SPIR-V parser (ws101-p002), included by spirv.c.
 *
 * A GLCompute entry point is a compute shader: its workgroup size comes from
 * the LocalSize or LocalSizeId execution mode, its built-in inputs
 * (LocalInvocationId, LocalInvocationIndex, WorkgroupId, NumWorkgroups and
 * GlobalInvocationId) are read with LOAD_SYSTEM or computed from it, and it
 * reaches memory through storage buffers, whose words it may also change
 * indivisibly (OpAtomic*).  gl_NumWorkGroups is three words in memory the
 * dispatch places: the parser gives them a storage buffer of the set
 * I915_IR_SYSTEM_SET, so the code generator reads them as it reads any
 * storage buffer.
 *
 * A Workgroup variable is shared memory (ws101-p006): the parser lays the
 * variables out one after the other in a natural layout of its own (a
 * scalar 4 bytes, a vector 4 n, an array its elements one after the other,
 * a structure its members in order: SPIR-V gives Workgroup variables no
 * Offset or ArrayStride), and every access is a LOAD_SHARED, STORE_SHARED
 * or ATOMIC of the group's memory -- never forwarded, since another
 * invocation may have written the word.  OpControlBarrier of the workgroup
 * is a BARRIER after the fences its semantics ask for; OpMemoryBarrier and
 * the ordering semantics of an atomic are FENCEs.
 *
 * The file is part of spirv.c's translation unit: it uses that file's types,
 * constants and helpers, and its functions are declared with the others at
 * the top of spirv.c.
 */

/* The execution modes the parser interprets (Khronos SPIR-V spec, section 3.6). */
#define EXECUTION_MODE_LOCAL_SIZE	17U
#define EXECUTION_MODE_LOCAL_SIZE_ID	38U

/* The largest workgroup along x, y and z (the device reports these as maxComputeWorkGroupSize). */
#define COMPUTE_MAX_SIZE_X	128U
#define COMPUTE_MAX_SIZE_Y	128U
#define COMPUTE_MAX_SIZE_Z	64U

/*
 * The memory semantics bits that order other memory accesses around an
 * atomic (Acquire, Release, AcquireRelease, SequentiallyConsistent); the
 * storage-class bits beside them name what is ordered and order nothing by
 * themselves.
 */
#define MEMORY_SEMANTICS_ORDERING	0x1EU

/* The byte offsets of gl_NumWorkGroups' x, y and z in their storage buffer. */
#define SYSTEM_GROUP_COUNT_BYTES	4U

/* The storage-class bits of memory semantics: UniformMemory (buffers) and WorkgroupMemory (shared memory). */
#define MEMORY_SEMANTICS_UNIFORM	0x40U
#define MEMORY_SEMANTICS_WORKGROUP	0x100U

/* The Acquire and AcquireRelease / SequentiallyConsistent bits, which order the accesses after. */
#define MEMORY_SEMANTICS_ACQUIRING	0x1AU

/* The largest size shared memory's layout measures, so no sum overflows; the declaration refuses past the device's. */
#define SHARED_MEASURE_LIMIT		0x40000000U

/* The execution scope of a workgroup barrier (SPIR-V spec, section 3.27, Scope). */
#define SCOPE_WORKGROUP			2U

/*
 * Records one execution mode of the entry point.
 *
 * LocalSize gives the workgroup's size; LocalSizeId names three constants
 * that give it, which are resolved once every constant is declared
 * (i915_spirv_compute_declared()).  Any other OpExecutionMode has no effect
 * on this lowering (a fragment shader's OriginUpperLeft, for one); any other
 * OpExecutionModeId is refused, since its operands could be anything.
 */
static int
i915_spirv_declare_execution_mode(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	/* The instruction must name an entry point and a mode. */
	if (count < 3U)
		return EINVAL;

	/* LocalSize: the three sizes are literals. */
	if (opcode == OP_EXECUTION_MODE && word[2] == EXECUTION_MODE_LOCAL_SIZE) {
		if (count < 6U)
			return EINVAL;
		parser->ir->local_size[0] = word[3];
		parser->ir->local_size[1] = word[4];
		parser->ir->local_size[2] = word[5];
		return 0;
	}

	/* Any other literal execution mode changes nothing the lowering does. */
	if (opcode == OP_EXECUTION_MODE)
		return 0;

	/* LocalSizeId: the three sizes are constants, resolved after the declarations. */
	if (word[2] == EXECUTION_MODE_LOCAL_SIZE_ID) {
		if (count < 6U)
			return EINVAL;
		parser->local_size_id[0] = word[3];
		parser->local_size_id[1] = word[4];
		parser->local_size_id[2] = word[5];
		parser->local_size_by_id = 1;
		return 0;
	}

	/* Refuses an execution mode whose operands this parser does not interpret. */
	return i915_spirv_refuse(parser, opcode, offset, "OpExecutionModeId with a mode other than LocalSizeId");
}

/*
 * Settles the workgroup size of a compute shader once the declarations are
 * read: the constants a LocalSizeId names, and the limits of the device.
 * A module of another stage passes unchanged.
 */
static int
i915_spirv_compute_declared(
	struct i915_spirv_parser *parser)
{
	struct i915_spirv_id *constant;
	uint32_t *size;
	uint32_t invocations;
	uint32_t axis;

	/* Only a compute shader has a workgroup. */
	if (parser->ir->stage != I915_STAGE_COMPUTE)
		return 0;

	/* Reads each size a LocalSizeId names from its constant; a specialization constant is refused. */
	size = parser->ir->local_size;
	if (parser->local_size_by_id != 0) {
		for (axis = 0U; axis < 3U; axis++) {
			constant = i915_spirv_id(parser, parser->local_size_id[axis]);
			if (constant == NULL)
				return EINVAL;
			if (constant->kind != ID_CONSTANT)
				return i915_spirv_refuse(parser, OP_EXECUTION_MODE_ID, 0U, "LocalSizeId of something that is not a constant");
			size[axis] = constant->constant;
		}
	}

	/* A compute shader must say how large its workgroup is. */
	if (size[0] == 0U || size[1] == 0U || size[2] == 0U)
		return i915_spirv_refuse(parser, OP_ENTRY_POINT, 0U, "compute shader without a LocalSize");

	/* Refuses a workgroup larger than the device along any axis. */
	if (size[0] > COMPUTE_MAX_SIZE_X || size[1] > COMPUTE_MAX_SIZE_Y || size[2] > COMPUTE_MAX_SIZE_Z)
		return i915_spirv_refuse(parser, OP_ENTRY_POINT, 0U, "workgroup larger than the device along an axis");

	/* Refuses a workgroup of more invocations than the device runs at once. */
	invocations = size[0] * size[1] * size[2];
	if (invocations > I915_SHADER_MAX_GROUP_INVOCATIONS)
		return i915_spirv_refuse(parser, OP_ENTRY_POINT, 0U, "workgroup of more invocations than the device supports");

	/* Succeeded: the workgroup size is known and within the device's limits. */
	return 0;
}

/*
 * Records a compute shader's built-in input variable: one of the five the
 * dispatch gives every invocation.  Any other built-in is refused.
 */
static int
i915_spirv_declare_system(
	struct i915_spirv_parser *parser,
	struct i915_spirv_id *record,
	uint32_t opcode,
	uint32_t offset)
{
	/* The built-ins a compute invocation has. */
	switch (record->builtin) {
	case BUILTIN_NUM_WORKGROUPS:
	case BUILTIN_WORKGROUP_ID:
	case BUILTIN_LOCAL_INVOCATION_ID:
	case BUILTIN_GLOBAL_INVOCATION_ID:
	case BUILTIN_LOCAL_INVOCATION_INDEX:
		break;
	default:
		return i915_spirv_refuse(parser, opcode, offset, "compute built-in input that is not lowered");
	}

	/* A load of it reads the dispatch's values, not an interface location. */
	record->ptr_kind = PTR_SYSTEM;

	/* Succeeded: the built-in is known. */
	return 0;
}

/*
 * Steps a pointer to a compute built-in one index down: a constant
 * component of its vector.
 */
static int
i915_spirv_chain_system(
	struct i915_spirv_parser *parser,
	struct i915_spirv_id *record,
	struct i915_spirv_id *pointee,
	struct i915_spirv_id *index_record,
	uint32_t opcode,
	uint32_t offset)
{
	/* Only a component of a vector, chosen by a constant, is selected. */
	if (pointee->kind != ID_TYPE_VECTOR || index_record->kind != ID_CONSTANT)
		return i915_spirv_refuse(parser, opcode, offset, "access chain into a compute built-in other than a constant component");
	if (index_record->constant >= pointee->count)
		return EINVAL;

	/* The pointer now addresses the one component. */
	record->component = (int32_t)index_record->constant;
	record->pointee = pointee->type;

	/* Succeeded: the pointer names the component. */
	return 0;
}

/*
 * Lowers a load of a compute built-in: each component it reads, from the
 * dispatch's values (i915_spirv_system_component()).
 */
static int
i915_spirv_lower_load_system(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	const struct i915_spirv_id *pointer,
	const struct i915_spirv_id *variable,
	uint32_t components,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t first;
	uint32_t index;

	/* The built-ins are integer scalars and vectors of at most three. */
	if (components > 3U)
		return i915_spirv_refuse(parser, opcode, offset, "load of a compute built-in wider than three components");

	/* The first component the pointer addresses. */
	first = 0U;
	if (pointer->component >= 0)
		first = (uint32_t)pointer->component;

	/* Names the loaded value's scalars. */
	record = i915_spirv_result(parser, word[2], word[1], components, 0);
	if (record == NULL)
		return EINVAL;

	/* Reads each component the load covers. */
	for (index = 0U; index < components; index++)
		record->comp[index] = i915_spirv_system_component(parser, variable->builtin, first + index);

	/* A value the parser could not make fails the load. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the built-in's scalars are named. */
	return 0;
}

/*
 * Returns the IR value of one component of a compute built-in.
 *
 * The invocation's place in its group and the group's place in the dispatch
 * are the thread's payload (LOAD_SYSTEM); the global invocation is the
 * group's first invocation plus the local one; the group counts are words
 * in memory the dispatch places (the system storage buffer).
 */
static uint32_t
i915_spirv_system_component(
	struct i915_spirv_parser *parser,
	uint32_t builtin,
	uint32_t component)
{
	struct i915_shader_ir_inst *inst;
	uint32_t local;
	uint32_t group;
	uint32_t size;
	uint32_t first;
	uint32_t place;
	uint32_t value;
	uint32_t uniform;

	/* Reads by the built-in. */
	switch (builtin) {
	case BUILTIN_LOCAL_INVOCATION_ID:
		/* The channel's place in its group along the axis. */
		value = i915_spirv_load_system(parser, I915_IR_SYSTEM_LOCAL_ID_X + component);
		return value;

	case BUILTIN_LOCAL_INVOCATION_INDEX:
		/* The channel's linear place in its group. */
		value = i915_spirv_load_system(parser, I915_IR_SYSTEM_LOCAL_INDEX);
		return value;

	case BUILTIN_WORKGROUP_ID:
		/* The group's place in the dispatch along the axis. */
		value = i915_spirv_load_system(parser, I915_IR_SYSTEM_GROUP_ID_X + component);
		return value;

	case BUILTIN_GLOBAL_INVOCATION_ID:
		/* The group's first invocation along the axis, then the channel's place after it. */
		group = i915_spirv_load_system(parser, I915_IR_SYSTEM_GROUP_ID_X + component);
		size = parser->ir->local_size[component];
		first = group;
		if (size != 1U) {
			first = i915_spirv_emit_value(parser, I915_IR_IMUL, group, i915_spirv_integer_constant(parser, size));
		}

		/* A group one invocation wide along the axis has every channel at its first. */
		if (size == 1U)
			return first;

		/* Adds the channel's place in the group. */
		local = i915_spirv_load_system(parser, I915_IR_SYSTEM_LOCAL_ID_X + component);
		value = i915_spirv_emit_value(parser, I915_IR_IADD, first, local);
		return value;

	default:
		break;
	}

	/* The group counts: a word of the system storage buffer, read by every channel. */
	uniform = i915_spirv_system_uniform(parser);
	place = i915_spirv_integer_constant(parser, component * SYSTEM_GROUP_COUNT_BYTES);
	value = i915_spirv_new_value(parser);
	inst = i915_spirv_emit(parser, I915_IR_LOAD_STORAGE, value, place, 0U);
	if (inst != NULL)
		inst->location = uniform;

	/* Succeeded: the group count along the axis. */
	return value;
}

/* Emits a LOAD_SYSTEM of one built-in value and returns it. */
static uint32_t
i915_spirv_load_system(
	struct i915_spirv_parser *parser,
	uint32_t which)
{
	struct i915_shader_ir_inst *inst;
	uint32_t value;

	/* Reads the value from the thread's payload into a fresh value. */
	value = i915_spirv_new_value(parser);
	inst = i915_spirv_emit(parser, I915_IR_LOAD_SYSTEM, value, 0U, 0U);
	if (inst != NULL)
		inst->component = which;

	/* Succeeded: the built-in value. */
	return value;
}

/*
 * Returns the index of the system storage buffer in the uniform list,
 * adding it the first time gl_NumWorkGroups is read.
 */
static uint32_t
i915_spirv_system_uniform(
	struct i915_spirv_parser *parser)
{
	struct i915_shader_ir_uniform *slot;

	/* The buffer is added once; the parser keeps its index plus one. */
	if (parser->system_uniform != 0U)
		return parser->system_uniform - 1U;

	/* Takes the next slot of the uniform list for the three group counts. */
	slot = &parser->ir->uniforms[parser->ir->uniform_count];
	slot->set = I915_IR_SYSTEM_SET;
	slot->binding = 0U;
	slot->kind = I915_IR_UNIFORM_STORAGE;
	slot->offset = 0U;
	slot->size = 0U;
	parser->ir->uniform_count++;
	parser->system_uniform = parser->ir->uniform_count;

	/* Succeeded: the buffer's index. */
	return parser->system_uniform - 1U;
}

/*
 * Lowers an atomic read-modify-write of a storage buffer word:
 * OpAtomicExchange, OpAtomicCompareExchange, OpAtomicIIncrement,
 * OpAtomicIDecrement, OpAtomicIAdd, OpAtomicISub, OpAtomicSMin, UMin, SMax,
 * UMax, And, Or and Xor on a 32-bit integer.
 *
 * The operation runs for the channels of the block it is in (its
 * predicate), in one message of the data port.  An atomic that orders other
 * accesses around it (Acquire, Release and their kind) needs a memory fence,
 * which this parser does not lower yet, and is refused.
 */
static int
i915_spirv_lower_atomic(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_shader_ir_inst *inst;
	struct i915_spirv_id *record;
	struct i915_spirv_id *pointer;
	struct i915_spirv_id *variable;
	struct i915_spirv_id *type;
	uint32_t value_comp[4];
	uint32_t compare_comp[4];
	uint32_t components;
	uint32_t fences;
	uint32_t unequal;
	uint32_t operation;
	uint32_t operands;
	uint32_t value_word;
	uint32_t value;
	uint32_t compare;
	uint32_t place;
	uint32_t dst;
	int error;

	/* Every form names a result type, a result, a pointer, a scope and semantics. */
	if (count < 6U)
		return EINVAL;

	/* The operation, how many words its instruction has and which of them is the value. */
	operands = 7U;
	value_word = 6U;
	compare = NO_VALUE;
	switch (opcode) {
	case OP_ATOMIC_EXCHANGE:
		operation = I915_IR_ATOMIC_XCHG;
		break;
	case OP_ATOMIC_COMPARE_EXCHANGE:
		operation = I915_IR_ATOMIC_CMPXCHG;
		operands = 9U;
		value_word = 7U;
		break;
	case OP_ATOMIC_I_INCREMENT:
		operation = I915_IR_ATOMIC_ADD;
		operands = 6U;
		break;
	case OP_ATOMIC_I_DECREMENT:
		operation = I915_IR_ATOMIC_SUB;
		operands = 6U;
		break;
	case OP_ATOMIC_I_ADD:
		operation = I915_IR_ATOMIC_ADD;
		break;
	case OP_ATOMIC_I_SUB:
		operation = I915_IR_ATOMIC_SUB;
		break;
	case OP_ATOMIC_S_MIN:
		operation = I915_IR_ATOMIC_SMIN;
		break;
	case OP_ATOMIC_U_MIN:
		operation = I915_IR_ATOMIC_UMIN;
		break;
	case OP_ATOMIC_S_MAX:
		operation = I915_IR_ATOMIC_SMAX;
		break;
	case OP_ATOMIC_U_MAX:
		operation = I915_IR_ATOMIC_UMAX;
		break;
	case OP_ATOMIC_AND:
		operation = I915_IR_ATOMIC_AND;
		break;
	case OP_ATOMIC_OR:
		operation = I915_IR_ATOMIC_OR;
		break;
	default:
		operation = I915_IR_ATOMIC_XOR;
		break;
	}

	/* The instruction must carry every operand of its form. */
	if (count < operands)
		return EINVAL;

	/* The fences the semantics ask for around the atomic (ws101-p006); both of a compare and exchange's. */
	error = i915_spirv_semantics(parser, word[5], &fences, opcode, offset);
	if (error != 0)
		return error;
	if (opcode == OP_ATOMIC_COMPARE_EXCHANGE) {
		error = i915_spirv_semantics(parser, word[6], &unequal, opcode, offset);
		if (error != 0)
			return error;
		fences |= unequal;
	}

	/* Resolves the word the atomic works on: a 32-bit integer of a storage buffer or of shared memory. */
	error = i915_spirv_atomic_pointer(parser, word[3], &pointer, &variable, opcode, offset);
	if (error != 0)
		return error;
	type = i915_spirv_id(parser, pointer->pointee);
	if (type == NULL || type->kind != ID_TYPE_INT || type->width != 32U)
		return i915_spirv_refuse(parser, opcode, offset, "atomic on something that is not a 32-bit integer");

	/* The value the operation combines with the word: one, or the operand. */
	if (operands == 6U) {
		value = i915_spirv_integer_constant(parser, 1U);
	} else {
		components = i915_spirv_operand(parser, word[value_word], value_comp);
		if (components != 1U)
			return i915_spirv_refuse(parser, opcode, offset, "atomic whose value is not an integer scalar");
		value = value_comp[0];
	}

	/* A compare and exchange also takes the value the word is compared with. */
	if (opcode == OP_ATOMIC_COMPARE_EXCHANGE) {
		components = i915_spirv_operand(parser, word[8], compare_comp);
		if (components != 1U)
			return i915_spirv_refuse(parser, opcode, offset, "atomic whose comparator is not an integer scalar");
		compare = compare_comp[0];
	}

	/* The word's byte offset in the buffer or in shared memory. */
	place = i915_spirv_storage_offset(parser, pointer, pointer->byte_offset);

	/* Names the result: the word's value before the operation. */
	record = i915_spirv_result(parser, word[2], word[1], 1U, 1);
	if (record == NULL)
		return EINVAL;
	dst = record->comp[0];

	/* An ordering atomic is fenced on both sides (ws101-p006): before for a release, after for an acquire. */
	i915_spirv_fence(parser, fences);

	/* Emits the operation on the word. */
	inst = i915_spirv_emit(parser, I915_IR_ATOMIC, dst, place, value);
	if (inst == NULL)
		return parser->error;
	inst->immediate = operation;
	inst->location = variable->uniform;
	if (pointer->ptr_kind == PTR_SHARED)
		inst->location = I915_IR_LOCATION_SHARED;
	if (compare != NO_VALUE)
		inst->src[2] = compare;

	/* Under a predicate only the block's channels change the word; the predicate follows the values. */
	if (parser->predicate != PREDICATE_ALWAYS) {
		inst->component = I915_IR_ATOMIC_PREDICATED;
		if (compare != NO_VALUE) {
			inst->src[3] = parser->predicate;
		} else {
			inst->src[2] = parser->predicate;
		}
	}

	i915_spirv_fence(parser, fences);

	/* A value the parser could not make fails the atomic. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the atomic is lowered. */
	return 0;
}

/*
 * Lowers OpAtomicLoad and OpAtomicStore of a storage buffer word without
 * ordering semantics: a load or a store of the word, which the data port
 * performs whole.
 */
static int
i915_spirv_lower_atomic_access(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *pointer;
	struct i915_spirv_id *variable;
	uint32_t scalars[4];
	uint32_t components;
	uint32_t pointer_id;
	uint32_t semantics_id;
	uint32_t fences;
	int error;

	/* A load names a result type, a result, the pointer, a scope and semantics; a store the pointer, a scope, semantics and the value. */
	if (opcode == OP_ATOMIC_LOAD) {
		if (count < 6U)
			return EINVAL;
		pointer_id = word[3];
		semantics_id = word[5];
	} else {
		if (count < 5U)
			return EINVAL;
		pointer_id = word[1];
		semantics_id = word[3];
	}

	/* The fences the semantics ask for around the access (ws101-p006). */
	error = i915_spirv_semantics(parser, semantics_id, &fences, opcode, offset);
	if (error != 0)
		return error;

	/* Resolves the word of a storage buffer or of shared memory. */
	error = i915_spirv_atomic_pointer(parser, pointer_id, &pointer, &variable, opcode, offset);
	if (error != 0)
		return error;

	/* A load reads the word as any load of its memory does, fenced after for an acquire. */
	if (opcode == OP_ATOMIC_LOAD) {
		if (pointer->ptr_kind == PTR_SHARED) {
			error = i915_spirv_lower_shared(parser, word, pointer, NULL, 1U, opcode, offset);
		} else {
			error = i915_spirv_lower_storage(parser, word, pointer, variable, NULL, 1U, opcode, offset);
		}
		if (error != 0)
			return error;
		i915_spirv_fence(parser, fences);
		return parser->error;
	}

	/* A store writes the one scalar value it names, fenced before for a release. */
	components = i915_spirv_operand(parser, word[4], scalars);
	if (components != 1U)
		return i915_spirv_refuse(parser, opcode, offset, "atomic store of something that is not a scalar");
	i915_spirv_fence(parser, fences);
	if (pointer->ptr_kind == PTR_SHARED) {
		error = i915_spirv_lower_shared(parser, word, pointer, scalars, 1U, opcode, offset);
	} else {
		error = i915_spirv_lower_storage(parser, word, pointer, variable, scalars, 1U, opcode, offset);
	}
	if (error != 0)
		return error;

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Resolves the pointer of an atomic: it must lead into a storage buffer or
 * into shared memory.  Returns 0 with the pointer and its variable, or the
 * refusal.
 */
static int
i915_spirv_atomic_pointer(
	struct i915_spirv_parser *parser,
	uint32_t pointer_id,
	struct i915_spirv_id **pointer,
	struct i915_spirv_id **variable,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *base;
	struct i915_spirv_id *owner;

	/* Resolves the pointer and the variable it leads to. */
	base = i915_spirv_id(parser, pointer_id);
	if (base == NULL)
		return EINVAL;
	if (base->kind != ID_VARIABLE && base->kind != ID_POINTER)
		return EINVAL;
	owner = i915_spirv_id(parser, base->var);
	if (owner == NULL)
		return EINVAL;

	/* Only the words of a storage buffer or of shared memory are changed atomically. */
	if (base->ptr_kind != PTR_SSBO && base->ptr_kind != PTR_SHARED)
		return i915_spirv_refuse(parser, opcode, offset, "atomic on something that is not a storage buffer or shared memory");

	/* Succeeded: the atomic works on a storage buffer word. */
	*pointer = base;
	*variable = owner;
	return 0;
}

/*
 * Reads the memory semantics of an atomic or a barrier: the fences
 * (I915_IR_FENCE_*) an ordering (Acquire, Release, AcquireRelease,
 * SequentiallyConsistent) asks for, of the memory its storage-class bits
 * name -- buffers (UniformMemory), shared memory (WorkgroupMemory), or both
 * when it names neither -- and none for relaxed semantics.  Returns 0, or
 * the refusal of semantics that are not a constant.
 */
static int
i915_spirv_semantics(
	struct i915_spirv_parser *parser,
	uint32_t semantics_id,
	uint32_t *fences,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *semantics;
	uint32_t bits;

	/* The semantics must be a constant. */
	semantics = i915_spirv_id(parser, semantics_id);
	if (semantics == NULL)
		return EINVAL;
	if (semantics->kind != ID_CONSTANT)
		return i915_spirv_refuse(parser, opcode, offset, "memory semantics that are not a constant");

	/* Relaxed semantics order nothing. */
	bits = semantics->constant;
	*fences = 0U;
	if ((bits & MEMORY_SEMANTICS_ORDERING) == 0U)
		return 0;

	/* The memory the ordering is about: what the storage-class bits name, or all of it. */
	if ((bits & MEMORY_SEMANTICS_UNIFORM) != 0U)
		*fences |= I915_IR_FENCE_GLOBAL;
	if ((bits & MEMORY_SEMANTICS_WORKGROUP) != 0U)
		*fences |= I915_IR_FENCE_SHARED;
	if (*fences == 0U)
		*fences = I915_IR_FENCE_GLOBAL | I915_IR_FENCE_SHARED;

	/* An acquire also waits for the writes before it. */
	if ((bits & MEMORY_SEMANTICS_ACQUIRING) != 0U)
		*fences |= I915_IR_FENCE_ACQUIRE;

	/* Succeeded: the fences are known. */
	return 0;
}

/* Emits a FENCE of the memory `fences` names; nothing when it names none. */
static void
i915_spirv_fence(
	struct i915_spirv_parser *parser,
	uint32_t fences)
{
	struct i915_shader_ir_inst *inst;

	/* Relaxed semantics need no fence. */
	if ((fences & (I915_IR_FENCE_GLOBAL | I915_IR_FENCE_SHARED)) == 0U)
		return;

	/* The fence orders the memory it names. */
	inst = i915_spirv_emit(parser, I915_IR_FENCE, 0U, 0U, 0U);
	if (inst != NULL)
		inst->immediate = fences;
}

/*
 * Lowers OpControlBarrier and OpMemoryBarrier (ws101-p006).
 *
 * A control barrier of the workgroup is a BARRIER after the fences its
 * semantics ask for; one of a narrower scope (a subgroup, an invocation) is
 * its fences only, as is a memory barrier.  A workgroup barrier every
 * thread of the group might not reach the same number of times is refused:
 * one after a return (a thread whose channels all returned leaves the
 * loops early) and one in a loop that not every invocation enters.
 */
static int
i915_spirv_lower_barrier(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_shader_ir_inst *inst;
	struct i915_spirv_id *scope;
	uint32_t fences;
	uint32_t level;
	int error;

	/* A memory barrier: its scope and its semantics, which give the fences. */
	if (opcode == OP_MEMORY_BARRIER) {
		if (count < 3U)
			return EINVAL;
		error = i915_spirv_semantics(parser, word[2], &fences, opcode, offset);
		if (error != 0)
			return error;
		i915_spirv_fence(parser, fences);
		return parser->error;
	}

	/* A control barrier: the execution scope, the memory scope and the semantics. */
	if (count < 4U)
		return EINVAL;
	scope = i915_spirv_id(parser, word[1]);
	if (scope == NULL)
		return EINVAL;
	if (scope->kind != ID_CONSTANT)
		return i915_spirv_refuse(parser, opcode, offset, "barrier whose execution scope is not a constant");
	error = i915_spirv_semantics(parser, word[3], &fences, opcode, offset);
	if (error != 0)
		return error;

	/* A barrier of a narrower scope than the workgroup is its fences: the channels of a thread run together. */
	if (scope->constant != SCOPE_WORKGROUP) {
		i915_spirv_fence(parser, fences);
		return parser->error;
	}

	/* Refuses a barrier some thread might not reach as often as the others. */
	if (parser->returned != 0)
		return i915_spirv_refuse(parser, opcode, offset, "workgroup barrier after a return");
	for (level = 0U; level < parser->loop_depth; level++) {
		if (parser->loops[level].entry_predicate != PREDICATE_ALWAYS)
			return i915_spirv_refuse(parser, opcode, offset, "workgroup barrier in a loop not every invocation enters");
	}

	/* The barrier, after its fences. */
	inst = i915_spirv_emit(parser, I915_IR_BARRIER, 0U, 0U, 0U);
	if (inst != NULL)
		inst->immediate = fences & (I915_IR_FENCE_GLOBAL | I915_IR_FENCE_SHARED);
	parser->ir->uses_barrier = 1U;

	/* A value the parser could not make fails the barrier. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the barrier is lowered. */
	return 0;
}

/*
 * Declares a Workgroup variable of a compute shader (ws101-p006): the next
 * bytes of the group's shared memory, as many as its natural layout takes
 * (i915_spirv_shared_bytes()).  Refuses a type the layout does not cover
 * and a total past I915_IR_MAX_SHARED_BYTES.
 */
static int
i915_spirv_declare_shared(
	struct i915_spirv_parser *parser,
	struct i915_spirv_id *record,
	uint32_t opcode,
	uint32_t offset)
{
	uint32_t bytes;

	/* The bytes the variable takes. */
	bytes = i915_spirv_shared_bytes(parser, record->pointee);
	if (bytes == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "Workgroup variable of a type shared memory does not lay out");

	/* Refuses more shared memory than the device has. */
	if (bytes > I915_IR_MAX_SHARED_BYTES || parser->ir->shared_bytes > I915_IR_MAX_SHARED_BYTES - bytes)
		return i915_spirv_refuse(parser, opcode, offset, "Workgroup variables larger than the shared memory");

	/* The variable starts where the ones before it end. */
	record->ptr_kind = PTR_SHARED;
	record->byte_offset = parser->ir->shared_bytes;
	record->component_stride = 4U;
	parser->ir->shared_bytes += bytes;

	/* Succeeded: the variable has its bytes. */
	return 0;
}

/*
 * Returns the bytes of a type in shared memory's natural layout: 4 for a
 * 32-bit integer or float, 4 n for a vector of n, a column's bytes times the
 * columns for a matrix, the element's times the length for an array, the
 * members' in order for a structure.  0 for anything else (a Boolean, a
 * run-time array, a width other than 32 bits, more than
 * SHARED_MEASURE_LIMIT bytes), which is refused.
 */
static uint32_t
i915_spirv_shared_bytes(
	struct i915_spirv_parser *parser,
	uint32_t type_id)
{
	struct i915_spirv_id *type;
	uint32_t total;
	uint32_t part;
	uint32_t member;

	/* Measures by the kind of type. */
	type = i915_spirv_id(parser, type_id);
	if (type == NULL)
		return 0U;
	switch (type->kind) {
	case ID_TYPE_INT:
	case ID_TYPE_FLOAT:
		/* A 32-bit scalar. */
		if (type->width != 32U)
			return 0U;
		return 4U;

	case ID_TYPE_VECTOR:
	case ID_TYPE_MATRIX:
		/* Its components or columns one after the other. */
		part = i915_spirv_shared_bytes(parser, type->type);
		return part * type->count;

	case ID_TYPE_ARRAY:
		/* Its elements one after the other; a run-time array has no size, and a size past 1 GiB is not measured. */
		part = i915_spirv_shared_bytes(parser, type->type);
		if (type->length == 0U || part == 0U || type->length > SHARED_MEASURE_LIMIT / part)
			return 0U;
		return part * type->length;

	case ID_TYPE_STRUCT:
		/* Its members in order. */
		total = 0U;
		for (member = 0U; member < type->count; member++) {
			part = i915_spirv_shared_bytes(parser, type->member_type[member]);
			if (part == 0U || part > SHARED_MEASURE_LIMIT - total)
				return 0U;
			total += part;
		}
		return total;

	default:
		break;
	}

	/* Anything else is not laid out. */
	return 0U;
}

/*
 * Steps a pointer into shared memory one index down, in the natural layout
 * (ws101-p006): a member after the members before it, an element by the
 * element's bytes, a column by the column's bytes, a component by 4.  Any
 * number of dynamic indices add up to one byte offset, as a storage
 * buffer's do.
 */
static int
i915_spirv_chain_shared(
	struct i915_spirv_parser *parser,
	struct i915_spirv_id *record,
	struct i915_spirv_id *pointee,
	struct i915_spirv_id *index_record,
	uint32_t index_id,
	uint32_t opcode,
	uint32_t offset)
{
	uint32_t scalars[MAX_COMPONENTS];
	uint32_t scalar_count;
	uint32_t selected;
	uint32_t stride;
	uint32_t before;
	uint32_t after;
	uint32_t member;
	int constant;

	/* A constant index names its part now; any other is an IR integer. */
	constant = 0;
	selected = 0U;
	if (index_record->kind == ID_CONSTANT) {
		constant = 1;
		selected = index_record->constant;
	}

	/* A structure's member: after the members before it; only a constant selects one. */
	if (pointee->kind == ID_TYPE_STRUCT) {
		if (constant == 0)
			return i915_spirv_refuse(parser, opcode, offset, "access chain with a dynamic index into a structure");
		if (selected >= pointee->count)
			return EINVAL;
		for (member = 0U; member < selected; member++)
			record->byte_offset += i915_spirv_shared_bytes(parser, pointee->member_type[member]);
		record->pointee = pointee->member_type[selected];
		return 0;
	}

	/* An array's element, a matrix's column or a vector's component: the part's bytes apart. */
	if (pointee->kind != ID_TYPE_ARRAY && pointee->kind != ID_TYPE_MATRIX && pointee->kind != ID_TYPE_VECTOR)
		return i915_spirv_refuse(parser, opcode, offset, "access chain into a scalar");
	stride = i915_spirv_shared_bytes(parser, pointee->type);
	if (stride == 0U)
		return EINVAL;
	record->pointee = pointee->type;

	/* A constant index moves the byte offset. */
	if (constant != 0) {
		if (pointee->kind == ID_TYPE_ARRAY && selected >= pointee->length)
			return EINVAL;
		if (pointee->kind != ID_TYPE_ARRAY && selected >= pointee->count)
			return EINVAL;
		record->byte_offset += selected * stride;
		return 0;
	}

	/* A dynamic index: an integer scalar, folded with any earlier one into bytes. */
	scalar_count = i915_spirv_operand(parser, index_id, scalars);
	if (scalar_count != 1U)
		return i915_spirv_refuse(parser, opcode, offset, "dynamic index that is not an integer scalar");
	if (record->dynamic_index != NO_VALUE) {
		before = i915_spirv_emit_value(parser, I915_IR_IMUL, record->dynamic_index, i915_spirv_integer_constant(parser, record->dynamic_stride));
		after = i915_spirv_emit_value(parser, I915_IR_IMUL, scalars[0], i915_spirv_integer_constant(parser, stride));
		record->dynamic_index = i915_spirv_emit_value(parser, I915_IR_IADD, before, after);
		record->dynamic_stride = 1U;
	} else {
		record->dynamic_index = scalars[0];
		record->dynamic_stride = stride;
	}
	record->dynamic_length = 0U;

	/* A value the parser could not make fails the step. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the pointer addresses the selected part. */
	return 0;
}

/*
 * Lowers a load from (`scalars` NULL) or a store to (`scalars` the stored
 * values) shared memory (ws101-p006): one LOAD_SHARED or STORE_SHARED of
 * each scalar of a scalar or a vector, 4 bytes apart from the pointer's
 * byte offset plus the dynamic index times its stride, under the block's
 * predicate.  Never forwarded: another invocation may have written the word.
 */
static int
i915_spirv_lower_shared(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	const struct i915_spirv_id *pointer,
	const uint32_t *scalars,
	uint32_t components,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	struct i915_spirv_id *type;
	struct i915_shader_ir_inst *inst;
	uint32_t index;
	uint32_t place;
	uint32_t value;

	/* Only a scalar or a vector is moved at once. */
	type = i915_spirv_id(parser, pointer->pointee);
	if (type == NULL)
		return EINVAL;
	if (type->kind != ID_TYPE_INT && type->kind != ID_TYPE_FLOAT && type->kind != ID_TYPE_VECTOR)
		return i915_spirv_refuse(parser, opcode, offset, "load or store of something that is not a scalar or a vector in shared memory");

	/* A load's result names fresh scalars. */
	record = NULL;
	if (scalars == NULL) {
		record = i915_spirv_result(parser, word[2], word[1], components, 0);
		if (record == NULL)
			return EINVAL;
	}

	/* Each scalar at its byte, under the block's predicate. */
	for (index = 0U; index < components; index++) {
		place = i915_spirv_storage_offset(parser, pointer, pointer->byte_offset + index * 4U);
		if (scalars == NULL) {
			value = i915_spirv_new_value(parser);
			inst = i915_spirv_emit(parser, I915_IR_LOAD_SHARED, value, place, 0U);
			record->comp[index] = value;
			if (inst != NULL && parser->predicate != PREDICATE_ALWAYS) {
				inst->src[1] = parser->predicate;
				inst->component = 1U;
			}
			continue;
		}

		/* A store. */
		inst = i915_spirv_emit(parser, I915_IR_STORE_SHARED, 0U, place, scalars[index]);
		if (inst != NULL && parser->predicate != PREDICATE_ALWAYS) {
			inst->src[2] = parser->predicate;
			inst->component = 1U;
		}
	}

	/* A value the parser could not make fails the lowering. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the words are loaded or stored. */
	return 0;
}

/*
 * Lowers OpArrayLength: the elements of a storage buffer's last member, a
 * run-time array, that the buffer's range holds -- the range less the
 * member's offset, divided by its stride.
 */
static int
i915_spirv_lower_array_length(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_shader_ir_inst *inst;
	struct i915_spirv_id *record;
	struct i915_spirv_id *base;
	struct i915_spirv_id *variable;
	struct i915_spirv_id *block;
	struct i915_spirv_id *array;
	uint32_t member;
	uint32_t range;
	uint32_t beyond;
	uint32_t length;
	uint32_t stride;
	uint32_t shift;

	/* The instruction names a result type, a result, the structure pointer and the member. */
	if (count < 5U)
		return EINVAL;

	/* Resolves the pointer to the whole block of a storage buffer. */
	base = i915_spirv_id(parser, word[3]);
	if (base == NULL)
		return EINVAL;
	if (base->kind != ID_VARIABLE && base->kind != ID_POINTER)
		return EINVAL;
	if (base->ptr_kind != PTR_SSBO)
		return i915_spirv_refuse(parser, opcode, offset, "OpArrayLength of something that is not a storage buffer");
	variable = i915_spirv_id(parser, base->var);
	block = i915_spirv_id(parser, base->pointee);
	if (variable == NULL || block == NULL || block->kind != ID_TYPE_STRUCT)
		return EINVAL;

	/* The member must be a run-time array with a stride and an offset. */
	member = word[4];
	if (member >= block->count)
		return EINVAL;
	array = i915_spirv_id(parser, block->member_type[member]);
	if (array == NULL || array->kind != ID_TYPE_ARRAY || array->length != 0U)
		return EINVAL;
	if (array->stride == 0U || block->member_has_offset[member] == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "OpArrayLength of an array without a stride or an offset");

	/* The bytes of the buffer from the array's start on. */
	range = i915_spirv_new_value(parser);
	inst = i915_spirv_emit(parser, I915_IR_STORAGE_SIZE, range, 0U, 0U);
	if (inst != NULL)
		inst->location = variable->uniform;
	beyond = i915_spirv_emit_value(parser, I915_IR_ISUB, range, i915_spirv_integer_constant(parser, block->member_offset[member]));

	/* Whole elements of the array: a shift for a power-of-two stride, a division otherwise. */
	stride = array->stride;
	shift = 0U;
	while (((uint32_t)1U << shift) < stride)
		shift++;
	if (((uint32_t)1U << shift) == stride) {
		length = i915_spirv_emit_value(parser, I915_IR_SHR, beyond, i915_spirv_integer_constant(parser, shift));
	} else {
		length = i915_spirv_emit_value(parser, I915_IR_UDIV, beyond, i915_spirv_integer_constant(parser, stride));
	}

	/* Names the result. */
	record = i915_spirv_result(parser, word[2], word[1], 1U, 0);
	if (record == NULL)
		return EINVAL;
	record->comp[0] = length;

	/* A value the parser could not make fails the instruction. */
	if (parser->error != 0)
		return parser->error;

	/* Succeeded: the length is named. */
	return 0;
}

/*
 * Returns why a compute shader's instruction is refused before it is
 * lowered, or NULL when it may be: images and samplers (the dispatch binds
 * none).
 */
static const char *
i915_spirv_compute_refusal(
	uint32_t opcode)
{
	/* An image of any kind: the dispatch gives a compute shader no binding table. */
	if (opcode >= OP_IMAGE_SAMPLE_IMPLICIT_LOD && opcode <= OP_IMAGE_QUERY_LEVELS)
		return "image or sampler access in a compute shader";

	/* Succeeded: nothing refuses the instruction here. */
	return NULL;
}
