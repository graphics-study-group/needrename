#ifndef RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED
#define RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED

#include <vulkan/vulkan.hpp>
#include <memory>

namespace Engine{

    class RenderSystem;

    /// @brief A buffer with allocated memory, which could be on device or host.
    /// TODO: Rewrite with a Vulkan allocator library
    class Buffer {
    public:

        enum class BufferType {
            // Staging buffer on host memory
            Staging,
            // Vertex buffer on device memory
            Vertex,
            // Uniform buffer on host memory
            Uniform
        };

        Buffer (std::weak_ptr <RenderSystem> system);

        /// @brief Create a buffer, and perform allocation if needed.
        /// @param type 
        /// @param size 
        void Create(BufferType type, size_t size);

        vk::Buffer GetBuffer () const;
        vk::DeviceMemory GetMemory () const;

        size_t GetSize() const;

        std::byte * Map() const;
        void Unmap() const;
    
    protected:
        size_t m_size {0ULL};

        [[maybe_unused]]
        size_t m_offset {0ULL};

        std::weak_ptr <RenderSystem> m_system;
        vk::UniqueBuffer m_buffer {};
        vk::UniqueDeviceMemory m_memory {};

        static vk::MemoryPropertyFlags GetMemoryProperty(BufferType type);
        static vk::BufferUsageFlags GetBufferUsage(BufferType type);
    };
}

#endif // RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED
