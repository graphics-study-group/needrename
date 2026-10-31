#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief The Vulkan encoder: a mark is one compute-to-compute memory barrier.
     *
     * The barrier is the widest pair that is still correct for shader-visible
     * work — compute writes becoming visible to compute reads and writes — which
     * is exactly what the physics step's hand-written barriers said before the
     * seam existed, so recording is unchanged for every existing call site.
     */
    struct CommandEncoder::impl {
        vk::CommandBuffer command_buffer{};
    };

    CommandEncoder::CommandEncoder(CommandBufferHandle handle) : pimpl(std::make_unique<impl>()) {
        pimpl->command_buffer = ToVulkanCommandBuffer(handle);
    }

    CommandEncoder::~CommandEncoder() = default;
    CommandEncoder::CommandEncoder(CommandEncoder &&) noexcept = default;
    CommandEncoder &CommandEncoder::operator=(CommandEncoder &&) noexcept = default;

    void CommandEncoder::Barrier() {
        const vk::MemoryBarrier2 barrier{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
        };
        pimpl->command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {barrier}, {}, {}});
    }

    CommandBufferHandle CommandEncoder::GetHandle() const noexcept {
        return FromVulkan(pimpl->command_buffer);
    }
} // namespace Engine::Rhi
