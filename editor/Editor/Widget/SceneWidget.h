#ifndef EDITOR_WIDGET_SCENEWIDGET_INCLUDED
#define EDITOR_WIDGET_SCENEWIDGET_INCLUDED

#include "Widget.h"
#include <Editor/Functional/SceneCamera.h>
#include <imgui.h>
#include <memory>

namespace Editor {
    class SceneWidget : public Widget {
    public:
        SceneWidget(const std::string &name);
        virtual ~SceneWidget();

        virtual void Render() override;

    protected:
        SceneCamera m_camera{};
        bool m_camera_control_on{false};

    public:
        ImVec2 m_viewport_size{1280, 720};
        float m_camera_fov{45.0f};
        int m_texture_width{1960};
        int m_texture_height{1080};

        ImTextureID m_color_att_id{0};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_SCENEWIDGET_INCLUDED
