#ifndef EDITOR_WIDGET_SCENEWIDGET_INCLUDED
#define EDITOR_WIDGET_SCENEWIDGET_INCLUDED

#include "Widget.h"
#include <Render/Pipeline/RenderTargetBinding.h>
#include <memory>
#include <imgui.h>
#include <Editor/Functional/SceneCamera.h>

namespace Engine
{
    class RenderSystem;
    class SampledTexture;
}

namespace Editor
{
    class SceneWidget : public Widget
    {
    public:
        SceneWidget(const std::string &name);
        virtual ~SceneWidget();

        virtual void Render() override;
        
        void CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system);
        void PreRender();

    protected:
        Engine::RenderTargetBinding m_render_target_binding{};
        SceneCamera m_camera{};

        ImVec2 m_viewport_size{1280, 720};

        bool m_camera_control_on{false};

    public:
        float m_camera_fov{45.0f};

    public:
        // TODO: Need better way to allocate textures and set barriers.
        int m_texture_width{1960};
        int m_texture_height{1080};
        std::shared_ptr<Engine::SampledTexture> m_color_texture{};
        std::shared_ptr<Engine::SampledTexture> m_depth_texture{};
        ImTextureID m_color_att_id{};
    };
}

#endif // EDITOR_WIDGET_SCENEWIDGET_INCLUDED
