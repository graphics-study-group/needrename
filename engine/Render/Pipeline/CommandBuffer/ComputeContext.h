#ifndef PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandContext.h"

namespace Engine {
    class ComputeContext : public ICommandContext {
    public:
        using ImageComputeAccessType = AccessHelper::ImageComputeAccessType;
        void UseImage(vk::Image img, ImageAccessType currentAccess, ImageAccessType previousAccess) noexcept override;
        void UseImage(vk::Image img, ImageComputeAccessType currentAccess, ImageAccessType previousAccess) noexcept;
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
