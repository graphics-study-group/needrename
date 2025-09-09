#ifndef EDITOR_WIDGET_GAMEWIDGET_INCLUDED
#define EDITOR_WIDGET_GAMEWIDGET_INCLUDED

#include "Widget.h"
#include <imgui.h>
#include <memory>

namespace Engine {
    class RenderSystem;
    class Texture;
    class GraphicsCommandBuffer;
} // namespace Engine

namespace Editor {
    class GameWidget : public Widget {
    public:
        GameWidget(const std::string &name);
        virtual ~GameWidget();

        virtual void Render() override;

        void CreateRenderTargets(std::shared_ptr<Engine::RenderSystem> render_system);
        /// @brief Draw the active camera to the widget texture used for ImGui. Contain a synchronization operation for
        /// writting to the color and depth textures.
        /// XXX: Should be rewritten after we have a better render pipline.
        void PreRender();

    protected:
        ImVec2 m_viewport_size{1280, 720};

    public:
        // TODO: Need better way to allocate textures and set barriers.
        int m_texture_width{1920};
        int m_texture_height{1080};
        std::shared_ptr<Engine::Texture> m_color_texture{};
        std::shared_ptr<Engine::Texture> m_depth_texture{};
        ImTextureID m_color_att_id{};

        bool m_accept_input{false};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_GAMEWIDGET_INCLUDED
