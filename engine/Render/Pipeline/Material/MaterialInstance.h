#ifndef PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED
#define PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED

#include "MaterialTemplate.h"
#include "Render/Memory/Buffer.h"

#include "Asset/InstantiatedFromAsset.h"

#include <any>

namespace Engine {
    class Texture;
    class Buffer;
    class MaterialAsset;

    namespace ShdrRfl {
        class ShaderParameters;
    }

    /// @brief A light-weight instance of a given material,
    /// where all mutable data such as texture and uniforms are stored.
    class MaterialInstance : public IInstantiatedFromAsset<MaterialAsset> {
    protected:
        RenderSystem &m_system;
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        MaterialInstance(RenderSystem &system, std::shared_ptr<MaterialTemplate> tpl);
        virtual ~MaterialInstance();

        /**
         * @brief Get the template associated with this material instance.
         * 
         * This
         * method returns a constant reference to the `MaterialTemplate` object
         * that was used to create this
         * material instance. The template provides information
         * about the material's structure and
         * configuration, including its passes,
         * bindings, and other properties.
         * 
         *
         * @return A constant reference to the `MaterialTemplate` associated with this material instance.
         */
        const MaterialTemplate &GetTemplate() const;

        /**
         * @brief Acquire a reference to the underlying shader parameters.
         */
        const ShdrRfl::ShaderParameters & GetShaderParameters() const noexcept;

        void AssignSimpleVariables(const std::string & name, std::variant<uint32_t, int32_t, float> value);
        void AssignSimpleVariables(const std::string & name, std::variant<glm::vec4, glm::mat4> value);
        void AssignTexture(const std::string & name, const Texture & texture);
        void AssignBuffer(const std::string & name, const Buffer & buffer);

        /**
         * @brief Upload current state of this instance to GPU:
         * Performs descriptor writes and UBO buffer writes.
         */
        void UpdateGPUInfo(uint32_t backbuffer = std::numeric_limits<uint32_t>::max());

        /**
         * @brief Get a list of dynamic uniform buffer offsets
         * used in `vkCmdBindDescriptorSets`. These dynamic offsets
         * are returned in the order of binding numbers.
         */
        std::vector<uint32_t> GetDynamicUBOOffset(uint32_t backbuffer = std::numeric_limits<uint32_t>::max());

        /**
         * @brief Get the descriptor set for a specific pass
         *
         * This method returns the
         * descriptor set associated with the given pass index.
         * The descriptor set contains the bindings for
         * various resources such as textures and uniform buffers.
         *
         * @return A handle to the Descriptor Set object

         */
        vk::DescriptorSet GetDescriptor() const noexcept;

        /**
         * @brief Instantiate a material asset to the material instance. Load properties to the uniforms.

         * * 
         * @param asset The MaterialAsset to convert.
         */
        void Instantiate(const MaterialAsset &asset) override;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED
