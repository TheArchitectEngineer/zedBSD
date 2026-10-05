/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's built-ins: the functions (a table of signatures),
 * and the variables and constants each stage has.
 *
 * A signature is a string of type codes, the return type first:
 *
 *   G genType (float, vec2..4)     I genIType     U genUType     B genBType
 *   V vec2..4   W ivec2..4   X uvec2..4   Y bvec2..4   (the same size as G)
 *   f float  i int  u uint  b bool  2 3 4 vec2..4  j k ivec2..3
 *   M a square matrix   m any matrix (the same one for every m)
 *   a sampler1D  d sampler2D  e sampler3D  c samplerCube
 *   h sampler1DShadow  s sampler2DShadow
 *   A D E C a sampler of that dimension with any texel type, T its gvec4
 *   R a 2D array sampler of any texel type
 *   p sampler2DArrayShadow  q samplerCubeShadow
 *   F a rectangle sampler of any texel type  r sampler2DRectShadow
 *   Q a buffer sampler of any texel type
 *   Z a multisample sampler of any texel type
 *   v void (a result only)
 *
 * Every generic code of one signature takes the same size, so a
 * signature is tried once for each size.
 */

#include "internal.h"

#include <string.h>

/* The stages a built-in exists in (BI_BOTH: every stage, the compute stage of ws101-p008 among them). */
#define BI_VERTEX		1U
#define BI_FRAGMENT		2U
#define BI_GEOMETRY		4U
#define BI_COMPUTE		8U
#define BI_BOTH			15U

/* The SPIR-V opcodes of the built-ins that are one instruction. */
#define BI_OP_ANY		154U
#define BI_OP_BITCAST		124U
#define BI_OP_TRANSPOSE		84U
#define BI_OP_ISNAN		156U
#define BI_OP_ISINF		157U
#define BI_OP_IEQUAL		170U
#define BI_OP_INOTEQUAL		171U
#define BI_OP_UGREATER		172U
#define BI_OP_SGREATER		173U
#define BI_OP_UGREATER_EQUAL	174U
#define BI_OP_SGREATER_EQUAL	175U
#define BI_OP_ULESS		176U
#define BI_OP_SLESS		177U
#define BI_OP_ULESS_EQUAL	178U
#define BI_OP_SLESS_EQUAL	179U
#define BI_OP_FEQUAL		180U
#define BI_OP_FNOTEQUAL		183U
#define BI_OP_FLESS		184U
#define BI_OP_FGREATER		186U
#define BI_OP_FLESS_EQUAL	188U
#define BI_OP_FGREATER_EQUAL	190U
#define BI_OP_LOGICAL_EQUAL	164U
#define BI_OP_LOGICAL_NOTEQUAL	165U
#define BI_OP_DPDX		207U
#define BI_OP_DPDY		208U
#define BI_OP_FWIDTH		209U

/*
 * The built-in functions, grouped by name (glsl_builtin_first and
 * glsl_builtin_next walk one name's group).
 */
static const struct glsl_builtin builtins_table[] = {
	/* Angle and trigonometry. */
	{ "radians", "GG", GLSL_BI_EXT, 11U, BI_BOTH, GLSL_IN_ALL },
	{ "degrees", "GG", GLSL_BI_EXT, 12U, BI_BOTH, GLSL_IN_ALL },
	{ "sin", "GG", GLSL_BI_EXT, 13U, BI_BOTH, GLSL_IN_ALL },
	{ "cos", "GG", GLSL_BI_EXT, 14U, BI_BOTH, GLSL_IN_ALL },
	{ "tan", "GG", GLSL_BI_EXT, 15U, BI_BOTH, GLSL_IN_ALL },
	{ "asin", "GG", GLSL_BI_EXT, 16U, BI_BOTH, GLSL_IN_ALL },
	{ "acos", "GG", GLSL_BI_EXT, 17U, BI_BOTH, GLSL_IN_ALL },
	{ "atan", "GGG", GLSL_BI_EXT, 25U, BI_BOTH, GLSL_IN_ALL },
	{ "atan", "GG", GLSL_BI_EXT, 18U, BI_BOTH, GLSL_IN_ALL },
	{ "sinh", "GG", GLSL_BI_EXT, 19U, BI_BOTH, GLSL_IN_130_UP },
	{ "cosh", "GG", GLSL_BI_EXT, 20U, BI_BOTH, GLSL_IN_130_UP },
	{ "tanh", "GG", GLSL_BI_EXT, 21U, BI_BOTH, GLSL_IN_130_UP },
	{ "asinh", "GG", GLSL_BI_EXT, 22U, BI_BOTH, GLSL_IN_130_UP },
	{ "acosh", "GG", GLSL_BI_EXT, 23U, BI_BOTH, GLSL_IN_130_UP },
	{ "atanh", "GG", GLSL_BI_EXT, 24U, BI_BOTH, GLSL_IN_130_UP },

	/* Exponentials. */
	{ "pow", "GGG", GLSL_BI_EXT, 26U, BI_BOTH, GLSL_IN_ALL },
	{ "exp", "GG", GLSL_BI_EXT, 27U, BI_BOTH, GLSL_IN_ALL },
	{ "log", "GG", GLSL_BI_EXT, 28U, BI_BOTH, GLSL_IN_ALL },
	{ "exp2", "GG", GLSL_BI_EXT, 29U, BI_BOTH, GLSL_IN_ALL },
	{ "log2", "GG", GLSL_BI_EXT, 30U, BI_BOTH, GLSL_IN_ALL },
	{ "sqrt", "GG", GLSL_BI_EXT, 31U, BI_BOTH, GLSL_IN_ALL },
	{ "inversesqrt", "GG", GLSL_BI_EXT, 32U, BI_BOTH, GLSL_IN_ALL },

	/* Common functions. */
	{ "abs", "GG", GLSL_BI_EXT, 4U, BI_BOTH, GLSL_IN_ALL },
	{ "abs", "II", GLSL_BI_EXT, 5U, BI_BOTH, GLSL_IN_130_UP },
	{ "sign", "GG", GLSL_BI_EXT, 6U, BI_BOTH, GLSL_IN_ALL },
	{ "sign", "II", GLSL_BI_EXT, 7U, BI_BOTH, GLSL_IN_130_UP },
	{ "floor", "GG", GLSL_BI_EXT, 8U, BI_BOTH, GLSL_IN_ALL },
	{ "ceil", "GG", GLSL_BI_EXT, 9U, BI_BOTH, GLSL_IN_ALL },
	{ "fract", "GG", GLSL_BI_EXT, 10U, BI_BOTH, GLSL_IN_ALL },
	{ "trunc", "GG", GLSL_BI_EXT, 3U, BI_BOTH, GLSL_IN_130_UP },
	{ "round", "GG", GLSL_BI_EXT, 1U, BI_BOTH, GLSL_IN_130_UP },
	{ "roundEven", "GG", GLSL_BI_EXT, 2U, BI_BOTH, GLSL_IN_130_UP },
	{ "mod", "GGG", GLSL_BI_SPECIAL, GLSL_SPECIAL_MOD, BI_BOTH, GLSL_IN_ALL },
	{ "mod", "GGf", GLSL_BI_SPECIAL, GLSL_SPECIAL_MOD, BI_BOTH, GLSL_IN_ALL },
	{ "min", "GGG", GLSL_BI_EXT, 37U, BI_BOTH, GLSL_IN_ALL },
	{ "min", "GGf", GLSL_BI_EXT, 37U, BI_BOTH, GLSL_IN_ALL },
	{ "min", "III", GLSL_BI_EXT, 39U, BI_BOTH, GLSL_IN_130_UP },
	{ "min", "IIi", GLSL_BI_EXT, 39U, BI_BOTH, GLSL_IN_130_UP },
	{ "min", "UUU", GLSL_BI_EXT, 38U, BI_BOTH, GLSL_IN_130_UP },
	{ "min", "UUu", GLSL_BI_EXT, 38U, BI_BOTH, GLSL_IN_130_UP },
	{ "max", "GGG", GLSL_BI_EXT, 40U, BI_BOTH, GLSL_IN_ALL },
	{ "max", "GGf", GLSL_BI_EXT, 40U, BI_BOTH, GLSL_IN_ALL },
	{ "max", "III", GLSL_BI_EXT, 42U, BI_BOTH, GLSL_IN_130_UP },
	{ "max", "IIi", GLSL_BI_EXT, 42U, BI_BOTH, GLSL_IN_130_UP },
	{ "max", "UUU", GLSL_BI_EXT, 41U, BI_BOTH, GLSL_IN_130_UP },
	{ "max", "UUu", GLSL_BI_EXT, 41U, BI_BOTH, GLSL_IN_130_UP },
	{ "clamp", "GGGG", GLSL_BI_EXT, 43U, BI_BOTH, GLSL_IN_ALL },
	{ "clamp", "GGff", GLSL_BI_EXT, 43U, BI_BOTH, GLSL_IN_ALL },
	{ "clamp", "IIII", GLSL_BI_EXT, 45U, BI_BOTH, GLSL_IN_130_UP },
	{ "clamp", "IIii", GLSL_BI_EXT, 45U, BI_BOTH, GLSL_IN_130_UP },
	{ "clamp", "UUUU", GLSL_BI_EXT, 44U, BI_BOTH, GLSL_IN_130_UP },
	{ "clamp", "UUuu", GLSL_BI_EXT, 44U, BI_BOTH, GLSL_IN_130_UP },
	{ "mix", "GGGG", GLSL_BI_EXT, 46U, BI_BOTH, GLSL_IN_ALL },
	{ "mix", "GGGf", GLSL_BI_EXT, 46U, BI_BOTH, GLSL_IN_ALL },
	{ "mix", "GGGB", GLSL_BI_SPECIAL, GLSL_SPECIAL_MIX_BOOL, BI_BOTH, GLSL_IN_130_UP },
	{ "step", "GGG", GLSL_BI_EXT, 48U, BI_BOTH, GLSL_IN_ALL },
	{ "step", "GfG", GLSL_BI_EXT, 48U, BI_BOTH, GLSL_IN_ALL },
	{ "smoothstep", "GGGG", GLSL_BI_EXT, 49U, BI_BOTH, GLSL_IN_ALL },
	{ "smoothstep", "GffG", GLSL_BI_EXT, 49U, BI_BOTH, GLSL_IN_ALL },
	{ "isnan", "BG", GLSL_BI_OP, BI_OP_ISNAN, BI_BOTH, GLSL_IN_130_UP },
	{ "isinf", "BG", GLSL_BI_OP, BI_OP_ISINF, BI_BOTH, GLSL_IN_130_UP },

	/* Geometric functions. */
	{ "length", "fG", GLSL_BI_EXT, 66U, BI_BOTH, GLSL_IN_ALL },
	{ "distance", "fGG", GLSL_BI_EXT, 67U, BI_BOTH, GLSL_IN_ALL },
	{ "dot", "fGG", GLSL_BI_SPECIAL, GLSL_SPECIAL_DOT, BI_BOTH, GLSL_IN_ALL },
	{ "cross", "333", GLSL_BI_EXT, 68U, BI_BOTH, GLSL_IN_ALL },
	{ "normalize", "GG", GLSL_BI_EXT, 69U, BI_BOTH, GLSL_IN_ALL },
	{ "faceforward", "GGGG", GLSL_BI_SPECIAL, GLSL_SPECIAL_FACEFORWARD, BI_BOTH, GLSL_IN_ALL },
	{ "reflect", "GGG", GLSL_BI_EXT, 71U, BI_BOTH, GLSL_IN_ALL },
	{ "refract", "GGGf", GLSL_BI_EXT, 72U, BI_BOTH, GLSL_IN_ALL },

	/* Matrix functions. */
	{ "matrixCompMult", "mmm", GLSL_BI_SPECIAL, GLSL_SPECIAL_MATRIX_COMP_MULT, BI_BOTH, GLSL_IN_ALL },
	{ "outerProduct", "oVV", GLSL_BI_SPECIAL, GLSL_SPECIAL_OUTER_PRODUCT, BI_BOTH, GLSL_IN_120_UP },
	{ "transpose", "tm", GLSL_BI_OP, BI_OP_TRANSPOSE, BI_BOTH, GLSL_IN_120_UP },
	{ "determinant", "fM", GLSL_BI_EXT, 33U, BI_BOTH, GLSL_IN_140_UP },
	{ "inverse", "MM", GLSL_BI_EXT, 34U, BI_BOTH, GLSL_IN_140_UP },

	/* Bits of floats and packed values (3.30, OpenGL ES 3.00). */
	{ "floatBitsToInt", "IG", GLSL_BI_OP, BI_OP_BITCAST, BI_BOTH, GLSL_IN_330_UP },
	{ "floatBitsToUint", "UG", GLSL_BI_OP, BI_OP_BITCAST, BI_BOTH, GLSL_IN_330_UP },
	{ "intBitsToFloat", "GI", GLSL_BI_OP, BI_OP_BITCAST, BI_BOTH, GLSL_IN_330_UP },
	{ "uintBitsToFloat", "GU", GLSL_BI_OP, BI_OP_BITCAST, BI_BOTH, GLSL_IN_330_UP },
	{ "packSnorm2x16", "u2", GLSL_BI_EXT, 56U, BI_BOTH, GLSL_IN_ES300 },
	{ "packUnorm2x16", "u2", GLSL_BI_EXT, 57U, BI_BOTH, GLSL_IN_ES300 },
	{ "packHalf2x16", "u2", GLSL_BI_EXT, 58U, BI_BOTH, GLSL_IN_ES300 },
	{ "unpackSnorm2x16", "2u", GLSL_BI_EXT, 60U, BI_BOTH, GLSL_IN_ES300 },
	{ "unpackUnorm2x16", "2u", GLSL_BI_EXT, 61U, BI_BOTH, GLSL_IN_ES300 },
	{ "unpackHalf2x16", "2u", GLSL_BI_EXT, 62U, BI_BOTH, GLSL_IN_ES300 },

	/* Vector relational functions. */
	{ "lessThan", "YVV", GLSL_BI_OP, BI_OP_FLESS, BI_BOTH, GLSL_IN_ALL },
	{ "lessThan", "YWW", GLSL_BI_OP, BI_OP_SLESS, BI_BOTH, GLSL_IN_ALL },
	{ "lessThan", "YXX", GLSL_BI_OP, BI_OP_ULESS, BI_BOTH, GLSL_IN_130_UP },
	{ "lessThanEqual", "YVV", GLSL_BI_OP, BI_OP_FLESS_EQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "lessThanEqual", "YWW", GLSL_BI_OP, BI_OP_SLESS_EQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "lessThanEqual", "YXX", GLSL_BI_OP, BI_OP_ULESS_EQUAL, BI_BOTH, GLSL_IN_130_UP },
	{ "greaterThan", "YVV", GLSL_BI_OP, BI_OP_FGREATER, BI_BOTH, GLSL_IN_ALL },
	{ "greaterThan", "YWW", GLSL_BI_OP, BI_OP_SGREATER, BI_BOTH, GLSL_IN_ALL },
	{ "greaterThan", "YXX", GLSL_BI_OP, BI_OP_UGREATER, BI_BOTH, GLSL_IN_130_UP },
	{ "greaterThanEqual", "YVV", GLSL_BI_OP, BI_OP_FGREATER_EQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "greaterThanEqual", "YWW", GLSL_BI_OP, BI_OP_SGREATER_EQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "greaterThanEqual", "YXX", GLSL_BI_OP, BI_OP_UGREATER_EQUAL, BI_BOTH, GLSL_IN_130_UP },
	{ "equal", "YVV", GLSL_BI_OP, BI_OP_FEQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "equal", "YWW", GLSL_BI_OP, BI_OP_IEQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "equal", "YXX", GLSL_BI_OP, BI_OP_IEQUAL, BI_BOTH, GLSL_IN_130_UP },
	{ "equal", "YYY", GLSL_BI_OP, BI_OP_LOGICAL_EQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "notEqual", "YVV", GLSL_BI_OP, BI_OP_FNOTEQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "notEqual", "YWW", GLSL_BI_OP, BI_OP_INOTEQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "notEqual", "YXX", GLSL_BI_OP, BI_OP_INOTEQUAL, BI_BOTH, GLSL_IN_130_UP },
	{ "notEqual", "YYY", GLSL_BI_OP, BI_OP_LOGICAL_NOTEQUAL, BI_BOTH, GLSL_IN_ALL },
	{ "any", "bY", GLSL_BI_SPECIAL, GLSL_SPECIAL_ANY, BI_BOTH, GLSL_IN_ALL },
	{ "all", "bY", GLSL_BI_SPECIAL, GLSL_SPECIAL_ALL, BI_BOTH, GLSL_IN_ALL },
	{ "not", "YY", GLSL_BI_SPECIAL, GLSL_SPECIAL_NOT, BI_BOTH, GLSL_IN_ALL },

	/* Texture lookups of OpenGL ES 1.00 and desktop 1.10 to 1.30. */
	{ "texture2D", "4d2", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_LEGACY },
	{ "texture2D", "4d2f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_LEGACY },
	{ "texture2DProj", "4d3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_LEGACY },
	{ "texture2DProj", "4d4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_LEGACY },
	{ "texture2DProj", "4d3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_BIAS, BI_FRAGMENT, GLSL_IN_LEGACY },
	{ "texture2DProj", "4d4f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_BIAS, BI_FRAGMENT, GLSL_IN_LEGACY },
	{ "texture2DLod", "4d2f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_VERTEX, GLSL_IN_LEGACY },
	{ "texture2DProjLod", "4d3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_LOD, BI_VERTEX, GLSL_IN_LEGACY },
	{ "texture2DProjLod", "4d4f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_LOD, BI_VERTEX, GLSL_IN_LEGACY },
	{ "textureCube", "4c3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_LEGACY },
	{ "textureCube", "4c3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_LEGACY },
	{ "textureCubeLod", "4c3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_VERTEX, GLSL_IN_LEGACY },
	{ "texture1D", "4af", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_DESKTOP },
	{ "texture1D", "4aff", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_DESKTOP },
	{ "texture1DProj", "4a2", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_DESKTOP },
	{ "texture1DProj", "4a4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_DESKTOP },
	{ "texture1DLod", "4aff", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_VERTEX, GLSL_IN_DESKTOP },
	{ "texture3D", "4e3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_DESKTOP },
	{ "texture3D", "4e3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_DESKTOP },
	{ "texture3DProj", "4e4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_DESKTOP },
	{ "texture3DLod", "4e3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_VERTEX, GLSL_IN_DESKTOP },
	{ "shadow1D", "4h3", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_DESKTOP },
	{ "shadow2D", "4s3", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_DESKTOP },
	{ "shadow1DProj", "4h4", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW_PROJ, BI_BOTH, GLSL_IN_DESKTOP },
	{ "shadow2DProj", "4s4", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW_PROJ, BI_BOTH, GLSL_IN_DESKTOP },

	/* Texture lookups of GLSL 1.30. */
	{ "texture", "TAf", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TD2", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TE3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TC3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TAff", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "texture", "TD2f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "texture", "TE3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "texture", "TC3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "texture", "fh3", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "fs3", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TR3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TR3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "texture", "fp4", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "fq4", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_130_UP },
	{ "texture", "TF2", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "texture", "fr3", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureProj", "TA2", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProj", "TA4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProj", "TD3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProj", "TD4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProj", "TE4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProj", "TF3", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureProj", "TF4", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureProj", "fr4", GLSL_BI_SPECIAL, GLSL_SPECIAL_SHADOW_PROJ, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureLod", "TAff", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLod", "TD2f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLod", "TE3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLod", "TC3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLod", "TR3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProjLod", "TD3f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProjLod", "TD4f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureProjLod", "TE4f", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_PROJ_LOD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureGrad", "TD222", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_GRAD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureGrad", "TE333", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_GRAD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureGrad", "TC333", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_GRAD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureGrad", "TR322", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_GRAD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureGrad", "fs322", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_GRAD, BI_BOTH, GLSL_IN_130_UP },
	{ "textureOffset", "TD2j", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureOffset", "TD2jf", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_OFFSET_BIAS, BI_FRAGMENT, GLSL_IN_130_UP },
	{ "textureOffset", "TE3k", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureOffset", "TR3j", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureOffset", "fs3j", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLodOffset", "TD2fj", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLodOffset", "TE3fk", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureLodOffset", "TR3fj", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_LOD_OFFSET, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "iAi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "jDi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "kEi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "jCi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "kRi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "jsi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "jqi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "kpi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_130_UP },
	{ "textureSize", "jF", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureSize", "jr", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureSize", "iQ", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "textureSize", "jZ", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXTURE_SIZE, BI_BOTH, GLSL_IN_150_UP },
	{ "texelFetch", "TAii", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_130_UP },
	{ "texelFetch", "TDji", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_130_UP },
	{ "texelFetch", "TEki", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_130_UP },
	{ "texelFetch", "TRki", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_130_UP },
	{ "texelFetch", "TFj", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "texelFetch", "TQi", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_DESKTOP_140_UP },
	{ "texelFetch", "TZji", GLSL_BI_SPECIAL, GLSL_SPECIAL_TEXEL_FETCH, BI_BOTH, GLSL_IN_150_UP },


	/* Derivatives (OpenGL ES needs OES_standard_derivatives). */
	{ "dFdx", "GG", GLSL_BI_OP, BI_OP_DPDX, BI_FRAGMENT, GLSL_IN_ALL | GLSL_IN_DERIVATIVES },
	{ "dFdy", "GG", GLSL_BI_OP, BI_OP_DPDY, BI_FRAGMENT, GLSL_IN_ALL | GLSL_IN_DERIVATIVES },
	{ "fwidth", "GG", GLSL_BI_OP, BI_OP_FWIDTH, BI_FRAGMENT, GLSL_IN_ALL | GLSL_IN_DERIVATIVES },

	/* Noise (desktop; the result is 0, which the specification allows). */
	{ "noise1", "fG", GLSL_BI_SPECIAL, GLSL_SPECIAL_NOISE, BI_BOTH, GLSL_IN_DESKTOP },
	{ "noise2", "2G", GLSL_BI_SPECIAL, GLSL_SPECIAL_NOISE, BI_BOTH, GLSL_IN_DESKTOP },
	{ "noise3", "3G", GLSL_BI_SPECIAL, GLSL_SPECIAL_NOISE, BI_BOTH, GLSL_IN_DESKTOP },
	{ "noise4", "4G", GLSL_BI_SPECIAL, GLSL_SPECIAL_NOISE, BI_BOTH, GLSL_IN_DESKTOP },

	/* A geometry shader's vertices and primitives (desktop GLSL 1.50). */
	{ "EmitVertex", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_EMIT_VERTEX, BI_GEOMETRY, GLSL_IN_150_UP },
	{ "EndPrimitive", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_END_PRIMITIVE, BI_GEOMETRY, GLSL_IN_150_UP },

	/*
	 * GLSL ES 3.10's atomic memory functions on a buffer or shared
	 * variable's int or uint (the checker requires such a variable as
	 * the first argument), and the compute shader's barriers (ws101-p008).
	 */
	{ "atomicAdd", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_ADD, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicAdd", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_ADD, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicMin", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_MIN, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicMin", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_MIN, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicMax", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_MAX, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicMax", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_MAX, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicAnd", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_AND, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicAnd", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_AND, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicOr", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_OR, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicOr", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_OR, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicXor", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_XOR, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicXor", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_XOR, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicExchange", "uuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_EXCHANGE, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicExchange", "iii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_EXCHANGE, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicCompSwap", "uuuu", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_COMP_SWAP, BI_COMPUTE, GLSL_IN_ES310 },
	{ "atomicCompSwap", "iiii", GLSL_BI_SPECIAL, GLSL_SPECIAL_ATOMIC_COMP_SWAP, BI_COMPUTE, GLSL_IN_ES310 },
	{ "barrier", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_BARRIER, BI_COMPUTE, GLSL_IN_ES310 },
	{ "memoryBarrier", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_MEMORY_BARRIER, BI_COMPUTE, GLSL_IN_ES310 },
	{ "memoryBarrierBuffer", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_MEMORY_BARRIER_BUFFER, BI_COMPUTE, GLSL_IN_ES310 },
	{ "memoryBarrierShared", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_MEMORY_BARRIER_SHARED, BI_COMPUTE, GLSL_IN_ES310 },
	{ "groupMemoryBarrier", "v", GLSL_BI_SPECIAL, GLSL_SPECIAL_GROUP_MEMORY_BARRIER, BI_COMPUTE, GLSL_IN_ES310 }
};

/*
 * A built-in constant: its name, value and versions.
 */
struct builtins_constant {
	const char *name;
	int32_t value;
	unsigned versions;
};

/*
 * The built-in constants every stage has (the limits of zedBSD's
 * translation layer: 16 attributes and texture units, one draw buffer).
 */
static const struct builtins_constant builtins_constants[] = {
	{ "gl_MaxVertexAttribs", 16, GLSL_IN_ALL },
	{ "gl_MaxVertexUniformVectors", 256, GLSL_IN_ES100 },
	{ "gl_MaxVaryingVectors", 15, GLSL_IN_ES100 },
	{ "gl_MaxVertexTextureImageUnits", 16, GLSL_IN_ALL },
	{ "gl_MaxCombinedTextureImageUnits", 32, GLSL_IN_ALL },
	{ "gl_MaxTextureImageUnits", 16, GLSL_IN_ALL },
	{ "gl_MaxFragmentUniformVectors", 256, GLSL_IN_ES100 },
	{ "gl_MaxDrawBuffers", 1, GLSL_IN_ALL },
	{ "gl_MaxVertexUniformComponents", 1024, GLSL_IN_DESKTOP },
	{ "gl_MaxFragmentUniformComponents", 1024, GLSL_IN_DESKTOP },
	{ "gl_MaxVaryingFloats", 60, GLSL_IN_110 | GLSL_IN_120 },
	{ "gl_MaxVaryingComponents", 60, GLSL_IN_130_UP },
	{ "gl_MaxTextureCoords", 8, GLSL_IN_DESKTOP },
	{ "gl_MaxTextureUnits", 16, GLSL_IN_DESKTOP },
	{ "gl_MaxLights", 8, GLSL_IN_DESKTOP },
	{ "gl_MaxClipPlanes", 8, GLSL_IN_DESKTOP },
	{ "gl_MaxClipDistances", 8, GLSL_IN_130_UP }
};

static int builtins_match_size(const struct glsl_builtin *builtin, const struct glsl_type **arguments, unsigned count, unsigned size, int convert, const struct glsl_type **parameters, const struct glsl_type **result);
static const struct glsl_type *builtins_code_type(char code, unsigned size, const struct glsl_type **arguments, unsigned count, unsigned index);
static int builtins_accepts(const struct glsl_type *parameter, const struct glsl_type *argument, int convert);
static int builtins_sampler_code(char code, const struct glsl_type *argument);
static void builtins_variable(struct glsl_shader *shader, const char *name, const struct glsl_type *type, unsigned where, unsigned builtin);
static void builtins_geometry(struct glsl_shader *shader);
static struct glsl_symbol *builtins_uniform(struct glsl_shader *shader, const char *name, const struct glsl_type *type);
static void builtins_depth_range(struct glsl_shader *shader);
static void builtins_compute(struct glsl_shader *shader);

/*
 * Returns the first built-in function of a name, or NULL.
 */
const struct glsl_builtin *
glsl_builtin_first(
	const char *name)
{
	size_t index;
	int differs;

	/* The first entry of the name. */
	for (index = 0U; index < sizeof(builtins_table) / sizeof(builtins_table[0]); index++) {
		differs = strcmp(builtins_table[index].name, name);
		if (differs == 0)
			return &builtins_table[index];
	}

	/* No built-in of that name. */
	return NULL;
}

/*
 * Returns the next built-in function of the same name, or NULL.
 */
const struct glsl_builtin *
glsl_builtin_next(
	const struct glsl_builtin *builtin)
{
	const struct glsl_builtin *next;
	const struct glsl_builtin *end;
	int differs;

	/* The entry after it, while it has the same name. */
	next = builtin + 1;
	end = builtins_table + sizeof(builtins_table) / sizeof(builtins_table[0]);
	if (next >= end)
		return NULL;
	differs = strcmp(next->name, builtin->name);
	if (differs != 0)
		return NULL;

	/* Succeeded: the next signature. */
	return next;
}

/*
 * Matches arguments against a built-in's signature, in the shader's
 * version and stage.  With `convert`, int and uint arguments may become
 * float parameters (desktop GLSL 1.20 on).  Returns 1 and the parameter
 * and result types on a match, 0 otherwise.
 */
int
glsl_builtin_match(
	const struct glsl_shader *shader,
	const struct glsl_builtin *builtin,
	const struct glsl_type **arguments,
	unsigned count,
	int convert,
	const struct glsl_type **parameters,
	const struct glsl_type **result)
{
	unsigned mask;
	unsigned stage;
	unsigned size;
	size_t length;
	int matched;

	/* The version and the stage. */
	mask = glsl_version_mask(shader);
	if ((builtin->versions & mask) == 0U)
		return 0;
	stage = BI_VERTEX;
	if (shader->stage == GLSL_STAGE_FRAGMENT)
		stage = BI_FRAGMENT;
	if (shader->stage == GLSL_STAGE_GEOMETRY)
		stage = BI_GEOMETRY;
	if (shader->stage == GLSL_STAGE_COMPUTE)
		stage = BI_COMPUTE;
	if ((builtin->stages & stage) == 0U)
		return 0;

	/* OpenGL ES's derivatives need their extension. */
	if ((builtin->versions & GLSL_IN_DERIVATIVES) != 0U && shader->es && shader->version < GLSL_VERSION_ES300 && !shader->derivatives)
		return 0;

	/* The number of arguments. */
	length = strlen(builtin->signature);
	if (length != count + 1U)
		return 0;

	/* Each size of the generic types. */
	for (size = 1U; size <= 4U; size++) {
		matched = builtins_match_size(builtin, arguments, count, size, convert, parameters, result);
		if (matched)
			return 1;
	}

	/* No size fits. */
	return 0;
}

/*
 * Declares the built-in variables and constants of the shader's stage
 * and version in the current (global) scope.
 */
void
glsl_builtin_variables(
	struct glsl_shader *shader)
{
	struct glsl_symbol *symbol;
	struct glsl_constant *value;
	const struct glsl_type *type;
	unsigned mask;
	size_t index;

	/* The constants. */
	mask = glsl_version_mask(shader);
	for (index = 0U; index < sizeof(builtins_constants) / sizeof(builtins_constants[0]); index++) {
		if ((builtins_constants[index].versions & mask) == 0U)
			continue;

		/* A const int with its value. */
		value = glsl_constant_new(&shader->arena, glsl_type_scalar(GLSL_BASE_INT));
		value->values[0].i = builtins_constants[index].value;
		symbol = glsl_declare(shader, builtins_constants[index].name, GLSL_SYMBOL_VARIABLE, 0U);
		symbol->type = value->type;
		symbol->storage = GLSL_STORAGE_CONST;
		symbol->where = GLSL_VAR_CONST;
		symbol->constant = value;
	}

	/* The depth range, a uniform of every stage that draws (ws068-p004). */
	if (shader->stage != GLSL_STAGE_COMPUTE)
		builtins_depth_range(shader);

	/* The vertex stage: the position and point size it writes, and the vertex's index (1.30). */
	if (shader->stage == GLSL_STAGE_VERTEX) {
		builtins_variable(shader, "gl_Position", glsl_type_vector(GLSL_BASE_FLOAT, 4U), GLSL_VAR_OUTPUT, GLSL_BUILTIN_POSITION);
		builtins_variable(shader, "gl_PointSize", glsl_type_scalar(GLSL_BASE_FLOAT), GLSL_VAR_OUTPUT, GLSL_BUILTIN_POINT_SIZE);
		if ((mask & GLSL_IN_130_UP) != 0U)
			builtins_variable(shader, "gl_VertexID", glsl_type_scalar(GLSL_BASE_INT), GLSL_VAR_INPUT, GLSL_BUILTIN_VERTEX_ID);
		if ((mask & GLSL_IN_140_UP) != 0U)
			builtins_variable(shader, "gl_InstanceID", glsl_type_scalar(GLSL_BASE_INT), GLSL_VAR_INPUT, GLSL_BUILTIN_INSTANCE_ID);
		return;
	}

	/* The geometry stage: its input vertices and primitive's number, and what it emits. */
	if (shader->stage == GLSL_STAGE_GEOMETRY) {
		builtins_geometry(shader);
		return;
	}

	/* The compute stage (ws101-p008): its invocation's and group's places, and the workgroup size. */
	if (shader->stage == GLSL_STAGE_COMPUTE) {
		builtins_compute(shader);
		return;
	}

	/* The fragment stage: its window position, facing and point coordinate (and the primitive's number, desktop 1.50). */
	builtins_variable(shader, "gl_FragCoord", glsl_type_vector(GLSL_BASE_FLOAT, 4U), GLSL_VAR_INPUT, GLSL_BUILTIN_FRAG_COORD);
	if ((mask & GLSL_IN_150_UP) != 0U)
		builtins_variable(shader, "gl_PrimitiveID", glsl_type_scalar(GLSL_BASE_INT), GLSL_VAR_INPUT, GLSL_BUILTIN_PRIMITIVE_ID);
	builtins_variable(shader, "gl_FrontFacing", glsl_type_scalar(GLSL_BASE_BOOL), GLSL_VAR_INPUT, GLSL_BUILTIN_FRONT_FACING);
	if ((mask & (GLSL_IN_ES100 | GLSL_IN_120_UP)) != 0U)
		builtins_variable(shader, "gl_PointCoord", glsl_type_vector(GLSL_BASE_FLOAT, 2U), GLSL_VAR_INPUT, GLSL_BUILTIN_POINT_COORD);

	/* The hidden uniform that turns the two into GL's directions (ws068-p004). */
	shader->zed_fragment = builtins_uniform(shader, "gl_ZedFragment", glsl_type_vector(GLSL_BASE_FLOAT, 4U));

	/* Its outputs: the colour and the draw buffers (one; not in OpenGL ES 3.00), and the depth (not in OpenGL ES 1.00). */
	if ((mask & GLSL_IN_LEGACY) != 0U) {
		builtins_variable(shader, "gl_FragColor", glsl_type_vector(GLSL_BASE_FLOAT, 4U), GLSL_VAR_OUTPUT, GLSL_BUILTIN_FRAG_COLOR);
		type = glsl_type_array(&shader->arena, glsl_type_vector(GLSL_BASE_FLOAT, 4U), 1U);
		builtins_variable(shader, "gl_FragData", type, GLSL_VAR_OUTPUT, GLSL_BUILTIN_FRAG_DATA);
	}

	/* The depth, which OpenGL ES 1.00 does not have. */
	if ((mask & GLSL_IN_ES100) == 0U)
		builtins_variable(shader, "gl_FragDepth", glsl_type_scalar(GLSL_BASE_FLOAT), GLSL_VAR_OUTPUT, GLSL_BUILTIN_FRAG_DEPTH);
}

/* Matches a signature with its generic codes taking one size. */
static int
builtins_match_size(
	const struct glsl_builtin *builtin,
	const struct glsl_type **arguments,
	unsigned count,
	unsigned size,
	int convert,
	const struct glsl_type **parameters,
	const struct glsl_type **result)
{
	const struct glsl_type *parameter;
	unsigned index;
	int accepted;
	int sampler;
	char code;

	/* Each parameter's type for this size, and whether its argument fits. */
	for (index = 0U; index < count; index++) {
		code = builtin->signature[index + 1U];

		/* A sampler code matches the argument's sampler kind. */
		sampler = builtins_sampler_code(code, arguments[index]);
		if (sampler < 0)
			return 0;
		if (sampler > 0) {
			parameters[index] = arguments[index];
			continue;
		}

		/* Any other code is a type the argument must be (or convert to). */
		parameter = builtins_code_type(code, size, arguments, count, index);
		if (parameter == NULL)
			return 0;
		accepted = builtins_accepts(parameter, arguments[index], convert);
		if (!accepted)
			return 0;
		parameters[index] = parameter;
	}

	/* The result's type. */
	*result = builtins_code_type(builtin->signature[0], size, arguments, count, count);
	if (*result == NULL)
		return 0;

	/* Succeeded: every argument fits. */
	return 1;
}

/*
 * Returns the type a code stands for with the generic size, or NULL when
 * the code does not exist in that size.  The matrix codes take their
 * type from the arguments (index is the parameter being matched, or
 * count for the result).
 */
static const struct glsl_type *
builtins_code_type(
	char code,
	unsigned size,
	const struct glsl_type **arguments,
	unsigned count,
	unsigned index)
{
	const struct glsl_type *matrix;
	unsigned other;

	/* The codes that stand for fixed types. */
	switch (code) {
	case 'v':
		return glsl_type_void();
	case 'f':
		return glsl_type_scalar(GLSL_BASE_FLOAT);
	case 'i':
		return glsl_type_scalar(GLSL_BASE_INT);
	case 'u':
		return glsl_type_scalar(GLSL_BASE_UINT);
	case 'b':
		return glsl_type_scalar(GLSL_BASE_BOOL);
	case '2':
		return glsl_type_vector(GLSL_BASE_FLOAT, 2U);
	case '3':
		return glsl_type_vector(GLSL_BASE_FLOAT, 3U);
	case '4':
		return glsl_type_vector(GLSL_BASE_FLOAT, 4U);
	case 'j':
		return glsl_type_vector(GLSL_BASE_INT, 2U);
	case 'k':
		return glsl_type_vector(GLSL_BASE_INT, 3U);
	case 'G':
		return glsl_type_vector(GLSL_BASE_FLOAT, size);
	case 'I':
		return glsl_type_vector(GLSL_BASE_INT, size);
	case 'U':
		return glsl_type_vector(GLSL_BASE_UINT, size);
	case 'B':
		return glsl_type_vector(GLSL_BASE_BOOL, size);
	default:
		break;
	}

	/* The vector-only codes exist from size 2. */
	if (code == 'V' || code == 'W' || code == 'X' || code == 'Y') {
		if (size < 2U)
			return NULL;
		if (code == 'V')
			return glsl_type_vector(GLSL_BASE_FLOAT, size);
		if (code == 'W')
			return glsl_type_vector(GLSL_BASE_INT, size);
		if (code == 'X')
			return glsl_type_vector(GLSL_BASE_UINT, size);
		return glsl_type_vector(GLSL_BASE_BOOL, size);
	}

	/* A square matrix of the size. */
	if (code == 'M') {
		if (size < 2U)
			return NULL;
		return glsl_type_matrix(size, size);
	}

	/* Any matrix, taken from the first argument (size 1 only, so it is tried once). */
	if (code == 'm' || code == 't') {
		if (size != 1U || count == 0U || arguments[0]->kind != GLSL_KIND_MATRIX)
			return NULL;
		matrix = arguments[0];
		if (code == 't')
			return glsl_type_matrix(matrix->components, matrix->columns);
		return matrix;
	}

	/* outerProduct's matrix: the second vector's size of columns, the first's of rows (sizes may differ). */
	if (code == 'o') {
		if (count != 2U)
			return NULL;
		other = arguments[1]->components;
		return glsl_type_matrix(other, arguments[0]->components);
	}

	/* A sampler's texel vector: a gvec4 of the sampler argument's base. */
	if (code == 'T') {
		for (other = 0U; other < count; other++) {
			if (arguments[other]->kind == GLSL_KIND_SAMPLER)
				return glsl_type_vector(arguments[other]->base, 4U);
		}

		/* No sampler among the arguments. */
		return NULL;
	}

	/* An unknown code (index is unused but for the matrix codes). */
	(void)index;
	return NULL;
}

/* Reports whether an argument fits a parameter type, converting int and uint to float when allowed. */
static int
builtins_accepts(
	const struct glsl_type *parameter,
	const struct glsl_type *argument,
	int convert)
{
	int same;

	/* The same type. */
	same = glsl_type_equal(parameter, argument);
	if (same)
		return 1;

	/* An int or uint scalar or vector to a float one of the same size. */
	if (!convert)
		return 0;
	if (parameter->kind != GLSL_KIND_SCALAR && parameter->kind != GLSL_KIND_VECTOR)
		return 0;
	if (argument->kind != parameter->kind || argument->components != parameter->components)
		return 0;
	if (parameter->base != GLSL_BASE_FLOAT)
		return 0;
	if (argument->base != GLSL_BASE_INT && argument->base != GLSL_BASE_UINT)
		return 0;

	/* Succeeded: convertible. */
	return 1;
}

/* Returns 1 when a sampler code matches its argument, -1 when it does not, 0 when the code is not a sampler code. */
static int
builtins_sampler_code(
	char code,
	const struct glsl_type *argument)
{
	unsigned sampler;
	unsigned shadow;
	unsigned arrayed;
	int any_base;

	/* The dimension, shadow, layers and texel base each code wants. */
	any_base = 0;
	shadow = 0U;
	arrayed = 0U;
	switch (code) {
	case 'a':
		sampler = GLSL_SAMPLER_1D;
		break;
	case 'd':
		sampler = GLSL_SAMPLER_2D;
		break;
	case 'e':
		sampler = GLSL_SAMPLER_3D;
		break;
	case 'c':
		sampler = GLSL_SAMPLER_CUBE;
		break;
	case 'h':
		sampler = GLSL_SAMPLER_1D;
		shadow = 1U;
		break;
	case 's':
		sampler = GLSL_SAMPLER_2D;
		shadow = 1U;
		break;
	case 'A':
		sampler = GLSL_SAMPLER_1D;
		any_base = 1;
		break;
	case 'D':
		sampler = GLSL_SAMPLER_2D;
		any_base = 1;
		break;
	case 'E':
		sampler = GLSL_SAMPLER_3D;
		any_base = 1;
		break;
	case 'C':
		sampler = GLSL_SAMPLER_CUBE;
		any_base = 1;
		break;
	case 'R':
		sampler = GLSL_SAMPLER_2D;
		arrayed = 1U;
		any_base = 1;
		break;
	case 'p':
		sampler = GLSL_SAMPLER_2D;
		arrayed = 1U;
		shadow = 1U;
		break;
	case 'q':
		sampler = GLSL_SAMPLER_CUBE;
		shadow = 1U;
		break;
	case 'F':
		sampler = GLSL_SAMPLER_RECT;
		any_base = 1;
		break;
	case 'r':
		sampler = GLSL_SAMPLER_RECT;
		shadow = 1U;
		break;
	case 'Q':
		sampler = GLSL_SAMPLER_BUFFER;
		any_base = 1;
		break;
	case 'Z':
		sampler = GLSL_SAMPLER_MS;
		any_base = 1;
		break;
	default:
		return 0;
	}

	/* The argument must be that sampler. */
	if (argument->kind != GLSL_KIND_SAMPLER || argument->sampler != sampler || argument->shadow != shadow)
		return -1;
	if (argument->arrayed != arrayed)
		return -1;
	if (!any_base && argument->base != GLSL_BASE_FLOAT)
		return -1;

	/* Succeeded: it matches. */
	return 1;
}

/* Declares one built-in variable, and puts it among the shader's globals. */
static void
builtins_variable(
	struct glsl_shader *shader,
	const char *name,
	const struct glsl_type *type,
	unsigned where,
	unsigned builtin)
{
	struct glsl_symbol *symbol;

	/* The symbol. */
	symbol = glsl_declare(shader, name, GLSL_SYMBOL_VARIABLE, 0U);
	symbol->type = type;
	symbol->where = where;
	symbol->builtin = builtin;
	symbol->precision = GLSL_PRECISION_HIGH;

	/* Among the globals, so the emitter finds it when it is used. */
	glsl_add_global(shader, symbol);
}

/*
 * Declares a built-in uniform (ws068-p004): a member of the default block
 * that libGLESv2 fills at each draw, never the application.
 */
static struct glsl_symbol *
builtins_uniform(
	struct glsl_shader *shader,
	const char *name,
	const struct glsl_type *type)
{
	struct glsl_symbol *symbol;

	/* The symbol. */
	symbol = glsl_declare(shader, name, GLSL_SYMBOL_VARIABLE, 0U);
	symbol->type = type;
	symbol->storage = GLSL_STORAGE_UNIFORM;
	symbol->where = GLSL_VAR_UNIFORM;
	symbol->precision = GLSL_PRECISION_HIGH;

	/* Among the globals, so the link finds it when it is used. */
	glsl_add_global(shader, symbol);
	return symbol;
}

/*
 * Declares gl_DepthRange (ws068-p004), the uniform of the struct
 * gl_DepthRangeParameters: the near and far values glDepthRangef set, and
 * far minus near.
 */
static void
builtins_depth_range(
	struct glsl_shader *shader)
{
	static const char *names[3] = { "near", "far", "diff" };
	struct glsl_type *parameters;
	unsigned index;

	/* The struct of three highp floats. */
	parameters = glsl_alloc(&shader->arena, sizeof(*parameters));
	parameters->kind = GLSL_KIND_STRUCT;
	parameters->name = "gl_DepthRangeParameters";
	parameters->fields = glsl_alloc(&shader->arena, 3U * sizeof(*parameters->fields));
	for (index = 0U; index < 3U; index++) {
		parameters->fields[index].name = names[index];
		parameters->fields[index].type = glsl_type_scalar(GLSL_BASE_FLOAT);
	}

	/* The three members. */
	parameters->field_count = 3U;

	/* The uniform. */
	(void)builtins_uniform(shader, "gl_DepthRange", parameters);
}

/*
 * Declares a geometry shader's built-ins: gl_in, an array of the input
 * vertices' gl_PerVertex block (gl_Position; unsized until the input
 * layout says how many; gl_PointSize is left out, as Vulkan reads it only
 * with a feature of its own), gl_PrimitiveIDIn, and the outputs
 * gl_Position, gl_PointSize, gl_PrimitiveID and gl_Layer.
 */
static void
builtins_geometry(
	struct glsl_shader *shader)
{
	struct glsl_type *per_vertex;
	const struct glsl_type *vec4_type;
	const struct glsl_type *float_type;
	const struct glsl_type *int_type;

	/* The block's struct: gl_Position. */
	vec4_type = glsl_type_vector(GLSL_BASE_FLOAT, 4U);
	float_type = glsl_type_scalar(GLSL_BASE_FLOAT);
	int_type = glsl_type_scalar(GLSL_BASE_INT);
	per_vertex = glsl_alloc(&shader->arena, sizeof(*per_vertex));
	per_vertex->kind = GLSL_KIND_STRUCT;
	per_vertex->name = "gl_PerVertex";
	per_vertex->block = GLSL_STORAGE_IN;
	per_vertex->fields = glsl_alloc(&shader->arena, sizeof(*per_vertex->fields));
	per_vertex->fields[0].name = "gl_Position";
	per_vertex->fields[0].type = vec4_type;
	per_vertex->field_count = 1U;

	/* The inputs. */
	builtins_variable(shader, "gl_in", glsl_type_array(&shader->arena, per_vertex, 0U), GLSL_VAR_INPUT, GLSL_BUILTIN_PER_VERTEX);
	builtins_variable(shader, "gl_PrimitiveIDIn", int_type, GLSL_VAR_INPUT, GLSL_BUILTIN_PRIMITIVE_ID_IN);

	/* The outputs of each vertex emitted. */
	builtins_variable(shader, "gl_Position", vec4_type, GLSL_VAR_OUTPUT, GLSL_BUILTIN_POSITION);
	builtins_variable(shader, "gl_PointSize", float_type, GLSL_VAR_OUTPUT, GLSL_BUILTIN_POINT_SIZE);
	builtins_variable(shader, "gl_PrimitiveID", int_type, GLSL_VAR_OUTPUT, GLSL_BUILTIN_PRIMITIVE_ID);
	builtins_variable(shader, "gl_Layer", int_type, GLSL_VAR_OUTPUT, GLSL_BUILTIN_LAYER);
}

/*
 * Declares a compute shader's built-ins (ws101-p008): the uvec3 inputs
 * gl_GlobalInvocationID, gl_LocalInvocationID, gl_WorkGroupID and
 * gl_NumWorkGroups, the uint gl_LocalInvocationIndex, and the const uvec3
 * gl_WorkGroupSize, whose value the local_size layout gives (1, 1, 1 until
 * it does).
 */
static void
builtins_compute(
	struct glsl_shader *shader)
{
	const struct glsl_type *uvec3;
	struct glsl_symbol *symbol;
	struct glsl_constant *value;
	unsigned axis;

	/* The inputs. */
	uvec3 = glsl_type_vector(GLSL_BASE_UINT, 3U);
	builtins_variable(shader, "gl_GlobalInvocationID", uvec3, GLSL_VAR_INPUT, GLSL_BUILTIN_GLOBAL_INVOCATION_ID);
	builtins_variable(shader, "gl_LocalInvocationID", uvec3, GLSL_VAR_INPUT, GLSL_BUILTIN_LOCAL_INVOCATION_ID);
	builtins_variable(shader, "gl_WorkGroupID", uvec3, GLSL_VAR_INPUT, GLSL_BUILTIN_WORK_GROUP_ID);
	builtins_variable(shader, "gl_NumWorkGroups", uvec3, GLSL_VAR_INPUT, GLSL_BUILTIN_NUM_WORK_GROUPS);
	builtins_variable(shader, "gl_LocalInvocationIndex", glsl_type_scalar(GLSL_BASE_UINT), GLSL_VAR_INPUT, GLSL_BUILTIN_LOCAL_INVOCATION_INDEX);

	/* The workgroup size, a constant the layout fills. */
	value = glsl_constant_new(&shader->arena, uvec3);
	for (axis = 0U; axis < 3U; axis++)
		value->values[axis].u = 1U;
	symbol = glsl_declare(shader, "gl_WorkGroupSize", GLSL_SYMBOL_VARIABLE, 0U);
	symbol->type = uvec3;
	symbol->storage = GLSL_STORAGE_CONST;
	symbol->where = GLSL_VAR_CONST;
	symbol->constant = value;
}
