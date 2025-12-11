#include "SubmissionHelper.h"

#include "Render/Memory/Buffer.h"
#include "Render/Memory/Texture.h"
#include "Render/Pipeline/CommandBuffer/BufferTransferHelper.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace {
    enum class TextureTransferType {
        TextureUploadBefore,
        TextureUploadAfter,
        TextureClearBefore,
        TextureClearAfter
    };

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1(TextureTransferType type) {
        using enum TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2(TextureTransferType type) {
        using enum TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(TextureTransferType type) {
        using enum TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eReadOnlyOptimal);
        }
        __builtin_unreachable();
    }

    vk::ImageMemoryBarrier2 GetTextureBarrier(TextureTransferType type, vk::Image image, vk::ImageAspectFlags aspect) {
        auto [scope1, scope2, layouts] = std::tuple{GetScope1(type), GetScope2(type), GetLayouts(type)};
        vk::ImageSubresourceRange subresource{
            aspect, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers
        };
        vk::ImageMemoryBarrier2 barrier{
            scope1.first,
            scope1.second,
            scope2.first,
            scope2.second,
            layouts.first,
            layouts.second,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            image,
            subresource
        };
        return barrier;
    }
}

namespace Engine::RenderSystemState {
    struct SubmissionHelper::impl {
        std::queue<CmdOperation> m_pending_operations{};
        std::vector<Buffer> m_pending_dellocations{};

        vk::UniqueCommandBuffer m_one_time_cb{};
        vk::UniqueFence m_completion_fence{};
    };

    SubmissionHelper::SubmissionHelper(RenderSystem &system) : IFrameManagerComponent(system), pimpl(std::make_unique<impl>()) {
        // Pre-allocate a fence
        vk::FenceCreateInfo fcinfo{};
        pimpl->m_completion_fence = system.GetDevice().createFenceUnique(fcinfo);
    }

    SubmissionHelper::~SubmissionHelper() = default;

    void SubmissionHelper::EnqueueVertexBufferSubmission(const HomogeneousMesh &mesh) {
        auto enqueued = [&mesh, this](vk::CommandBuffer cb) {
            std::array<vk::MemoryBarrier2, 1> barriers = {
                BufferTransferHelper::GetBufferBarrier(BufferTransferHelper::BufferTransferType::VertexBefore)
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});

            auto buffer{mesh.CreateStagingBuffer(m_system.GetAllocatorState())};
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
        assert(ImageUtils::GetVkAspect(texture.GetTextureDescription().format) & vk::ImageAspectFlagBits::eColor);

        auto enqueued = [&texture, data, length, this](vk::CommandBuffer cb) {
            Buffer buffer{texture.CreateStagingBuffer(m_system.GetAllocatorState())};
            assert(length <= buffer.GetSize());
            std::byte *mapped_ptr = buffer.GetVMAddress();
            std::memcpy(mapped_ptr, data, length);
            buffer.Flush();

            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {GetTextureBarrier(
                TextureTransferType::TextureUploadBefore, texture.GetImage(), ImageUtils::GetVkAspect(texture.GetTextureDescription().format)
            )};
            vk::DependencyInfo dinfo{vk::DependencyFlags{}, {}, {}, barriers};
            cb.pipelineBarrier2(dinfo);

            // Copy buffer to image
            vk::BufferImageCopy copy{
                0,
                0,
                0,
                vk::ImageSubresourceLayers{ImageUtils::GetVkAspect(texture.GetTextureDescription().format), 0, 0, texture.GetTextureDescription().array_layers},
                vk::Offset3D{0, 0, 0},
                vk::Extent3D{
                    texture.GetTextureDescription().width,
                    texture.GetTextureDescription().height,
                    texture.GetTextureDescription().depth
                }
            };
            cb.copyBufferToImage(buffer.GetBuffer(), texture.GetImage(), vk::ImageLayout::eTransferDstOptimal, {copy});

            // Transfer image for sampling
            barriers[0] = GetTextureBarrier(
                TextureTransferType::TextureUploadAfter, texture.GetImage(), ImageUtils::GetVkAspect(texture.GetTextureDescription().format)
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);

            pimpl->m_pending_dellocations.push_back(std::move(buffer));
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color) {
        assert(ImageUtils::GetVkAspect(texture.GetTextureDescription().format) & vk::ImageAspectFlagBits::eColor);

        auto enqueued = [&texture, color, this](vk::CommandBuffer cb) {
            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {GetTextureBarrier(
                TextureTransferType::TextureClearBefore, texture.GetImage(), vk::ImageAspectFlagBits::eColor
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
            barriers[0] = GetTextureBarrier(
                TextureTransferType::TextureClearAfter, texture.GetImage(), vk::ImageAspectFlagBits::eColor
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, float depth) {
        assert(0.0f <= depth && depth <= 1.0f);
        assert(ImageUtils::GetVkAspect(texture.GetTextureDescription().format) & vk::ImageAspectFlagBits::eDepth);
    
        auto enqueued = [&texture, depth, this](vk::CommandBuffer cb) {
            // Transit layout to TransferDstOptimal
            std::array<vk::ImageMemoryBarrier2, 1> barriers = {GetTextureBarrier(
                TextureTransferType::TextureClearBefore, texture.GetImage(), vk::ImageAspectFlagBits::eDepth
            )};
            vk::DependencyInfo dinfo{vk::DependencyFlags{}, {}, {}, barriers};
            cb.pipelineBarrier2(dinfo);
            cb.clearDepthStencilImage(
                texture.GetImage(),
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearDepthStencilValue{depth, 0U},
                {vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eDepth, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers
                }}
            );

            // Transfer image for sampling
            barriers[0] = GetTextureBarrier(
                TextureTransferType::TextureClearAfter, texture.GetImage(), vk::ImageAspectFlagBits::eDepth
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::ExecuteSubmission() {
        if (pimpl->m_pending_operations.empty()) return;

        // Allocate one-time command buffer
        const auto & queue_info = m_system.GetDeviceInterface().GetQueueInfo();
        vk::CommandBufferAllocateInfo cbainfo{
            queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto cbs = m_system.GetDevice().allocateCommandBuffersUnique(cbainfo);
        assert(cbs.size() == 1);
        pimpl->m_one_time_cb = std::move(cbs[0]);
        DEBUG_SET_NAME_TEMPLATE(m_system.GetDevice(), pimpl->m_one_time_cb.get(), "One-time submission CB");

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
        queue_info.graphicsQueue.submit(sinfos, {pimpl->m_completion_fence.get()});
    }

    void SubmissionHelper::OnPreMainCbSubmission()
    {
        this->ExecuteSubmission();
    }

    void SubmissionHelper::OnFrameComplete() {
        if (!pimpl->m_one_time_cb) return;

        auto wfresult = m_system.GetDevice().waitForFences(
            {pimpl->m_completion_fence.get()}, true, std::numeric_limits<uint64_t>::max()
        );
        if (wfresult != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "An error occured when waiting for submission fence.");
        }

        m_system.GetDevice().resetFences({pimpl->m_completion_fence.get()});
        pimpl->m_one_time_cb.reset();
        pimpl->m_pending_dellocations.clear();
    }

} // namespace Engine::RenderSystemState
