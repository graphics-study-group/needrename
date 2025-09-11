#include "SDLWindow.h"

#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags) {
        m_window = SDL_CreateWindow(title, width, height, flags);
        if (!m_window) throw std::runtime_error("Cannot create SDL window.");
    }

    SDLWindow::~SDLWindow() {
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void SDLWindow::CreateRenderTargets(std::shared_ptr<RenderSystem> render_system) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);

        RenderTargetTexture::RenderTargetTextureDesc desc{
            .dimensions = 2,
            .width = (uint32_t)w,
            .height = (uint32_t)h,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::R8G8B8A8UNorm,
            .multisample = 1,
            .is_cube_map = false
        };

        m_color_texture = RenderTargetTexture::CreateUnique(*render_system, desc, ImageUtils::SamplerDesc{}, "Color attachment");
        desc.format = RenderTargetTexture::RTTFormat::D32SFLOAT;
        m_depth_texture = RenderTargetTexture::CreateUnique(*render_system, desc, ImageUtils::SamplerDesc{}, "Depth attachment");
    }

    vk::Extent2D SDLWindow::GetExtent() const {
        return {m_color_texture->GetTextureDescription().width, m_color_texture->GetTextureDescription().height};
    }

    const RenderTargetTexture &SDLWindow::GetColorTexture() const noexcept {
        return *m_color_texture;
    }

    const RenderTargetTexture &SDLWindow::GetDepthTexture() const noexcept {
        return *m_depth_texture;
    }

    SDL_Window *SDLWindow::GetWindow() {
        return m_window;
    }
} // namespace Engine
