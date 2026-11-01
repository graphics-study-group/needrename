#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/Vulkan/MemoryAccessHelper.hpp"
#include "Rhi/Backend/Vulkan/VulkanImageUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    namespace {
        vk::AttachmentLoadOp ToVkLoadOperation(LoadOperation operation) {
            switch (operation) {
            case LoadOperation::Load:
                return vk::AttachmentLoadOp::eLoad;
            case LoadOperation::Clear:
                return vk::AttachmentLoadOp::eClear;
            case LoadOperation::DontCare:
            default:
                return vk::AttachmentLoadOp::eDontCare;
            }
        }

        vk::AttachmentStoreOp ToVkStoreOperation(StoreOperation operation) {
            return operation == StoreOperation::Store ? vk::AttachmentStoreOp::eStore
                                                       : vk::AttachmentStoreOp::eDontCare;
        }

        /// @brief The stage a transition executes in, from the kind of work it separates.
        ///
        /// `Unspecified` is a pass that records no work of its own, so the widest
        /// pair is the only safe answer. A named kind of work narrows it: a compute
        /// dispatch or a copy does not have to wait for rasterisation to drain.
        vk::PipelineStageFlags2 ToVkStage(PipelineScope scope) {
            switch (scope) {
            case PipelineScope::Transfer:
                return vk::PipelineStageFlagBits2::eAllTransfer;
            case PipelineScope::Compute:
                return vk::PipelineStageFlagBits2::eComputeShader;
            case PipelineScope::Graphics:
                return vk::PipelineStageFlagBits2::eAllGraphics;
            case PipelineScope::Unspecified:
            default:
                return vk::PipelineStageFlagBits2::eAllCommands;
            }
        }
    } // namespace
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

    void CommandEncoder::Barrier(const ImageBarrier &barrier) {
        vk::ImageMemoryBarrier2 image_barrier{};
        image_barrier.srcStageMask = ToVkStage(barrier.from_scope);
        image_barrier.srcAccessMask = GetAccessFlags(barrier.from);
        image_barrier.dstStageMask = ToVkStage(barrier.to_scope);
        image_barrier.dstAccessMask = GetAccessFlags(barrier.to);
        image_barrier.oldLayout = GetImageLayout(barrier.from);
        image_barrier.newLayout = GetImageLayout(barrier.to);
        image_barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        image_barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        image_barrier.image = ToVulkanImage(barrier.image);
        image_barrier.subresourceRange = vk::ImageSubresourceRange{
            GetVkAspect(barrier.aspect),
            barrier.base_mip,
            barrier.mip_count,
            barrier.base_layer,
            barrier.layer_count
        };
        pimpl->command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {image_barrier}});
    }

    void CommandEncoder::BeginRenderPass(const RenderPassDescription &description) {
        std::vector<vk::RenderingAttachmentInfo> color_attachments;
        color_attachments.reserve(description.color_attachments.size());
        for (const ColorAttachmentDescription &attachment : description.color_attachments) {
            vk::RenderingAttachmentInfo info{};
            info.imageView = ToVulkanImageView(attachment.view);
            info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            info.loadOp = ToVkLoadOperation(attachment.load);
            info.storeOp = ToVkStoreOperation(attachment.store);
            info.clearValue = vk::ClearColorValue{
                std::array<float, 4>{attachment.clear.r, attachment.clear.g, attachment.clear.b, attachment.clear.a}
            };
            color_attachments.push_back(info);
        }

        vk::RenderingAttachmentInfo depth_attachment{};
        const bool has_depth = description.depth_attachment.view.IsValid();
        if (has_depth) {
            depth_attachment.imageView = ToVulkanImageView(description.depth_attachment.view);
            depth_attachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depth_attachment.loadOp = ToVkLoadOperation(description.depth_attachment.load);
            depth_attachment.storeOp = ToVkStoreOperation(description.depth_attachment.store);
            depth_attachment.clearValue = vk::ClearDepthStencilValue{description.depth_attachment.clear_depth, 0};
        }

        const vk::Rect2D area{
            vk::Offset2D{static_cast<int32_t>(description.area.offset.x), static_cast<int32_t>(description.area.offset.y)},
            vk::Extent2D{description.area.extent.width, description.area.extent.height}
        };

        vk::RenderingInfo rendering_info{};
        rendering_info.renderArea = area;
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = static_cast<uint32_t>(color_attachments.size());
        rendering_info.pColorAttachments = color_attachments.data();
        rendering_info.pDepthAttachment = has_depth ? &depth_attachment : nullptr;

        pimpl->command_buffer.beginRendering(rendering_info);
    }

    void CommandEncoder::EndRenderPass() {
        pimpl->command_buffer.endRendering();
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




