/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The control device of the amdrdna driver: /dev/amdrdna0.
 *
 * The driver brings an AMD RDNA GPU up one stage at a time.  This device
 * reports how far it came and what each stage read, lets root ask for the
 * next stages, and hands out the GPU's video BIOS image.  It exists while
 * the driver is being developed; a kernel built without it brings the GPU
 * up as far as the boot line says and offers no such device.
 *
 * Every structure starts with its version and its size, holds fixed-width
 * integers only, and keeps its reserved words zero.
 */

#ifndef KERN_UAPI_AMDRDNA_H
#define KERN_UAPI_AMDRDNA_H

#include <stddef.h>
#include <stdint.h>
#include <uapi/ioctl.h>

#define KERN_AMDRDNA_IOC_GROUP		'R'

/* The version every structure of this interface carries. */
#define AMDRDNA_INTERFACE_VERSION	1U

/*
 * The stages in the order the driver runs them.  A stage reported as the
 * last one done means every earlier stage is done too.
 */
#define AMDRDNA_STAGE_NONE		0U
#define AMDRDNA_STAGE_A0		1U
#define AMDRDNA_STAGE_A1		2U
#define AMDRDNA_STAGE_A1V		3U
#define AMDRDNA_STAGE_A2		4U
#define AMDRDNA_STAGE_A3T		5U
#define AMDRDNA_STAGE_A3		6U
#define AMDRDNA_STAGE_A4		7U
#define AMDRDNA_STAGE_A5		8U
#define AMDRDNA_STAGE_A5S		9U
#define AMDRDNA_STAGE_A6		10U
#define AMDRDNA_STAGE_A7		11U
#define AMDRDNA_STAGE_A8		12U
#define AMDRDNA_STAGE_A9		13U
#define AMDRDNA_STAGE_A5C		14U
#define AMDRDNA_STAGE_A10		15U
#define AMDRDNA_STAGE_A11		16U
#define AMDRDNA_STAGE_A12		17U
#define AMDRDNA_STAGE_A13		18U
#define AMDRDNA_STAGE_COUNT		19U

/* What the status's flags word says about the GPU. */
#define AMDRDNA_STATUS_POSTED		0x00000001U
#define AMDRDNA_STATUS_SECURE_OS_ALIVE	0x00000002U
#define AMDRDNA_STATUS_PLATFORM_READY	0x00000004U
#define AMDRDNA_STATUS_RESIZABLE_BAR	0x00000008U
#define AMDRDNA_STATUS_VBIOS		0x00000010U
#define AMDRDNA_STATUS_BOOT_DATABASE	0x00000020U
#define AMDRDNA_STATUS_TRAINING_ALLOWED	0x00000040U
#define AMDRDNA_STATUS_GOP_ON_GPU	0x00000080U
#define AMDRDNA_STATUS_THREAD_RUNNING	0x00000100U

/* Where the video BIOS image came from. */
#define AMDRDNA_VBIOS_SOURCE_NONE	0U
#define AMDRDNA_VBIOS_SOURCE_ACPI	1U
#define AMDRDNA_VBIOS_SOURCE_ROM	2U

/*
 * How the firmware's surface reaches the screen: it fills the active area
 * (gop_width by gop_height) pixel for pixel, it sits unscaled inside it,
 * or the scaler stretches the fetched part (gop_source_width by
 * gop_source_height) over it.
 */
#define AMDRDNA_GOP_SHAPE_WHOLE		0U
#define AMDRDNA_GOP_SHAPE_PLACED	1U
#define AMDRDNA_GOP_SHAPE_SCALED	2U

/* The output kind the firmware lit, as the display engine names it. */
#define AMDRDNA_OUTPUT_NONE		0U
#define AMDRDNA_OUTPUT_DP		1U
#define AMDRDNA_OUTPUT_DVI		2U
#define AMDRDNA_OUTPUT_HDMI		3U
#define AMDRDNA_OUTPUT_DP_MST		5U

/*
 * The driver's state and what its stages read (AMDRDNA_IOC_STATUS).
 *
 * The caller sets version and size; the driver fills the rest.  An IP
 * version is major << 16 | minor << 8 | step.  A value a stage has not
 * read yet is zero.
 */
struct amdrdna_status {
	uint32_t version;
	uint32_t size;
	uint32_t stage;
	uint32_t target;
	uint32_t running;
	uint32_t failed_stage;
	int32_t failed_error;
	uint32_t flags;
	uint32_t pci_id;
	uint32_t pci_revision;
	uint32_t pci_subsystem;
	uint32_t graphics_version;
	uint32_t display_version;
	uint32_t security_version;
	uint32_t power_version;
	uint32_t vram_mib;
	uint32_t bar0_mib;
	uint32_t rebar_capability;
	uint32_t rebar_control;
	uint32_t power_firmware_state;
	uint32_t secure_os_word;
	uint32_t boot_mailbox;
	uint32_t engine_status;
	uint32_t bios_scratch_7;
	uint64_t frame_buffer_base;
	uint64_t gop_vram_offset;
	uint32_t gop_timing;
	uint32_t gop_plane;
	uint32_t gop_output;
	uint32_t gop_output_kind;
	uint32_t gop_width;
	uint32_t gop_height;
	uint32_t gop_pitch_word;
	uint32_t gop_format;
	uint32_t vbios_source;
	uint32_t vbios_length;
	uint32_t firmware_capability_bits;
	uint32_t firmware_table_version;
	uint32_t power_table_id;
	uint32_t shutdown_temperature;
	uint32_t cpu_window_mib;
	uint32_t gop_source_width;
	uint32_t gop_source_height;
	uint32_t gop_shape;
	uint32_t reserved[18];
};

/* Asks the driver to run every stage up to target (AMDRDNA_IOC_ADVANCE, root only). */
struct amdrdna_advance {
	uint32_t version;
	uint32_t size;
	uint32_t target;
	uint32_t reserved[5];
};

/*
 * Copies part of the video BIOS image to the caller (AMDRDNA_IOC_VBIOS).
 *
 * The driver copies at most length bytes from offset into buffer, and
 * reports the bytes copied and the image's whole length.
 */
struct amdrdna_vbios_read {
	uint32_t version;
	uint32_t size;
	uint32_t offset;
	uint32_t length;
	uint64_t buffer;
	uint32_t copied;
	uint32_t total;
	uint32_t reserved[4];
};

_Static_assert(sizeof(struct amdrdna_status) == 256U, "amdrdna status ABI size");
_Static_assert(offsetof(struct amdrdna_status, frame_buffer_base) == 96U, "amdrdna status ABI alignment");
_Static_assert(sizeof(struct amdrdna_advance) == 32U, "amdrdna advance ABI size");
_Static_assert(sizeof(struct amdrdna_vbios_read) == 48U, "amdrdna vbios ABI size");

#define AMDRDNA_IOC_STATUS	_IOWR(KERN_AMDRDNA_IOC_GROUP, 1, struct amdrdna_status)
#define AMDRDNA_IOC_ADVANCE	_IOW(KERN_AMDRDNA_IOC_GROUP, 2, struct amdrdna_advance)
#define AMDRDNA_IOC_VBIOS	_IOWR(KERN_AMDRDNA_IOC_GROUP, 3, struct amdrdna_vbios_read)

#endif
