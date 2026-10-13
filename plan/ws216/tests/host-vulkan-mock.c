/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p004: the model of a Vulkan device for Notes' renderer on the host
 * (see host-vulkan-mock.h).  Every function render.c calls is defined
 * here with the prototype of <vulkan/vulkan.h>; each object is a numbered
 * handle that is never reused, so a test can tell every object apart.
 */

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include "host-vulkan-mock.h"

#include <keiland/keiland.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The memory types: the device's own, and one the host sees. */
#define MOCK_TYPE_DEVICE	0U
#define MOCK_TYPE_HOST		1U

/* The swapchain's images. */
#define MOCK_SWAPCHAIN_IMAGES	3U

/* The model, which the tests set and read. */
struct mock_device mock;

/* The objects, by number, and how many there are; numbers are never reused. */
static struct mock_object *objects;
static size_t object_count;
static size_t object_capacity;

/* What the requirements asked for last are for: a buffer (1) or an image (0); the allocation that follows takes its limit. */
static int requirements_for_buffer;

/* The command buffer's bound pipeline, vertex buffer and set, and its push constants. */
static size_t bound_pipeline;
static size_t bound_buffer;
static size_t bound_set;
static float pushed[8];

static void *mock_new(int kind, size_t *number);

/* Gives an object of a handle (NULL for a null handle). */
struct mock_object *
mock_get(
	const void *handle)
{
	size_t number;

	if (handle == NULL)
		return NULL;
	number = mock_number(handle);
	if (number >= object_count)
		return NULL;
	return &objects[number];
}

/* Gives a handle's number. */
size_t
mock_number(
	const void *handle)
{
	return (size_t)((uintptr_t)handle / 16U) - 1U;
}

/* Counts the live objects of a kind. */
size_t
mock_live(
	int kind)
{
	size_t index;
	size_t count;

	count = 0;
	for (index = 0; index < object_count; index++) {
		if (objects[index].kind == kind)
			count++;
	}
	return count;
}

/* Tells whether two shaders' SPIR-V are the same words (each file that includes shaders.h has its own copy). */
int
mock_same_code(
	const uint32_t *code,
	size_t size,
	const uint32_t *other,
	size_t other_size)
{
	if (code == NULL || other == NULL || size != other_size)
		return 0;
	return memcmp(code, other, size) == 0;
}

/* Forgets every object and every draw (between tests). */
void
mock_reset(
	void)
{
	size_t index;

	for (index = 0; index < object_count; index++)
		free(objects[index].bytes);
	free(objects);
	objects = NULL;
	object_count = 0;
	object_capacity = 0;
	free(mock.draws);
	memset(&mock, 0, sizeof(mock));
	bound_pipeline = 0;
	bound_buffer = 0;
	bound_set = 0;
}

/* Makes an object of a kind and gives its handle (and its number). */
static void *
mock_new(
	int kind,
	size_t *number)
{
	struct mock_object *larger;
	size_t capacity;

	if (object_count == object_capacity) {
		capacity = object_capacity * 2U;
		if (capacity < 256U)
			capacity = 256U;
		larger = realloc(objects, capacity * sizeof(*larger));
		if (larger == NULL) {
			fprintf(stderr, "mock: out of memory\n");
			exit(2);
		}
		objects = larger;
		object_capacity = capacity;
	}
	memset(&objects[object_count], 0, sizeof(objects[0]));
	objects[object_count].kind = kind;
	if (number != NULL)
		*number = object_count;
	object_count++;
	return (void *)(uintptr_t)(object_count * 16U);
}

/* Marks an object gone, freeing its bytes. */
static void
mock_free(
	const void *handle,
	int kind)
{
	struct mock_object *object;

	object = mock_get(handle);
	if (object == NULL)
		return;
	if (object->kind != kind) {
		fprintf(stderr, "mock: object %lu freed as kind %d, is %d\n", (unsigned long)mock_number(handle), kind, object->kind);
		exit(2);
	}
	free(object->bytes);
	object->bytes = NULL;
	object->kind = MOCK_FREE;
}

int
kl_window_vulkan_surface(
	struct kl_window *window,
	VkInstance instance,
	VkSurfaceKHR *surface)
{
	(void)window;
	(void)instance;
	*surface = mock_new(MOCK_OTHER, NULL);
	return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
	(void)pCreateInfo;
	(void)pAllocator;
	*pInstance = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
	(void)instance;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices)
{
	(void)instance;
	if (pPhysicalDevices != NULL && *pPhysicalDeviceCount >= 1U)
		pPhysicalDevices[0] = mock_new(MOCK_OTHER, NULL);
	*pPhysicalDeviceCount = 1U;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties)
{
	(void)physicalDevice;
	if (pQueueFamilyProperties != NULL && *pQueueFamilyPropertyCount >= 1U) {
		memset(&pQueueFamilyProperties[0], 0, sizeof(pQueueFamilyProperties[0]));
		pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
		pQueueFamilyProperties[0].queueCount = 1U;
	}
	*pQueueFamilyPropertyCount = 1U;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported)
{
	(void)physicalDevice;
	(void)queueFamilyIndex;
	(void)surface;
	*pSupported = VK_TRUE;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
	(void)physicalDevice;
	memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
	pMemoryProperties->memoryTypeCount = 2U;
	pMemoryProperties->memoryTypes[MOCK_TYPE_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	pMemoryProperties->memoryTypes[MOCK_TYPE_HOST].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	pMemoryProperties->memoryHeapCount = 1U;
	pMemoryProperties->memoryHeaps[0].size = 1024ULL * 1024ULL * 1024ULL;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
	(void)physicalDevice;
	(void)pCreateInfo;
	(void)pAllocator;
	*pDevice = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
}

VKAPI_ATTR void VKAPI_CALL
vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
	(void)device;
	(void)queueFamilyIndex;
	(void)queueIndex;
	*pQueue = mock_new(MOCK_OTHER, NULL);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats)
{
	(void)physicalDevice;
	(void)surface;
	if (pSurfaceFormats != NULL && *pSurfaceFormatCount >= 1U) {
		pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
		pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}
	*pSurfaceFormatCount = 1U;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
	(void)physicalDevice;
	(void)surface;
	memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
	pSurfaceCapabilities->minImageCount = 2U;
	pSurfaceCapabilities->maxImageCount = 8U;
	pSurfaceCapabilities->minImageExtent.width = 1U;
	pSurfaceCapabilities->minImageExtent.height = 1U;
	pSurfaceCapabilities->maxImageExtent.width = 8192U;
	pSurfaceCapabilities->maxImageExtent.height = 8192U;
	pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pSwapchain = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)swapchain;
	(void)pAllocator;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
{
	(void)instance;
	(void)surface;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
	uint32_t index;

	(void)device;
	(void)swapchain;
	if (pSwapchainImages != NULL) {
		for (index = 0; index < MOCK_SWAPCHAIN_IMAGES && index < *pSwapchainImageCount; index++)
			pSwapchainImages[index] = mock_new(MOCK_OTHER, NULL);
	}
	*pSwapchainImageCount = MOCK_SWAPCHAIN_IMAGES;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties)
{
	(void)physicalDevice;
	memset(pFormatProperties, 0, sizeof(*pFormatProperties));
	if (format == VK_FORMAT_D24_UNORM_S8_UINT)
		pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
	struct mock_object *object;
	size_t number;

	(void)device;
	(void)pAllocator;
	*pImage = mock_new(MOCK_IMAGE, &number);
	object = &objects[number];
	object->width = pCreateInfo->extent.width;
	object->height = pCreateInfo->extent.height;
	object->usage = pCreateInfo->usage;
	object->tiling = pCreateInfo->tiling;
	object->size = (size_t)pCreateInfo->extent.width * pCreateInfo->extent.height * 4U;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(image, MOCK_IMAGE);
}

VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements)
{
	(void)device;
	requirements_for_buffer = 0;
	memset(pMemoryRequirements, 0, sizeof(*pMemoryRequirements));
	pMemoryRequirements->size = mock_get(image)->size;
	pMemoryRequirements->alignment = 256U;
	pMemoryRequirements->memoryTypeBits = 0x3U;
}

VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout)
{
	(void)device;
	(void)pSubresource;
	memset(pLayout, 0, sizeof(*pLayout));
	pLayout->rowPitch = (VkDeviceSize)mock_get(image)->width * 4U;
	pLayout->size = mock_get(image)->size;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
	(void)device;
	(void)memoryOffset;
	mock_get(image)->memory = mock_number(memory);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
	size_t number;

	(void)device;
	(void)pAllocator;
	*pBuffer = mock_new(MOCK_BUFFER, &number);
	objects[number].size = (size_t)pCreateInfo->size;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(buffer, MOCK_BUFFER);
}

VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements)
{
	(void)device;
	requirements_for_buffer = 1;
	memset(pMemoryRequirements, 0, sizeof(*pMemoryRequirements));
	pMemoryRequirements->size = mock_get(buffer)->size;
	pMemoryRequirements->alignment = 256U;
	pMemoryRequirements->memoryTypeBits = 0x3U;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
	(void)device;
	(void)memoryOffset;
	mock_get(buffer)->memory = mock_number(memory);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory)
{
	size_t limit;
	size_t size;
	size_t number;

	(void)device;
	(void)pAllocator;
	size = (size_t)pAllocateInfo->allocationSize;
	limit = mock.image_limit;
	if (requirements_for_buffer)
		limit = mock.buffer_limit;
	if (limit != 0U && size > limit) {
		mock.refused++;
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	if (size > mock.largest)
		mock.largest = size;
	*pMemory = mock_new(MOCK_MEMORY, &number);
	objects[number].size = size;
	if (pAllocateInfo->memoryTypeIndex == MOCK_TYPE_HOST) {
		objects[number].bytes = calloc(1U, size);
		if (objects[number].bytes == NULL)
			return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(memory, MOCK_MEMORY);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData)
{
	(void)device;
	(void)size;
	(void)flags;
	if (mock_get(memory)->bytes == NULL)
		return VK_ERROR_MEMORY_MAP_FAILED;
	*ppData = mock_get(memory)->bytes + offset;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
	size_t number;

	(void)device;
	(void)pAllocator;
	*pView = mock_new(MOCK_VIEW, &number);
	objects[number].image = mock_number(pCreateInfo->image);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(imageView, MOCK_VIEW);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
	size_t number;

	(void)device;
	(void)pAllocator;
	*pSampler = mock_new(MOCK_SAMPLER, &number);
	objects[number].filter = pCreateInfo->magFilter;
	objects[number].address = pCreateInfo->addressModeU;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(sampler, MOCK_SAMPLER);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
	uint32_t index;
	VkImageLayout layout;

	(void)device;
	(void)pAllocator;
	for (index = 0; index < pCreateInfo->attachmentCount; index++) {
		layout = pCreateInfo->pAttachments[index].finalLayout;
		if (layout != VK_IMAGE_LAYOUT_GENERAL && layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
		    layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
		    layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
			mock.bad_final_layouts++;
	}
	*pRenderPass = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)renderPass;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pFramebuffer = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)framebuffer;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pSemaphore = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)semaphore;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pFence = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)fence;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pCommandPool = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)commandPool;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers)
{
	(void)device;
	(void)pAllocateInfo;
	pCommandBuffers[0] = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
	(void)device;
	(void)pCreateInfo;
	(void)pAllocator;
	*pSetLayout = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)descriptorSetLayout;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
	(void)device;
	(void)pAllocator;
	if (pCreateInfo->maxSets < 7U)
		fprintf(stderr, "mock: a descriptor pool of %u sets\n", pCreateInfo->maxSets);
	*pDescriptorPool = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)descriptorPool;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets)
{
	uint32_t index;

	(void)device;
	for (index = 0; index < pAllocateInfo->descriptorSetCount; index++)
		pDescriptorSets[index] = mock_new(MOCK_SET, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies)
{
	uint32_t index;

	(void)device;
	(void)descriptorCopyCount;
	(void)pDescriptorCopies;
	for (index = 0; index < descriptorWriteCount; index++) {
		mock_get(pDescriptorWrites[index].dstSet)->view = mock_number(pDescriptorWrites[index].pImageInfo->imageView);
		mock_get(pDescriptorWrites[index].dstSet)->sampler = mock_number(pDescriptorWrites[index].pImageInfo->sampler);
	}
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
	(void)device;
	(void)pAllocator;
	if (pCreateInfo->pushConstantRangeCount != 1U || pCreateInfo->pPushConstantRanges[0].size != 32U)
		fprintf(stderr, "mock: push constants of %u bytes\n", pCreateInfo->pPushConstantRanges[0].size);
	*pPipelineLayout = mock_new(MOCK_OTHER, NULL);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pipelineLayout;
	(void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule)
{
	size_t number;

	(void)device;
	(void)pAllocator;
	*pShaderModule = mock_new(MOCK_MODULE, &number);
	objects[number].code = pCreateInfo->pCode;
	objects[number].code_size = pCreateInfo->codeSize;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(shaderModule, MOCK_MODULE);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
	const VkPipelineColorBlendAttachmentState *blend;
	const struct mock_object *fragment;
	struct mock_object *object;
	size_t number;

	(void)device;
	(void)pipelineCache;
	(void)createInfoCount;
	(void)pAllocator;
	fragment = mock_get(pCreateInfos[0].pStages[1].module);
	blend = &pCreateInfos[0].pColorBlendState->pAttachments[0];
	if (mock.refuse_grain && mock_same_code(fragment->code, fragment->code_size, mock.grain_code, mock.grain_size)) {
		mock.pipelines_refused++;
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (mock.refuse_multiply && blend->dstColorBlendFactor == VK_BLEND_FACTOR_SRC_COLOR) {
		mock.pipelines_refused++;
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	pPipelines[0] = mock_new(MOCK_PIPELINE, &number);
	object = &objects[number];
	object->fragment = fragment->code;
	object->fragment_size = fragment->code_size;
	object->src_color = blend->srcColorBlendFactor;
	object->dst_color = blend->dstColorBlendFactor;
	object->src_alpha = blend->srcAlphaBlendFactor;
	object->dst_alpha = blend->dstAlphaBlendFactor;
	object->write_mask = blend->colorWriteMask;
	object->stencil = (int)pCreateInfos[0].pDepthStencilState->stencilTestEnable;
	object->compare = pCreateInfos[0].pDepthStencilState->front.compareOp;
	object->pass_op = pCreateInfos[0].pDepthStencilState->front.passOp;
	mock.pipelines_made++;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator)
{
	(void)device;
	(void)pAllocator;
	mock_free(pipeline, MOCK_PIPELINE);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkDeviceWaitIdle(VkDevice device)
{
	(void)device;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
	(void)device;
	(void)swapchain;
	(void)timeout;
	(void)semaphore;
	(void)fence;
	*pImageIndex = 0U;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
	(void)commandBuffer;
	(void)flags;
	mock.draw_count = 0;
	mock.passes = 0;
	bound_pipeline = 0;
	bound_buffer = 0;
	bound_set = 0;
	memset(pushed, 0, sizeof(pushed));
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
	(void)commandBuffer;
	(void)pBeginInfo;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
	(void)commandBuffer;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
	(void)commandBuffer;
	(void)srcStageMask;
	(void)dstStageMask;
	(void)dependencyFlags;
	(void)memoryBarrierCount;
	(void)pMemoryBarriers;
	(void)bufferMemoryBarrierCount;
	(void)pBufferMemoryBarriers;
	uint32_t index;
	struct mock_object *image;

	for (index = 0; index < imageMemoryBarrierCount; index++) {
		image = mock_get(pImageMemoryBarriers[index].image);
		if (image != NULL && image->kind == MOCK_IMAGE)
			image->layout = pImageMemoryBarriers[index].newLayout;
	}
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents)
{
	(void)commandBuffer;
	(void)pRenderPassBegin;
	(void)contents;
	mock.passes++;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
	(void)commandBuffer;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
	(void)commandBuffer;
	(void)pipelineBindPoint;
	bound_pipeline = mock_number(pipeline);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets)
{
	(void)commandBuffer;
	(void)firstBinding;
	(void)bindingCount;
	if (pOffsets[0] != 0U)
		fprintf(stderr, "mock: a vertex buffer bound at an offset\n");
	bound_buffer = mock_number(pBuffers[0]);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets)
{
	(void)commandBuffer;
	(void)pipelineBindPoint;
	(void)layout;
	(void)firstSet;
	(void)descriptorSetCount;
	(void)dynamicOffsetCount;
	(void)pDynamicOffsets;
	bound_set = mock_number(pDescriptorSets[0]);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues)
{
	(void)commandBuffer;
	(void)layout;
	(void)stageFlags;
	if (offset + size > sizeof(pushed) || offset % 4U != 0U || size % 4U != 0U) {
		fprintf(stderr, "mock: push constants at %u of %u bytes\n", offset, size);
		exit(2);
	}
	memcpy((unsigned char *)pushed + offset, pValues, size);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
	struct mock_object *image;
	struct mock_object *buffer;
	struct mock_object *memory;

	(void)commandBuffer;
	image = mock_get(srcImage);
	buffer = mock_get(dstBuffer);
	if (image == NULL || image->kind != MOCK_IMAGE || buffer == NULL || buffer->kind != MOCK_BUFFER || regionCount != 1U ||
	    srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || image->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
	    (image->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0U ||
	    pRegions[0].imageExtent.width != image->width || pRegions[0].imageExtent.height != image->height) {
		fprintf(stderr, "mock: a copy of an image to a buffer that does not fit\n");
		exit(2);
	}
	memory = &objects[buffer->memory];
	if (memory->bytes == NULL || memory->size < (size_t)image->width * image->height * 4U) {
		fprintf(stderr, "mock: a copy to a buffer the host does not see or too small\n");
		exit(2);
	}
	mock.copies++;
	if (mock.copy_hook != NULL)
		mock.copy_hook(memory->bytes, image->width, image->height);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports)
{
	(void)commandBuffer;
	(void)firstViewport;
	(void)viewportCount;
	(void)pViewports;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors)
{
	(void)commandBuffer;
	(void)firstScissor;
	(void)scissorCount;
	(void)pScissors;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	struct mock_draw *larger;
	struct mock_draw *draw;
	size_t capacity;

	(void)commandBuffer;
	(void)instanceCount;
	(void)firstInstance;
	if (mock.draw_count == mock.draw_capacity) {
		capacity = mock.draw_capacity * 2U;
		if (capacity < 1024U)
			capacity = 1024U;
		larger = realloc(mock.draws, capacity * sizeof(*larger));
		if (larger == NULL) {
			fprintf(stderr, "mock: out of memory\n");
			exit(2);
		}
		mock.draws = larger;
		mock.draw_capacity = capacity;
	}
	draw = &mock.draws[mock.draw_count];
	draw->pipeline = bound_pipeline;
	draw->buffer = bound_buffer;
	draw->set = bound_set;
	draw->first = firstVertex;
	draw->count = vertexCount;
	memcpy(draw->push, pushed, sizeof(pushed));
	draw->pass = mock.passes;
	mock.draw_count++;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences)
{
	(void)device;
	(void)fenceCount;
	(void)pFences;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence)
{
	(void)queue;
	(void)submitCount;
	(void)pSubmits;
	(void)fence;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
	(void)queue;
	(void)pPresentInfo;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout)
{
	(void)device;
	(void)fenceCount;
	(void)pFences;
	(void)waitAll;
	(void)timeout;
	return VK_SUCCESS;
}
