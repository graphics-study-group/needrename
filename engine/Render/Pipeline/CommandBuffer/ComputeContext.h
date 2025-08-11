#ifndef PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED

#include "Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/ICommandContext.h"

namespace Engine {
    class Texture;
    class ComputeContext : public ICommandContext {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        using ImageComputeAccessType = AccessHelper::ImageComputeAccessType;

        ComputeContext(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);

        virtual ~ComputeContext();

        ICommandBuffer &GetCommandBuffer() const noexcept override;

        /**
         * @brief Mark an image for use for the following compute context.
         */
        void UseImage(
            const Texture &texture, ImageComputeAccessType currentAccess, ImageAccessType previousAccess
        ) noexcept;

        /**
         * @brief Set up barriers according to `UseX()` directives.
         */
        void PrepareCommandBuffer() override;
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_COMPUTECONTEXT_INCLUDED
