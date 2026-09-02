#include "VGL/object.h"

Mesh::Mesh(std::string path) {
    std::cerr << "Loading from obj files is not supported using this fork!" << std::endl;
    exit(EXIT_FAILURE);
}

Mesh::Mesh(MeshData mesh_data) {
    data = std::move(mesh_data);

    index_count = static_cast<VkDeviceSize>(data.indices.size());
}

void Mesh::load_mesh_into_buffer(VmaAllocator allocator) {
    // std::cout
    //     << "vertices: " << data.vertices.size()
    //     << " (" << data.vertices.size() * sizeof(Vertex) << " bytes)\n"
    //     << "indices: " << data.indices.size()
    //     << " (" << data.indices.size() * sizeof(uint32_t) << " bytes)\n"
    //     << "total: "
    //     << data.vertices.size() * sizeof(Vertex)
    //     + data.indices.size() * sizeof(uint32_t)
    //     << " bytes\n";


    // VmaTotalStatistics stats{};
    // vmaCalculateStatistics(allocator, &stats);

    // std::cout
    //     << "blockBytes:      " << stats.total.statistics.blockBytes << '\n'
    //     << "allocationBytes: " << stats.total.statistics.allocationBytes << '\n'
    //     << "blockCount:      " << stats.total.statistics.blockCount << '\n'
    //     << "allocationCount: " << stats.total.statistics.allocationCount << '\n';



    // std::cout << "Loading mesh into buffer...\n";

    v_buffer_size = VkDeviceSize {sizeof(Vertex) * data.vertices.size()};
    VkDeviceSize i_buffer_size {sizeof(uint32_t) * data.indices.size()};

    if (v_buffer_size == 0 || i_buffer_size == 0) {
        std::cerr << "Error: Mesh has no vertices or indices.\n";
        return;
    }

    VkBufferCreateInfo buffer_CI {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = v_buffer_size + i_buffer_size, // since we combine both into one buffer
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT // tell GPU, that we are combining the buffers
    };

    VmaAllocationCreateInfo v_buffer_alloc_CI {
        // make sure we get memory, that on the GPU (in VRAM) and accessible by host
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VmaAllocationInfo v_buffer_alloc_info {};
    vk_check(vmaCreateBuffer(allocator, &buffer_CI, &v_buffer_alloc_CI, &v_buffer, &v_buffer_allocation, &v_buffer_alloc_info));

    
    // copy data into buffer
    memcpy(v_buffer_alloc_info.pMappedData, data.vertices.data(), v_buffer_size);
    memcpy(((char*)v_buffer_alloc_info.pMappedData) + v_buffer_size, data.indices.data(), i_buffer_size);
}

void Mesh::destroy(VmaAllocator allocator) {
    vmaDestroyBuffer(allocator, v_buffer, v_buffer_allocation);
}