#ifndef RENDER_MATERIAL_MATERIAL_INCLUDED
#define RENDER_MATERIAL_MATERIAL_INCLUDED

#include <vulkan/vulkan.hpp>
#include <unordered_map>

namespace Engine {
    class RenderSystem;
    class Pipeline;
    class PipelineLayout;

    /// @brief A material at runtime, constructed from MaterialAsset
    class Material
    {
    protected:
        std::weak_ptr <RenderSystem> m_renderSystem;
        std::unordered_map <
            uint32_t, 
            std::pair<
                std::unique_ptr <Pipeline>, 
                std::unique_ptr <PipelineLayout>
            >
        > m_pipelines;

        /// @brief A helper function for constructing pipeline layout
        /// @return 
        std::vector <vk::DescriptorSetLayout> GetGlobalDescriptorSetLayout();

    public:
        // TODO: construct material from MaterialAsset

        Material (std::weak_ptr <RenderSystem> system);
        virtual ~Material () = default;

        // TODO: Design an interface for per-material descriptors

        const Pipeline & GetPipeline(uint32_t pass_index) const;
        const PipelineLayout & GetPipelineLayout (uint32_t pass_index) const;
    };
};

#endif // RENDER_MATERIAL_MATERIAL_INCLUDED
