#ifndef PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
#define PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED

#include "Asset/InstantiatedFromAsset.h"
#include "Render/Pipeline/PipelineInfo.h"
#include <any>

namespace Engine {
    class RenderSystem;
    class ShaderAsset;
    class ComputeBuffer;

    namespace ShdrRfl {
        class ShaderParameters;
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

        void UpdateGPUInfo(uint32_t backbuffer);

        void AssignScalarVariable(const std::string & name, std::variant<uint32_t, float> value) noexcept;
        void AssignVectorVariable(const std::string & name, std::variant<glm::vec4, glm::mat4> value) noexcept;

        void AssignTexture(const std::string & name, const Texture & texture) noexcept;
        void AssignBuffer(const std::string & name, const DeviceBuffer & buffer) noexcept;
        void AssignComputeBuffer(const std::string & name, const ComputeBuffer & buffer) noexcept;

        vk::Pipeline GetPipeline() const noexcept;
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;

        vk::DescriptorSet GetDescriptorSet(uint32_t backbuffer) const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
