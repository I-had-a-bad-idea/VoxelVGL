#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <tinyobj/tiny_obj_loader.h>
#include <ktx.h>
#include <ktxvulkan.h>
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "VGL/vk_check.hpp"
#include "VGL/math.h"

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::uvec2 atlas_tile;
};

class MeshData {
    public:
        std::vector<Vertex> vertices {};
        std::vector<uint32_t> indices {};
};

class Mesh {
    public:
        Mesh(std::string filepath);
        Mesh(MeshData mesh_data);

        MeshData data;

        VkBuffer v_buffer {VK_NULL_HANDLE};
        VkDeviceSize v_buffer_size;
        VkDeviceSize index_count;

        void load_mesh_into_buffer(VmaAllocator allocator);

        void destroy(VmaAllocator allocator);

    private:

        VmaAllocation v_buffer_allocation{ VK_NULL_HANDLE };
        
};

class Texture {
    public:
        Texture(std::string filepath);

    	VkImage image {VK_NULL_HANDLE};
	    VkImageView view {VK_NULL_HANDLE};
	    VkSampler sampler {VK_NULL_HANDLE};
        uint32_t descriptor_index;

        void load_image_into_buffer(VkPhysicalDevice physical_device, VkDevice device, VkCommandPool command_pool, VkQueue graphics_queue, VmaAllocator allocator);

        void destroy(VkDevice device, VmaAllocator allocator);

    private:
    	VmaAllocation allocation {VK_NULL_HANDLE};
        ktxTexture* ktx_texture {nullptr};
};

struct GraphicsPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

class Shader {
    public:
        GraphicsPipeline graphics_pipeline;
        VkShaderModule shader_module {};

        Shader(std::string filepath, VkDevice device, Slang::ComPtr<slang::ISession> slang_session);     
        
        void destroy(VkDevice device);
    
};

class Material {
    public:
        Texture* albedo;
        Shader* shader;
        Material(Texture* texture, Shader* shader);
};

class Object {
    public:
        Mesh* mesh;
        Material* material;
        glm::vec3 position;
        glm::vec3 rotation;

        bool visible = true;

        uint32_t selected;
        Object(Mesh* mesh, Material* material, glm::vec3 position, glm::vec3 rotation);

        glm::mat4 model_matrix() const;

};
