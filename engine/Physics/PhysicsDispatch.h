#ifndef ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED
#define ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    /**
     * @brief Record the dependency barrier between two physics compute dispatches.
     *
     * Every physics pass writes storage buffers that a later pass reads, so this
     * is the boundary each step records. Compute is the only stage involved: no
     * physics pass writes an attachment or a transfer.
     *
     * @param cb Command buffer in recording state.
     */
    inline void DispatchBarrier(vk::CommandBuffer cb) {
        const vk::MemoryBarrier2 barrier{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {barrier}, {}, {}});
    }
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED
