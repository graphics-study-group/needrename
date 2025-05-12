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

    std::shared_ptr<WindowAttachmentDescription> SDLWindow::GetAttachmentDescription()
    {
        if (!m_attachment_description)
        {
            m_color_image = std::make_shared<Engine::AllocatedImage2D>(MainClass::GetInstance()->GetRenderSystem());
            m_depth_image = std::make_shared<Engine::AllocatedImage2D>(MainClass::GetInstance()->GetRenderSystem());
            int w, h;
            SDL_GetWindowSizeInPixels(m_window, &w, &h);
            m_color_image->Create(w, h, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
            m_depth_image->Create(w, h, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);
            m_attachment_description = std::make_shared<WindowAttachmentDescription>();
            m_attachment_description->color.image = m_color_image->GetImage();
            m_attachment_description->color.image_view = m_color_image->GetImageView();
            m_attachment_description->color.load_op = vk::AttachmentLoadOp::eClear;
            m_attachment_description->color.store_op = vk::AttachmentStoreOp::eStore;
            m_attachment_description->depth.image = m_depth_image->GetImage();
            m_attachment_description->depth.image_view = m_depth_image->GetImageView();
            m_attachment_description->depth.load_op = vk::AttachmentLoadOp::eClear;
            m_attachment_description->depth.store_op = vk::AttachmentStoreOp::eDontCare;
            m_attachment_description->extent = vk::Extent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        }
        return m_attachment_description;
    }

    SDL_Window *SDLWindow::GetWindow()
    {
        return m_window;
    }
} // namespace Engine
