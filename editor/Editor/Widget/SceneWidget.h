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
    class AllocatedImage2D;
    class RenderCommandBuffer;
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
        void PreRender(Engine::RenderCommandBuffer &cb);

    protected:
        Engine::RenderTargetBinding m_render_target_binding{};
        SceneCamera m_camera;

        int m_game_width = 1280;
        int m_game_height = 720;

    private:
        std::shared_ptr<Engine::AllocatedImage2D> m_color_image{};
        std::shared_ptr<Engine::AllocatedImage2D> m_depth_image{};
        vk::Sampler m_sampler{};
        ImTextureID m_color_att_id{};
    };
}

#endif // EDITOR_WIDGET_SCENEWIDGET_INCLUDED
