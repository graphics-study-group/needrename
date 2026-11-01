#ifndef ENGINE_RHI_VULKANHANDLES_INCLUDED
#define ENGINE_RHI_VULKANHANDLES_INCLUDED

#include "Rhi/RhiTypes.h"

#include <vector>
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
    /// @brief Convert a portable shader module handle to its Vulkan module.
    inline vk::ShaderModule ToVulkanShaderModule(ShaderModuleHandle handle) noexcept {
        return vk::ShaderModule{reinterpret_cast<VkShaderModule>(handle.GetValue())};
    }

    inline ShaderModuleHandle FromVulkan(vk::ShaderModule module) noexcept {
        return FromVulkan<ShaderModuleHandle>(static_cast<VkShaderModule>(module));
    }

    inline vk::PipelineLayout ToVulkanPipelineLayout(PipelineLayoutHandle handle) noexcept {
        return vk::PipelineLayout(ToVulkan<VkPipelineLayout>(handle));
    }
    /// @brief Convert a Vulkan instance to its portable handle.
    inline InstanceHandle FromVulkan(vk::Instance instance) noexcept {
        return FromVulkan<InstanceHandle>(static_cast<VkInstance>(instance));
    }
    /// @brief Convert a portable instance handle to its Vulkan instance.
    inline vk::Instance ToVulkanInstance(InstanceHandle handle) noexcept {
        return vk::Instance(ToVulkan<VkInstance>(handle));
    }

    /// @brief Convert a Vulkan physical device to its portable handle.
    inline PhysicalDeviceHandle FromVulkan(vk::PhysicalDevice device) noexcept {
        return FromVulkan<PhysicalDeviceHandle>(static_cast<VkPhysicalDevice>(device));
    }
    /// @brief Convert a portable physical device handle to its Vulkan object.
    inline vk::PhysicalDevice ToVulkanPhysicalDevice(PhysicalDeviceHandle handle) noexcept {
        return vk::PhysicalDevice(ToVulkan<VkPhysicalDevice>(handle));
    }

    /// @brief Convert a Vulkan device to its portable handle.
    inline DeviceHandle FromVulkan(vk::Device device) noexcept {
        return FromVulkan<DeviceHandle>(static_cast<VkDevice>(device));
    }
    /// @brief Convert a portable device handle to its Vulkan device.
    inline vk::Device ToVulkanDevice(DeviceHandle handle) noexcept {
        return vk::Device(ToVulkan<VkDevice>(handle));
    }

    /// @brief Convert a Vulkan surface to its portable handle.
    inline SurfaceHandle FromVulkan(vk::SurfaceKHR surface) noexcept {
        return FromVulkan<SurfaceHandle>(static_cast<VkSurfaceKHR>(surface));
    }
    /// @brief Convert a portable surface handle to its Vulkan surface.
    inline vk::SurfaceKHR ToVulkanSurface(SurfaceHandle handle) noexcept {
        return vk::SurfaceKHR(ToVulkan<VkSurfaceKHR>(handle));
    }
    /// @brief Convert a Vulkan command buffer to its portable handle.
    inline CommandBufferHandle FromVulkan(vk::CommandBuffer command_buffer) noexcept {
        return FromVulkan<CommandBufferHandle>(static_cast<VkCommandBuffer>(command_buffer));
    }
    /// @brief Convert a portable command buffer handle to its Vulkan object.
    inline vk::CommandBuffer ToVulkanCommandBuffer(CommandBufferHandle handle) noexcept {
        return vk::CommandBuffer(ToVulkan<VkCommandBuffer>(handle));
    }

    /// @brief Convert a Vulkan semaphore to its portable handle.
    inline SemaphoreHandle FromVulkan(vk::Semaphore semaphore) noexcept {
        return FromVulkan<SemaphoreHandle>(static_cast<VkSemaphore>(semaphore));
    }
    /// @brief Convert a portable semaphore handle to its Vulkan object.
    inline vk::Semaphore ToVulkanSemaphore(SemaphoreHandle handle) noexcept {
        return vk::Semaphore(ToVulkan<VkSemaphore>(handle));
    }

    /// @brief Convert a Vulkan fence to its portable handle.
    inline FenceHandle FromVulkan(vk::Fence fence) noexcept {
        return FromVulkan<FenceHandle>(static_cast<VkFence>(fence));
    }
    /// @brief Convert a portable fence handle to its Vulkan object.
    inline vk::Fence ToVulkanFence(FenceHandle handle) noexcept {
        return vk::Fence(ToVulkan<VkFence>(handle));
    }

    /// @brief Convert a portable descriptor kind to its Vulkan descriptor type.
    inline vk::DescriptorType ToVulkanDescriptorType(DescriptorType type) noexcept {
        switch (type) {
        case DescriptorType::StorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case DescriptorType::StorageBufferDynamic:
            return vk::DescriptorType::eStorageBufferDynamic;
        case DescriptorType::UniformBufferDynamic:
            return vk::DescriptorType::eUniformBufferDynamic;
        case DescriptorType::CombinedImageSampler:
            return vk::DescriptorType::eCombinedImageSampler;
        case DescriptorType::SampledImage:
            return vk::DescriptorType::eSampledImage;
        case DescriptorType::Sampler:
            return vk::DescriptorType::eSampler;
        case DescriptorType::StorageImage:
            return vk::DescriptorType::eStorageImage;
        case DescriptorType::UniformBuffer:
        default:
            return vk::DescriptorType::eUniformBuffer;
        }
    }

    /**
     * @brief Translate a portable descriptor-set layout into Vulkan bindings.
     *
     * The engine states a set's shape once, in portable terms; this is the one
     * place that turns it into the structure a Vulkan layout is created from.
     */
    inline std::vector<vk::DescriptorSetLayoutBinding> ToVulkanBindings(
        const DescriptorSetLayout &layout, std::vector<vk::Sampler> *immutable_sampler_storage = nullptr
    ) {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(layout.size());
        if (immutable_sampler_storage != nullptr) {
            // Reserved up front: the create-info points into this storage, so it
            // must not reallocate while the bindings are being built.
            immutable_sampler_storage->reserve(layout.size());
        }
        for (const DescriptorBinding &binding : layout) {
            const vk::Sampler *samplers = nullptr;
            if (immutable_sampler_storage != nullptr && binding.immutable_sampler.IsValid()) {
                immutable_sampler_storage->push_back(ToVulkanSampler(binding.immutable_sampler));
                samplers = &immutable_sampler_storage->back();
            }
            bindings.emplace_back(
                binding.binding, ToVulkanDescriptorType(binding.type), binding.count, vk::ShaderStageFlagBits::eAll, samplers
            );
        }
        return bindings;
    }

    /// @brief Convert a Vulkan descriptor type to its portable kind.
    inline DescriptorType FromVulkanDescriptorType(vk::DescriptorType type) noexcept {
        switch (type) {
        case vk::DescriptorType::eStorageBuffer:
            return DescriptorType::StorageBuffer;
        case vk::DescriptorType::eStorageBufferDynamic:
            return DescriptorType::StorageBufferDynamic;
        case vk::DescriptorType::eUniformBufferDynamic:
            return DescriptorType::UniformBufferDynamic;
        case vk::DescriptorType::eCombinedImageSampler:
            return DescriptorType::CombinedImageSampler;
        case vk::DescriptorType::eSampledImage:
            return DescriptorType::SampledImage;
        case vk::DescriptorType::eSampler:
            return DescriptorType::Sampler;
        case vk::DescriptorType::eStorageImage:
            return DescriptorType::StorageImage;
        default:
            return DescriptorType::UniformBuffer;
        }
    }

    /// @brief Convert a portable resource state to the Vulkan image layout it means.
    inline vk::ImageLayout ToVulkanImageLayout(ResourceState state) noexcept {
        switch (state) {
        case ResourceState::General:
            return vk::ImageLayout::eGeneral;
        case ResourceState::ColorAttachment:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceState::DepthStencilAttachment:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceState::TransferSource:
            return vk::ImageLayout::eTransferSrcOptimal;
        case ResourceState::TransferDestination:
            return vk::ImageLayout::eTransferDstOptimal;
        case ResourceState::PresentSource:
            return vk::ImageLayout::ePresentSrcKHR;
        case ResourceState::ShaderReadOnly:
            return vk::ImageLayout::eReadOnlyOptimal;
        case ResourceState::Undefined:
        default:
            return vk::ImageLayout::eUndefined;
        }
    }

} // namespace Engine::Rhi

#endif // ENGINE_RHI_VULKANHANDLES_INCLUDED








