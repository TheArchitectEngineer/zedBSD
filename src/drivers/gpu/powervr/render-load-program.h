/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The background that loads a colour attachment into the tile (load op
 * LOAD, and the partial render's pds_pr_bgnd): a shading cluster program
 * that samples the attachment at the pixel's position and packs it into
 * pixel output register 0, the eight shared words it reads (the image
 * and sampler state), and the data sequencer program that copies them
 * in.  Nothing here touches the core or memory; the host check builds
 * this file unchanged.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_LOAD_PROGRAM_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_LOAD_PROGRAM_H

#include <stdint.h>

#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-program.h"

/* The shared words the load program reads: the image state, then the sampler state. */
#define POWERVR_RENDER_LOAD_SHARED_WORDS	8U

int powervr_render_load_usc(const struct powervr_format *format, struct powervr_render_usc *program);
int powervr_render_load_words(const struct powervr_view_shape *attachment, uint32_t words[POWERVR_RENDER_LOAD_SHARED_WORDS]);
int powervr_render_load_shared(uint64_t words_address, struct powervr_data_shared_program *program);

#endif /* KERN_DRIVERS_GPU_POWERVR_RENDER_LOAD_PROGRAM_H */
