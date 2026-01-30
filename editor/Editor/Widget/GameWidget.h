#ifndef EDITOR_WIDGET_GAMEWIDGET_INCLUDED
#define EDITOR_WIDGET_GAMEWIDGET_INCLUDED

#include "Widget.h"
#include <imgui.h>
#include <memory>

namespace Engine {
    class RenderTargetTexture;
}

namespace Editor {
    class GameWidget : public Widget {
    public:
        GameWidget(const std::string &name);
        virtual ~GameWidget();

        virtual void Render() override;
        
        void SetDisplayTexture(const Engine::RenderTargetTexture &texture);

    public:
        ImVec2 m_viewport_size{1920, 1080};
        bool m_accept_input{false};

    protected:
        ImTextureID m_color_att_id{};
        ImVec2 m_texture_size{};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
