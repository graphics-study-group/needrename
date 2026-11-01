#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/Pipeline.h"

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
    struct Pipeline::impl {
        vk::UniquePipeline pipeline{};
    };

    Pipeline::Pipeline(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {}

    Pipeline Pipeline::Create(const DeviceContext &device_context, const GraphicsPipelineBuildDescription &description) {
        const vk::Device device = ToVulkanDevice(device_context.GetDeviceInterface().GetDevice());

        // Specialization constants are baked into the pipeline, so both the values
        // and where each sits have to outlive the create call.
        //
        // The entries are translated rather than reinterpreted: vk::SpecializationMapEntry
        // ends with a size_t while the portable one ends with a uint32_t, so the two
        // are not the same layout and casting between them reads garbage for the size.
        std::vector<vk::SpecializationMapEntry> map_entries;
        map_entries.reserve(description.specialization_entries.size());
        for (const SpecializationConstant &constant : description.specialization_entries) {
            map_entries.push_back(vk::SpecializationMapEntry{constant.constant_id, constant.offset, constant.size});
        }

        const vk::SpecializationInfo specialization_info{
            static_cast<uint32_t>(map_entries.size()),
            map_entries.data(),
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
                map_entries.empty() ? nullptr : &specialization_info
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

        auto result = device.createGraphicsPipelineUnique(nullptr, create_info);
        auto p = std::make_unique<impl>();
        if (result.result == vk::Result::eSuccess) {
            p->pipeline = std::move(result.value);
        }
        return Pipeline(std::move(p));
    }
    Pipeline::~Pipeline() = default;
    Pipeline::Pipeline(Pipeline &&) noexcept = default;
    Pipeline &Pipeline::operator=(Pipeline &&) noexcept = default;

    PipelineHandle Pipeline::GetHandle() const noexcept {
        return pimpl && pimpl->pipeline ? FromVulkan(pimpl->pipeline.get()) : PipelineHandle{};
    }

    bool Pipeline::IsValid() const noexcept {
        return pimpl && static_cast<bool>(pimpl->pipeline);
    }

} // namespace Engine::Rhi

