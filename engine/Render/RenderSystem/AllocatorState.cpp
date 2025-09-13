#include "AllocatorState.h"

#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/RenderSystem.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan_hash.hpp>

namespace Engine::RenderSystemState {
    struct AllocatorState::impl {
        VmaAllocator m_allocator{};

        std::unordered_map <vk::Format, vk::FormatProperties2> m_format_properties {};
        std::unordered_map <vk::PhysicalDeviceImageFormatInfo2, vk::ImageFormatProperties2> m_image_format_properties {};

        static const std::tuple<vk::BufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage> GetBufferFlags(
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

        const auto & UpdateFormatSupportInfo(vk::PhysicalDevice dev, vk::Format format) {
            auto image_prop_itr = m_format_properties.find(format);
            if (image_prop_itr == m_format_properties.end()) {
                auto fp = dev.getFormatProperties2(format);
                image_prop_itr = m_format_properties.insert(std::make_pair(format, fp)).first;
                SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, 
                    std::format(
                        R"(Querying format capability for {}:
    Linear tiling:  {}
    Optimal tiling: {}
    Buffer:         {}
)", 
                        to_string(format),
                        to_string(image_prop_itr->second.formatProperties.linearTilingFeatures),
                        to_string(image_prop_itr->second.formatProperties.optimalTilingFeatures),
                        to_string(image_prop_itr->second.formatProperties.bufferFeatures)
                    ).c_str()
                );
            }
            return image_prop_itr->second;
        }

        const auto & UpdateImageFormatSupportInfo(
            vk::PhysicalDevice dev,
            vk::Format format, 
            vk::ImageType dimension, 
            vk::ImageTiling tiling, 
            vk::ImageUsageFlags iusage) {
            auto pdifi = vk::PhysicalDeviceImageFormatInfo2{
                format, dimension, vk::ImageTiling::eOptimal, iusage, {}
            };
            auto image_format_prop_itr = m_image_format_properties.find(pdifi);
            if (image_format_prop_itr == m_image_format_properties.end()) {
                auto ifp = dev.getImageFormatProperties2(pdifi);
                image_format_prop_itr = m_image_format_properties.insert(std::make_pair(pdifi, ifp)).first;
                SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, 
                    std::format(
                        R"(Querying image capability for {}:
    Max extent:         {}x{}x{}
    Max mipmap:         {}
    Max array layers:   {}
    Samples:            {}
)",
                        to_string(format),
                        image_format_prop_itr->second.imageFormatProperties.maxExtent.width,
                        image_format_prop_itr->second.imageFormatProperties.maxExtent.height,
                        image_format_prop_itr->second.imageFormatProperties.maxExtent.depth,
                        image_format_prop_itr->second.imageFormatProperties.maxMipLevels,
                        image_format_prop_itr->second.imageFormatProperties.maxArrayLayers,
                        to_string(image_format_prop_itr->second.imageFormatProperties.sampleCounts)
                    ).c_str()
                );
            }

            return image_format_prop_itr->second;
        }

        /**
         * @brief Query whether an image format supports given use cases.
         * 
         * @return The first term is an integer indicating support:
         * negative if fully supported only in linear tiling,
         * zero if not supported at all, or only partially supported in all tilings,
         * positive if fully supported in optimal tiling.
         * The second item of the pair returns supported features.
         */
        std::pair<int, vk::FormatFeatureFlags> QueryFormatSupport(vk::PhysicalDevice dev, vk::Format format, ImageUtils::ImageType type) {
            const auto & s = UpdateFormatSupportInfo(dev, format);
            if (!(~s.formatProperties.optimalTilingFeatures & ImageUtils::GetFormatFeatures(type))) {
                return std::make_pair(1, s.formatProperties.optimalTilingFeatures);
            }
            if (!(~s.formatProperties.linearTilingFeatures & ImageUtils::GetFormatFeatures(type))) {
                return std::make_pair(-1, s.formatProperties.optimalTilingFeatures);
            }
            return std::make_pair(0, s.formatProperties.optimalTilingFeatures);
        }
    };

    AllocatorState::AllocatorState(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    AllocatorState::~AllocatorState() {
        vmaDestroyAllocator(pimpl->m_allocator);
    }
    void AllocatorState::Create() {
        VmaAllocatorCreateInfo info{};
        info.device = m_system.getDevice();
        info.physicalDevice = m_system.GetPhysicalDevice();
        info.instance = m_system.getInstance();
        info.vulkanApiVersion = vk::ApiVersion13;

        vmaDestroyAllocator(pimpl->m_allocator);
        vmaCreateAllocator(&info, &pimpl->m_allocator);
        assert(pimpl->m_allocator && "Failed to create allocator.");
    }

    VmaAllocator AllocatorState::GetAllocator() const {
        return pimpl->m_allocator;
    }

    BufferAllocation AllocatorState::AllocateBuffer(BufferType type, size_t size, const std::string &name) const {
        assert(pimpl->m_allocator && "Allocated not initalized.");
        auto [busage, flags, musage] = pimpl->GetBufferFlags(type);

        VkBufferCreateInfo bcinfo{};
        bcinfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bcinfo.size = size;
        bcinfo.usage = static_cast<VkBufferUsageFlags>(busage);

        VmaAllocationCreateInfo ainfo = {};
        ainfo.flags = flags;
        ainfo.usage = musage;

        VkBuffer buffer{};
        VmaAllocation allocation{};

        VkResult result = vmaCreateBuffer(pimpl->m_allocator, &bcinfo, &ainfo, &buffer, &allocation, nullptr);
        vk::detail::resultCheck(vk::Result{result}, "Failed to create buffer.");
        assert(buffer != nullptr && allocation != nullptr);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), static_cast<vk::Buffer>(buffer), name);
        return BufferAllocation(static_cast<vk::Buffer>(buffer), allocation, pimpl->m_allocator);
    }

    std::unique_ptr<BufferAllocation> AllocatorState::AllocateBufferUnique(
        BufferType type, size_t size, const std::string &name
    ) const noexcept try {
        return std::make_unique<BufferAllocation>(AllocateBuffer(type, size, name));
    } catch (std::exception & e) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, e.what());
        return nullptr;
    }

    std::unique_ptr<ImageAllocation> AllocatorState::AllocateImageUnique(
        ImageUtils::ImageType type,
        vk::ImageType dimension,
        vk::Extent3D extent,
        vk::Format format,
        uint32_t miplevel,
        uint32_t array_layers,
        vk::SampleCountFlagBits samples,
        const std::string &name
    ) const noexcept try {
        const auto [iusage, musage] = ImageUtils::GetImageFlags(type);
        auto fsupport = pimpl->QueryFormatSupport(m_system.GetPhysicalDevice(), format, type);
        if (fsupport.first == 0) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                std::format(
                    "Format {} does not support requested features. We requested: {} but only {} are supported.",
                    to_string(format),
                    to_string(ImageUtils::GetFormatFeatures(type)),
                    to_string(fsupport.second)
                ).c_str()
            );
            throw std::bad_alloc();
        }
        if (fsupport.first < 0) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                std::format(
                    "Format {} does not support requested features in optimal tiling. "
                    "We requested: {} but only {} are supported. Performance may degrade.",
                    to_string(format),
                    to_string(ImageUtils::GetFormatFeatures(type)),
                    to_string(fsupport.second)
                ).c_str()
            );
        }

        auto ifsupport = pimpl->UpdateImageFormatSupportInfo(
            m_system.GetPhysicalDevice(), 
            format, 
            dimension, 
            fsupport.first > 0 ? vk::ImageTiling::eOptimal : vk::ImageTiling::eLinear, 
            iusage
        );
        const auto & max_extent = ifsupport.imageFormatProperties.maxExtent;
        if (extent.width > max_extent.width || extent.height > max_extent.height || extent.depth > max_extent.depth) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, 
                std::format(
                    "Image extent exceeded capability: {}x{}x{} > {}x{}x{}",
                    extent.width, extent.height, extent.depth,
                    max_extent.width, max_extent.height, max_extent.depth
                ).c_str()
            );
            throw std::bad_alloc();
        }
        if (miplevel > ifsupport.imageFormatProperties.maxMipLevels) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, 
                std::format(
                    "Image miplevel exceeded capability: {} > {}",
                    miplevel,
                    ifsupport.imageFormatProperties.maxMipLevels
                ).c_str()
            );
            throw std::bad_alloc();
        }
        if (array_layers > ifsupport.imageFormatProperties.maxArrayLayers) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, 
                std::format(
                    "Image array layer exceeded capability: {} > {}",
                    array_layers,
                    ifsupport.imageFormatProperties.maxArrayLayers
                ).c_str()
            );
            throw std::bad_alloc();
        }
        if (!(samples & ifsupport.imageFormatProperties.sampleCounts)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, 
                std::format(
                    "Requested multisample not supported: {}",
                    to_string(samples)
                ).c_str()
            );
            throw std::bad_alloc();
        }
        
        // VkImageCreateInfo iinfo {};
        vk::ImageCreateInfo iinfo{
            vk::ImageCreateFlags{0U},
            dimension,
            format,
            extent,
            miplevel,
            array_layers,
            samples,
            fsupport.first > 0 ? vk::ImageTiling::eOptimal : vk::ImageTiling::eLinear,
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
        vmaCreateImage(pimpl->m_allocator, &iinfo2, &ainfo, &image, &allocation, nullptr);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), static_cast<vk::Image>(image), name);
        return std::unique_ptr<ImageAllocation>(new ImageAllocation(static_cast<vk::Image>(image), allocation, pimpl->m_allocator));
    }
    catch (std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, e.what());
        return nullptr;
    }

    bool AllocatorState::QueryFormatFeatures(
        vk::Format format, 
        vk::FormatFeatureFlagBits feature
    ) const noexcept {
        auto ret = pimpl->UpdateFormatSupportInfo(m_system.GetPhysicalDevice(), format);
        return static_cast<bool>(vk::FormatFeatureFlags{ret.formatProperties.optimalTilingFeatures & feature});
    }

    ImageAllocation AllocatorState::AllocateImage(
        ImageUtils::ImageType type,
        vk::ImageType dimension,
        vk::Extent3D extent,
        vk::Format format,
        uint32_t miplevel,
        uint32_t array_layers,
        vk::SampleCountFlagBits samples,
        const std::string &name
    ) const {
        assert(!"Unimplemented");
    }
} // namespace Engine::RenderSystemState
