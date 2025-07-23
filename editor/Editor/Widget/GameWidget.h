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
        GameWidget(const std::string &name);
        virtual ~GameWidget();

        virtual void Render() override;

        void CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system);
        void PreRender();
        
    protected:
        Engine::RenderTargetBinding m_render_target_binding{};
        ImVec2 m_viewport_size{1280, 720};

    public:
        // TODO: Need better way to allocate textures and set barriers.
        int m_texture_width{1960};
        int m_texture_height{1080};
        std::shared_ptr<Engine::SampledTexture> m_color_texture{};
        std::shared_ptr<Engine::SampledTexture> m_depth_texture{};
        ImTextureID m_color_att_id{};

        bool m_accept_input{false};
    };
}

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
