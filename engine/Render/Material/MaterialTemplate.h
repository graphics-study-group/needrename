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
    public:
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

            struct Attachments {
                std::vector <AttachmentUtils::AttachmentOp> color_attachment_ops {};
                AttachmentUtils::AttachmentOp ds_attachment_ops {};
            } attachments {};

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

        struct PoolInfo {
            static constexpr uint32_t MAX_SET_SIZE = 64;
            static constexpr std::array <vk::DescriptorPoolSize, 2> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 64},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 64}
            };
             vk::UniqueDescriptorPool pool {};
        };

    private:
        std::weak_ptr <RenderSystem> m_system;
        std::shared_ptr <AssetRef> m_asset;

        
        std::unordered_map <uint32_t, PassInfo> m_passes {};
        PoolInfo m_poolInfo {};
        vk::UniqueSampler m_default_sampler {};
        std::string m_name {};

        void CreatePipeline(uint32_t pass_index, const MaterialTemplateSinglePassProperties & prop, vk::Device device);

        /// @brief Initalize descriptor pool, and create pipelines according to the asset.
        /// @param props pipeline description from the asset
        void CreatePipelines(const MaterialTemplateProperties & props);
    public:
        MaterialTemplate (std::weak_ptr <RenderSystem> system, std::shared_ptr <AssetRef> asset = nullptr);
        virtual ~MaterialTemplate () = default;

        vk::Pipeline GetPipeline(uint32_t pass_index = 0) const;
        vk::PipelineLayout GetPipelineLayout(uint32_t pass_index = 0) const;
        auto GetAllPassInfo() const -> const decltype(m_passes) &;
        auto GetPassInfo(uint32_t pass_index) const -> const PassInfo &;
        vk::DescriptorSetLayout GetDescriptorSetLayout(uint32_t pass_index = 0) const;

        /// @brief Allocate a descriptor set with the layout of a given pass index.
        /// As per Vulkan recommendation, allocated descriptors are generally not required to be cleaned up to speed up allocation.
        /// When its descriptor pool is de-allocated, all descriptors attached are automatically destroyed.
        /// @param pass_index 
        /// @return Allocated descriptor set
        vk::DescriptorSet AllocateDescriptorSet(uint32_t pass_index = 0);
    
        const ShaderVariable & GetVariable(const std::string & name, uint32_t pass_index = 0) const;
        const ShaderVariable & GetVariable(uint32_t index, uint32_t pass_index = 0) const;
        uint32_t GetVariableIndex(const std::string & name, uint32_t pass_index = 0) const;
    
        AttachmentUtils::AttachmentOp GetDSAttachmentOperation(uint32_t pass_index = 0) const;
        AttachmentUtils::AttachmentOp GetColorAttachmentOperation(uint32_t index, uint32_t pass_index = 0) const;

        /// @brief Get the maximal UBO size of a given pass, estimated from offsets and types of uniforms.
        /// Can be used to allocate uniform or staging buffers.
        /// @param pass_index 
        /// @return Estimated maximal UBO size
        uint64_t GetMaximalUBOSize(uint32_t pass_index = 0) const;

        /// @brief Place UBO variables at given memory address.
        /// @param instance material instance saving values of uniforms
        /// @param memory pointer to a sufficently large buffer. Caller should allocate and de-allocate this buffer.
        /// @param pass_index 
        /// @note While it is possible to directly write to UBO using this method, it is not recommended to do so as
        /// the uniform buffer memory might be sequentially writable but not randomly writable.
        /// Confer `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for more details.
        void PlaceUBOVariables(const MaterialInstance & instance, void * memory, uint32_t pass_index = 0) const;
    };
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
