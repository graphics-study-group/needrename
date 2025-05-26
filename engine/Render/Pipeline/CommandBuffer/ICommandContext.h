#ifndef PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED

#include <vulkan/vulkan.hpp>
#include "AccessHelper.h"

namespace Engine {
    class ICommandBuffer;
    class ICommandContext {
    public:
        using ImageAccessType = AccessHelper::ImageAccessType;

        ICommandContext() = default;
        ICommandContext(const ICommandContext &) = delete;
        void operator = (const ICommandContext &) = delete;

        virtual ~ICommandContext() = default;

        virtual ICommandBuffer & GetCommandBuffer() const noexcept = 0;

        virtual void UseImage(
            vk::Image img,
            ImageAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept = 0;

        /* virtual void UseImage (
            vk::Image & img,
            vk::SubresourceRange range,
            ImageAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept = 0; */

        virtual void PrepareCommandBuffer () = 0;
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
