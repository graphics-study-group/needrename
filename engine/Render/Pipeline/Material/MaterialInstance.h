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
         * Use this member to modify underlying shader parameters.
         */
        ShdrRfl::ShaderParameters & GetShaderParameters() noexcept;

        /**
         * @brief Write out UBO changes if it is dirty.
         * 
         * You in general does not need
         * to call this function. It is automatically called
         * when a direct draw call is issued on a command
         * buffer.
         */
        void WriteUBO();

        /**
         * @brief Write out pending descriptor changes to the descriptor set.
         * Calls
         * vk::WriteDescriptorSet to upload.
         * 
         * You in general does not need to call this function.
         * It is automatically called
         * when a direct draw call is issued on a command buffer.
         */
        void WriteDescriptors();

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
