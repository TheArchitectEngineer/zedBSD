/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The fixed programs and state words of a PowerVR render that the
 * driver writes itself, without the compiler: the end-of-tile program
 * that hands the tile to the pixel back end, the background that fills
 * a tile with the clear colour, the pixel back end's words of a colour
 * attachment, and the kernel's own test triangle (its programs and its
 * vertex and primitive stream).
 *
 * Nothing here touches the core or memory: the callers give the heap
 * offsets and device addresses where the results go, and the host
 * check builds this file unchanged.  Data sequencer and shading cluster
 * addresses are offsets from their heap's base, as the core reads them.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_PROGRAM_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_PROGRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"

/* The largest fixed shading cluster program here, in bytes. */
#define POWERVR_RENDER_USC_MAX		256U

/*
 * The data and code words of a data sequencer program that only starts
 * a shading cluster task: the task word in the first two of eight data
 * words, one code word after them.  The core assumes the eight data
 * words and finds the code after them.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pds/pvr_pds.c (pvr_pds_kick_usc:
 * ROGUE_PDS_FIXED_PIXEL_SHADER_DATA_SIZE 8 data words, one code word);
 * src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_cs_write_load_op_for_view: the core assumes two 128-bit data
 * words and the code after them).
 */
#define POWERVR_RENDER_KICK_DATA_WORDS	8U
#define POWERVR_RENDER_KICK_CODE_WORDS	1U

/*
 * The data words of the end-of-tile data sequencer program, whose code
 * is the fixed area's (fixed-area.h): the task word of the end-of-tile
 * shading cluster program.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pds/pvr_pds.c
 * (pvr_pds_generate_pixel_event with no emit word pairs: one 64-bit
 * constant for the task word, data size 2).
 */
#define POWERVR_RENDER_EVENT_DATA_WORDS	2U

/* The words of the primitive state block of the test triangle. */
#define POWERVR_RENDER_PPP_WORDS	19U

/* The words of the test triangle's vertex control stream. */
#define POWERVR_RENDER_VDM_WORDS	10U

/* The coordinates of one vertex of the test triangle: x and y in pixels, z. */
#define POWERVR_RENDER_VERTEX_WORDS	3U

/* A shading cluster program and the temporaries it needs. */
struct powervr_render_usc {
	uint8_t code[POWERVR_RENDER_USC_MAX];
	size_t length;
	unsigned temps;
};

/* A data sequencer program that starts one shading cluster task. */
struct powervr_render_kick {
	uint32_t data[POWERVR_RENDER_KICK_DATA_WORDS];
	uint32_t code[POWERVR_RENDER_KICK_CODE_WORDS];
};

/*
 * The colour attachment the pixel back end stores a tile to: its format,
 * its device address, its edges in pixels, whether it is linear (rows of
 * row_pitch bytes) or twiddled (over padded_width texels a row).
 */
struct powervr_render_surface {
	const struct powervr_format *format;
	uint64_t address;
	uint32_t width;
	uint32_t height;
	bool linear;
	uint32_t row_pitch;
	uint32_t padded_width;
};

/* A rectangle of pixels: its corner and its edges. */
struct powervr_render_area {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

/*
 * The data sequencer programs of a background: where the program that
 * starts the shading cluster task lies (its data, the code after it),
 * where the program that fills the shared registers has its code and its
 * data, how many data words that one has and the shared registers it
 * fills.  All are offsets in the data sequencer heap.
 */
struct powervr_render_background {
	uint32_t kick_offset;
	uint32_t shared_code_offset;
	uint32_t shared_data_offset;
	uint32_t shared_data_words;
	uint32_t shared_registers;
};

/*
 * What the test triangle's streams name: the primitive state block's
 * device address, the fragment's background-shaped programs (as for a
 * background), and the vertex data sequencer program's data and code
 * offsets, its data words and temporaries.
 */
struct powervr_render_triangle {
	uint64_t ppp_address;
	struct powervr_render_background fragment;
	uint32_t vertex_data_offset;
	uint32_t vertex_code_offset;
	uint32_t vertex_data_words;
	uint32_t vertex_temps;
};

int powervr_render_eot_usc(const uint32_t state[2], struct powervr_render_usc *program);
int powervr_render_event_data(uint32_t usc_offset, unsigned temps, uint32_t data[POWERVR_RENDER_EVENT_DATA_WORDS]);
int powervr_render_event_words(uint32_t data_offset, uint32_t *code_word, uint32_t *data_word, uint32_t *info_word);
int powervr_render_clear_usc(struct powervr_render_usc *program);
int powervr_render_clear_usc_words(uint32_t words, struct powervr_render_usc *program);
int powervr_render_clear_shared(uint32_t colour, struct powervr_data_shared_program *program);
int powervr_render_clear_shared_words(const uint32_t *colour, uint32_t words, struct powervr_data_shared_program *program);
int powervr_render_colour_usc(uint32_t colour, struct powervr_render_usc *program);
int powervr_render_vertex_usc(struct powervr_render_usc *program);
int powervr_render_vertex_pds(uint64_t vertices, uint32_t usc_offset, unsigned temps, struct powervr_data_vertex_program *program);
int powervr_render_kick_build(uint32_t usc_offset, unsigned temps, struct powervr_render_kick *kick);
int powervr_render_background_words(const struct powervr_render_background *background, uint64_t words[3]);
int powervr_render_pbe_words(const struct powervr_render_surface *surface, const struct powervr_render_area *area, uint32_t state[2], uint64_t reg[3]);
int powervr_render_triangle_ppp(const struct powervr_render_triangle *triangle, uint32_t words[POWERVR_RENDER_PPP_WORDS]);
int powervr_render_triangle_vdm(const struct powervr_render_triangle *triangle, uint32_t words[POWERVR_RENDER_VDM_WORDS]);

#endif /* KERN_DRIVERS_GPU_POWERVR_RENDER_PROGRAM_H */
