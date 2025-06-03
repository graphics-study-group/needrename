#ifndef PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED
#define PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED

/** Structures holding information with regards to VkPipeline etc. */

#include <vulkan/vulkan.hpp>
#include "Asset/Material/ShaderAsset.h"
#include "Render/AttachmentUtils.h"

namespace Engine {
    class Buffer;
    namespace PipelineInfo {
        struct ShaderVariable {
            using Type = ShaderVariableProperty::Type;
            using InBlockVarType = ShaderInBlockVariableProperty::InBlockVarType;

            Type type {};
            InBlockVarType ubo_type {};
            struct Location {
                uint32_t set{};
                uint32_t binding{};
                uint32_t offset{};
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
        };

        struct MaterialPassInfo : PassInfo {
            constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };

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

        struct InstancedPassInfo {
            // FIXME: We are only allocating one buffer for multiple frames-in-flight. This might lead to synchronization problems.
            std::unique_ptr<Buffer> ubo {};
            vk::DescriptorSet desc_set {};
            bool is_ubo_dirty {false};
            bool is_descriptor_set_dirty {false};
        };

        /**
         * @brief Place UBO variables at given buffer, ready for directly writing into UBO.
         * 
         * @remark For internal use only.
         */
        void PlaceUBOVariables(const std::unordered_map <uint32_t, std::any> & variables, const PassInfo & info, std::vector<std::byte> & memory) noexcept;

        /**
         * @brief Get the descriptor image info for a specific variable set.
         * 
         * The layout for the image is selected based on its type.
         * If the type is `Texture`, then the layout is `ReadOnlyOptimal`.
         * If the type is `StorageImage`, then the layout is `General`.
         * 
         * @remark For internal use only.
         */
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> 
        GetDescriptorImageInfo(const std::unordered_map <uint32_t, std::any> & variables, const PassInfo & info, vk::Sampler sampler) noexcept;

        /**
         * @brief Get the descriptor for storage buffers info for a specific variable set.
         * @remark For internal use only.
         * @warning unimplemented
         */
        std::vector<std::pair<uint32_t, vk::DescriptorBufferInfo>>
        GetDescriptorBufferInfo(const std::unordered_map <uint32_t, std::any> & variables, const PassInfo & info) noexcept;
    }
}

#endif // PIPELINE_MATERIAL_PIPELINEINFO_INCLUDED
