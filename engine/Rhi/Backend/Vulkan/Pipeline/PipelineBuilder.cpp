#include "Rhi/Device/DeviceContext.h"

#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Backend/Vulkan/VulkanPipelineState.h"
#include "Rhi/Device/DeviceInterface.h"

#include <array>
#include <vulkan/vulkan.hpp>

namespace {
    /// @brief The stages a pipeline records through, in the order they are set.
    ///
    /// Viewport and scissor are dynamic because a pass decides them; the sample
    /// count is one because the engine never renders multisampled targets.
    constexpr std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };

    vk::ShaderStageFlagBits ToVkShaderStage(Engine::Rhi::ShaderStage stage) {
        switch (stage) {
        case Engine::Rhi::ShaderStage::Vertex:
            return vk::ShaderStageFlagBits::eVertex;
        case Engine::Rhi::ShaderStage::Fragment:
            return vk::ShaderStageFlagBits::eFragment;
        case Engine::Rhi::ShaderStage::Compute:
            return vk::ShaderStageFlagBits::eCompute;
        case Engine::Rhi::ShaderStage::Geometry:
            return vk::ShaderStageFlagBits::eGeometry;
        case Engine::Rhi::ShaderStage::TessellationControl:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case Engine::Rhi::ShaderStage::TessellationEvaluation:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        default:
            return vk::ShaderStageFlagBits::eVertex;
        }
    }
} // namespace

namespace Engine::Rhi {
    PipelineHandle DeviceContext::CreateGraphicsPipeline(const GraphicsPipelineBuildDescription &description) {
        const vk::Device device = ToVulkanDevice(m_device_interface->GetDevice());

        // Specialization constants are baked into the pipeline, so both the values
        // and where each sits have to outlive the create call; the two vectors are
        // sized before any entry points into them.
        const vk::SpecializationInfo specialization_info{
            static_cast<uint32_t>(description.specialization_entries.size()),
            reinterpret_cast<const vk::SpecializationMapEntry *>(description.specialization_entries.data()),
            description.specialization_data.size(),
            description.specialization_data.data()
        };

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        stages.reserve(description.stages.size());
        for (const GraphicsPipelineStage &stage : description.stages) {
            stages.push_back(vk::PipelineShaderStageCreateInfo{
                {},
                ToVkShaderStage(stage.stage),
                ToVulkanShaderModule(stage.module),
                stage.entry_point.c_str(),
                description.specialization_entries.empty() ? nullptr : &specialization_info
            });
        }

        const VulkanPipelineState state{description.pipeline};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, vk::False};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, PIPELINE_DYNAMIC_STATES};

        vk::GraphicsPipelineCreateInfo create_info{};
        create_info.setStages(stages);
        create_info.pNext = &state.Rendering();
        create_info.pVertexInputState = &state.VertexInput();
        create_info.pInputAssemblyState = &state.InputAssembly();
        create_info.pViewportState = &viewport_state;
        create_info.pRasterizationState = &state.Rasterization();
        create_info.pMultisampleState = &multisample_state;
        create_info.pDepthStencilState = &state.DepthStencil();
        create_info.pColorBlendState = &state.ColorBlend();
        create_info.pDynamicState = &dynamic_state;
        create_info.layout = ToVulkanPipelineLayout(description.layout);
        create_info.renderPass = nullptr;
        create_info.subpass = 0;

        const auto result = device.createGraphicsPipelineUnique(nullptr, create_info);
        if (result.result != vk::Result::eSuccess) {
            return {};
        }
        return FromVulkan(result.value.get());
    }
} // namespace Engine::Rhi
