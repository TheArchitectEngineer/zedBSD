/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's objects: what a published identity of each kind stands for,
 * and the handlers that make them.
 *
 * Every image is linear, four bytes a pixel: the display, the image
 * descriptor and the colour target all read that layout, so nothing is
 * converted between them.  A buffer or an image names the memory it is
 * bound to by holding that memory's object; the memory's storage arrives
 * with the blob libvulkan creates for it (vk-memory.c).
 */

#ifndef DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_OBJECTS_H
#define DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_OBJECTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-compile.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-program.h"

/*
 * An image's rows are its width's bytes rounded up to this many, and an
 * image or a buffer starts on this boundary of its memory: a linear
 * colour target and image descriptor of GFX10 take their row pitch from the
 * width rounded up to 256 bytes, and their address from bit 8.
 */
#define AMDRDNA_VK_PITCH_ALIGN		256U
#define AMDRDNA_VK_RESOURCE_ALIGN		256U

/* The bytes of one pixel of every image format the executor takes. */
#define AMDRDNA_VK_PIXEL_BYTES		4U

/* The widest and tallest image (the colour target's and the image descriptor's limit). */
#define AMDRDNA_VK_IMAGE_SIDE_MAX		16384U

/*
 * The memory types the executor reports: device local only (VRAM the CPU
 * does not reach, heap 0), and device local, host visible and coherent
 * (the CPU's window, heap 1); every buffer and image takes either.
 */
#define AMDRDNA_VK_MEMORY_DEVICE	0U
#define AMDRDNA_VK_MEMORY_HOST		1U
#define AMDRDNA_VK_MEMORY_TYPES		2U

/*
 * A VkDeviceMemory.
 *
 * bytes is the size libvulkan asked for (whole pages).  Once its storage
 * is attached (a blob, or the executor's own outside the CPU's window),
 * cpu is the CPU's view of its first byte (through the VRAM window, or
 * system memory; NULL outside the window), va the GPU's address of it in
 * the session's address space, and backing the node's record the memory
 * holds a reference to.
 */
struct amdrdna_vk_memory {
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
struct amdrdna_vk_buffer {
	uint64_t size;
	uint32_t usage;
	struct amdrdna_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImage: a 2D image of one level, one layer and one sample, pitch
 * linear with rows pitch bytes apart, bytes long; once bound the memory
 * object it holds and its offset in that memory.
 */
struct amdrdna_vk_image {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t usage;
	uint32_t pitch;
	uint64_t bytes;
	struct amdrdna_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImageView: the image object it holds, the format it reads the image
 * as, and where each of red, green, blue and alpha comes from
 * (VkComponentSwizzle, identity resolved).
 */
struct amdrdna_vk_view {
	struct amdrdna_vk_object *image;
	uint32_t format;
	uint32_t swizzle[4];
};

/*
 * A VkSampler: the filters and the address modes a texture read uses,
 * whether coordinates are in pixels, and the depth compare a compare read
 * makes (VkCompareOp, when compare_enable is set).
 */
struct amdrdna_vk_sampler {
	uint32_t mag_filter;
	uint32_t min_filter;
	uint32_t mipmap_mode;
	uint32_t address_u;
	uint32_t address_v;
	uint32_t address_w;
	uint32_t border_color;
	uint32_t unnormalized;
	uint32_t compare_enable;
	uint32_t compare_op;
};

/* The binding numbers a descriptor set layout may use, and the sets a pipeline layout holds. */
#define AMDRDNA_VK_BINDINGS_MAX		16U
#define AMDRDNA_VK_SETS_MAX		4U

/* The vertex buffers and attributes a pipeline reads. */
#define AMDRDNA_VK_VERTEX_BINDINGS_MAX	4U
#define AMDRDNA_VK_VERTEX_ATTRIBUTES_MAX	8U

/* The sampled images one program reads (each an image and a sampler descriptor in its resource table). */
#define AMDRDNA_VK_TEXTURES_MAX		8U

/* The push constant bytes a pipeline takes (the resource table from 0). */
#define AMDRDNA_VK_PUSH_BYTES		256U

/*
 * A VkShaderModule: a copy of its SPIR-V words, allocated with it.
 */
struct amdrdna_vk_shader {
	uint32_t word_count;
	uint32_t *words;
};

/* One binding of a descriptor set layout: its number, descriptor type and count. */
struct amdrdna_vk_layout_binding {
	uint32_t binding;
	uint32_t type;
	uint32_t count;
};

/*
 * A VkDescriptorSetLayout: its bindings.  Combined image samplers,
 * uniform buffers and storage buffers are bound.
 */
struct amdrdna_vk_set_layout {
	uint32_t count;
	struct amdrdna_vk_layout_binding bindings[AMDRDNA_VK_BINDINGS_MAX];
};

/* A VkPipelineLayout: how many sets it binds and how many push constant bytes it declares. */
struct amdrdna_vk_pipeline_layout {
	uint32_t set_count;
	uint32_t push_bytes;
};

/* A VkDescriptorPool: the most sets it hands out (nothing else is bounded). */
struct amdrdna_vk_descriptor_pool {
	uint32_t max_sets;
};

/*
 * One binding of a descriptor set: the image view and the sampler a
 * combined image sampler names, or the buffer, offset and range a uniform
 * or storage buffer names; each object held while the set names it.
 */
struct amdrdna_vk_descriptor_slot {
	struct amdrdna_vk_object *view;
	struct amdrdna_vk_object *sampler;
	struct amdrdna_vk_object *buffer;
	uint64_t offset;
	uint64_t range;
};

/*
 * A VkDescriptorSet: the pool it came from (only compared, to free the
 * pool's sets with it) and its bindings by number.
 */
struct amdrdna_vk_descriptor_set {
	const void *pool;
	struct amdrdna_vk_descriptor_slot slots[AMDRDNA_VK_BINDINGS_MAX];
};

/*
 * A VkRenderPass of one subpass writing at most one colour attachment:
 * the attachment's format and load operation, and whether the subpass
 * writes it at all.
 */
struct amdrdna_vk_render_pass {
	uint32_t colour;
	uint32_t format;
	uint32_t load_op;
};

/* A VkFramebuffer: its size and the view of its one attachment, held (NULL with none). */
struct amdrdna_vk_framebuffer {
	uint32_t width;
	uint32_t height;
	struct amdrdna_vk_object *view;
};

/*
 * One compiled program of a pipeline: what the compiler made of it (its
 * interface and the layout of its resource table), the register values
 * that run it, and the GPU address of its code (256-byte aligned).
 */
struct amdrdna_vk_program {
	struct amdrdna_gfx10_program *compiled;
	struct amdrdna_gfx10_registers registers;
	uint64_t va;
};

/* A vertex buffer binding of a pipeline: its number, stride and whether it steps per instance. */
struct amdrdna_vk_vertex_binding {
	uint32_t binding;
	uint32_t stride;
	uint32_t instance;
};

/* A vertex attribute of a pipeline: its location, binding, format and offset. */
struct amdrdna_vk_vertex_attribute {
	uint32_t location;
	uint32_t binding;
	uint32_t format;
	uint32_t offset;
};

/*
 * A VkPipeline: its programs in a run of the executor's GPU memory
 * (code_token names the run to the node): a graphics pipeline's vertex and
 * fragment programs, or a compute pipeline's one; for a graphics pipeline
 * the vertex input, the topology, the culling, the blend of the colour
 * attachment, and the viewport and scissor unless they are dynamic (floats
 * as their bits).
 */
struct amdrdna_vk_pipeline {
	void *code_token;
	uint32_t compute;
	struct amdrdna_vk_program vertex;
	struct amdrdna_vk_program fragment;
	struct amdrdna_vk_program kernel;
	uint32_t push_bytes;
	uint32_t binding_count;
	struct amdrdna_vk_vertex_binding bindings[AMDRDNA_VK_VERTEX_BINDINGS_MAX];
	uint32_t attribute_count;
	struct amdrdna_vk_vertex_attribute attributes[AMDRDNA_VK_VERTEX_ATTRIBUTES_MAX];
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

/* The regions one copy, blit or clear records. */
#define AMDRDNA_VK_REGIONS_MAX		16U

/* A command buffer's states. */
#define AMDRDNA_VK_COMMAND_INITIAL	0U
#define AMDRDNA_VK_COMMAND_RECORDING	1U
#define AMDRDNA_VK_COMMAND_EXECUTABLE	2U
#define AMDRDNA_VK_COMMAND_INVALID	3U

/* A VkFence: whether it is signaled (a submission that names it signals it once its work is done). */
struct amdrdna_vk_fence {
	bool signaled;
};

/*
 * The queue families: the universal one (graphics, compute and transfer,
 * on the graphics queue) and the compute one (on a compute queue of its
 * own).
 */
#define AMDRDNA_VK_FAMILY_UNIVERSAL	0U
#define AMDRDNA_VK_FAMILY_COMPUTE	1U
#define AMDRDNA_VK_FAMILIES		2U

/* A VkCommandPool: its queue family, and compared to free and reset its command buffers with it. */
struct amdrdna_vk_command_pool {
	uint32_t family;
};

/*
 * One recorded command: its opcode, the objects it names (each held while
 * the record lives), and its values.  regions points at its own run of
 * VkImageCopy, VkImageBlit or VkBufferImageCopy records, push at its push
 * constant bytes.
 */
struct amdrdna_vk_record {
	uint32_t opcode;
	struct amdrdna_vk_object *objects[AMDRDNA_VK_SETS_MAX];
	uint32_t object_count;
	uint64_t offsets[AMDRDNA_VK_VERTEX_BINDINGS_MAX];
	uint32_t first;
	uint32_t count;
	uint32_t values[6];
	VkRect2D rect;
	void *regions;
	uint32_t region_count;
	uint8_t *push;
};

/*
 * A VkCommandBuffer: the pool it came from (only compared), its state, its
 * records in order (a growing array) and the first error its recording
 * met, which its End reports.
 */
struct amdrdna_vk_command_buffer {
	const void *pool;
	uint32_t state;
	struct amdrdna_vk_record *records;
	uint32_t record_count;
	uint32_t record_capacity;
	int error;
};

int amdrdna_vk_memory_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_resource_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_layout_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_target_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_pipeline_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_command_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_record_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
void amdrdna_vk_command_reset(struct amdrdna_vk_command_buffer *command);
int amdrdna_vk_queue_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int amdrdna_vk_execute(struct amdrdna_vk_session *session, struct amdrdna_vk_command_buffer *command);

uint32_t amdrdna_vk_result(int error);
uint64_t amdrdna_vk_create_tail(struct i915_wire_reader *reader);
void amdrdna_vk_create_reply(struct amdrdna_vk_session *session, struct i915_wire_writer *reply, enum i915_vk_object_kind kind, uint64_t identity, void *payload, int (*destroy)(struct amdrdna_vk_session *, void *), int error);
int amdrdna_vk_destroy_plain(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, enum i915_vk_object_kind kind);
void *amdrdna_vk_payload(struct amdrdna_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int amdrdna_vk_free_payload(struct amdrdna_vk_session *session, void *payload);

bool amdrdna_vk_format_colour(uint32_t format);
int amdrdna_vk_buffer_address(const struct amdrdna_vk_buffer *buffer, uint64_t *va, uint8_t **cpu);
int amdrdna_vk_image_address(const struct amdrdna_vk_image *image, uint64_t *va, uint8_t **cpu);

#endif /* DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_OBJECTS_H */
