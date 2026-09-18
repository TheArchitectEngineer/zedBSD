/*
 * zedBSD — transcribed hardware definitions (values only)
 *
 * Source: Mesa src/intel/genxml/gen120.xml and its imports (core Mesa, MIT;
 * values are Intel PRM hardware facts). SHA-256 in the audit.
 *
 * The 3D pipeline command header dword is
 *   (Command Type 3 << 29) | (Pipeline 3 << 27) | (Opcode << 24) |
 *   (Sub Opcode << 16) | (DWord Length = total dwords - 2)
 * The high sixteen bits of that dword are the opcode constants below, taken
 * from each instruction's Command Type / Pipeline / 3D Command Opcode /
 * 3D Command Sub Opcode default fields.  The assembly logic is new.
 */

#ifndef I915_VK_3DSTATE_GEN12_INC
#define I915_VK_3DSTATE_GEN12_INC

/* Command opcodes (high sixteen bits of the header dword). */
#define GEN12_CMD_PIPELINE_SELECT		0x6104U
#define GEN12_CMD_STATE_BASE_ADDRESS		0x6101U
#define GEN12_CMD_3DSTATE_VS			0x7810U
#define GEN12_CMD_3DSTATE_PS			0x7820U
#define GEN12_CMD_3DSTATE_PS_EXTRA		0x784FU
#define GEN12_CMD_3DSTATE_VERTEX_ELEMENTS	0x7809U
#define GEN12_CMD_3DPRIMITIVE			0x7B00U

/* Builds a command header from an opcode and the packet's dword count. */
#define GEN12_CMD_HEADER(opcode, dwords)	(((opcode) << 16) | ((dwords) - 2U))

/* Packet lengths (gen120.xml instruction "length"). */
#define GEN12_3DSTATE_VS_DWORDS			9U
#define GEN12_3DSTATE_PS_DWORDS			12U
#define GEN12_3DSTATE_VERTEX_ELEMENTS_DWORDS	3U
#define GEN12_3DPRIMITIVE_DWORDS		7U
#define GEN12_PIPELINE_SELECT_DWORDS		1U
#define GEN12_STATE_BASE_ADDRESS_DWORDS		22U

/*
 * PIPELINE_SELECT: the 3D pipeline is selection value 0.  Gen12 applies the
 * selection only when its mask bits (15:8) cover it, and Mesa also masks in
 * and sets the media sampler DOP clock gate (gen12 mask 0x13, bit 4).
 */
#define GEN12_PIPELINE_SELECT_3D		0U
#define GEN12_PIPELINE_SELECT_MASK		(0x13U << 8)
#define GEN12_PIPELINE_SELECT_MEDIA_DOP_GATE	(1U << 4)
#define GEN12_PIPELINE_SELECT_DWORD(pipeline)	\
	((GEN12_CMD_PIPELINE_SELECT << 16) | GEN12_PIPELINE_SELECT_MASK | \
	 GEN12_PIPELINE_SELECT_MEDIA_DOP_GATE | (pipeline))


/*
 * The rest of the 3D pipeline, transcribed from gen120.xml resolved through its
 * <import> chain of gen110.xml.  Each opcode is the high sixteen bits of the
 * packet header and each DWORDS is the packet's total dword count, so
 * GEN12_CMD_HEADER() produces the header with the right DWord Length.
 */
#define GEN12_CMD_3DSTATE_VF_STATISTICS		0x680BU
#define GEN12_CMD_3DSTATE_VF			0x780CU
#define GEN12_CMD_3DSTATE_VF_TOPOLOGY		0x784BU
#define GEN12_CMD_3DSTATE_VF_SGVS		0x784AU
#define GEN12_CMD_3DSTATE_VF_SGVS_2		0x7856U
#define GEN12_CMD_3DSTATE_VERTEX_BUFFERS	0x7808U
#define GEN12_CMD_3DSTATE_URB_ALLOC_VS		0x7858U
#define GEN12_CMD_3DSTATE_URB_ALLOC_HS		0x7859U
#define GEN12_CMD_3DSTATE_URB_ALLOC_DS		0x785AU
#define GEN12_CMD_3DSTATE_URB_ALLOC_GS		0x785BU
#define GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_VS	0x7912U
#define GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_HS	0x7913U
#define GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_DS	0x7914U
#define GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_GS	0x7915U
#define GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS	0x7916U
#define GEN12_CMD_3DSTATE_HS			0x781BU
#define GEN12_CMD_3DSTATE_TE			0x781CU
#define GEN12_CMD_3DSTATE_DS			0x781DU
#define GEN12_CMD_3DSTATE_STREAMOUT		0x781EU
#define GEN12_CMD_3DSTATE_GS			0x7811U
#define GEN12_CMD_3DSTATE_CLIP			0x7812U
#define GEN12_CMD_3DSTATE_SF			0x7813U
#define GEN12_CMD_3DSTATE_RASTER		0x7850U
#define GEN12_CMD_3DSTATE_SBE			0x781FU
#define GEN12_CMD_3DSTATE_WM			0x7814U
#define GEN12_CMD_3DSTATE_WM_DEPTH_STENCIL	0x784EU
#define GEN12_CMD_3DSTATE_PS_BLEND		0x784DU
#define GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_PS	0x782AU
#define GEN12_CMD_3DSTATE_BLEND_STATE_POINTERS	0x7824U
#define GEN12_CMD_3DSTATE_CC_STATE_POINTERS	0x780EU
#define GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_CC	0x7823U
#define GEN12_CMD_3DSTATE_DRAWING_RECTANGLE	0x7900U
#define GEN12_CMD_3DSTATE_MULTISAMPLE		0x780DU
#define GEN12_CMD_3DSTATE_SAMPLE_MASK		0x7818U
#define GEN12_CMD_3DSTATE_DEPTH_BUFFER		0x7805U
#define GEN12_CMD_3DSTATE_STENCIL_BUFFER	0x7806U
#define GEN12_CMD_3DSTATE_HIER_DEPTH_BUFFER	0x7807U
#define GEN12_CMD_3DSTATE_CLEAR_PARAMS		0x7804U
#define GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION	0x786CU

#define GEN12_3DSTATE_VF_DWORDS			2U
#define GEN12_3DSTATE_VF_TOPOLOGY_DWORDS	2U
#define GEN12_3DSTATE_VF_SGVS_DWORDS		2U
#define GEN12_3DSTATE_VF_SGVS_2_DWORDS		3U
#define GEN12_3DSTATE_URB_ALLOC_DWORDS		3U
#define GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS	2U
#define GEN12_3DSTATE_HS_DWORDS			9U
#define GEN12_3DSTATE_TE_DWORDS			5U
#define GEN12_3DSTATE_DS_DWORDS			11U
#define GEN12_3DSTATE_STREAMOUT_DWORDS		5U
#define GEN12_3DSTATE_GS_DWORDS			10U
#define GEN12_3DSTATE_CLIP_DWORDS		4U
#define GEN12_3DSTATE_SF_DWORDS			4U
#define GEN12_3DSTATE_RASTER_DWORDS		5U
#define GEN12_3DSTATE_SBE_DWORDS		6U
#define GEN12_3DSTATE_WM_DWORDS			2U
#define GEN12_3DSTATE_WM_DEPTH_STENCIL_DWORDS	4U
#define GEN12_3DSTATE_PS_EXTRA_DWORDS		2U
#define GEN12_3DSTATE_PS_BLEND_DWORDS		2U
#define GEN12_3DSTATE_POINTERS_DWORDS		2U
#define GEN12_3DSTATE_DRAWING_RECTANGLE_DWORDS	4U
#define GEN12_3DSTATE_MULTISAMPLE_DWORDS	2U
#define GEN12_3DSTATE_SAMPLE_MASK_DWORDS	2U
#define GEN12_3DSTATE_DEPTH_BUFFER_DWORDS	8U
#define GEN12_3DSTATE_STENCIL_BUFFER_DWORDS	8U
#define GEN12_3DSTATE_HIER_DEPTH_BUFFER_DWORDS	5U
#define GEN12_3DSTATE_CLEAR_PARAMS_DWORDS	3U
#define GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS	6U

/* Structure sizes in dwords (gen120.xml struct lengths). */
#define GEN12_RENDER_SURFACE_STATE_DWORDS	16U
#define GEN12_VERTEX_BUFFER_STATE_DWORDS	4U
#define GEN12_VERTEX_ELEMENT_STATE_DWORDS	2U
#define GEN12_COLOR_CALC_STATE_DWORDS		6U
#define GEN12_BLEND_STATE_DWORDS		1U
#define GEN12_BLEND_STATE_ENTRY_DWORDS		2U
#define GEN12_CC_VIEWPORT_DWORDS		2U

/* 3D_Prim_Topo_Type: a screen-space rectangle needs no vertex shader. */
#define GEN12_3DPRIM_RECTLIST			15U
#define GEN12_3DPRIM_TRILIST			4U

/* 3D_Vertex_Component_Control. */
#define GEN12_VFCOMP_NOSTORE			0U
#define GEN12_VFCOMP_STORE_SRC			1U
#define GEN12_VFCOMP_STORE_0			2U
#define GEN12_VFCOMP_STORE_1_FP			3U

/* Surface formats (isl_format numbering). */
#define GEN12_FORMAT_R32G32B32A32_FLOAT		0U
#define GEN12_FORMAT_R32G32B32_FLOAT		64U
#define GEN12_FORMAT_B8G8R8A8_UNORM		192U

/* RENDER_SURFACE_STATE surface types and alignments. */
#define GEN12_SURFTYPE_2D			1U
#define GEN12_SURFTYPE_NULL			7U
#define GEN12_SURFACE_ALIGN_4			1U
#define GEN12_TILEMODE_LINEAR			0U

/* BLEND_STATE_ENTRY colour clamp range. */
#define GEN12_COLORCLAMP_RTFORMAT		2U

/* URB allocations are made in 8 KiB chunks and entries in 64-byte units. */
#define GEN12_URB_CHUNK_KB			8U

/* 3DSTATE_SF deref block size (intel_l3_config.h). */
#define GEN12_URB_DEREF_BLOCK_SIZE_32		0U
#define GEN12_URB_DEREF_BLOCK_SIZE_PER_POLY	1U

/* The pixel shader dispatch field is programmed as the thread count less one. */
#define GEN12_MAX_THREADS_PER_PSD		64U

/* IEEE-754 bit patterns; the kernel builds vertex data without floating point. */
#define GEN12_F32_0				0x00000000U
#define GEN12_F32_1				0x3f800000U

/* Pipeline statistics counters (gen70.xml register numbers, 64-bit each). */
#define GEN12_REG_IA_VERTICES_COUNT		0x2310U
#define GEN12_REG_IA_PRIMITIVES_COUNT		0x2318U
#define GEN12_REG_VS_INVOCATION_COUNT		0x2320U
#define GEN12_REG_CL_INVOCATION_COUNT		0x2338U
#define GEN12_REG_CL_PRIMITIVES_COUNT		0x2340U
#define GEN12_REG_PS_INVOCATION_COUNT		0x2348U

/*
 * The L3 cache is partitioned before the URB can hold a single vertex entry.
 * These are the Tiger Lake values Mesa validates: 32 ways of URB and 88 ways
 * shared, written into L3ALLOC (URB in bits 7:1, All in bits 31:25).
 */
#define GEN12_REG_L3ALLOC			0xB134U
#define GEN12_L3ALLOC_URB_WAYS			32U
#define GEN12_L3ALLOC_ALL_WAYS			88U
#define GEN12_L3ALLOC_VALUE \
	((GEN12_L3ALLOC_URB_WAYS << 1) | (GEN12_L3ALLOC_ALL_WAYS << 25))

#define GEN12_CMD_3DSTATE_VF_INSTANCING		0x7849U
#define GEN12_3DSTATE_VF_INSTANCING_DWORDS	3U

/*
 * A disabled depth buffer is still a typed one: Mesa's isl always pairs
 * SURFTYPE_NULL with D32_FLOAT, because format zero names a depth-stencil
 * format and makes the depth unit expect a stencil buffer that is not there.
 */
#define GEN12_DEPTH_FORMAT_D32_FLOAT		1U

/* 3DSTATE_SBE attribute component format: all four channels active. */
#define GEN12_ACF_XYZW				3U

/* Fragments that reached the depth test, whether or not a shader ran. */
#define GEN12_REG_PS_DEPTH_COUNT		0x2350U

/*
 * Every MOCS field carries the cache-table index shifted left by one; bit zero
 * of the field is reserved.  Mesa's isl programs Tiger Lake the same way
 * (internal = 2 << 1, uncached = 3 << 1), and the table in i915-mocs.inc gives
 * index 2 as write-back and index 3 as uncached.  Writing a raw index selects
 * the entry one bit to the right, and entries 0 and 1 are reserved.
 */
#define GEN12_MOCS(index)			((index) << 1)

/*
 * 3DSTATE_RASTER cull modes.  Zero means cull both faces, so a rasterizer left
 * at its zeroed default discards every primitive; both BLORP and anv name
 * CULLMODE_NONE explicitly for that reason.
 */
#define GEN12_CULLMODE_BOTH			0U
#define GEN12_CULLMODE_NONE			1U
#define GEN12_CULLMODE_FRONT			2U
#define GEN12_CULLMODE_BACK			3U

/*
 * State Mesa's anv programs once per render context before any draw: the
 * depth-clear override packet cleared, sample positions, depth bounds and
 * the other stages' binding table pointers.  A context that skips them runs
 * with whatever the hardware or a previous owner left behind.
 */
#define GEN12_CMD_3DSTATE_WM_HZ_OP		0x7852U
#define GEN12_CMD_3DSTATE_AA_LINE_PARAMETERS	0x790AU
#define GEN12_CMD_3DSTATE_WM_CHROMAKEY		0x784CU
#define GEN12_CMD_3DSTATE_SAMPLE_PATTERN	0x791CU
#define GEN12_CMD_3DSTATE_DEPTH_BOUNDS		0x7871U
#define GEN12_CMD_3DSTATE_POLY_STIPPLE_OFFSET	0x7906U
#define GEN12_CMD_3DSTATE_LINE_STIPPLE		0x7908U
#define GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_VS	0x7826U
#define GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_HS	0x7827U
#define GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_DS	0x7828U
#define GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_GS	0x7829U

#define GEN12_3DSTATE_WM_HZ_OP_DWORDS		5U
#define GEN12_3DSTATE_AA_LINE_PARAMETERS_DWORDS	3U
#define GEN12_3DSTATE_WM_CHROMAKEY_DWORDS	2U
#define GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS	9U
#define GEN12_3DSTATE_DEPTH_BOUNDS_DWORDS	4U
#define GEN12_3DSTATE_POLY_STIPPLE_OFFSET_DWORDS	2U
#define GEN12_3DSTATE_LINE_STIPPLE_DWORDS	3U

/* 3DSTATE_SAMPLE_PATTERN dword 8: the single sample sits at the pixel centre (u0.4 0.5). */
#define GEN12_SAMPLE_PATTERN_1X_CENTRE		((8U << 20) | (8U << 16))

/*
 * Instruction fetches fill the per-subslice instruction cache through the L3,
 * so the instruction base needs a write-back entry; Mesa's isl names index 2
 * ("internal") for it on Tiger Lake and never an uncached one.
 */
#define I915_MOCS_WRITEBACK_INDEX		2U

/*
 * Push constants.  Every Mesa path programs a 3DSTATE_CONSTANT_* for each
 * stage before its first draw even with nothing to push, and gives the pixel
 * stage the whole 32 KiB push constant region when it is the only user.
 */
#define GEN12_CMD_3DSTATE_CONSTANT_VS		0x7815U
#define GEN12_CMD_3DSTATE_CONSTANT_GS		0x7816U
#define GEN12_CMD_3DSTATE_CONSTANT_PS		0x7817U
#define GEN12_CMD_3DSTATE_CONSTANT_HS		0x7819U
#define GEN12_CMD_3DSTATE_CONSTANT_DS		0x781AU
#define GEN12_3DSTATE_CONSTANT_DWORDS		11U
#define GEN12_PUSH_CONSTANT_KB			32U

/* VERTEX_BUFFER_STATE: vertex fetches stay in the L3 on this generation. */
#define GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE	(1U << 25)

/*
 * Coarse pixel shading state is fetched through a pointer relative to the
 * dynamic state base; anv points it at a disabled CPS_STATE at device
 * initialisation.  The binding table pool is disabled the same way, since
 * anv found that state leaking in from other contexts.
 */
#define GEN12_CMD_3DSTATE_CPS_POINTERS		0x7822U
#define GEN12_CPS_STATE_DWORDS			8U

#define GEN12_CMD_3DSTATE_BINDING_TABLE_POOL_ALLOC	0x7919U
#define GEN12_3DSTATE_BINDING_TABLE_POOL_ALLOC_DWORDS	4U

/* PIPE_CONTROL dw1 bit 1: stall the command streamer until the pixel scoreboard is idle. */
#define PIPE_CONTROL_STALL_AT_SCOREBOARD	(1U << 1)
#define PIPE_CONTROL_DEPTH_STALL_ENABLE		(1U << 13)

#endif /* I915_VK_3DSTATE_GEN12_INC */
