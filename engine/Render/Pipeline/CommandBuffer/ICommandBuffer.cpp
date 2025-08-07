#include "ICommandBuffer.h"

#include "Render/DebugUtils.h"

namespace Engine {
    ICommandBuffer::ICommandBuffer(vk::CommandBuffer _cb) : cb(_cb) {
    }

    ICommandBuffer::~ICommandBuffer() {
    }

    vk::CommandBuffer ICommandBuffer::GetCommandBuffer() const noexcept {
        return cb;
    };

    void ICommandBuffer::Begin(const std::string &name) const {
        cb.begin(vk::CommandBufferBeginInfo{});
        DEBUG_CMD_START_LABEL(cb, name.c_str());
    }

    void ICommandBuffer::End() const noexcept {
        DEBUG_CMD_END_LABEL(cb);
        cb.end();
    }

    void ICommandBuffer::Reset() noexcept {
        cb.reset();
    }
} // namespace Engine
