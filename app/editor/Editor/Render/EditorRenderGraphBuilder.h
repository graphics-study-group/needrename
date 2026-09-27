#ifndef EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
#define EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED

#include "Asset/AssetRef.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include <memory>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
        class ComputeKernel;
    } // namespace Rhi
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
        /// @brief The shared bloom kernel, owned by the device context. The scene
        /// and game widget passes both dispatch it.
        Engine::Rhi::ComputeKernel *m_bloom_kernel{};
    };
} // namespace Editor

#endif // EDITOR_RENDER_EDITORRENDERGRAPHBUILDER_INCLUDED
