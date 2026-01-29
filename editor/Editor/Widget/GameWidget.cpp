#include "GameWidget.h"

#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>

#include <Framework/world/WorldSystem.h>
#include <Core/Functional/SDLWindow.h>
#include <MainClass.h>
#include <Render/Renderer/Camera.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SamplerManager.h>
#include <Render/RenderSystem/CameraManager.h>

namespace Editor {
    GameWidget::GameWidget(const std::string &name) : Widget(name) {
    }

    GameWidget::~GameWidget() {
    }

    void GameWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            ImGui::Image(
                m_color_att_id,
                m_viewport_size,
                ImVec2(0, 0),
                ImVec2(m_viewport_size.x / m_texture_width, m_viewport_size.y / m_texture_height)
            );
            m_accept_input = ImGui::IsWindowFocused(ImGuiFocusedFlags_None)
                             && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        }
        ImGui::End();
    }
} // namespace Editor
