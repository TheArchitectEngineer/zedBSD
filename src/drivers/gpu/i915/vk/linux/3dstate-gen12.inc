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

#endif /* I915_VK_3DSTATE_GEN12_INC */
