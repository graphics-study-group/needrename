#ifndef PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <vulkan/vulkan.hpp>
#include <optional>
#include <variant>
#include "Render/AttachmentUtils.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/InstantiatedFromAsset.h"
#include "Render/Pipeline/PipelineInfo.h"

namespace Engine {
    class MaterialInstance;
    class Pipeline;
    class PipelineLayout;

    /// @brief A factory class for instantiation of materials.
    /// Contains all public immutable data for a given type of materials, such as pipeline
    /// and its configurations, descriptor set layout and attachment operations.
    ///
    /// There are two ways to use this class:
    /// To use it dynamically, construct an `MaterialTemplateAsset` and pass it in when constructing.
    /// To use it statically, inherent a subclass, and create corresponding asset and instance classes.
    ///
    /// When using indices in its methods, all indices are assumed to be valid.
    /// In-valid indices will cause assertion failure.
    class MaterialTemplate 
        : protected std::enable_shared_from_this<MaterialTemplate>, public IInstantiatedFromAsset<MaterialTemplateAsset> 
    {
    public:
        using PassInfo = PipelineInfo::MaterialPassInfo;
        using PoolInfo = PipelineInfo::MaterialPoolInfo;
        using DescVar = ShaderUtils::DesciptorVariableData;
        using InblockVar = ShaderUtils::InBlockVariableData;

    protected:
        RenderSystem & m_system;

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
        MaterialTemplate (RenderSystem & system);

        void Instantiate(const MaterialTemplateAsset & asset) override;

        virtual ~MaterialTemplate () = default;

        /**
         * @brief Create a new instance of MaterialInstance based on this MaterialTemplate.
         * 
         * @return A shared pointer to the newly created MaterialInstance object.
         * @note It is expected that derived material templates override this method with own derivation of MaterialInstance.
         */
        virtual std::shared_ptr <MaterialInstance> CreateInstance();

        /**
         * @brief Get the pipeline for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the pipeline from.
         * @return vk::Pipeline The pipeline associated with the specified pass index.
         */
        vk::Pipeline GetPipeline(uint32_t pass_index) const noexcept;

        /**
         * @brief Get the pipeline layout for a specific pass index.
         * 
         * @param pass_index The index of the pass to retrieve the pipeline layout from.
         * @return vk::PipelineLayout The pipeline layout associated with the specified pass index.
         */
        vk::PipelineLayout GetPipelineLayout(uint32_t pass_index) const noexcept;

        /**
         * @brief Get all pass information.
         * 
         * @return const decltype(m_passes) & A reference to a constant unordered_map containing all pass information.
         */
        auto GetAllPassInfo() const noexcept -> const decltype(m_passes) &;

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
        vk::DescriptorSetLayout GetDescriptorSetLayout(uint32_t pass_index) const;

        /// @brief Allocate a descriptor set with the layout of a given pass index.
        /// As per Vulkan recommendation, allocated descriptors are generally not required to be cleaned up to speed up allocation.
        /// When its descriptor pool is de-allocated, all descriptors attached are automatically destroyed.
        /// @param pass_index
        /// @return Allocated descriptor set
        vk::DescriptorSet AllocateDescriptorSet(uint32_t pass_index);
    
        /**
         * @brief Get a shader variable by name for a specific pass index.
         * 
         * @param name The name of the shader variable to retrieve.
         * @param pass_index The index of the pass to retrieve the shader variable from.
         * @return const ShaderVariable & A constant reference to the ShaderVariable struct associated with the specified name and pass index.
         */
        std::variant<std::monostate, std::reference_wrapper<const DescVar>, std::reference_wrapper<const InblockVar>>
        GetVariable(const std::string & name, uint32_t pass_index) const;

        /**
         * @brief Get a shader descriptor variable (textures, UBOs, SSBOs, etc.) by index for a specific pass index.
         * 
         * @param index The index of the shader variable to retrieve.
         * @param pass_index The index of the pass to retrieve the shader variable from.
         * @return const DescVar & A constant reference to the DescVar struct associated with the specified index and pass index.
         */
        const DescVar & GetDescVariable(uint32_t index, uint32_t pass_index) const;

        /**
         * @brief Get a shader in-block variable (floats, matrices, etc.) by index for a specific pass index.
         * 
         * @param index The index of the shader variable to retrieve.
         * @param pass_index The index of the pass to retrieve the shader variable from.
         * @return const InblockVar & A constant reference to the InblockVar struct associated with the specified index and pass index.
         */
        const InblockVar & GetInBlockVariable(uint32_t index, uint32_t pass_index) const;

        /**
         * @brief Get the index of a shader variable by name for a specific pass index.
         * It first checks in-block names, and then descriptor names, so in-block variables 
         * can mask descriptor variables with the same name. In that case a warning would be
         * issued when the material is being created.
         * 
         * @param name The name of the shader variable to retrieve the index for.
         * @param pass_index The index of the pass to retrieve the index from.
         * @return (uint32_t, bool) The index associated with the specified name and pass index,
         * and whether the variable is a in-block variable (floats, etc) or not.
         */
        std::optional<std::pair<uint32_t, bool>> GetVariableIndex(const std::string & name, uint32_t pass_index) const noexcept;

        /// @brief Get the maximal UBO size of a given pass, estimated from offsets and types of uniforms.
        /// Can be used to allocate uniform or staging buffers.
        /// @param pass_index
        /// @return Estimated maximal UBO size
        uint64_t GetMaximalUBOSize(uint32_t pass_index) const;

        /**
         * @brief Place UBO variables at given buffer, ready for directly writing into UBO.
         * 
         * @param instance material instance saving values of uniforms
         * @param memory Buffer. It should ideally be large enough to avoid memory allocation.
         * @param pass_index
         */
        void PlaceUBOVariables(const MaterialInstance & instance, std::vector<std::byte> & memory, uint32_t pass_index) const;

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

#endif // PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
