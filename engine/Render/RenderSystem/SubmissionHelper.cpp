#include "SubmissionHelper.h"

#include "Render/Memory/Image2DTexture.h"
#include "Render/Memory/Texture.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Pipeline/CommandBuffer/BufferTransferHelper.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"

#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace Engine::RenderSystemState {
    SubmissionHelper::SubmissionHelper(RenderSystem &system) : m_system(system)
    {
        // Pre-allocate a fence
        vk::FenceCreateInfo fcinfo {};
        m_completion_fence = system.getDevice().createFenceUnique(fcinfo);
    }

    void SubmissionHelper::EnqueueVertexBufferSubmission(const HomogeneousMesh &mesh)
    {
        auto enqueued = [&mesh, this] (vk::CommandBuffer cb) {
            std::array <vk::MemoryBarrier2, 1> barriers = {
                BufferTransferHelper::GetBufferBarrier(BufferTransferHelper::BufferTransferType::VertexBefore)
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});


            auto buffer {mesh.CreateStagingBuffer()};
            vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(mesh.GetExpectedBufferSize())};
            cb.copyBuffer(buffer.GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});

            barriers[0] = BufferTransferHelper::GetBufferBarrier(BufferTransferHelper::BufferTransferType::VertexAfter);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});
            m_pending_dellocations.push_back(std::move(buffer));
        };
        m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureBufferSubmission(const AllocatedImage2DTexture &texture, const std::byte *data, size_t length)
    {
        auto enqueued = [&texture, data, length, this] (vk::CommandBuffer cb) {
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
            cb.pipelineBarrier2(dinfo);

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
            cb.copyBufferToImage(
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
            cb.pipelineBarrier2(dinfo);

            m_pending_dellocations.push_back(std::move(buffer));
        };
        m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureBufferSubmission(const Texture &texture, const std::byte *data, size_t length)
    {
        auto enqueued = [&texture, data, length, this] (vk::CommandBuffer cb) {
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
            cb.pipelineBarrier2(dinfo);

            // Copy buffer to image
            vk::BufferImageCopy copy{
                0, 0, 0,
                vk::ImageSubresourceLayers {
                    vk::ImageAspectFlagBits::eColor,
                    0, 0, 1
                },
                vk::Offset3D{0, 0, 0},
                vk::Extent3D{
                    texture.GetTextureDescription().width,
                    texture.GetTextureDescription().height,
                    texture.GetTextureDescription().depth
                }
            };
            cb.copyBufferToImage(
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
            cb.pipelineBarrier2(dinfo);

            m_pending_dellocations.push_back(std::move(buffer));
        };
        m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureClear(const AllocatedImage2DTexture &texture, std::tuple<float, float, float, float> color)
    {
        auto enqueued = [&texture, color, this] (vk::CommandBuffer cb) {
            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {
                LayoutTransferHelper::GetTextureBarrier(LayoutTransferHelper::TextureTransferType::TextureClearBefore, texture.GetImage())
            };
            vk::DependencyInfo dinfo {
                vk::DependencyFlags{},
                {}, {}, barriers
            };
            cb.pipelineBarrier2(dinfo);
            auto [r, g, b, a] = color;
            cb.clearColorImage(
                texture.GetImage(), 
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearColorValue{r, g, b, a},
                {
                    vk::ImageSubresourceRange{
                        vk::ImageAspectFlagBits::eColor,
                        0, vk::RemainingMipLevels,
                        0, vk::RemainingArrayLayers
                    }
                }
            );

            // Transfer image for sampling
            barriers[0] = LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureClearAfter, 
                texture.GetImage()
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color)
    {
        auto enqueued = [&texture, color, this] (vk::CommandBuffer cb) {
            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {
                LayoutTransferHelper::GetTextureBarrier(LayoutTransferHelper::TextureTransferType::TextureClearBefore, texture.GetImage())
            };
            vk::DependencyInfo dinfo {
                vk::DependencyFlags{},
                {}, {}, barriers
            };
            cb.pipelineBarrier2(dinfo);
            auto [r, g, b, a] = color;
            cb.clearColorImage(
                texture.GetImage(), 
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearColorValue{r, g, b, a},
                {
                    vk::ImageSubresourceRange{
                        vk::ImageAspectFlagBits::eColor,
                        0, vk::RemainingMipLevels,
                        0, vk::RemainingArrayLayers
                    }
                }
            );

            // Transfer image for sampling
            barriers[0] = LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureClearAfter, 
                texture.GetImage()
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::StartFrame()
    {
        if (m_pending_operations.empty())   return;

        // Allocate one-time command buffer
        vk::CommandBufferAllocateInfo cbainfo {
            m_system.getQueueInfo().graphicsPool.get(),
            vk::CommandBufferLevel::ePrimary,
            1
        };
        auto cbs = m_system.getDevice().allocateCommandBuffersUnique(cbainfo);
        assert(cbs.size() == 1);
        m_one_time_cb = std::move(cbs[0]);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), m_one_time_cb.get(), "One-time submission CB");

        // Record all operations
        vk::CommandBufferBeginInfo cbbinfo {
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        m_one_time_cb->begin(cbbinfo);
        DEBUG_CMD_START_LABEL(m_one_time_cb.get(), "Resource Submission");

        while(!m_pending_operations.empty()) {
            auto enqueued = m_pending_operations.front();
            enqueued(m_one_time_cb.get());
            m_pending_operations.pop();
        }

        DEBUG_CMD_END_LABEL(m_one_time_cb.get());
        m_one_time_cb->end();

        std::array <vk::CommandBuffer, 1> submitted_cb = {m_one_time_cb.get()};
        std::array <vk::SubmitInfo, 1> sinfos = {
            vk::SubmitInfo{
                {}, {}, submitted_cb, {}
            }
        };
        m_system.getQueueInfo().graphicsQueue.submit(sinfos, {m_completion_fence.get()});
    }

    void SubmissionHelper::CompleteFrame()
    {
        if (!m_one_time_cb) return;
        
        auto wfresult = m_system.getDevice().waitForFences(
            {m_completion_fence.get()}, 
            true, 
            std::numeric_limits<uint64_t>::max()
        );
        if (wfresult != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "An error occured when waiting for submission fence.");
        }

        m_system.getDevice().resetFences({m_completion_fence.get()});
        m_one_time_cb.reset();
        m_pending_dellocations.clear();
    }

}
