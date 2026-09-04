#ifndef ENGINE_RHI_COMPUTESTAGE_INCLUDED
#define ENGINE_RHI_COMPUTESTAGE_INCLUDED

#include "Rhi/Pipeline/PipelineInfo.h"
#include "Rhi/rhi_export.h"

#include <any>
#include <memory>

namespace Engine {
    class RenderSystem;
    class ShaderAsset;
} // namespace Engine

namespace Engine::Rhi {
    class ComputeBuffer;
    class ComputeResourceBinding;
    class DeviceContext;
    struct SPLayout;

    /**
     * @brief Compute pipeline used for compute kernel dispatches.
     *
     * It maintains a pipeline layout, a descriptor set layout, a pipeline
     * and a decriptor pool.
     *
     * Its descriptor set layout is reflected from the compute shader, and
     * follows the same restrictions specified in the @ref material_descriptor
     * "(`Engine::MaterialTemplate` documentation)".
     */
    class RHI_API ComputeStage {
        using PassInfo = PipelineInfo::ComputePassInfo;

        DeviceContext &m_device_context;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        explicit ComputeStage(DeviceContext &device_context);

        /**
         * @brief Instantiate a ComputeStage from compute shader SPIR-V code.
         */
        void Instantiate(const std::vector<uint32_t> &code, const std::string_view name);

        ~ComputeStage();

        /**
         * @brief Allocate a new resource binding to this compute stage.
         *
         * This allocated binding is guaranteed to be available until the
         * destruction of this compute stage.
         *
         * @param slot_count Rotation depth of the binding: the number of
         * descriptor-set / UBO-slice slots the caller rotates through in
         * lockstep with its own submission cadence. Must be at least the
         * number of batches the caller keeps in flight.
         *
         * @note These bindings will not be de-allocated. Call this member
         * sparingly to avoid memory leak.
         */
        ComputeResourceBinding &AllocateResourceBinding(uint32_t slot_count = 1) noexcept;

        /// @brief Get all reflected information of the shader.
        const Rhi::SPLayout &GetReflectedShaderInfo() const noexcept;
        /// @brief Get the size in bytes of the shader's push constant block (0 if none).
        uint32_t GetPushConstantSize() const noexcept;
        /// @brief Get the compute pipeline
        vk::Pipeline GetPipeline() const noexcept;
        /// @brief Get the pipeline layout
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        /// @brief Get the descriptor set layout
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        /// @brief Get the descriptor pool
        vk::DescriptorPool GetDescriptorPool() const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTESTAGE_INCLUDED
