#ifndef EDITOR_WIDGET_SCENEWIDGET_INCLUDED
#define EDITOR_WIDGET_SCENEWIDGET_INCLUDED

#include "Widget.h"
#include <Editor/Functional/SceneCamera.h>
#include <imgui.h>
#include <memory>

namespace Engine {
    class RenderTargetTexture;
}

namespace Editor {
    class SceneWidget : public Widget {
    public:
        SceneWidget(const std::string &name);
        virtual ~SceneWidget();

        virtual void Render() override;

        void SetDisplayTexture(const Engine::RenderTargetTexture &texture);
        uint8_t GetCameraIndex() const;

    public:
        ImVec2 m_viewport_size{1280, 720};
        float m_camera_fov{45.0f};

    protected:
        SceneCamera m_camera{};
        bool m_camera_control_on{false};
        ImTextureID m_color_att_id{0};
        ImVec2 m_texture_size{};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_SCENEWIDGET_INCLUDED
