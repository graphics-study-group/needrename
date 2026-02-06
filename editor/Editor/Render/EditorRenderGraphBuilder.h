#ifndef EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
#define EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED

#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    class AssetRef;
    class ComputeStage;
}

namespace Editor {
    

    class EditorRenderGraphBuilder : public Engine::RenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        EditorRenderGraphBuilder(Engine::RenderSystem &system);
        ~EditorRenderGraphBuilder() = default;

        std::unique_ptr<Engine::RenderGraph> BuildEditorRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            std::function<vk::Extent2D()> get_scene_widget_viewport_func,
            std::function<uint8_t()> get_scene_camera_index_func,
            std::function<vk::Extent2D()> get_game_widget_viewport_func,
            std::function<uint8_t()> get_game_camera_index_func,
            Engine::GUISystem * gui_system,
            int32_t &scene_widget_color_id,
            int32_t &game_widget_color_id,
            int32_t &final_color_target_id
        );

    protected:
        std::shared_ptr<Engine::AssetRef> m_bloom_shader{};
        std::shared_ptr<Engine::ComputeStage> m_game_bloom_compute_stage{};
        std::shared_ptr<Engine::ComputeStage> m_scene_bloom_compute_stage{};
    };
} // namespace Engine

#endif // EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
