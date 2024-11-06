#include "Pipeline.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Renderer/SkinnedHomogeneousMesh.h"
#include "Render/Pipeline/Shader.h"

namespace Engine
{
    Pipeline::Pipeline(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}

    vk::PipelineVertexInputStateCreateInfo Pipeline::GetVertexInputState(bool skinned) {
        if (skinned) return SkinnedHomogeneousMesh::GetVertexInputState();
        return HomogeneousMesh::GetVertexInputState();
    }

    vk::PipelineDynamicStateCreateInfo Pipeline::GetDynamicState() {
        vk::PipelineDynamicStateCreateInfo info{};
        info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        info.pDynamicStates = dynamic_states.data();
        return info;
    }

    vk::PipelineInputAssemblyStateCreateInfo Pipeline::GetInputAssemblyState() {
        vk::PipelineInputAssemblyStateCreateInfo info{};
        info.topology = vk::PrimitiveTopology::eTriangleList;
        info.primitiveRestartEnable = vk::False;
        return info;
    }

    vk::PipelineViewportStateCreateInfo Pipeline::GetViewportState() {
        vk::PipelineViewportStateCreateInfo info{};
        info.viewportCount = 1;
        info.scissorCount = 1;
        return info;
    }
} // namespace Engine

