#ifndef RENDER_PIPELINE_PIPELINEINFO_INCLUDED
#define RENDER_PIPELINE_PIPELINEINFO_INCLUDED

#include <array>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class Buffer;
    namespace PipelineInfo {
        struct ComputePassInfo {
            vk::UniquePipeline pipeline{};
            vk::UniquePipelineLayout pipeline_layout{};
            vk::UniqueDescriptorSetLayout desc_layout{};
            vk::UniqueShaderModule shader;
        };

        struct PoolInfo {
            static constexpr uint32_t MAX_SET_SIZE = 64;
            vk::UniqueDescriptorPool pool{};
        };

        struct MaterialPoolInfo : PoolInfo {
            static constexpr std::array<vk::DescriptorPoolSize, 4> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 128}
            };
        };
    } // namespace PipelineInfo
} // namespace Engine

#endif // RENDER_PIPELINE_PIPELINEINFO_INCLUDED
