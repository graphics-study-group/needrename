#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief The Vulkan encoder: one command buffer and the layout constants go to.
     *
     * A mark is one compute-to-compute memory barrier — the widest pair that is
     * still correct for shader-visible work. That is exactly what the physics
     * step wrote by hand before the seam existed, so recording is unchanged for
     * every existing call site.
     */
    struct CommandEncoder::impl {
        vk::CommandBuffer command_buffer{};
        /// @brief Layout the last bound compute pipeline reads its constants from.
        vk::PipelineLayout pipeline_layout{};
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

    void CommandEncoder::BindComputePipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        pimpl->pipeline_layout = ToVulkanPipelineLayout(layout);
        pimpl->command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, ToVulkanPipeline(pipeline));
    }

    void CommandEncoder::BindDescriptorSet(uint32_t set_index, DescriptorSetHandle set) {
        pimpl->command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, pimpl->pipeline_layout, set_index, ToVulkanDescriptorSet(set), {}
        );
    }

    void CommandEncoder::SetKernelParams(std::span<const std::byte> params) {
        if (params.empty()) {
            return;
        }
        // Vulkan has a first-class mechanism for per-dispatch constants, so the
        // window a backend without one would build is not needed here.
        pimpl->command_buffer.pushConstants(
            pimpl->pipeline_layout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            static_cast<uint32_t>(params.size()),
            params.data()
        );
    }

    void CommandEncoder::DispatchCompute(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        pimpl->command_buffer.dispatch(group_count_x, group_count_y, group_count_z);
    }

    CommandBufferHandle CommandEncoder::GetHandle() const noexcept {
        return FromVulkan(pimpl->command_buffer);
    }
} // namespace Engine::Rhi
