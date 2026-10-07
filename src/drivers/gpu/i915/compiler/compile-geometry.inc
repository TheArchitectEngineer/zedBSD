/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The geometry part of the code generator (ws075-p007a), included by
 * compile.c.
 *
 * Register conventions of a SIMD8 geometry thread on Gen12.0, one channel
 * to an input primitive (Mesa 25.0.7 brw_fs_thread_payload.cpp,
 * gs_thread_payload; brw_compile_gs.cpp):
 *
 *   r0              the thread header (the scratch space in dwords 3 and 5)
 *   r1              the output URB handles, a dword to a channel; bits 31:27
 *                   carry the instance, so a write takes bits 15:0 only
 *   r2              the input primitive's number, when the kernel reads it
 *                   (3DSTATE_GS Include Primitive ID)
 *   then vertices_in the URB handles of the input vertices, one register to
 *                   a vertex (3DSTATE_GS Include Vertex Handles)
 *   then            the push data, from the dispatch's first register after
 *                   the fixed ones (dispatch_grf_start), as a vertex
 *                   shader's; no input is pushed (Vertex URB Entry Read
 *                   Length 0), every input is pulled
 *   values          the vertex count and the cut bits in the first two value
 *                   registers, then the IR's values
 *   staged VUE      as a vertex shader's, r(127 - 4 slots) .. r126:
 *                   [header: 0, layer, 0, 0][position][varyings ascending];
 *                   right below it the header of each vertex write -- the
 *                   output handles (r1 bits 15:0, copied once) and the
 *                   per-slot offsets of the vertex
 *   r126, r127      the last write: the vertex count and the handles
 *
 * An input is read from the producer's VUE with one SIMD8 URB read of the
 * slot it is in (pull), from the handle of the vertex: a constant vertex's
 * own register, or a handle chosen channel by channel by comparing the
 * vertex's number with each vertex in turn.  The reply is a register to a
 * component, up to the one read.
 *
 * The output URB entry (brw_compile_gs.cpp, with the vertex count dynamic):
 * the vertex count in its first 32 bytes, then the control data header (the
 * cut bits), then the vertices, each a whole number of 32-byte units.  The
 * thread ends with the write of the vertex count.
 *
 * The file is part of compile.c's translation unit: it uses that file's
 * state, constants and helpers, and its functions are declared with the
 * others at the top of compile.c.
 */

/* Geometry: the output URB handles, and the input primitive's number when the kernel reads it. */
#define COMPILE_GS_OUTPUT_HANDLES_GRF	1U
#define COMPILE_GS_PRIMITIVE_ID_GRF	2U

/* Geometry: the bits of an output handle; bits 31:27 are the instance (Mesa: the AND with 0xFFFF on Gen12). */
#define COMPILE_GS_HANDLE_MASK		0xFFFFU

/* Geometry: the most vertices an input primitive has, a triangle with adjacency. */
#define COMPILE_GS_MAX_VERTICES_IN	6U

/*
 * Geometry: the 32-byte unit the vertex count, the control data header and
 * each output vertex take in the output URB entry, the 64-byte unit the
 * entry's size is given in, and the largest entry (Mesa 25.0.7
 * brw_eu_defines.h, GFX7_MAX_GS_URB_ENTRY_SIZE_BYTES).
 */
#define COMPILE_GS_HWORD_BYTES		32U
#define COMPILE_GS_URB_UNIT_BYTES	64U
#define COMPILE_GS_MAX_URB_ENTRY_BYTES	(512U * 64U)

/* Geometry: one VUE slot is 16 bytes; the control data header holds 256 bits to a 32-byte unit. */
#define COMPILE_GS_SLOT_BYTES		16U
#define COMPILE_GS_HWORD_BITS		256U

/*
 * Geometry: the control data formats of 3DSTATE_GS -- cut bits (a strip
 * ends after the vertex whose bit is set) and stream IDs, which a shader
 * emitting points has (Mesa 25.0.7 brw_eu_defines.h,
 * GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT and _SID).
 */
#define COMPILE_GS_CONTROL_CUT		0U
#define COMPILE_GS_CONTROL_SID		1U

/*
 * The URB's SIMD8 read message (Mesa 25.0.7 brw_eu_defines.h,
 * GFX8_URB_OPCODE_SIMD8_READ; brw_eu.h brw_urb_desc()): the handle as the
 * one-register header, `length` registers of reply -- component c of the
 * slot in register c -- and the slot in the global offset (OWords).
 */
#define COMPILE_URB_OPCODE_SIMD8_READ	8U
#define COMPILE_DESC_URB_READ(slot, length)	((1U << EU_DESC_MLEN_SHIFT) | \
						 ((uint32_t)(length) << EU_DESC_RLEN_SHIFT) | \
						 EU_DESC_HEADER_PRESENT | \
						 ((uint32_t)(slot) << 4) | \
						 COMPILE_URB_OPCODE_SIMD8_READ)

/*
 * Lays out a geometry thread: where its handles and its push data are, the
 * per-vertex inputs it reads, and the output URB entry its vertices make.
 * An entry larger than the URB takes is refused.
 */
static void
i915_compile_geometry_interface(
	struct i915_compile_state *state)
{
	const struct i915_shader_ir_inst *inst;
	uint32_t index;
	uint32_t control_bits;
	uint32_t entry_bytes;

	/* An input primitive of no vertex, or of more than a triangle with adjacency has, is inconsistent IR. */
	if (state->ir->vertices_in == 0U || state->ir->vertices_in > COMPILE_GS_MAX_VERTICES_IN) {
		state->error = 1;
		return;
	}

	/* The output is a VUE: no colour, no discard, no interpolated input; nor a point size (refused by the parser). */
	if (state->input_count != 0U ||
	    state->fs_outputs != 0U ||
	    state->writes_point_size != 0)
		state->unsupported = 1;

	/* Lists the located per-vertex inputs read, for the pipeline to check against the vertex shader's varyings. */
	for (index = 0U; index < state->ir->instruction_count; index++) {
		inst = &state->ir->instructions[index];
		if (inst->op != I915_IR_LOAD_VERTEX_INPUT)
			continue;
		if (inst->location == I915_IR_LOCATION_POSITION)
			continue;
		if (inst->location == I915_IR_LOCATION_POINT_SIZE)
			continue;
		i915_compile_note(state, state->inputs, &state->input_count, COMPILE_MAX_INPUTS, inst->location);
	}

	/* The input vertices' handles follow r1 and the primitive's number; the push data follows them. */
	state->gs_handle_grf = COMPILE_GS_PRIMITIVE_ID_GRF;
	if (state->ir->uses_primitive_id != 0U)
		state->gs_handle_grf = COMPILE_GS_PRIMITIVE_ID_GRF + 1U;
	state->gs_push_grf = state->gs_handle_grf + state->ir->vertices_in;

	/* One output vertex: the header, the position and the varyings, a whole number of 32-byte units. */
	state->gs_vertex_hwords = ((2U + state->varying_count) * COMPILE_GS_SLOT_BYTES + COMPILE_GS_HWORD_BYTES - 1U) /
				  COMPILE_GS_HWORD_BYTES;

	/*
	 * The control data: a cut bit to each vertex when the shader ends
	 * strips, nothing for points (stream 0 only) or for a shader that never
	 * ends one.
	 */
	control_bits = 0U;
	if (state->ir->output_topology != I915_IR_OUTPUT_POINTS && state->ir->uses_end_primitive != 0U)
		control_bits = state->ir->max_vertices;
	state->gs_control_hwords = (control_bits + COMPILE_GS_HWORD_BITS - 1U) / COMPILE_GS_HWORD_BITS;

	/* The entry: the vertex count, the control data header, then every vertex the shader may emit. */
	entry_bytes = COMPILE_GS_HWORD_BYTES;
	entry_bytes += state->gs_control_hwords * COMPILE_GS_HWORD_BYTES;
	entry_bytes += state->ir->max_vertices * state->gs_vertex_hwords * COMPILE_GS_HWORD_BYTES;

	/* An entry larger than the URB takes is refused, as Mesa refuses it. */
	if (entry_bytes > COMPILE_GS_MAX_URB_ENTRY_BYTES) {
		state->unsupported = 1;
		return;
	}

	/* The entry's size in 64-byte units, as 3DSTATE_URB_ALLOC_GS takes it. */
	state->gs_urb_entry_size = (entry_bytes + COMPILE_GS_URB_UNIT_BYTES - 1U) / COMPILE_GS_URB_UNIT_BYTES;
}

/*
 * Prepares a geometry thread's vertex writes: the output handles without
 * the instance bits, copied once below the staged VUE, and the vertex
 * count and the cut bits, both zero.
 */
static void
i915_compile_geometry_prologue(
	struct i915_compile_state *state)
{
	struct i915_eu_reg counter;
	uint32_t handles;

	/* The handles every vertex write and the thread's end send to. */
	handles = state->vue_grf - COMPILE_GS_WRITE_HEADER_REGS;
	drv_i915_eu_alu2(&state->code,
			 I915_EU_AND,
			 drv_i915_eu_grf_ud(handles),
			 drv_i915_eu_grf_ud(COMPILE_GS_OUTPUT_HANDLES_GRF),
			 drv_i915_eu_imm_ud(COMPILE_GS_HANDLE_MASK));

	/* No vertex is emitted yet. */
	counter = drv_i915_eu_grf_ud(state->gs_count_grf);
	counter.type = COMPILE_TYPE_D;
	drv_i915_eu_mov(&state->code, counter, drv_i915_eu_imm_d(0U));

	/* No strip is ended yet. */
	counter = drv_i915_eu_grf_ud(state->gs_cut_grf);
	counter.type = COMPILE_TYPE_D;
	drv_i915_eu_mov(&state->code, counter, drv_i915_eu_imm_d(0U));
}

/*
 * Lowers a per-vertex input load: one URB read of the slot the input is in,
 * in the producer's VUE of the vertex, from the vertex's handle -- a
 * constant vertex's own register, or one chosen channel by channel.  The
 * value is the reply's register of the component, bit for bit.
 */
static void
i915_compile_load_vertex_input(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	struct i915_eu_buf *code;
	struct i915_eu_reg null;
	uint32_t slot;
	uint32_t component;
	uint32_t index_grf;
	uint32_t handle_grf;
	uint32_t reply_grf;
	uint32_t vertex;
	uint32_t dst;
	int chosen;
	int found;

	/* Emits into the shader's encoder buffer. */
	code = &state->code;

	/* Only a geometry shader reads a vertex of its input primitive. */
	if (state->ir->stage != I915_STAGE_GEOMETRY) {
		state->error = 1;
		return;
	}

	/* Finds the slot and the component of the producer's VUE the input is in. */
	found = i915_compile_vertex_slot(state, inst, &slot, &component);
	if (found != 0) {
		state->unsupported = 1;
		return;
	}

	/* A constant vertex past the primitive's is inconsistent IR. */
	chosen = 0;
	if (inst->immediate == I915_IR_VERTEX_DYNAMIC)
		chosen = 1;
	if (chosen == 0 && inst->immediate >= state->ir->vertices_in) {
		state->error = 1;
		return;
	}

	/* Reads the vertex's number when it is chosen at run time, then gives the value its register. */
	index_grf = COMPILE_NO_GRF;
	if (chosen != 0)
		index_grf = i915_compile_grf(state, inst->src[0]);
	dst = i915_compile_define(state, inst->dst, 1U);

	/* A constant vertex's handle is its own register of the payload. */
	if (chosen == 0) {
		handle_grf = state->gs_handle_grf + inst->immediate;
	} else {
		/* A chosen vertex's handle starts as vertex 0's and takes vertex v's on the channels whose number is v. */
		handle_grf = i915_compile_temporary(state);
		if (state->out_of_registers != 0)
			return;
		drv_i915_eu_mov(code, drv_i915_eu_grf_ud(handle_grf), drv_i915_eu_grf_ud(state->gs_handle_grf));
		null = drv_i915_eu_null();
		null.type = COMPILE_TYPE_D;

		/* Compares the number with each later vertex and selects that vertex's handle where it matches. */
		for (vertex = 1U; vertex < state->ir->vertices_in; vertex++) {
			drv_i915_eu_cmp(code,
					I915_EU_COND_EQ,
					I915_EU_FLAG_F0_0,
					0,
					null,
					drv_i915_eu_grf_d(index_grf),
					drv_i915_eu_imm_d(vertex));
			drv_i915_eu_select(code,
					   I915_EU_FLAG_F0_0,
					   drv_i915_eu_grf_ud(handle_grf),
					   drv_i915_eu_grf_ud(state->gs_handle_grf + vertex),
					   drv_i915_eu_grf_ud(handle_grf));
		}
	}

	/* The reply: the slot's components up to the one read, a register each. */
	reply_grf = i915_compile_temporaries(state, component + 1U);
	if (state->out_of_registers != 0)
		return;

	/* Reads the slot of the vertex's VUE. */
	drv_i915_eu_send(code,
			 drv_i915_eu_grf_ud(reply_grf),
			 drv_i915_eu_grf_ud(handle_grf),
			 drv_i915_eu_null(),
			 EU_SFID_URB,
			 COMPILE_DESC_URB_READ(slot, component + 1U),
			 0U,
			 0,
			 0);

	/* The value is the component's register, bit for bit. */
	drv_i915_eu_mov(code, drv_i915_eu_grf_ud(dst), drv_i915_eu_grf_ud(reply_grf + component));

	/* The reply and a chosen handle are free again. */
	for (vertex = 0U; vertex <= component; vertex++)
		state->grf_busy[reply_grf + vertex] = 0U;
	if (chosen != 0)
		state->grf_busy[handle_grf] = 0U;
}

/*
 * Finds where a per-vertex input is in the producer's VUE: gl_Position is
 * slot 1, gl_PointSize component 3 of the header (slot 0), and a located
 * input the slot after the position of its location among the producer's
 * varyings.  Returns ENOENT for a location the producer does not write, or
 * when there is no producer to say.
 */
static int
i915_compile_vertex_slot(
	const struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst,
	uint32_t *slot,
	uint32_t *component)
{
	uint32_t rank;
	int found;

	/* A component beyond w is not lowered. */
	if (inst->component > 3U)
		return ENOENT;

	/* The position follows the header; the point size is the header's last dword; a located input is a varying. */
	if (inst->location == I915_IR_LOCATION_POSITION) {
		*slot = 1U;
		*component = inst->component;
	} else if (inst->location == I915_IR_LOCATION_POINT_SIZE) {
		*slot = 0U;
		*component = COMPILE_VUE_POINT_SIZE;
	} else {
		/* A located input needs the producer's varyings to be placed. */
		if (state->producer == NULL)
			return ENOENT;

		/* Its slot follows the position, in the producer's ascending location order. */
		found = i915_compile_rank(state->producer->varying_locations, state->producer->varying_count, inst->location, &rank);
		if (found != 0)
			return found;
		*slot = 2U + rank;
		*component = inst->component;
	}

	/* Succeeded: the input's slot and component. */
	return 0;
}

/* Lowers a geometry thread's LOAD_SYSTEM: the input primitive's number, a dword to a channel of its own payload register. */
static void
i915_compile_geometry_system(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	uint32_t dst;

	/* A geometry thread has no other built-in; reading this one without asking for it is inconsistent IR. */
	if (inst->component != I915_IR_SYSTEM_PRIMITIVE_ID || state->ir->uses_primitive_id == 0U) {
		state->error = 1;
		return;
	}

	/* Moves the number into the value's register. */
	dst = i915_compile_define(state, inst->dst, 1U);
	drv_i915_eu_mov(&state->code, drv_i915_eu_grf_ud(dst), drv_i915_eu_grf_ud(COMPILE_GS_PRIMITIVE_ID_GRF));
}

/*
 * Ends a geometry thread: writes how many vertices it emitted into the first
 * dword of its output URB entry, and retires (Mesa's thread end with a
 * dynamic vertex count).  The handles and the count are the payload in
 * r112..r127, as a message that ends the thread needs.
 */
static void
i915_compile_terminate_geometry(
	struct i915_compile_state *state)
{
	struct i915_eu_buf *code;

	/* Emits into the shader's encoder buffer. */
	code = &state->code;

	/* The handles, without the instance bits, and the count, in the last two registers. */
	drv_i915_eu_mov(code, drv_i915_eu_grf_ud(COMPILE_MAX_GRF), drv_i915_eu_grf_ud(state->vue_grf - COMPILE_GS_WRITE_HEADER_REGS));
	drv_i915_eu_mov(code, drv_i915_eu_grf_ud(COMPILE_MAX_GRF - 1U), drv_i915_eu_grf_ud(state->gs_count_grf));

	/* Writes the count at the entry's start and ends the thread. */
	drv_i915_eu_send(code,
			 drv_i915_eu_null(),
			 drv_i915_eu_grf(COMPILE_MAX_GRF),
			 drv_i915_eu_grf(COMPILE_MAX_GRF - 1U),
			 EU_SFID_URB,
			 COMPILE_DESC_URB_WRITE(0U),
			 COMPILE_EX_MLEN(1U),
			 0,
			 1);
}

/* Records in the binary what the draw programs around a geometry kernel. */
static void
i915_compile_describe_geometry(
	const struct i915_compile_state *state,
	struct i915_shader_binary *binary)
{
	/* The varyings it writes, as a vertex kernel's, and where its push data starts. */
	binary->varying_count = state->varying_count;
	kern_memcpy(binary->varying_locations, state->varyings, sizeof(state->varyings));
	binary->dispatch_grf_start = state->gs_push_grf;

	/* The primitives it takes and emits. */
	binary->vertices_in = state->ir->vertices_in;
	binary->output_topology = state->ir->output_topology;

	/* The layout of its output URB entry. */
	binary->output_vertex_hwords = state->gs_vertex_hwords;
	binary->control_data_hwords = state->gs_control_hwords;
	binary->urb_entry_size = state->gs_urb_entry_size;

	/* Points carry stream IDs in their control data; strips cut bits. */
	binary->control_data_format = COMPILE_GS_CONTROL_CUT;
	if (state->ir->output_topology == I915_IR_OUTPUT_POINTS)
		binary->control_data_format = COMPILE_GS_CONTROL_SID;

	/* What the payload carries and what the clipper reads from the VUE. */
	binary->uses_primitive_id = state->ir->uses_primitive_id;
	binary->writes_layer = state->ir->writes_layer;
}
