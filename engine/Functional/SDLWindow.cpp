#include "SDLWindow.h"
#include "Exception/exception.h"
#include <Render/Memory/Image2D.h>
#include <MainClass.h>

namespace Engine
{
    SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags)
    {
        m_window = SDL_CreateWindow(title, width, height, flags);
        if (!m_window)
            throw Exception::SDLExceptions::cant_create_window();
    }

    SDLWindow::~SDLWindow()
    {
        if (m_window)
        {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void SDLWindow::CreateRenderTargetBinding(std::shared_ptr<RenderSystem> render_system)
    {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        m_color_image = std::make_shared<AllocatedImage2D>(render_system);
        m_color_image->Create(w, h, ImageUtils::ImageType::ColorAttachment, ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
        m_depth_image = std::make_shared<AllocatedImage2D>(render_system);
        m_depth_image->Create(w, h, ImageUtils::ImageType::DepthImage, ImageUtils::ImageFormat::D32SFLOAT, 1);
        AttachmentUtils::AttachmentDescription color_att, depth_att;
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
    }

    const RenderTargetBinding &SDLWindow::GetRenderTargetBinding() const
    {
        return m_render_target_binding;
    }

    vk::Extent2D SDLWindow::GetExtent() const
    {
        return m_color_image->GetExtent();
    }

    SDL_Window *SDLWindow::GetWindow()
    {
        return m_window;
    }
} // namespace Engine
