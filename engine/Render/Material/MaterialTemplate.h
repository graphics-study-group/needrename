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
        /**
         * @brief Construct a new Material Template object.
         * 
         * @param system The RenderSystem associated with this material template.
         * @param asset A shared pointer to the AssetRef representing the material template's properties.
         */
        MaterialTemplate (std::weak_ptr <RenderSystem> system, std::shared_ptr <AssetRef> asset = nullptr);

        virtual ~MaterialTemplate () = default;

        /**
         * @brief Get the pipeline for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the pipeline from.
         * @return vk::Pipeline The pipeline associated with the specified pass index.
         */
        vk::Pipeline GetPipeline(uint32_t pass_index = 0) const;

        /**
         * @brief Get the pipeline layout for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the pipeline layout from.
         * @return vk::PipelineLayout The pipeline layout associated with the specified pass index.
         */
        vk::PipelineLayout GetPipelineLayout(uint32_t pass_index = 0) const;

        /**
         * @brief Get all pass information.
         * 
         * @return const decltype(m_passes) & A reference to a constant unordered_map containing all pass information.
         */
        auto GetAllPassInfo() const -> const decltype(m_passes) &;

        /**
         * @brief Get the pass information for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the information from.
         * @return const PassInfo & A constant reference to the PassInfo struct associated with the specified pass index.
         */
        auto GetPassInfo(uint32_t pass_index) const -> const PassInfo &;

        /**
         * @brief Get the descriptor set layout for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the descriptor set layout from.
         * @return vk::DescriptorSetLayout The descriptor set layout associated with the specified pass index.
         */
        vk::DescriptorSetLayout GetDescriptorSetLayout(uint32_t pass_index = 0) const;

        /// @brief Allocate a descriptor set with the layout of a given pass index.
        /// As per Vulkan recommendation, allocated descriptors are generally not required to be cleaned up to speed up allocation.
        /// When its descriptor pool is de-allocated, all descriptors attached are automatically destroyed.
        /// @param pass_index
        /// @return Allocated descriptor set
        vk::DescriptorSet AllocateDescriptorSet(uint32_t pass_index = 0);
    
        /**
         * @brief Get a shader variable by name for a specific pass index.
         * 
         * @param name The name of the shader variable to retrieve.
         * @param pass_index The index of the pass to retrieve the shader variable from.
         * @return const ShaderVariable & A constant reference to the ShaderVariable struct associated with the specified name and pass index.
         */
        const ShaderVariable & GetVariable(const std::string & name, uint32_t pass_index = 0) const;

        /**
         * @brief Get a shader variable by index for a specific pass index.
         * 
         * @param index The index of the shader variable to retrieve.
         * @param pass_index The index of the pass to retrieve the shader variable from.
         * @return const ShaderVariable & A constant reference to the ShaderVariable struct associated with the specified index and pass index.
         */
        const ShaderVariable & GetVariable(uint32_t index, uint32_t pass_index = 0) const;

        /**
         * @brief Get the index of a shader variable by name for a specific pass index.
         * 
         * @param name The name of the shader variable to retrieve the index for.
         * @param pass_index The index of the pass to retrieve the index from.
         * @return uint32_t The index associated with the specified name and pass index.
         */
        uint32_t GetVariableIndex(const std::string & name, uint32_t pass_index = 0) const;
    
        /**
         * @brief Get the depth stencil attachment operation for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the depth stencil attachment operation from.
         * @return AttachmentUtils::AttachmentOp The depth stencil attachment operation associated with the specified pass index.
         */
        AttachmentUtils::AttachmentOp GetDSAttachmentOperation(uint32_t pass_index = 0) const;

        /**
         * @brief Get the color attachment operation for a specific pass index and index.
         * 
         * @param index The index of the color attachment to retrieve the operation from.
         * @param pass_index The index of the pass to retrieve the operation from.
         * @return AttachmentUtils::AttachmentOp The color attachment operation associated with the specified index and pass index.
         */
        AttachmentUtils::AttachmentOp GetColorAttachmentOperation(uint32_t index, uint32_t pass_index = 0) const;

        /// @brief Get the maximal UBO size of a given pass, estimated from offsets and types of uniforms.
        /// Can be used to allocate uniform or staging buffers.
        /// @param pass_index
        /// @return Estimated maximal UBO size
        uint64_t GetMaximalUBOSize(uint32_t pass_index = 0) const;

        /**
         * @brief Place UBO variables at given buffer, ready for directly writing into UBO.
         * 
         * @param instance material instance saving values of uniforms
         * @param memory Buffer. It should ideally be large enough to avoid memory allocation.
         * @param pass_index
         */
        void PlaceUBOVariables(const MaterialInstance & instance, std::vector<std::byte> & memory, uint32_t pass_index = 0) const;

        /**
         * @brief Get the descriptor image info for a specific material instance and pass index.
         *
         * This method retrieves a vector of pairs containing binding indices and corresponding descriptor image information
         * for a given material instance and pass index. The returned vector can be used to update descriptor sets with images.
         *
         * @param instance The MaterialInstance object from which to retrieve the descriptor image info.
         * @param pass_index The index of the pass for which to retrieve the descriptor image info.
         * @return A vector of pairs, where each pair contains a binding index and a vk::DescriptorImageInfo struct.
         */
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>>
        GetDescriptorImageInfo(const MaterialInstance & instance, uint32_t pass_index) const;
    };
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATE_INCLUDED
