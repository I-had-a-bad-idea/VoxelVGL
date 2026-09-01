#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <array>

#include "VGL/vk_check.hpp"
#include "VGL/sdl.hpp"
#include "VGL/volk.hpp"
#include "VGL/object.h"
#include "VGL/math.h"

struct PushConstants {
    VkDeviceAddress shader_data_addrress;
    float time;
};

class Scene {
    public:
        glm::vec3 cam_pos {0.0f, 0.0f, -6.0f};
        glm::vec3 cam_rot {0.0f, 0.0f, 0.0f};
        glm::quat cam_orientation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 light_pos {0.0f, 100.0f, 10.0f};
        glm::vec4 clear_color {0.0f, 0.0f, 0.0f, 0.0f};

        float fovy = 45.0f;

        float near_plane {0.1f};
        float far_plane {32.0f};

        std::unordered_map<Shader*, std::unordered_map<Mesh*, std::vector<Object*>>> objects_by_mesh_by_shader;

        void add_object_to_scene(Object* object);
        void remove_object_from_scene(Object* object);

        glm::mat4 view_matrix() const; 
};

class Renderer {
public:
    static constexpr uint32_t max_frames_in_flight = 2;
    static constexpr uint32_t max_textures = 4096;
    static constexpr uint32_t max_objects = 4096;

    Renderer(std::string name, int w, int h, bool capture_mouse, SDL_Window* window = nullptr, bool wireframe = false, uint32_t preferred_device_index = UINT32_MAX);

    Mesh load_mesh(std::string filepath);
    Mesh load_mesh(MeshData mesh_data);
    Texture load_texture(std::string filepath);
    Shader load_shader(std::string filepath);

    void render_scene(const Scene& scene);

    void destroy_mesh(Mesh mesh);
    void destroy_texture(Texture texture);
    void destroy_shader(Shader shader);

    void destroy();

private:

    struct GlobalData {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos;
    };
    struct ObjectData {
        glm::mat4 model;
        uint32_t texture_index;
        uint32_t selected;
    };

    struct ShaderData {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos{ 0.0f, 100.0f, 10.0f, 0.0f };
        std::array<ObjectData, max_objects> objects;
    };

    struct ShaderDataBuffer {
        VmaAllocation allocation {VK_NULL_HANDLE};
        VmaAllocationInfo allocation_info {};
        VkBuffer buffer {VK_NULL_HANDLE};
        VkDeviceAddress deviceAddress {};
    };

    struct QueueInfo {
        uint32_t queue_family;
        VkDeviceQueueCreateInfo queue_CI;
    };

    struct PhysicalDevice {
        uint32_t device_index;
        VkPhysicalDeviceProperties2 device_properties;
    };

    struct WindowData {
        VkSurfaceKHR surface;
        int x;
        int y;
    };

    struct SwapchainData {
        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
    };

private:
    // Instance helpers
    VkInstance get_vulkan_instance(std::string app_name);

    // Physical device
    std::vector<VkPhysicalDevice> get_devices();
    PhysicalDevice select_device(uint32_t preferred_device_index = UINT32_MAX);

    // Queues
    std::vector<VkQueueFamilyProperties> get_queue_families();
    void get_queue_family_with_graphics_support(std::vector<VkQueueFamilyProperties> queue_families, bool check_presentation_support = true);

    // Logical device
    void create_logical_device(bool wireframe);
    VkQueue get_device_queue(uint32_t queue_family);

    // Window / Surface
    SDL_Window* create_window(std::string name, int w, int h, bool capture_mouse, SDL_Window* window = nullptr);
    WindowData create_vulkan_surface_for_window();
    VkSurfaceCapabilitiesKHR get_surface_properties();
    VkSwapchainKHR create_swapchain(VkSurfaceCapabilitiesKHR surface_caps);
    SwapchainData get_swapchain_data();

    // Depth attachment
    VkFormat get_depth_format();
    void create_depth_image_and_depth_image_view();

    // Memory
    void setup_vma();

    // Shaders
    void create_shader_buffers();

    // Sync objects
    void create_sync_objects();
    
    // Command pool and buffers
    void create_command_buffers();

    // Texture descriptors
    void create_texture_descriptors();
    void update_texture_descriptor(uint32_t index, Texture& texture);

    // Shaders
    void create_slang_session();

    // Rendering pipeline
    void create_graphics_pipeline(Shader& shader, bool wireframe);

    inline void vk_check_swapchain(VkResult result);
    void update_swapchain();


private:
    bool wireframe = false;

    uint32_t image_index {0};
    uint32_t frame_index {0};
    bool swapchain_update_required {false};

    VkInstance instance = VK_NULL_HANDLE;

    std::vector<VkPhysicalDevice> physical_devices;
    uint32_t device_index = 0;
    PhysicalDevice physical_device;

    VkDevice device = VK_NULL_HANDLE;
    QueueInfo queue_info;
    VkQueue graphics_queue = VK_NULL_HANDLE;

    SDL_Window* window = nullptr;
    WindowData window_data;
    VkSurfaceCapabilitiesKHR surface_caps;
    VkSwapchainCreateInfoKHR swapchain_CI;
    uint32_t image_count {0};
    VkSemaphoreCreateInfo semaphore_CI;
    VkImageCreateInfo depth_image_CI;

    VkFormat image_format;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    SwapchainData swapchain_data;

    VmaAllocator allocator = VK_NULL_HANDLE;

    VkFormat depth_format;
    VkImage depth_image;
    VmaAllocation depth_image_allocation;
    VkImageView depth_image_view;

    ShaderData shader_data {};
    std::array<ShaderDataBuffer, max_frames_in_flight> shader_data_buffers;

    std::array<VkFence, max_frames_in_flight> fences;
    std::array<VkSemaphore, max_frames_in_flight> image_acquired_semaphores;
    std::vector<VkSemaphore> render_complete_semaphores;

    VkCommandPool command_pool {VK_NULL_HANDLE};
    std::array<VkCommandBuffer, max_frames_in_flight> command_buffers;

    std::vector<Texture> textures;

    VkDescriptorPool descriptor_pool {VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout_tex {VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set_tex {VK_NULL_HANDLE};

    Slang::ComPtr<slang::IGlobalSession> slang_global_session;
    Slang::ComPtr<slang::ISession> slang_session;

};


