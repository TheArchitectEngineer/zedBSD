/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private DMA ownership.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * pcie/iwl-context-info-v2.h and iwl-fh.h.  They were cross-checked against
 * OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at commit 0f464d413c50.  Each
 * block of values below names the header it was taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-dma.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

/*
 * The layout of the scratch area, and the boot context's fixed fields.
 *
 * Source: Linux iwlwifi v6.17 pcie/iwl-context-info-v2.h (struct
 * iwl_prph_scratch and enum iwl_prph_scratch_flags), cross-checked with
 * OpenBSD if_iwxreg.h (struct iwx_prph_scratch).  ax211_scratch_build()
 * writes, from byte 0: the silicon revision as read from the device, a zero
 * layout version, the size of the area in 32-bit words and, at byte 8, the
 * control flags.  The flags used are 0x000f0000: receive buffers of 4 KiB
 * (bit 16), completions in the descriptor form (bit 17), and transfer
 * descriptors of 256 bytes (bits 19:18 set to 3), the forms the transport
 * implements.  Byte 16 holds the address of the PNVM address table and
 * byte 24 the total length of the copied PNVM sections, both written only
 * once the firmware has named its board.  Byte 48 holds the address of the
 * receive transfer ring.  From byte 124 the area holds three lists of 64
 * section addresses (64-bit, 512 bytes each): the UMAC sections, then the
 * LMAC sections, then the paged sections.  The firmware copies its code
 * from those addresses; a boot with an empty group is refused.
 */
#define AX211_SCRATCH_CONTROL_FLAGS 0x000f0000U
#define AX211_SCRATCH_PNVM_BASE_OFFSET 16U
#define AX211_SCRATCH_PNVM_SIZE_OFFSET 24U
#define AX211_SCRATCH_RBD_BASE_OFFSET 48U
#define AX211_SCRATCH_DRAM_OFFSET 124U
#define AX211_SCRATCH_UMAC_OFFSET AX211_SCRATCH_DRAM_OFFSET
#define AX211_SCRATCH_LMAC_OFFSET (AX211_SCRATCH_UMAC_OFFSET + 512U)
#define AX211_SCRATCH_PAGING_OFFSET (AX211_SCRATCH_LMAC_OFFSET + 512U)
#define AX211_BOOT_CONTEXT_VERSION 0U
#define AX211_BOOT_CONTEXT_CONFIG 0U

#ifdef INTEL_AX211_DMA_HOST_TEST
#define AX211_DMA_SCRUB intel_ax211_dma_host_scrub
#else
#define AX211_DMA_SCRUB drv_intel_ax211_scrub
#endif

static int ax211_boot_manifest_validate( const uint8_t *firmware_bytes, size_t firmware_length, const struct intel_ax211_firmware_manifest *manifest);
static int ax211_range_valid(size_t offset, size_t length, size_t capacity);
static int ax211_fixed_buffers_allocate(struct intel_ax211_dma_resources *resources);
static int ax211_buffer_allocate(struct intel_ax211_dma_resources *resources, struct drv_dma_buffer *buffer, size_t size, size_t alignment);
static int ax211_firmware_buffers_allocate( struct intel_ax211_dma_resources *resources, const uint8_t *bytes, size_t length, const struct intel_ax211_firmware_manifest *manifest);
static int ax211_rx_buffers_allocate(struct intel_ax211_dma_resources *resources);
static int ax211_scratch_build(struct intel_ax211_dma_resources *resources, uint16_t hardware_revision);
static void ax211_put_le16(uint8_t *output, uint16_t value);
static void ax211_put_le32(uint8_t *output, uint32_t value);
static void ax211_put_le64(uint8_t *output, uint64_t value);
static int ax211_context_build(struct intel_ax211_dma_resources *resources);
static void ax211_buffer_release(struct intel_ax211_dma_resources *resources, struct drv_dma_buffer *buffer);

/*
 * Implements the drv intel ax211 dma prepare boot operation.
 */
int
drv_intel_ax211_dma_prepare_boot(
	struct drv_dma_device *device,
	const uint8_t *firmware_bytes,
	size_t firmware_length,
	const struct intel_ax211_firmware_manifest *manifest,
	uint16_t hardware_revision,
	struct intel_ax211_dma_resources *resources)
{
	int error;

	/* Handles the device availability. */
	if (device == NULL || resources == NULL || firmware_length == 0U ||
	    resources->device != NULL || resources->boot_prepared) {
		/* Failed. */
		return EINVAL;
	}

	/* Checks the operation status. */
	error = ax211_boot_manifest_validate(firmware_bytes, firmware_length,
					     manifest);
	if (error != 0 ||
	    !drv_intel_ax211_mac_type_supported(
		    (uint16_t)((hardware_revision & 0xfff0U) >> 4))) {
		/* Returns the computed result. */
		return error != 0 ? error : EINVAL;
	}
	resources->device = device;

	/* Checks the operation status. */
	error = ax211_fixed_buffers_allocate(resources);
	if (error == 0) {
		error = ax211_firmware_buffers_allocate(
			resources, firmware_bytes, firmware_length, manifest);
	}

	/* Checks the operation status. */
	if (error == 0)
		error = ax211_rx_buffers_allocate(resources);
	if (error == 0)
		error = ax211_scratch_build(resources, hardware_revision);
	if (error == 0)
		error = ax211_context_build(resources);
	if (error != 0) {
		drv_intel_ax211_dma_release(resources);

		/* Failed. */
		return error;
	}

	resources->boot_prepared = 1U;

	/* Succeeded. */
	return 0;
}

/*
 * Implements the drv intel ax211 dma prepare pnvm operation.
 */
int
drv_intel_ax211_dma_prepare_pnvm(
	const uint8_t *pnvm_bytes,
	size_t pnvm_length,
	const struct intel_ax211_pnvm_manifest *manifest,
	struct intel_ax211_dma_resources *resources)
{
	const struct intel_ax211_section *section;
	uint8_t *table;
	size_t index;
	int error = 0;

	/* Handles the pnvm bytes availability. */
	if (pnvm_bytes == NULL || manifest == NULL || resources == NULL ||
	    !resources->boot_prepared || !resources->boot_images_released ||
	    resources->pnvm_prepared || pnvm_length != INTEL_AX211_PNVM_FILE_SIZE ||
	    manifest->section_count == 0U ||
	    manifest->section_count > INTEL_AX211_PNVM_SECTION_LIMIT ||
	    manifest->total_length == 0U || manifest->total_length > UINT32_MAX) {
		/* Failed. */
		return EINVAL;
	}
	error = ax211_buffer_allocate(resources, &resources->pnvm_table,
				      INTEL_AX211_PNVM_ADDRESS_TABLE_SIZE, 1U);
	/* Process each remaining element. */
	for (index = 0U; index < manifest->section_count && error == 0;
	     index++) {
		/* Checks the ax211 range valid result. */
		section = &manifest->section[index];
		if (section->length == 0U ||
		    !ax211_range_valid(section->file_offset, section->length,
				       pnvm_length) ||
		    resources->pnvm_total_length > SIZE_MAX - section->length) {
			error = EINVAL;
			continue;
		}

		/* Checks the operation status. */
		error = ax211_buffer_allocate(resources,
					      &resources->pnvm[index],
					      section->length, 1U);
		if (error == 0) {
			kern_memcpy(resources->pnvm[index].address,
			       pnvm_bytes + section->file_offset,
			       section->length);
			resources->pnvm_total_length += section->length;
			resources->pnvm_count++;
		}
	}

	/* Checks the operation status. */
	if (error == 0 &&
	    resources->pnvm_total_length != manifest->total_length)
		error = EINVAL;
	if (error != 0) {
		/* Process each remaining element. */
		while (resources->pnvm_count != 0U) {
			resources->pnvm_count--;
			ax211_buffer_release(
				resources,
				&resources->pnvm[resources->pnvm_count]);
		}

		resources->pnvm_total_length = 0U;
		ax211_buffer_release(resources, &resources->pnvm_table);

		/* Failed. */
		return error;
	}

	table = resources->pnvm_table.address;
	/* Process each remaining element. */
	for (index = 0U; index < resources->pnvm_count; index++) {
		ax211_put_le64(table + index * 8U,
			       resources->pnvm[index].device_address);
	}

	ax211_put_le64((uint8_t *)resources->scratch.address +
			       AX211_SCRATCH_PNVM_BASE_OFFSET,
		       resources->pnvm_table.device_address);
	ax211_put_le32((uint8_t *)resources->scratch.address +
			       AX211_SCRATCH_PNVM_SIZE_OFFSET,
		       (uint32_t)resources->pnvm_total_length);
	resources->pnvm_prepared = 1U;

	/* Succeeded. */
	return 0;
}

/*
 * Implements the drv intel ax211 dma release boot images operation.
 */
void
drv_intel_ax211_dma_release_boot_images(
	struct intel_ax211_dma_resources *resources)
{
	size_t count;

	/* Handles the resources availability. */
	if (resources == NULL || resources->device == NULL ||
	    !resources->boot_prepared || resources->boot_images_released) {
		/* Returns the computed result. */
		return;
	}
	count = resources->firmware_count;
	/* Process each remaining element. */
	while (count != 0U) {
		count--;

		/* Handles the resources condition. */
		if (resources->firmware[count].image_class !=
		    INTEL_AX211_DMA_IMAGE_PAGING) {
			ax211_buffer_release(
				resources, &resources->firmware[count].buffer);
		}
	}

	ax211_buffer_release(resources, &resources->iml);
	resources->boot_images_released = 1U;
}

/*
 * Implements the drv intel ax211 dma release operation.
 */
void
drv_intel_ax211_dma_release(
	struct intel_ax211_dma_resources *resources)
{
	size_t count;

	/* Handles the resources availability. */
	if (resources == NULL)
		return;

	/* Handles the device availability. */
	if (resources->device == NULL) {
		kern_memset(resources, 0, sizeof(*resources));

		/* Returns the computed result. */
		return;
	}

	count = resources->pnvm_count;
	/* Process each remaining element. */
	while (count != 0U) {
		count--;
		ax211_buffer_release(resources, &resources->pnvm[count]);
	}

	ax211_buffer_release(resources, &resources->pnvm_table);
	count = resources->rx_buffer_count;
	/* Process each remaining element. */
	while (count != 0U) {
		count--;
		ax211_buffer_release(resources, &resources->rx_buffer[count]);
	}

	count = resources->firmware_count;
	/* Process each remaining element. */
	while (count != 0U) {
		count--;
		ax211_buffer_release(resources,
				     &resources->firmware[count].buffer);
	}

	ax211_buffer_release(resources, &resources->iml);
	ax211_buffer_release(resources, &resources->rx_status);
	ax211_buffer_release(resources, &resources->rx_completion);
	ax211_buffer_release(resources, &resources->rx_transfer);
	ax211_buffer_release(resources, &resources->command_external);
	ax211_buffer_release(resources, &resources->command_slots);
	ax211_buffer_release(resources, &resources->command_byte_count);
	ax211_buffer_release(resources, &resources->command_tfd);
	ax211_buffer_release(resources, &resources->ict);
	ax211_buffer_release(resources, &resources->status_page);
	ax211_buffer_release(resources, &resources->scratch);
	ax211_buffer_release(resources, &resources->context);
	kern_memset(resources, 0, sizeof(*resources));
}

/* Supports the ax211 boot manifest validate operation. */
static int
ax211_boot_manifest_validate(
	const uint8_t *firmware_bytes,
	size_t firmware_length,
	const struct intel_ax211_firmware_manifest *manifest)
{
	const struct intel_ax211_section *section;
	size_t cpu_separator;
	size_t paging_separator;
	size_t index;

	/* Checks the ax211 range valid result. */
	if (firmware_bytes == NULL || manifest == NULL ||
	    firmware_length != INTEL_AX211_FIRMWARE_SIZE ||
	    manifest->runtime_count > INTEL_AX211_FIRMWARE_SECTION_LIMIT ||
	    manifest->lmac_count == 0U || manifest->lmac_count > 64U ||
	    manifest->umac_count == 0U || manifest->umac_count > 64U ||
	    manifest->paging_count == 0U || manifest->paging_count > 64U ||
	    manifest->lmac_count + manifest->umac_count +
			    manifest->paging_count + 2U !=
		    manifest->runtime_count ||
	    manifest->iml_length != INTEL_AX211_LOADER_IMAGE_SIZE ||
	    !ax211_range_valid(manifest->iml_offset, manifest->iml_length,
			       firmware_length)) {
		/* Failed. */
		return EINVAL;
	}
	cpu_separator = manifest->lmac_count;
	paging_separator = cpu_separator + manifest->umac_count + 1U;
	/* Process each remaining element. */
	for (index = 0U; index < manifest->runtime_count; index++) {
		/* Checks the current index. */
		section = &manifest->runtime[index];
		if (index == cpu_separator) {
			/* Handles the section condition. */
			if (section->destination !=
				    INTEL_AX211_UPPER_CPU_IMAGE_MARK ||
			    section->length != 0U) {
				/* Failed. */
				return EINVAL;
			}
		} else if (index == paging_separator) {
			/* Handles the section condition. */
			if (section->destination !=
				    INTEL_AX211_PAGED_IMAGE_MARK ||
			    section->length != 0U) {
				/* Failed. */
				return EINVAL;
			}
		} else if (section->destination ==
				   INTEL_AX211_UPPER_CPU_IMAGE_MARK ||
			   section->destination ==
				   INTEL_AX211_PAGED_IMAGE_MARK ||
			   section->length == 0U ||
			   section->length >
				   INTEL_AX211_FIRMWARE_SECTION_SIZE_MAX ||
			   !ax211_range_valid(section->file_offset,
					      section->length,
					      firmware_length)) {
			/* Failed. */
			return EINVAL;
		}
	}

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 range valid operation. */
static int
ax211_range_valid(
	size_t offset,
	size_t length,
	size_t capacity)
{
	/* Returns the computed result. */
	return offset <= capacity && length <= capacity - offset;
}

/* Supports the ax211 fixed buffers allocate operation. */
static int
ax211_fixed_buffers_allocate(
	struct intel_ax211_dma_resources *resources)
{
	int error;

	/* Checks the operation status. */
	error = ax211_buffer_allocate(resources, &resources->context,
				      INTEL_AX211_BOOT_CONTEXT_SIZE, 1U);
	if (error == 0) {
		error = ax211_buffer_allocate(resources, &resources->scratch,
					      INTEL_AX211_SCRATCH_AREA_SIZE,
					      1U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(resources, &resources->status_page,
					      INTEL_AX211_STATUS_PAGE_SIZE, 1U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(resources, &resources->ict,
					      INTEL_AX211_INTERRUPT_TABLE_SIZE,
					      INTEL_AX211_INTERRUPT_TABLE_SIZE);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->command_tfd,
			INTEL_AX211_COMMAND_TFD_RING_SIZE, 256U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->command_byte_count,
			INTEL_AX211_COMMAND_LENGTH_TABLE_SIZE, 128U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->command_slots,
			INTEL_AX211_COMMAND_SLOTS_SIZE, 64U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->command_external,
			INTEL_AX211_COMMAND_EXTERNAL_SIZE, 64U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->rx_transfer,
			INTEL_AX211_RX_TRANSFER_RING_SIZE, 256U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(
			resources, &resources->rx_completion,
			INTEL_AX211_RX_COMPLETION_RING_SIZE, 256U);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = ax211_buffer_allocate(resources, &resources->rx_status,
					      INTEL_AX211_RX_STATUS_SIZE, 16U);
	}

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 buffer allocate operation. */
static int
ax211_buffer_allocate(
	struct intel_ax211_dma_resources *resources,
	struct drv_dma_buffer *buffer,
	size_t size,
	size_t alignment)
{
	int error;

	/* Checks the operation status. */
	error = drv_dma_alloc_coherent(resources->device, size, alignment,
				       buffer);
	if (error != 0)
		return error;

	/* Handles the address availability. */
	if (buffer->address == NULL || buffer->size != size ||
	    buffer->device_address > UINT64_MAX - (uint64_t)(size - 1U) ||
	    (alignment > 1U &&
	     (buffer->device_address & (uint64_t)(alignment - 1U)) != 0U)) {
		/* Handles the address availability. */
		if (buffer->address != NULL && buffer->size != 0U)
			AX211_DMA_SCRUB(buffer->address, buffer->size);
		drv_dma_free_coherent(resources->device, buffer);

		/* Failed. */
		return EIO;
	}

	kern_memset(buffer->address, 0, buffer->size);

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 firmware buffers allocate operation. */
static int
ax211_firmware_buffers_allocate(
	struct intel_ax211_dma_resources *resources,
	const uint8_t *bytes,
	size_t length,
	const struct intel_ax211_firmware_manifest *manifest)
{
	const struct intel_ax211_section *section;
	struct intel_ax211_dma_image *image;
	size_t index;
	unsigned image_class = INTEL_AX211_DMA_IMAGE_LMAC;
	int error = 0;

	/* Checks the ax211 range valid result. */
	if (!ax211_range_valid(manifest->iml_offset, manifest->iml_length,
			       length) ||
	    manifest->iml_length != INTEL_AX211_LOADER_IMAGE_SIZE) {
		/* Failed. */
		return EINVAL;
	}

	/* Checks the operation status. */
	error = ax211_buffer_allocate(resources, &resources->iml,
				      manifest->iml_length, 1U);
	if (error != 0)
		return error;
	kern_memcpy(resources->iml.address, bytes + manifest->iml_offset,
	       manifest->iml_length);
	/* Process each remaining element. */
	for (index = 0U; index < manifest->runtime_count && error == 0;
	     index++) {
		/* Handles the section condition. */
		section = &manifest->runtime[index];
		if (section->destination == INTEL_AX211_UPPER_CPU_IMAGE_MARK) {
			image_class = INTEL_AX211_DMA_IMAGE_UMAC;
			continue;
		}

		/* Handles the section condition. */
		if (section->destination == INTEL_AX211_PAGED_IMAGE_MARK) {
			image_class = INTEL_AX211_DMA_IMAGE_PAGING;
			continue;
		}

		/* Checks the ax211 range valid result. */
		if (section->length == 0U ||
		    section->length > INTEL_AX211_FIRMWARE_SECTION_SIZE_MAX ||
		    !ax211_range_valid(section->file_offset, section->length,
				       length) ||
		    resources->firmware_count >= INTEL_AX211_FIRMWARE_SECTION_LIMIT) {
			error = EINVAL;
			continue;
		}

		image = &resources->firmware[resources->firmware_count];

		/* Checks the operation status. */
		error = ax211_buffer_allocate(resources, &image->buffer,
					      section->length, 1U);
		if (error == 0) {
			kern_memcpy(image->buffer.address,
			       bytes + section->file_offset, section->length);
			image->destination = section->destination;
			image->image_class = (uint8_t)image_class;
			resources->firmware_count++;
		}
	}

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 rx buffers allocate operation. */
static int
ax211_rx_buffers_allocate(
	struct intel_ax211_dma_resources *resources)
{
	uint8_t *descriptor;
	int result;
	size_t index;
	int error = 0;

	/* Process each remaining element. */
	for (index = 0U; index < INTEL_AX211_RX_RING_SIZE && error == 0;
	     index++) {
		/* Checks the operation status. */
		error = ax211_buffer_allocate(
			resources, &resources->rx_buffer[index],
			INTEL_AX211_RX_BUFFER_SIZE, INTEL_AX211_RX_BUFFER_SIZE);
		if (error == 0) {
			resources->rx_buffer_count++;
			descriptor =
				(uint8_t *)resources->rx_transfer.address +
				index * INTEL_AX211_RX_TRANSFER_DESCRIPTOR_SIZE;

			/* Checks the operation result. */
			result = drv_intel_ax211_rx_transfer_descriptor_encode(
				descriptor, (uint16_t)index,
				resources->rx_buffer[index].device_address);
			if (result != INTEL_AX211_SUCCESS)
				error = EINVAL;
		}
	}

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 scratch build operation. */
static int
ax211_scratch_build(
	struct intel_ax211_dma_resources *resources,
	uint16_t hardware_revision)
{
	const struct intel_ax211_dma_image *image;
	size_t offset;
	uint8_t *scratch = resources->scratch.address;
	size_t index;
	size_t lmac = 0U;
	size_t umac = 0U;
	size_t paging = 0U;

	/* Handles the scratch availability. */
	if (scratch == NULL)
		return EINVAL;
	kern_memset(scratch, 0, resources->scratch.size);
	ax211_put_le16(scratch, hardware_revision);
	ax211_put_le16(scratch + 2U, 0U);
	ax211_put_le16(scratch + 4U, INTEL_AX211_SCRATCH_AREA_SIZE / 4U);
	ax211_put_le32(scratch + 8U, AX211_SCRATCH_CONTROL_FLAGS);
	ax211_put_le64(scratch + AX211_SCRATCH_RBD_BASE_OFFSET,
		       resources->rx_transfer.device_address);
	/* Process each remaining element. */
	for (index = 0U; index < resources->firmware_count; index++) {
		/* Handles the image condition. */
		image = &resources->firmware[index];
		if (image->image_class == INTEL_AX211_DMA_IMAGE_LMAC) {
			/* Handles the lmac condition. */
			if (lmac >= 64U)
				return EOVERFLOW;
			offset = AX211_SCRATCH_LMAC_OFFSET + lmac++ * 8U;
		} else if (image->image_class == INTEL_AX211_DMA_IMAGE_UMAC) {
			/* Handles the umac condition. */
			if (umac >= 64U)
				return EOVERFLOW;
			offset = AX211_SCRATCH_UMAC_OFFSET + umac++ * 8U;
		} else if (image->image_class == INTEL_AX211_DMA_IMAGE_PAGING) {
			/* Handles the paging condition. */
			if (paging >= 64U)
				return EOVERFLOW;
			offset = AX211_SCRATCH_PAGING_OFFSET + paging++ * 8U;
		} else {
			/* Failed. */
			return EINVAL;
		}

		ax211_put_le64(scratch + offset, image->buffer.device_address);
	}

	/* Returns the computed result. */
	return lmac == 0U || umac == 0U || paging == 0U ? EINVAL : 0;
}

/* Supports the ax211 put le16 operation. */
static void
ax211_put_le16(
	uint8_t *output,
	uint16_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
}

/* Supports the ax211 put le32 operation. */
static void
ax211_put_le32(
	uint8_t *output,
	uint32_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
	output[2] = (uint8_t)(value >> 16);
	output[3] = (uint8_t)(value >> 24);
}

/* Supports the ax211 put le64 operation. */
static void
ax211_put_le64(
	uint8_t *output,
	uint64_t value)
{
	ax211_put_le32(output, (uint32_t)value);
	ax211_put_le32(output + 4U, (uint32_t)(value >> 32));
}

/* Supports the ax211 context build operation. */
static int
ax211_context_build(
	struct intel_ax211_dma_resources *resources)
{
	struct intel_ax211_boot_context context;
	int result;

	/* Describes every ring and scratch area to the firmware. */
	kern_memset(&context, 0, sizeof(context));
	context.version = AX211_BOOT_CONTEXT_VERSION;
	context.config = AX211_BOOT_CONTEXT_CONFIG;
	context.status_page_address = resources->status_page.device_address;
	context.completion_head_array_address = resources->rx_status.device_address;
	context.transfer_tail_array_address =
		resources->status_page.device_address + 2048U;
	context.completion_tail_array_address =
		resources->status_page.device_address + 3072U;
	context.command_ring_address =
		resources->command_tfd.device_address;
	context.response_ring_address =
		resources->rx_completion.device_address;
	context.command_ring_size_code = INTEL_AX211_COMMAND_RING_CB_SIZE;
	context.response_ring_size_code = INTEL_AX211_RX_RING_CB_SIZE;
	context.scratch_area_address = resources->scratch.device_address;
	context.scratch_area_size = INTEL_AX211_SCRATCH_AREA_SIZE;
	result = drv_intel_ax211_boot_context_encode(
		resources->context.address, &context);

	/* Returns the computed result. */
	return result == INTEL_AX211_SUCCESS ? 0 : EINVAL;
}

/* Supports the ax211 buffer release operation. */
static void
ax211_buffer_release(
	struct intel_ax211_dma_resources *resources,
	struct drv_dma_buffer *buffer)
{
	/* Handles the address availability. */
	if (buffer->address != NULL) {
		AX211_DMA_SCRUB(buffer->address, buffer->size);
		drv_dma_free_coherent(resources->device, buffer);
	}

	kern_memset(buffer, 0, sizeof(*buffer));
}
