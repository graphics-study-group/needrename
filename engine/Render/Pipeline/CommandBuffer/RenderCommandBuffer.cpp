#include "RenderCommandBuffer.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/Pipeline/RenderTargetBinding.h"

#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"
#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace Engine
{
    RenderCommandBuffer::RenderCommandBuffer(
        RenderSystem & system,
        vk::CommandBuffer cb,
        uint32_t frame_in_flight
        ) : m_system(system), 
        m_handle(cb), 
        m_inflight_frame_index(frame_in_flight)
    {
    }

    void RenderCommandBuffer::Begin(const std::string & name)
    {
        m_handle.begin(vk::CommandBufferBeginInfo{});
        DEBUG_CMD_START_LABEL(m_handle, name.c_str());
    }

    void RenderCommandBuffer::BeginRendering(
        AttachmentUtils::AttachmentDescription color, 
        AttachmentUtils::AttachmentDescription depth,
        vk::Extent2D extent)
    {
        std::vector <vk::RenderingAttachmentInfo> color_attachment;
        std::vector <vk::ImageMemoryBarrier2> barriers;
        
        if (color.image && color.image_view) {
            color_attachment.push_back(GetVkAttachmentInfo(
                color,
                vk::ImageLayout::eColorAttachmentOptimal, 
                vk::ClearColorValue{0, 0, 0, 0}
            ));

            // Set up layout transition barrier
            barriers.push_back(LayoutTransferHelper::GetAttachmentBarrier(
                LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPrepare, 
                color.image
            ));
        }

        vk::RenderingAttachmentInfo depth_attachment;
        if (depth.image && depth.image_view) {
            depth_attachment = vk::RenderingAttachmentInfo{
                GetVkAttachmentInfo(
                    depth, 
                    vk::ImageLayout::eDepthStencilAttachmentOptimal, 
                    vk::ClearDepthStencilValue{1.0f, 0U}
                )
            };
            barriers.push_back(LayoutTransferHelper::GetAttachmentBarrier(
                LayoutTransferHelper::AttachmentTransferType::DepthAttachmentPrepare, 
                depth.image
            ));
        }

        // Issue layout transition barriers
        vk::DependencyInfo dep {
            vk::DependencyFlags{0},
            {}, {}, barriers
        };
        m_handle.pipelineBarrier2(dep);

        vk::RenderingInfo info {
            vk::RenderingFlags{0},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            color_attachment,
            depth.image ? &depth_attachment : nullptr,
            nullptr
        };
        // Begin rendering after transit
        m_handle.beginRendering(info);
    }

    void RenderCommandBuffer::BeginRendering(const RenderTargetBinding &binding, vk::Extent2D extent, const std::string & name)
    {
        DEBUG_CMD_START_LABEL(m_handle, name.c_str());
        size_t total_attachment_count = binding.GetColorAttachmentCount() + binding.HasDepthAttachment();
        std::vector <vk::RenderingAttachmentInfo> color_attachment_info (binding.GetColorAttachmentCount(), vk::RenderingAttachmentInfo{});
        std::vector <vk::ImageMemoryBarrier2> barriers (total_attachment_count, vk::ImageMemoryBarrier2{});

        const auto & color_attachments = binding.GetColorAttachments();
        for (size_t i = 0; i < color_attachments.size(); i++) {
            color_attachment_info[i] = GetVkAttachmentInfo(
                color_attachments[i],
                vk::ImageLayout::eColorAttachmentOptimal, 
                vk::ClearColorValue{0, 0, 0, 0}
            );
            barriers[i] = LayoutTransferHelper::GetAttachmentBarrier(
                LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPrepare, 
                color_attachments[i].image
            );
        }

        vk::RenderingAttachmentInfo depth_attachment_info {};
        if (binding.HasDepthAttachment()) {
            const auto & depth_attachment = binding.GetDepthAttachment();
            depth_attachment_info = GetVkAttachmentInfo(
                depth_attachment,
                vk::ImageLayout::eDepthStencilAttachmentOptimal, 
                vk::ClearDepthStencilValue{1.0f, 0U}
            );
            *(barriers.rbegin()) = LayoutTransferHelper::GetAttachmentBarrier(
                LayoutTransferHelper::AttachmentTransferType::DepthAttachmentPrepare, 
                depth_attachment.image
            );
        }

        vk::DependencyInfo dep {
            vk::DependencyFlags{0},
            {}, {}, barriers
        };
        m_handle.pipelineBarrier2(dep);

        vk::RenderingInfo info {
            vk::RenderingFlags{0},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            color_attachment_info,
            binding.HasDepthAttachment() ? &depth_attachment_info : nullptr,
            nullptr
        };
        // Begin rendering after transit
        m_handle.beginRendering(info);
    }

    void RenderCommandBuffer::BindMaterial(MaterialInstance &material, uint32_t pass_index)
    {
        const auto & pipeline = material.GetTemplate().GetPipeline(pass_index);
        const auto & pipeline_layout = material.GetTemplate().GetPipelineLayout(pass_index);

        m_handle.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);

        const auto & global_pool = m_system.GetGlobalConstantDescriptorPool();
        const auto & per_scenc_descriptor_set = global_pool.GetPerSceneConstantSet(m_inflight_frame_index);
        const auto & per_camera_descriptor_set = global_pool.GetPerCameraConstantSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptor(pass_index);

        if (material_descriptor_set) {
            m_handle.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                pipeline_layout, 
                0,
                {per_scenc_descriptor_set, per_camera_descriptor_set, material_descriptor_set},
                {}
            );
        } else {
            m_handle.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                pipeline_layout, 
                0,
                {per_scenc_descriptor_set, per_camera_descriptor_set},
                {}
            );
        }
        
        material.WriteUBO(pass_index);
        material.WriteDescriptors(pass_index);
    }

    void RenderCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        m_handle.setViewport(0, 1, &vp);
        m_handle.setScissor(0, 1, &scissor);
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh, const glm::mat4 & model_matrix) {
        auto bindings = mesh.GetBindingInfo();
        m_handle.bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        m_handle.pushConstants(
            m_bound_material_pipeline.value().second, 
            vk::ShaderStageFlagBits::eVertex, 
            0, 
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&model_matrix)
        );
        m_handle.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::EndRendering()
    {
        m_handle.endRendering();
        DEBUG_CMD_END_LABEL(m_handle);
    }

    void RenderCommandBuffer::InsertAttachmentBarrier(AttachmentBarrierType type, vk::Image image)
    {
        std::array <vk::ImageMemoryBarrier2, 1> barriers {
            LayoutTransferHelper::GetAttachmentBarrier(type, image)
        };
        vk::DependencyInfo dep{{}, {}, {}, barriers};
        m_handle.pipelineBarrier2(dep);
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh &mesh)
    {
        auto bindings = mesh.GetBindingInfo();
        m_handle.bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        m_handle.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::End() {
        DEBUG_CMD_END_LABEL(m_handle);
        m_handle.end();
    }

    void RenderCommandBuffer::Reset() {
        m_handle.reset();
        m_bound_material_pipeline.reset();
    }

    vk::CommandBuffer RenderCommandBuffer::get()
    {
        return m_handle;
    }
}
