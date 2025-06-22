#ifndef PIPELINE_COMMANDBUFFER_TRANSFERCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_TRANSFERCONTEXT_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandContext.h"

namespace Engine
{
    class TransferContext : public ICommandContext 
    {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        using ImageTransferAccessType = AccessHelper::ImageTransferAccessType;

        TransferContext(RenderSystem & system,
            vk::CommandBuffer cb,
            uint32_t frame_in_flight
        );

        virtual ~TransferContext();

        ICommandBuffer & GetCommandBuffer() const noexcept override;

        /**
         * @brief Mark an image for use for the following graphics context.
         */
        void UseImage(
            const Texture & texture,
            ImageTransferAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept;

        /**
         * @brief Set up barriers according to `UseX()` directives.
         */
        void PrepareCommandBuffer() override;
    };
} // namespace Engine


#endif // PIPELINE_COMMANDBUFFER_TRANSFERCONTEXT_INCLUDED
