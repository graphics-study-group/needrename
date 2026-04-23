#ifndef PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
#define PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED

#include <optional>
#include <unordered_map>
#include <variant>

#include "Render/Renderer/VertexAttribute.h"

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
    class PipelineRuntimeInfo;

    namespace PipelineInfo {
        class MaterialPassInfo;
        class MaterialPoolInfo;
    } // namespace PipelineInfo

    namespace ShdrRfl {
        class SPVariable;
        class SPLayout;
    } // namespace ShdrRfl

    /**
     * @brief `MaterialTemplate` class holds a pipeline object for draw calls.
     *
     * They are generally stored and owned by a `MaterialLibrary` class.
     * It contains all public immutable data for a given pipeline, mainly
     * the layout configuration and a handle to the binary object.
     *
     * @section shader_interop Interop of Shader with Material System
     *
     * In the engine we use a frequency-based descriptor management system.
     * The descriptors are used in two sets:
     * 1. *Set index 0* stores global per-scene uniforms, such as environmental
     * light;
     * 2. *Set index 1* stores per-view or per-camera uniforms, such as view
     * and projection matrices;
     * 3. *Set index 2* stores per-material uniforms, such as diffuse textures.
     * 4. Model matrices are pushed to shader via *push constants*.
     *
     * The only descriptor set that can be changed freely is therefore set
     * index 2.
     * This descriptor set is dynamically reflected from the shader code via
     * `Engine::ShdrRfl::SPLayout` class.
     * Descriptor sets of index 0 and 1 are hardcoded and defined in the GLSL
     * file "builtin_assets/shaders/include/engine/interface.glsl".
     * These two sets cooperates closely with
     * `Engine::RenderSystemState::SceneDataManager` and
     * `Engine::RenderSystemState::CameraManager` respectively.
     *
     * @subsection material_descriptor Per Material Descriptor
     *
     * To faciliate custom material composition, descriptor set 2 is not
     * hardcoded and always behaves as if being dynamically reflected on the
     * run-time. They can be indexed and modified by name freely via the
     * corresponding `Engine::MaterialInstance` class.
     * For example, consider the following GLSL interface blocks:
     * @code{.glsl}
     * layout(set = 2, binding = 0) uniform UBO {
     *     vec4 base_color;
     * } ubo;
     * layout(set = 2, binding = 1) uniform sampler2D diffuse;
     * @endcode
     * You can use:
     * @code{.cpp}
     * AssignVectorVariable("UBO::base_vector", glm::vec4{...});
     * AssignTexture("diffuse", ...);
     * @endcode
     * To modify them dynamically.
     * The placement of variables in UBOs into the memory are handled by the
     * `Engine::StructuredBuffer` and `Engine::StructuredBufferPlacer` classes.
     *
     * However, the following restrictions apply:
     * 1. For all shaders that are linked into the same pipeline, their
     * interface definition must be compatible. Conflicting definitions that
     * occupy the same binding lead to undefined behavior.
     * 2. For variables in a block, only `int`, `float`, `vec4` and `mat4` are
     * currently supported. Arrays are not supported.
     * 3. For other variables (i.e. opaque types), only combined image samplers
     * are currently supported. Arrays are not supported either.
     */
    class MaterialTemplate : protected std::enable_shared_from_this<MaterialTemplate> {
    public:
        using PoolInfo = PipelineInfo::MaterialPoolInfo;

    protected:
        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

        MaterialTemplate(RenderSystem &system);

    public:
        /**
         * @brief Construct a new Material Template object.
         */
        MaterialTemplate(
            RenderSystem &system,
            MaterialTemplateSinglePassProperties &properties,
            const std::vector<vk::ShaderModule> &shaders,
            vk::PipelineLayout layout,
            vk::DescriptorPool pool,
            const ShdrRfl::SPLayout &reflected,
            const PipelineRuntimeInfo &attribute,
            const std::string &name = ""
        );

        virtual ~MaterialTemplate();

        /**
         * @brief Get the pipeline for a specific pass index.
         *
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
         * @brief Get the descriptor pool for this material.
         *
         * @return vk::DescriptorPool can be null if no material descriptor presents.
         */
        vk::DescriptorPool GetDescriptorPool() const noexcept;

        /**
         * @brief Get all reflected shader info.
         */
        const ShdrRfl::SPLayout &GetReflectedShaderInfo() const noexcept;

        /**
         * @brief Query whether this material template has data to be submitted
         * to GPU before draws.
         */
        bool HasMaterialData() const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALTEMPLATE_INCLUDED
