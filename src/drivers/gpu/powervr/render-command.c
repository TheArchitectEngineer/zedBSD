/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register words of a render's geometry and fragment commands.
 */

#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/powervr/render-command.h"

/*
 * The pixel output control and the tile control of the BXE-4-32 for a
 * single-sample attachment of one 32-bit word a pixel: two output
 * registers a pixel (the core's least), a partition mask of every one of
 * its ten partitions, and four tiles in flight on its one pipe.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/cr.xml
 * (USC_PIXEL_OUTPUT_CTRL: partition_mask 3..20, width 0..1 with
 * 2REGISTERS 0; ISP_CTL: skip_init_hdrs 31, process_empty_tiles 17,
 * sample_pos 16, pipe_enable 12..15 with PIPE_ONE 0);
 * src/imagination/common/device_info/bxe-4-32.h
 * (usc_min_output_registers_per_pix 2, max_partitions 10,
 * isp_max_tiles_in_flight 4, tile_size_x 16, num_raster_pipes 1,
 * simple_parameter_format_version 2, has_usc_pixel_partition_mask,
 * has_gpu_multicore_support, quirks 72168 and 72463).
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_common.c
 * (pvr_arch_setup_tiles_in_flight: the width rounded up to the least
 * registers, the partitions of 16x16 tiles, the ISP tiles in flight no
 * more than the partitions; the mask of every partition; the pipes less
 * one); pvr_arch_job_render.c (pvr_frag_state_stream_init: the sample
 * positions, empty tiles processed, the header skip of a multicore core
 * unless the quirks' tile shapes forbid it); Linux v7.3
 * drivers/gpu/drm/imagination/pvr_drv.c
 * (rogue_get_common_store_partition_space_size).
 */
#define PIXEL_OUTPUT_TWO_REGISTERS	(0x3ffU << 3)
#define ISP_SKIP_INIT_HEADERS		(1U << 31)
#define ISP_PROCESS_EMPTY_TILES		(1U << 17)
#define ISP_SAMPLE_POSITIONS		(1U << 16)
#define ISP_TILES_IN_FLIGHT		(3U << 12)

/*
 * The background object's depth (1.0 as a 32-bit float, the depth a
 * frame without a depth attachment clears to) and its values (the
 * background tag on, the stencil mask on).
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/cr.xml (ISP_BGOBJDEPTH value
 * 0..31; ISP_BGOBJVALS: enablebgtag 9, mask 8, stencil 0..7).
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_render.c
 * (pvr_frag_state_stream_init: the depth's clear value in the F32Z
 * form, the mask set, the tag when a background runs).
 */
#define BACKGROUND_DEPTH_ONE		0x3f800000U
#define BACKGROUND_TAG_AND_MASK		((1U << 9) | (1U << 8))

/*
 * The geometry's primitive and tiling controls: the w clamp and the
 * fixed point format 1; completion when the stream terminates, and the
 * region header stride in units of 4096 bytes; the resume task size as
 * a data sequencer state word with the common store the shared
 * registers take, in units of 64 bytes.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/cr.xml (PPP_CTRL:
 * fixed_point_format 9, wclampen 1; TE_PSG: completeonterminate 17,
 * region_stride 0..10 in units of 4096); vdm.xml (PDS_STATE0: block_type
 * 29..31 with PDS_STATE_UPDATE 1, usc_common_size 16..24 in units of
 * 64).
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_render.c
 * (pvr_geom_state_stream_init: PPP_CTRL, TE_PSG for the last geometry,
 * the resume program's common size from the shared registers).
 */
#define PPP_WCLAMP_FIXED_POINT		((1U << 9) | (1U << 1))
#define TE_COMPLETE_ON_TERMINATE	(1U << 17)
#define TE_REGION_STRIDE_UNIT		4096U
#define TE_REGION_STRIDE_MAX		0x7ffU
#define RESUME_STATE_UPDATE		(1U << 29)
#define RESUME_COMMON_SHIFT		16U
#define RESUME_COMMON_UNIT		64U

static void put32(uint8_t *bytes, uint32_t offset, uint32_t value);
static void put64(uint8_t *bytes, uint32_t offset, uint64_t value);

/*
 * Fills the registers of a fragment command.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_render.c
 * (pvr_frag_state_stream_init: the pixel back end words, the background
 * and the background after a partial render, no clear registers, the
 * pixel output control, the background depth and values, no
 * antialiasing, the tile control, the end-of-tile program's size and
 * data; nothing for depth or stencil).
 */
void
powervr_fragment_command(
	const struct powervr_fragment_setup *setup,
	uint8_t *command)
{
	unsigned index;

	/* The first attachment's back end words. */
	for (index = 0; index < 3U; index++)
		put64(command, POWERVR_FRAGMENT_PBE + index * 8U, setup->pbe[index]);

	/* The background, and the one after a partial render. */
	for (index = 0; index < POWERVR_BACKGROUND_WORDS; index++) {
		put64(command, POWERVR_FRAGMENT_BACKGROUND + index * 8U, setup->background[index]);
		put64(command, POWERVR_FRAGMENT_PARTIAL_BACKGROUND + index * 8U, setup->partial_background[index]);
	}

	/* The tile's pixel output, background object and tile controls. */
	put32(command, POWERVR_FRAGMENT_PIXEL_OUTPUT, PIXEL_OUTPUT_TWO_REGISTERS);
	put32(command, POWERVR_FRAGMENT_BACKGROUND_DEPTH, setup->has_depth ? setup->background_depth : BACKGROUND_DEPTH_ONE);
	put32(command, POWERVR_FRAGMENT_BACKGROUND_VALUES, BACKGROUND_TAG_AND_MASK);
	put32(command, POWERVR_FRAGMENT_ANTIALIAS, 0);
	put32(command, POWERVR_FRAGMENT_ISP_CONTROL, powervr_fragment_isp_control(setup->tiles_x, setup->tiles_y));

	/*
	 * The scissor and depth bias tables, each register keeping the
	 * address's bits 2 to 39.
	 *
	 * Values referenced:
	 * Mesa 26.2.5 src/imagination/csbgen/rogue/cr.xml (ISP_SCISSOR_BASE,
	 * ISP_DBIAS_BASE: addr 2..39 shifted by 2).
	 */
	put64(command, POWERVR_FRAGMENT_SCISSOR_BASE, setup->scissor_table & 0xfffffffffcULL);
	put64(command, POWERVR_FRAGMENT_DBIAS_BASE, setup->dbias_table & 0xfffffffffcULL);

	/* A depth target's loads and stores, and the depth buffer flag. */
	if (setup->has_depth) {
		put64(command, POWERVR_FRAGMENT_ZLS_CONTROL, setup->zls_control);
		put64(command, POWERVR_FRAGMENT_ZLS_BASE, setup->zls_base);
		put32(command, POWERVR_FRAGMENT_ZLS_STRIDE, setup->zls_stride);
		put32(command, POWERVR_FRAGMENT_FLAGS, POWERVR_FRAGMENT_DEPTH_BUFFER);
	}

	/* The end-of-tile program. */
	put32(command, POWERVR_FRAGMENT_EVENT_INFO, setup->event_info);
	put32(command, POWERVR_FRAGMENT_EVENT_DATA, setup->event_data);
}

/*
 * Fills the registers of a geometry command: its stream, its primitive
 * and tiling controls, its resume size and its flags (the render's
 * first and last geometry).
 */
void
powervr_geometry_command(
	const struct powervr_geometry_setup *setup,
	uint8_t *command)
{
	uint64_t stride;
	uint32_t common;

	/* The stream and the primitive control. */
	powervr_geometry_stream(command, setup->stream);
	put32(command, POWERVR_GEOMETRY_PPP_CONTROL, PPP_WCLAMP_FIXED_POINT);

	/* The tiling control: done at the stream's end, the region header stride. */
	stride = setup->region_header_bytes / TE_REGION_STRIDE_UNIT;
	if (stride > TE_REGION_STRIDE_MAX)
		stride = TE_REGION_STRIDE_MAX;
	put32(command, POWERVR_GEOMETRY_TE_PSG, TE_COMPLETE_ON_TERMINATE | (uint32_t)stride);

	/* The resume size: the common store of the shared registers. */
	common = (setup->shared_registers * 4U + RESUME_COMMON_UNIT - 1U) / RESUME_COMMON_UNIT;
	put32(command, POWERVR_GEOMETRY_RESUME_SIZE, RESUME_STATE_UPDATE | (common << RESUME_COMMON_SHIFT));

	/* The only geometry of its render. */
	put32(command, POWERVR_GEOMETRY_FLAGS, POWERVR_GEOMETRY_FIRST_AND_LAST);
}

/*
 * Fills a transfer command from its registers, each at its place in the
 * firmware's transfer registers; the common part and the flags stay
 * zero.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_submit_info_stream_init: the registers of one transfer).
 */
void
powervr_transfer_command(
	const struct powervr_transfer_regs *regs,
	uint8_t *command)
{
	unsigned index;

	/* The 32-bit registers. */
	put32(command, 8U + 0U, regs->isp_bgobjvals);
	put32(command, 8U + 4U, regs->usc_pixel_output_ctrl);
	for (index = 0; index < 4U; index++)
		put32(command, 8U + 8U + index * 4U, regs->usc_clear[index]);
	put32(command, 8U + 24U, regs->isp_mtile_size);
	put32(command, 8U + 28U, regs->isp_render_origin);
	put32(command, 8U + 32U, regs->isp_ctl);
	put32(command, 8U + 40U, regs->isp_aa);
	put32(command, 8U + 44U, regs->event_pixel_pds_info);
	put32(command, 8U + 48U, regs->event_pixel_pds_code);
	put32(command, 8U + 52U, regs->event_pixel_pds_data);
	put32(command, 8U + 56U, regs->isp_render);
	put32(command, 8U + 60U, regs->isp_rgn);
	put32(command, 8U + 64U, regs->frag_screen);

	/* The 64-bit ones: the background, the macrotile base, the back end words. */
	for (index = 0; index < 3U; index++)
		put64(command, 8U + 72U + index * 8U, regs->pds_bgnd[index]);
	put64(command, 8U + 96U, regs->isp_mtile_base);
	for (index = 0; index < POWERVR_TRANSFER_PBE_WORDS; index++)
		put64(command, 8U + 104U + index * 8U, regs->pbe[index]);
}

/* Reports the tile control word the fragment command takes for a frame of tiles. */
uint32_t
powervr_fragment_isp_control(
	uint32_t tiles_x,
	uint32_t tiles_y)
{
	uint32_t word;
	uint32_t groups;

	/* Sample positions, empty tiles processed, the tiles in flight, the header skip. */
	word = ISP_SAMPLE_POSITIONS | ISP_PROCESS_EMPTY_TILES | ISP_TILES_IN_FLIGHT | ISP_SKIP_INIT_HEADERS;

	/* No header skip for a frame two tiles wide or less whose tile group rows (tiles down rounded up to pairs, halved) the quirks name. */
	groups = (tiles_y + 1U) / 2U;
	if (tiles_x <= 2U && ((groups & 1U) == 1U || groups <= 2U))
		word &= ~ISP_SKIP_INIT_HEADERS;

	/* Succeeded: the word. */
	return word;
}

/* Writes a little-endian 32-bit word. */
static void
put32(
	uint8_t *bytes,
	uint32_t offset,
	uint32_t value)
{
	/* Low byte first, as the firmware reads it. */
	bytes[offset + 0U] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
	bytes[offset + 2U] = (uint8_t)(value >> 16);
	bytes[offset + 3U] = (uint8_t)(value >> 24);
}

/* Writes a little-endian 64-bit word. */
static void
put64(
	uint8_t *bytes,
	uint32_t offset,
	uint64_t value)
{
	/* The low half, then the high. */
	put32(bytes, offset, (uint32_t)value);
	put32(bytes, offset + 4U, (uint32_t)(value >> 32));
}
