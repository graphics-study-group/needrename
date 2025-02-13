#include "RenderCommandBuffer.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Material/MaterialInstance.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/Synch/Synchronization.h"
#include "Render/ConstantData/PerModelConstants.h"

#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"

namespace Engine
{
    RenderCommandBuffer::RenderCommandBuffer(
        std::weak_ptr<RenderSystem> system
        ) : m_system(system.lock().get())
    {
    }

    void RenderCommandBuffer::SetCommandBuffer(
        vk::CommandBuffer cb, 
        vk::Queue queue, 
        vk::Fence fence, 
        vk::Semaphore wait, 
        vk::Semaphore signal, 
        uint32_t frame_in_flight
    ) {
        this->m_handle = cb;
    }

    void RenderCommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{};
        m_handle.begin(binfo);
    }

    void RenderCommandBuffer::BeginRendering(const RenderTargetSetup &pass, vk::Extent2D extent, uint32_t framebuffer_id)
    {
        m_bound_render_target = std::cref(pass);
        const auto & clear_values = pass.GetClearValues();
        auto depth = pass.GetDepthAttachment(framebuffer_id);

        m_image_for_present = pass.GetImageForPresentation(framebuffer_id);

        auto max_color_index = pass.GetColorAttachmentSize();

        std::vector <vk::RenderingAttachmentInfo> color_attachment;
        color_attachment.resize(max_color_index);
        std::vector<vk::ImageMemoryBarrier2> barriers;
        barriers.resize(max_color_index + 1);
    
        for (size_t i = 0; i < max_color_index; i++) {
            auto color = pass.GetColorAttachment(framebuffer_id, i);

            // Prepare attachment information
            if (!((color.load_op == vk::AttachmentLoadOp::eClear || color.load_op == vk::AttachmentLoadOp::eLoad) &&
                color.store_op == vk::AttachmentStoreOp::eStore)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Pathological color buffer operations.");
            }
            color_attachment[i] = GetVkAttachmentInfo(color, vk::ImageLayout::eColorAttachmentOptimal, clear_values[0]);

            // Set up layout transition barrier
            barriers[i] = LayoutTransferHelper::GetAttachmentBarrier(
                LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPrepare, 
                color.image
            );
        }

        if (!(depth.load_op == vk::AttachmentLoadOp::eClear 
            && depth.store_op == vk::AttachmentStoreOp::eDontCare)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Pathological depth buffer operations.");
        }
        vk::RenderingAttachmentInfo depth_attachment{
            GetVkAttachmentInfo(depth, vk::ImageLayout::eDepthAttachmentOptimal, clear_values[1])
        };
        barriers[color_attachment.size()] = LayoutTransferHelper::GetAttachmentBarrier(
            LayoutTransferHelper::AttachmentTransferType::DepthAttachmentPrepare, 
            depth.image
        );

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
            &depth_attachment,
            nullptr
        };
        // Begin rendering after transit
        m_handle.beginRendering(info);
    }

    void RenderCommandBuffer::BindMaterial(MaterialInstance &material, uint32_t pass_index)
    {
        assert(m_bound_render_target.has_value());
        const auto & pipeline = material.GetTemplate().GetPipeline(pass_index);
        const auto & pipeline_layout = material.GetTemplate().GetPipelineLayout(pass_index);

        m_handle.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);

        const auto & global_pool = m_system->GetGlobalConstantDescriptorPool();
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
#ifndef NDEBUG
        // assert(m_bound_render_target.has_value());
        if (!m_bound_render_target.has_value()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "End rendering called without beginning a rendering pass.");
        }
#endif
        m_handle.endRendering();
        m_bound_render_target.reset();
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh) {
        auto bindings = mesh.GetBindingInfo();
        m_handle.bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle.bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        m_handle.drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::End() {
#ifndef NDEBUG
        if (m_bound_render_target.has_value()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Command buffer ended within rendering pass.");
        }
#endif
        // Transit color attachment to present layout
        if (m_image_for_present.has_value()) {
            if (m_image_for_present.value() != nullptr) {
                std::array<vk::ImageMemoryBarrier2, 1> barriers = {
                    LayoutTransferHelper::GetAttachmentBarrier(LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPresent, m_image_for_present.value())
                };
                vk::DependencyInfo dep {
                    vk::DependencyFlags{0},
                    {}, {}, barriers
                };
                m_handle.pipelineBarrier2(dep);
            }
        }
        m_handle.end();
    }

    void RenderCommandBuffer::Submit() {
        vk::SubmitInfo info{};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &m_handle;

        // const auto & synch = m_system.getSynchronization();
        auto wait = this->m_image_ready_semaphore;
        auto waitFlags = vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput};
        auto signal = this->m_completed_semaphore;

        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &wait;
        info.pWaitDstStageMask = &waitFlags;

        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &signal;
        std::array<vk::SubmitInfo, 1> infos{info};
        m_queue.submit(infos, m_completed_fence);
    }

    void RenderCommandBuffer::Reset() {
        m_handle.reset();
        m_bound_material_pipeline.reset();
        m_image_for_present.reset();
    }

    vk::CommandBuffer RenderCommandBuffer::get()
    {
        return m_handle;
    }
}
