#ifndef PIPELINE_MATERIAL_MATERIALINSTANCE
#define PIPELINE_MATERIAL_MATERIALINSTANCE

#include "Asset/InstantiatedFromAsset.h"
#include "MaterialTemplate.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Resource/RenderResourceHandle.h"

#include <any>
#include <fwd.hpp>

namespace Engine {
    class Texture;
    class DeviceBuffer;
    class MaterialAsset;
    class MaterialLibrary;
    class VertexAttribute;

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
        RenderSystemState::MaterialLibraryHandle m_library;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        MaterialInstance(RenderSystem &system, RenderSystemState::MaterialLibraryHandle library);
        virtual ~MaterialInstance();

        /// @brief Assign values to a variable.
        void AssignScalarVariable(const std::string &name, std::variant<uint32_t, float> value);
        /// @overload void MaterialInstance::AssignScalarVariable(const std::string &name, std::variant<uint32_t, float> value)
        void AssignVectorVariable(const std::string &name, std::variant<glm::vec4, glm::mat4> value);
        /// @brief Assign Texture reference to a variable.
        void AssignTexture(const std::string &name, std::shared_ptr<Texture> texture);
        /// @brief Assign buffer reference to a variable.
        void AssignBuffer(const std::string &name, std::shared_ptr<const DeviceBuffer> buffer);

        /**
         * @brief Upload current state of this instance to GPU:
         * Performs descriptor writes and UBO buffer writes.
         *
         * May perform lazy buffer or descriptor allocations.
         *
         * No action will be performed if the template has no per-material data.
         *
         * @return A vector containing all dynamic uniform buffer offsets.
         * Guaranteed to be sorted by binding numbers.
         * A zero-sized vector will be returned if the template has no
         * per-material data.
         */
        std::vector<uint32_t> UpdateGPUInfo(MaterialTemplate &tpl, uint32_t backbuffer);

        /// @overload std::vector<uint32_t> MaterialInstance::UpdateGPUInfo(MaterialTemplate &tpl, uint32_t backbuffer);
        std::vector<uint32_t> UpdateGPUInfo(
            const std::string &tag, const PipelineRuntimeInfo &pri, uint32_t backbuffer
        );

        /**
         * @brief Get the descriptor set for a specific pass
         *
         * This method returns the
         * descriptor set associated with the given pass index.
         * The descriptor set contains the bindings for
         * various resources such as textures and uniform buffers.
         *
         * @return A handle to the Descriptor Set object.
         * If the descriptor is not yet allocated, a null descriptor will be
         * returned.
         * If the material template has no per-material data, a null descriptor
         * will be returned.
         */
        vk::DescriptorSet GetDescriptor(const MaterialTemplate &tpl, uint32_t backbuffer) const noexcept;

        /// @overload vk::DescriptorSet MaterialInstance::GetDescriptor(const MaterialTemplate &tpl, uint32_t backbuffer) const noexcept
        vk::DescriptorSet GetDescriptor(
            const std::string &tag, const PipelineRuntimeInfo &pri, uint32_t backbuffer
        ) const noexcept;

        /**
         * @brief Instantiate a material asset to the material instance. Load properties to the uniforms.

         * *
         * @param asset The MaterialAsset to convert.
         */
        void Instantiate(MaterialAsset &asset) override;

        /**
         * @brief Get the material library assigned to this instance.
         */
        MaterialLibrary &GetLibrary() const;
    };
} // namespace Engine

#endif // PIPELINE_MATERIAL_MATERIALINSTANCE
