#include "AllocatorState.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {
    std::tuple<vk::BufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage> AllocatorState::GetBufferFlags(BufferType type)
    {
        switch (type) {
            case BufferType::Staging:
                return std::make_tuple(
                    vk::BufferUsageFlagBits::eTransferSrc,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_HOST
                    );
            case BufferType::Vertex:
                return std::make_tuple(
                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
                    0,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                    );
            case BufferType::Uniform:
                return std::make_tuple(
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_HOST
                    );
        }
        __builtin_unreachable();
    }

    void AllocatorState::RaiseException(VkResult result)
    {
    }

    AllocatorState::~AllocatorState()
    {
        vmaDestroyAllocator(m_allocator);
    }
    void AllocatorState::Create(std::shared_ptr<RenderSystem> system)
    {
        m_system = system;

        VmaAllocatorCreateInfo info;
        info.device = system->getDevice();
        info.physicalDevice = system->GetPhysicalDevice();
        info.instance = system->getInstance();
        info.vulkanApiVersion = vk::ApiVersion13;

        vmaDestroyAllocator(m_allocator);
        vmaCreateAllocator(&info, &m_allocator);
        assert(m_allocator && "Failed to create allocator.");
    }

    VmaAllocator AllocatorState::GetAllocator() const
    {
        return m_allocator;
    }

    AllocatedMemory AllocatorState::AllocateBuffer(BufferType type, size_t size) const
    {
        assert(m_allocator && "Allocated not initalized.");
        auto [busage, flags, musage] = GetBufferFlags(type);

        VkBufferCreateInfo bcinfo {};
        bcinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bcinfo.size = size;
        bcinfo.usage = static_cast<VkBufferUsageFlags>(busage);

        VmaAllocationCreateInfo ainfo = {};
        ainfo.flags = flags;
        ainfo.usage = musage;
        
        VkBuffer buffer {};
        VmaAllocation allocation{};

        VkResult result = vmaCreateBuffer(m_allocator, &bcinfo, &ainfo, &buffer, &allocation, nullptr);
        RaiseException(result);
        assert(buffer != nullptr && allocation != nullptr);
        return AllocatedMemory(static_cast<vk::Buffer>(buffer), allocation, m_allocator);
    }

    std::unique_ptr<AllocatedMemory> AllocatorState::AllocateBufferUnique(BufferType type, size_t size) const
    {
        assert(m_allocator && "Allocated not initalized.");
        auto [busage, flags, musage] = GetBufferFlags(type);

        VkBufferCreateInfo bcinfo {};
        bcinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bcinfo.size = size;
        bcinfo.usage = static_cast<VkBufferUsageFlags>(busage);

        VmaAllocationCreateInfo ainfo = {};
        ainfo.flags = flags;
        ainfo.usage = musage;
        
        VkBuffer buffer {};
        VmaAllocation allocation{};

        VkResult result = vmaCreateBuffer(m_allocator, &bcinfo, &ainfo, &buffer, &allocation, nullptr);
        RaiseException(result);
        assert(buffer != nullptr && allocation != nullptr);
        return std::make_unique<AllocatedMemory>(static_cast<vk::Buffer>(buffer), allocation, m_allocator);
    }
}
