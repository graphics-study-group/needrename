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

    std::pair<int, int> SDLWindow::GetSize() const {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return {w, h};
    }

    SDL_Window *SDLWindow::GetWindow() {
        return m_window;
    }
} // namespace Engine
