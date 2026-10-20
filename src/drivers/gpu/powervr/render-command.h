/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register words of a render's geometry and fragment commands that
 * the driver fills for a single-sample render to one colour attachment:
 * the geometry's control stream, primitive and tiling controls, and the
 * fragment's tile, background, end-of-tile and pixel back end words.
 *
 * Nothing here touches the core or memory; the host check builds this
 * file unchanged.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_COMMAND_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_COMMAND_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/gpu/powervr/render-kick-layout.h"
#include "drivers/gpu/powervr/transfer-program.h"

/*
 * A transfer command: its type in a context's buffer, its bytes, and
 * where its registers sit (after the 8-byte common part).
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif.h
 * (ROGUE_FWIF_CCB_CMD_TYPE_TQ_3D 202 with ROGUE_CCB_TYPE_TASK and the
 * magic); pvr_rogue_fwif_client_check.h (struct rogue_fwif_cmd_transfer,
 * 192 bytes: regs 8, flags 184; struct rogue_fwif_transfer_regs:
 * isp_bgobjvals 0, usc_pixel_output_ctrl 4, usc_clear_register0..3 8 to
 * 20, isp_mtile_size 24, isp_render_origin 28, isp_ctl 32, isp_aa 40,
 * event_pixel_pds_info 44, _code 48, _data 52, isp_render 56, isp_rgn
 * 60, frag_screen 64, pds_bgnd0_base 72, pds_bgnd1_base 80,
 * pds_bgnd3_sizeinfo 88, isp_mtile_base 96, pbe_wordx_mrty 104).
 */
#define POWERVR_CLIENT_TRANSFER			(0x2abc0000U | 0x8000U | 202U)
#define POWERVR_TRANSFER_COMMAND_BYTES		192U

/*
 * Where the fragment command's registers are, from the command's start:
 * the registers at 16, then each at its offset in them.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_client_check.h
 * (struct rogue_fwif_frag_regs: usc_pixel_output_ctrl 0,
 * usc_clear_register 4, isp_bgobjdepth 36, isp_bgobjvals 40, isp_aa 44,
 * isp_ctl 52, event_pixel_pds_info 60, event_pixel_pds_data 72,
 * isp_scissor_base 96, isp_dbias_base 104, isp_zlsctl 120,
 * isp_zload_store_base 128, pbe_word 152, pds_bgnd 352, pds_pr_bgnd 400; struct
 * rogue_fwif_cmd_frag: regs 16, flags 464, zls_stride 468; struct
 * rogue_fwif_frag_regs 448 bytes); pvr_rogue_fwif_client.h
 * (ROGUE_FRAG_FLAGS_DEPTHBUFFER bit 7).
 */
#define POWERVR_FRAGMENT_PIXEL_OUTPUT		(16U + 0U)
#define POWERVR_FRAGMENT_BACKGROUND_DEPTH	(16U + 36U)
#define POWERVR_FRAGMENT_BACKGROUND_VALUES	(16U + 40U)
#define POWERVR_FRAGMENT_ANTIALIAS		(16U + 44U)
#define POWERVR_FRAGMENT_ISP_CONTROL		(16U + 52U)
#define POWERVR_FRAGMENT_EVENT_INFO		(16U + 60U)
#define POWERVR_FRAGMENT_EVENT_DATA		(16U + 72U)
#define POWERVR_FRAGMENT_SCISSOR_BASE	(16U + 96U)
#define POWERVR_FRAGMENT_DBIAS_BASE		(16U + 104U)
#define POWERVR_FRAGMENT_ZLS_CONTROL		(16U + 120U)
#define POWERVR_FRAGMENT_ZLS_BASE		(16U + 128U)
#define POWERVR_FRAGMENT_PBE			(16U + 152U)
#define POWERVR_FRAGMENT_ZLS_STRIDE		468U
#define POWERVR_FRAGMENT_DEPTH_BUFFER		(1U << 7)
#define POWERVR_FRAGMENT_BACKGROUND		(16U + 352U)
#define POWERVR_FRAGMENT_FLAGS			464U

/*
 * Where the geometry command's registers are: the control stream base
 * (POWERVR_GEOMETRY_STREAM_BASE), the primitive and tiling controls, the
 * resume task's size, then the command's flags.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_client_check.h
 * (struct rogue_fwif_geom_regs: ppp_ctrl 28, te_psg 32,
 * vdm_context_resume_task0_size 40; struct rogue_fwif_cmd_geom: regs 16,
 * flags 80); pvr_rogue_fwif_client.h (ROGUE_GEOM_FLAGS_FIRSTKICK bit 0,
 * ROGUE_GEOM_FLAGS_LASTKICK bit 1).
 */
#define POWERVR_GEOMETRY_PPP_CONTROL		(16U + 28U)
#define POWERVR_GEOMETRY_TE_PSG			(16U + 32U)
#define POWERVR_GEOMETRY_RESUME_SIZE		(16U + 40U)
#define POWERVR_GEOMETRY_FLAGS			80U
#define POWERVR_GEOMETRY_FIRST_AND_LAST		0x3U

/*
 * The single-core flags of the geometry and fragment commands, set on a
 * core with multicore support so the firmware does not wait for other
 * cores (Mesa's use_single_core); the switch is set once from the
 * firmware image's features.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif_client.h
 * (ROGUE_GEOM_FLAGS_SINGLE_CORE bit 3, ROGUE_FRAG_FLAGS_SINGLE_CORE bit
 * 3); Mesa 26 src/imagination/vulkan/winsys/powervr/pvr_drm_job_render.c
 * (DRM_PVR_SUBMIT_JOB_GEOM_CMD_SINGLE_CORE and _FRAG_CMD_SINGLE_CORE from
 * use_single_core).
 */
#define POWERVR_GEOMETRY_SINGLE_CORE		(1U << 3)
#define POWERVR_FRAGMENT_SINGLE_CORE		(1U << 3)
void powervr_render_command_single_core(int on);

/*
 * What a fragment command of one colour attachment names: the tiles
 * across and down the frame, the background and the background after a
 * partial render, the end-of-tile program's data word and size word,
 * the attachment's three pixel back end register words, and the
 * scissor and depth bias tables the draws' primitives index (zero when
 * there are no draws), and with a depth target its ZLS control, base,
 * background depth and layer stride.
 */
struct powervr_fragment_setup {
	uint32_t tiles_x;
	uint32_t tiles_y;
	uint64_t background[POWERVR_BACKGROUND_WORDS];
	uint64_t partial_background[POWERVR_BACKGROUND_WORDS];
	uint32_t event_data;
	uint32_t event_info;
	uint64_t pbe[3];
	uint64_t scissor_table;
	uint64_t dbias_table;
	bool has_depth;
	uint64_t zls_control;
	uint64_t zls_base;
	uint32_t background_depth;
	uint32_t zls_stride;
};

/*
 * What a geometry command names: its control stream, the bytes of one
 * half's region headers, and the shared registers its programs use.
 */
struct powervr_geometry_setup {
	uint64_t stream;
	uint64_t region_header_bytes;
	uint32_t shared_registers;
};

/* Fills the registers of a fragment command (POWERVR_FRAGMENT_BYTES, zeroed by the caller). */
void powervr_fragment_command(const struct powervr_fragment_setup *setup, uint8_t *command);

/* Fills the registers of a geometry command (POWERVR_GEOMETRY_BYTES, zeroed by the caller). */
void powervr_geometry_command(const struct powervr_geometry_setup *setup, uint8_t *command);

/* Fills a transfer command (POWERVR_TRANSFER_COMMAND_BYTES, zeroed by the caller) from its registers. */
void powervr_transfer_command(const struct powervr_transfer_regs *regs, uint8_t *command);

/* Reports the tile control word the fragment command takes for a frame of tiles. */
uint32_t powervr_fragment_isp_control(uint32_t tiles_x, uint32_t tiles_y);

#endif
