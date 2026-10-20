/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What a render takes of the PowerVR firmware beyond its targets: the
 * record of a render context (its geometry and fragment contexts and
 * the state the firmware switches it with), the kernel commands that
 * kick a context with the cleanup counts of the render target it uses,
 * the combined geometry and partial render kick, the fields of the
 * geometry command the partial render test reads, and the reading of a
 * context reset notice into a stage and its reason.  This part only
 * computes; it holds no lock and no memory, and it builds on the host
 * too.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_KICK_LAYOUT_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_KICK_LAYOUT_H

#include <stdint.h>

#include "drivers/gpu/powervr/stage.h"

/* The words of one kernel or firmware command. */
#define POWERVR_KICK_COMMAND_WORDS	22U

/*
 * A render context as the firmware sees it: the geometry context's common
 * part, the fragment context's, then the static state of two geometry
 * cores' context switch registers, each 184 bytes.  Within one core's
 * registers: the VDM context state base and resume addresses, the TA
 * context state base, then for each of two buffers the VDM store tasks 0
 * to 2, resume tasks 0 to 2, store tasks 3 and 4 and resume tasks 3 and 4,
 * 8 bytes each.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif.h
 * (struct rogue_fwif_fwrendercontext: geom_context, frag_context,
 * static_render_context_state, work_est_ccb_submitted,
 * fw_render_ctx_flags, aligned to 8); pvr_rogue_fwif_check.h
 * (struct rogue_fwif_fwcommoncontext, 152 bytes);
 * pvr_rogue_fwif_shared_check.h (struct
 * rogue_fwif_geom_registers_caswitch, 184 bytes, its fields at 0, 8, 16
 * and geom_state[0] from 24, geom_state[1] from 104; struct
 * rogue_fwif_static_rendercontext_state, 368 bytes); pvr_rogue_fwif_shared.h
 * (ROGUE_NUM_GEOM_CORES_SIZE 2).
 */
#define POWERVR_RENDER_CONTEXT_GEOMETRY		0U
#define POWERVR_RENDER_CONTEXT_FRAGMENT		152U
#define POWERVR_RENDER_CONTEXT_STATIC		304U
#define POWERVR_RENDER_CONTEXT_BYTES		680U
#define POWERVR_SWITCH_CORE_BYTES		184U
#define POWERVR_SWITCH_CORES			2U
#define POWERVR_SWITCH_VDM_STATE		0U
#define POWERVR_SWITCH_VDM_RESUME		8U
#define POWERVR_SWITCH_TA_STATE			16U
#define POWERVR_SWITCH_BUFFER			24U
#define POWERVR_SWITCH_BUFFER_BYTES		80U
#define POWERVR_SWITCH_STORE_0			0U
#define POWERVR_SWITCH_STORE_1			8U
#define POWERVR_SWITCH_STORE_2			16U
#define POWERVR_SWITCH_RESUME_0			24U
#define POWERVR_SWITCH_RESUME_1			32U
#define POWERVR_SWITCH_RESUME_2			40U
#define POWERVR_SWITCH_STORE_3			48U
#define POWERVR_SWITCH_STORE_4			56U
#define POWERVR_SWITCH_RESUME_3			64U
#define POWERVR_SWITCH_RESUME_4			72U

/*
 * The geometry and fragment contexts' own state the firmware writes on a
 * switch: the geometry's for up to four cores of 40 bytes each, the VDM
 * call stack's initial pointer at 8 in each; the fragment's three words
 * and its ISP store words.  The fragment state is given room for 61 store
 * words, more than any core needs.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_check.h
 * (struct rogue_fwif_geom_ctx_state_per_geom, 40 bytes,
 * geom_reg_vdm_call_stack_pointer_init 8; struct rogue_fwif_geom_ctx_state,
 * 160 bytes; struct rogue_fwif_frag_ctx_state, frag_reg_isp_store 12);
 * pvr_queue.c (reg_state_init: the call stack's address in the first
 * core's initial pointer; get_frag_ctx_state_size).
 */
#define POWERVR_GEOMETRY_STATE_BYTES		160U
#define POWERVR_GEOMETRY_STATE_CALL_STACK	8U
#define POWERVR_FRAGMENT_STATE_BYTES		256U

/*
 * The data masters a context reset names.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_common.h
 * (PVR_FWIF_DM_GP 0, PVR_FWIF_DM_TDM 1, PVR_FWIF_DM_GEOM 2,
 * PVR_FWIF_DM_FRAG 3, PVR_FWIF_DM_CDM 4).
 */
#define POWERVR_DATA_MASTER_GENERAL		0U
#define POWERVR_DATA_MASTER_TRANSFER		1U
#define POWERVR_DATA_MASTER_GEOMETRY		2U
#define POWERVR_DATA_MASTER_FRAGMENT		3U

/*
 * The kick and the combined geometry and fragment kick: each names a
 * context, the new write offset and the wrap mask of its command buffer,
 * and the cleanup counts of the objects its commands use (at most three:
 * two partial render buffers and the render target data set).
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif.h
 * (ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK 117 with the magic,
 * ROGUE_FWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS
 * ROGUE_FWIF_PRBUFFER_MAXSUPPORTED + 1); pvr_rogue_fwif_check.h
 * (struct rogue_fwif_kccb_cmd_kick_data, 32 bytes: context_fw_addr 0,
 * client_woff_update 4, client_wrap_mask_update 8, num_cleanup_ctl 12,
 * cleanup_ctl_fw_addr 16, work_est_cmd_header_offset 28; struct
 * rogue_fwif_kccb_cmd_combined_geom_frag_kick_data: geom 0, frag 32,
 * 64 bytes; the command's data at 8).
 */
#define POWERVR_COMMAND_COMBINED_KICK		(0x2abc0000U | 117U)
#define POWERVR_KICK_DATA_BYTES			32U
#define POWERVR_KICK_CLEANUP_ADDRESSES		16U
#define POWERVR_KICK_CLEANUPS_MAX		3U
#define POWERVR_KICK_WORK_ESTIMATE		28U

/*
 * The commands of a render in a context's command buffer, and the fields
 * of the geometry command the firmware tests for a partial render.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif.h
 * (ROGUE_FWIF_CCB_CMD_TYPE_GEOM 201, ..._FRAG 203, ..._FRAG_PR 204, each
 * with ROGUE_CCB_TYPE_TASK and the magic; ..._FENCE_PR 215 with the
 * magic; ROGUE_FWIF_CCB_CMD_MAX_UFOS 32); pvr_rogue_fwif_client_check.h
 * (struct rogue_fwif_cmd_geom, 112 bytes: cmd_shared 0, regs 16, flags
 * 80, partial_render_geom_frag_fence 84; struct rogue_fwif_cmd_frag,
 * 480 bytes: cmd_shared 0, regs 16, flags 464; struct
 * rogue_fwif_frag_regs: pds_pr_bgnd at 400 of the registers, three 64-bit
 * words); pvr_rogue_fwif_shared_check.h (struct
 * rogue_fwif_cmd_geom_frag_shared: hwrt_data_fw_addr 4).
 */
#define POWERVR_CLIENT_GEOMETRY			(0x2abc0000U | 0x8000U | 201U)
#define POWERVR_CLIENT_FRAGMENT			(0x2abc0000U | 0x8000U | 203U)
#define POWERVR_CLIENT_PARTIAL_RENDER		(0x2abc0000U | 0x8000U | 204U)
#define POWERVR_CLIENT_FENCE_PARTIAL		(0x2abc0000U | 215U)
#define POWERVR_CLIENT_FENCES_MAX		32U
#define POWERVR_GEOMETRY_BYTES			112U
#define POWERVR_FRAGMENT_BYTES			480U
#define POWERVR_SHARED_TARGET_DATA		4U
#define POWERVR_GEOMETRY_PARTIAL_FENCE		84U
#define POWERVR_FRAGMENT_REGISTERS		16U
#define POWERVR_FRAGMENT_PARTIAL_BACKGROUND	(16U + 400U)

/*
 * A context reset notice after the command's type and flags: the host's
 * identity of the context, the firmware's reason, the data master, the
 * job, the flags (a page fault, every context), the catalogue and the
 * faulting address.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_check.h
 * (struct rogue_fwif_fwccb_cmd_context_reset_data, 40 bytes:
 * server_common_context_id 0, reset_reason 4, dm 8, reset_job_ref 12,
 * flags 16, pc_address 24, fault_address 32); pvr_rogue_fwif.h
 * (ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF bit 0, ..._ALL_CTXS bit 1).
 */
#define POWERVR_RESET_CONTEXT			0U
#define POWERVR_RESET_REASON			4U
#define POWERVR_RESET_DATA_MASTER		8U
#define POWERVR_RESET_FLAGS			16U
#define POWERVR_RESET_FAULT_ADDRESS		32U
#define POWERVR_RESET_BYTES			40U
#define POWERVR_RESET_FLAG_FAULT		0x1U
#define POWERVR_RESET_FLAG_ALL			0x2U

/* One firmware address and the value a fence waits for or an update writes. */
struct powervr_sync {
	uint32_t address;
	uint32_t value;
};

/*
 * The task words of one buffer of a render context's switch: the store
 * tasks 0 to 2 and the resume tasks 0 to 2, as register values.
 */
struct powervr_switch_tasks {
	uint64_t store[3];
	uint64_t resume[3];
};

/*
 * What one kick names: the context, its command buffer's new write offset
 * and wrap mask, and the firmware addresses of the cleanup counts its
 * commands use.
 */
struct powervr_kick {
	uint32_t context;
	uint32_t write_offset;
	uint32_t wrap_mask;
	uint32_t cleanups[POWERVR_KICK_CLEANUPS_MAX];
	unsigned cleanup_count;
};

/* The kinds of firmware context a session has; each has an identity of its own. */
enum powervr_context_kind {
	POWERVR_CONTEXT_COMPUTE,
	POWERVR_CONTEXT_RENDER,
	POWERVR_CONTEXT_TRANSFER
};

/* What a context reset notice says, read from its frame. */
struct powervr_reset_notice {
	uint32_t context;
	uint32_t reason;
	uint32_t data_master;
	uint32_t flags;
	uint64_t fault_address;
};

/* What a stage line says of a reset: the stage, the reason and its value. */
struct powervr_reset_stage {
	enum powervr_stage stage;
	enum powervr_stage_reason reason;
	uint64_t value;
};

/* Builds the kick command of one context; EINVAL for more cleanup counts than a kick carries. */
int powervr_kick_command(const struct powervr_kick *kick, uint32_t command[POWERVR_KICK_COMMAND_WORDS]);

/* Builds the combined kick of a geometry context and its fragment (partial render) context. */
int powervr_kick_combined_command(const struct powervr_kick *geometry, const struct powervr_kick *fragment, uint32_t command[POWERVR_KICK_COMMAND_WORDS]);

/*
 * Puts the fields of a geometry command (of POWERVR_GEOMETRY_BYTES bytes)
 * that the firmware's partial render test reads: the render target data
 * set, and the geometry timeline with the value one before the geometry's
 * own.
 */
void powervr_geometry_partial_fence(uint8_t *command, uint32_t target_data, uint32_t timeline, uint32_t value);

/* Puts the render target data set of a fragment or partial render command. */
void powervr_fragment_target(uint8_t *command, uint32_t target_data);

/*
 * Fills the static state of a render context (in the record at
 * POWERVR_RENDER_CONTEXT_STATIC): the first core's VDM and TA state
 * buffer words and each buffer's store and resume task words.
 */
void powervr_render_switch_state(uint8_t *record, uint64_t vdm_state, uint64_t ta_state, const struct powervr_switch_tasks tasks[2]);

/* Reads a context reset notice's data (POWERVR_RESET_BYTES bytes after the type and flags). */
void powervr_reset_notice_read(const uint8_t *data, struct powervr_reset_notice *notice);

/*
 * The stage a reset of a context of a kind names and what its line says:
 * the faulting address when the firmware gave one, the reset reason
 * otherwise.  A render context's geometry master is the TA stage and its
 * other masters the 3D; a compute context is the compute stage and a
 * transfer context the transfer stage, whatever master the firmware names
 * (the transfer runs on the fragment master).
 */
void powervr_reset_stage_of(const struct powervr_reset_notice *notice, enum powervr_context_kind kind, struct powervr_reset_stage *result);

#endif
