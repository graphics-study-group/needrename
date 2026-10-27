#ifndef PIPELINE_MATERIAL_MATERIALINSTANCE
#define PIPELINE_MATERIAL_MATERIALINSTANCE

#include "Asset/InstantiatedFromAsset.h"
#include "MaterialTemplate.h"
#include "Render/Resource/RenderResourceHandle.h"
#include "Render/render_export.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Resource/DescriptorArena.h"

#include <any>
#include <fwd.hpp>

namespace Engine {
    namespace Rhi {
        class DeviceBuffer;
        class Texture;
        struct TextureSubresourceRange;
    } // namespace Rhi
    class MaterialAsset;
    class MaterialLibrary;
    struct VertexAttribute;

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
    class RENDER_API MaterialInstance : public IInstantiatedFromAsset<MaterialAsset> {
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
        /// @overload void MaterialInstance::AssignTexture()
        /// Defaults to the full subresource range with no swizzling.
        void AssignTexture(const std::string &name, std::shared_ptr<Rhi::Texture> texture);
        /// @brief Assign Rhi::Texture reference to a variable.
        void AssignTexture(
            const std::string &name, std::shared_ptr<Rhi::Texture> texture, Rhi::TextureSubresourceRange range
        );
        /// @brief Assign buffer reference to a variable.
        void AssignBuffer(const std::string &name, std::shared_ptr<const Rhi::DeviceBuffer> buffer);

        /**
         * @brief Upload current state of this instance to GPU:
         * Performs descriptor writes and UBO buffer writes.
         *
         * May perform lazy buffer allocations, and acquires the descriptor set
         * from the device descriptor arena on every call so that the arena's
         * recorded epoch stays an upper bound on the epochs that reference it.
         *
         * No action will be performed if the template has no per-material data.
         *
         * @return The material descriptor set together with all dynamic uniform
         * buffer offsets, sorted by binding numbers. A null set is returned if
         * the template has no per-material data.
         */
        Rhi::DescriptorSetBinding UpdateGPUInfo(MaterialTemplate &tpl, uint32_t backbuffer);

        /// @overload Rhi::DescriptorSetBinding MaterialInstance::UpdateGPUInfo(MaterialTemplate &tpl, uint32_t backbuffer);
        Rhi::DescriptorSetBinding UpdateGPUInfo(
            const std::string &tag, const PipelineRuntimeInfo &pri, uint32_t backbuffer
        );

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
