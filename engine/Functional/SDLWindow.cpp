#include "SDLWindow.h"
#include "Exception/exception.h"
#include <MainClass.h>
#include <Render/Memory/Texture.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags) {
        m_window = SDL_CreateWindow(title, width, height, flags);
        if (!m_window) throw Exception::SDLExceptions::cant_create_window();
    }

    SDLWindow::~SDLWindow() {
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void SDLWindow::CreateRenderTargetBinding(std::shared_ptr<RenderSystem> render_system) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);

        m_color_texture = std::make_shared<Texture>(*render_system);
        m_depth_texture = std::make_shared<Texture>(*render_system);
        Engine::Texture::TextureDesc desc{
            .dimensions = 2,
            .width = (uint32_t)w,
            .height = (uint32_t)h,
            .depth = 1,
            .format = Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm,
            .type = Engine::ImageUtils::ImageType::ColorAttachment,
            .mipmap_levels = 1,
            .array_layers = 1,
            .is_cube_map = false
        };
        m_color_texture->CreateTexture(desc, "Color attachment");
        desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
        desc.type = Engine::ImageUtils::ImageType::DepthImage;
        m_depth_texture->CreateTexture(desc, "Depth attachment");

        AttachmentUtils::AttachmentDescription color_att, depth_att;
        color_att.texture = m_color_texture.get();
        color_att.texture_view = nullptr;
        color_att.load_op = AttachmentUtils::LoadOperation::Clear;
        color_att.store_op = AttachmentUtils::StoreOperation::Store;
        m_render_target_binding.SetColorAttachment(color_att);
        depth_att.texture = m_depth_texture.get();
        depth_att.texture_view = nullptr;
        depth_att.load_op = AttachmentUtils::LoadOperation::Clear;
        depth_att.store_op = AttachmentUtils::StoreOperation::DontCare;
        m_render_target_binding.SetDepthAttachment(depth_att);
    }

    const RenderTargetBinding &SDLWindow::GetRenderTargetBinding() const {
        return m_render_target_binding;
    }

    vk::Extent2D SDLWindow::GetExtent() const {
        return {m_color_texture->GetTextureDescription().width, m_color_texture->GetTextureDescription().height};
    }

    const Texture &SDLWindow::GetColorTexture() const noexcept {
        return *m_color_texture;
    }

    const Texture &SDLWindow::GetDepthTexture() const noexcept {
        return *m_depth_texture;
    }

    SDL_Window *SDLWindow::GetWindow() {
        return m_window;
    }
} // namespace Engine
