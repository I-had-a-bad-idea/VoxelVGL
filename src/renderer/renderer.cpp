#include "VGL/renderer.h"


inline void Renderer::vk_check_swapchain(VkResult result) {
	if (result < VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			swapchain_update_required = true;
			return;
		}
		std::cerr << "Vulkan call returned an error (" << result << ")\n";
		exit(result);
	}
}

Renderer::Renderer(std::string name, int w, int h, bool capture_mouse, SDL_Window* window_, bool wireframe_, uint32_t preferred_device_index)
    : wireframe(wireframe_)
{
    std::cout << "Initializing up SDL...\n";
    init_sdl();
    std::cout << "Initializing up Volk...\n";
    init_volk();

    std::cout << "Creating Vulkan instance...\n";
    instance = get_vulkan_instance(name);
    volk_load_instance(instance);

    std::cout << "Getting physical device...\n";
    physical_devices = get_devices();
    physical_device = select_device(preferred_device_index);

    std::cout << "Getting queue families...\n";
    std::vector<VkQueueFamilyProperties> queue_families = get_queue_families();
    std::cout << "Getting queue family with graphics support...\n";
    get_queue_family_with_graphics_support(queue_families);

    std::cout << "Creating logical device...\n";
    create_logical_device(wireframe);
    std::cout << "Getting device queue...\n";
    graphics_queue = get_device_queue(queue_info.queue_family);

    std::cout << "Setting up VMA...\n";
    setup_vma();

    std::cout << "Creating window...\n";
    window = create_window(name, w, h, capture_mouse, window_);
    window_data = create_vulkan_surface_for_window();
    surface_caps = get_surface_properties();

    std::cout << "Creating swapchain...\n";
    swapchain = create_swapchain(surface_caps);
    swapchain_data = get_swapchain_data();

    std::cout << "Depth attachment...\n";
    depth_format = get_depth_format();
    create_depth_image_and_depth_image_view();

    std::cout << "Creaing shader buffers...\n";
    create_shader_buffers();

    std::cout << "Creating sync objects (fences, semaphores)...\n";
    create_sync_objects();

    std::cout << "Creating command buffers...\n";
    create_command_buffers();

    std::cout << "Creating texture descriptors...\n";
    create_texture_descriptors();

    std::cout << "Setting up Slang...\n";
    create_slang_session();

    std::cout << "Render setup completed!" << std::endl;
}

void Renderer::render_scene(const Scene& scene) {
    // Wait for fence
    vk_check(vkWaitForFences(device, 1, &fences[frame_index], true, UINT64_MAX));
    vk_check(vkResetFences(device, 1, &fences[frame_index]));
    // Acquire next swapchain image
    vk_check_swapchain(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_acquired_semaphores[frame_index], VK_NULL_HANDLE, &image_index));
    // std::cout << "Waited for fence and qcquired next swapchain image\n";
    // Update shader data
    shader_data.projection = glm::perspective(glm::radians(scene.fovy), (float)window_data.x / (float)window_data.y, scene.near_plane, scene.far_plane);
    shader_data.projection[1][1] *= -1.0f;
    // shader_data.view = glm::translate(glm::mat4(1.0f), scene.cam_pos);
    shader_data.view = scene.view_matrix();
    shader_data.lightPos = glm::vec4(scene.light_pos, 0.0f);
    uint32_t object_index = 0;
    for (const auto& [shader, meshes] : scene.objects_by_mesh_by_shader) {
        for (const auto& [mesh, objects] : meshes) {
            for (const Object* object : objects) {
                if (!object->visible) {
                    continue;
                }

                if (object_index >= max_objects) {
                    
                    size_t objects_in_scene = 0;
                    for (const auto& [shader, meshes] : scene.objects_by_mesh_by_shader) {
                        for (const auto& [mesh, objects] : meshes) {
                            objects_in_scene += objects.size();
                        }
                    }

                    std::cerr << "Warning: Maximum number of objects in scene exceeded. Max: "
                              << max_objects << ", Current: " << objects_in_scene << std::endl;
                    break;
                }

                shader_data.objects[object_index].model = object->model_matrix();
                shader_data.objects[object_index].texture_index = object->material->albedo->descriptor_index;
                shader_data.objects[object_index].selected = object->selected;
                ++object_index;
            }
        }
    }

    memcpy(shader_data_buffers[frame_index].allocation_info.pMappedData, &shader_data, sizeof(ShaderData)); // Copy shader data over
    // std::cout << "Updated shader data\n";
    // Command buffer
    auto cb = command_buffers[frame_index];
    vk_check(vkResetCommandBuffer(cb, 0));

    VkCommandBufferBeginInfo cb_BI {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vk_check(vkBeginCommandBuffer(cb, &cb_BI));

    // Issue layout transitions for swapchain and depth images
    std::array<VkImageMemoryBarrier2, 2> output_barriers {
        VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = swapchain_data.swapchain_images[image_index],
            .subresourceRange {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        },
        VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = depth_image,
            .subresourceRange {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1}
        }
            };
    VkDependencyInfo barrier_dependency_info {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = output_barriers.data()
    };
    vkCmdPipelineBarrier2(cb, &barrier_dependency_info);

    // Define how we will use attachments
    VkRenderingAttachmentInfo color_attachment_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_data.swapchain_image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue {.color{scene.clear_color.r, scene.clear_color.g, scene.clear_color.b, scene.clear_color.a}}
    };
    VkRenderingAttachmentInfo depth_attachment_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f,  0}}
    };
    
    // Begin dynamic render pass
    VkRenderingInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea {.extent{.width = static_cast<uint32_t>(window_data.x), .height = static_cast<uint32_t>(window_data.y)}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_info,
        .pDepthAttachment = &depth_attachment_info
    };
    vkCmdBeginRendering(cb, &rendering_info);

    VkViewport vp {
        .width = static_cast<uint32_t>(window_data.x),
        .height = static_cast<uint32_t>(window_data.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor {.extent{.width = static_cast<uint32_t>(window_data.x), .height = static_cast<uint32_t>(window_data.y)}};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    // std::cout << "Rendering scene with " << scene.objects.size() << " objects...\n";
    // For each shader do the pipeline
    // std::cout << "There are " << shader_to_objects.size() << " unique shaders in the scene\n";
    object_index = 0;
    for (const auto& [shader, meshes] : scene.objects_by_mesh_by_shader) {
        // std::cout << "There are " << meshes.size() << " unique meshes in the scene\n";
        // bind resources (graphics pipeline, descriptor set)
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->graphics_pipeline.pipeline);
        VkDeviceSize v_offset {0};
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->graphics_pipeline.layout, 0, 1, &descriptor_set_tex, 0, nullptr);
        
        PushConstants pc {
            .shader_data_addrress = shader_data_buffers[frame_index].deviceAddress,
            .time = static_cast<float>(SDL_GetTicks()) / 1000.0f
        };
        vkCmdPushConstants(cb, shader->graphics_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        for (const auto& [mesh, objects] : meshes) {
            // bind vertext/index buffers (bind each mesh only once)
            vkCmdBindVertexBuffers(cb, 0, 1, &mesh->v_buffer, &v_offset);
            vkCmdBindIndexBuffer(cb, mesh->v_buffer, mesh->v_buffer_size, VK_INDEX_TYPE_UINT32);
            
            uint32_t object_count = 0;
            for (const Object* object : objects) {
                if (object->visible) { // only count visible objects
                    ++object_count;
                }
            }

            if (object_count == 0) { // if no visible objects for mesh just skip
                continue;
            }

            // Finally actually draw
            vkCmdDrawIndexed(
                cb,
                mesh->index_count,
                object_count, // 3rd argument, how many of the thing we want to draw
                0,
                0,
                object_index); 
            object_index += object_count; // so that shader knows which one it is
        }
    }
    vkCmdEndRendering(cb);
    // std::cout << "Finished rendering scene\n";
    // transition swapchain image to a layout for presentation
    VkImageMemoryBarrier2 barrier_present {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchain_data.swapchain_images[image_index],
        .subresourceRange {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
    };
    VkDependencyInfo barrier_present_dependency_info {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_present
    };
    vkCmdPipelineBarrier2(cb, &barrier_present_dependency_info);

    // we dont need a barrier for the depth image, as we dont use that outside of this render pass

    vkEndCommandBuffer(cb); // end comamnd buffer


    // submit command buffer
    VkSemaphoreSubmitInfo wait_semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = image_acquired_semaphores[frame_index],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkCommandBufferSubmitInfo cb_submit_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cb
    };
    VkSemaphoreSubmitInfo signal_semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = render_complete_semaphores[image_index],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSubmitInfo2 submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait_semaphore_info,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cb_submit_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal_semaphore_info,
    };
    vk_check(vkQueueSubmit2(graphics_queue, 1, &submit_info, fences[frame_index])); // submit command buffer

    frame_index = (frame_index + 1) % max_frames_in_flight; // set frame_index for next render loop iteration

    // Present image (enque image for presentation and wait for render complete semaphore [wait for rendering to finish])
    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_complete_semaphores[image_index],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index
    };
    vk_check_swapchain(vkQueuePresentKHR(graphics_queue, &present_info)); // present image

    if (swapchain_update_required) {
        update_swapchain();
    }
}

Mesh Renderer::load_mesh(std::string filepath) {
    Mesh mesh(filepath);
    mesh.load_mesh_into_buffer(allocator);
    return mesh;
}

Mesh Renderer::load_mesh(MeshData mesh_data) {
    Mesh mesh(mesh_data);
    mesh.load_mesh_into_buffer(allocator);
    return mesh;
}

Texture Renderer::load_texture(std::string filepath) {
    Texture texture(filepath);
    texture.load_image_into_buffer(physical_devices[device_index], device, command_pool, graphics_queue, allocator);

    uint32_t id = textures.size();
    textures.push_back(texture);
    update_texture_descriptor(id, textures[id]);
    texture.descriptor_index  = id;

    return texture;
}

Shader Renderer::load_shader(std::string filepath) {
    Shader shader(filepath, device, slang_session);
    std::cout << "Creating graphics pipeline for shader...\n";
    create_graphics_pipeline(shader, wireframe);

    return shader;
}

void Renderer::destroy_mesh(Mesh mesh) {
	vk_check(vkDeviceWaitIdle(device));
    mesh.destroy(allocator);
}

void Renderer::destroy_meshes(const std::vector<Mesh> meshes) {
    if (meshes.empty()) {
        return;
    }

	vk_check(vkDeviceWaitIdle(device));
    for (Mesh mesh : meshes) {
        mesh.destroy(allocator);
    }
}

void Renderer::destroy_texture(Texture texture) {
    vk_check(vkDeviceWaitIdle(device));
    texture.destroy(device, allocator);
}
void Renderer::destroy_shader(Shader shader) {
    vk_check(vkDeviceWaitIdle(device));
    shader.destroy(device);
}


void Renderer::destroy() {
	vk_check(vkDeviceWaitIdle(device)); // make sure none of the GPU resources we want to destroy are still in use

	for (auto i = 0; i < max_frames_in_flight; i++) {
		vkDestroyFence(device, fences[i], nullptr);
		vkDestroySemaphore(device, image_acquired_semaphores[i], nullptr);
		vmaDestroyBuffer(allocator, shader_data_buffers[i].buffer, shader_data_buffers[i].allocation);
	}
	for (auto i = 0; i < render_complete_semaphores.size(); i++) {
		vkDestroySemaphore(device, render_complete_semaphores[i], nullptr);
	}

	vmaDestroyImage(allocator, depth_image, depth_image_allocation);
	vkDestroyImageView(device, depth_image_view, nullptr);
	for (auto i = 0; i < swapchain_data.swapchain_image_views.size(); i++) {
		vkDestroyImageView(device, swapchain_data.swapchain_image_views[i], nullptr);
	}

	vkDestroyDescriptorSetLayout(device, descriptor_set_layout_tex, nullptr);
	vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, window_data.surface, nullptr);
	vkDestroyCommandPool(device, command_pool, nullptr);

	vmaDestroyAllocator(allocator);
	SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();

	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
}