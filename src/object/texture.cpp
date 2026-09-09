#include "VGL/object.h"


Texture::Texture(std::string path) {
    // if no exist throw error
    if (!std::filesystem::exists(path)) {
        std::cerr << "File does not exist: " << path << std::endl;
        exit(EXIT_FAILURE);
    }
    ktxTexture_CreateFromNamedFile(path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture); // load texture from file
}

void Texture::load_image_into_buffer(VkPhysicalDevice physical_device, VkDevice device, VkCommandPool command_pool, VkQueue graphics_queue, VmaAllocator allocator) {
    VkFormat format = ktxTexture_GetVkFormat(ktx_texture);

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);

    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) { // if format is unsuported
        format = VK_FORMAT_R8G8B8A8_SRGB; // fallback (should be common)
    }
    
    
    VkImageCreateInfo texture_image_CI {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent {.width = ktx_texture->baseWidth, .height = ktx_texture->baseHeight, .depth = 1},
        .mipLevels = ktx_texture->numLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // load from disk to this image + want to sample it from a shader
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo texture_image_alloc_CI {.usage = VMA_MEMORY_USAGE_AUTO};
    vk_check(vmaCreateImage(allocator, &texture_image_CI, &texture_image_alloc_CI, &image, &allocation, nullptr));

    VkImageViewCreateInfo texture_view_CI {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = texture_image_CI.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktx_texture->numLevels, .layerCount = 1}
    };
    vk_check(vkCreateImageView(device, &texture_view_CI, nullptr, &view));

    // create a temp buffer to copy the image into
    // Then issue a command to the GPU to coppy this buffer to the image, which then does a conversion to the hardware-specific layout
    VkBuffer image_src_buffer {};
    VmaAllocation image_src_allocation {};
    VkBufferCreateInfo image_src_buffer_CI {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (uint32_t)ktx_texture->dataSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo image_src_alloc_CI {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VmaAllocationInfo image_src_allocation_info{};
    vk_check(vmaCreateBuffer(allocator, &image_src_buffer_CI, &image_src_alloc_CI, &image_src_buffer, &image_src_allocation, &image_src_allocation_info));
    memcpy(image_src_allocation_info.pMappedData, ktx_texture->pData, ktx_texture->dataSize);


    // now copy image data from buffer to optimal tiles image on GPU
    VkFenceCreateInfo fence_onetime_CI {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    VkFence fence_onetime {};
    vk_check(vkCreateFence(device, &fence_onetime_CI, nullptr, &fence_onetime));
    
    VkCommandBuffer cb_onetime;
    VkCommandBufferAllocateInfo cb_onetime_AI {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .commandBufferCount = 1
    };
    vk_check(vkAllocateCommandBuffers(device, &cb_onetime_AI, &cb_onetime));

    VkCommandBufferBeginInfo cb_onetime_BI {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vk_check(vkBeginCommandBuffer(cb_onetime, &cb_onetime_BI));
    

    // First transition all mip levels of the texture omage to a layout, that allows to transfer data to it
    VkImageMemoryBarrier2 barrier_texture_image {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = image,
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktx_texture->numLevels, .layerCount = 1 }
    };
    VkDependencyInfo barrier_texture_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_texture_image
    };

    vkCmdPipelineBarrier2(cb_onetime, &barrier_texture_info);
    std::vector<VkBufferImageCopy> copy_regions{};
    for (auto j = 0; j < ktx_texture->numLevels; j++) {
        ktx_size_t mipOffset{0};
        KTX_error_code ret = ktxTexture_GetImageOffset(ktx_texture, j, 0, 0, &mipOffset);
        copy_regions.push_back({
            .bufferOffset = mipOffset,
            .imageSubresource {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
            .imageExtent {.width = ktx_texture->baseWidth >> j, .height = ktx_texture->baseHeight >> j, .depth = 1 },
        });
    }
    // Copy all mip levels from temp buffer to the image
    vkCmdCopyBufferToImage(cb_onetime, image_src_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copy_regions.size()), copy_regions.data());
    
    // Transition all mip levels from transfer dest to layout readable from shader
    VkImageMemoryBarrier2 barrier_texture_read{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktx_texture->numLevels, .layerCount = 1 }
    };

    barrier_texture_info.pImageMemoryBarriers = &barrier_texture_read;
    vkCmdPipelineBarrier2(cb_onetime, &barrier_texture_info);
    vk_check(vkEndCommandBuffer(cb_onetime));

    VkCommandBufferSubmitInfo cb_onetime_submit_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cb_onetime
    };
    VkSubmitInfo2 onetime_SI{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cb_onetime_submit_info
    };
    
    // Submit command buffer to the graphics queue to execute the commands.
    vk_check(vkQueueSubmit2(graphics_queue, 1, &onetime_SI, fence_onetime));
    vk_check(vkWaitForFences(device, 1, &fence_onetime, VK_TRUE, UINT64_MAX));


    VkSamplerCreateInfo sampler_CI {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE, // enable anisotropic filter to reduce blur and aliasing
        .maxAnisotropy = 16.0f, // 16 is max quality
        .minLod = 0.0f,
        .maxLod = (float)ktx_texture->numLevels
    };
    vk_check(vkCreateSampler(device, &sampler_CI, nullptr, &sampler));

    ktxTexture_Destroy(ktx_texture);
    
}


void Texture::destroy(VkDevice device, VmaAllocator allocator) {
    vkDestroyImageView(device, view, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vmaDestroyImage(allocator, image, allocation);
}