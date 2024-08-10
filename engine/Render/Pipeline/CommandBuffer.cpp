#include "CommandBuffer.h"

#include "Render/Pipeline/Synchronization.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/Pipeline.h"

namespace Engine
{
    void CommandBuffer::CreateCommandBuffer(vk::Device logical_device, vk::CommandPool command_pool)
    {
        vk::CommandBufferAllocateInfo info{};
        info.commandPool = command_pool;
        info.commandBufferCount = 1;
        info.level = vk::CommandBufferLevel::ePrimary;

        auto cbvector = logical_device.allocateCommandBuffersUnique(info);
        assert(cbvector.size() == 1);
        m_handle = std::move(cbvector[0]);

        vk::CommandBufferBeginInfo binfo{};
        binfo.flags = vk::CommandBufferUsageFlags{0};
        binfo.pInheritanceInfo = nullptr;
        m_handle.get().begin(binfo);
    }

    void CommandBuffer::BeginRenderPass(const RenderPass& pass, uint32_t frame_index, vk::Extent2D extent)
    {
        vk::RenderPassBeginInfo info{};
        info.renderPass = pass.get();
        info.framebuffer = pass.GetFramebuffers().GetFramebuffer(frame_index);
        info.renderArea.offset = vk::Offset2D{0, 0};
        info.renderArea.extent = extent;

        const auto & attachments = pass.GetAttachments();
        std::vector <vk::ClearValue> clear_color{attachments.size()};
        for (size_t i = 0; i < attachments.size(); i++) {
            if (attachments[i].loadOp == vk::AttachmentLoadOp::eClear && attachments[i].stencilLoadOp == vk::AttachmentLoadOp::eClear) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "An attachment has both color and stencil load op set.");
            }

            if (attachments[i].loadOp == vk::AttachmentLoadOp::eClear) {
                clear_color.emplace_back(vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f});
            } else if (attachments[i].stencilLoadOp == vk::AttachmentLoadOp::eClear) {
                clear_color.emplace_back(vk::ClearDepthStencilValue{0.0f, 0u});
            }
        }
        info.clearValueCount = clear_color.size();
        info.pClearValues = clear_color.data();
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

    void CommandBuffer::Draw() {
        m_handle->draw(3, 1, 0, 0);
    }

    void CommandBuffer::End() {
        m_handle->endRenderPass();
        m_handle->end();
    }

    void CommandBuffer::SubmitToQueue(
        vk::Queue queue, 
        const Synchronization & synch,
        uint32_t frame_index
    ) {
        vk::SubmitInfo info{};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &m_handle.get();

        auto wait = synch.GetCommandBufferWaitSignals(frame_index);
        auto waitFlags = synch.GetCommandBufferWaitSignalFlags(frame_index);
        auto signal = synch.GetCommandBufferSigningSignals(frame_index);

        assert(wait.size() == waitFlags.size());
        info.waitSemaphoreCount = wait.size();
        info.pWaitSemaphores = wait.data();
        info.pWaitDstStageMask = waitFlags.data();

        info.signalSemaphoreCount = signal.size();
        info.pSignalSemaphores = signal.data();
        std::array<vk::SubmitInfo, 1> infos{info};
        queue.submit(infos, synch.GetCommandBufferFence(0));
    }

    void CommandBuffer::Reset() {
        m_handle.get().reset();
    }
    
} // namespace Engine

