#ifndef PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED

#include "ICommandContext.h"

namespace Engine {
    class RenderCommandBuffer;

    class GraphicsContext : ICommandContext
    {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        GraphicsContext(RenderCommandBuffer && cb);
        virtual ~GraphicsContext();

        ICommandBuffer & GetCommandBuffer() const noexcept override;

        void UseImage(
            vk::Image & img,
            AccessType currentAccess,
            AccessType previousAccess,
            ContextType previousCtx
        ) noexcept override;

        void PrepareCommandBuffer() override;
    };
}

#endif // PIPELINE_COMMANDBUFFER_GRAPHICSCONTEXT_INCLUDED
