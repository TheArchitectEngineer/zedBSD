/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The words of a PowerVR transfer job (copy and clear) and its fixed
 * programs: the fragment registers of the transfer command, the copy's
 * shading cluster program, the shared register words the copy reads its
 * source with, and the packed clear colour.
 *
 * A transfer is a full-rate tile render without geometry: the copy is a
 * background object whose program reads the source texel for each
 * destination pixel, the clear has no object at all and lets the pixel
 * back end store the tile's clear registers; either way the pixel back
 * end's clip is the destination rectangle.  Nothing here touches the core
 * or memory, and the host check builds this file unchanged.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_TRANSFER_PROGRAM_H
#define KERN_DRIVERS_GPU_POWERVR_TRANSFER_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-program.h"

/* The shared register words the copy program reads: image state, sampler state, the four coordinate floats. */
#define POWERVR_TRANSFER_SHARED_WORDS	12U

/* The pixel back end's register words of the transfer command (three for each of three targets). */
#define POWERVR_TRANSFER_PBE_WORDS	9U

/* What a transfer job does. */
enum powervr_transfer_kind {
	POWERVR_TRANSFER_CLEAR = 0,
	POWERVR_TRANSFER_COPY,
	POWERVR_TRANSFER_BLIT
};

/*
 * The registers of one transfer command, in the order the firmware's
 * transfer register block holds them.
 */
struct powervr_transfer_regs {
	uint64_t pds_bgnd[3];
	uint64_t isp_mtile_base;
	uint64_t pbe[POWERVR_TRANSFER_PBE_WORDS];
	uint32_t isp_bgobjvals;
	uint32_t usc_pixel_output_ctrl;
	uint32_t usc_clear[4];
	uint32_t isp_mtile_size;
	uint32_t isp_render_origin;
	uint32_t isp_ctl;
	uint32_t isp_aa;
	uint32_t event_pixel_pds_info;
	uint32_t event_pixel_pds_code;
	uint32_t event_pixel_pds_data;
	uint32_t isp_render;
	uint32_t isp_rgn;
	uint32_t frag_screen;
};

/*
 * One transfer job: its kind, the destination and the rectangle written,
 * the clear colour (four float or integer bit patterns) or the source
 * corner the rectangle's corner reads (a blit's source rectangle and
 * filter go in its shared words, powervr_transfer_blit_words), and where
 * the job's data sequencer programs lie (offsets in the data sequencer
 * heap): the copy or blit program's task, the shared register program's
 * code and data and its data words, and the end-of-tile program's data.
 */
struct powervr_transfer_job {
	enum powervr_transfer_kind kind;
	struct powervr_render_surface destination;
	struct powervr_render_area area;
	uint32_t colour[4];
	uint32_t source_x;
	uint32_t source_y;
	uint32_t copy_kick_offset;
	uint32_t shared_code_offset;
	uint32_t shared_data_offset;
	uint32_t shared_data_words;
	uint32_t event_data_offset;
};

const struct powervr_format *powervr_transfer_raw_format(uint32_t texel_bytes);
bool powervr_transfer_needs_rows(const struct powervr_render_surface *surface, const struct powervr_render_area *area);
int powervr_transfer_copy_usc(struct powervr_render_usc *program);
int powervr_transfer_blit_usc(const struct powervr_format *format, struct powervr_render_usc *program);
int powervr_transfer_shared_words(const struct powervr_view_shape *source, uint32_t source_x, uint32_t source_y, const struct powervr_render_area *area, uint32_t words[POWERVR_TRANSFER_SHARED_WORDS]);
int powervr_transfer_blit_words(const struct powervr_view_shape *source, const struct powervr_render_area *source_area, const struct powervr_render_area *area, bool flip_x, bool flip_y, bool linear, uint32_t words[POWERVR_TRANSFER_SHARED_WORDS]);
int powervr_transfer_shared_program(uint64_t words_address, struct powervr_data_shared_program *program);
int powervr_transfer_clear_colour(const struct powervr_format *format, const uint32_t colour[4], uint32_t packed[4]);
int powervr_transfer_regs_build(const struct powervr_transfer_job *job, struct powervr_transfer_regs *regs, uint32_t state[2]);

#endif /* KERN_DRIVERS_GPU_POWERVR_TRANSFER_PROGRAM_H */
