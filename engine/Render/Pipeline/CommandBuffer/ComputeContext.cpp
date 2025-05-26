#include "ComputeContext.h"

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {

    struct ComputeContext::impl {
        ComputeCommandBuffer cb;
        std::vector <vk::ImageMemoryBarrier2> barriers {};
        impl(ComputeCommandBuffer &&cb_) : cb(std::move(cb_)) {}
    };

    void ComputeContext::UseImage(vk::Image img, ImageAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
    }

    ComputeContext::ComputeContext(
        ComputeCommandBuffer &&cb
    ) : pimpl(std::make_unique<ComputeContext::impl>(std::move(cb)))
    {
    }
    ComputeContext::~ComputeContext() = default;

    void ComputeContext::UseImage(vk::Image img, ImageComputeAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        switch(currentAccess) {
            case ImageComputeAccessType::ShaderRandomWrite:
                UseImage(img, ImageAccessType::ShaderRandomWrite, previousAccess);
                break;
            case ImageComputeAccessType::ShaderRead:
                UseImage(img, ImageAccessType::ShaderRead, previousAccess);
                break;
            case ImageComputeAccessType::ShaderReadRandomWrite:
                UseImage(img, ImageAccessType::ShaderReadRandomWrite, previousAccess);
                break;
        }
    }

    void ComputeContext::PrepareCommandBuffer()
    {
        if (pimpl->barriers.empty())    return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
    }
}
