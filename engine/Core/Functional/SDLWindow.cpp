#include "SDLWindow.h"

#include <stdexcept>
#include <string>

namespace Engine {
    SDLWindow::SDLWindow(const char *title, int width, int height, Uint32 flags) {
        m_window = SDL_CreateWindow(title, width, height, flags);
        if (!m_window) {
            // SDL knows why, and a caller that only sees "cannot create" has no next step.
            throw std::runtime_error(std::string{"Cannot create SDL window: "} + SDL_GetError());
        }
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

