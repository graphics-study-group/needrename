#ifndef MATERIAL_MATERIALTEMPLATE
#define MATERIAL_MATERIALTEMPLATE

#include <optional>
#include <unordered_map>
#include <variant>

#include "Asset/InstantiatedFromAsset.h"

namespace vk {
    class Pipeline;
    class PipelineLayout;
    class DescriptorSetLayout;
    class DescriptorSet;
    class DescriptorImageInfo;
} // namespace vk

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;
    class MaterialTemplateAsset;
    class MaterialTemplateProperties;
    class MaterialTemplateSinglePassProperties;

    namespace PipelineInfo {
        class MaterialPassInfo;
        class MaterialPoolInfo;
    } // namespace PipelineInfo

    namespace ShdrRfl {
        class SPVariable;
        class SPLayout;
    }

    /// @brief A factory class for instantiation of materials.
    /// Contains all public immutable data for a given type of materials, such as pipeline
    /// and its configurations, descriptor set layout and attachment operations.
    class MaterialTemplate : protected std::enable_shared_from_this<MaterialTemplate>,
                             public IInstantiatedFromAsset<MaterialTemplateAsset> {
    public:
        using PassInfo = PipelineInfo::MaterialPassInfo;
        using PoolInfo = PipelineInfo::MaterialPoolInfo;

    protected:
        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

        void CreatePipeline(const MaterialTemplateSinglePassProperties &prop, vk::Device device);
    public:
        /**
         * @brief Construct a new Material Template object.
         * 
         * @param system The
         * RenderSystem associated with this material template.
         * @param asset A shared pointer to the AssetRef
         * representing the material template's properties.
         */
        MaterialTemplate(RenderSystem &system);

        void Instantiate(const MaterialTemplateAsset &asset) override;

        virtual ~MaterialTemplate();

        /**
         * @brief Get the pipeline for a specific pass index.
         * 
         * @param pass_index The
         * index of the pass to retrieve the pipeline from.
         * @return vk::Pipeline The pipeline associated with
         * the specified pass index.
         */
        vk::Pipeline GetPipeline() const noexcept;

        /**
         * @brief Get the pipeline layout for a specific pass index.
         * 
         * @param
         * pass_index The index of the pass to retrieve the pipeline layout from.
         * @return vk::PipelineLayout
         * The pipeline layout associated with the specified pass index.
         */
        vk::PipelineLayout GetPipelineLayout() const noexcept;

        /**
         * @brief Get the pass information for a specific pass index.
         * 
         * @return const PassInfo & A
         * constant reference to the PassInfo struct associated with the specified pass index.
         */
        const PassInfo &GetPassInfo() const;

        /**
         * @brief Get the descriptor set layout for a specific pass index.
         *
         * @return
         * vk::DescriptorSetLayout The descriptor set layout associated with the specified pass index.
         */
        vk::DescriptorSetLayout GetDescriptorSetLayout() const;

        /// @brief Allocate a descriptor set with the layout of a given pass index.
        /// As per Vulkan recommendation, allocated descriptors are generally not required to be cleaned up to speed up
        /// allocation. When its descriptor pool is de-allocated, all descriptors attached are automatically destroyed.
        /// @return Allocated descriptor set
        vk::DescriptorSet AllocateDescriptorSet();

        /**
         * @brief Query a reflected variable data by its name.
         */
        const ShdrRfl::SPVariable * GetVariable(const std::string & name) const noexcept;

        /**
         * @brief Get all reflected shader info.
         */
        const ShdrRfl::SPLayout & GetReflectedShaderInfo () const noexcept;
        
        /**
         * @brief Query the expected size of the uniform buffer object.
         * If multiple UBOs for descriptor set 3 (material slot)
         * are found in the shader, the maximum size will be returned.
         * Note that multiple UBOs are currently poorly supported.
         */
        size_t GetExpectedUniformBufferSize() const noexcept;
    };
} // namespace Engine

#endif // MATERIAL_MATERIALTEMPLATE
