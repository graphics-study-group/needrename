#ifndef RENDER_PIPELINE_PIPELINEINFO_INCLUDED
#define RENDER_PIPELINE_PIPELINEINFO_INCLUDED

/** Structures holding information with regards to VkPipeline etc. */

#include "Asset/Shader/ShaderAsset.h"
#include <any>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class Buffer;
    namespace PipelineInfo {
        struct PassInfo {
            vk::UniquePipeline pipeline{};
            vk::UniquePipelineLayout pipeline_layout{};
            vk::UniqueDescriptorSetLayout desc_layout{};
            std::vector<vk::UniqueShaderModule> shaders{};
        };

        struct MaterialPassInfo : PassInfo {
            constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
                vk::DynamicState::eViewport, vk::DynamicState::eScissor
            };
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
