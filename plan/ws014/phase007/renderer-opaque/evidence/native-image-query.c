#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>

static void query(VkPhysicalDevice physical, VkFormat format, VkImageTiling tiling, VkExternalMemoryHandleTypeFlagBits type)
{
    VkPhysicalDeviceExternalImageFormatInfo external;
    VkPhysicalDeviceImageFormatInfo2 input;
    VkExternalImageFormatProperties properties;
    VkImageFormatProperties2 output;
    VkResult result;
    memset(&external, 0, sizeof(external));
    external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external.handleType = type;
    memset(&input, 0, sizeof(input));
    input.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    input.pNext = &external;
    input.format = format;
    input.type = VK_IMAGE_TYPE_2D;
    input.tiling = tiling;
    input.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    memset(&output, 0, sizeof(output));
    output.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    output.pNext = &properties;
    result = vkGetPhysicalDeviceImageFormatProperties2(physical, &input, &output);
    printf("IMAGE format=%u tiling=%u usage=%u handle=0x%x result=%d features=0x%x compatible=0x%x export_from_imported=0x%x extent=%ux%ux%u mips=%u layers=%u samples=0x%x resource_bytes=%llu\n",
        input.format, input.tiling, input.usage, type, result,
        properties.externalMemoryProperties.externalMemoryFeatures,
        properties.externalMemoryProperties.compatibleHandleTypes,
        properties.externalMemoryProperties.exportFromImportedHandleTypes,
        output.imageFormatProperties.maxExtent.width,
        output.imageFormatProperties.maxExtent.height,
        output.imageFormatProperties.maxExtent.depth,
        output.imageFormatProperties.maxMipLevels,
        output.imageFormatProperties.maxArrayLayers,
        output.imageFormatProperties.sampleCounts,
        (unsigned long long)output.imageFormatProperties.maxResourceSize);
}

int main(void)
{
    VkApplicationInfo app;
    VkInstanceCreateInfo info;
    VkInstance instance;
    VkPhysicalDevice physical[8];
    VkPhysicalDeviceProperties properties;
    uint32_t count = 8;
    uint32_t index;
    VkResult result;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_1;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    result = vkCreateInstance(&info, NULL, &instance);
    printf("INSTANCE result=%d\n", result);
    if (result != VK_SUCCESS)
        return 1;
    result = vkEnumeratePhysicalDevices(instance, &count, physical);
    printf("ENUM result=%d count=%u\n", result, count);
    if (result != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    for (index = 0; index < count; index++) {
        vkGetPhysicalDeviceProperties(physical[index], &properties);
        printf("DEVICE index=%u name=%s vendor=0x%x device=0x%x api=%u driver=%u\n",
            index, properties.deviceName, properties.vendorID, properties.deviceID,
            properties.apiVersion, properties.driverVersion);
        query(physical[index], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
        query(physical[index], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
        query(physical[index], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
        query(physical[index], VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    }
    vkDestroyInstance(instance, NULL);
    return 0;
}
