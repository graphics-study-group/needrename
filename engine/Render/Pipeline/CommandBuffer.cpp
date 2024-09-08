#include "CommandBuffer.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Material/Material.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/Synch/Synchronization.h"

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
        m_system = system;
        m_queue = queue;
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

    void RenderCommandBuffer::BindMaterial(const Material & material, uint32_t pass_index) {
        m_handle->bindPipeline(vk::PipelineBindPoint::eGraphics, material.GetPipeline(pass_index)->get());
        m_bound_material = std::make_pair(std::cref(material), pass_index);
        const auto & global_pool = m_system.lock()->GetGlobalConstantDescriptorPool();
        const auto & per_camera_descriptor_set = global_pool.GetPerCameraConstantSet(m_inflight_frame_index);
        auto material_descriptor_set = material.GetDescriptorSet(pass_index);

        if (material_descriptor_set) {
            m_handle->bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                material.GetPipelineLayout(pass_index)->get(), 
                0,
                {per_camera_descriptor_set, material_descriptor_set},
                {}
            );
        } else {
            m_handle->bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, 
                material.GetPipelineLayout(pass_index)->get(), 
                0,
                {per_camera_descriptor_set},
                {}
            );
        }
        
        // TODO: Write per-material descriptors
    }

    void RenderCommandBuffer::SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor) {
        vk::Viewport vp;
        vp.setWidth(vpWidth).setHeight(vpHeight);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);

        m_handle->setViewport(0, 1, &vp);
        m_handle->setScissor(0, 1, &scissor);
    }

    void RenderCommandBuffer::DrawMesh(const HomogeneousMesh& mesh) {
        // TODO: Write per-mesh descriptors
        // which are typically the model transform (via push constant) and skeletal transforms.
        auto bindings = mesh.GetBindingInfo();
        m_handle->bindVertexBuffers(0, bindings.first, bindings.second);
        auto indices = mesh.GetIndexInfo();
        m_handle->bindIndexBuffer(indices.first, indices.second, vk::IndexType::eUint32);
        m_handle->drawIndexed(mesh.GetVertexIndexCount(), 1, 0, 0, 0);
    }

    void RenderCommandBuffer::End() {
        m_handle->endRenderPass();
        m_handle->end();
    }

    void RenderCommandBuffer::Submit(const Synchronization & synch) {
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
        m_queue.submit(infos, synch.GetCommandBufferFence(m_inflight_frame_index));
    }

    void RenderCommandBuffer::Reset() {
        m_handle->reset();
        m_bound_material.reset();
    }

    void OneTimeCommandBuffer::Create(std::shared_ptr<RenderSystem> system, vk::CommandPool command_pool, vk::Queue queue) {
        // Allocate command buffer
        vk::CommandBufferAllocateInfo info{
            command_pool, vk::CommandBufferLevel::ePrimary, 1
        };
        auto cbvector = system->getDevice().allocateCommandBuffersUnique(info);
        assert(cbvector.size() == 1);
        m_handle = std::move(cbvector[0]);

        // Create fence
        vk::FenceCreateInfo finfo {};
        m_complete_fence = system->getDevice().createFenceUnique(finfo);
        m_system = system;
        m_queue = queue;
    }

    void OneTimeCommandBuffer::CommitVertexBuffer(const HomogeneousMesh& mesh) {
        // auto device = m_system.lock()->getDevice();
        // device.waitIdle();

        // Set up a barrier for buffer transfering
        vk::MemoryBarrier barrier {
            vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead,
            vk::AccessFlagBits::eTransferWrite
        };
        m_handle->pipelineBarrier(
            vk::PipelineStageFlagBits::eVertexInput,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{0},
            { barrier },
            {},
            {}
        );

        auto buffer {mesh.CreateStagingBuffer()};
        vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(mesh.GetExpectedBufferSize())};
        m_handle->copyBuffer(buffer.GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});

        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead;
        m_handle->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eVertexInput,
            vk::DependencyFlags{0},
            { barrier },
            {},
            {}
        );

        m_pending_buffers.push_back(std::move(buffer));
    }

    void OneTimeCommandBuffer::CommitTextureImage(const AllocatedImage2DTexture& texture, std::byte * data, size_t length) {
        Buffer buffer {texture.CreateStagingBuffer()};
        assert(length <= buffer.GetSize());
        std::byte * mapped_ptr = buffer.Map();
        std::memcpy(mapped_ptr, data, length);
        buffer.Unmap();

        // Transit layout to TransferDstOptimal
        vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};  // FIXME: mip level
        vk::ImageMemoryBarrier barrier {
            vk::AccessFlagBits::eShaderRead,
            vk::AccessFlagBits::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            texture.GetImage(),
            range
        };
        m_handle->pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader, 
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{0},
            {},
            {},
            { barrier }
        );

        // Copy buffer to image
        vk::BufferImageCopy copy{
            0, 0, 0,
            vk::ImageSubresourceLayers {
                vk::ImageAspectFlagBits::eColor,
                0, 0, 1
            },
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{texture.GetExtent(), 1}
        };
        m_handle->copyBufferToImage(
            buffer.GetBuffer(), 
            texture.GetImage(), 
            vk::ImageLayout::eTransferDstOptimal,
            { copy }
        );

        // Transfer image for sampling
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        m_handle->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, 
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{0},
            {},
            {},
            { barrier }
        );

        m_pending_buffers.push_back(std::move(buffer));
    }

    void OneTimeCommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        m_handle->begin(binfo);
    }

    void OneTimeCommandBuffer::End() {
        m_handle->end();
    }

    void OneTimeCommandBuffer::SubmitAndExecute() {
        auto device = m_system.lock()->getDevice();

        vk::SubmitInfo info{};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &m_handle.get();
        info.waitSemaphoreCount = 0;
        info.signalSemaphoreCount = 0;

        std::array<vk::SubmitInfo, 1> infos{info};
        m_queue.submit(infos, m_complete_fence.get());
        auto fence_result = device.waitForFences({m_complete_fence.get()}, vk::True, std::numeric_limits<uint64_t>::max());
        if (fence_result == vk::Result::eTimeout) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for transfer command buffer execution.");
        }
        device.resetFences({m_complete_fence.get()});
        m_pending_buffers.clear();
    }
} // namespace Engine

