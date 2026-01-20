#ifndef PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <optional>
#include <unordered_map>
#include <variant>

namespace vk {
    class Pipeline;
    class PipelineLayout;
    class DescriptorSetLayout;
    class DescriptorSet;
    class DescriptorPool;
    class DescriptorImageInfo;
    class ShaderModule;
} // namespace vk

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;
    class MaterialTemplateAsset;
    class MaterialTemplateProperties;
    class MaterialTemplateSinglePassProperties;

    enum class MeshVertexType;

    namespace PipelineInfo {
        class MaterialPassInfo;
        class MaterialPoolInfo;
    } // namespace PipelineInfo

    namespace ShdrRfl {
        class SPVariable;
        class SPLayout;
    }

    /**
     * @brief `MaterialTemplate` class holds a pipeline object for draw calls.
     * 
     * They are generally stored and owned by a `MaterialLibrary` class.
     * It contains all public immutable data for a given pipeline, mainly
     * the layout configuration and a handle to the binary object.
     */
    class MaterialTemplate : protected std::enable_shared_from_this<MaterialTemplate> {
    public:
        using PoolInfo = PipelineInfo::MaterialPoolInfo;

    protected:
        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

        MaterialTemplate(RenderSystem & system);

    public:
        /**
         * @brief Construct a new Material Template object.
         */
        MaterialTemplate(
            RenderSystem & system,
            const MaterialTemplateSinglePassProperties & properties,
            const std::vector <vk::ShaderModule> & shaders,
            vk::PipelineLayout layout,
            std::optional<
                std::pair<vk::DescriptorPool, vk::DescriptorSetLayout>
            > material_descriptor_info,
            const ShdrRfl::SPLayout * reflected,
            VertexAttribute attribute,
            const std::string & name = ""
        );

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
         * @return vk::PipelineLayout
         * The pipeline layout associated with the specified pass index.
         */
        vk::PipelineLayout GetPipelineLayout() const noexcept;

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
        std::vector<vk::DescriptorSet> AllocateDescriptorSets(uint32_t sz = 1);

        /**
         * @brief Get all reflected shader info.
         */
        const ShdrRfl::SPLayout & GetReflectedShaderInfo () const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
