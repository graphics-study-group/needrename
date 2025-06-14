#include "SceneWidget.h"
#include <backends/imgui_impl_vulkan.h>
#include <Render/Memory/Image2D.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Render/Pipeline/CommandBuffer/RenderCommandBuffer.h>

namespace Editor
{
    SceneWidget::SceneWidget(const std::string &name) : Widget(name)
    {
        // m_camera.m_transform.SetPosition({0.0f, 0.05f, -0.7f});
        // m_camera.m_transform.SetRotationEuler(glm::vec3{1.57, 0.0, 3.1415926});
        // m_camera.m_fov = 45.0f;
        // m_camera.m_aspect_ratio = 1.0f;
        // m_camera.m_clipping_near = 1e-3f;
        // m_camera.m_clipping_far = 1e3f;
        // m_camera.UpdateViewMatrix();
        // m_camera.UpdateProjectionMatrix();
    }

    SceneWidget::~SceneWidget()
    {
        vkDestroySampler(Engine::MainClass::GetInstance()->GetRenderSystem()->getDevice(), m_sampler, nullptr);
    }

    void SceneWidget::CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system)
    {
        SDL_GetWindowSizeInPixels(Engine::MainClass::GetInstance()->GetWindow()->GetWindow(), &m_texture_width, &m_texture_height);
        m_color_image = std::make_shared<Engine::AllocatedImage2D>(render_system);
        m_color_image->Create(m_texture_width, m_texture_height, Engine::ImageUtils::ImageType::ColorGeneral, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
        m_depth_image = std::make_shared<Engine::AllocatedImage2D>(render_system);
        m_depth_image->Create(m_texture_width, m_texture_height, Engine::ImageUtils::ImageType::SampledDepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);
        Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
        color_att.image = m_color_image->GetImage();
        color_att.image_view = m_color_image->GetImageView();
        color_att.load_op = vk::AttachmentLoadOp::eClear;
        color_att.store_op = vk::AttachmentStoreOp::eStore;
        m_render_target_binding.SetColorAttachment(color_att);
        depth_att.image = m_depth_image->GetImage();
        depth_att.image_view = m_depth_image->GetImageView();
        depth_att.load_op = vk::AttachmentLoadOp::eClear;
        depth_att.store_op = vk::AttachmentStoreOp::eDontCare;
        m_render_target_binding.SetDepthAttachment(depth_att);

        vk::SamplerCreateInfo sci{};
        sci.magFilter = sci.minFilter = vk::Filter::eNearest;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = vk::SamplerAddressMode::eRepeat;
        m_sampler = render_system->getDevice().createSampler(sci);
        m_color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(m_sampler, color_att.image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }

    void SceneWidget::PreRender(Engine::RenderCommandBuffer &cb)
    {
        m_camera.m_aspect_ratio = static_cast<float>(m_viewport_size.x) / static_cast<float>(m_viewport_size.y);
        m_camera.UpdateProjectionMatrix();
        cb.BeginRendering(m_render_target_binding, {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y});
        Engine::MainClass::GetInstance()->GetRenderSystem()->DrawMeshes(m_camera.m_view_matrix, m_camera.m_projection_matrix, {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y});
        cb.EndRendering();
        cb.InsertAttachmentBarrier(Engine::RenderCommandBuffer::AttachmentBarrierType::ColorAttachmentRAW, m_color_image->GetImage());
    }

    void SceneWidget::Render()
    {
        if (ImGui::Begin(m_name.c_str()))
        {
            m_viewport_size = ImGui::GetContentRegionAvail();
            ImGui::Image(m_color_att_id, m_viewport_size, ImVec2(0, 0), ImVec2(m_viewport_size.x / m_texture_width, m_viewport_size.y / m_texture_height));

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_camera_control_on = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                m_camera_control_on = false;
            }

            if (m_camera_control_on)
            {
                ImVec2 mouse_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
                m_camera.RotateControl(mouse_delta.x, mouse_delta.y);
                float delta_forward = ImGui::IsKeyDown(ImGuiKey_W) * 1.0f - ImGui::IsKeyDown(ImGuiKey_S) * 1.0f;
                float delta_right = ImGui::IsKeyDown(ImGuiKey_D) * 1.0f - ImGui::IsKeyDown(ImGuiKey_A) * 1.0f;
                m_camera.MoveControl(delta_forward, delta_right);
                m_camera.UpdateViewMatrix();
            }
        }
        ImGui::End();
    }
}
