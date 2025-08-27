#include "SceneWidget.h"
#include <Functional/SDLWindow.h>
#include <MainClass.h>
#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>
#include <Render/Memory/SampledTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
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
    }

    SceneWidget::~SceneWidget() {
    }

    void SceneWidget::CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system) {
        SDL_GetWindowSizeInPixels(
            Engine::MainClass::GetInstance()->GetWindow()->GetWindow(), &m_texture_width, &m_texture_height
        );
        m_color_texture = std::make_shared<Engine::SampledTexture>(*render_system);
        m_depth_texture = std::make_shared<Engine::SampledTexture>(*render_system);
        Engine::Texture::TextureDesc desc{
            .dimensions = 2,
            .width = (uint32_t)m_texture_width,
            .height = (uint32_t)m_texture_height,
            .depth = 1,
            .format = Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm,
            .type = Engine::ImageUtils::ImageType::ColorGeneral,
            .mipmap_levels = 1,
            .array_layers = 1,
            .is_cube_map = false
        };
        m_color_texture->CreateTextureAndSampler(desc, {}, "Scene color attachment");
        desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
        desc.type = Engine::ImageUtils::ImageType::SampledDepthImage;
        m_depth_texture->CreateTextureAndSampler(desc, {}, "Scene depth attachment");

        Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
        color_att.texture = m_color_texture.get();
        color_att.texture_view = nullptr;
        color_att.load_op = Engine::AttachmentUtils::LoadOperation::Clear;
        color_att.store_op = Engine::AttachmentUtils::StoreOperation::Store;
        m_render_target_binding.SetColorAttachment(color_att);
        depth_att.texture = m_depth_texture.get();
        depth_att.texture_view = nullptr;
        depth_att.load_op = Engine::AttachmentUtils::LoadOperation::Clear;
        depth_att.store_op = Engine::AttachmentUtils::StoreOperation::DontCare;
        m_render_target_binding.SetDepthAttachment(depth_att);

        m_color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
            m_color_texture->GetSampler(), m_color_texture->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ));
    }

    void SceneWidget::PreRender() {
        float aspect_ratio = static_cast<float>(m_viewport_size.x) / static_cast<float>(m_viewport_size.y);
        m_camera.m_camera->set_aspect_ratio(aspect_ratio);
        if (aspect_ratio > 1.0f) {
            m_camera.m_camera->set_fov_vertical(m_camera_fov);
        } else {
            m_camera.m_camera->set_fov_horizontal(m_camera_fov);
        }

        auto context = Engine::MainClass::GetInstance()->GetRenderSystem()->GetFrameManager().GetGraphicsContext();
        auto &cb = dynamic_cast<Engine::GraphicsCommandBuffer &>(context.GetCommandBuffer());
        context.UseImage(
            *m_color_texture,
            Engine::GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            Engine::GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            *m_depth_texture,
            Engine::GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            Engine::GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        cb.BeginRendering(
            m_render_target_binding, {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y}, "Editor Scene Pass"
        );
        Engine::MainClass::GetInstance()->GetRenderSystem()->SetActiveCamera(m_camera.m_camera);
        cb.DrawRenderers(
            Engine::MainClass::GetInstance()->GetRenderSystem()->GetRendererManager().FilterAndSortRenderers({}),
            m_camera.m_camera->GetViewMatrix(),
            m_camera.m_camera->GetProjectionMatrix(),
            {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y},
            0
        );
        cb.EndRendering();
    }

    void SceneWidget::Render() {
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
