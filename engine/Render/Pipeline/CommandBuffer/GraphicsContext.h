#ifndef PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED

#include "ICommandContext.h"

namespace Engine {
    class RenderCommandBuffer;

    class GraphicsContext : public ICommandContext
    {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        using ImageGraphicsAccessType = AccessHelper::ImageGraphicsAccessType;
        
        GraphicsContext(RenderCommandBuffer && cb);
        virtual ~GraphicsContext();

        ICommandBuffer & GetCommandBuffer() const noexcept override;

        void UseImage(
            vk::Image img,
            ImageAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept override;

        void UseImage(
            vk::Image img,
            ImageGraphicsAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept;

        void PrepareCommandBuffer() override;
    };
}

#endif // PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
