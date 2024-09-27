#ifndef RENDER_MATERIAL_MATERIAL_INCLUDED
#define RENDER_MATERIAL_MATERIAL_INCLUDED

#include <vulkan/vulkan.hpp>
#include <unordered_map>
#include "Render/Pipeline/PipelineLayout.h"
#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    class RenderSystem;

    /// @brief A material at runtime, constructed from MaterialAsset
    class Material
    {
    protected:
        struct MaterialPass {
            std::unique_ptr <Pipeline> pipeline {};
            std::unique_ptr <PipelineLayout> pipeline_layout {};
            vk::DescriptorSet descriptor_set {};
        };

        std::weak_ptr <RenderSystem> m_renderSystem;
        std::vector <MaterialPass> m_passes {};

        /// @brief Get global constant descriptor layouts for PipelineLayout construction.
        /// A helper function for constructing pipeline layout.
        /// @return A vector of vk::DescriptorSetLayout for global constant descriptors
        [[deprecated("Use PipelineLayout::CreateWithDefault instead.")]]
        std::vector <vk::DescriptorSetLayout> GetGlobalDescriptorSetLayout();

    public:
        // TODO: construct material from MaterialAsset

        Material (std::weak_ptr <RenderSystem> system);
        virtual ~Material () = default;

        const virtual Pipeline * GetPipeline(uint32_t pass_index, const RenderTargetSetup & rts) = 0;
        const PipelineLayout * GetPipelineLayout (uint32_t pass_index) const;
        vk::DescriptorSet GetDescriptorSet(uint32_t pass_index) const;
        virtual void WriteDescriptors () const;
    };
};

#endif // RENDER_MATERIAL_MATERIAL_INCLUDED
