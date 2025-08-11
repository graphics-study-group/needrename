#ifndef PIPELINE_COMMANDBUFFER_ICOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_ICOMMANDBUFFER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class ICommandBuffer {
    protected:
        vk::CommandBuffer cb;

    public:
        ICommandBuffer(vk::CommandBuffer cb);

        virtual ~ICommandBuffer() = 0;

        vk::CommandBuffer GetCommandBuffer() const noexcept;

        virtual void Begin(const std::string &name = "") const;

        virtual void End() const noexcept;

        virtual void Reset() noexcept;
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_ICOMMANDBUFFER_INCLUDED
