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

    class ComputeStage : public IInstantiatedFromAsset<ShaderAsset> {
        using PassInfo = PipelineInfo::ComputePassInfo;

        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        ComputeStage(RenderSystem &system);

        // TODO: Add specialization constant support.
        void Instantiate(const ShaderAsset &asset) override;
        void Instantiate(const std::vector<uint32_t> & code, const std::string_view name);

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
        ComputeResourceBinding & AllocateResourceBinding() noexcept;

        const ShdrRfl::SPLayout & GetReflectedShaderInfo() const noexcept;
        vk::Pipeline GetPipeline() const noexcept;
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        vk::DescriptorPool GetDescriptorPool() const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_COMPUTE_COMPUTESTAGE
