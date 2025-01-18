#ifndef RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/AttachmentUtils.h"
#include "Asset/Material/MaterialTemplateAsset.h"

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;

    /// @brief A factory class for instantiation of materials.
    /// Contains all public immutable data for a given type of materials, such as pipeline
    /// and its configurations, descriptor set layout and attachment operations.
    class MaterialTemplate {

        std::weak_ptr <RenderSystem> m_system;
        std::shared_ptr <AssetRef> m_asset {};

        struct PassInfo {
            vk::UniquePipeline pipeline;
            vk::UniquePipelineLayout pipeline_layout;
            vk::UniqueDescriptorSetLayout desc_layout;
            std::vector <vk::UniqueShaderModule> shaders;

            struct {
                std::vector <AttachmentUtils::AttachmentOp> color_attachment_ops;
                AttachmentUtils::AttachmentOp ds_attachment_ops;
            } attachments;

            constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };
        };
        std::unordered_map <uint32_t, PassInfo> m_passes {};

        struct PoolInfo {
            static constexpr uint32_t MAX_SET_SIZE = 64;
            static constexpr std::array <vk::DescriptorPoolSize, 2> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 64},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 64}
            };
             vk::UniqueDescriptorPool pool;
        } m_poolInfo {};

        vk::UniqueSampler m_default_sampler {};

        std::string m_name {};

        void CreatePipeline(uint32_t pass_index, const MaterialTemplateSinglePassProperties & prop, vk::Device device);

        /// @brief Initalize descriptor pool, and create pipelines according to the asset.
        /// @param props pipeline description from the asset
        void CreatePipelines(const MaterialTemplateProperties & props);
    public:
        MaterialTemplate (std::weak_ptr <RenderSystem> system, std::shared_ptr <AssetRef> asset = nullptr);
        virtual ~MaterialTemplate () = default;

        MaterialInstance * CreateMaterial();

        vk::Pipeline GetPipeline(uint32_t pass_index = 0) const;
        vk::PipelineLayout GetPipelineLayout(uint32_t pass_index = 0) const;
        vk::DescriptorSetLayout GetDescriptorSetLayout(uint32_t pass_index = 0) const;
        vk::DescriptorSet AllocateDescriptorSet(uint32_t pass_index = 0);

        AttachmentUtils::AttachmentOp GetDSAttachmentOperation(uint32_t pass_index = 0);
        AttachmentUtils::AttachmentOp GetColorAttachmentOperation(uint32_t index, uint32_t pass_index = 0);
    };
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
