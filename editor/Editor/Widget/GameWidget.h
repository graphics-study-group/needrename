#ifndef EDITOR_WIDGET_GAMEWIDGET_INCLUDED
#define EDITOR_WIDGET_GAMEWIDGET_INCLUDED

#include "Widget.h"
#include <Render/Pipeline/RenderTargetBinding.h>
#include <memory>
#include <imgui.h>

namespace Engine
{
    class RenderSystem;
    class AllocatedImage2D;
    class RenderCommandBuffer;
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
        void PreRender(Engine::RenderCommandBuffer &cb);

    protected:
        Engine::RenderTargetBinding m_render_target_binding{};

        int m_game_width = 1280;
        int m_game_height = 720;

    private:
        std::shared_ptr<Engine::AllocatedImage2D> m_color_image{};
        std::shared_ptr<Engine::AllocatedImage2D> m_depth_image{};
        vk::Sampler m_sampler{};
        ImTextureID m_color_att_id{};
    };
}

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
