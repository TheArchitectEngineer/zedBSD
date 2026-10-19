/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR descriptor set's memory: where each binding of a set
 * layout lies, and the bytes one buffer, image, sampler or combined
 * image and sampler descriptor holds there.  The driver's data sequencer
 * programs copy these bytes into shared registers for the shader.  This
 * part only computes; it holds no lock and no memory, and it builds on
 * the host too.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_DESCRIPTOR_SET_H
#define KERN_DRIVERS_GPU_POWERVR_DESCRIPTOR_SET_H

#include <stdint.h>

/* The most bindings of a set layout, numbered from 0. */
#define POWERVR_SET_BINDINGS_MAX	8U

/* The bytes of each kind of descriptor. */
#define POWERVR_DESCRIPTOR_BUFFER_BYTES		16U
#define POWERVR_DESCRIPTOR_IMAGE_BYTES		32U
#define POWERVR_DESCRIPTOR_SAMPLER_BYTES	48U
#define POWERVR_DESCRIPTOR_COMBINED_BYTES	80U

/* The pack information of a format the shader's pack instruction has no format for. */
#define POWERVR_PACK_FORMAT_INVALID	0x1fU

/* One binding a set layout's create names: its number, its Vulkan type and its descriptor count. */
struct powervr_set_binding_info {
	uint32_t binding;
	uint32_t type;
	uint32_t count;
};

/*
 * Where one binding's descriptors lie in a set's memory: the first at
 * offset, each next one stride bytes on.  A binding the layout does not
 * name has a count of 0.
 */
struct powervr_set_binding {
	uint32_t type;
	uint32_t count;
	uint32_t offset;
	uint32_t stride;
};

/* A set layout's bindings by number, and the bytes of a set of it. */
struct powervr_set_layout {
	struct powervr_set_binding bindings[POWERVR_SET_BINDINGS_MAX];
	uint32_t bytes;
};

int powervr_set_layout_build(const struct powervr_set_binding_info *infos, uint32_t count, struct powervr_set_layout *layout);
int powervr_set_descriptor_offset(const struct powervr_set_layout *layout, uint32_t binding, uint32_t element, uint32_t part, uint32_t *offset);
uint32_t powervr_pack_info(uint32_t vk_format);
void powervr_descriptor_buffer(uint8_t bytes[POWERVR_DESCRIPTOR_BUFFER_BYTES], uint64_t address, uint32_t range);
void powervr_descriptor_image(uint8_t bytes[POWERVR_DESCRIPTOR_IMAGE_BYTES], const uint64_t words[2], uint32_t layer_bytes, uint32_t pack_info);
void powervr_descriptor_sampler(uint8_t bytes[POWERVR_DESCRIPTOR_SAMPLER_BYTES], const uint64_t words[2], uint32_t compare_op);

#endif /* KERN_DRIVERS_GPU_POWERVR_DESCRIPTOR_SET_H */
