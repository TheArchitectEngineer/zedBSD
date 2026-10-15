/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the amdrdna driver learns from the tables an RDNA GPU and its video
 * BIOS carry, and the plain functions that read those tables.
 *
 * Nothing here touches the hardware: every function takes bytes the
 * caller has already read and fills a description, so the same code runs
 * in the kernel and in the host tests.
 */

#ifndef DRIVERS_GPU_AMDRDNA_AMDRDNA_HW_H
#define DRIVERS_GPU_AMDRDNA_AMDRDNA_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most windows one IP block lists, and the most display pipes. */
#define AMDRDNA_IP_WINDOWS		6U
#define AMDRDNA_DISPLAY_PIPES		6U

/* The most reserved ranges a VRAM layout holds. */
#define AMDRDNA_LAYOUT_RANGES		8U

/*
 * The IP blocks the driver knows by name.  The GPU's block table lists
 * many more; the others are counted but not kept.
 */
enum amdrdna_block {
	AMDRDNA_BLOCK_GC,
	AMDRDNA_BLOCK_NBIO,
	AMDRDNA_BLOCK_PSP,
	AMDRDNA_BLOCK_SMU,
	AMDRDNA_BLOCK_SDMA0,
	AMDRDNA_BLOCK_SDMA1,
	AMDRDNA_BLOCK_IH,
	AMDRDNA_BLOCK_HDP,
	AMDRDNA_BLOCK_MMHUB,
	AMDRDNA_BLOCK_ATHUB,
	AMDRDNA_BLOCK_DCN,
	AMDRDNA_BLOCK_THM,
	AMDRDNA_BLOCK_SMUIO,
	AMDRDNA_BLOCK_VCN,
	AMDRDNA_BLOCK_DF,
	AMDRDNA_BLOCK_UMC,
	AMDRDNA_BLOCK_COUNT
};

/*
 * The generations of AMD GPU the driver recognizes by device ID.  Only
 * the first is driven; the others are named in the log and left alone.
 */
enum amdrdna_generation {
	AMDRDNA_GENERATION_UNKNOWN,
	AMDRDNA_GENERATION_RDNA1,
	AMDRDNA_GENERATION_RDNA2,
	AMDRDNA_GENERATION_RDNA3
};

/* The display registers the readout and the host tests name. */
enum amdrdna_dcn_register {
	AMDRDNA_DCN_HEAD_LEGACY_MODE,
	AMDRDNA_DCN_DISPLAY_CLOCK_DIVIDER,
	AMDRDNA_DCN_PIXEL_RATE_CONTROL,
	AMDRDNA_DCN_DP_CLOCK_PHASE,
	AMDRDNA_DCN_DP_CLOCK_MODULO,
	AMDRDNA_DCN_PLANE_FORMAT,
	AMDRDNA_DCN_PLANE_TILING,
	AMDRDNA_DCN_PLANE_VIEWPORT_START,
	AMDRDNA_DCN_PLANE_VIEWPORT_SIZE,
	AMDRDNA_DCN_PLANE_CONTROL,
	AMDRDNA_DCN_PLANE_CLOCK,
	AMDRDNA_DCN_PLANE_PITCH,
	AMDRDNA_DCN_PLANE_ADDRESS_LOW,
	AMDRDNA_DCN_PLANE_ADDRESS_HIGH,
	AMDRDNA_DCN_PLANE_SURFACE_CONTROL,
	AMDRDNA_DCN_PLANE_FLIP_CONTROL,
	AMDRDNA_DCN_PLANE_COLOR_ROUTE,
	AMDRDNA_DCN_PIXEL_PROCESSOR_CONTROL,
	AMDRDNA_DCN_BLEND_TOP,
	AMDRDNA_DCN_BLEND_BOTTOM,
	AMDRDNA_DCN_BLEND_OUTPUT,
	AMDRDNA_DCN_BLEND_CONTROL,
	AMDRDNA_DCN_BLEND_TREE_ROOT,
	AMDRDNA_DCN_COMBINER_SOURCE,
	AMDRDNA_DCN_COMBINER_INPUT,
	AMDRDNA_DCN_TIMING_H_TOTAL,
	AMDRDNA_DCN_TIMING_H_BLANK,
	AMDRDNA_DCN_TIMING_H_SYNC,
	AMDRDNA_DCN_TIMING_H_SYNC_CONTROL,
	AMDRDNA_DCN_TIMING_V_TOTAL,
	AMDRDNA_DCN_TIMING_V_BLANK,
	AMDRDNA_DCN_TIMING_V_SYNC,
	AMDRDNA_DCN_TIMING_V_SYNC_CONTROL,
	AMDRDNA_DCN_TIMING_CONTROL,
	AMDRDNA_DCN_TIMING_UPDATE_LOCK,
	AMDRDNA_DCN_ENCODER_FRONT_CONTROL,
	AMDRDNA_DCN_ENCODER_BACK_CONTROL,
	AMDRDNA_DCN_ENCODER_BACK_ENABLE,
	AMDRDNA_DCN_DP_STREAM_CONTROL,
	AMDRDNA_DCN_DP_LINK_CONTROL,
	AMDRDNA_DCN_SCALER_MODE,
	AMDRDNA_DCN_SCALER_H_RATIO,
	AMDRDNA_DCN_SCALER_V_RATIO,
	AMDRDNA_DCN_SCALER_OUTPUT_START,
	AMDRDNA_DCN_SCALER_OUTPUT_SIZE,
	AMDRDNA_DCN_SCALER_AUTOMATIC,
	AMDRDNA_DCN_REGISTER_COUNT
};

/*
 * How the firmware's surface reaches the screen: it fills the timing's
 * active area pixel for pixel, it sits unscaled inside a larger active
 * area, or the pixel processor's scaler stretches it.  The order is the
 * one of the AMDRDNA_GOP_SHAPE_ values of the control interface.
 */
enum amdrdna_picture_shape {
	AMDRDNA_PICTURE_WHOLE,
	AMDRDNA_PICTURE_PLACED,
	AMDRDNA_PICTURE_SCALED
};

/*
 * One IP block as the block table lists it: its version and where its
 * register windows start (dword indices).  found is zero for a block the
 * table did not list.
 */
struct amdrdna_ip {
	uint8_t found;
	uint8_t major;
	uint8_t minor;
	uint8_t step;
	uint8_t window_count;
	uint32_t window[AMDRDNA_IP_WINDOWS];
};

/*
 * The shape of the graphics engine as the block table states it: the
 * shader engines, the arrays in each, the paired compute units in each
 * array, the render back ends and the level-2 cache channels.
 */
struct amdrdna_shader_shape {
	uint32_t engines;
	uint32_t arrays_per_engine;
	uint32_t pairs_per_array;
	uint32_t backends_per_engine;
	uint32_t cache_channels;
	uint32_t wave_lanes;
	uint32_t waves_per_simd;
};

/*
 * What the GPU's own block table says: its layout version, the blocks the
 * driver knows, how many entries it listed in all, and the shader shape
 * when the table carries one.
 */
struct amdrdna_block_table {
	uint16_t layout_major;
	uint16_t layout_minor;
	uint16_t list_layout;
	unsigned listed;
	struct amdrdna_ip blocks[AMDRDNA_BLOCK_COUNT];
	bool has_shape;
	struct amdrdna_shader_shape shape;
};

/*
 * The boot database the security processor may leave at the top of VRAM:
 * whether it is there, its version, how many entries it lists, and the
 * boot configuration bits when one entry carries them.  config_offset is
 * where that entry lies from the database's start, for a caller that has
 * to read it separately.
 */
struct amdrdna_boot_database {
	bool present;
	uint16_t version;
	uint16_t entries;
	bool has_config;
	uint32_t config_offset;
	bool config_read;
	uint32_t config_bits;
};

/*
 * One command routine of the video BIOS: where it starts in the image,
 * and the workspace and parameter bytes it takes.
 */
struct amdrdna_vbios_routine {
	uint16_t offset;
	uint16_t work_bytes;
	uint16_t parameter_bytes;
};

/* The command routines the driver names in its log and later runs. */
enum amdrdna_vbios_command {
	AMDRDNA_VBIOS_COMMAND_POST,
	AMDRDNA_VBIOS_COMMAND_ENCODER,
	AMDRDNA_VBIOS_COMMAND_PIXEL_CLOCK,
	AMDRDNA_VBIOS_COMMAND_PIPE_POWER,
	AMDRDNA_VBIOS_COMMAND_TIMING_ENABLE,
	AMDRDNA_VBIOS_COMMAND_DISPLAY_CLOCK,
	AMDRDNA_VBIOS_COMMAND_TRANSMITTER,
	AMDRDNA_VBIOS_COMMAND_AUX,
	AMDRDNA_VBIOS_COMMAND_COUNT
};

/*
 * What the video BIOS image says that the driver uses: where its lists of
 * data tables and command routines are, the firmware description, the VRAM
 * the firmware keeps for itself, the power table's header and the board's
 * power-delivery table version, and the command routines named above.
 *
 * A field whose table the image lacks is zero.
 */
struct amdrdna_vbios_info {
	uint32_t length;
	uint16_t header_offset;
	uint16_t data_list;
	uint16_t command_list;
	uint16_t firmware_table;
	uint8_t firmware_format;
	uint8_t firmware_content;
	uint32_t capability_bits;
	uint32_t boot_engine_clock_10khz;
	uint32_t boot_memory_clock_10khz;
	uint32_t scratch_first;
	uint32_t power_table_id;
	uint32_t reserved_top_kib;
	uint16_t reserve_table;
	uint8_t reserve_format;
	uint8_t reserve_content;
	uint32_t firmware_start_kib;
	uint32_t firmware_kib;
	uint32_t driver_start_kib;
	uint32_t driver_kib;
	uint16_t power_table;
	uint8_t power_format;
	uint8_t power_content;
	uint8_t power_revision;
	uint16_t power_bytes;
	uint16_t shutdown_temperature;
	uint16_t board_table;
	uint8_t board_format;
	uint8_t board_content;
	struct amdrdna_vbios_routine commands[AMDRDNA_VBIOS_COMMAND_COUNT];
};

/* The PCI place and identity a video BIOS image in the ACPI table names. */
struct amdrdna_vbios_owner {
	uint32_t bus;
	uint32_t device;
	uint32_t function;
	uint16_t vendor;
	uint16_t product;
};

/*
 * What the display chain and the loader say about the firmware's picture.
 *
 * The timing generator gives the active area the screen shows; the plane
 * gives the viewport, the part of the surface it fetches; the pixel
 * processor's scaler gives the rectangle of the active area the viewport
 * is drawn into, its mode (0 passes the pixels through), its two ratios,
 * the viewport's size over the rectangle's in units of 1/2^24, and its
 * automatic mode (0 off; 1 the scaler works the ratios out itself; 2 and 3
 * it centres the viewport unscaled, 3 also repeating pixels); the loader
 * gives the size of the framebuffer it handed the kernel.
 */
struct amdrdna_picture_input {
	uint32_t active_width;
	uint32_t active_height;
	uint32_t viewport_x;
	uint32_t viewport_y;
	uint32_t viewport_width;
	uint32_t viewport_height;
	uint32_t scaler_mode;
	uint32_t scaler_x;
	uint32_t scaler_y;
	uint32_t scaler_width;
	uint32_t scaler_height;
	uint32_t horizontal_ratio;
	uint32_t vertical_ratio;
	uint32_t scaler_automatic;
	bool loader_present;
	uint32_t loader_width;
	uint32_t loader_height;
};

/*
 * What the check found: how the surface reaches the screen, whether the
 * scaler's rectangle lies inside the active area, and whether its mode and
 * ratios agree with the viewport and the rectangle.  The last two are only
 * noted; they do not decide whether the picture is the console's.
 */
struct amdrdna_picture {
	enum amdrdna_picture_shape shape;
	bool rectangle_inside;
	bool ratios_agree;
};

/*
 * What the VRAM layout is planned from: the VRAM's size, the part of it the
 * CPU window shows, the firmware's picture, and the ranges the video BIOS
 * keeps for the firmware and the driver.
 */
struct amdrdna_layout_input {
	uint64_t vram_bytes;
	uint64_t window_bytes;
	bool gop_present;
	uint64_t gop_offset;
	uint64_t gop_bytes;
	uint64_t firmware_start;
	uint64_t firmware_bytes;
	uint64_t driver_start;
	uint64_t driver_bytes;
};

/* Why a VRAM range is kept out of the allocator. */
enum amdrdna_range_reason {
	AMDRDNA_RANGE_TOP,
	AMDRDNA_RANGE_GOP,
	AMDRDNA_RANGE_FIRMWARE,
	AMDRDNA_RANGE_DRIVER,
	AMDRDNA_RANGE_TRAINING
};

/* One VRAM range kept out of the allocator, [start, end). */
struct amdrdna_range {
	uint64_t start;
	uint64_t end;
	enum amdrdna_range_reason reason;
};

/*
 * The planned VRAM layout: the ranges no allocation may take, the pool
 * the CPU can reach through its window, and the pool only the GPU reaches.
 * The training range is held only until memory training has run or has
 * been skipped.
 */
struct amdrdna_layout {
	struct amdrdna_range reserved[AMDRDNA_LAYOUT_RANGES];
	unsigned reserved_count;
	uint64_t cpu_pool_start;
	uint64_t cpu_pool_end;
	uint64_t gpu_pool_start;
	uint64_t gpu_pool_end;
};

/* One known AMD GPU device ID and what it is. */
struct amdrdna_product {
	uint16_t device;
	enum amdrdna_generation generation;
	const char *chip;
};

uint16_t
amdrdna_le16(
	const uint8_t *bytes,
	size_t at);

uint32_t
amdrdna_le32(
	const uint8_t *bytes,
	size_t at);

int
amdrdna_block_table_parse(
	const uint8_t *bytes,
	size_t length,
	struct amdrdna_block_table *table);

int
amdrdna_boot_database_parse(
	const uint8_t *bytes,
	size_t length,
	struct amdrdna_boot_database *database);

void
amdrdna_boot_database_set_config(
	struct amdrdna_boot_database *database,
	const uint8_t *entry);

int
amdrdna_vbios_find_in_acpi(
	const uint8_t *table,
	size_t length,
	const struct amdrdna_vbios_owner *owner,
	size_t *image_offset,
	size_t *image_length);

int
amdrdna_vbios_check(
	const uint8_t *image,
	size_t length);

uint32_t
amdrdna_vbios_declared_length(
	const uint8_t *image);

int
amdrdna_vbios_parse(
	const uint8_t *image,
	size_t length,
	struct amdrdna_vbios_info *info);

int
amdrdna_picture_check(
	const struct amdrdna_picture_input *input,
	struct amdrdna_picture *picture);

const char *
amdrdna_picture_shape_name(
	enum amdrdna_picture_shape shape);

int
amdrdna_layout_plan(
	const struct amdrdna_layout_input *input,
	struct amdrdna_layout *layout);

const char *
amdrdna_range_reason_name(
	enum amdrdna_range_reason reason);

const struct amdrdna_product *
amdrdna_product_find(
	uint16_t device);

const char *
amdrdna_generation_name(
	enum amdrdna_generation generation);

const char *
amdrdna_graphics_chip_name(
	const struct amdrdna_ip *graphics);

uint32_t
amdrdna_dcn_index(
	const struct amdrdna_ip *display,
	enum amdrdna_dcn_register which,
	unsigned instance);

const char *
amdrdna_block_name(
	enum amdrdna_block block);

#endif
