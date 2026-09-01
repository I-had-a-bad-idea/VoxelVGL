#include "VGL/renderer.h"

void Renderer::create_logical_device(bool wireframe) {
    const std::vector<const char*> device_extensions {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceVulkan12Features enabled_vk_12_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true
    };
    VkPhysicalDeviceVulkan13Features enabled_vk_13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabled_vk_12_features,
        .synchronization2 = true,
        .dynamicRendering = true,
    };
    VkPhysicalDeviceFeatures enabled_vk_10_features{
        .fillModeNonSolid = (wireframe) ? VK_TRUE : VK_FALSE,
        .samplerAnisotropy = VK_TRUE
    };

    // create logical device

    VkDeviceCreateInfo device_CI {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_vk_13_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info.queue_CI,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &enabled_vk_10_features
    };
    vk_check(vkCreateDevice(physical_devices[device_index], &device_CI, nullptr, &device));
}


VkQueue Renderer::get_device_queue(uint32_t queue_family) {
    VkQueue queue {VK_NULL_HANDLE};
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    return queue;
}