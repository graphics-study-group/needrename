#include "GameWidget.h"
#include <backends/imgui_impl_vulkan.h>
#include <Render/Memory/Image2D.h>
#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <Render/Pipeline/CommandBuffer/RenderCommandBuffer.h>

namespace Editor
{
    GameWidget::GameWidget(const char *name) : Widget(name)
    {
    }

    GameWidget::~GameWidget()
    {
        vkDestroySampler(Engine::MainClass::GetInstance()->GetRenderSystem()->getDevice(), m_sampler, nullptr);
    }

    void GameWidget::CreateRenderTargetBinding(std::shared_ptr<Engine::RenderSystem> render_system)
    {
        m_color_image = std::make_shared<Engine::AllocatedImage2D>(render_system);
        m_color_image->Create(m_game_width, m_game_height, Engine::ImageUtils::ImageType::ColorGeneral, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
        m_depth_image = std::make_shared<Engine::AllocatedImage2D>(render_system);
        m_depth_image->Create(m_game_width, m_game_height, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);
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

    void GameWidget::PreRender(Engine::RenderCommandBuffer &cb)
    {
        cb.BeginRendering(m_render_target_binding, m_color_image->GetExtent());
        Engine::MainClass::GetInstance()->GetRenderSystem()->DrawMeshes();
        cb.EndRendering();
        cb.InsertAttachmentBarrier(Engine::RenderCommandBuffer::AttachmentBarrierType::ColorAttachmentRAW, m_color_image->GetImage());
    }

    void GameWidget::Render()
    {
        if (ImGui::Begin(m_name))
        {
            ImGui::Image(m_color_att_id, ImVec2(m_game_width, m_game_height));
        }
        ImGui::End();
    }
}
