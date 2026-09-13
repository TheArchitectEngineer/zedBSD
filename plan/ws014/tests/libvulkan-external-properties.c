/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Independent wire peer for standard external allocation capability profiles. */
#include "internal.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/gpu-allocation.h>

/* Holds the single wire context used by this deterministic peer. */
static struct vulkan_context context;

/* Identifies the physical device whose native query fields are independently checked. */
static struct VkPhysicalDevice_T physical;

/* Selects the native allocation feature bits returned by the peer. */
static uint32_t native_features;

/* Selects the native external handle types returned by the peer. */
static uint32_t native_compatible;

/* The independent peer checks the native type negotiated for this scenario. */
static uint32_t expected_native_type = 0x200U;

/* Corrupts one structure tag to exercise terminal protocol failure. */
static unsigned bad_header;

/* Truncates the advertised UUID length without changing the fixed backing bytes. */
static unsigned bad_uuid_length;

/* Counts native queries so rejected guest profiles cannot silently reach the backend. */
static unsigned calls;

/* Tracks the next byte written to the independent response buffer. */
static unsigned cursor;

/* Stores one bounded native reply before the library takes its own snapshot. */
static uint8_t output[256];

static uint32_t get32(const uint8_t *p);
static uint64_t get64(const uint8_t *p);
static void put32(uint32_t value);
static void put64(uint64_t value);
static void put_header(uint32_t type, unsigned next);

/* Core properties decoding has a separately tested full codec; this peer isolates the UUID chain. */
void
vulkan_decode_VkPhysicalDeviceProperties(
	struct vulkan_reader *reader,
	VkPhysicalDeviceProperties *properties)
{
	uint32_t sentinel;

	/* The adapter must hand the core codec an intact reader after the UUID chain. */
	(void)properties;
	if (reader->error != VK_SUCCESS)
		return;

	/* The raw peer terminates the UUID reply with this independent core sentinel. */
	sentinel = vulkan_read_u32(reader);
	assert(sentinel == 0xaabbccddU);
}

/* Ordinary image queries remain independently distinguishable from external queries. */
void
vulkan_decode_VkImageFormatProperties(
	struct vulkan_reader *reader,
	VkImageFormatProperties *properties)
{
	/* The capability adapter hands this fixed core payload to the separately tested codec. */
	properties->maxExtent.width = vulkan_read_u32(reader);
	properties->maxExtent.height = vulkan_read_u32(reader);
	properties->maxExtent.depth = vulkan_read_u32(reader);
	properties->maxMipLevels = vulkan_read_u32(reader);
	properties->maxArrayLayers = vulkan_read_u32(reader);
	properties->sampleCounts = vulkan_read_u32(reader);
	properties->maxResourceSize = vulkan_read_u64(reader);
}

/* Ordinary image queries remain independently distinguishable from external queries. */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceImageFormatProperties(
	VkPhysicalDevice device,
	VkFormat format,
	VkImageType type,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkImageCreateFlags flags,
	VkImageFormatProperties *properties)
{
	(void)device;
	(void)format;
	(void)type;
	(void)tiling;
	(void)usage;
	(void)flags;
	memset(properties, 0, sizeof(*properties));
	properties->maxMipLevels = 77U;

	/* Succeeded: the caller receives the complete independent capability reply. */
	return VK_SUCCESS;
}

/* The wire peer checks raw offsets and constructs replies without production encoders. */
VkResult
vulkan_context_execute(
	struct vulkan_context *ctx,
	const struct vulkan_writer *writer,
	size_t capacity,
	struct vulkan_reader *reader)
{
	uint32_t opcode;
	unsigned index;

	/* Every query must name the same live native physical device. */
	assert(ctx == &context);
	assert(get32(writer->data + 4U) == 1U);
	assert(get64(writer->data + 8U) == 0x1234U);

	/* Dispatches the three exact native opcodes without the production codec. */
	opcode = get32(writer->data);
	cursor = 0U;
	calls++;
	put32(opcode);
	if (opcode == 161U) {
		assert(writer->bytes == 68U);
		assert(get32(writer->data + 36U) == 0U);
		assert(get32(writer->data + 40U) == 3U);
		assert(get32(writer->data + 44U) == expected_native_type);
		put64(1U);
		put_header(VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES, 0U);
		put32(native_features);
		put32(native_compatible);
		put32(native_compatible);
	} else if (opcode == 150U) {
		assert(writer->bytes == 104U);
		assert(get32(writer->data + 48U) == expected_native_type);
		assert(get32(writer->data + 52U) == VK_FORMAT_R8G8B8A8_UNORM);
		assert(get32(writer->data + 56U) == VK_IMAGE_TYPE_2D);
		assert(get32(writer->data + 60U) == VK_IMAGE_TILING_OPTIMAL);
		assert(get32(writer->data + 64U) == 3U);
		put32(VK_SUCCESS);
		put64(1U);
		put_header(VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, 1U);
		put_header(VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES, 0U);
		put32(native_features);
		put32(native_compatible);
		put32(native_compatible);
		put32(4096U);
		put32(2048U);
		put32(1U);
		put32(12U);
		put32(8U);
		put32(1U);
		put64(0x10000000U);
	} else {
		assert(opcode == 148U && writer->bytes == 48U);
		assert(get32(writer->data + 36U) ==
		       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
		put64(1U);
		put_header(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, 1U);
		put_header(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, 0U);
		put64(bad_uuid_length ? 15U : 16U);

		/* Supplies a nonuniform device UUID to expose field ordering mistakes. */
		for (index = 0U; index < 16U; index++)
			output[cursor++] = (uint8_t)(index + 1U);

		/* Driver UUID and LUID are independently recognizable fixed arrays. */
		put64(16U);
		memset(output + cursor, 0x81U, 16U);
		cursor += 16U;
		put64(8U);
		memset(output + cursor, 0x42U, 8U);
		cursor += 8U;
		put32(1U);
		put32(1U);
		put32(0xaabbccddU);
	}

	/* Returns a snapshot whose ownership matches a completed native reply. */
	assert(cursor <= capacity);
	memset(reader, 0, sizeof(*reader));
	reader->data = malloc(cursor);
	assert(reader->data != NULL);
	memcpy(reader->data, output, cursor);
	reader->bytes = cursor;

	/* Succeeded: the caller receives the complete independent capability reply. */
	return VK_SUCCESS;
}

/* Exercises supported profiles, explicit refusals and malformed native responses. */
int
main(
	void)
{
	VkPhysicalDeviceExternalBufferInfo buffer;
	VkExternalBufferProperties buffer_properties;
	VkPhysicalDeviceImageFormatInfo2 image;
	VkPhysicalDeviceExternalImageFormatInfo input;
	VkImageFormatProperties2 image_properties;
	VkExternalImageFormatProperties external;
	VkPhysicalDeviceIDProperties identity;
	VkPhysicalDeviceIDProperties second;
	VkResult status;
	unsigned before;

	/* Enables sharing for one synthetic native device. */
	memset(&physical, 0, sizeof(physical));
	physical.object.kind = VULKAN_OBJECT_PHYSICAL_DEVICE;
	physical.object.context = &context;
	physical.object.wire_id = 0x1234U;
	context.capabilities = GPU_CAP_ALLOCATION_SHARE;

	/* The guest asks for an OPAQUE_FD storage/transfer allocation profile. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
	buffer.usage = 3U;
	buffer.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

	/* The output chain belongs to the application and survives the query. */
	memset(&buffer_properties, 0, sizeof(buffer_properties));
	buffer_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;

	/* Import/export native DMA_BUF support becomes guest OPAQUE_FD support. */
	native_features = 6U;
	native_compatible = 0x200U;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(buffer_properties.externalMemoryProperties.externalMemoryFeatures == 6U);
	assert(buffer_properties.externalMemoryProperties.compatibleHandleTypes == 1U);
	assert(buffer_properties.externalMemoryProperties.exportFromImportedHandleTypes == 1U);

	/* Dedicated-only profiles and unsupported types must not become false guest capabilities. */
	native_features = 7U;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(buffer_properties.externalMemoryProperties.externalMemoryFeatures == 0U);

	/* Linux handle types remain unavailable at the guest API boundary. */
	native_features = 6U;
	before = calls;
	buffer.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(calls == before &&
	       buffer_properties.externalMemoryProperties.externalMemoryFeatures == 0U);

	/* A missing kernel sharing capability must suppress the native query entirely. */
	buffer.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	context.capabilities = 0U;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(calls == before);
	context.capabilities = GPU_CAP_ALLOCATION_SHARE;

	/* Image capability queries preserve the exact optimal profile and both caller-owned chains. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
	image.pNext = &input;
	image.format = VK_FORMAT_R8G8B8A8_UNORM;
	image.type = VK_IMAGE_TYPE_2D;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = 3U;

	/* Attaches the requested handle type without changing the ordinary image profile. */
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
	input.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

	/* Preserves both caller-owned output records while filling their payloads. */
	memset(&image_properties, 0, sizeof(image_properties));
	image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	image_properties.pNext = &external;

	/* Only the external properties payload belongs to this extension. */
	memset(&external, 0, sizeof(external));
	external.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

	/* Observes the requested profile through the public properties2 entry point. */
	status = vkGetPhysicalDeviceImageFormatProperties2KHR(&physical, &image, &image_properties);
	assert(status == VK_SUCCESS && image_properties.imageFormatProperties.maxMipLevels == 12U);
	assert(image_properties.pNext == &external &&
	       external.externalMemoryProperties.externalMemoryFeatures == 6U);
	native_features = 7U;

	/* Observes the requested profile through the public properties2 entry point. */
	status = vkGetPhysicalDeviceImageFormatProperties2KHR(&physical, &image, &image_properties);
	assert(status == VK_ERROR_FORMAT_NOT_SUPPORTED &&
	       image_properties.imageFormatProperties.maxMipLevels == 0U);
	input.handleType = 0U;

	/* Observes the requested profile through the public properties2 entry point. */
	status = vkGetPhysicalDeviceImageFormatProperties2KHR(&physical, &image, &image_properties);
	assert(status == VK_SUCCESS && image_properties.imageFormatProperties.maxMipLevels == 77U);

	/* A paired renderer's opaque profile enables optimal sharing without DMA_BUF. */
	context.external_memory_type = 1U;
	expected_native_type = 1U;
	native_compatible = 1U;
	native_features = 6U;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(buffer_properties.externalMemoryProperties.externalMemoryFeatures == 6U);
	assert(buffer_properties.externalMemoryProperties.compatibleHandleTypes == 1U);

	/* The same native type is used for the exact optimal image query. */
	input.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	status = vkGetPhysicalDeviceImageFormatProperties2KHR(&physical, &image, &image_properties);
	assert(status == VK_SUCCESS);
	assert(external.externalMemoryProperties.externalMemoryFeatures == 6U);
	assert(image_properties.imageFormatProperties.maxMipLevels == 12U);

	/*
	 * UUIDs are stable across queries, distinguish the guest driver and never claim a host
	 * LUID.
	 */
	memset(&identity, 0, sizeof(identity));
	memset(&second, 0, sizeof(second));
	status = vulkan_physical_identity(&physical, &identity);
	assert(status == VK_SUCCESS && identity.deviceUUID[0] == 1U &&
	       identity.deviceUUID[15] == 16U);
	assert(identity.driverUUID[0] == (0x81U ^ 0x7aU) && identity.deviceLUIDValid == VK_FALSE);
	status = vulkan_physical_identity(&physical, &second);
	assert(status == VK_SUCCESS && memcmp(&identity, &second, sizeof(identity)) == 0);

	/* Malformed native UUID extents must become device loss, not partial identity. */
	bad_uuid_length = 1U;
	status = vulkan_physical_identity(&physical, &identity);
	assert(status == VK_ERROR_DEVICE_LOST && context.error == VK_ERROR_DEVICE_LOST);

	/* A separately corrupted structure tag also terminates the query context. */
	bad_uuid_length = 0U;
	context.error = VK_SUCCESS;
	bad_header = 1U;
	vkGetPhysicalDeviceExternalBufferPropertiesKHR(&physical, &buffer, &buffer_properties);
	assert(context.error == VK_ERROR_DEVICE_LOST &&
	       buffer_properties.externalMemoryProperties.externalMemoryFeatures == 0U);

	/* Succeeded: every advertised profile and malformed response matched its contract. */
	puts("Vulkan external profiles: exact native usage/type translation, dedicated-only "
	     "rejection, guest-only OPAQUE_FD, stable UUIDs, malformed reply loss PASS");
	return 0;
}

/* Reads one little-endian scalar independently of the maintained Venus codec. */
static uint32_t
get32(
	const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8U | (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

/* Combines two independently decoded little-endian words. */
static uint64_t
get64(
	const uint8_t *p)
{
	uint32_t low;
	uint32_t high;

	/* Decode the two words before exposing the complete handle identity. */
	low = get32(p);
	high = get32(p + 4U);

	/* Succeeded: both words preserve the native little-endian value. */
	return low | (uint64_t)high << 32U;
}

/* Appends one scalar to the raw response buffer. */
static void
put32(
	uint32_t value)
{
	unsigned index;

	/* Each byte has a fixed position independent of the host integer byte order. */
	for (index = 0U; index < 4U; index++)
		output[cursor++] = (uint8_t)(value >> (index * 8U));
}

/* Appends a low word followed by its high word. */
static void
put64(
	uint64_t value)
{
	put32((uint32_t)value);
	put32((uint32_t)(value >> 32U));
}

/* Appends a structure tag and its optional-chain presence marker. */
static void
put_header(
	uint32_t type,
	unsigned next)
{
	put32(type + bad_header);
	put64(next);
}
