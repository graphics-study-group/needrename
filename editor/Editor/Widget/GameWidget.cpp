#include "GameWidget.h"

#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>

#include <Core/Functional/SDLWindow.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SamplerManager.h>
#include <Render/Renderer/Camera.h>

#include <backends/imgui_impl_vulkan.h>

namespace Editor {
    GameWidget::GameWidget(const std::string &name) : Widget(name) {
    }

    GameWidget::~GameWidget() {
    }

    void GameWidget::SetDisplayTexture(const Engine::RenderTargetTexture &texture) {
        m_color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
            texture.GetSampler(), texture.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ));
        m_texture_size = ImVec2(texture.GetTextureDescription().width, texture.GetTextureDescription().height);
    }

    void GameWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            ImVec2 available_size = ImGui::GetContentRegionAvail();
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
