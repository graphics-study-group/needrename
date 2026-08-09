#ifndef ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED
#define ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "rhi_export.h"

namespace vk {
    class DescriptorSet;
}

namespace Engine::Rhi {
    class ComputeStage;
    class DeviceContext;
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
     * - A small uniform buffer for placing variables, which is split into
     * `slot_count` rotation slots.
     */
    class RHI_API ComputeResourceBinding {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        ComputeResourceBinding(DeviceContext &device_context, ComputeStage &compute, uint32_t slot_count = 1);
        ~ComputeResourceBinding() noexcept;

        /**
         * @brief Get the structured buffer for variable managements.
         */
        StructuredBuffer &GetStructuredBuffer() noexcept;
        const StructuredBuffer &GetStructuredBuffer() const noexcept;

        /**
         * @brief Get the shader resource binding for resource managements.
         */
        ShaderResourceBinding &GetShaderResourceBinding() noexcept;

        /**
         * @brief Bind a owning shared texture to this binding.
         *
         * For a non-owning reference, call `GetShaderResourceBinding()` and
         * do a manual binding.
         */
        void BindTexture(const std::string &name, std::shared_ptr<Texture> texture) noexcept;

        /**
         * @brief Bind a owning compute buffer to this binding.
         */
        void BindComputeBuffer(
            const std::string &name,
            std::shared_ptr<const ComputeBuffer> buffer,
            size_t offset,
            size_t size = std::numeric_limits<size_t>::max()
        );

        /**
         * @brief Upload GPU info by recording descriptor writes and perform
         * Uniform buffer writes.
         *
         * @param slot Rotation slot index, advanced by the caller in
         * lockstep with its own submission cadence. Must be less than
         * `slot_count`.
         *
         * @return offsets for dynamically offseted uniform buffers.
         */
        std::vector<uint32_t> UpdateGPUInfo(uint32_t slot) const noexcept;

        /**
         * @brief Get the descriptor set of this compute resource binding.
         *
         * Must be called after `UpdateGPUInfo`, or an invaild/outdated
         * descriptor set might be returned.
         *
         * @param slot Rotation slot index, must be less than `slot_count`.
         */
        vk::DescriptorSet GetDescriptorSet(uint32_t slot) const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED
