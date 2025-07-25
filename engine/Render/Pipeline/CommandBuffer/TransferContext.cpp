#include "TransferContext.h"
#include "Render/Pipeline/CommandBuffer/TransferCommandBuffer.h"

namespace Engine {
    struct TransferContext::impl {
        TransferCommandBuffer cb;
        std::vector<vk::ImageMemoryBarrier2> barriers{};
        impl(TransferCommandBuffer &&_cb) : cb(std::move(_cb)) {};
    };
    TransferContext::TransferContext(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight) :
        pimpl(std::make_unique<TransferContext::impl>(TransferCommandBuffer(system, cb))) {
    }
    TransferContext::~TransferContext() = default;

    ICommandBuffer &TransferContext::GetCommandBuffer() const noexcept {
        return pimpl->cb;
    }

    void TransferContext::UseImage(
        const Texture &texture, ImageTransferAccessType currentAccess, ImageAccessType previousAccess
    ) noexcept {
        vk::ImageMemoryBarrier2 barrier;
        switch (currentAccess) {
        case ImageTransferAccessType::TransferRead:
            barrier = GetImageBarrier(texture, ImageAccessType::TransferRead, previousAccess);
            break;
        case ImageTransferAccessType::TransferWrite:
            barrier = GetImageBarrier(texture, ImageAccessType::TransferWrite, previousAccess);
            break;
        }
        pimpl->barriers.push_back(std::move(barrier));
    }

    void TransferContext::PrepareCommandBuffer() {
        if (pimpl->barriers.empty()) return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
        pimpl->barriers.clear();
    }

} // namespace Engine
