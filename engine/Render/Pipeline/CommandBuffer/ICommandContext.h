#ifndef PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED

#include <vulkan/vulkan.hpp>
#include "AccessHelperTypes.h"

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

        /**
         * @brief Record memory barriers according to `UseX()` directives.
         */
        virtual void PrepareCommandBuffer () = 0;
    protected:
        /**
         * @brief Get a image barrier for request usages.
         * All subresources (e.g. mipmaps and array layers) of the image is marked for use.
         * The actual recording of barriers are defered until `PrepareCommandBuffer` is called.
         * 
         * @param currentAccess Intended access type of the image.
         * @param previousAccess The last access type of the image. This is supposed to be
         * automatically managed with a RenderGraph mechanism in future if possible.
         * @return description of the barrier to be inserted.
         */
        static vk::ImageMemoryBarrier2 GetImageBarrier(
            vk::Image img,
            ImageAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept;

        /* virtual void UseImage (
            vk::Image & img,
            vk::SubresourceRange range,
            ImageAccessType currentAccess,
            ImageAccessType previousAccess
        ) noexcept = 0; */

        /* virtual void UseBuffer (
            vk::Buffer buffer,
            BufferAccessType currentAccess,
            BufferAccessType previousAccess
        ) noexcept = 0; */
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMMANDCONTEXT_INCLUDED
