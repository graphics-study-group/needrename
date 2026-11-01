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

        /// @brief The stage a transition executes in, from the work and the access.
        ///
        /// A transition with no access at all is the first use of a resource: there
        /// is nothing to wait for, and naming a stage would claim otherwise. A named
        /// kind of work narrows the stage - with one exception: a copy or blit
        /// recorded inside a graphics pass synchronizes as transfer work, because
        /// that is the stage its access belongs to.
        vk::PipelineStageFlags2 ToVkStage(PipelineScope scope, MemoryAccessTypeImage access) {
            const vk::AccessFlags2 access_flags = GetAccessFlags(access);
            if (access_flags == vk::AccessFlagBits2::eNone) {
                return vk::PipelineStageFlagBits2::eNone;
            }
            switch (scope) {
            case PipelineScope::Transfer:
                return vk::PipelineStageFlagBits2::eAllTransfer;
            case PipelineScope::Compute:
                return vk::PipelineStageFlagBits2::eComputeShader;
            case PipelineScope::Graphics: {
                if (access_flags == vk::AccessFlagBits2::eTransferRead
                    || access_flags == vk::AccessFlagBits2::eTransferWrite) {
                    return vk::PipelineStageFlagBits2::eAllTransfer;
                }
                return vk::PipelineStageFlagBits2::eAllGraphics;
            }
            case PipelineScope::Unspecified:
            default:
                // A pass with no work of its own still has to name a stage, and
                // the access says which one: a copy is transfer work wherever it
                // was recorded, an attachment access belongs to graphics, and
                // anything else really is every command.
                if (access_flags == vk::AccessFlagBits2::eTransferRead
                    || access_flags == vk::AccessFlagBits2::eTransferWrite) {
                    return vk::PipelineStageFlagBits2::eTransfer;
                }
                if (access_flags == vk::AccessFlagBits2::eColorAttachmentRead
                    || access_flags == vk::AccessFlagBits2::eColorAttachmentWrite
                    || access_flags == (vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite)
                    || access_flags == vk::AccessFlagBits2::eDepthStencilAttachmentRead
                    || access_flags == vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                    || access_flags == (vk::AccessFlagBits2::eDepthStencilAttachmentRead
                                        | vk::AccessFlagBits2::eDepthStencilAttachmentWrite)) {
                    return vk::PipelineStageFlagBits2::eAllGraphics;
                }
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
        const std::array single{barrier};
        Barrier(std::span<const ImageBarrier>{single});
    }

    void CommandEncoder::Barrier(std::span<const ImageBarrier> barriers) {
        std::vector<vk::ImageMemoryBarrier2> image_barriers;
        image_barriers.reserve(barriers.size());
        for (const ImageBarrier &barrier : barriers) {
            vk::ImageMemoryBarrier2 image_barrier{};
            image_barrier.srcStageMask = ToVkStage(barrier.from_scope, barrier.from);
            image_barrier.srcAccessMask = GetAccessFlags(barrier.from);
            image_barrier.dstStageMask = ToVkStage(barrier.to_scope, barrier.to);
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
            image_barriers.push_back(image_barrier);
        }
        pimpl->command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, image_barriers});
    }

    void CommandEncoder::BlitImage(const ImageBlitDescription &description, bool linear) {
        const vk::ImageBlit2 blit{
            vk::ImageSubresourceLayers{
                GetVkAspect(description.aspect),
                description.source_mip,
                description.source_layer,
                description.source_layer_count
            },
            {vk::Offset3D{description.source_min.x, description.source_min.y, description.source_min.z},
             vk::Offset3D{description.source_max.x, description.source_max.y, description.source_max.z}},
            vk::ImageSubresourceLayers{
                GetVkAspect(description.aspect),
                description.destination_mip,
                description.destination_layer,
                description.destination_layer_count
            },
            {vk::Offset3D{
                 description.destination_min.x, description.destination_min.y, description.destination_min.z
             },
             vk::Offset3D{
                 description.destination_max.x, description.destination_max.y, description.destination_max.z
             }},
        };
        const vk::BlitImageInfo2 info{
            ToVulkanImage(description.source),
            vk::ImageLayout::eTransferSrcOptimal,
            ToVulkanImage(description.destination),
            vk::ImageLayout::eTransferDstOptimal,
            {blit},
            linear ? vk::Filter::eLinear : vk::Filter::eNearest
        };
        pimpl->command_buffer.blitImage2(info);
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

    void CommandEncoder::BindGraphicsPipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        pimpl->pipeline_layout = ToVulkanPipelineLayout(layout);
        pimpl->command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ToVulkanPipeline(pipeline));
    }

    void CommandEncoder::PushConstants(std::span<const std::byte> params) {
        if (params.empty()) {
            return;
        }
        pimpl->command_buffer.pushConstants(
            pimpl->pipeline_layout,
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            static_cast<uint32_t>(params.size()),
            params.data()
        );
    }

    void CommandEncoder::BindGraphicsDescriptorSet(
        uint32_t set_index,
        DescriptorSetHandle set,
        PipelineLayoutHandle layout,
        std::span<const uint32_t> dynamic_offsets
    ) {
        pimpl->command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            ToVulkanPipelineLayout(layout),
            set_index,
            {ToVulkanDescriptorSet(set)},
            dynamic_offsets
        );
    }

    void CommandEncoder::BindVertexBuffers(
        std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets
    ) {
        std::vector<vk::Buffer> vk_buffers;
        vk_buffers.reserve(buffers.size());
        for (const BufferHandle buffer : buffers) {
            vk_buffers.push_back(ToVulkanBuffer(buffer));
        }
        pimpl->command_buffer.bindVertexBuffers(0, vk_buffers, offsets);
    }

    void CommandEncoder::BindIndexBuffer(BufferHandle buffer, uint64_t offset, bool wide) {
        pimpl->command_buffer.bindIndexBuffer(
            ToVulkanBuffer(buffer), offset, wide ? vk::IndexType::eUint32 : vk::IndexType::eUint16
        );
    }

    void CommandEncoder::SetViewport(float width, float height, const Rect2D &scissor) {
        const vk::Viewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
        pimpl->command_buffer.setViewport(0, 1, &viewport);
        const vk::Rect2D area{
            vk::Offset2D{static_cast<int32_t>(scissor.offset.x), static_cast<int32_t>(scissor.offset.y)},
            vk::Extent2D{scissor.extent.width, scissor.extent.height}
        };
        pimpl->command_buffer.setScissor(0, 1, &area);
    }

    void CommandEncoder::DrawIndexed(
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance
    ) {
        pimpl->command_buffer.drawIndexed(
            index_count, instance_count, first_index, vertex_offset, first_instance
        );
    }

    void CommandEncoder::Draw(
        uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance
    ) {
        pimpl->command_buffer.draw(vertex_count, instance_count, first_vertex, first_instance);
    }

    void CommandEncoder::CopyBuffer(BufferHandle source, BufferHandle destination, uint64_t size) {
        pimpl->command_buffer.copyBuffer(
            ToVulkanBuffer(source), ToVulkanBuffer(destination), vk::BufferCopy{0, 0, size}
        );
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









