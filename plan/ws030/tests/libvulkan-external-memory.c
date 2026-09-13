/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reuse the independent core peer, adding only native sharing and immutable K metadata. */
#define main core_memory_main
#define vulkan_context_execute core_context_execute
#define vulkan_resource_blob_flags core_resource_blob_flags
#define vulkan_resource_destroy core_resource_destroy
#define vulkan_physical_identity core_physical_identity
#define memory_test_ioctl core_memory_ioctl
#include "libvulkan-memory.c"
#undef main
#undef vulkan_context_execute
#undef vulkan_resource_blob_flags
#undef vulkan_resource_destroy
#undef vulkan_physical_identity
#undef memory_test_ioctl
#include <uapi/gpu-allocation.h>
#include <uapi/gpu-scanout.h>

/* Each K import has a distinct attachment lifetime before native VkDeviceMemory creation. */
struct imported_alias {
	uint64_t handle;
	uint32_t resource;
	uint64_t native;
	int fd;
	unsigned active;
};

/* This serial oracle retains immutable descriptions alongside actual duplicated file references. */
static struct gpu_allocation_descriptor exported_metadata[1024];
static struct imported_alias aliases[64];
static unsigned alias_count;
static unsigned alias_retired;
static unsigned map_queries;
static unsigned export_queries;
static unsigned fail_export_copyout;
static unsigned fail_import;
static unsigned corrupt_metadata;

/* Independent profile inputs describe the expected native type and export flags. */
static uint32_t expected_native_type;
static uint32_t expected_blob_flags;

/* Numeric placement inputs are checked without decoding through the production UAPI struct. */
static unsigned placed_queries;
static unsigned legacy_blob_queries;
static int placed_error;
static uint32_t expected_placement_flags;
static uint64_t expected_placement_limit;
static uint64_t expected_placement_alignment;

VkResult vulkan_context_execute(struct vulkan_context *ctx, const struct vulkan_writer *writer, size_t capacity, struct vulkan_reader *reply);
VkResult vulkan_resource_blob_flags(struct vulkan_context *ctx, uint64_t bytes, uint64_t id, uint32_t flags, uint64_t *handle, uint32_t *resource);
VkResult vulkan_resource_destroy(struct vulkan_context *ctx, uint64_t handle);
VkResult vulkan_physical_identity(struct VkPhysicalDevice_T *device, VkPhysicalDeviceIDProperties *identity);
int external_memory_ioctl(int fd, unsigned long command, ...);
static struct imported_alias *find_alias(uint64_t handle);
static int export_memory(VkDeviceMemory memory);
static VkResult import_memory(int fd, uint64_t bytes, uint32_t type, VkDeviceMemory *memory);
static int test_profile(uint32_t native_type);
static void test_placement(const VkMemoryAllocateInfo *info);

/* Decode the pinned sharing prefix independently, then reuse the unrelated core allocation oracle. */
VkResult
vulkan_context_execute(struct vulkan_context *ctx, const struct vulkan_writer *writer, size_t capacity, struct vulkan_reader *reply)
{
	struct vulkan_writer translated;
	struct imported_alias *alias;
	struct native_memory *memory;
	uint8_t bytes[72];
	uint32_t structure;
	uint32_t resource;
	uint64_t identity;
	unsigned index;
	VkResult status;

	if (get32(writer->data) != 21 || writer->bytes == 72)
		return core_context_execute(ctx, writer, capacity, reply);

	/* Sharing allocation adds exactly one 16-byte chain node to the fixed core record. */
	assert(writer->bytes == 88);
	assert(get64(writer->data + 28) == 1);
	structure = get32(writer->data + 36);
	assert(get64(writer->data + 40) == 0);
	resource = get32(writer->data + 48);
	alias = NULL;
	if (structure == 1000072002U) {
		assert(resource == expected_native_type);
	} else {
		assert(structure == 1000384002U);
		assert(get64(writer->data + 52) == 8192);
		for (index = 0; index < alias_count; index++) {
			if (aliases[index].resource == resource && aliases[index].active)
				alias = &aliases[index];
		}
		assert(alias != NULL);
	}

	/* The test removes the already checked extension rather than using production encoders. */
	memcpy(bytes, writer->data, 36);
	memset(bytes + 28, 0, 8);
	memcpy(bytes + 36, writer->data + 52, 36);
	translated = *writer;
	translated.data = bytes;
	translated.bytes = sizeof(bytes);
	status = core_context_execute(ctx, &translated, capacity, reply);
	if (status != VK_SUCCESS || alias == NULL || (VkResult)get32(reply->data + 4) != VK_SUCCESS)
		return status;

	/* Native import retains its own shared-file reference behind the new Vulkan identity. */
	identity = get64(bytes + 64);
	memory = find(identity);
	memory->fd = dup(alias->fd);
	assert(memory->fd >= 0);
	memory->blob = 1;
	memory->exported = 1;
	alias->native = identity;
	return VK_SUCCESS;
}

VkResult
vulkan_resource_blob_flags(struct vulkan_context *ctx, uint64_t bytes, uint64_t id, uint32_t flags, uint64_t *handle, uint32_t *resource)
{
	assert(flags == expected_blob_flags);
	legacy_blob_queries++;
	return core_resource_blob_flags(ctx, bytes, id, GPU_BLOB_MAPPABLE, handle, resource);
}

VkResult
vulkan_resource_destroy(struct vulkan_context *ctx, uint64_t handle)
{
	struct imported_alias *alias;
	struct native_memory *memory;
	int error;

	locked();
	alias = find_alias(handle);
	if (alias == NULL)
		return core_resource_destroy(ctx, handle);

	assert(alias->active);
	if (alias->native != 0) {
		memory = find(alias->native);
		assert(memory->mapped == 0 && memory->blob == 1);
		error = close(memory->fd);
		assert(error == 0);
		memory->fd = -1;
		memory->blob = 0;
	}

	error = close(alias->fd);
	assert(error == 0);
	alias->fd = -1;
	alias->active = 0;
	alias_retired++;
	return VK_SUCCESS;
}

VkResult
vulkan_physical_identity(struct VkPhysicalDevice_T *device, VkPhysicalDeviceIDProperties *identity)
{
	assert(device == &physical);
	memset(identity->deviceUUID, 0x31, VK_UUID_SIZE);
	memset(identity->driverUUID, 0x97, VK_UUID_SIZE);
	return VK_SUCCESS;
}

int
external_memory_ioctl(int fd, unsigned long command, ...)
{
	va_list ap;
	void *argument;
	struct gpu_allocation_export *export;
	struct gpu_allocation_import *import;
	struct gpu_resource_map *map;
	struct gpu_resource_map core_map;
	struct imported_alias *alias;
	struct native_memory *memory;
	uint8_t *raw;
	uint64_t handle;
	uint32_t resource;
	VkResult status;
	int exported;
	int error;

	locked();
	assert(fd == context.fd);
	va_start(ap, command);
	argument = va_arg(ap, void *);
	va_end(ap);
	/* Fixed independent byte positions detect accidental reuse of the old 40-byte request. */
	if (command == _IOWR('G', 33, uint8_t[64])) {
		placed_queries++;
		raw = argument;
		assert(get32(raw) == 1U && get32(raw + 4) == 64U);
		assert(get64(raw + 8) == 8192U && get64(raw + 16) != 0U);
		assert(get64(raw + 24) == 0U && get32(raw + 36) == 0U);
		assert(get32(raw + 32) == 7U);
		assert(get32(raw + 40) == expected_placement_flags);
		assert(get32(raw + 44) == 0U);
		assert(get64(raw + 48) == expected_placement_limit);
		assert(get64(raw + 56) == expected_placement_alignment);
		memory = find(get64(raw + 16));
		assert(memory->bytes == 8192U && memory->blob == 0U && memory->fd == -1);

		/* Rejected placement never acquires a K alias or exported backing reference. */
		if (placed_error != 0) {
			errno = placed_error;
			placed_error = 0;
			return -1;
		}

		/* An accepted fake backend exports the actual native allocation's shared file. */
		status = core_resource_blob_flags(&context, 8192U, get64(raw + 16),
		    GPU_BLOB_MAPPABLE, &handle, &resource);
		assert(status == VK_SUCCESS);
		put64(raw + 24, handle);
		put32(raw + 36, resource);
		return 0;
	}

	if (command == GPU_ALLOCATION_EXPORT) {
		export = argument;
		export_queries++;
		assert(export->version == GPU_ABI_VERSION && export->size == sizeof(*export));
		assert(export->flags == GPU_HANDLE_CLOEXEC && export->fd == -1);
		assert(export->allocation.schema == 0x564d3031U && export->allocation.metadata_bytes == 48);
		assert(get64(export->allocation.metadata) == 5000);
		assert(get32(export->allocation.metadata + 8) == 0);
		assert(get32(export->allocation.metadata + 12) == 0);
		assert(export->allocation.metadata[16] == 0x31 && export->allocation.metadata[32] == 0x97);
		memory = find(export->handle);
		assert(memory->blob == 1 && export->allocation.allocation_bytes == 8192);
		exported = fcntl(memory->fd, F_DUPFD_CLOEXEC, 0);
		assert(exported >= 0 && exported < 1024);
		if (fail_export_copyout) {
			fail_export_copyout = 0;
			close(exported);
			errno = EFAULT;
			return -1;
		}
		export->fd = exported;
		export->allocation.device_id = 0x1025;
		exported_metadata[exported] = export->allocation;
		return 0;
	}

	if (command == GPU_ALLOCATION_IMPORT) {
		import = argument;
		assert(import->version == GPU_ABI_VERSION && import->size == sizeof(*import));
		assert(import->handle == 0 && import->resource_id == 0 && import->flags == 0);
		if (fail_import) {
			fail_import = 0;
			errno = EXDEV;
			return -1;
		}
		assert(import->fd >= 0 && import->fd < 1024);
		error = fcntl(import->fd, F_GETFD);
		assert(error >= 0);
		assert(alias_count < 64);
		alias = &aliases[alias_count++];
		alias->handle = UINT64_C(0x70000000) + alias_count;
		alias->resource = 7000 + alias_count;
		alias->fd = dup(import->fd);
		assert(alias->fd >= 0);
		alias->active = 1;
		import->handle = alias->handle;
		import->resource_id = alias->resource;
		import->allocation = exported_metadata[import->fd];
		if (corrupt_metadata == 1)
			import->allocation.schema++;
		if (corrupt_metadata == 2)
			import->allocation.metadata[16] ^= 1;
		if (corrupt_metadata == 3)
			import->allocation.metadata[32] ^= 1;
		if (corrupt_metadata == 4)
			import->allocation.metadata_bytes--;
		if (corrupt_metadata == 5)
			import->allocation.metadata[12] = 1;
		return 0;
	}

	assert(command == GPU_RESOURCE_MAP);
	map = argument;
	map_queries++;
	alias = find_alias(map->handle);
	if (alias == NULL)
		return core_memory_ioctl(fd, command, argument);

	assert(alias->active && alias->native != 0);
	core_map = *map;
	core_map.handle = alias->native;
	error = core_memory_ioctl(fd, command, &core_map);
	map->offset = core_map.offset;
	map->bytes = core_map.bytes;
	return error;
}

static struct imported_alias *
find_alias(uint64_t handle)
{
	unsigned index;

	for (index = 0; index < alias_count; index++) {
		if (aliases[index].handle == handle)
			return &aliases[index];
	}
	return NULL;
}

static int
export_memory(VkDeviceMemory memory)
{
	VkMemoryGetFdInfoKHR info;
	VkResult status;
	int fd;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
	info.memory = memory;
	info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	status = vkGetMemoryFdKHR((VkDevice)&owner, &info, &fd);
	assert(status == VK_SUCCESS);
	return fd;
}

static VkResult
import_memory(int fd, uint64_t bytes, uint32_t type, VkDeviceMemory *memory)
{
	VkImportMemoryFdInfoKHR import;
	VkMemoryAllocateInfo info;

	memset(&import, 0, sizeof(import));
	import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
	import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	import.fd = fd;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = &import;
	info.allocationSize = bytes;
	info.memoryTypeIndex = type;
	return vkAllocateMemory((VkDevice)&owner, &info, NULL, memory);
}

/* Runs all ownership cases with independently selected stock and paired profiles. */
int
main(void)
{
	int error;

	/* Both native profiles retain the same public OPAQUE_FD ownership semantics. */
	error = test_profile(0x200U);
	assert(error == 0);
	error = test_profile(1U);
	assert(error == 0);

	/* Succeeded: type selection did not change ownership, bounds or private WSI flags. */
	puts("libvulkan external memory: stock DMA and negotiated OPAQUE, private WSI DMA, placed64/legacy40, placement refusal and terminal ownership, immutable metadata, native extent and lazy mmap PASS");
	return 0;
}

/* Exercises complete public allocation lifetime under one fixed native export profile. */
static int
test_profile(
	uint32_t native_type)
{
	VkExportMemoryAllocateInfo export;
	VkMemoryAllocateInfo info;
	VkMemoryGetFdInfoKHR get;
	VkDeviceMemory source;
	VkDeviceMemory imported;
	VkResult status;
	void *mapping;
	uint8_t expected;
	uint8_t actual;
	unsigned before;
	unsigned index;
	int fd;
	int error;
	ssize_t bytes;

	/* Selects independent expected wire and blob representations for this renderer. */
	expected_native_type = native_type;
	expected_blob_flags = 7U;
	if (native_type == 1U)
		expected_blob_flags = 3U;
	map_queries = 0;
	mappings = 0;

	memset(&context, 0, sizeof(context));
	context.external_memory_type = native_type;
	context.fd = 901;
	context.max_resource_bytes = 256U * 1024U * 1024U;
	context.capabilities = GPU_CAP_MAPPING | GPU_CAP_ALLOCATION_SHARE;
	pthread_mutex_init(&context.mutex, NULL);
	memset(&owner, 0, sizeof(owner));
	owner.object.context = &context;
	owner.object.wire_id = 1000;
	owner.object.kind = VULKAN_OBJECT_DEVICE;
	owner.enabled_extensions = VULKAN_DEVICE_EXTERNAL_MEMORY_FD;
	owner.physical = &physical;
	memset(&physical, 0, sizeof(physical));
	physical.memory.memoryTypeCount = 2;
	physical.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	physical.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	memset(&export, 0, sizeof(export));
	export.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
	export.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = &export;
	info.allocationSize = 5000;
	status = vkAllocateMemory((VkDevice)&owner, &info, NULL, &source);
	assert(status == VK_SUCCESS && map_queries == 0 && mappings == 0);
	fd = export_memory(source);
	assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);

	/* Every metadata rejection must retire only its new alias and leave the caller fd open. */
	for (index = 1; index <= 5; index++) {
		corrupt_metadata = index;
		before = alias_retired;
		imported = (VkDeviceMemory)(uintptr_t)1;
		status = import_memory(fd, 5000, 0, &imported);
		assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE && imported == VK_NULL_HANDLE);
		assert(alias_retired == before + 1 && live == 1);
		assert(fcntl(fd, F_GETFD) >= 0);
	}
	corrupt_metadata = 0;
	status = import_memory(fd, 5001, 0, &imported);
	assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE && fcntl(fd, F_GETFD) >= 0);
	status = import_memory(fd, 5000, 1, &imported);
	assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE && fcntl(fd, F_GETFD) >= 0);

	/* Cross-device kernel refusal and native allocation refusal each preserve input fd ownership. */
	fail_import = 1;
	before = alias_count;
	status = import_memory(fd, 5000, 0, &imported);
	assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE && alias_count == before);
	fail_native = 1;
	before = alias_retired;
	status = import_memory(fd, 5000, 0, &imported);
	assert(status == VK_ERROR_OUT_OF_DEVICE_MEMORY && alias_retired == before + 1);
	assert(fcntl(fd, F_GETFD) >= 0 && live == 1 && map_queries == 0);

	/* Failed copyout does not expose an fd and does not damage the source allocation. */
	memset(&get, 0, sizeof(get));
	get.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
	get.memory = source;
	get.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	fail_export_copyout = 1;
	error = 123;
	status = vkGetMemoryFdKHR((VkDevice)&owner, &get, &error);
	assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE && error == -1 && live == 1);

	/* Exported file ownership survives destruction of the original Vulkan allocation. */
	expected = 0x5d;
	bytes = pwrite(fd, &expected, 1, 4999);
	assert(bytes == 1);
	vkFreeMemory((VkDevice)&owner, source, NULL);
	assert(live == 0 && fcntl(fd, F_GETFD) >= 0);
	status = import_memory(fd, 5000, 0, &imported);
	assert(status == VK_SUCCESS && imported != VK_NULL_HANDLE && live == 1);
	errno = 0;
	assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
	assert(map_queries == 0 && mappings == 0);

	/* The page-rounded native import must not enlarge the original public allocation bounds. */
	mapping = (void *)(uintptr_t)1;
	status = vkMapMemory((VkDevice)&owner, imported, 5000, 1, 0, &mapping);
	assert(status == VK_ERROR_MEMORY_MAP_FAILED && mapping == NULL);
	assert(map_queries == 0 && mappings == 0);

	/* Host-visible imported memory obtains its mmap token lazily and exposes the same real bytes. */
	status = vkMapMemory((VkDevice)&owner, imported, 0, VK_WHOLE_SIZE, 0, &mapping);
	assert(status == VK_SUCCESS && map_queries == 1 && mappings == 1);
	actual = ((uint8_t *)mapping)[4999];
	assert(actual == expected);
	((uint8_t *)mapping)[7] = 0xa3;
	actual = 0;
	bytes = pread(aliases[alias_count - 1].fd, &actual, 1, 7);
	assert(bytes == 1 && actual == 0xa3);
	vkUnmapMemory((VkDevice)&owner, imported);
	vkFreeMemory((VkDevice)&owner, imported, NULL);
	assert(live == 0 && alias_count == alias_retired && owner.object.first_child == NULL);
	/* Explicit private WSI allocations retain cross-device DMA-BUF on both profiles. */
	info.pNext = NULL;
	expected_native_type = 0x200U;
	expected_blob_flags = 7U;
	status = vulkan_memory_allocate((VkDevice)&owner, &info, NULL, VK_TRUE, &source);
	assert(status == VK_SUCCESS && live == 1);
	vkFreeMemory((VkDevice)&owner, source, NULL);
	assert(live == 0);

	/* Device-only public allocations never acquire MAPPABLE as an incidental share flag. */
	info.pNext = &export;
	info.memoryTypeIndex = 1;
	expected_native_type = native_type;
	expected_blob_flags = 6U;
	if (native_type == 1U)
		expected_blob_flags = 2U;
	status = vkAllocateMemory((VkDevice)&owner, &info, NULL, &source);
	assert(status == VK_SUCCESS && live == 1);
	vkFreeMemory((VkDevice)&owner, source, NULL);
	assert(live == 0 && owner.object.first_child == NULL);

	/* Placement requests preserve ownership and leave terminal-session cleanup until last. */
	info.pNext = NULL;
	info.memoryTypeIndex = 0U;
	expected_native_type = 0x200U;
	expected_blob_flags = 7U;
	test_placement(&info);
	pthread_mutex_destroy(&context.mutex);
	return 0;
}

/*
 * Tests nonzero placement byte-for-byte and independently counts failed native ownership.
 */
static void
test_placement(
	const VkMemoryAllocateInfo *info)
{
	struct gpu_placement placement;
	VkAllocationCallbacks callbacks;
	VkDeviceMemory memory;
	VkResult status;
	unsigned before_placed;
	unsigned before_legacy;
	unsigned before_frees;
	unsigned before_aliases;
	unsigned before_exports;
	unsigned index;
	int failures[3];
	VkResult results[3];

	/* Callback counters include the unpublished owner and native command temporaries. */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.pUserData = &create_userdata;
	callbacks.pfnAllocation = allocate;
	callbacks.pfnReallocation = reallocate;
	callbacks.pfnFree = release;
	assert(live == 0U && owner.object.first_child == NULL);
	assert(alias_count == alias_retired);
	before_aliases = alias_count;

	/* NULL is the old operation, not a new placement request with invented constraints. */
	before_placed = placed_queries;
	before_legacy = legacy_blob_queries;
	status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, NULL, &memory);
	assert(status == VK_SUCCESS && live == 1U);
	assert(placed_queries == before_placed && legacy_blob_queries == before_legacy + 1U);
	vkFreeMemory((VkDevice)&owner, memory, &callbacks);
	assert(live == 0U && callback_allocs == callback_frees);

	/* An explicit empty requirement still reaches ioctl33, whose backend may use its old allocator. */
	memset(&placement, 0, sizeof(placement));
	expected_placement_flags = 0U;
	expected_placement_limit = 0U;
	expected_placement_alignment = 0U;
	before_placed = placed_queries;
	before_legacy = legacy_blob_queries;
	status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, &placement, &memory);
	assert(status == VK_SUCCESS && live == 1U);
	assert(placed_queries == before_placed + 1U && legacy_blob_queries == before_legacy);
	vkFreeMemory((VkDevice)&owner, memory, &callbacks);
	assert(live == 0U && callback_allocs == callback_frees);

	/* A full 64-bit address ceiling and a distinct alignment reach the backend unchanged. */
	memset(&placement, 0, sizeof(placement));
	placement.flags = 2U;
	placement.max_dma_address = UINT64_C(0x123456780);
	placement.alignment = 65536U;
	expected_placement_flags = 2U;
	expected_placement_limit = UINT64_C(0x123456780);
	expected_placement_alignment = 65536U;
	before_placed = placed_queries;
	before_legacy = legacy_blob_queries;
	status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, &placement, &memory);
	assert(status == VK_SUCCESS && live == 1U);
	assert(placed_queries == before_placed + 1U && legacy_blob_queries == before_legacy);
	vkFreeMemory((VkDevice)&owner, memory, &callbacks);
	assert(live == 0U && callback_allocs == callback_frees);

	/* Unsupported placement permits one caller fallback; allocation pressure remains distinct. */
	failures[0] = ENOTSUP;
	failures[1] = ENOMEM;
	failures[2] = ENOTTY;
	results[0] = VK_ERROR_FEATURE_NOT_PRESENT;
	results[1] = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	results[2] = VK_ERROR_FEATURE_NOT_PRESENT;
	for (index = 0U; index < 3U; index++) {
		before_frees = frees;
		before_exports = exports;
		before_placed = placed_queries;
		placed_error = failures[index];
		memory = (VkDeviceMemory)(uintptr_t)1U;
		status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, &placement, &memory);
		assert(status == results[index] && memory == VK_NULL_HANDLE);
		assert(placed_queries == before_placed + 1U && frees == before_frees + 1U);
		assert(live == 0U && exports == before_exports && alias_count == before_aliases);
		assert(alias_retired == before_aliases && owner.object.first_child == NULL);
		assert(callback_allocs == callback_frees && context.error == VK_SUCCESS);
	}

	/* Unsupported placement must not retry through an unconstrained allocator behind the caller. */
	assert(legacy_blob_queries == before_legacy);

	/* A rejected native allocation never issues the later constrained blob request. */
	before_frees = frees;
	before_placed = placed_queries;
	fail_native = 1U;
	status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, &placement, &memory);
	assert(status == VK_ERROR_OUT_OF_DEVICE_MEMORY && memory == VK_NULL_HANDLE);
	assert(placed_queries == before_placed && frees == before_frees);
	assert(live == 0U && owner.object.first_child == NULL && callback_allocs == callback_frees);

	/* Transport loss leaves the uncertain native allocation for session destruction, never an unsafe free. */
	before_frees = frees;
	before_exports = exports;
	before_placed = placed_queries;
	placed_error = EIO;
	status = vulkan_memory_allocate_placed((VkDevice)&owner, info, &callbacks, &placement, &memory);
	assert(status == VK_ERROR_DEVICE_LOST && memory == VK_NULL_HANDLE);
	assert(placed_queries == before_placed + 1U && frees == before_frees);
	assert(context.error == VK_ERROR_DEVICE_LOST && live == 1U && exports == before_exports);
	assert(alias_count == before_aliases && alias_retired == before_aliases);
	assert(owner.object.first_child == NULL && callback_allocs == callback_frees);

	/* The fake renderer namespace closes only after verifying the retained record owns no fd or blob. */
	for (index = 0U; index < 256U; index++) {
		if (native[index].id == 0U)
			continue;

		assert(native[index].fd == -1 && native[index].blob == 0U && native[index].mapped == 0U);
		memset(&native[index], 0, sizeof(native[index]));
		live--;
	}
	assert(live == 0U);
}
