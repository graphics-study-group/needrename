#ifndef ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED
#define ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/rhi_export.h"

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
         * @brief Upload GPU info by recording descriptor writes and perform
         * Uniform buffer writes.
         *
         * @param slot Rotation slot index, advanced by the caller in
         * lockstep with its own submission cadence. Must be less than
         * `slot_count`.
         *
         * @return The slot's descriptor set, acquired from the device
         * descriptor arena, together with the dynamic offsets needed to bind
         * it. Re-acquire in every epoch whose command buffers use the set.
         */
        DescriptorSetBinding UpdateGPUInfo(uint32_t slot);
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTERESOURCEBINDING_INCLUDED
