#ifndef RENDER_PIPELINE_PIPELINE_INCLUDED
#define RENDER_PIPELINE_PIPELINE_INCLUDED

#include <memory>
#include <vulkan/vulkan.hpp>
#include "Render/RenderSystem.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/PipelineLayout.h"

namespace Engine{

    class ShaderModule;

    /// @brief A Vulkan pipeline.
    /// Pipelines define fixed-function and programmable stages of an actual rasterization pipeline on graphic cards, 
    /// whereas passes specify data (called attachments) flow within them.
    class Pipeline : public VkWrapper<vk::UniquePipeline> {
    public:

        Pipeline (std::weak_ptr <RenderSystem> system);

        virtual void CreatePipeline(
            Subpass subpass, 
            const PipelineLayout & layout, 
            const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
        ) = 0;

        const Subpass & GetSubpass() const;

    protected:

        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState();
        static vk::PipelineDynamicStateCreateInfo GetDynamicState();
        static vk::PipelineInputAssemblyStateCreateInfo GetInputAssemblyState();
        static vk::PipelineViewportStateCreateInfo GetViewportState();

        Subpass m_attached_subpass {};

        constexpr static std::array<vk::DynamicState, 2> dynamic_states = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
    };
}

#endif // RENDER_PIPELINE_PIPELINE_INCLUDED
