#ifndef EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
#define EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED

#include "Asset/AssetRef.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include <memory>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class ComputeStage;
    class RenderGraph;
    class RenderSystem;
} // namespace Engine

namespace Editor {
    class SceneWidget;
    class GameWidget;

    class EditorRenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        EditorRenderGraphBuilder(Engine::RenderSystem &system);
        ~EditorRenderGraphBuilder() = default;

        std::unique_ptr<Engine::RenderGraph> BuildEditorRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            SceneWidget *scene_widget,
            GameWidget *game_widget,
            Engine::RGTextureHandle &scene_widget_color_id,
            Engine::RGTextureHandle &game_widget_color_id,
            Engine::RGTextureHandle &final_color_target_id
        );

    protected:
        Engine::RenderSystem &m_system;
        Engine::AssetRef m_bloom_shader{};
        std::shared_ptr<Engine::ComputeStage> m_game_bloom_compute_stage{};
        std::shared_ptr<Engine::ComputeStage> m_scene_bloom_compute_stage{};
    };
} // namespace Editor

#endif // EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
