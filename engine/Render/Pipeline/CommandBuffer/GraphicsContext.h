#ifndef PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED

#include "TransferContext.h"

namespace Engine {
    class GraphicsCommandBuffer;

    class GraphicsContext : public TransferContext {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        using ImageGraphicsAccessType = AccessHelper::ImageGraphicsAccessType;

        GraphicsContext(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);
        virtual ~GraphicsContext();

        ICommandBuffer &GetCommandBuffer() const noexcept override;

        /**
         * @brief Mark an image for use for the following graphics context.
         */
        void UseImage(
            const Texture &texture, ImageGraphicsAccessType currentAccess, ImageAccessType previousAccess
        ) noexcept;

        /**
         * @brief Set up barriers according to `UseX()` directives.
         */
        void PrepareCommandBuffer() override;
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
