#include "RenderCommandBuffer.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Material/Material.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/PipelineLayout.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/Synch/Synchronization.h"
#include "Render/ConstantData/PerModelConstants.h"

#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"

namespace Engine
{
    void RenderCommandBuffer::CreateCommandBuffer(
        std::shared_ptr<RenderSystem> system, 
        vk::CommandPool command_pool,
        vk::Queue queue,
        uint32_t frame_index
    ) {
        vk::CommandBufferAllocateInfo info{};
        info.commandPool = command_pool;
        info.commandBufferCount = 1;
        info.level = vk::CommandBufferLevel::ePrimary;

        auto cbvector = system->getDevice().allocateCommandBuffersUnique(info);
        assert(cbvector.size() == 1);
        m_handle = std::move(cbvector[0]);

        m_inflight_frame_index = frame_index;
        m_system = system.get();
        m_queue = queue;
    }

    void RenderCommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{};
        m_handle->begin(binfo);
    }

    void RenderCommandBuffer::BeginRendering(const RenderTargetSetup &pass, vk::Extent2D extent, uint32_t framebuffer_id)
    {
        m_bound_render_target = std::cref(pass);
        const auto & clear_values = pass.GetClearValues();
        auto color = pass.GetColorAttachment(framebuffer_id);
        auto depth = pass.GetDepthAttachment(framebuffer_id);
        m_image_for_present = color.first;
        std::vector <vk::RenderingAttachmentInfo> color_attachment{
            {
                color.second,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                {},
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                clear_values[0]
            }
        };
        vk::RenderingAttachmentInfo depth_attachment{
            depth.second,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            clear_values[1]
        };

        vk::RenderingInfo info {
            vk::RenderingFlags{0},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            color_attachment,
            &depth_attachment,
            nullptr
        };

        // Transit attachments layout, from undefined to optimal

        std::array<vk::ImageMemoryBarrier2, 2> barriers = {
            LayoutTransferHelper::GetAttachmentBarrier(LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPrepare, color.first),
            LayoutTransferHelper::GetAttachmentBarrier(LayoutTransferHelper::AttachmentTransferType::DepthAttachmentPrepare, depth.first),
        };
        vk::DependencyInfo dep {
            vk::DependencyFlags{0},
            {}, {}, barriers
        };
        m_handle->pipelineBarrier2(dep);

        // Begin rendering after transit
        m_handle->beginRendering(info);
    }

    void RenderCommandBuffer::BindMaterial(Material & material, uint32_t pass_index) {
        assert(m_bound_render_target.has_value());
        const auto & pipeline = material.GetPipeline(pass_index)->get();
        const auto & pipeline_layout = material.GetPipelineLayout(pass_index)->get();

        m_handle->bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        m_bound_material = std::make_pair(std::cref(material), pass_index);
        m_bound_material_pipeline = std::make_pair(pipeline, pipeline_layout);

        const auto & global_pool = m_system->GetGlobalConstantDescriptorPool();
        const auto & per_camera_descriptor_set = global_pool.GetPerCameraConstantSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptorSet(pass_index);

        if (material_descriptor_set) {
            m_handle->bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                pipeline_layout, 
                0,
                {per_camera_descriptor_set, material_descriptor_set},
                {}
            );
        } else {
            m_handle->bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                pipeline_layout, 
                0,
                {per_camera_descriptor_set},
                {}
            );
        }
        
        material.WriteDescriptors();
    }

    void RenderCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        m_handle->setViewport(0, 1, &vp);
        m_handle->setScissor(0, 1, &scissor);
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh, const glm::mat4 & model_matrix) {
#ifndef NDEBUG
        if (!m_bound_material.has_value()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Rendering a mesh with no material bound.");
        }
#endif
        auto bindings = mesh.GetBindingInfo();
        m_handle->bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle->bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        m_handle->pushConstants(
            m_bound_material_pipeline.value().second, 
            vk::ShaderStageFlagBits::eVertex, 
            0, 
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&model_matrix)
        );
        m_handle->drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::EndRendering()
    {
#ifndef NDEBUG
        // assert(m_bound_render_target.has_value());
        if (!m_bound_render_target.has_value()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "End rendering called without beginning a rendering pass.");
        }
#endif
        m_handle->endRendering();
        m_bound_render_target.reset();
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh) {
        auto bindings = mesh.GetBindingInfo();
        m_handle->bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle->bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);

        m_handle->pushConstants(
            m_bound_material_pipeline.value().second, 
            vk::ShaderStageFlagBits::eVertex, 
            0, 
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&mesh.GetModelTransform())
        );
        m_handle->drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::End() {
#ifndef NDEBUG
        if (m_bound_render_target.has_value()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Command buffer ended within rendering pass.");
        }
#endif
        // Transit color attachment to present layout
        if (m_image_for_present.has_value()) {
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {
                LayoutTransferHelper::GetAttachmentBarrier(LayoutTransferHelper::AttachmentTransferType::ColorAttachmentPresent, m_image_for_present.value())
            };
            vk::DependencyInfo dep {
                vk::DependencyFlags{0},
                {}, {}, barriers
            };
            m_handle->pipelineBarrier2(dep);
        }
        m_handle->end();
    }

    void RenderCommandBuffer::Submit() {
        vk::SubmitInfo info{};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &m_handle.get();

        const auto & synch = m_system->getSynchronization();
        auto wait = synch.GetCommandBufferWaitSignals(m_inflight_frame_index);
        auto waitFlags = synch.GetCommandBufferWaitSignalFlags(m_inflight_frame_index);
        auto signal = synch.GetCommandBufferSigningSignals(m_inflight_frame_index);

        assert(wait.size() == waitFlags.size());
        info.waitSemaphoreCount = wait.size();
        info.pWaitSemaphores = wait.data();
        info.pWaitDstStageMask = waitFlags.data();

        info.signalSemaphoreCount = signal.size();
        info.pSignalSemaphores = signal.data();
        std::array<vk::SubmitInfo, 1> infos{info};
        m_queue.submit(infos, synch.GetCommandBufferFence(m_inflight_frame_index));
    }

    void RenderCommandBuffer::Reset() {
        m_handle->reset();
        m_bound_material.reset();
        m_bound_material_pipeline.reset();
        m_image_for_present.reset();
    }
}
