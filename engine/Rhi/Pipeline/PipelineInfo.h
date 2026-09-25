#ifndef ENGINE_RHI_PIPELINEINFO_INCLUDED
#define ENGINE_RHI_PIPELINEINFO_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    class DeviceBuffer;
    namespace PipelineInfo {
        struct ComputePassInfo {
            vk::UniquePipeline pipeline{};
            vk::UniquePipelineLayout pipeline_layout{};
            /// @note The descriptor set layout is owned by the immutable
            /// resource cache, never by the pass.
            vk::DescriptorSetLayout desc_layout{};
            vk::UniqueShaderModule shader;
        };
    } // namespace PipelineInfo
} // namespace Engine::Rhi

#endif // ENGINE_RHI_PIPELINEINFO_INCLUDED
