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
        m_color_image = std::make_shared<Engine::AllocatedImage2D>(render_system);
        m_color_image->Create(w, h, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
        Engine::AttachmentUtils::AttachmentDescription color_att;
        color_att.image = m_color_image->GetImage();
        color_att.image_view = m_color_image->GetImageView();
        color_att.load_op = vk::AttachmentLoadOp::eClear;
        color_att.store_op = vk::AttachmentStoreOp::eStore;
        m_render_target_binding.SetColorAttachment(color_att);
        m_render_target_binding.SetExtent(vk::Extent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)});
    }

    const RenderTargetBinding &SDLWindow::GetRenderTargetBinding() const
    {
        return m_render_target_binding;
    }

    SDL_Window *SDLWindow::GetWindow()
    {
        return m_window;
    }
} // namespace Engine
