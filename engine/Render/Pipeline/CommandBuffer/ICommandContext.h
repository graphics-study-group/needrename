#ifndef PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class ICommandBuffer;
    class ICommandContext {
    public:
        enum class AccessType {
            None,
            // Read by fragment shaders, etc.
            Read,
            // Write by graphics as attachments or transfer commands.
            Write,
            // Random write by compute shaders.
            RandomWrite
        };

        enum class ContextType {
            None,
            Graphics,
            Compute,
            Transfer
        };

        ICommandContext() = default;
        ICommandContext(const ICommandContext &) = delete;
        void operator = (const ICommandContext &) = delete;

        virtual ~ICommandContext() = default;

        virtual ICommandBuffer & GetCommandBuffer() const noexcept = 0;

        virtual void UseImage(
            vk::Image & img,
            AccessType currentAccess,
            AccessType previousAccess,
            ContextType previousCtx
        ) noexcept = 0;

        /* virtual void UseImage (
            vk::Image & img,
            vk::SubresourceRange range,
            AccessType currentAccess,
            AccessType previousAccess,
            ContextType previousCtx
        ) noexcept = 0; */

        virtual void PrepareCommandBuffer () = 0;
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
