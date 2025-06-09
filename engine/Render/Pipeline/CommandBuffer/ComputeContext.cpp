#include "ComputeContext.h"

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {

    struct ComputeContext::impl {
        ComputeCommandBuffer cb;
        std::vector <vk::ImageMemoryBarrier2> barriers {};
        impl(ComputeCommandBuffer &&cb_) : cb(std::move(cb_)) {}
    };

    ComputeContext::ComputeContext(
        ComputeCommandBuffer &&cb
    ) : pimpl(std::make_unique<ComputeContext::impl>(std::move(cb)))
    {
    }
    ComputeContext::~ComputeContext() = default;

    ICommandBuffer & ComputeContext::GetCommandBuffer() const noexcept
    {
        return pimpl->cb;
    }

    void ComputeContext::UseImage(const Texture & texture, ImageComputeAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        vk::ImageMemoryBarrier2 barrier;
        switch(currentAccess) {
            case ImageComputeAccessType::ShaderRandomWrite:
                barrier = GetImageBarrier(texture, ImageAccessType::ShaderRandomWrite, previousAccess);
                break;
            case ImageComputeAccessType::ShaderRead:
                barrier = GetImageBarrier(texture, ImageAccessType::ShaderRead, previousAccess);
                break;
            case ImageComputeAccessType::ShaderReadRandomWrite:
                barrier = GetImageBarrier(texture, ImageAccessType::ShaderReadRandomWrite, previousAccess);
                break;
        }
        pimpl->barriers.push_back(std::move(barrier));
    }

    void ComputeContext::PrepareCommandBuffer()
    {
        if (pimpl->barriers.empty())    return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
    }
}
