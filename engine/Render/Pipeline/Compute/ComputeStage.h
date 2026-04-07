#ifndef PIPELINE_COMPUTE_COMPUTESTAGE
#define PIPELINE_COMPUTE_COMPUTESTAGE

#include "Asset/InstantiatedFromAsset.h"
#include "Render/Pipeline/PipelineInfo.h"
#include <any>

namespace Engine {
    class RenderSystem;
    class ShaderAsset;
    class ComputeBuffer;
    class ComputeResourceBinding;

    namespace ShdrRfl {
        class SPLayout;
    }

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
    class ComputeStage : public IInstantiatedFromAsset<ShaderAsset> {
        using PassInfo = PipelineInfo::ComputePassInfo;

        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        ComputeStage(RenderSystem &system);

        /**
         * @brief Instantiate a ComputeStage from a compute shader asset.
         * @todo Add specialization constant support.
         */
        void Instantiate(ShaderAsset &asset) override;

        /**
         * @brief Instantiate a ComputeStage from a compute shader SPIR-V code.
         */
        void Instantiate(const std::vector<uint32_t> &code, const std::string_view name);

        ~ComputeStage();

        /**
         * @brief Allocate a new resource binding to this compute stage.
         *
         * This allocated binding is guaranteed to be available until the
         * destruction of this compute stage.
         *
         * @note These bindings will not be de-allocated. Call this member
         * sparingly to avoid memory leak.
         */
        ComputeResourceBinding &AllocateResourceBinding() noexcept;

        /// @brief Get all reflected information of the shader.
        const ShdrRfl::SPLayout &GetReflectedShaderInfo() const noexcept;
        /// @brief Get the compute pipeline
        vk::Pipeline GetPipeline() const noexcept;
        /// @brief Get the pipeline layout
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        /// @brief Get the descriptor set layout
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        /// @brief Get the descriptor pool
        vk::DescriptorPool GetDescriptorPool() const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_COMPUTE_COMPUTESTAGE
