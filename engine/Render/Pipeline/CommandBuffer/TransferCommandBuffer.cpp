#include "TransferCommandBuffer.h"

#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "LayoutTransferHelper.h"

namespace Engine {
    void TransferCommandBuffer::Create(
        std::shared_ptr<RenderSystem> system, 
        vk::CommandPool command_pool, 
        vk::Queue queue
    ) {
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
        m_system = system.get();
        m_queue = queue;
    }

    void TransferCommandBuffer::CommitVertexBuffer(const HomogeneousMesh& mesh) {
        // auto device = m_system.lock()->getDevice();
        // device.waitIdle();

        // Set up a barrier for buffer transfering
        std::array <vk::MemoryBarrier2, 1> barriers = {vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eVertexInput,
            vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite
        }};
        m_handle->pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});


        auto buffer {mesh.CreateStagingBuffer()};
        vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(mesh.GetExpectedBufferSize())};
        m_handle->copyBuffer(buffer.GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});

        barriers[0] = {
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite |,
            vk::PipelineStageFlagBits2::eVertexInput,
            vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead
        };
        m_handle->pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});
        m_pending_buffers.push_back(std::move(buffer));
    }

    void TransferCommandBuffer::CommitTextureImage(const AllocatedImage2DTexture& texture, std::byte * data, size_t length) {
        Buffer buffer {texture.CreateStagingBuffer()};
        assert(length <= buffer.GetSize());
        std::byte * mapped_ptr = buffer.Map();
        std::memcpy(mapped_ptr, data, length);
        buffer.Flush();
        buffer.Unmap();

        // Transit layout to TransferDstOptimal
        std::array<vk::ImageMemoryBarrier2, 1> barriers = {
            LayoutTransferHelper::GetTextureBarrier(LayoutTransferHelper::TextureTransferType::TextureUploadBefore, texture.GetImage())
        };
        vk::DependencyInfo dinfo {
            vk::DependencyFlags{},
            {}, {}, barriers
        };
        m_handle->pipelineBarrier2(dinfo);

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
        barriers[0] = LayoutTransferHelper::GetTextureBarrier(
            LayoutTransferHelper::TextureTransferType::TextureUploadAfter, 
            texture.GetImage()
        );
        dinfo.setImageMemoryBarriers(barriers);
        m_handle->pipelineBarrier2(dinfo);

        m_pending_buffers.push_back(std::move(buffer));
    }

    void TransferCommandBuffer::Begin() {
        vk::CommandBufferBeginInfo binfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        m_handle->begin(binfo);
    }

    void TransferCommandBuffer::End() {
        m_handle->end();
    }

    void TransferCommandBuffer::SubmitAndExecute() {
        auto device = m_system->getDevice();

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
}
