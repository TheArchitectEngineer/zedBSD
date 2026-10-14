/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's objects (ws037-p007 stage D3): what a published identity
 * of each kind stands for, and the handlers that make them.
 *
 * Every image is pitch linear, four bytes a pixel: the display, the
 * texture header and the colour target all read that layout, so nothing
 * is converted between them.  A buffer or an image names the memory it is
 * bound to by holding that memory's object; the memory's storage arrives
 * with the blob libvulkan creates for it (vk-memory.c).
 */

#ifndef DRIVERS_GPU_NVRTX_VK_OBJECTS_H
#define DRIVERS_GPU_NVRTX_VK_OBJECTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/*
 * An image's rows are this many bytes apart at least (a multiple of it),
 * and an image or a buffer starts on this boundary of its memory: the
 * display's offset (256) and stride (64), the texture header's pitch (32)
 * and the colour target's pitch (128) all divide it.
 */
#define NVRTX_VK_PITCH_ALIGN		256U
#define NVRTX_VK_RESOURCE_ALIGN		256U

/* The bytes of one pixel of every image format the executor takes. */
#define NVRTX_VK_PIXEL_BYTES		4U

/* The widest and tallest image (the colour target's and the texture header's limit). */
#define NVRTX_VK_IMAGE_SIDE_MAX		16384U

/* The memory type the executor reports (one: device local, host visible and coherent). */
#define NVRTX_VK_MEMORY_TYPE		0U

/*
 * A VkDeviceMemory.
 *
 * bytes is the size libvulkan asked for (whole pages).  Once the blob for
 * it is attached, cpu is the CPU's view of its first byte (through the
 * BAR1 window), va the GPU's address of it in the work channel's space,
 * and backing the node's record the memory holds a reference to.
 */
struct nvrtx_vk_memory {
	uint64_t bytes;
	uint8_t *cpu;
	uint64_t va;
	void *backing;
	bool attached;
};

/*
 * A VkBuffer: its size and usage, and once bound the memory object it
 * holds and its offset in that memory.
 */
struct nvrtx_vk_buffer {
	uint64_t size;
	uint32_t usage;
	struct nvrtx_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImage: a 2D image of one level, one layer and one sample, pitch
 * linear with rows pitch bytes apart, bytes long; once bound the memory
 * object it holds and its offset in that memory.
 */
struct nvrtx_vk_image {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t usage;
	uint32_t pitch;
	uint64_t bytes;
	struct nvrtx_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImageView: the image object it holds, the format it reads the image
 * as, and where each of red, green, blue and alpha comes from
 * (VkComponentSwizzle, identity resolved).
 */
struct nvrtx_vk_view {
	struct nvrtx_vk_object *image;
	uint32_t format;
	uint32_t swizzle[4];
};

/*
 * A VkSampler: the filters and the address modes a texture read uses, and
 * whether coordinates are in pixels.
 */
struct nvrtx_vk_sampler {
	uint32_t mag_filter;
	uint32_t min_filter;
	uint32_t mipmap_mode;
	uint32_t address_u;
	uint32_t address_v;
	uint32_t address_w;
	uint32_t border_color;
	uint32_t unnormalized;
};

/* The binding numbers a descriptor set layout may use, and the sets a pipeline layout holds. */
#define NVRTX_VK_BINDINGS_MAX		16U
#define NVRTX_VK_SETS_MAX		4U

/* The vertex buffers and attributes a pipeline reads. */
#define NVRTX_VK_VERTEX_BINDINGS_MAX	4U
#define NVRTX_VK_VERTEX_ATTRIBUTES_MAX	8U

/* The sampled images one program reads (each a texture handle in constant buffer 0). */
#define NVRTX_VK_TEXTURES_MAX		8U

/* The push constant bytes a pipeline takes (constant buffer 0 from 0). */
#define NVRTX_VK_PUSH_BYTES		256U

/*
 * A VkShaderModule: a copy of its SPIR-V words, allocated with it.
 */
struct nvrtx_vk_shader {
	uint32_t word_count;
	uint32_t *words;
};

/* One binding of a descriptor set layout: its number, descriptor type and count. */
struct nvrtx_vk_layout_binding {
	uint32_t binding;
	uint32_t type;
	uint32_t count;
};

/*
 * A VkDescriptorSetLayout: its bindings.  Only combined image samplers
 * are bound to anything (the compositor's one kind of descriptor).
 */
struct nvrtx_vk_set_layout {
	uint32_t count;
	struct nvrtx_vk_layout_binding bindings[NVRTX_VK_BINDINGS_MAX];
};

/* A VkPipelineLayout: how many sets it binds and how many push constant bytes it declares. */
struct nvrtx_vk_pipeline_layout {
	uint32_t set_count;
	uint32_t push_bytes;
};

/* A VkDescriptorPool: the most sets it hands out (nothing else is bounded). */
struct nvrtx_vk_descriptor_pool {
	uint32_t max_sets;
};

/*
 * One binding of a descriptor set: the image view and the sampler a
 * combined image sampler names, each object held while the set names it.
 */
struct nvrtx_vk_descriptor_slot {
	struct nvrtx_vk_object *view;
	struct nvrtx_vk_object *sampler;
};

/*
 * A VkDescriptorSet: the pool it came from (only compared, to free the
 * pool's sets with it) and its bindings by number.
 */
struct nvrtx_vk_descriptor_set {
	const void *pool;
	struct nvrtx_vk_descriptor_slot slots[NVRTX_VK_BINDINGS_MAX];
};

/*
 * A VkRenderPass of one subpass writing at most one colour attachment:
 * the attachment's format and load operation, and whether the subpass
 * writes it at all.
 */
struct nvrtx_vk_render_pass {
	uint32_t colour;
	uint32_t format;
	uint32_t load_op;
};

/* A VkFramebuffer: its size and the view of its one attachment, held (NULL with none). */
struct nvrtx_vk_framebuffer {
	uint32_t width;
	uint32_t height;
	struct nvrtx_vk_object *view;
};

/* Where a program's sampled image comes from: a descriptor set and a binding. */
struct nvrtx_vk_texture_source {
	uint32_t set;
	uint32_t binding;
};

/*
 * One compiled program of a pipeline: its address (its header, the code
 * after it), its registers, and the descriptors its texture handles come
 * from, in the order of their slots in constant buffer 0.
 */
struct nvrtx_vk_program {
	uint64_t va;
	uint32_t gprs;
	uint32_t texture_count;
	struct nvrtx_vk_texture_source textures[NVRTX_VK_TEXTURES_MAX];
};

/* A vertex buffer binding of a pipeline: its number, stride and whether it steps per instance. */
struct nvrtx_vk_vertex_binding {
	uint32_t binding;
	uint32_t stride;
	uint32_t instance;
};

/* A vertex attribute of a pipeline: its location, binding, format and offset. */
struct nvrtx_vk_vertex_attribute {
	uint32_t location;
	uint32_t binding;
	uint32_t format;
	uint32_t offset;
};

/*
 * A graphics VkPipeline: its two programs in a run of the executor's GPU
 * memory (code_token names the run to the node), the vertex input, the
 * topology, the culling, the blend of the colour attachment, and the
 * viewport and scissor unless they are dynamic (floats as their bits).
 */
struct nvrtx_vk_pipeline {
	void *code_token;
	struct nvrtx_vk_program vertex;
	struct nvrtx_vk_program fragment;
	uint32_t push_bytes;
	uint32_t binding_count;
	struct nvrtx_vk_vertex_binding bindings[NVRTX_VK_VERTEX_BINDINGS_MAX];
	uint32_t attribute_count;
	struct nvrtx_vk_vertex_attribute attributes[NVRTX_VK_VERTEX_ATTRIBUTES_MAX];
	uint32_t topology;
	uint32_t cull_mode;
	uint32_t front_face;
	uint32_t blend_enable;
	uint32_t blend_src_colour;
	uint32_t blend_dst_colour;
	uint32_t blend_colour_op;
	uint32_t blend_src_alpha;
	uint32_t blend_dst_alpha;
	uint32_t blend_alpha_op;
	uint32_t write_mask;
	uint32_t dynamic_viewport;
	uint32_t dynamic_scissor;
	uint32_t viewport[6];
	VkRect2D scissor;
};

int nvrtx_vk_memory_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int nvrtx_vk_resource_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int nvrtx_vk_layout_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int nvrtx_vk_target_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int nvrtx_vk_pipeline_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

uint32_t nvrtx_vk_result(int error);
uint64_t nvrtx_vk_create_tail(struct i915_wire_reader *reader);
void nvrtx_vk_create_reply(struct nvrtx_vk_session *session, struct i915_wire_writer *reply, enum i915_vk_object_kind kind, uint64_t identity, void *payload, int (*destroy)(struct nvrtx_vk_session *, void *), int error);
int nvrtx_vk_destroy_plain(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, enum i915_vk_object_kind kind);
void *nvrtx_vk_payload(struct nvrtx_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int nvrtx_vk_free_payload(struct nvrtx_vk_session *session, void *payload);

bool nvrtx_vk_format_colour(uint32_t format);
int nvrtx_vk_buffer_address(const struct nvrtx_vk_buffer *buffer, uint64_t *va, uint8_t **cpu);
int nvrtx_vk_image_address(const struct nvrtx_vk_image *image, uint64_t *va, uint8_t **cpu);

#endif /* DRIVERS_GPU_NVRTX_VK_OBJECTS_H */
