#include "SubmissionHelper.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Texture.h"
#include "Render/Pipeline/CommandBuffer/BufferTransferHelper.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"
#include "Render/RenderSystem.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace Engine::RenderSystemState {
    struct SubmissionHelper::impl {
        std::queue<CmdOperation> m_pending_operations{};
        std::vector<Buffer> m_pending_dellocations{};

        vk::UniqueCommandBuffer m_one_time_cb{};
        vk::UniqueFence m_completion_fence{};
    };

    SubmissionHelper::SubmissionHelper(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
        // Pre-allocate a fence
        vk::FenceCreateInfo fcinfo{};
        pimpl->m_completion_fence = system.getDevice().createFenceUnique(fcinfo);
    }

    SubmissionHelper::~SubmissionHelper() = default;

    void SubmissionHelper::EnqueueVertexBufferSubmission(const HomogeneousMesh &mesh) {
        auto enqueued = [&mesh, this](vk::CommandBuffer cb) {
            std::array<vk::MemoryBarrier2, 1> barriers = {
                BufferTransferHelper::GetBufferBarrier(BufferTransferHelper::BufferTransferType::VertexBefore)
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});

            auto buffer{mesh.CreateStagingBuffer()};
            vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(mesh.GetExpectedBufferSize())};
            cb.copyBuffer(buffer.GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});

            barriers[0] = BufferTransferHelper::GetBufferBarrier(BufferTransferHelper::BufferTransferType::VertexAfter);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});
            pimpl->m_pending_dellocations.push_back(std::move(buffer));
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureBufferSubmission(
        const Texture &texture, const std::byte *data, size_t length
    ) {
        auto enqueued = [&texture, data, length, this](vk::CommandBuffer cb) {
            Buffer buffer{texture.CreateStagingBuffer()};
            assert(length <= buffer.GetSize());
            std::byte *mapped_ptr = buffer.Map();
            std::memcpy(mapped_ptr, data, length);
            buffer.Flush();
            buffer.Unmap();

            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureUploadBefore, texture.GetImage()
            )};
            vk::DependencyInfo dinfo{vk::DependencyFlags{}, {}, {}, barriers};
            cb.pipelineBarrier2(dinfo);

            // Copy buffer to image
            vk::BufferImageCopy copy{
                0,
                0,
                0,
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                vk::Offset3D{0, 0, 0},
                vk::Extent3D{
                    texture.GetTextureDescription().width,
                    texture.GetTextureDescription().height,
                    texture.GetTextureDescription().depth
                }
            };
            cb.copyBufferToImage(buffer.GetBuffer(), texture.GetImage(), vk::ImageLayout::eTransferDstOptimal, {copy});

            // Transfer image for sampling
            barriers[0] = LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureUploadAfter, texture.GetImage()
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);

            pimpl->m_pending_dellocations.push_back(std::move(buffer));
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color) {
        auto enqueued = [&texture, color, this](vk::CommandBuffer cb) {
            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureClearBefore, texture.GetImage()
            )};
            vk::DependencyInfo dinfo{vk::DependencyFlags{}, {}, {}, barriers};
            cb.pipelineBarrier2(dinfo);
            auto [r, g, b, a] = color;
            cb.clearColorImage(
                texture.GetImage(),
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearColorValue{r, g, b, a},
                {vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers
                }}
            );

            // Transfer image for sampling
            barriers[0] = LayoutTransferHelper::GetTextureBarrier(
                LayoutTransferHelper::TextureTransferType::TextureClearAfter, texture.GetImage()
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::ExecuteSubmission() {
        if (pimpl->m_pending_operations.empty()) return;

        // Allocate one-time command buffer
        vk::CommandBufferAllocateInfo cbainfo{
            m_system.getQueueInfo().graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto cbs = m_system.getDevice().allocateCommandBuffersUnique(cbainfo);
        assert(cbs.size() == 1);
        pimpl->m_one_time_cb = std::move(cbs[0]);
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), pimpl->m_one_time_cb.get(), "One-time submission CB");

        // Record all operations
        vk::CommandBufferBeginInfo cbbinfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        pimpl->m_one_time_cb->begin(cbbinfo);
        DEBUG_CMD_START_LABEL(pimpl->m_one_time_cb.get(), "Resource Submission");

        while (!pimpl->m_pending_operations.empty()) {
            auto enqueued = pimpl->m_pending_operations.front();
            enqueued(pimpl->m_one_time_cb.get());
            pimpl->m_pending_operations.pop();
        }

        DEBUG_CMD_END_LABEL(pimpl->m_one_time_cb.get());
        pimpl->m_one_time_cb->end();

        std::array<vk::CommandBuffer, 1> submitted_cb = {pimpl->m_one_time_cb.get()};
        std::array<vk::SubmitInfo, 1> sinfos = {vk::SubmitInfo{{}, {}, submitted_cb, {}}};
        m_system.getQueueInfo().graphicsQueue.submit(sinfos, {pimpl->m_completion_fence.get()});
    }

    void SubmissionHelper::CompleteFrame() {
        if (!pimpl->m_one_time_cb) return;

        auto wfresult = m_system.getDevice().waitForFences(
            {pimpl->m_completion_fence.get()}, true, std::numeric_limits<uint64_t>::max()
        );
        if (wfresult != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "An error occured when waiting for submission fence.");
        }

        m_system.getDevice().resetFences({pimpl->m_completion_fence.get()});
        pimpl->m_one_time_cb.reset();
        pimpl->m_pending_dellocations.clear();
    }

} // namespace Engine::RenderSystemState
