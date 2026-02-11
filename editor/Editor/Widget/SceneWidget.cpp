#include "SceneWidget.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/Scene/LevelAsset.h>
#include <Core/Functional/SDLWindow.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/serialization.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/Renderer/Camera.h>

#include <SDL3/SDL.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

namespace Editor {
    SceneWidget::SceneWidget(const std::string &name) : Widget(name) {
        m_camera.m_camera->m_display_id = 15u;
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
                ImVec2(m_viewport_size.x / m_texture_size.x, m_viewport_size.y / m_texture_size.y)
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

            if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                auto &adb = *std::dynamic_pointer_cast<Engine::FileSystemDatabase>(
                    Engine::MainClass::GetInstance()->GetAssetDatabase()
                );
                auto level_asset = adb.GetNewAssetRef(Engine::AssetPath{adb, "/default_level.asset"}).as<Engine::LevelAsset>();
                Engine::MainClass::GetInstance()->GetWorldSystem()->SaveLevelToAsset(*level_asset);
                Engine::Serialization::Archive archive;
                archive.prepare_save();
                level_asset->save_asset_to_archive(archive);
                adb.SaveArchive(archive, adb.GetAssetPath(level_asset->GetGUID()));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Save level to %s", "/default_level.asset");
            }
        }
        ImGui::End();
    }

    void SceneWidget::SetDisplayTexture(const Engine::RenderTargetTexture &texture) {
        m_color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
            texture.GetSampler(), texture.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ));
        m_texture_size = ImVec2(texture.GetTextureDescription().width, texture.GetTextureDescription().height);
    }

    uint8_t SceneWidget::GetCameraIndex() const {
        return m_camera.m_camera->m_display_id;
    }
} // namespace Editor
