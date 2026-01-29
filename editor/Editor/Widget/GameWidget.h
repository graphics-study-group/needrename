#ifndef EDITOR_WIDGET_GAMEWIDGET_INCLUDED
#define EDITOR_WIDGET_GAMEWIDGET_INCLUDED

#include "Widget.h"
#include <imgui.h>
#include <memory>

namespace Editor {
    class GameWidget : public Widget {
    public:
        GameWidget(const std::string &name);
        virtual ~GameWidget();

        virtual void Render() override;

    public:
        ImVec2 m_viewport_size{1280, 720};
        int m_texture_width{1920};
        int m_texture_height{1080};
        bool m_accept_input{false};

        ImTextureID m_color_att_id{0};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
