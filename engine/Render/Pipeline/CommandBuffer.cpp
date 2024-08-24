#include "CommandBuffer.h"

#include "Render/Pipeline/Synchronization.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Renderer/HomogeneousMesh.h"

namespace Engine
{
    void CommandBuffer::CreateCommandBuffer(vk::Device logical_device, vk::CommandPool command_pool, uint32_t frame_index)
    {
        vk::CommandBufferAllocateInfo info{};
        info.commandPool = command_pool;
        info.commandBufferCount = 1;
        info.level = vk::CommandBufferLevel::ePrimary;

        auto cbvector = logical_device.allocateCommandBuffersUnique(info);
        assert(cbvector.size() == 1);
        m_handle = std::move(cbvector[0]);

        m_inflight_frame_index = frame_index;
    }

    void CommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{};
        m_handle->begin(binfo);
    }

    void CommandBuffer::BeginRenderPass(
        const RenderPass& pass, 
        vk::Extent2D extent, 
        uint32_t framebuffer_id, 
        std::vector <vk::ClearValue> clear
    ) {
        assert(clear.size() == pass.GetAttachments().size());

        vk::RenderPassBeginInfo info{
            pass.get(),
            pass.GetFramebuffers().GetFramebuffer(framebuffer_id),
            {vk::Offset2D{0, 0}, extent},
            clear
        };
        m_handle->beginRenderPass(info, vk::SubpassContents::eInline);
    }

    void CommandBuffer::BindPipelineProgram(const Pipeline& pipeline) {
        m_handle->bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());
    }

    void CommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        m_handle->setViewport(0, 1, &vp);
        m_handle->setScissor(0, 1, &scissor);
    }

    void CommandBuffer::DrawMesh(const HomogeneousMesh& mesh) {
        auto bindings = mesh.GetBindingInfo();
        m_handle->bindVertexBuffers(0, bindings.first, bindings.second);
        m_handle->draw(mesh.GetVertexCount(), 1, 0, 0);
    }

    void CommandBuffer::End() {
        m_handle->endRenderPass();
        m_handle->end();
    }

    void CommandBuffer::SubmitToQueue(
        vk::Queue queue, 
        const Synchronization & synch
    ) {
        vk::SubmitInfo info{};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &m_handle.get();

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
        queue.submit(infos, synch.GetCommandBufferFence(m_inflight_frame_index));
    }

    void CommandBuffer::Reset() {
        m_handle->reset();
    }
    
} // namespace Engine

