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
        MaterialInstance(RenderSystem &system);
        virtual ~MaterialInstance();

        /**
         * @brief Acquire a reference to the underlying shader parameters.
         */
        const ShdrRfl::ShaderParameters & GetShaderParameters() const noexcept;

        void AssignScalarVariable(const std::string & name, std::variant<uint32_t, int32_t, float> value);
        void AssignVectorVariable(const std::string & name, std::variant<glm::vec4, glm::mat4> value);
        void AssignTexture(const std::string & name, const Texture & texture);
        void AssignBuffer(const std::string & name, const Buffer & buffer);

        /**
         * @brief Upload current state of this instance to GPU:
         * Performs descriptor writes and UBO buffer writes.
         * 
         * May perform lazy buffer or descriptor allocations.
         */
        void UpdateGPUInfo(uint32_t backbuffer, MaterialTemplate & tpl);

        /**
         * @brief Get a list of dynamic uniform buffer offsets
         * used in `vkCmdBindDescriptorSets`. These dynamic offsets
         * are returned in the order of binding numbers.
         * 
         * If uniform buffer objects of the given template is not yet allocated,
         * an exception will be thrown.
         */
        std::vector<uint32_t> GetDynamicUBOOffset(uint32_t backbuffer, const MaterialTemplate & tpl);

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
        vk::DescriptorSet GetDescriptor(uint32_t backbuffer, const MaterialTemplate & tpl) const noexcept;

        /**
         * @brief Instantiate a material asset to the material instance. Load properties to the uniforms.

         * * 
         * @param asset The MaterialAsset to convert.
         */
        void Instantiate(const MaterialAsset &asset) override;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALINSTANCE_INCLUDED
