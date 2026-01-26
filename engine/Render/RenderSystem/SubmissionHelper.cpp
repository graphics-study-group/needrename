#include "SubmissionHelper.h"

#include "Render/Memory/DeviceBuffer.h"
#include "Render/Memory/Texture.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/RenderSystem/FrameSemaphore.hpp"

#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace {
    enum class TextureTransferType {
        TextureUploadBefore,
        TextureUploadAfter,
        TextureClearBefore,
        TextureClearAfter
    };

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1Image(TextureTransferType type) {
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
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2Image(TextureTransferType type) {
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
        auto [scope1, scope2, layouts] = std::tuple{GetScope1Image(type), GetScope2Image(type), GetLayouts(type)};
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

    enum class BufferTransferType {
        // Barrier before writing to a vertex buffer.
        VertexBefore,
        // Barrier after writing to a vertex buffer.
        VertexAfter,
        // General transfer to a unknown buffer
        GeneralTransferBefore,
        // General transfer to a unknown buffer
        GeneralTransferAfter
    };

    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope1Buffer(BufferTransferType type) {
        switch (type) {
        case BufferTransferType::VertexBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eVertexInput,
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead
            );
        case BufferTransferType::VertexAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        case BufferTransferType::GeneralTransferBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
            );
        case BufferTransferType::GeneralTransferAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite
            );
        default:
            assert(false && "Unexpected buffer transfer type.");
            return std::make_pair(vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone);
        }
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope2Buffer(BufferTransferType type) {
        switch (type) {
        case BufferTransferType::VertexBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        case BufferTransferType::VertexAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eVertexInput,
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead
            );
        case BufferTransferType::GeneralTransferBefore:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite
            );
        case BufferTransferType::GeneralTransferAfter:
            return std::make_pair(
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
            );
        default:
            assert(false && "Unexpected buffer transfer type.");
            return std::make_pair(vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone);
        }
    }

    vk::MemoryBarrier2 GetBufferBarrier(BufferTransferType type) {
        return vk::MemoryBarrier2{
            GetScope1Buffer(type).first,
            GetScope1Buffer(type).second,
            GetScope2Buffer(type).first,
            GetScope2Buffer(type).second
        };
    }
}

namespace Engine::RenderSystemState {
    struct SubmissionHelper::impl {
        std::queue<CmdOperation> m_pending_operations{};
        std::vector<std::unique_ptr<DeviceBuffer>> m_pending_dellocations{};

        vk::UniqueCommandBuffer m_one_time_cb{};
        vk::UniqueFence m_completion_fence{};
    };

    SubmissionHelper::SubmissionHelper(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
        // Pre-allocate a fence
        vk::FenceCreateInfo fcinfo{};
        pimpl->m_completion_fence = system.GetDevice().createFenceUnique(fcinfo);
    }

    SubmissionHelper::~SubmissionHelper() = default;

    void SubmissionHelper::EnqueueBufferSubmission(const DeviceBuffer &buffer, std::vector<std::byte> &&data) {
        assert(data.size() >= buffer.GetSize());

        auto staging_buffer = DeviceBuffer::CreateUnique(
                this->m_system.GetAllocatorState(),
                {BufferTypeBits::StagingToDevice},
                buffer.GetSize(),
                "Staging buffer"
            );
        std::memcpy(staging_buffer->GetVMAddress(), data.data(), sizeof(buffer.GetSize()));
        staging_buffer->Flush();
        data.clear();

        auto enqueued = [
            data = std::move(data), &buffer, this,
            pbuf = staging_buffer.get()
        ](vk::CommandBuffer cb) {
            auto mbarrier = GetBufferBarrier(BufferTransferType::GeneralTransferBefore);
            std::array<vk::BufferMemoryBarrier2, 1> barriers{};
            barriers[0] = {
                mbarrier.srcStageMask, mbarrier.srcAccessMask,
                mbarrier.dstStageMask, mbarrier.dstAccessMask,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                buffer.GetBuffer(),
                buffer.GetSize()
            };
            
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, barriers, {}});
            vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(buffer.GetSize())};
            cb.copyBuffer(pbuf->GetBuffer(), buffer.GetBuffer(), {copy});

            mbarrier = GetBufferBarrier(BufferTransferType::GeneralTransferAfter);
            barriers[0] = {
                mbarrier.srcStageMask, mbarrier.srcAccessMask,
                mbarrier.dstStageMask, mbarrier.dstAccessMask,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                buffer.GetBuffer(),
                buffer.GetSize()
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, barriers, {}});
        };
        pimpl->m_pending_dellocations.push_back(std::move(staging_buffer));
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueBufferSubmission(const DeviceBuffer &buffer, const std::vector<std::byte> &data) {
        auto staging_buffer = DeviceBuffer::CreateUnique(
                this->m_system.GetAllocatorState(),
                {BufferTypeBits::StagingToDevice},
                buffer.GetSize(),
                "Staging buffer"
            );
        std::memcpy(staging_buffer->GetVMAddress(), data.data(), sizeof(buffer.GetSize()));
        staging_buffer->Flush();

        auto enqueued = [
            data = std::move(data), &buffer, this,
            pbuf = staging_buffer.get()
        ](vk::CommandBuffer cb) {
            auto mbarrier = GetBufferBarrier(BufferTransferType::GeneralTransferBefore);
            std::array<vk::BufferMemoryBarrier2, 1> barriers{};
            barriers[0] = {
                mbarrier.srcStageMask, mbarrier.srcAccessMask,
                mbarrier.dstStageMask, mbarrier.dstAccessMask,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                buffer.GetBuffer(),
                buffer.GetSize()
            };
            
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, barriers, {}});
            vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(buffer.GetSize())};
            cb.copyBuffer(pbuf->GetBuffer(), buffer.GetBuffer(), {copy});

            mbarrier = GetBufferBarrier(BufferTransferType::GeneralTransferAfter);
            barriers[0] = {
                mbarrier.srcStageMask, mbarrier.srcAccessMask,
                mbarrier.dstStageMask, mbarrier.dstAccessMask,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                buffer.GetBuffer(),
                buffer.GetSize()
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, barriers, {}});
        };
        pimpl->m_pending_dellocations.push_back(std::move(staging_buffer));
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueVertexBufferSubmission(const HomogeneousMesh &mesh) {
        auto buffer{mesh.CreateStagingBuffer(m_system.GetAllocatorState())};

        auto enqueued = [&mesh, this, pbuf = buffer.get()](vk::CommandBuffer cb) {
            std::array<vk::MemoryBarrier2, 1> barriers = {
                GetBufferBarrier(BufferTransferType::VertexBefore)
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});

            vk::BufferCopy copy{0, 0, static_cast<vk::DeviceSize>(mesh.GetExpectedBufferSize())};
            cb.copyBuffer(pbuf->GetBuffer(), mesh.GetBuffer().GetBuffer(), {copy});

            barriers[0] = GetBufferBarrier(BufferTransferType::VertexAfter);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, barriers, {}, {}});
        };
        pimpl->m_pending_dellocations.push_back(std::move(buffer));
        pimpl->m_pending_operations.push(enqueued);
    }

    void SubmissionHelper::EnqueueTextureBufferSubmission(
        const Texture &texture, const std::byte *data, size_t length
    ) {
        assert(ImageUtils::GetVkAspect(texture.GetTextureDescription().format) & vk::ImageAspectFlagBits::eColor);

        auto staging_buffer{texture.CreateStagingBuffer(m_system.GetAllocatorState())};
        assert(length <= staging_buffer->GetSize());
        std::byte *mapped_ptr = staging_buffer->GetVMAddress();
        std::memcpy(mapped_ptr, data, length);
        staging_buffer->Flush();

        auto enqueued = [
            &texture,
            pbuf = staging_buffer.get(),
            data, length, this](vk::CommandBuffer cb) {

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
            cb.copyBufferToImage(pbuf->GetBuffer(), texture.GetImage(), vk::ImageLayout::eTransferDstOptimal, {copy});

            // Transfer image for sampling
            barriers[0] = GetTextureBarrier(
                TextureTransferType::TextureUploadAfter, texture.GetImage(), ImageUtils::GetVkAspect(texture.GetTextureDescription().format)
            );
            dinfo.setImageMemoryBarriers(barriers);
            cb.pipelineBarrier2(dinfo);
        };
        pimpl->m_pending_dellocations.push_back(std::move(staging_buffer));
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
        auto & frame_semaphore = m_system.GetFrameManager().GetFrameSemaphore();
        if (pimpl->m_pending_operations.empty()) {
            // We do not need to worry about synchronization too much
            // as timeline semaphores are guaranteed to be monotonically increasing.
            m_system.GetDevice().signalSemaphore(
                vk::SemaphoreSignalInfo{
                    frame_semaphore.timeline_semaphore.get(),
                    frame_semaphore.GetTimepointValue(FrameSemaphore::TimePoint::PreTransferFinished)
                }
            );
            return;
        }

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

        vk::SemaphoreSubmitInfo wait_info{}, signal_info{};
        wait_info = frame_semaphore.GetSubmitInfo(FrameSemaphore::TimePoint::Pending, vk::PipelineStageFlagBits2::eAllCommands);
        signal_info = frame_semaphore.GetSubmitInfo(FrameSemaphore::TimePoint::PreTransferFinished, vk::PipelineStageFlagBits2::eAllTransfer);
        vk::CommandBufferSubmitInfo cbsinfo{pimpl->m_one_time_cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {wait_info}, {cbsinfo}, {signal_info}};
        queue_info.graphicsQueue.submit2(sinfo, pimpl->m_completion_fence.get());
    }

    void SubmissionHelper::ExecuteSubmissionImmediately() {
        if (pimpl->m_pending_operations.empty())    return;

        vk::UniqueFence fence = m_system.GetDevice().createFenceUnique(vk::FenceCreateInfo{});

        const auto & queue_info = m_system.GetDeviceInterface().GetQueueInfo();
        vk::CommandBufferAllocateInfo cbainfo{
            queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto cbs = m_system.GetDevice().allocateCommandBuffersUnique(cbainfo);
        assert(cbs.size() == 1);
        auto cb = std::move(cbs[0]);
        DEBUG_SET_NAME_TEMPLATE(m_system.GetDevice(), cb.get(), "One-time submission CB");

        // Record all operations
        vk::CommandBufferBeginInfo cbbinfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        cb->begin(cbbinfo);
        DEBUG_CMD_START_LABEL(cb.get(), "Resource Submission");

        while (!pimpl->m_pending_operations.empty()) {
            auto enqueued = pimpl->m_pending_operations.front();
            enqueued(cb.get());
            pimpl->m_pending_operations.pop();
        }

        DEBUG_CMD_END_LABEL(cb.get());
        cb->end();

        // Submit and wait for the fence.
        vk::CommandBufferSubmitInfo cbsinfo{cb.get()};
        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, {}, {cbsinfo}, {}};
        queue_info.graphicsQueue.submit2(sinfo, fence.get());
        m_system.GetDevice().waitForFences({fence.get()}, true, std::numeric_limits<uint64_t>::max());
        pimpl->m_pending_dellocations.clear();
    }

    void SubmissionHelper::OnPreMainCbSubmission() {
        this->ExecuteSubmission();
    }

    void SubmissionHelper::OnFrameComplete() {
        if (!pimpl->m_one_time_cb) return;
        // Is this fence wait actually needed?
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
