#include "GameWidget.h"

#include <Render/AttachmentUtils.h>
#include <Rhi/Texture/ImageUtils.h>

#include <Core/Functional/SDLWindow.h>
#include <Framework/MainClass.h>
#include <Framework/World/WorldSystem.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Renderer/Camera.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/Resource/RenderTargetTexture.h>
#include <Render/UserInterface/GUISystem.h>

#include <SDL3/SDL.h>

namespace Editor {
    GameWidget::GameWidget(const std::string &name) : Widget(name) {
    }

    GameWidget::~GameWidget() {
    }

    void GameWidget::SetDisplayTexture(const Engine::RenderTargetTexture &texture) {
        // The GUI system owns what an image id means on the selected backend, so the widget does
        // not name a texture API itself: it asks for an id and releases the previous one.
        auto gui = Engine::MainClass::GetInstance()->GetGUISystem();
        if (m_color_att_id != ImTextureID_Invalid) {
            gui->RemoveTexture(m_color_att_id);
            m_color_att_id = ImTextureID_Invalid;
        }
        m_color_att_id = gui->AddTexture(texture);
        m_texture_size = ImVec2(texture.GetTextureDescription().width, texture.GetTextureDescription().height);
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "[GameWidget::SetDisplayTexture] texture=%p size=(%u,%u) ds=%llu",
            static_cast<const void *>(&texture),
            texture.GetTextureDescription().width,
            texture.GetTextureDescription().height,
            static_cast<unsigned long long>(m_color_att_id)
        );
    }

    void GameWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            ImVec2 available_size = ImGui::GetContentRegionAvail();
            m_viewport_size = available_size;
            ImVec2 scaled_size;
            float aspect_ratio = m_texture_size.x / m_texture_size.y;
            if (available_size.x / available_size.y > aspect_ratio) {
                scaled_size.y = available_size.y;
                scaled_size.x = available_size.y * aspect_ratio;
            } else {
                scaled_size.x = available_size.x;
                scaled_size.y = available_size.x / aspect_ratio;
            }
            ImVec2 cursor_pos = ImGui::GetCursorPos();
            ImVec2 centering_offset(
                (available_size.x - scaled_size.x) * 0.5f, (available_size.y - scaled_size.y) * 0.5f
            );
            ImGui::SetCursorPos(ImVec2(cursor_pos.x + centering_offset.x, cursor_pos.y + centering_offset.y));
            ImGui::Image(
                m_color_att_id,
                scaled_size,
                ImVec2(0, 0),
                ImVec2(m_viewport_size.x / m_texture_size.x, m_viewport_size.y / m_texture_size.y)
            );
            m_accept_input = ImGui::IsWindowFocused(ImGuiFocusedFlags_None)
                             && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        }
        ImGui::End();
    }
} // namespace Editor
