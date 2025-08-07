#include "AllocatorState.h"

#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {
    std::tuple<vk::BufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage> constexpr AllocatorState::GetBufferFlags(
        BufferType type
    ) {
        switch (type) {
        case BufferType::Staging:
            return std::make_tuple(
                vk::BufferUsageFlagBits::eTransferSrc,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST
            );
        case BufferType::Vertex:
            return std::make_tuple(
                vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
                    | vk::BufferUsageFlagBits::eVertexBuffer,
                0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            );
        case BufferType::Uniform:
            return std::make_tuple(
                vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST
            );
        }
        return std::make_tuple(vk::BufferUsageFlags{}, 0, VMA_MEMORY_USAGE_AUTO);
    }
    AllocatorState::AllocatorState(RenderSystem &system) : m_system(system) {
    }
    AllocatorState::~AllocatorState() {
        vmaDestroyAllocator(m_allocator);
    }
    void AllocatorState::Create() {
        VmaAllocatorCreateInfo info{};
        info.device = m_system.getDevice();
        info.physicalDevice = m_system.GetPhysicalDevice();
        info.instance = m_system.getInstance();
        info.vulkanApiVersion = vk::ApiVersion13;

        vmaDestroyAllocator(m_allocator);
        vmaCreateAllocator(&info, &m_allocator);
        assert(m_allocator && "Failed to create allocator.");
    }

    VmaAllocator AllocatorState::GetAllocator() const {
        return m_allocator;
    }

    AllocatedMemory AllocatorState::AllocateBuffer(BufferType type, size_t size, const std::string &name) const {
        assert(m_allocator && "Allocated not initalized.");
        auto [busage, flags, musage] = GetBufferFlags(type);

        VkBufferCreateInfo bcinfo{};
        bcinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bcinfo.size = size;
        bcinfo.usage = static_cast<VkBufferUsageFlags>(busage);

        VmaAllocationCreateInfo ainfo = {};
        ainfo.flags = flags;
        ainfo.usage = musage;

        VkBuffer buffer{};
        VmaAllocation allocation{};

        VkResult result = vmaCreateBuffer(m_allocator, &bcinfo, &ainfo, &buffer, &allocation, nullptr);
        vk::detail::resultCheck(vk::Result{result}, "Failed to create buffer.");
        assert(buffer != nullptr && allocation != nullptr);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), static_cast<vk::Buffer>(buffer), name);
        return AllocatedMemory(static_cast<vk::Buffer>(buffer), allocation, m_allocator);
    }

    std::unique_ptr<AllocatedMemory> AllocatorState::AllocateBufferUnique(
        BufferType type, size_t size, const std::string &name
    ) const {
        return std::make_unique<AllocatedMemory>(AllocateBuffer(type, size, name));
    }

    AllocatedMemory AllocatorState::AllocateImage(
        ImageUtils::ImageType type, VkExtent3D dimension, VkFormat format, const std::string &name
    ) const {
        return AllocateImageEx(type, dimension, format, 1, 1, name);
    }

    std::unique_ptr<AllocatedMemory> AllocatorState::AllocateImageUnique(
        ImageUtils::ImageType type, VkExtent3D dimension, VkFormat format, const std::string &name
    ) const {
        return std::make_unique<AllocatedMemory>(AllocateImage(type, dimension, format, name));
    }

    AllocatedMemory AllocatorState::AllocateImageEx(
        ImageUtils::ImageType type,
        VkExtent3D dimension,
        VkFormat format,
        uint32_t miplevel,
        uint32_t array_layers,
        const std::string &name
    ) const {
        const auto [iusage, musage] = ImageUtils::GetImageFlags(type);
        VkImageCreateInfo iinfo{};
        iinfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        iinfo.imageType = static_cast<VkImageType>(ImageUtils::GetVkTypeFromExtent(dimension));
        iinfo.format = format;
        iinfo.extent = dimension;
        iinfo.mipLevels = miplevel;
        iinfo.arrayLayers = array_layers;
        iinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        iinfo.usage = static_cast<VkImageUsageFlags>(iusage);
        iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        iinfo.queueFamilyIndexCount = 0;
        iinfo.pQueueFamilyIndices = nullptr;
        iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ainfo{};
        ainfo.usage = musage;

        VkImage image;
        VmaAllocation allocation;
        vmaCreateImage(m_allocator, &iinfo, &ainfo, &image, &allocation, nullptr);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), static_cast<vk::Image>(image), name);
        return AllocatedMemory(static_cast<vk::Image>(image), allocation, m_allocator);
    }
    std::unique_ptr<AllocatedMemory> AllocatorState::AllocateImageUniqueEx(
        ImageUtils::ImageType type,
        vk::ImageType dimension,
        vk::Extent3D extent,
        vk::Format format,
        uint32_t miplevel,
        uint32_t array_layers,
        vk::SampleCountFlagBits samples,
        const std::string &name
    ) const {
        const auto [iusage, musage] = ImageUtils::GetImageFlags(type);
        // VkImageCreateInfo iinfo {};
        vk::ImageCreateInfo iinfo{
            vk::ImageCreateFlags{0U},
            dimension,
            format,
            extent,
            miplevel,
            array_layers,
            samples,
            vk::ImageTiling::eOptimal,
            iusage,
            vk::SharingMode::eExclusive,
            {},
            vk::ImageLayout::eUndefined,
            nullptr
        };
        VkImageCreateInfo iinfo2 = static_cast<VkImageCreateInfo>(iinfo);

        VmaAllocationCreateInfo ainfo{};
        ainfo.usage = musage;

        VkImage image;
        VmaAllocation allocation;
        vmaCreateImage(m_allocator, &iinfo2, &ainfo, &image, &allocation, nullptr);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), static_cast<vk::Image>(image), name);
        return std::make_unique<AllocatedMemory>(
            AllocatedMemory(static_cast<vk::Image>(image), allocation, m_allocator)
        );
    }
} // namespace Engine::RenderSystemState
