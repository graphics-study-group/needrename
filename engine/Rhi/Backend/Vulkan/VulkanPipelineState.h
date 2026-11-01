#ifndef ENGINE_RHI_VULKANPIPELINESTATE_INCLUDED
#define ENGINE_RHI_VULKANPIPELINESTATE_INCLUDED

#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/rhi_export.h"

#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief The Vulkan structures one pipeline description becomes.
     *
     * The six state structures point into the vectors this object owns, so it is
     * neither copyable nor movable: a caller builds it, uses it to create a
     * pipeline, and lets it go out of scope afterwards.
     */
    class RHI_API VulkanPipelineState {
    public:
        /// @brief Translate a portable pipeline description.
        explicit VulkanPipelineState(const PipelineDescription &description);

        VulkanPipelineState(const VulkanPipelineState &) = delete;
        VulkanPipelineState &operator=(const VulkanPipelineState &) = delete;
        VulkanPipelineState(VulkanPipelineState &&) = delete;
        VulkanPipelineState &operator=(VulkanPipelineState &&) = delete;

        /// @brief Vertex buffers, translated.
        const vk::PipelineVertexInputStateCreateInfo &VertexInput() const noexcept { return m_vertex_input; }
        /// @brief Primitive assembly, translated.
        const vk::PipelineInputAssemblyStateCreateInfo &InputAssembly() const noexcept { return m_input_assembly; }
        /// @brief Rasterisation, translated.
        const vk::PipelineRasterizationStateCreateInfo &Rasterization() const noexcept { return m_rasterization; }
        /// @brief Depth and stencil, translated.
        const vk::PipelineDepthStencilStateCreateInfo &DepthStencil() const noexcept { return m_depth_stencil; }
        /// @brief Blending, translated.
        const vk::PipelineColorBlendStateCreateInfo &ColorBlend() const noexcept { return m_color_blend; }
        /// @brief The attachment formats the pipeline is created against.
        const vk::PipelineRenderingCreateInfo &Rendering() const noexcept { return m_rendering; }

    private:
        std::vector<vk::VertexInputBindingDescription> m_vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> m_vertex_attributes;
        std::vector<vk::PipelineColorBlendAttachmentState> m_blend_attachments;
        std::vector<vk::Format> m_color_formats;

        vk::PipelineVertexInputStateCreateInfo m_vertex_input{};
        vk::PipelineInputAssemblyStateCreateInfo m_input_assembly{};
        vk::PipelineRasterizationStateCreateInfo m_rasterization{};
        vk::PipelineDepthStencilStateCreateInfo m_depth_stencil{};
        vk::PipelineColorBlendStateCreateInfo m_color_blend{};
        vk::PipelineRenderingCreateInfo m_rendering{};
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_VULKANPIPELINESTATE_INCLUDED

