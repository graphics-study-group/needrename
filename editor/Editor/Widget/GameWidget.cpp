#include "GameWidget.h"
#include <backends/imgui_impl_vulkan.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Framework/world/WorldSystem.h>
#include <Render/RenderSystem.h>
#include <Render/ImageUtils.h>
#include <Render/Memory/SampledTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem/FrameManager.h>

namespace Editor
{
    GameWidget::GameWidget(const std::string &name) : Widget(name)
    {
    }

    GameWidget::~GameWidget()
    {
    }

    void GameWidget::CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system)
    {
        SDL_GetWindowSizeInPixels(Engine::MainClass::GetInstance()->GetWindow()->GetWindow(), &m_texture_width, &m_texture_height);
        m_color_texture = std::make_shared<Engine::SampledTexture>(*render_system);
        m_depth_texture = std::make_shared<Engine::SampledTexture>(*render_system);
        Engine::Texture::TextureDesc desc{
            .dimensions = 2,
            .width = (uint32_t)m_texture_width,
            .height = (uint32_t)m_texture_height,
            .depth = 1,
            .format = Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB,
            .type = Engine::ImageUtils::ImageType::ColorGeneral,
            .mipmap_levels = 1,
            .array_layers = 1,
            .is_cube_map = false};
        m_color_texture->CreateTextureAndSampler(desc, {}, "Game color attachment");
        desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
        desc.type = Engine::ImageUtils::ImageType::SampledDepthImage;
        m_depth_texture->CreateTextureAndSampler(desc, {}, "Game depth attachment");

        Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
        color_att.image = m_color_texture->GetImage();
        color_att.image_view = m_color_texture->GetImageView();
        color_att.load_op = vk::AttachmentLoadOp::eClear;
        color_att.store_op = vk::AttachmentStoreOp::eStore;
        m_render_target_binding.SetColorAttachment(color_att);
        depth_att.image = m_depth_texture->GetImage();
        depth_att.image_view = m_depth_texture->GetImageView();
        depth_att.load_op = vk::AttachmentLoadOp::eClear;
        depth_att.store_op = vk::AttachmentStoreOp::eDontCare;
        m_render_target_binding.SetDepthAttachment(depth_att);

        m_color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(m_color_texture->GetSampler(), color_att.image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }

    void GameWidget::PreRender()
    {
        auto context = Engine::MainClass::GetInstance()->GetRenderSystem()->GetFrameManager().GetGraphicsContext();
        auto &cb = dynamic_cast<Engine::GraphicsCommandBuffer &>(context.GetCommandBuffer());
        context.UseImage(*m_color_texture, Engine::GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, Engine::GraphicsContext::ImageAccessType::None);
        context.UseImage(*m_depth_texture, Engine::GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite, Engine::GraphicsContext::ImageAccessType::None);
        context.PrepareCommandBuffer();
        cb.BeginRendering(m_render_target_binding, {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y}, "Editor Game Pass");
        Engine::MainClass::GetInstance()->GetRenderSystem()->SetActiveCamera(Engine::MainClass::GetInstance()->GetWorldSystem()->m_active_camera);
        Engine::MainClass::GetInstance()->GetRenderSystem()->DrawMeshes();
        cb.EndRendering();
    }

    void GameWidget::Render()
    {
        if (ImGui::Begin(m_name.c_str()))
        {
            ImGui::Image(m_color_att_id, m_viewport_size, ImVec2(0, 0), ImVec2(m_viewport_size.x / m_texture_width, m_viewport_size.y / m_texture_height));
        }
        ImGui::End();
    }
}
