#ifndef ENGINE_RHI_VULKANHANDLES_INCLUDED
#define ENGINE_RHI_VULKANHANDLES_INCLUDED

#include "Rhi/RhiTypes.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief Conversions between the portable handles and Vulkan objects.
     *
     * A portable handle is the Vulkan handle flattened to an integer: Vulkan's
     * handles are either pointers (dispatchable) or pointer-width integers
     * (non-dispatchable), so the round trip is exact. Only the Vulkan backend
     * includes this header; portable code never sees a `vk::` type.
     *
     * @note This header belongs to the Vulkan implementation, not to the
     * interface. A translation unit that includes only `engine/Rhi` public
     * headers never reaches it and needs no Vulkan header at all.
     */
    template <typename Handle, typename VulkanHandle>
    inline Handle FromVulkan(VulkanHandle handle) noexcept {
        return Handle::FromValue(reinterpret_cast<uintptr_t>(handle));
    }

    template <typename VulkanHandle, typename Handle>
    inline VulkanHandle ToVulkan(Handle handle) noexcept {
        return reinterpret_cast<VulkanHandle>(handle.GetValue());
    }

    /// @brief Convert a Vulkan buffer to its portable handle.
    inline BufferHandle FromVulkan(vk::Buffer buffer) noexcept {
        return FromVulkan<BufferHandle>(static_cast<VkBuffer>(buffer));
    }
    /// @brief Convert a portable buffer handle to its Vulkan buffer.
    inline vk::Buffer ToVulkanBuffer(BufferHandle handle) noexcept {
        return vk::Buffer(ToVulkan<VkBuffer>(handle));
    }

    /// @brief Convert a Vulkan image to its portable handle.
    inline ImageHandle FromVulkan(vk::Image image) noexcept {
        return FromVulkan<ImageHandle>(static_cast<VkImage>(image));
    }
    /// @brief Convert a portable image handle to its Vulkan image.
    inline vk::Image ToVulkanImage(ImageHandle handle) noexcept {
        return vk::Image(ToVulkan<VkImage>(handle));
    }

    /// @brief Convert a Vulkan image view to its portable handle.
    inline ImageViewHandle FromVulkan(vk::ImageView view) noexcept {
        return FromVulkan<ImageViewHandle>(static_cast<VkImageView>(view));
    }
    /// @brief Convert a portable image view handle to its Vulkan image view.
    inline vk::ImageView ToVulkanImageView(ImageViewHandle handle) noexcept {
        return vk::ImageView(ToVulkan<VkImageView>(handle));
    }

    /// @brief Convert a Vulkan sampler to its portable handle.
    inline SamplerHandle FromVulkan(vk::Sampler sampler) noexcept {
        return FromVulkan<SamplerHandle>(static_cast<VkSampler>(sampler));
    }
    /// @brief Convert a portable sampler handle to its Vulkan sampler.
    inline vk::Sampler ToVulkanSampler(SamplerHandle handle) noexcept {
        return vk::Sampler(ToVulkan<VkSampler>(handle));
    }

    /// @brief Convert a Vulkan descriptor set to its portable handle.
    inline DescriptorSetHandle FromVulkan(vk::DescriptorSet set) noexcept {
        return FromVulkan<DescriptorSetHandle>(static_cast<VkDescriptorSet>(set));
    }
    /// @brief Convert a portable descriptor set handle to its Vulkan descriptor set.
    inline vk::DescriptorSet ToVulkanDescriptorSet(DescriptorSetHandle handle) noexcept {
        return vk::DescriptorSet(ToVulkan<VkDescriptorSet>(handle));
    }

    /// @brief Convert a Vulkan descriptor set layout to its portable handle.
    inline DescriptorSetLayoutHandle FromVulkan(vk::DescriptorSetLayout layout) noexcept {
        return FromVulkan<DescriptorSetLayoutHandle>(static_cast<VkDescriptorSetLayout>(layout));
    }
    /// @brief Convert a portable descriptor set layout handle to its Vulkan layout.
    inline vk::DescriptorSetLayout ToVulkanDescriptorSetLayout(DescriptorSetLayoutHandle handle) noexcept {
        return vk::DescriptorSetLayout(ToVulkan<VkDescriptorSetLayout>(handle));
    }

    /// @brief Convert a Vulkan pipeline to its portable handle.
    inline PipelineHandle FromVulkan(vk::Pipeline pipeline) noexcept {
        return FromVulkan<PipelineHandle>(static_cast<VkPipeline>(pipeline));
    }
    /// @brief Convert a portable pipeline handle to its Vulkan pipeline.
    inline vk::Pipeline ToVulkanPipeline(PipelineHandle handle) noexcept {
        return vk::Pipeline(ToVulkan<VkPipeline>(handle));
    }

    /// @brief Convert a Vulkan pipeline layout to its portable handle.
    inline PipelineLayoutHandle FromVulkan(vk::PipelineLayout layout) noexcept {
        return FromVulkan<PipelineLayoutHandle>(static_cast<VkPipelineLayout>(layout));
    }
    /// @brief Convert a portable pipeline layout handle to its Vulkan layout.
    inline vk::PipelineLayout ToVulkanPipelineLayout(PipelineLayoutHandle handle) noexcept {
        return vk::PipelineLayout(ToVulkan<VkPipelineLayout>(handle));
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_VULKANHANDLES_INCLUDED

