#include "CommandBuffer.h"

#include "Render/Pipeline/Synchronization.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Renderer/HomogeneousMesh.h"

namespace Engine
{
    void RenderCommandBuffer::CreateCommandBuffer(vk::Device logical_device, vk::CommandPool command_pool, uint32_t frame_index)
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

    void RenderCommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{};
        m_handle->begin(binfo);
    }

    void RenderCommandBuffer::BeginRenderPass(
        const RenderPass& pass, 
        vk::Extent2D extent, 
        uint32_t framebuffer_id
    ) {
        vk::RenderPassBeginInfo info{
            pass.get(),
            pass.GetFramebuffers().GetFramebuffer(framebuffer_id),
            {vk::Offset2D{0, 0}, extent},
            pass.GetClearValues()
        };
        m_handle->beginRenderPass(info, vk::SubpassContents::eInline);
    }

    void RenderCommandBuffer::BindPipelineProgram(const Pipeline& pipeline) {
        m_handle->bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());
    }

    void RenderCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        m_handle->setViewport(0, 1, &vp);
        m_handle->setScissor(0, 1, &scissor);
    }

    void RenderCommandBuffer::CommitVertexBuffer(const HomogeneousMesh& mesh) {
        SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "Allocating staging buffer and memory for %u vertices.", mesh.GetVertexCount());
        auto buffer = mesh.WriteToStagingBuffer();
        vk::BufferCopy copy{0, 0, mesh.GetVertexCount() * HomogeneousMesh::SINGLE_VERTEX_BUFFER_SIZE_WITH_INDEX};
        m_handle->copyBuffer(buffer.GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh) {
        auto bindings = mesh.GetBindingInfo();
        m_handle->bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle->bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);
        m_handle->drawIndexed(mesh.GetVertexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::End() {
        m_handle->endRenderPass();
        m_handle->end();
    }

    void RenderCommandBuffer::SubmitToQueue(
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

    void RenderCommandBuffer::Reset() {
        m_handle->reset();
    }

} // namespace Engine

