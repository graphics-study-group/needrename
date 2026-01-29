#include "SceneWidget.h"
#include <Core/Functional/SDLWindow.h>
#include <MainClass.h>
#include <Framework/world/WorldSystem.h>
#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SamplerManager.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/Renderer/Camera.h>
#include <backends/imgui_impl_vulkan.h>

namespace Editor {
    SceneWidget::SceneWidget(const std::string &name) : Widget(name) {
        m_camera.m_camera->m_display_id = 15u;
        // m_camera.m_transform.SetPosition({0.0f, 0.05f, -0.7f});
        // m_camera.m_transform.SetRotationEuler(glm::vec3{1.57, 0.0, 3.1415926});
        // m_camera.m_fov = 45.0f;
        // m_camera.m_aspect_ratio = 1.0f;
        // m_camera.m_clipping_near = 1e-3f;
        // m_camera.m_clipping_far = 1e3f;
        // m_camera.UpdateViewMatrix();
        // m_camera.UpdateProjectionMatrix();
        Engine::MainClass::GetInstance()->GetRenderSystem()->GetCameraManager().RegisterCamera(m_camera.m_camera);
    }

    SceneWidget::~SceneWidget() {
    }

    void SceneWidget::Render() {
        float aspect_ratio = static_cast<float>(m_viewport_size.x) / static_cast<float>(m_viewport_size.y);
        m_camera.m_camera->set_aspect_ratio(aspect_ratio);
        if (aspect_ratio > 1.0f) {
            m_camera.m_camera->set_fov_vertical(m_camera_fov);
        } else {
            m_camera.m_camera->set_fov_horizontal(m_camera_fov);
        }

        if (ImGui::Begin(m_name.c_str())) {
            m_viewport_size = ImGui::GetContentRegionAvail();
            ImGui::Image(
                m_color_att_id,
                m_viewport_size,
                ImVec2(0, 0),
                ImVec2(m_viewport_size.x / m_texture_width, m_viewport_size.y / m_texture_height)
            );

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                m_camera_control_on = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                m_camera_control_on = false;
            }

            if (m_camera_control_on) {
                ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
                m_camera.RotateControl(mouse_delta.x, mouse_delta.y);
                float delta_forward = ImGui::IsKeyDown(ImGuiKey_W) * 1.0f - ImGui::IsKeyDown(ImGuiKey_S) * 1.0f;
                float delta_right = ImGui::IsKeyDown(ImGuiKey_D) * 1.0f - ImGui::IsKeyDown(ImGuiKey_A) * 1.0f;
                m_camera.MoveControl(delta_forward, delta_right);
                m_camera.m_camera->UpdateViewMatrix(m_camera.m_transform);
            }
        }
        ImGui::End();
    }
} // namespace Editor
