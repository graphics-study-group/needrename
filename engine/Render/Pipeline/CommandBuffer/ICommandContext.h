#ifndef PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class ICommandBuffer;
    class ICommandContext {
    public:
        enum class ImageAccessType {
            None,
            /// Read by transfer command, in transfer source layout.
            TransferRead,
            /// Read by rasterization pipeline in attachment load operation, in color attachment layout.
            /// @warning Disregarded for now.
            ColorAttachmentRead,
            /// Read by rasterization pipeline in attachment load operation, in depth attachment layout.
            /// @warning Disregarded for now.
            DepthAttachmentWrite,
            /// Read by shader, in shader readonly layout.
            ShaderRead,
            /// Write by rasterization pipeline color output stage, in color attachment layout.
            ColorAttachmentWrite,
            /// Write by rasterization pipeline depth test stage, in depth attachment layout.
            DepthAttachmentWrite,
            /// Write by transfer command, in transfer destination layout.
            TransferWrite,
            /// Random write in shaders as storage image, in general layout.
            ShaderRandomWrite,
        };

        ICommandContext() = default;
        ICommandContext(const ICommandContext &) = delete;
        void operator = (const ICommandContext &) = delete;

        virtual ~ICommandContext() = default;

        virtual ICommandBuffer & GetCommandBuffer() const noexcept = 0;

        virtual void UseImage(
            vk::Image & img,
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
