#!/usr/bin/env python3
# ws075-p001: lists everything in SPIR-V modules that the i915 executor's shader compiler does not take, without stopping
# at the first thing (the compiler itself refuses the whole module at its first refusal).  The rules copy what
# src/drivers/gpu/i915/compiler/spirv.c accepts as of 2026-09-27; each rule names the function it copies.  Shape rules
# (operand sizes, nesting depths, dynamic indices, phis) are not copied: the compiler's first refusal is compared by
# run.sh to catch a module whose only gaps are of that kind.
#
#   survey.py [--first] FILE.spv ...     one line per module: its gaps (or "ok"); --first prints only the first gap
#                                        in module order, for the comparison with the compiler's own refusal
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import subprocess
import sys

# i915_spirv_declare_decoration, i915_spirv_declare_member_decoration.
DECORATIONS = {'Location', 'Binding', 'DescriptorSet', 'BuiltIn', 'ArrayStride', 'Block', 'RelaxedPrecision', 'Flat',
               'Centroid'}
# i915_spirv_decoration_ignored: NoPerspective only on a vertex shader's output.
VERTEX_DECORATIONS = {'NoPerspective'}
MEMBER_DECORATIONS = {'Offset', 'BuiltIn', 'MatrixStride', 'RowMajor', 'ColMajor', 'RelaxedPrecision'}

# i915_spirv_declare_variable: module variables of these storage classes (Input and Output with a Location; an Output
# without one is a block written only through its Position builtin).
STORAGE = {'Input', 'Output', 'PushConstant', 'UniformConstant', 'Uniform'}

# i915_spirv_declare (module level): what is interpreted, and what has no execution semantics.
MODULE = {'OpEntryPoint', 'OpDecorate', 'OpMemberDecorate', 'OpTypeVoid', 'OpTypeBool', 'OpTypeSampler', 'OpTypeFunction',
          'OpTypeImage', 'OpTypeInt', 'OpTypeFloat', 'OpTypeVector', 'OpTypeMatrix', 'OpTypeSampledImage', 'OpTypeArray',
          'OpTypeStruct', 'OpTypePointer', 'OpConstant', 'OpConstantTrue', 'OpConstantFalse', 'OpConstantComposite',
          'OpVariable', 'OpExtInstImport', 'OpNop', 'OpSourceContinued', 'OpSource', 'OpSourceExtension', 'OpName',
          'OpMemberName', 'OpString', 'OpLine', 'OpNoLine', 'OpModuleProcessed', 'OpCapability', 'OpExtension',
          'OpMemoryModel', 'OpExecutionMode'}

# i915_spirv_lower_instruction: the instructions of a function body that are lowered.
BODY = {'OpReturn', 'OpUnreachable', 'OpBranch', 'OpBranchConditional', 'OpSelectionMerge', 'OpLoopMerge', 'OpKill',
        'OpPhi', 'OpFDiv', 'OpFMod', 'OpFRem', 'OpFOrdEqual', 'OpFUnordEqual', 'OpFOrdNotEqual', 'OpFUnordNotEqual',
        'OpFOrdLessThan', 'OpFUnordLessThan', 'OpFOrdGreaterThan', 'OpFUnordGreaterThan', 'OpFOrdLessThanEqual',
        'OpFUnordLessThanEqual', 'OpFOrdGreaterThanEqual', 'OpFUnordGreaterThanEqual', 'OpIEqual', 'OpINotEqual',
        'OpUGreaterThan', 'OpSGreaterThan', 'OpUGreaterThanEqual', 'OpSGreaterThanEqual', 'OpULessThan', 'OpSLessThan',
        'OpULessThanEqual', 'OpSLessThanEqual', 'OpLogicalAnd', 'OpLogicalOr', 'OpLogicalNot', 'OpLogicalEqual',
        'OpLogicalNotEqual', 'OpSelect', 'OpVariable', 'OpAccessChain', 'OpLoad', 'OpStore', 'OpFAdd', 'OpFSub', 'OpFMul',
        'OpIAdd', 'OpISub', 'OpIMul', 'OpUDiv', 'OpSDiv', 'OpUMod', 'OpSRem', 'OpSMod', 'OpShiftRightLogical',
        'OpShiftRightArithmetic', 'OpShiftLeftLogical', 'OpBitwiseOr', 'OpBitwiseXor', 'OpBitwiseAnd', 'OpSNegate', 'OpNot',
        'OpConvertFToU', 'OpConvertFToS', 'OpConvertSToF', 'OpConvertUToF', 'OpBitcast', 'OpVectorTimesScalar',
        'OpMatrixTimesScalar', 'OpVectorTimesMatrix', 'OpMatrixTimesVector', 'OpMatrixTimesMatrix', 'OpTranspose',
        'OpOuterProduct', 'OpFNegate', 'OpDot', 'OpCompositeConstruct', 'OpCompositeExtract', 'OpVectorShuffle', 'OpExtInst',
        'OpImageSampleImplicitLod', 'OpLabel', 'OpFunction', 'OpFunctionEnd', 'OpNop', 'OpLine', 'OpNoLine'}

# i915_spirv_lower_extended: GLSL.std.450.
EXTENDED = {'Round', 'RoundEven', 'Trunc', 'FAbs', 'SAbs', 'FSign', 'SSign', 'Floor', 'Ceil', 'Fract', 'Radians', 'Degrees',
            'Sin', 'Cos', 'Tan', 'Pow', 'Exp', 'Log', 'Exp2', 'Log2', 'Sqrt', 'InverseSqrt', 'FMin', 'UMin', 'SMin', 'FMax',
            'UMax', 'SMax', 'FClamp', 'UClamp', 'SClamp', 'FMix', 'Step', 'SmoothStep', 'Length', 'Distance', 'Cross',
            'Normalize', 'Reflect'}

# i915_spirv_lower_store: the one output builtin that is written.
OUTPUT_BUILTINS = {'Position'}


def disassemble(path):
	"""Returns the module's instructions as lists of words of spirv-dis --raw-id."""
	text = subprocess.run(['spirv-dis', '--raw-id', '--no-header', '--no-color', path], capture_output=True, text=True,
	                      check=True).stdout
	instructions = []
	for line in text.splitlines():
		line = line.split(';')[0].strip()
		if line:
			instructions.append(line.split())
	return instructions


def survey(path):
	"""Returns the module's gaps, in module order, each once."""
	gaps = []

	def gap(text):
		if text not in gaps:
			gaps.append(text)

	instructions = disassemble(path)
	types = {}
	variables = {}
	member_builtins = {}
	builtins = {}
	chains = {}
	constants = {}
	locations = {}
	flats = set()
	loads = {}
	stage = None
	functions = 0
	in_body = False
	for words in instructions:
		# The result id, if any, and the opcode.
		result = None
		if len(words) > 2 and words[1] == '=':
			result = words[0]
			words = words[2:]
		opcode = words[0]
		operands = words[1:]

		# Types, for the variables' and the constants' checks.
		if opcode.startswith('OpType') and result is not None:
			types[result] = words
			if opcode in ('OpTypeInt', 'OpTypeFloat') and int(operands[0]) != 32:
				gap('%d-bit %s' % (int(operands[0]), 'integers' if opcode == 'OpTypeInt' else 'floats'))

		# The entry point's stage.
		if opcode == 'OpEntryPoint':
			stage = operands[0]
			if stage not in ('Vertex', 'Fragment'):
				gap('execution model %s' % stage)

		# Decorations; the builtins kept for the variables.
		if opcode == 'OpDecorate':
			if operands[1] not in DECORATIONS and not (stage == 'Vertex' and operands[1] in VERTEX_DECORATIONS):
				gap('decoration %s' % operands[1])
			if operands[1] == 'BuiltIn':
				builtins[operands[0]] = operands[2]
			if operands[1] == 'Location':
				locations[operands[0]] = int(operands[2])
			if operands[1] == 'Flat':
				flats.add(operands[0])
		if opcode == 'OpMemberDecorate':
			if operands[2] not in MEMBER_DECORATIONS:
				gap('member decoration %s' % operands[2])
			if operands[2] == 'BuiltIn':
				member_builtins[(operands[0], int(operands[1]))] = operands[3]

		# Functions: one, never called.
		if opcode == 'OpFunction':
			functions += 1
			in_body = True
			if functions == 2:
				gap('function calls (more than one function)')
		if opcode == 'OpFunctionCall':
			gap('function calls (more than one function)')
		if opcode == 'OpFunctionEnd':
			in_body = False
			continue

		# Module level.
		if not in_body:
			if opcode not in MODULE:
				gap('module-level %s' % opcode)
			if opcode == 'OpConstant' and types.get(operands[0], ['?'])[0] == 'OpTypeInt':
				constants[result] = int(operands[1])
			if opcode == 'OpConstantComposite':
				kind = types.get(operands[0], ['?'])[0]
				if kind not in ('OpTypeVector', 'OpTypeMatrix'):
					gap('constant composite of %s' % kind[6:].lower())
			if opcode == 'OpVariable':
				storage = operands[1]
				variables[result] = (storage, operands[0])
				if len(operands) > 2:
					gap('variable initializer')
				if storage not in STORAGE:
					gap('module variable in %s' % storage)
				elif storage == 'Input' and result in builtins:
					# i915_spirv_declare_variable: a vertex shader's VertexIndex and InstanceIndex are generated inputs.
					# A fragment shader's FrontFacing is the payload's facing bit.
					generated = stage == 'Vertex' and builtins[result] in ('VertexIndex', 'InstanceIndex')
					if stage == 'Fragment' and builtins[result] == 'FrontFacing':
						generated = True
					if not generated:
						gap('input builtin %s' % builtins[result])
				elif storage == 'Input':
					# i915_spirv_lower_load: an input is floats, or integers in a vertex shader or a Flat fragment input.
					pointee = types.get(types.get(operands[0], ['', '', ''])[2], ['?', ''])
					if pointee[0] == 'OpTypeVector':
						pointee = types.get(pointee[1], ['?'])
					if pointee[0] == 'OpTypeInt' and stage != 'Vertex' and result not in flats:
						gap('integer inputs')
				# i915_compile_store_output: a fragment shader writes one colour, at location 0.
				if storage == 'Output' and stage == 'Fragment' and locations.get(result, 0) != 0:
					gap('colour outputs past location 0 (MRT)')
			continue

		# A function body.
		if opcode not in BODY:
			if opcode == 'OpSwitch':
				gap('OpSwitch')
			elif opcode in ('OpFunctionCall', 'OpFunctionParameter', 'OpReturnValue'):
				gap('function calls (more than one function)')
			else:
				gap('instruction %s' % opcode)
		if opcode == 'OpExtInst' and operands[2] not in EXTENDED:
			gap('GLSL.std.450 %s' % operands[2])
		if opcode == 'OpLoad':
			loads[result] = operands[0]
		if opcode == 'OpImageSampleImplicitLod':
			# i915_spirv_lower_sample: texture(sampler2D, vec2) of four floats, without operands.
			if len(operands) > 3:
				gap('texture() with operands (%s)' % operands[3])
			sampled = types.get(loads.get(operands[1], ''), ['?', ''])
			image = types.get(sampled[1], ['?', '', '?', '0', '0', '0'])
			if image[0] == 'OpTypeImage' and (image[2] != '2D' or image[3] != '0' or image[4] != '0' or image[5] != '0'):
				gap('texture() of a sampler%s%s%s%s' % (image[2], 'Shadow' if image[3] != '0' else '',
				                                         'Array' if image[4] != '0' else '', 'MS' if image[5] != '0' else ''))
			vector = types.get(operands[0], ['?', '', '0'])
			if vector[0] != 'OpTypeVector' or types.get(vector[1], ['?'])[0] != 'OpTypeFloat':
				gap('texture() of an integer sampler')
		if opcode == 'OpVariable':
			pointer = types.get(operands[0])
			pointee = types.get(pointer[2]) if pointer is not None else None
			if pointee is not None and pointee[0] in ('OpTypeArray', 'OpTypeStruct'):
				gap('local %s' % pointee[0][6:].lower())
			if len(operands) > 2:
				gap('local variable initializer')
		if opcode == 'OpAccessChain':
			chains[result] = (operands[2], operands[3:])
		if opcode == 'OpStore':
			base, indices = chains.get(operands[0], (operands[0], []))
			if base in variables and variables[base][0] == 'Output':
				name = builtins.get(base)
				pointer = types.get(variables[base][1])
				if name is None and pointer is not None and indices:
					name = member_builtins.get((pointer[2], constants.get(indices[0])))
				if name is not None and name not in OUTPUT_BUILTINS:
					gap('output builtin %s' % name)
	return gaps


def main():
	first = False
	paths = sys.argv[1:]
	if paths and paths[0] == '--first':
		first = True
		paths = paths[1:]
	for path in paths:
		gaps = survey(path)
		if first:
			gaps = gaps[:1]
		print('%s: %s' % (path, '; '.join(gaps) if gaps else 'ok'))


if __name__ == '__main__':
	main()
