#ifndef PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED
#define PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED

#include "MaterialTemplate.h"
#include "Render/Memory/Buffer.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Asset/InstantiatedFromAsset.h"

#include <any>

namespace Engine {
    class Texture;
    class Buffer;
    class MaterialAsset;
    class MaterialLibrary;
    class VertexAttribute;

    namespace ShdrRfl {
        class ShaderParameters;
    }

    /**
     * @brief A light-weight instance of a given material library.
     * 
     * It contains all mutable data (e.g. texture references, uniform variables)
     * needed to draw a renderer.
     * 
     * Implementation-wise, it contains an unordered mapping from a specific
     * pipeline (i.e. `MaterialTemplate`) to its underlying mutable data such
     * as UBOs and descriptor sets, and an unordered mapping from names of
     * variables to their values. When draw calls are initiated by a command
     * buffer, after the pipeline to draw is determined, it updates these
     * mutable data accordingly, and possibly perform lazy allocation.
     * 
     * It holds a pointer to the material library to facilitate draw calls.
     */
    class MaterialInstance : public IInstantiatedFromAsset<MaterialAsset> {
    protected:
        RenderSystem &m_system;
        MaterialLibrary &m_library;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        MaterialInstance(
            RenderSystem & system,
            MaterialLibrary & library
        );
        virtual ~MaterialInstance();

        /**
         * @brief Acquire a reference to the underlying shader parameters.
         */
        const ShdrRfl::ShaderParameters & GetShaderParameters() const noexcept;

        void AssignScalarVariable(const std::string & name, std::variant<uint32_t, float> value);
        void AssignVectorVariable(const std::string & name, std::variant<glm::vec4, glm::mat4> value);
        void AssignTexture(const std::string & name, std::shared_ptr <const Texture> texture);
        void AssignBuffer(const std::string & name, std::shared_ptr <const Buffer> buffer);

        /**
         * @brief Upload current state of this instance to GPU:
         * Performs descriptor writes and UBO buffer writes.
         * 
         * May perform lazy buffer or descriptor allocations.
         */
        void UpdateGPUInfo(
            MaterialTemplate & tpl,
            uint32_t backbuffer
        );
        void UpdateGPUInfo(
            const std::string & tag,
            VertexAttribute type,
            uint32_t backbuffer
        );

        /**
         * @brief Get the descriptor set for a specific pass
         *
         * This method returns the
         * descriptor set associated with the given pass index.
         * The descriptor set contains the bindings for
         * various resources such as textures and uniform buffers.
         * 
         * If the descriptor is not yet allocated, a null handle
         * will be returned.
         *
         * @return A handle to the Descriptor Set object

         */
        vk::DescriptorSet GetDescriptor(
            const MaterialTemplate & tpl,
            uint32_t backbuffer
        ) const noexcept;
        vk::DescriptorSet GetDescriptor(
            const std::string & tag,
            VertexAttribute type,
            uint32_t backbuffer
        ) const noexcept;

        /**
         * @brief Instantiate a material asset to the material instance. Load properties to the uniforms.

         * * 
         * @param asset The MaterialAsset to convert.
         */
        void Instantiate(const MaterialAsset &asset) override;

        /**
         * @brief Get the material library assigned to this instance.
         */
        MaterialLibrary & GetLibrary() const;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED
