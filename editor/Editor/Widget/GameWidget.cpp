#include "GameWidget.h"

#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>

#include <Framework/world/WorldSystem.h>
#include <Functional/SDLWindow.h>
#include <MainClass.h>
#include <Render/Memory/SampledTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <backends/imgui_impl_vulkan.h>

namespace Editor {
    GameWidget::GameWidget(const std::string &name) : Widget(name) {
    }

    GameWidget::~GameWidget() {
    }

    void GameWidget::CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system) {
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
        m_color_texture->CreateTextureAndSampler(desc, {}, "Game color attachment");
        desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
        desc.type = Engine::ImageUtils::ImageType::SampledDepthImage;
        m_depth_texture->CreateTextureAndSampler(desc, {}, "Game depth attachment");

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

    void GameWidget::PreRender() {
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
            m_render_target_binding, {(uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y}, "Editor Game Pass"
        );
        Engine::MainClass::GetInstance()->GetRenderSystem()->SetActiveCamera(
            Engine::MainClass::GetInstance()->GetWorldSystem()->m_active_camera
        );
        cb.DrawRenderers(
            Engine::MainClass::GetInstance()->GetRenderSystem()->GetRendererManager().FilterAndSortRenderers({}), 0
        );
        cb.EndRendering();
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
