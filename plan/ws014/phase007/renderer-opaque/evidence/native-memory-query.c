#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    VkPhysicalDevice physical[8];
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    uint32_t count = 8;
    uint32_t device, type;
    if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS)
        return 1;
    if (vkEnumeratePhysicalDevices(instance, &count, physical) != VK_SUCCESS)
        return 1;
    for (device = 0; device < count; device++) {
        vkGetPhysicalDeviceProperties(physical[device], &properties);
        vkGetPhysicalDeviceMemoryProperties(physical[device], &memory);
        printf("DEVICE name=%s types=%u heaps=%u\n", properties.deviceName, memory.memoryTypeCount, memory.memoryHeapCount);
        for (type = 0; type < memory.memoryTypeCount; type++)
            printf("MEMORY type=%u flags=0x%x heap=%u\n", type, memory.memoryTypes[type].propertyFlags, memory.memoryTypes[type].heapIndex);
    }
    vkDestroyInstance(instance, NULL);
    return 0;
}
