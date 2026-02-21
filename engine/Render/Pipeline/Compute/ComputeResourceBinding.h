#ifndef PIPELINE_COMPUTE_COMPUTERESOURCEBINDING
#define PIPELINE_COMPUTE_COMPUTERESOURCEBINDING

#include <memory>
#include <string>

namespace vk {
    class DescriptorSet;
}

namespace Engine {
    class StructuredBuffer;
    class ShaderResourceBinding;
    class ComputeBuffer;

    class Texture;

    /**
     * @brief A class handling bindings to compute shader resources.
     * 
     * It manages:
     * - Bindings of resources (e.g. textures and storage buffers);
     * - Values of variables (e.g. vectors in uniform buffer);
     * - A small uniform buffer for placing variables, which is indexed
     * for multiple frames-in-flight.
     */
    class ComputeResourceBinding {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        ComputeResourceBinding(RenderSystem & system, ComputeStage & compute);
        ~ComputeResourceBinding() noexcept;

        /**
         * @brief Get the structured buffer for variable managements.
         */
        StructuredBuffer & GetStructuredBuffer() noexcept;
        const StructuredBuffer & GetStructuredBuffer() const noexcept;

        /**
         * @brief Get the shader resource binding for resource managements.
         */
        ShaderResourceBinding & GetShaderResourceBinding() noexcept;

        /**
         * @brief Bind a owning shared texture to this binding.
         * 
         * For a non-owning reference, call `GetShaderResourceBinding()` and
         * do a manual binding.
         */
        void BindTexture(
            const std::string & name,
            std::shared_ptr <const Texture> texture
        ) noexcept;

        /**
         * @brief Bind a owning compute buffer to this binding.
         */
        void BindComputeBuffer(
            const std::string & name,
            std::shared_ptr <const ComputeBuffer> buffer,
            size_t offset,
            size_t size = std::numeric_limits<size_t>::max()
        );

        /**
         * @brief Upload GPU info by recording descriptor writes and perform
         * Uniform buffer writes.
         * 
         * @return offsets for dynamically offseted uniform buffers.
         */
        std::vector<uint32_t> UpdateGPUInfo(uint32_t backbuffer) const noexcept;

        /**
         * @brief Get the descriptor set of this compute resource binding.
         * 
         * Must be called after `UpdateGPUInfo`, or an invaild/outdated
         * descriptor set might be returned.
         */
        vk::DescriptorSet GetDescriptorSet(uint32_t backbuffer) const noexcept;
    };
}

#endif // PIPELINE_COMPUTE_COMPUTERESOURCEBINDING
