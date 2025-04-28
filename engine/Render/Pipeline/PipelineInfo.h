#ifndef PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED
#define PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED

/** Structures holding information with regards to VkPipeline etc. */

#include <vulkan/vulkan.hpp>
#include "Asset/Material/ShaderAsset.h"
#include "Render/AttachmentUtils.h"

namespace Engine {
    namespace PipelineInfo {
        struct ShaderVariable {
            using Type = ShaderVariableProperty::Type;

            Type type {};
            struct Location {
                uint32_t set {};
                uint32_t binding {};
                uint32_t offset {};
            } location {};
        };

        struct PassInfo {
            vk::UniquePipeline pipeline {};
            vk::UniquePipelineLayout pipeline_layout {};
            vk::UniqueDescriptorSetLayout desc_layout {};
            std::vector <vk::UniqueShaderModule> shaders {};

            struct Uniforms {
                std::unordered_map <std::string, uint32_t> name_mapping {};
                std::vector <ShaderVariable> variables {};
                uint64_t maximal_ubo_size {};
            } uniforms {};

            constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };
        };

        struct MaterialPassInfo : PassInfo {
            struct Attachments {
                std::vector <AttachmentUtils::AttachmentOp> color_attachment_ops {};
                AttachmentUtils::AttachmentOp ds_attachment_ops {};
            } attachments {};
        };

        struct PoolInfo {
            static constexpr uint32_t MAX_SET_SIZE = 64;
            vk::UniqueDescriptorPool pool {};
        };

        struct MaterialPoolInfo : PoolInfo {
            static constexpr std::array <vk::DescriptorPoolSize, 2> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 64},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 64}
            };
        };

        /**
         * @brief Place UBO variables at given buffer, ready for directly writing into UBO.
         * 
         * @remark For internal use only.
         */
        void PlaceUBOVariables(const std::unordered_map <uint32_t, std::any> & variables, const PassInfo & info, std::vector<std::byte> & memory);

        /**
         * @brief Get the descriptor image info for a specific variable set.
         * 
         * @param sampler If null, the vk::WriteDescriptorSet is assumed to be of type `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`,
         * and the layout is `VK_IMAGE_LAYOUT_GENERAL`. Otherwise, it is assumed to be of type 
         * `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and the layout is `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL`.
         * 
         * @remark For internal use only.
         */
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> 
        GetDescriptorImageInfo(const std::unordered_map <uint32_t, std::any> & variables, const PassInfo & info, vk::Sampler sampler);
    }
}

#endif // PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED
