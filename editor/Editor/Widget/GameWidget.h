#ifndef EDITOR_WIDGET_GAMEWIDGET_INCLUDED
#define EDITOR_WIDGET_GAMEWIDGET_INCLUDED

#include "Widget.h"
#include <Render/Pipeline/RenderTargetBinding.h>
#include <memory>
#include <imgui.h>

namespace Engine
{
    class RenderSystem;
    class SampledTexture;
    class GraphicsCommandBuffer;
}

namespace Editor
{
    class GameWidget : public Widget
    {
    public:
        GameWidget(const char *name);
        virtual ~GameWidget();

        virtual void Render() override;
        
        void CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system);
        void PreRender(Engine::GraphicsCommandBuffer &cb);

        Engine::RenderTargetBinding m_render_target_binding{};

        int m_game_width = 1280;
        int m_game_height = 720;

        std::shared_ptr<Engine::SampledTexture> m_color_texture{};
        std::shared_ptr<Engine::SampledTexture> m_depth_texture{};
        ImTextureID m_color_att_id{};
    };
}

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
