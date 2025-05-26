#ifndef PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandContext.h"
#include "Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h"

namespace Engine {
    class ComputeContext : public ICommandContext {
        void UseImage(vk::Image img, ImageAccessType currentAccess, ImageAccessType previousAccess) noexcept override;
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        using ImageComputeAccessType = AccessHelper::ImageComputeAccessType;

        ComputeContext(ComputeCommandBuffer && cb);
        virtual ~ComputeContext();
        
        /**
         * @brief Mark an image for use for the following compute context.
         */
        void UseImage(vk::Image img, ImageComputeAccessType currentAccess, ImageAccessType previousAccess) noexcept;

        /**
         * @brief Set up barriers according to `UseX()` directives.
         */
        void PrepareCommandBuffer() override;
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
